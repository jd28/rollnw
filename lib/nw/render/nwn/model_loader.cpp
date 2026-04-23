#include "model_loader.hpp"
#include "nwn_animation.hpp"

#include <nw/render/animation_backend.hpp>

#include <nw/formats/Image.hpp>
#include <nw/formats/Txi.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/ModelCache.hpp>
#include <nw/log.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/util/string.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <unordered_map>

namespace nw::render::nwn {

namespace nwm = nw::model;

namespace {

float g_dangly_debug_scale = 1.0f;
DanglyMode g_dangly_mode = DanglyMode::legacy;

Vertex convert_vertex(const nwm::Vertex& vertex)
{
    return Vertex{
        .position = vertex.position,
        .normal = vertex.normal,
        .texcoord = vertex.tex_coords,
        .tangent = vertex.tangent,
    };
}

void expand_bounds(Bounds& bounds, const glm::vec3& position, bool first)
{
    if (first) {
        bounds.min = position;
        bounds.max = position;
        return;
    }

    bounds.min = glm::min(bounds.min, position);
    bounds.max = glm::max(bounds.max, position);
}

glm::vec3 transform_point(const glm::mat4& transform, const glm::vec3& point)
{
    return glm::vec3{transform * glm::vec4{point, 1.0f}};
}

glm::vec3 transform_vector(const glm::mat4& transform, const glm::vec3& value)
{
    return glm::vec3{transform * glm::vec4{value, 0.0f}};
}

float mesh_alpha_value(const nwm::TrimeshNode* node)
{
    if (!node) { return 1.0f; }
    auto value = node->get_controller(nwm::ControllerType::Alpha);
    if (value.key && !value.data.empty()) {
        return value.data[0];
    }
    return 1.0f;
}

glm::vec3 rotate_axis_angle(const glm::vec3& value, const glm::vec3& axis, float angle)
{
    const float s = std::sin(angle);
    const float c = std::cos(angle);
    return value * c + glm::cross(axis, value) * s + axis * glm::dot(axis, value) * (1.0f - c);
}

bool finite_vec3(const glm::vec3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

void recompute_static_vertex_normals(const nwm::TrimeshNode* node, std::vector<Vertex>& vertices)
{
    if (!node || vertices.size() != node->vertices.size() || node->indices.empty()) {
        return;
    }

    for (auto& vertex : vertices) {
        vertex.normal = glm::vec3{0.0f};
    }

    for (size_t i = 0; i + 2 < node->indices.size(); i += 3) {
        const auto i0 = static_cast<size_t>(node->indices[i]);
        const auto i1 = static_cast<size_t>(node->indices[i + 1]);
        const auto i2 = static_cast<size_t>(node->indices[i + 2]);
        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
            continue;
        }

        const auto& p0 = vertices[i0].position;
        const auto& p1 = vertices[i1].position;
        const auto& p2 = vertices[i2].position;
        const auto face_normal = glm::cross(p1 - p0, p2 - p0);
        if (glm::length2(face_normal) < 1.0e-10f || !finite_vec3(face_normal)) {
            continue;
        }

        vertices[i0].normal += face_normal;
        vertices[i1].normal += face_normal;
        vertices[i2].normal += face_normal;
    }

    for (auto& vertex : vertices) {
        if (glm::length2(vertex.normal) > 1.0e-10f && finite_vec3(vertex.normal)) {
            vertex.normal = glm::normalize(vertex.normal);
        } else {
            vertex.normal = glm::vec3{0.0f, 0.0f, 1.0f};
        }
    }
}

template <typename TVertex>
void recompute_vertex_tangents(const nwm::TrimeshNode* node, std::vector<TVertex>& vertices)
{
    if (!node || vertices.size() != node->vertices.size() || node->indices.empty()) {
        return;
    }

    std::vector<glm::vec3> tan1(vertices.size(), glm::vec3{0.0f});
    std::vector<glm::vec3> tan2(vertices.size(), glm::vec3{0.0f});

    for (size_t i = 0; i + 2 < node->indices.size(); i += 3) {
        const auto i0 = static_cast<size_t>(node->indices[i]);
        const auto i1 = static_cast<size_t>(node->indices[i + 1]);
        const auto i2 = static_cast<size_t>(node->indices[i + 2]);
        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
            continue;
        }

        const auto& v0 = vertices[i0];
        const auto& v1 = vertices[i1];
        const auto& v2 = vertices[i2];

        const float x1 = v1.position.x - v0.position.x;
        const float x2 = v2.position.x - v0.position.x;
        const float y1 = v1.position.y - v0.position.y;
        const float y2 = v2.position.y - v0.position.y;
        const float z1 = v1.position.z - v0.position.z;
        const float z2 = v2.position.z - v0.position.z;

        const float s1 = v1.texcoord.x - v0.texcoord.x;
        const float s2 = v2.texcoord.x - v0.texcoord.x;
        const float t1 = v1.texcoord.y - v0.texcoord.y;
        const float t2 = v2.texcoord.y - v0.texcoord.y;

        const float denom = s1 * t2 - s2 * t1;
        if (std::abs(denom) < 1.0e-10f) {
            continue;
        }

        const float r = 1.0f / denom;
        const glm::vec3 sdir{(t2 * x1 - t1 * x2) * r, (t2 * y1 - t1 * y2) * r, (t2 * z1 - t1 * z2) * r};
        const glm::vec3 tdir{(s1 * x2 - s2 * x1) * r, (s1 * y2 - s2 * y1) * r, (s1 * z2 - s2 * z1) * r};
        if (!finite_vec3(sdir) || !finite_vec3(tdir)) {
            continue;
        }

        tan1[i0] += sdir;
        tan1[i1] += sdir;
        tan1[i2] += sdir;
        tan2[i0] += tdir;
        tan2[i1] += tdir;
        tan2[i2] += tdir;
    }

    for (size_t i = 0; i < vertices.size(); ++i) {
        const auto& normal = vertices[i].normal;
        const auto& t = tan1[i];
        if (!finite_vec3(normal) || !finite_vec3(t) || glm::length2(normal) < 1.0e-10f || glm::length2(t) < 1.0e-10f) {
            vertices[i].tangent = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f};
            continue;
        }

        const auto orthogonal = t - normal * glm::dot(normal, t);
        if (!finite_vec3(orthogonal) || glm::length2(orthogonal) < 1.0e-10f) {
            vertices[i].tangent = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f};
            continue;
        }

        const auto tangent = glm::normalize(orthogonal);
        const float handedness = (glm::dot(glm::cross(normal, t), tan2[i]) < 0.0f) ? -1.0f : 1.0f;
        vertices[i].tangent = glm::vec4{tangent, handedness};
    }
}

template <typename TVertex>
bool has_invalid_tangents(const std::vector<TVertex>& vertices)
{
    for (const auto& vertex : vertices) {
        if (!std::isfinite(vertex.tangent.x) || !std::isfinite(vertex.tangent.y)
            || !std::isfinite(vertex.tangent.z) || !std::isfinite(vertex.tangent.w)
            || glm::length2(glm::vec3{vertex.tangent}) < 1.0e-10f) {
            return true;
        }
    }
    return false;
}

uint32_t pack_u8x4(const glm::ivec4& values)
{
    return (static_cast<uint32_t>(values.x) & 0xffu)
        | ((static_cast<uint32_t>(values.y) & 0xffu) << 8u)
        | ((static_cast<uint32_t>(values.z) & 0xffu) << 16u)
        | ((static_cast<uint32_t>(values.w) & 0xffu) << 24u);
}

uint32_t pack_unorm8x4(const glm::vec4& values)
{
    auto to_u8 = [](float v) {
        return static_cast<uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return to_u8(values.x)
        | (to_u8(values.y) << 8u)
        | (to_u8(values.z) << 16u)
        | (to_u8(values.w) << 24u);
}

SkinnedVertex convert_vertex(const nwm::SkinVertex& vertex)
{
    return SkinnedVertex{
        .position = vertex.position,
        .normal = vertex.normal,
        .texcoord = vertex.tex_coords,
        .tangent = vertex.tangent,
        .joint_indices = pack_u8x4(vertex.bones),
        .joint_weights = pack_unorm8x4(vertex.weights),
    };
}

float hash_phase(std::string_view name)
{
    uint32_t value = 2166136261u;
    for (unsigned char c : name) {
        value ^= c;
        value *= 16777619u;
    }
    return static_cast<float>(value & 0xffffu) / 65535.0f * glm::two_pi<float>();
}

float safe_debug_scale()
{
    return std::max(0.0f, g_dangly_debug_scale);
}

bool use_modern_dangly_mode()
{
    return g_dangly_mode == DanglyMode::modern;
}

std::string ascii_lower(std::string_view value)
{
    std::string result;
    result.reserve(value.size());
    for (unsigned char c : value) {
        result.push_back(static_cast<char>(std::tolower(c)));
    }
    return result;
}

bool looks_like_foliage_mesh(const nwm::DanglymeshNode* node)
{
    if (!node) {
        return false;
    }

    const auto contains_any = [](std::string_view text) {
        return text.find("bush") != std::string::npos
            || text.find("grass") != std::string::npos
            || text.find("leaf") != std::string::npos
            || text.find("vine") != std::string::npos
            || text.find("fern") != std::string::npos;
    };

    return contains_any(ascii_lower(node->name))
        || contains_any(ascii_lower(node->bitmap))
        || contains_any(ascii_lower(node->textures[0]));
}

bool looks_like_foliage_texture(std::string_view texture_name)
{
    const auto lowered = ascii_lower(texture_name);
    return lowered.find("treefol") != std::string::npos
        || lowered.find("leaf") != std::string::npos
        || lowered.find("bush") != std::string::npos
        || lowered.find("fern") != std::string::npos
        || lowered.find("vine") != std::string::npos
        || lowered.find("shrub") != std::string::npos
        || lowered.find("foliage") != std::string::npos;
}

bool looks_like_web_texture(std::string_view texture_name)
{
    const auto lowered = ascii_lower(texture_name);
    return lowered.find("web") != std::string::npos
        || lowered.find("cobweb") != std::string::npos;
}

enum class AlphaProfile {
    opaque,
    binary,
    graded,
};

struct TxiMaterialInfo {
    bool has_txi = false;
    std::string blending;
    float alphamean = 0.5f;
    bool decal = false;
};

struct TextureAnalysis {
    AlphaProfile alpha_profile = AlphaProfile::opaque;
    bool has_color_key = false;
    glm::vec3 color_key{0.0f};
    float color_key_threshold = 0.0f;
    float alpha_coverage_50 = 0.0f;
    float alpha_coverage_75 = 0.0f;
    int width = 0;
    int height = 0;
};

std::string resolve_bitmap_name(const nwm::TrimeshNode* node)
{
    if (!node) {
        return {};
    }
    if (!node->bitmap.empty() && node->bitmap != "null") {
        return std::string(node->bitmap);
    }
    if (!node->textures[0].empty() && node->textures[0] != "null") {
        return std::string(node->textures[0]);
    }
    return {};
}

TxiMaterialInfo load_txi_material_info(std::string_view bitmap_name)
{
    TxiMaterialInfo result;
    if (bitmap_name.empty()) {
        return result;
    }

    auto data = nw::kernel::resman().demand({nw::Resref{bitmap_name}, nw::ResourceType::txi});
    if (data.bytes.size() == 0) {
        return result;
    }

    nw::Txi txi{std::move(data)};
    if (!txi.valid()) {
        return result;
    }

    result.has_txi = true;
    nw::String blending;
    if (txi.get_to("blending", blending)) {
        nw::string::tolower(&blending);
        result.blending = blending;
    }
    txi.get_to("alphamean", result.alphamean);
    int decal = 0;
    if (txi.get_to("decal", decal)) {
        result.decal = decal != 0;
    }
    return result;
}

TextureAnalysis analyze_texture(std::string_view bitmap_name)
{
    static std::unordered_map<std::string, TextureAnalysis> cache;
    if (bitmap_name.empty()) {
        return {};
    }

    if (auto it = cache.find(std::string(bitmap_name)); it != cache.end()) {
        return it->second;
    }

    TextureAnalysis result;
    if (bitmap_name.empty()) {
        return result;
    }

    auto img = std::unique_ptr<nw::Image>{nw::kernel::resman().texture(nw::Resref{bitmap_name})};
    if (!img || !img->valid()) {
        return result;
    }

    result.width = img->width();
    result.height = img->height();

    const size_t pixel_count = static_cast<size_t>(img->width()) * img->height();
    const uint8_t* src = img->data();
    if (img->channels() >= 4) {
        bool has_zero = false;
        bool has_partial = false;
        size_t coverage_50 = 0;
        size_t coverage_75 = 0;
        for (size_t i = 0; i < pixel_count; ++i) {
            const uint8_t alpha = src[i * 4 + 3];
            has_zero |= alpha == 0;
            has_partial |= alpha > 0 && alpha < 255;
            coverage_50 += alpha >= 128;
            coverage_75 += alpha >= 192;
        }

        result.alpha_coverage_50 = static_cast<float>(coverage_50) / static_cast<float>(pixel_count);
        result.alpha_coverage_75 = static_cast<float>(coverage_75) / static_cast<float>(pixel_count);
        if (has_partial) {
            result.alpha_profile = AlphaProfile::graded;
        } else {
            result.alpha_profile = has_zero ? AlphaProfile::binary : AlphaProfile::opaque;
        }
    }

    const auto sample_rgb = [&](size_t index) {
        const size_t base = index * img->channels();
        return glm::vec3{
            src[base + 0] / 255.0f,
            src[base + 1] / 255.0f,
            src[base + 2] / 255.0f,
        };
    };

    if (img->channels() >= 3 && img->width() > 2 && img->height() > 2) {
        const size_t w = img->width();
        const size_t h = img->height();
        const auto c0 = sample_rgb(0);
        const auto c1 = sample_rgb(w - 1);
        const auto c2 = sample_rgb((h - 1) * w);
        const auto c3 = sample_rgb(h * w - 1);
        const auto average = (c0 + c1 + c2 + c3) * 0.25f;
        const float corner_variation = std::max({glm::length(c0 - average), glm::length(c1 - average), glm::length(c2 - average), glm::length(c3 - average)});
        if (corner_variation < 0.08f) {
            size_t foreground_pixels = 0;
            for (size_t i = 0; i < pixel_count; ++i) {
                if (glm::length(sample_rgb(i) - average) > 0.18f) {
                    ++foreground_pixels;
                }
            }

            if (foreground_pixels > pixel_count / 64) {
                result.has_color_key = true;
                result.color_key = average;
                result.color_key_threshold = 0.12f;
            }
        }
    }

    cache[std::string(bitmap_name)] = result;
    return result;
}

template <typename TVertex>
void inset_transparent_subrect_uvs(const Mesh& mesh, std::vector<TVertex>& vertices)
{
    if (mesh.material_mode != MaterialMode::transparent || mesh.transparencyhint <= 0 || vertices.empty()) {
        return;
    }

    float min_u = vertices.front().texcoord.x;
    float max_u = vertices.front().texcoord.x;
    float min_v = vertices.front().texcoord.y;
    float max_v = vertices.front().texcoord.y;
    for (const auto& vertex : vertices) {
        min_u = std::min(min_u, vertex.texcoord.x);
        max_u = std::max(max_u, vertex.texcoord.x);
        min_v = std::min(min_v, vertex.texcoord.y);
        max_v = std::max(max_v, vertex.texcoord.y);
    }

    const float span_u = max_u - min_u;
    const float span_v = max_v - min_v;
    if (span_u <= 1.0e-6f || span_v <= 1.0e-6f) {
        return;
    }

    // Only adjust atlas-like subrects that live inside the normalized texture domain.
    if (min_u < 0.0f || min_v < 0.0f || max_u > 1.0f || max_v > 1.0f) {
        return;
    }
    if (span_u >= 0.98f && span_v >= 0.98f) {
        return;
    }

    const auto texture = analyze_texture(mesh.bitmap_name);
    if (texture.width <= 1 || texture.height <= 1) {
        return;
    }

    const float inset_u = 0.5f / static_cast<float>(texture.width);
    const float inset_v = 0.5f / static_cast<float>(texture.height);
    if (span_u <= inset_u * 2.0f || span_v <= inset_v * 2.0f) {
        return;
    }

    for (auto& vertex : vertices) {
        const float u01 = (vertex.texcoord.x - min_u) / span_u;
        const float v01 = (vertex.texcoord.y - min_v) / span_v;
        vertex.texcoord.x = glm::mix(min_u + inset_u, max_u - inset_u, u01);
        vertex.texcoord.y = glm::mix(min_v + inset_v, max_v - inset_v, v01);
    }
}

MaterialMode classify_material(const nwm::TrimeshNode* node, std::string_view bitmap_name,
    nwm::ModelClass model_class)
{
    const auto texture = analyze_texture(bitmap_name);
    const auto alpha_profile = texture.alpha_profile;
    const bool foliage_texture = looks_like_foliage_texture(bitmap_name);
    const bool web_texture = looks_like_web_texture(bitmap_name);
    const auto txi = load_txi_material_info(bitmap_name);
    if (txi.has_txi) {
        if (txi.blending == "punchthrough") {
            return MaterialMode::cutout;
        }
        if (txi.blending == "lighten") {
            return MaterialMode::cutout;
        }
        if (txi.blending == "additive") {
            return MaterialMode::transparent;
        }
        if (txi.blending == "normal") {
            if (alpha_profile == AlphaProfile::binary || foliage_texture) {
                return MaterialMode::cutout;
            }
            if (alpha_profile == AlphaProfile::graded) {
                return MaterialMode::transparent;
            }
        }
        if (txi.decal && alpha_profile != AlphaProfile::opaque) {
            return MaterialMode::cutout;
        }
    }

    if ((foliage_texture || web_texture) && (alpha_profile != AlphaProfile::opaque || texture.has_color_key)) {
        return MaterialMode::cutout;
    }

    if (node) {
        if (mesh_alpha_value(node) < 0.999f) {
            return MaterialMode::transparent;
        }
        const auto renderhint = ascii_lower(node->renderhint);
        const auto materialname = ascii_lower(node->materialname);
        if (renderhint.find("additive") != std::string::npos
            || renderhint.find("trans") != std::string::npos
            || materialname.find("add") != std::string::npos) {
            return MaterialMode::transparent;
        }
        if (node->transparencyhint > 0) {
            switch (alpha_profile) {
            case AlphaProfile::binary:
                return MaterialMode::cutout;
            case AlphaProfile::graded:
                return MaterialMode::transparent;
            case AlphaProfile::opaque:
            default:
                return MaterialMode::opaque;
            }
        }
    }

    switch (alpha_profile) {
    case AlphaProfile::binary:
        return MaterialMode::cutout;
    case AlphaProfile::graded:
        switch (model_class) {
        case nwm::ModelClass::tile:
        case nwm::ModelClass::effect:
        case nwm::ModelClass::gui:
            return MaterialMode::transparent;
        case nwm::ModelClass::invalid:
        case nwm::ModelClass::character:
        case nwm::ModelClass::door:
        case nwm::ModelClass::item:
        default:
            return MaterialMode::opaque;
        }
    case AlphaProfile::opaque:
    default:
        return MaterialMode::opaque;
    }
}

void initialize_mesh_material(Mesh& mesh, const nwm::TrimeshNode* node, nwm::ModelClass model_class)
{
    if (!node) {
        return;
    }

    mesh.bitmap_name = resolve_bitmap_name(node);
    mesh.renderhint = std::string(node->renderhint);
    mesh.materialname = std::string(node->materialname);
    mesh.transparencyhint = node->transparencyhint;
    mesh.opacity = mesh_alpha_value(node);
    mesh.is_ground_overlay = false;
    const auto texture = analyze_texture(mesh.bitmap_name);
    const auto txi = load_txi_material_info(mesh.bitmap_name);
    mesh.alpha_cutout_threshold = 0.5f;
    if (txi.has_txi && txi.alphamean > 0.0f && txi.alphamean < 1.0f) {
        mesh.alpha_cutout_threshold = txi.alphamean;
    } else if (looks_like_foliage_texture(mesh.bitmap_name)
        && texture.alpha_profile == AlphaProfile::graded
        && texture.alpha_coverage_50 - texture.alpha_coverage_75 < 0.03f) {
        // Mostly-binary foliage alpha benefits from a stronger cutout threshold.
        mesh.alpha_cutout_threshold = 0.75f;
    }
    if (txi.blending == "lighten") {
        mesh.color_key = glm::vec3(0.0f);
        mesh.color_key_threshold = 0.18f;
    } else if (texture.alpha_profile == AlphaProfile::opaque) {
        mesh.color_key = texture.color_key;
        mesh.color_key_threshold = texture.color_key_threshold;
    }
    mesh.material_mode = classify_material(node, mesh.bitmap_name, model_class);

    glm::vec3 min_v{std::numeric_limits<float>::max()};
    glm::vec3 max_v{std::numeric_limits<float>::lowest()};
    if (auto skin = dynamic_cast<const nwm::SkinNode*>(node)) {
        for (const auto& vertex : skin->vertices) {
            min_v = glm::min(min_v, vertex.position);
            max_v = glm::max(max_v, vertex.position);
        }
    } else {
        for (const auto& vertex : node->vertices) {
            min_v = glm::min(min_v, vertex.position);
            max_v = glm::max(max_v, vertex.position);
        }
    }
    const glm::vec3 span = max_v - min_v;
    const float horizontal_span = std::max(span.x, span.y);
    mesh.is_ground_overlay = mesh.material_mode == MaterialMode::transparent
        && horizontal_span >= 0.5f
        && span.z <= 0.2f
        && span.z <= horizontal_span * 0.1f;
}

size_t vertex_count(const nwm::TrimeshNode* node)
{
    if (auto skin = dynamic_cast<const nwm::SkinNode*>(node)) {
        return skin->vertices.size();
    }
    return node ? node->vertices.size() : 0;
}

void for_each_vertex_position(const nwm::TrimeshNode* node, const std::function<void(const glm::vec3&)>& fn)
{
    if (!node) {
        return;
    }
    if (auto skin = dynamic_cast<const nwm::SkinNode*>(node)) {
        for (const auto& vertex : skin->vertices) {
            fn(vertex.position);
        }
        return;
    }
    for (const auto& vertex : node->vertices) {
        fn(vertex.position);
    }
}

} // namespace

// ============================================================================
// Node
// ============================================================================

glm::mat4 Node::get_transform() const
{
    // Walk up parent chain to get world transform
    glm::mat4 parent = glm::mat4{1.0f};
    if (!has_transform_) { return parent; }
    if (parent_) {
        parent = parent_->get_transform();
    }

    return parent * get_local_transform();
}

glm::mat4 Node::get_local_transform() const
{
    auto trans = glm::translate(glm::mat4{1.0f}, position_);
    trans = trans * glm::toMat4(rotation_);
    return glm::scale(trans, scale_);
}

// ============================================================================
// Mesh
// ============================================================================

Mesh::~Mesh()
{
    if (vertices.valid()) {
        nw::gfx::destroy_buffer(vertices);
    }
    if (indices.valid()) {
        nw::gfx::destroy_buffer(indices);
    }
}

void SkinMesh::initialize_skinning()
{
    for (auto& bind : inverse_bind_pose_) {
        bind = glm::mat4(1.0f);
    }

    for (size_t i = 0; i < bone_nodes.size(); ++i) {
        auto* bone = owner_ ? owner_->node_from_source_index(bone_nodes[i]) : nullptr;
        if (!bone) {
            continue;
        }
        inverse_bind_pose_[i] = glm::inverse(bone->bind_pose_) * bind_pose_;
    }
}

void SkinMesh::fill_skin_matrices(glm::mat4* out, size_t count) const
{
    if (!out) {
        return;
    }

    const auto limit = std::min(count, bone_nodes.size());
    for (size_t i = 0; i < limit; ++i) {
        auto* bone = owner_ ? owner_->node_from_source_index(bone_nodes[i]) : nullptr;
        out[i] = bone ? bone->get_transform() * inverse_bind_pose_[i] : glm::mat4(1.0f);
    }
}

void DanglyMesh::initialize_dangly(const nwm::DanglymeshNode* node)
{
    cpu_vertices_.clear();
    rest_positions_.clear();
    offsets_.clear();
    velocities_.clear();
    freedom_.clear();

    if (!node) {
        return;
    }

    displacement_ = std::max(0.0f, node->displacement);
    period_ = std::max(0.0f, node->period);
    tightness_ = std::max(0.0f, node->tightness);
    phase_ = hash_phase(node->name);

    cpu_vertices_.reserve(node->vertices.size());
    rest_positions_.reserve(node->vertices.size());
    offsets_.assign(node->vertices.size(), glm::vec3{0.0f});
    velocities_.assign(node->vertices.size(), glm::vec3{0.0f});

    for (const auto& vertex : node->vertices) {
        cpu_vertices_.push_back(convert_vertex(vertex));
        rest_positions_.push_back(vertex.position);
    }

    const bool valid_constraints = node->constraints.size() == node->vertices.size();
    if (!valid_constraints && !node->constraints.empty()) {
        LOG_F(WARNING,
            "Dangly mesh {} has {} constraints for {} vertices; pinning mesh",
            node->name,
            node->constraints.size(),
            node->vertices.size());
    }

    freedom_.reserve(node->vertices.size());
    for (size_t i = 0; i < node->vertices.size(); ++i) {
        if (!valid_constraints) {
            freedom_.push_back(0.0f);
            continue;
        }
        freedom_.push_back(std::clamp(node->constraints[i] / 255.0f, 0.0f, 1.0f));
    }

    glm::vec3 pinned_sum{0.0f};
    glm::vec3 loose_sum{0.0f};
    size_t pinned_count = 0;
    size_t loose_count = 0;
    for (size_t i = 0; i < rest_positions_.size(); ++i) {
        if (freedom_[i] <= 0.05f) {
            pinned_sum += rest_positions_[i];
            ++pinned_count;
        }
        if (freedom_[i] > 0.05f) {
            loose_sum += rest_positions_[i];
            ++loose_count;
        }
    }
    if (pinned_count > 0) {
        pinned_center_ = pinned_sum / static_cast<float>(pinned_count);
    }
    if (loose_count > 0) {
        loose_center_ = loose_sum / static_cast<float>(loose_count);
    } else {
        loose_center_ = pinned_center_;
    }

    this->is_foliage_like_ = looks_like_foliage_mesh(node);
    foliage_pivot_ = pinned_center_;
    glm::vec3 axis = loose_center_ - pinned_center_;

    if (this->is_foliage_like_ && !rest_positions_.empty()) {
        if (rest_positions_.size() == 3) {
            std::array<size_t, 3> order = {0, 1, 2};
            std::sort(order.begin(), order.end(), [&](size_t lhs, size_t rhs) {
                return rest_positions_[lhs].z < rest_positions_[rhs].z;
            });

            const auto& root_a = rest_positions_[order[0]];
            const auto& root_b = rest_positions_[order[1]];
            const auto& tip = rest_positions_[order[2]];
            foliage_pivot_ = (root_a + root_b) * 0.5f;
            axis = tip - foliage_pivot_;
        } else {
            size_t root_index = 0;
            for (size_t i = 1; i < rest_positions_.size(); ++i) {
                if (rest_positions_[i].z < rest_positions_[root_index].z) {
                    root_index = i;
                }
            }

            foliage_pivot_ = rest_positions_[root_index];
            glm::vec3 tip_center{0.0f};
            size_t tip_count = 0;
            for (size_t i = 0; i < rest_positions_.size(); ++i) {
                if (i == root_index) {
                    continue;
                }
                tip_center += rest_positions_[i];
                ++tip_count;
            }
            if (tip_count > 0) {
                tip_center /= static_cast<float>(tip_count);
                axis = tip_center - foliage_pivot_;
            }
        }
    }

    if (glm::length2(axis) < 1.0e-6f) {
        axis = glm::vec3{0.0f, 0.0f, 1.0f};
    }
    foliage_extent_ = std::max(glm::length(axis), 0.05f);
    foliage_axis_ = glm::normalize(axis);
    foliage_amplitude_ = std::clamp(foliage_extent_ * 0.45f, 0.12f, 0.35f);

    if (!freedom_.empty()) {
        float min_freedom = freedom_.front();
        float max_freedom = freedom_.front();
        float total_freedom = 0.0f;
        for (float value : freedom_) {
            min_freedom = std::min(min_freedom, value);
            max_freedom = std::max(max_freedom, value);
            total_freedom += value;
        }
        LOG_F(INFO,
            "Dangly mesh {}: displacement={} period={} tightness={} freedom[min={}, avg={}, max={}] foliage={} amp={}",
            node->name,
            displacement_,
            period_,
            tightness_,
            min_freedom,
            total_freedom / static_cast<float>(freedom_.size()),
            max_freedom,
            this->is_foliage_like_ ? "yes" : "no",
            foliage_amplitude_);
    }

    if (this->is_foliage_like_ && cpu_vertices_.size() == 3) {
        float min_u = cpu_vertices_[0].texcoord.x;
        float max_u = cpu_vertices_[0].texcoord.x;
        for (const auto& vertex : cpu_vertices_) {
            min_u = std::min(min_u, vertex.texcoord.x);
            max_u = std::max(max_u, vertex.texcoord.x);
        }

        // Some NWN1 bush cards appear to reference the same cutout with a +0.5 U
        // offset and flipped V relative to their mirrored siblings.
        if (max_u > 1.25f && min_u >= 0.5f) {
            for (auto& vertex : cpu_vertices_) {
                vertex.texcoord.x -= 0.5f;
                vertex.texcoord.y = 1.0f - vertex.texcoord.y;
            }
        }
    }
}

void DanglyMesh::update_dangly(const glm::mat4& world_transform, int32_t dt_ms)
{
    if (cpu_vertices_.empty() || rest_positions_.size() != cpu_vertices_.size()) {
        return;
    }

    if (this->is_foliage_like_) {
        if (use_modern_dangly_mode()) {
            update_modern_foliage(world_transform, dt_ms);
        } else {
            update_legacy_foliage(world_transform, dt_ms);
        }
        return;
    }

    const auto world_origin = transform_point(world_transform, glm::vec3{0.0f});
    if (!has_previous_world_origin_) {
        previous_world_origin_ = world_origin;
        has_previous_world_origin_ = true;
    }

    if (dt_ms <= 0 || displacement_ <= 0.0f || period_ <= 0.0f) {
        previous_world_origin_ = world_origin;
        upload_vertices();
        return;
    }

    const float dt = std::min(static_cast<float>(dt_ms) / 1000.0f, 0.05f);
    const float debug_scale = safe_debug_scale();
    time_ += dt;

    const auto inverse_world = glm::inverse(world_transform);
    const glm::vec3 breeze_world = glm::normalize(glm::vec3{0.9f, 0.25f, 0.08f});
    glm::vec3 breeze_local = transform_vector(inverse_world, breeze_world);
    if (glm::length2(breeze_local) < 1.0e-6f) {
        breeze_local = glm::vec3{1.0f, 0.0f, 0.0f};
    } else {
        breeze_local = glm::normalize(breeze_local);
    }

    glm::vec3 flutter_local = glm::cross(breeze_local, glm::vec3{0.0f, 0.0f, 1.0f});
    if (glm::length2(flutter_local) < 1.0e-6f) {
        flutter_local = glm::vec3{0.0f, 1.0f, 0.0f};
    } else {
        flutter_local = glm::normalize(flutter_local);
    }

    const glm::vec3 motion_delta_world = world_origin - previous_world_origin_;
    glm::vec3 inertia_local = transform_vector(inverse_world, -motion_delta_world);
    if (glm::length2(inertia_local) > 1.0e-6f) {
        inertia_local = glm::normalize(inertia_local);
    }

    const float gust = 0.5f + 0.5f * std::sin(time_ * 0.8f + phase_);
    const float pulse = std::sin(time_ * (1.2f + std::min(period_, 20.0f) * 0.045f) + phase_);
    const float flutter = std::sin(time_ * 2.4f + phase_ * 1.7f);
    const float wind_strength = 0.35f + 0.65f * gust;
    const float inertia_strength = glm::min(glm::length(motion_delta_world) * 12.0f, 0.4f);
    const float stiffness = 8.5f + tightness_ * 0.8f + period_ * 0.18f;
    const float damping = 4.0f + tightness_ * 0.6f;
    glm::vec3 mesh_axis = loose_center_ - pinned_center_;
    if (glm::length2(mesh_axis) < 1.0e-6f) {
        mesh_axis = glm::vec3{0.0f, 0.0f, 1.0f};
    } else {
        mesh_axis = glm::normalize(mesh_axis);
    }

    for (size_t i = 0; i < cpu_vertices_.size(); ++i) {
        const float freedom = i < freedom_.size() ? freedom_[i] : 0.0f;
        if (freedom <= 0.0f) {
            offsets_[i] = glm::vec3{0.0f};
            velocities_[i] = glm::vec3{0.0f};
            cpu_vertices_[i].position = rest_positions_[i];
            continue;
        }

        const float effective_freedom = std::sqrt(freedom);
        const float max_offset = displacement_ * freedom * debug_scale;
        const glm::vec3& rest = rest_positions_[i];
        const float vertex_phase = rest.x * 0.35f + rest.y * 0.2f + rest.z * 0.1f;
        glm::vec3 root_dir = rest - pinned_center_;
        if (glm::length2(root_dir) < 1.0e-6f) {
            root_dir = mesh_axis;
        } else {
            root_dir = glm::normalize(root_dir);
        }

        glm::vec3 sway_dir = breeze_local;
        if (glm::length2(inertia_local) > 1.0e-6f) {
            sway_dir += inertia_local * 0.8f;
        }
        sway_dir += root_dir * (0.2f * glm::max(0.0f, glm::dot(root_dir, breeze_local)));
        if (glm::length2(sway_dir) < 1.0e-6f) {
            sway_dir = breeze_local;
        } else {
            sway_dir = glm::normalize(sway_dir);
        }

        const float bend = wind_strength * (0.65f + 0.35f * pulse * std::sin(phase_ + vertex_phase));
        const float flutter_amount = 0.08f * flutter * effective_freedom * std::sin(vertex_phase + phase_ * 0.5f);

        glm::vec3 target = sway_dir * (bend * max_offset);
        target += flutter_local * (flutter_amount * max_offset);
        target += inertia_local * (inertia_strength * max_offset);

        // Keep dangly motion on the outward side of the rooted rest direction so
        // head-attached meshes like hair do not fold back through the face.
        const float inward_component = glm::dot(target, root_dir);
        if (inward_component < 0.0f) {
            target -= root_dir * inward_component;
        }

        glm::vec3 accel = (target - offsets_[i]) * stiffness - velocities_[i] * damping;
        velocities_[i] += accel * dt;
        offsets_[i] += velocities_[i] * dt;

        const float inward_offset = glm::dot(offsets_[i], root_dir);
        if (inward_offset < 0.0f) {
            offsets_[i] -= root_dir * inward_offset;
            velocities_[i] -= root_dir * glm::dot(velocities_[i], root_dir);
        }

        const float offset_len = glm::length(offsets_[i]);
        if (offset_len > max_offset && offset_len > 0.0f) {
            offsets_[i] *= max_offset / offset_len;
            velocities_[i] *= 0.5f;
        }

        cpu_vertices_[i].position = rest + offsets_[i];
    }

    previous_world_origin_ = world_origin;
    recompute_normals();
    upload_vertices();
}

void DanglyMesh::update_legacy_foliage(const glm::mat4& world_transform, int32_t dt_ms)
{
    if (cpu_vertices_.empty() || rest_positions_.size() != cpu_vertices_.size()) {
        return;
    }

    const float dt = std::min(std::max(static_cast<float>(dt_ms), 0.0f) / 1000.0f, 0.05f);
    time_ += dt;

    const auto inverse_world = glm::inverse(world_transform);
    glm::vec3 breeze_local = transform_vector(inverse_world, glm::normalize(glm::vec3{0.95f, 0.18f, 0.04f}));
    if (glm::length2(breeze_local) < 1.0e-6f) {
        breeze_local = glm::vec3{1.0f, 0.0f, 0.0f};
    } else {
        breeze_local = glm::normalize(breeze_local);
    }

    glm::vec3 bend_axis = glm::cross(foliage_axis_, breeze_local);
    if (glm::length2(bend_axis) < 1.0e-6f) {
        bend_axis = glm::vec3{0.0f, 1.0f, 0.0f};
    } else {
        bend_axis = glm::normalize(bend_axis);
    }

    const float gust = 0.6f + 0.4f * std::sin(time_ * 0.8f + phase_);
    const float sway = std::sin(time_ * 1.35f + phase_);
    const float flutter = std::sin(time_ * 2.6f + phase_ * 0.7f);
    const float max_angle = 0.22f * dangly_debug_scale() * (0.75f + 0.25f * gust);

    const bool rigid_card = cpu_vertices_.size() == 3;
    const float rigid_angle = max_angle * sway + 0.03f * flutter;

    for (size_t i = 0; i < cpu_vertices_.size(); ++i) {
        const glm::vec3 local = rest_positions_[i] - foliage_pivot_;
        const float height = std::clamp(glm::dot(local, foliage_axis_) / std::max(foliage_extent_, 0.001f), 0.0f, 1.0f);
        const float freedom = i < freedom_.size() ? freedom_[i] : height;
        const float angle = rigid_card
            ? rigid_angle
            : (max_angle * sway * std::max(height, freedom) + 0.03f * flutter * height);
        const glm::vec3 rotated = rotate_axis_angle(local, bend_axis, angle);
        cpu_vertices_[i].position = foliage_pivot_ + rotated;
    }

    recompute_normals();
    upload_vertices();
}

void DanglyMesh::update_modern_foliage(const glm::mat4& world_transform, int32_t dt_ms)
{
    if (cpu_vertices_.empty() || rest_positions_.size() != cpu_vertices_.size()) {
        return;
    }

    const float dt = std::min(std::max(static_cast<float>(dt_ms), 0.0f) / 1000.0f, 0.05f);
    time_ += dt;

    const auto inverse_world = glm::inverse(world_transform);
    glm::vec3 breeze_local = transform_vector(inverse_world, glm::normalize(glm::vec3{0.88f, 0.26f, 0.06f}));
    if (glm::length2(breeze_local) < 1.0e-6f) {
        breeze_local = glm::vec3{1.0f, 0.0f, 0.0f};
    } else {
        breeze_local = glm::normalize(breeze_local);
    }

    glm::vec3 lift_local = glm::normalize(glm::vec3{0.0f, 0.0f, 1.0f} + breeze_local * 0.15f);
    glm::vec3 bend_axis = glm::cross(foliage_axis_, breeze_local);
    if (glm::length2(bend_axis) < 1.0e-6f) {
        bend_axis = glm::vec3{0.0f, 1.0f, 0.0f};
    } else {
        bend_axis = glm::normalize(bend_axis);
    }

    const float gust = 0.55f + 0.45f * std::sin(time_ * 0.62f + phase_);
    const float swell = std::sin(time_ * 0.95f + phase_ * 0.7f);
    const float flutter = std::sin(time_ * 2.8f + phase_ * 1.3f);
    const float max_angle = 0.34f * dangly_debug_scale() * (0.8f + 0.2f * gust);
    const bool rigid_card = cpu_vertices_.size() == 3;

    for (size_t i = 0; i < cpu_vertices_.size(); ++i) {
        const glm::vec3 local = rest_positions_[i] - foliage_pivot_;
        const float height = std::clamp(glm::dot(local, foliage_axis_) / std::max(foliage_extent_, 0.001f), 0.0f, 1.0f);
        const float lateral_phase = local.x * 0.31f + local.y * 0.19f + phase_;
        const float wave = swell + 0.35f * std::sin(lateral_phase + time_ * 1.1f);
        const float angle = rigid_card
            ? max_angle * wave
            : max_angle * wave * (0.35f + 0.65f * height);

        glm::vec3 rotated = rotate_axis_angle(local, bend_axis, angle);
        rotated += lift_local * (0.025f * flutter * height * foliage_extent_);
        cpu_vertices_[i].position = foliage_pivot_ + rotated;
    }

    recompute_normals();
    upload_vertices();
}

void DanglyMesh::recompute_normals()
{
    auto* trimesh = dynamic_cast<const nwm::TrimeshNode*>(orig_);
    if (!trimesh || trimesh->indices.empty() || cpu_vertices_.empty()) {
        return;
    }

    if (is_foliage_like_ && cpu_vertices_.size() == 3) {
        const auto face_normal = glm::cross(
            cpu_vertices_[1].position - cpu_vertices_[0].position,
            cpu_vertices_[2].position - cpu_vertices_[0].position);
        glm::vec3 normal = glm::vec3{0.0f, 0.0f, 1.0f};
        if (glm::length2(face_normal) > 1.0e-10f && finite_vec3(face_normal)) {
            normal = glm::normalize(face_normal);
        } else if (auto* dangly = dynamic_cast<const nwm::DanglymeshNode*>(orig_);
            dangly && dangly->vertices.size() >= 3) {
            const auto fallback = glm::cross(
                dangly->vertices[1].position - dangly->vertices[0].position,
                dangly->vertices[2].position - dangly->vertices[0].position);
            if (glm::length2(fallback) > 1.0e-10f && finite_vec3(fallback)) {
                normal = glm::normalize(fallback);
            }
        }

        for (auto& vertex : cpu_vertices_) {
            vertex.normal = normal;
        }
        return;
    }

    for (auto& vertex : cpu_vertices_) {
        vertex.normal = glm::vec3{0.0f};
    }

    for (size_t i = 0; i + 2 < trimesh->indices.size(); i += 3) {
        const auto i0 = static_cast<size_t>(trimesh->indices[i]);
        const auto i1 = static_cast<size_t>(trimesh->indices[i + 1]);
        const auto i2 = static_cast<size_t>(trimesh->indices[i + 2]);
        if (i0 >= cpu_vertices_.size() || i1 >= cpu_vertices_.size() || i2 >= cpu_vertices_.size()) {
            continue;
        }

        const auto& p0 = cpu_vertices_[i0].position;
        const auto& p1 = cpu_vertices_[i1].position;
        const auto& p2 = cpu_vertices_[i2].position;
        const auto face_normal = glm::cross(p1 - p0, p2 - p0);
        if (glm::length2(face_normal) < 1.0e-10f) {
            continue;
        }

        cpu_vertices_[i0].normal += face_normal;
        cpu_vertices_[i1].normal += face_normal;
        cpu_vertices_[i2].normal += face_normal;
    }

    for (size_t i = 0; i < cpu_vertices_.size(); ++i) {
        if (glm::length2(cpu_vertices_[i].normal) > 1.0e-10f && finite_vec3(cpu_vertices_[i].normal)) {
            cpu_vertices_[i].normal = glm::normalize(cpu_vertices_[i].normal);
        } else if (auto* dangly = dynamic_cast<const nwm::DanglymeshNode*>(orig_); dangly && i < dangly->vertices.size()) {
            if (finite_vec3(dangly->vertices[i].normal) && glm::length2(dangly->vertices[i].normal) > 1.0e-10f) {
                cpu_vertices_[i].normal = dangly->vertices[i].normal;
            } else {
                cpu_vertices_[i].normal = glm::vec3{0.0f, 0.0f, 1.0f};
            }
        }
    }
}

void DanglyMesh::upload_vertices() const
{
    if (!vertices.valid() || cpu_vertices_.empty()) {
        return;
    }

    auto* vertex_buffer = nw::gfx::map_buffer(vertices);
    if (!vertex_buffer) {
        return;
    }

    std::memcpy(vertex_buffer, cpu_vertices_.data(), cpu_vertices_.size() * sizeof(Vertex));
    nw::gfx::unmap_buffer(vertices);
}

// ============================================================================
// Model
// ============================================================================

Node* ModelInstance::find(std::string_view name)
{
    for (const auto& node : nodes_) {
        if (node->orig_ && nw::string::icmp(node->orig_->name, name)) {
            return node.get();
        }
    }
    return nullptr;
}

const Node* ModelInstance::find(std::string_view name) const
{
    for (const auto& node : nodes_) {
        if (node->orig_ && nw::string::icmp(node->orig_->name, name)) {
            return node.get();
        }
    }
    return nullptr;
}

bool ModelInstance::load(const nwm::Mdl* mdl, nw::gfx::Context* ctx, std::string_view root_name)
{
    const nwm::Node* root_node = nullptr;
    auto desired_root = root_name.empty() ? std::string_view{mdl->model.name} : root_name;
    for (const auto& node : mdl->model.nodes) {
        if (node && nw::string::icmp(node->name, desired_root)) {
            root_node = node.get();
            break;
        }
    }
    if (!root_node) {
        LOG_F(INFO, "No root dummy");
        return false;
    }
    mdl_ = mdl;
    ctx_ = ctx;
    if (load_node(root_node, nullptr)) {
        source_nodes_.resize(mdl->model.nodes.size(), nullptr);
        for (auto& node : nodes_) {
            node->owner_ = this;
            for (size_t i = 0; i < mdl->model.nodes.size(); ++i) {
                if (mdl->model.nodes[i].get() == node->orig_) {
                    source_nodes_[i] = node.get();
                    break;
                }
            }
        }
        capture_bind_pose();
        build_ozz_data();
        return true;
    }
    return false;
}

Node* ModelInstance::load_node(const nwm::Node* node, Node* parent)
{
    std::unique_ptr<Node> result;

    // Create appropriate node type
    if (node->type & nwm::NodeFlags::skin) {
        auto n = static_cast<const nwm::SkinNode*>(node);
        if (n->render && !n->vertices.empty() && !n->indices.empty()) {
            auto mesh = std::make_unique<SkinMesh>();
            mesh->bone_nodes = n->bone_nodes;
            initialize_mesh_material(*mesh, n, mdl_->model.classification);
            result = std::move(mesh);
        }
    } else if (node->type & nwm::NodeFlags::dangly) {
        auto n = static_cast<const nwm::DanglymeshNode*>(node);
        if (n->render && !n->vertices.empty() && !n->indices.empty()) {
            auto mesh = std::make_unique<DanglyMesh>();
            initialize_mesh_material(*mesh, n, mdl_->model.classification);
            mesh->initialize_dangly(n);
            result = std::move(mesh);
        }
    } else if (node->type & nwm::NodeFlags::mesh && !(node->type & nwm::NodeFlags::aabb)) {
        auto n = static_cast<const nwm::TrimeshNode*>(node);
        if (n->render && !n->vertices.empty() && !n->indices.empty()) {
            auto mesh = std::make_unique<Mesh>();
            initialize_mesh_material(*mesh, n, mdl_->model.classification);
            result = std::move(mesh);
        }
    }

    if (!result) {
        result = std::make_unique<Node>();
    }

    // Setup hierarchy
    result->parent_ = parent;
    result->orig_ = node;

    // Initialize from static controllers
    auto pos = node->get_controller(nwm::ControllerType::Position, false);
    if (pos.data.size() >= 3) {
        result->has_transform_ = true;
        result->position_ = glm::vec3{pos.data[0], pos.data[1], pos.data[2]};
    }

    auto ori = node->get_controller(nwm::ControllerType::Orientation, false);
    if (ori.data.size() >= 4) {
        result->has_transform_ = true;
        result->rotation_ = glm::quat{ori.data[3], ori.data[0], ori.data[1], ori.data[2]};
    }

    auto scale = node->get_controller(nwm::ControllerType::Scale, false);
    if (!scale.data.empty()) {
        result->has_transform_ = true;
        if (scale.data.size() >= 3) {
            result->scale_ = glm::vec3{scale.data[0], scale.data[1], scale.data[2]};
        } else {
            result->scale_ = glm::vec3{scale.data[0]};
        }
    }

    Node* result_ptr = result.get();
    nodes_.push_back(std::move(result));

    // Add to parent's children
    if (parent) {
        parent->children_.push_back(result_ptr);
    }

    // Load children recursively
    for (const auto* child : node->children) {
        load_node(child, result_ptr);
    }

    return result_ptr;
}

void ModelInstance::capture_bind_pose()
{
    for (auto& node : nodes_) {
        node->bind_pose_ = node->get_transform();
    }

    for (auto& node : nodes_) {
        if (auto* skin = dynamic_cast<SkinMesh*>(node.get())) {
            skin->initialize_skinning();
        }
    }
}

void ModelInstance::build_ozz_data()
{
    if (nodes_.empty()) return;

    nwn_backend_ = nw::render::make_animation_backend(nw::render::AnimationBackendKind::ozz);
    nwn_skeleton_ = build_nwn_skeleton(*this, joint_to_source_node_);
    if (nwn_skeleton_.joints.empty() || !nwn_backend_->build_skeleton(0, nwn_skeleton_)) {
        nwn_backend_.reset();
        return;
    }
    LOG_F(INFO, "NWN animation backend: ozz ({} nodes)", nwn_skeleton_.joints.size());
}

Node* ModelInstance::node_from_source_index(int32_t idx) const
{
    if (idx < 0 || static_cast<size_t>(idx) >= source_nodes_.size()) {
        return nullptr;
    }
    return source_nodes_[idx];
}

Bounds ModelInstance::current_bounds() const
{
    Bounds result{};
    bool first = true;
    const auto root = root_transform();

    for (const auto& node : nodes_) {
        auto* mesh = dynamic_cast<const Mesh*>(node.get());
        auto* trimesh = mesh ? dynamic_cast<const nwm::TrimeshNode*>(mesh->orig_) : nullptr;
        if (!mesh || !trimesh) {
            continue;
        }

        const auto world_transform = root * mesh->get_transform();
        if (auto* dangly = dynamic_cast<const DanglyMesh*>(mesh)) {
            for (const auto& vertex : dangly->cpu_vertices_) {
                expand_bounds(result, transform_point(world_transform, vertex.position), first);
                first = false;
            }
        } else if (auto* skin = dynamic_cast<const SkinMesh*>(mesh)) {
            std::array<glm::mat4, 64> skin_matrices{};
            for (auto& bone : skin_matrices) {
                bone = glm::mat4(1.0f);
            }
            skin->fill_skin_matrices(skin_matrices.data(), skin_matrices.size());

            const auto* skin_node = dynamic_cast<const nwm::SkinNode*>(skin->orig_);
            if (!skin_node) {
                continue;
            }

            for (const auto& vertex : skin_node->vertices) {
                const glm::vec4& weights = vertex.weights;
                const glm::uvec4 indices = glm::uvec4{vertex.bones.x, vertex.bones.y, vertex.bones.z, vertex.bones.w};
                const auto matrix_for = [&](uint32_t idx) -> const glm::mat4& {
                    static const glm::mat4 identity{1.0f};
                    return idx < skin_matrices.size() ? skin_matrices[idx] : identity;
                };
                glm::mat4 skin_matrix = matrix_for(indices.x) * weights.x
                    + matrix_for(indices.y) * weights.y
                    + matrix_for(indices.z) * weights.z
                    + matrix_for(indices.w) * weights.w;
                const glm::vec3 skinned = glm::vec3(skin_matrix * glm::vec4(vertex.position, 1.0f));
                expand_bounds(result, transform_point(root, skinned), first);
                first = false;
            }
        } else {
            for_each_vertex_position(trimesh, [&](const glm::vec3& position) {
                expand_bounds(result, transform_point(world_transform, position), first);
                first = false;
            });
        }
    }

    return first ? this->bounds : result;
}

bool ModelInstance::load_animation(std::string_view name)
{
    anim_ = nullptr;
    const nwm::Mdl* m = animation_source_ ? animation_source_ : mdl_;
    while (m) {
        for (const auto& it : m->model.animations) {
            if (it->name == name) {
                anim_ = it.get();
                break;
            }
        }
        if (!m->model.supermodel || anim_) { break; }
        m = m->model.supermodel.get();
    }
    if (anim_) {
        LOG_F(INFO, "Loaded animation: {}", name);

        const char* backend_name = "custom";
        if (nwn_backend_) {
            const std::string name_str(name);
            auto it = nwn_clip_name_to_index_.find(name_str);
            if (it != nwn_clip_name_to_index_.end()) {
                nwn_clip_index_ = static_cast<int32_t>(it->second);
                backend_name = "ozz";
            } else {
                const uint32_t clip_idx = static_cast<uint32_t>(nwn_clips_.size());
                auto clip = build_nwn_clip(*anim_, nwn_skeleton_, 0);
                if (nwn_backend_->build_clip(clip_idx, clip)) {
                    nwn_clip_name_to_index_[name_str] = clip_idx;
                    nwn_clips_.push_back(std::move(clip));
                    nwn_clip_index_ = static_cast<int32_t>(clip_idx);
                    backend_name = "ozz";
                } else {
                    nwn_clip_index_ = -1;
                }
            }
        }
        LOG_F(INFO, "NWN animation sampler: {}", backend_name);
    }
    return !!anim_;
}

glm::mat4 ModelInstance::root_transform() const
{
    auto base = placement_transform_;
    if (this->local_scale_ != 1.0f) {
        base = base * glm::scale(glm::mat4(1.0f), glm::vec3(this->local_scale_));
    }
    if (!transform_context_ || transform_anchor_.empty()) {
        return base;
    }

    auto* anchor = transform_context_->find(transform_anchor_);
    glm::mat4 anchor_transform = transform_context_->root_transform() * base;
    if (anchor_position_only) {
        const glm::mat4 context_root = transform_context_->root_transform();
        glm::vec3 anchor_world = glm::vec3(context_root[3]);
        if (anchor) {
            anchor_world = glm::vec3(context_root * anchor->get_transform()[3]);
        }

        glm::mat3 root_basis{context_root};
        for (int i = 0; i < 3; ++i) {
            const float length = glm::length(root_basis[i]);
            if (length > 0.0f) {
                root_basis[i] /= length;
            }
        }
        const glm::quat root_rotation = glm::normalize(glm::quat_cast(root_basis));
        anchor_transform = glm::translate(glm::mat4(1.0f), anchor_world) * glm::mat4_cast(root_rotation) * base;
    } else if (anchor) {
        anchor_transform = anchor_transform * anchor->get_transform();
    }

    if (this->anchor_uses_root_bind_offset && !nodes_.empty()) {
        const Node* local_anchor = this->transform_source_anchor_.empty() ? nullptr : find(this->transform_source_anchor_);
        if (local_anchor) {
            anchor_transform = anchor_transform * glm::inverse(local_anchor->bind_pose_);
        } else {
            auto bind_translation = glm::vec3(nodes_.front()->bind_pose_[3]);
            anchor_transform = anchor_transform * glm::translate(glm::mat4(1.0f), -bind_translation);
        }
    }

    return anchor_transform;
}

void ModelInstance::update(int32_t dt_ms)
{
    if (anim_) {
        // Update animation cursor with wrapping (arclight pattern)
        if (dt_ms + anim_cursor_ > int32_t(anim_->length * 1000)) {
            anim_cursor_ = dt_ms + anim_cursor_ - int32_t(anim_->length * 1000);
        } else {
            anim_cursor_ += dt_ms;
        }

        float time_ms = static_cast<float>(anim_cursor_);

        // Use the unified ozz backend when a clip is loaded.
        bool ozz_sampled = false;
        if (nwn_backend_ && nwn_clip_index_ >= 0) {
            const float time_s = time_ms / 1000.0f;
            if (nwn_backend_->sample(static_cast<uint32_t>(nwn_clip_index_), time_s, nwn_pose_, true)) {
                for (size_t ji = 0; ji < joint_to_source_node_.size(); ++ji) {
                    Node* bone = node_from_source_index(joint_to_source_node_[ji]);
                    if (!bone) continue;
                    const auto& local = nwn_pose_.local[ji];
                    bone->position_ = local.translation;
                    bone->rotation_ = local.rotation;
                    bone->scale_ = local.scale;
                    bone->has_transform_ = true;
                }
                ozz_sampled = true;
            }
        }

        if (!ozz_sampled) {
        // Update all animated nodes
        for (const auto& anim_node : anim_->nodes) {
            if (!anim_node) continue;

            auto node = find(anim_node->name);
            if (!node) { continue; }

            // Position controller (keyed)
            auto poskey = anim_node->get_controller(nwm::ControllerType::Position, true);
            if (poskey.time.size() > 0) {
                int idx1 = -1, idx2 = 0;

                for (size_t i = 0; i < poskey.time.size(); ++i) {
                    if (time_ms >= poskey.time[i] * 1000) {
                        idx1 = static_cast<int>(i);
                    } else {
                        idx2 = static_cast<int>(i);
                        break;
                    }
                }

                if (idx1 == -1) {
                    idx1 = static_cast<int>(poskey.time.size() - 1);
                    idx2 = 0;
                } else if (idx2 == 0 && idx1 != -1) {
                    idx2 = 0;
                }

                float time1 = poskey.time[idx1] * 1000;
                float time2 = (idx2 > idx1) ? poskey.time[idx2] * 1000
                                            : poskey.time[0] * 1000 + anim_->length * 1000;

                float t = 0.0f;
                if (time2 > time1) {
                    t = (time_ms - time1) / (time2 - time1);
                } else {
                    t = (time_ms - time1) / ((anim_->length * 1000) - time1);
                }
                t = std::max(0.0f, std::min(1.0f, t));

                glm::vec3 pos1(poskey.data[idx1 * 3], poskey.data[idx1 * 3 + 1], poskey.data[idx1 * 3 + 2]);
                glm::vec3 pos2;
                if (idx2 < static_cast<int>(poskey.time.size())) {
                    pos2 = glm::vec3(poskey.data[idx2 * 3], poskey.data[idx2 * 3 + 1], poskey.data[idx2 * 3 + 2]);
                } else {
                    pos2 = glm::vec3(poskey.data[0], poskey.data[1], poskey.data[2]);
                }

                node->position_ = glm::mix(pos1, pos2, t);
                node->has_transform_ = true;
            }

            // Orientation controller (keyed)
            auto orikey = anim_node->get_controller(nwm::ControllerType::Orientation, true);
            if (orikey.time.size() > 0) {
                int idx1 = -1, idx2 = 0;

                for (size_t i = 0; i < orikey.time.size(); ++i) {
                    if (time_ms >= orikey.time[i] * 1000) {
                        idx1 = static_cast<int>(i);
                    } else {
                        idx2 = static_cast<int>(i);
                        break;
                    }
                }

                if (idx1 == -1) {
                    idx1 = static_cast<int>(orikey.time.size() - 1);
                    idx2 = 0;
                } else if (idx2 == 0 && idx1 != -1) {
                    idx2 = 0;
                }

                float time1 = orikey.time[idx1] * 1000;
                float time2 = (idx2 > idx1) ? orikey.time[idx2] * 1000
                                            : orikey.time[0] * 1000 + anim_->length * 1000;

                float t = 0.0f;
                if (time2 > time1) {
                    t = (time_ms - time1) / (time2 - time1);
                } else {
                    t = (time_ms - time1) / ((anim_->length * 1000) - time1);
                }
                t = std::max(0.0f, std::min(1.0f, t));

                glm::quat rot1(orikey.data[idx1 * 4 + 3], orikey.data[idx1 * 4],
                    orikey.data[idx1 * 4 + 1], orikey.data[idx1 * 4 + 2]);
                glm::quat rot2;
                if (idx2 < static_cast<int>(orikey.time.size())) {
                    rot2 = glm::quat(orikey.data[idx2 * 4 + 3], orikey.data[idx2 * 4],
                        orikey.data[idx2 * 4 + 1], orikey.data[idx2 * 4 + 2]);
                } else {
                    rot2 = glm::quat(orikey.data[3], orikey.data[0],
                        orikey.data[1], orikey.data[2]);
                }
                node->rotation_ = glm::slerp(rot1, rot2, t);
                node->has_transform_ = true;
            }
        }
        } // if (!ozz_sampled)
    }

    const auto root = root_transform();
    for (const auto& node : nodes_) {
        auto* dangly = dynamic_cast<DanglyMesh*>(node.get());
        if (!dangly) {
            continue;
        }
        dangly->update_dangly(root * dangly->get_transform(), dt_ms);
    }
}

// ============================================================================
// ModelLoader
// ============================================================================

ModelLoader::ModelLoader(nw::gfx::Context* ctx)
    : ctx_(ctx)
{
}

std::unique_ptr<ModelInstance> ModelLoader::load(const nwm::Mdl* mdl, std::string_view root_name)
{
    if (!mdl) {
        return nullptr;
    }

    auto model = std::make_unique<ModelInstance>();
    if (!model->load(mdl, ctx_, root_name)) {
        return nullptr;
    }

    for (const auto& node : model->nodes_) {
        if (!node->is_mesh) continue;
        auto* mesh = static_cast<Mesh*>(node.get());

        auto* trimesh = dynamic_cast<const nwm::TrimeshNode*>(mesh->orig_);
        if (!trimesh) {
            continue;
        }

        bool created = true;
        if (ctx_) {
            if (auto* skin = dynamic_cast<SkinMesh*>(mesh)) {
                created = create_skin_buffers(*skin, static_cast<const nwm::SkinNode*>(trimesh));
            } else {
                created = create_mesh_buffers(*mesh, trimesh);
            }
        }
        if (!created) {
            LOG_F(WARNING, "Failed to create mesh buffers for node {}", mesh->orig_->name);
        }

        const auto world_transform = mesh->get_transform();
        const bool first_vertex = model->vertex_count == 0;
        model->vertex_count += vertex_count(trimesh);
        model->index_count += trimesh->indices.size();
        bool is_first = first_vertex;
        for_each_vertex_position(trimesh, [&](const glm::vec3& position) {
            expand_bounds(model->bounds, transform_point(world_transform, position), is_first);
            is_first = false;
        });
    }

    return model;
}

std::unique_ptr<ModelInstance> ModelLoader::load(std::string_view resref, std::string_view root_name)
{
    auto* mdl = nw::kernel::models().load(resref);
    if (!mdl) {
        LOG_F(ERROR, "Failed to load model: {}", resref);
        return nullptr;
    }

    auto model = load(mdl, root_name);
    if (!model) {
        LOG_F(ERROR, "Failed to load model: {}", resref);
    }
    return model;
}

bool ModelLoader::create_mesh_buffers(Mesh& mesh, const nwm::TrimeshNode* node)
{
    if (!ctx_ || !node || node->vertices.empty() || node->indices.empty()) {
        return false;
    }

    std::vector<Vertex> vertices;
    if (auto* dangly = dynamic_cast<DanglyMesh*>(&mesh); dangly && dangly->cpu_vertices_.size() == node->vertices.size()) {
        vertices = dangly->cpu_vertices_;
    } else {
        vertices.reserve(node->vertices.size());
        for (const auto& vertex : node->vertices) {
            vertices.push_back(convert_vertex(vertex));
        }

        bool has_invalid_normals = false;
        for (const auto& vertex : vertices) {
            if (!finite_vec3(vertex.normal)) {
                has_invalid_normals = true;
                break;
            }
        }
        if (has_invalid_normals) {
            recompute_static_vertex_normals(node, vertices);
        }
        if (has_invalid_tangents(vertices)) {
            recompute_vertex_tangents(node, vertices);
        }
    }

    inset_transparent_subrect_uvs(mesh, vertices);

    nw::gfx::BufferDesc vertex_desc{};
    vertex_desc.size = vertices.size() * sizeof(Vertex);
    vertex_desc.usage = nw::gfx::BufferUsage::Vertex;
    vertex_desc.cpu_visible = true;
    mesh.vertices = nw::gfx::create_buffer(ctx_, vertex_desc);

    nw::gfx::BufferDesc index_desc{};
    index_desc.size = node->indices.size() * sizeof(uint16_t);
    index_desc.usage = nw::gfx::BufferUsage::Index;
    index_desc.cpu_visible = true;
    mesh.indices = nw::gfx::create_buffer(ctx_, index_desc);

    auto* vertex_buffer = nw::gfx::map_buffer(mesh.vertices);
    auto* index_buffer = nw::gfx::map_buffer(mesh.indices);
    if (!vertex_buffer || !index_buffer) {
        return false;
    }

    std::memcpy(vertex_buffer, vertices.data(), vertex_desc.size);
    std::memcpy(index_buffer, node->indices.data(), index_desc.size);
    nw::gfx::unmap_buffer(mesh.vertices);
    nw::gfx::unmap_buffer(mesh.indices);

    mesh.index_count = static_cast<uint32_t>(node->indices.size());
    return true;
}

bool ModelLoader::create_skin_buffers(SkinMesh& mesh, const nwm::SkinNode* node)
{
    if (!ctx_ || !node || node->vertices.empty() || node->indices.empty()) {
        return false;
    }

    std::vector<SkinnedVertex> vertices;
    vertices.reserve(node->vertices.size());
    for (const auto& vertex : node->vertices) {
        vertices.push_back(convert_vertex(vertex));
    }

    bool has_invalid_normals = false;
    for (const auto& vertex : vertices) {
        if (!finite_vec3(vertex.normal)) {
            has_invalid_normals = true;
            break;
        }
    }
    if (has_invalid_normals) {
        std::vector<Vertex> static_vertices;
        static_vertices.reserve(vertices.size());
        for (const auto& vertex : vertices) {
            static_vertices.push_back(Vertex{
                .position = vertex.position,
                .normal = vertex.normal,
                .texcoord = vertex.texcoord,
                .tangent = vertex.tangent,
            });
        }
        recompute_static_vertex_normals(node, static_vertices);
        for (size_t i = 0; i < vertices.size(); ++i) {
            vertices[i].normal = static_vertices[i].normal;
        }
    }

    if (has_invalid_tangents(vertices)) {
        recompute_vertex_tangents(node, vertices);
    }

    inset_transparent_subrect_uvs(mesh, vertices);

    nw::gfx::BufferDesc vertex_desc{};
    vertex_desc.size = vertices.size() * sizeof(SkinnedVertex);
    vertex_desc.usage = nw::gfx::BufferUsage::Vertex;
    vertex_desc.cpu_visible = true;
    mesh.vertices = nw::gfx::create_buffer(ctx_, vertex_desc);

    nw::gfx::BufferDesc index_desc{};
    index_desc.size = node->indices.size() * sizeof(uint16_t);
    index_desc.usage = nw::gfx::BufferUsage::Index;
    index_desc.cpu_visible = true;
    mesh.indices = nw::gfx::create_buffer(ctx_, index_desc);

    auto* vertex_buffer = nw::gfx::map_buffer(mesh.vertices);
    auto* index_buffer = nw::gfx::map_buffer(mesh.indices);
    if (!vertex_buffer || !index_buffer) {
        return false;
    }

    std::memcpy(vertex_buffer, vertices.data(), vertex_desc.size);
    std::memcpy(index_buffer, node->indices.data(), index_desc.size);
    nw::gfx::unmap_buffer(mesh.vertices);
    nw::gfx::unmap_buffer(mesh.indices);

    mesh.index_count = static_cast<uint32_t>(node->indices.size());
    return true;
}

// ============================================================================
// Helper
// ============================================================================

std::unique_ptr<ModelInstance> load_model(std::string_view resref, std::string_view root_name)
{
    auto* mdl = nw::kernel::models().load(resref);
    if (!mdl) { return {}; }

    auto model = std::make_unique<ModelInstance>();
    if (!model->load(mdl, nullptr, root_name)) {
        return {};
    }
    return model;
}

void set_dangly_debug_scale(float scale)
{
    g_dangly_debug_scale = std::max(0.0f, scale);
}

float dangly_debug_scale()
{
    return safe_debug_scale();
}

void set_dangly_mode(DanglyMode mode)
{
    g_dangly_mode = mode;
}

DanglyMode dangly_mode()
{
    return g_dangly_mode;
}

} // namespace nw::render::nwn
