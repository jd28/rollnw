#include "model_loader.hpp"
#include "nwn_animation.hpp"

#include <nw/model/mdl_particle_import.hpp>
#include <nw/render/animation_backend.hpp>

#include <nw/formats/Image.hpp>
#include <nw/formats/Txi.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/ModelCache.hpp>
#include <nw/log.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/util/Tokenizer.hpp>
#include <nw/util/error_context.hpp>
#include <nw/util/string.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace nw::render::nwn {

namespace nwm = nw::model;

namespace {

float g_dangly_debug_scale = 1.0f;
DanglyMode g_dangly_mode = DanglyMode::legacy;
constexpr uint8_t kOpaqueCutoutPassMask = 0x1u;
constexpr uint8_t kWaterPassMask = 0x2u;
constexpr uint8_t kTransparentPassMask = 0x4u;
constexpr float kNwnFoliageMotionScale = 0.1f;
constexpr float kLegacyDefaultRoughness = 0.78f;
constexpr float kLegacyDefaultSpecularStrength = 0.12f;

std::string_view nwn_equipped_item_socket_alias(std::string_view source_name) noexcept
{
    if (nw::string::icmp(source_name, "rhand_g")) {
        return "rhand";
    }
    if (nw::string::icmp(source_name, "lhand_g")) {
        return "lhand";
    }
    return {};
}

bool model_socket_name_exists(
    const std::vector<nw::render::ModelSocket>& sockets,
    std::string_view name) noexcept
{
    return std::any_of(sockets.begin(), sockets.end(), [name](const auto& socket) {
        return nw::string::icmp(socket.name, name);
    });
}

void append_model_socket_if_missing(
    std::vector<nw::render::ModelSocket>& sockets,
    size_t source_node_index,
    size_t source_node_count,
    const glm::mat4& local_transform,
    const glm::mat4& bind_transform,
    std::string_view name)
{
    if (name.empty()
        || source_node_index >= source_node_count
        || source_node_index >= nw::render::kInvalidModelNodeIndex
        || model_socket_name_exists(sockets, name)) {
        return;
    }

    sockets.push_back(nw::render::ModelSocket{
        .source_node_index = static_cast<uint32_t>(source_node_index),
        .local_transform = local_transform,
        .bind_transform = bind_transform,
        .name = std::string(name),
    });
}

void append_model_asset_socket_if_missing(
    nw::render::ModelAsset& asset,
    size_t source_node_index,
    std::string_view name)
{
    if (source_node_index >= asset.nodes.size()) {
        return;
    }

    const auto& node = asset.nodes[source_node_index];
    append_model_socket_if_missing(
        asset.sockets,
        source_node_index,
        asset.nodes.size(),
        node.local_transform,
        node.world_transform,
        name);
}

void append_sidecar_socket_if_missing(
    std::vector<nw::render::ModelSocket>& sockets,
    const Node& node,
    size_t source_node_index,
    size_t source_node_count,
    std::string_view name)
{
    append_model_socket_if_missing(
        sockets,
        source_node_index,
        source_node_count,
        node.get_local_transform(),
        node.bind_pose_,
        name);
}

void log_error_context()
{
    if (nw::error_context_stack) {
        LOG_F(ERROR, "\n{}", nw::get_error_context());
    }
}

Vertex convert_vertex(const nwm::Vertex& vertex)
{
    return Vertex{
        .position = vertex.position,
        .normal = vertex.normal,
        .texcoord = vertex.tex_coords,
        .tangent = vertex.tangent,
    };
}

std::vector<uint16_t> validated_triangle_indices(
    const std::vector<uint16_t>& source,
    size_t vertex_count,
    std::string_view node_name)
{
    std::vector<uint16_t> result;
    result.reserve(source.size());

    size_t dropped = 0;
    size_t i = 0;
    for (; i + 2 < source.size(); i += 3) {
        const auto i0 = static_cast<size_t>(source[i + 0]);
        const auto i1 = static_cast<size_t>(source[i + 1]);
        const auto i2 = static_cast<size_t>(source[i + 2]);
        if (i0 >= vertex_count || i1 >= vertex_count || i2 >= vertex_count) {
            ++dropped;
            continue;
        }

        result.push_back(source[i + 0]);
        result.push_back(source[i + 1]);
        result.push_back(source[i + 2]);
    }

    if (i != source.size()) {
        ++dropped;
    }
    if (dropped != 0) {
        LOG_F(WARNING, "model loader: dropped {} invalid triangles for node '{}'", dropped, node_name);
    }
    return result;
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

uint64_t next_cache_revision(uint64_t revision) noexcept
{
    ++revision;
    return revision == 0 ? 1 : revision;
}

bool vec3_equal(const glm::vec3& lhs, const glm::vec3& rhs) noexcept
{
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

bool quat_equal(const glm::quat& lhs, const glm::quat& rhs) noexcept
{
    return lhs.w == rhs.w && lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

bool mat4_equal(const glm::mat4& lhs, const glm::mat4& rhs) noexcept
{
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            if (lhs[col][row] != rhs[col][row]) {
                return false;
            }
        }
    }
    return true;
}

glm::mat4 normal_matrix_for(const glm::mat4& model_matrix)
{
    return glm::mat4(glm::mat3(glm::transpose(glm::inverse(model_matrix))));
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

glm::vec3 mesh_self_illum_value(const nwm::TrimeshNode* node)
{
    if (!node) { return glm::vec3{0.0f}; }
    auto value = node->get_controller(nwm::ControllerType::SelfIllumColor);
    if (value.key && value.data.size() >= 3) {
        return glm::vec3{value.data[0], value.data[1], value.data[2]};
    }
    return glm::vec3{0.0f};
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

bool valid_tangent_handedness(float value)
{
    return std::isfinite(value) && std::abs(std::abs(value) - 1.0f) <= 1.0e-3f;
}

float tangent_handedness_or_default(float value)
{
    return valid_tangent_handedness(value) ? value : 1.0f;
}

glm::vec3 fallback_tangent_for_normal(const glm::vec3& normal)
{
    if (!finite_vec3(normal) || glm::length2(normal) < 1.0e-10f) {
        return glm::vec3{1.0f, 0.0f, 0.0f};
    }

    const auto n = glm::normalize(normal);
    const glm::vec3 axis = std::abs(n.z) < 0.999f ? glm::vec3{0.0f, 0.0f, 1.0f} : glm::vec3{0.0f, 1.0f, 0.0f};
    return glm::normalize(glm::cross(axis, n));
}

template <typename TVertex>
void recompute_static_vertex_normals_for_indices(
    const std::vector<uint16_t>& indices,
    std::vector<TVertex>& vertices)
{
    if (vertices.empty() || indices.empty()) {
        return;
    }

    for (auto& vertex : vertices) {
        vertex.normal = glm::vec3{0.0f};
    }

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const auto i0 = static_cast<size_t>(indices[i]);
        const auto i1 = static_cast<size_t>(indices[i + 1]);
        const auto i2 = static_cast<size_t>(indices[i + 2]);
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
void recompute_static_vertex_normals(const nwm::TrimeshNode* node, std::vector<TVertex>& vertices)
{
    if (!node || vertices.size() != node->vertices.size() || node->indices.empty()) {
        return;
    }
    recompute_static_vertex_normals_for_indices(node->indices, vertices);
}

template <typename TVertex>
void recompute_vertex_tangents_for_indices(
    const std::vector<uint16_t>& indices,
    std::vector<TVertex>& vertices)
{
    if (vertices.empty() || indices.empty()) {
        return;
    }

    std::vector<glm::vec3> tan1(vertices.size(), glm::vec3{0.0f});
    std::vector<glm::vec3> tan2(vertices.size(), glm::vec3{0.0f});

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const auto i0 = static_cast<size_t>(indices[i]);
        const auto i1 = static_cast<size_t>(indices[i + 1]);
        const auto i2 = static_cast<size_t>(indices[i + 2]);
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
        const auto normal = finite_vec3(vertices[i].normal) && glm::length2(vertices[i].normal) >= 1.0e-10f
            ? glm::normalize(vertices[i].normal)
            : glm::vec3{0.0f, 0.0f, 1.0f};
        const auto& t = tan1[i];
        if (!finite_vec3(t) || glm::length2(t) < 1.0e-10f) {
            vertices[i].tangent = glm::vec4{fallback_tangent_for_normal(normal), 1.0f};
            continue;
        }

        const auto orthogonal = t - normal * glm::dot(normal, t);
        if (!finite_vec3(orthogonal) || glm::length2(orthogonal) < 1.0e-10f) {
            vertices[i].tangent = glm::vec4{fallback_tangent_for_normal(normal), 1.0f};
            continue;
        }

        const auto tangent = glm::normalize(orthogonal);
        const float handedness = (glm::dot(glm::cross(normal, t), tan2[i]) < 0.0f) ? -1.0f : 1.0f;
        vertices[i].tangent = glm::vec4{tangent, handedness};
    }
}

template <typename TVertex>
void recompute_vertex_tangents(const nwm::TrimeshNode* node, std::vector<TVertex>& vertices)
{
    if (!node || vertices.size() != node->vertices.size() || node->indices.empty()) {
        return;
    }
    recompute_vertex_tangents_for_indices(node->indices, vertices);
}

template <typename TVertex>
bool has_invalid_tangents(const std::vector<TVertex>& vertices)
{
    for (const auto& vertex : vertices) {
        const glm::vec3 tangent{vertex.tangent};
        const float tangent_length2 = glm::length2(tangent);
        if (!finite_vec3(vertex.normal) || glm::length2(vertex.normal) < 1.0e-10f || !finite_vec3(tangent)
            || tangent_length2 < 1.0e-10f || !valid_tangent_handedness(vertex.tangent.w)) {
            return true;
        }

        const auto normal = glm::normalize(vertex.normal);
        const auto tangent_unit = glm::normalize(tangent);
        if (std::abs(glm::length(tangent) - 1.0f) > 1.0e-3f
            || std::abs(glm::dot(normal, tangent_unit)) > 1.0e-3f) {
            return true;
        }
    }
    return false;
}

template <typename TVertex>
bool has_invalid_normals(const std::vector<TVertex>& vertices)
{
    for (const auto& vertex : vertices) {
        if (!finite_vec3(vertex.normal) || glm::length2(vertex.normal) < 1.0e-10f) {
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

bool casts_shadow_material(MaterialMode mode)
{
    return mode != MaterialMode::transparent && mode != MaterialMode::water;
}

bool has_declared_texture(const nwm::TrimeshNode* node)
{
    if (!node) {
        return false;
    }
    return (!node->bitmap.empty() && node->bitmap != "null")
        || (!node->textures[0].empty() && node->textures[0] != "null");
}

bool should_create_mesh_node(const nwm::TrimeshNode* node, nwm::ModelClass model_class)
{
    if (!node || node->vertices.empty() || node->indices.empty()) {
        return false;
    }
    if (node->render) {
        return true;
    }

    // Many NWN tile sets mark visible tilefade/detail meshes as render=0. Keep
    // collision/AABB meshes hidden, but preserve textured tile geometry.
    return model_class == nwm::ModelClass::tile && has_declared_texture(node);
}

bool should_create_mesh_node(const nwm::SkinNode* node, nwm::ModelClass model_class)
{
    if (!node || node->vertices.empty() || node->indices.empty()) {
        return false;
    }
    if (node->render) {
        return true;
    }

    // Skin nodes keep their vertex stream on SkinNode, not TrimeshNode, so this
    // overload preserves the tile detail exception without losing skinned meshes.
    return model_class == nwm::ModelClass::tile && has_declared_texture(node);
}

bool should_register_shadow_caster(const nwm::TrimeshNode* node, const Mesh& mesh)
{
    return node && node->render && node->shadow && casts_shadow_material(mesh.material_mode);
}

uint8_t material_pass_mask(MaterialMode mode) noexcept
{
    switch (mode) {
    case MaterialMode::opaque:
    case MaterialMode::cutout:
        return kOpaqueCutoutPassMask;
    case MaterialMode::water:
        return kWaterPassMask;
    case MaterialMode::transparent:
        return kTransparentPassMask;
    }
    return 0;
}

nw::render::Transform node_bind_local_transform(const Node& node)
{
    if (node.parent_) {
        return nw::render::Transform::from_matrix(glm::inverse(node.parent_->bind_pose_) * node.bind_pose_);
    }
    return nw::render::Transform::from_matrix(node.bind_pose_);
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

bool has_foliage_token(std::string_view text)
{
    return text.find("bush") != std::string::npos
        || text.find("foliage") != std::string::npos
        || text.find("frond") != std::string::npos
        || text.find("grass") != std::string::npos
        || text.find("leaf") != std::string::npos
        || text.find("leaves") != std::string::npos
        || text.find("palm") != std::string::npos
        || text.find("plant") != std::string::npos
        || text.find("shrub") != std::string::npos
        || text.find("treefol") != std::string::npos
        || text.find("vine") != std::string::npos
        || text.find("fern") != std::string::npos;
}

DanglyDeformPolicy select_dangly_deform_policy(const nwm::DanglymeshNode* node)
{
    if (!node) {
        return DanglyDeformPolicy::legacy_spring_cpu;
    }

    // NWN dangly nodes do not distinguish foliage cards from other spring-like
    // attachments. Keep these content-name hints isolated as an importer
    // compatibility rule, not a renderer-wide wind contract.
    const bool foliage_hint = has_foliage_token(ascii_lower(node->name))
        || has_foliage_token(ascii_lower(node->bitmap))
        || has_foliage_token(ascii_lower(node->textures[0]));
    return foliage_hint
        ? DanglyDeformPolicy::foliage_sway_cpu
        : DanglyDeformPolicy::legacy_spring_cpu;
}

std::string_view dangly_deform_policy_name_impl(DanglyDeformPolicy policy) noexcept
{
    switch (policy) {
    case DanglyDeformPolicy::legacy_spring_cpu:
        return "legacy_spring_cpu";
    case DanglyDeformPolicy::foliage_sway_cpu:
        return "foliage_sway_cpu";
    }
    return "unknown";
}

nw::render::ModelDeformerKind model_deformer_kind_for_impl(DanglyDeformPolicy policy) noexcept
{
    switch (policy) {
    case DanglyDeformPolicy::legacy_spring_cpu:
        return nw::render::ModelDeformerKind::secondary_motion_chain;
    case DanglyDeformPolicy::foliage_sway_cpu:
        return nw::render::ModelDeformerKind::vertex_shader_sway;
    }
    return nw::render::ModelDeformerKind::legacy_reference_cpu;
}

bool looks_like_foliage_texture(std::string_view texture_name)
{
    return has_foliage_token(ascii_lower(texture_name));
}

bool looks_like_web_texture(std::string_view texture_name)
{
    const auto lowered = ascii_lower(texture_name);
    return lowered.find("web") != std::string::npos
        || lowered.find("cobweb") != std::string::npos;
}

bool water_material_model_class(nwm::ModelClass model_class)
{
    switch (model_class) {
    case nwm::ModelClass::tile:
    case nwm::ModelClass::invalid:
        return true;
    case nwm::ModelClass::effect:
    case nwm::ModelClass::character:
    case nwm::ModelClass::door:
    case nwm::ModelClass::item:
    case nwm::ModelClass::gui:
    default:
        return false;
    }
}

bool contains_water_token(std::string_view value)
{
    const auto lowered = ascii_lower(value);
    return lowered.find("water") != std::string::npos
        || lowered.find("watr") != std::string::npos;
}

bool looks_like_water_material(const nwm::TrimeshNode* node, std::string_view bitmap_name,
    nwm::ModelClass model_class)
{
    if (!water_material_model_class(model_class)) {
        return false;
    }

    if (contains_water_token(bitmap_name)) {
        return true;
    }
    if (!node) {
        return false;
    }
    return contains_water_token(node->name)
        || contains_water_token(node->renderhint)
        || contains_water_token(node->materialname);
}

float nwn_shininess_to_legacy_roughness(float shininess) noexcept
{
    if (!std::isfinite(shininess) || shininess <= 0.0f) {
        return kLegacyDefaultRoughness;
    }

    const float spec_power = std::clamp(shininess, 4.0f, 52.0f);
    return std::clamp(1.0f - ((spec_power - 4.0f) / 48.0f), 0.18f, 0.95f);
}

float nwn_specular_to_legacy_strength(const glm::vec3& specular) noexcept
{
    if (!std::isfinite(specular.x) || !std::isfinite(specular.y) || !std::isfinite(specular.z)) {
        return kLegacyDefaultSpecularStrength;
    }

    const float max_channel = std::max({specular.x, specular.y, specular.z});
    if (max_channel <= 0.001f) {
        return 0.0f;
    }

    return std::clamp(max_channel * 2.5f, 0.02f, 0.35f);
}

struct MtrMaterialInfo {
    bool has_mtr = false;
    std::optional<std::string> renderhint;
    std::optional<std::string> diffuse_texture;
    std::optional<std::string> normal_texture;
    std::optional<std::string> specular_texture;
    std::optional<std::string> roughness_texture;
    std::optional<std::string> emissive_texture;
    std::optional<float> roughness;
    std::optional<float> specular_strength;
    std::optional<float> alpha_cutout_threshold;
    std::optional<glm::vec3> emissive;
    std::optional<bool> transparency;
    std::optional<bool> two_sided;
};

std::optional<float> parse_float_token(std::string_view token)
{
    auto value = nw::string::from<float>(token);
    if (!value || !std::isfinite(*value)) {
        return std::nullopt;
    }
    return *value;
}

std::optional<float> next_float_token(nw::Tokenizer& tokens)
{
    const auto token = tokens.next();
    if (token.empty()) {
        return std::nullopt;
    }
    return parse_float_token(token);
}

std::optional<bool> next_bool_token(nw::Tokenizer& tokens)
{
    const auto token = tokens.next();
    if (token.empty()) {
        return std::nullopt;
    }
    return nw::string::from<bool>(token);
}

std::optional<glm::vec3> next_vec3_tokens(nw::Tokenizer& tokens)
{
    const auto x = next_float_token(tokens);
    const auto y = next_float_token(tokens);
    const auto z = next_float_token(tokens);
    if (!x || !y || !z) {
        return std::nullopt;
    }
    return glm::vec3{*x, *y, *z};
}

glm::vec3 clamp_material_color(const glm::vec3& value) noexcept
{
    if (!finite_vec3(value)) {
        return glm::vec3{0.0f};
    }
    return glm::clamp(value, glm::vec3{0.0f}, glm::vec3{4.0f});
}

bool is_mtr_scalar_key(std::string_view key)
{
    return key == "roughness"
        || key == "roughnessfactor"
        || key == "roughnessvalue"
        || key == "shininess"
        || key == "specularity"
        || key == "specularstrength"
        || key == "specularvalue"
        || key == "alphacutoff"
        || key == "alphacutout"
        || key == "alphacutoutthreshold"
        || key == "alphamean"
        || key == "selfillum";
}

bool is_mtr_vec3_key(std::string_view key)
{
    return key == "specular"
        || key == "specularcolor"
        || key == "selfillumcolor"
        || key == "emissive"
        || key == "emissivecolor";
}

bool is_mtr_parameter_type(std::string_view key)
{
    return key == "float"
        || key == "float1"
        || key == "float2"
        || key == "float3"
        || key == "float4"
        || key == "vec3"
        || key == "vec4"
        || key == "color"
        || key == "int"
        || key == "bool";
}

void apply_mtr_scalar(MtrMaterialInfo& info, std::string_view key, float value)
{
    if (!std::isfinite(value)) {
        return;
    }

    if (key == "roughness" || key == "roughnessfactor" || key == "roughnessvalue") {
        info.roughness = std::clamp(value, 0.05f, 1.0f);
    } else if (key == "shininess") {
        info.roughness = nwn_shininess_to_legacy_roughness(value);
    } else if (key == "specularity" || key == "specularstrength" || key == "specularvalue") {
        info.specular_strength = std::clamp(value, 0.0f, 1.0f);
    } else if (key == "alphacutoff" || key == "alphacutout"
        || key == "alphacutoutthreshold" || key == "alphamean") {
        info.alpha_cutout_threshold = std::clamp(value, 0.0f, 1.0f);
    } else if (key == "selfillum" || key == "emissive") {
        info.emissive = glm::vec3{std::clamp(value, 0.0f, 4.0f)};
    }
}

void apply_mtr_vec3(MtrMaterialInfo& info, std::string_view key, const glm::vec3& value)
{
    if (!finite_vec3(value)) {
        return;
    }

    if (key == "specular" || key == "specularcolor") {
        info.specular_strength = nwn_specular_to_legacy_strength(value);
    } else if (key == "selfillumcolor" || key == "emissive" || key == "emissivecolor") {
        info.emissive = clamp_material_color(value);
    }
}

std::string clean_mtr_resource_name(std::string_view raw_name)
{
    auto result = std::string{raw_name};
    nw::string::trim_in_place(&result);
    while (!result.empty() && (result.front() == '"' || result.front() == '\'')) {
        result.erase(result.begin());
    }
    while (!result.empty()
        && (result.back() == '"' || result.back() == '\'' || result.back() == ',' || result.back() == ';')) {
        result.pop_back();
    }
    if (result.empty()
        || nw::string::icmp(result, "null")
        || nw::string::icmp(result, "none")
        || nw::string::icmp(result, "****")) {
        return {};
    }

    const auto separator = result.find_last_of("/\\");
    if (separator != std::string::npos) {
        result.erase(0, separator + 1);
    }

    const auto extension = result.find_last_of('.');
    if (extension != std::string::npos) {
        result.resize(extension);
    }
    return ascii_lower(result);
}

std::optional<std::string> next_mtr_resource_token(nw::Tokenizer& tokens)
{
    const auto token = tokens.next();
    if (token.empty()) {
        return std::nullopt;
    }

    auto resource_name = clean_mtr_resource_name(token);
    if (resource_name.empty()) {
        return std::nullopt;
    }
    return resource_name;
}

bool consume_single_value_mtr_directive(std::string_view key)
{
    return key == "customshadervs"
        || key == "customshaderfs"
        || key == "customshadergs"
        || key == "technique"
        || key == "texture4"
        || key == "texture6"
        || key == "texture7"
        || key == "texture8"
        || key == "texture9"
        || key == "texture10"
        || key == "sample_framebuffer"
        || key == "volumetric";
}

void parse_mtr_parameter(nw::Tokenizer& tokens, MtrMaterialInfo& info)
{
    const auto first = tokens.next();
    const auto second = tokens.next();
    if (first.empty() || second.empty()) {
        return;
    }

    auto first_lower = ascii_lower(first);
    auto second_lower = ascii_lower(second);
    std::string_view type = first_lower;
    std::string_view name = second_lower;
    if (!is_mtr_parameter_type(type) && is_mtr_parameter_type(name)) {
        type = second_lower;
        name = first_lower;
    } else if (!is_mtr_parameter_type(type)) {
        return;
    }

    if (type == "float" || type == "float1") {
        if (auto value = next_float_token(tokens)) {
            apply_mtr_scalar(info, name, *value);
        }
    } else if (type == "float3" || type == "vec3" || type == "color") {
        if (auto value = next_vec3_tokens(tokens)) {
            apply_mtr_vec3(info, name, *value);
        }
    } else if (type == "float4" || type == "vec4") {
        if (auto value = next_vec3_tokens(tokens)) {
            apply_mtr_vec3(info, name, *value);
        }
        (void)next_float_token(tokens);
    } else if (type == "float2" || type == "int" || type == "bool") {
        (void)tokens.next();
        if (type == "float2") {
            (void)tokens.next();
        }
    }
}

MtrMaterialInfo parse_mtr_material_info(std::string_view text)
{
    MtrMaterialInfo info;
    nw::Tokenizer tokens{text, "//"};
    for (auto token = tokens.next(); !token.empty(); token = tokens.next()) {
        const auto key = ascii_lower(token);
        if (key == "mtr" || key == "style") {
            continue;
        }
        if (key == "parameter") {
            parse_mtr_parameter(tokens, info);
            continue;
        }
        if (key == "bitmap" || key == "texture0") {
            if (auto value = next_mtr_resource_token(tokens)) {
                info.diffuse_texture = std::move(*value);
            }
            continue;
        }
        if (key == "texture1") {
            if (auto value = next_mtr_resource_token(tokens)) {
                info.normal_texture = std::move(*value);
            }
            continue;
        }
        if (key == "texture2") {
            if (auto value = next_mtr_resource_token(tokens)) {
                info.specular_texture = std::move(*value);
            }
            continue;
        }
        if (key == "texture3") {
            if (auto value = next_mtr_resource_token(tokens)) {
                info.roughness_texture = std::move(*value);
            }
            continue;
        }
        if (key == "texture5") {
            if (auto value = next_mtr_resource_token(tokens)) {
                info.emissive_texture = std::move(*value);
            }
            continue;
        }
        if (key == "renderhint") {
            if (auto value = next_mtr_resource_token(tokens)) {
                info.renderhint = std::move(*value);
            }
            continue;
        }
        if (key == "transparency") {
            if (auto value = next_bool_token(tokens)) {
                info.transparency = *value;
            }
            continue;
        }
        if (key == "twosided") {
            if (auto value = next_bool_token(tokens)) {
                info.two_sided = *value;
            }
            continue;
        }
        if (is_mtr_vec3_key(key)) {
            if (auto value = next_vec3_tokens(tokens)) {
                apply_mtr_vec3(info, key, *value);
            }
            continue;
        }
        if (is_mtr_scalar_key(key)) {
            if (auto value = next_float_token(tokens)) {
                apply_mtr_scalar(info, key, *value);
            }
            continue;
        }
        if (consume_single_value_mtr_directive(key)) {
            (void)tokens.next();
        }
    }
    return info;
}

std::unordered_map<std::string, MtrMaterialInfo>& mtr_material_cache()
{
    static std::unordered_map<std::string, MtrMaterialInfo> cache;
    return cache;
}

MtrMaterialInfo load_mtr_material_info(std::string_view material_name)
{
    const auto resource_name = clean_mtr_resource_name(material_name);
    if (resource_name.empty()) {
        return {};
    }
    auto& cache = mtr_material_cache();
    if (auto it = cache.find(resource_name); it != cache.end()) {
        return it->second;
    }

    auto data = nw::kernel::resman().demand({nw::Resref{resource_name}, nw::ResourceType::mtr});
    if (data.bytes.size() == 0) {
        return {};
    }

    const auto* bytes = reinterpret_cast<const char*>(data.bytes.data());
    auto info = parse_mtr_material_info(std::string_view{bytes, data.bytes.size()});
    info.has_mtr = true;
    cache.emplace(resource_name, info);
    return info;
}

MtrMaterialInfo load_mtr_material_info(const nwm::TrimeshNode* node)
{
    if (!node) {
        return {};
    }

    const std::array<std::string_view, 3> candidates{
        std::string_view{node->materialname},
        std::string_view{node->textures[0]},
        std::string_view{node->bitmap},
    };
    for (const auto candidate : candidates) {
        auto info = load_mtr_material_info(candidate);
        if (info.has_mtr) {
            return info;
        }
    }
    return {};
}

struct TxiMaterialInfo {
    bool has_txi = false;
    std::string blending;
    float alphamean = 0.5f;
    bool decal = false;
};

struct TextureAnalysis {
    NwnMaterialAlphaProfile alpha_profile = NwnMaterialAlphaProfile::opaque;
    bool has_color_key = false;
    glm::vec3 color_key{0.0f};
    float color_key_threshold = 0.0f;
    float alpha_coverage_50 = 0.0f;
    float alpha_coverage_75 = 0.0f;
    int width = 0;
    int height = 0;
};

std::unordered_map<std::string, TextureAnalysis>& texture_analysis_cache()
{
    static std::unordered_map<std::string, TextureAnalysis> cache;
    return cache;
}

std::string resolve_bitmap_name(const nwm::TrimeshNode* node)
{
    if (!node) {
        return {};
    }
    if (!node->textures[0].empty() && node->textures[0] != "null") {
        return std::string(node->textures[0]);
    }
    if (!node->bitmap.empty() && node->bitmap != "null") {
        return std::string(node->bitmap);
    }
    return {};
}

bool source_resource_exists(std::string_view raw_name, nw::ResourceType::type type)
{
    const auto name = clean_mtr_resource_name(raw_name);
    return !name.empty() && nw::kernel::resman().contains({nw::Resref{name}, type});
}

bool source_texture_exists(std::string_view raw_name)
{
    static constexpr std::array texture_types{
        nw::ResourceType::dds,
        nw::ResourceType::tga,
        nw::ResourceType::plt,
    };
    for (const auto type : texture_types) {
        if (source_resource_exists(raw_name, type)) {
            return true;
        }
    }
    return false;
}

bool source_image_texture_exists(std::string_view raw_name)
{
    return source_resource_exists(raw_name, nw::ResourceType::dds)
        || source_resource_exists(raw_name, nw::ResourceType::tga);
}

bool explicit_texture_reference_missing(std::string_view raw_name, bool allow_plt)
{
    const auto name = clean_mtr_resource_name(raw_name);
    if (name.empty()) {
        return false;
    }
    return allow_plt ? !source_texture_exists(name) : !source_image_texture_exists(name);
}

bool material_uses_fallback_resources(const Mesh& mesh)
{
    return explicit_texture_reference_missing(mesh.bitmap_name, true)
        || explicit_texture_reference_missing(mesh.normal_map_name, false)
        || explicit_texture_reference_missing(mesh.specular_map_name, false)
        || explicit_texture_reference_missing(mesh.roughness_map_name, false)
        || explicit_texture_reference_missing(mesh.emissive_map_name, false);
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
    if (bitmap_name.empty()) {
        return {};
    }

    auto& cache = texture_analysis_cache();
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
            result.alpha_profile = NwnMaterialAlphaProfile::graded;
        } else {
            result.alpha_profile = has_zero ? NwnMaterialAlphaProfile::binary : NwnMaterialAlphaProfile::opaque;
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

Vertex interpolate_vertex(const Vertex& a, const Vertex& b, const Vertex& c, float wa, float wb, float wc)
{
    Vertex result{};
    result.position = a.position * wa + b.position * wb + c.position * wc;
    result.texcoord = a.texcoord * wa + b.texcoord * wb + c.texcoord * wc;
    result.normal = a.normal * wa + b.normal * wb + c.normal * wc;
    if (glm::length2(result.normal) > 1.0e-10f && finite_vec3(result.normal)) {
        result.normal = glm::normalize(result.normal);
    } else {
        result.normal = glm::vec3{0.0f, 0.0f, 1.0f};
    }

    const glm::vec3 tangent = glm::vec3(a.tangent) * wa + glm::vec3(b.tangent) * wb + glm::vec3(c.tangent) * wc;
    if (glm::length2(tangent) > 1.0e-10f && finite_vec3(tangent)) {
        const auto orthogonal = tangent - result.normal * glm::dot(result.normal, tangent);
        const float source_handedness = (wa >= wb && wa >= wc) ? a.tangent.w : (wb >= wc ? b.tangent.w : c.tangent.w);
        const float handedness = tangent_handedness_or_default(source_handedness);
        if (finite_vec3(orthogonal) && glm::length2(orthogonal) > 1.0e-10f) {
            result.tangent = glm::vec4(glm::normalize(orthogonal), handedness);
        } else {
            result.tangent = glm::vec4{fallback_tangent_for_normal(result.normal), handedness};
        }
    } else {
        result.tangent = glm::vec4{fallback_tangent_for_normal(result.normal), 1.0f};
    }
    return result;
}

void append_subdivided_triangle(const std::array<Vertex, 3>& tri, uint32_t divisions,
    std::vector<Vertex>& out_vertices, std::vector<uint16_t>& out_indices)
{
    const auto vertex_index = [divisions](uint32_t row, uint32_t col) {
        return row * (divisions + 1u) - (row * (row - 1u)) / 2u + col;
    };

    const uint32_t base_index = static_cast<uint32_t>(out_vertices.size());
    for (uint32_t row = 0; row <= divisions; ++row) {
        const uint32_t row_vertices = divisions - row + 1u;
        for (uint32_t col = 0; col < row_vertices; ++col) {
            const float wb = static_cast<float>(col) / static_cast<float>(divisions);
            const float wc = static_cast<float>(row) / static_cast<float>(divisions);
            const float wa = 1.0f - wb - wc;
            out_vertices.push_back(interpolate_vertex(tri[0], tri[1], tri[2], wa, wb, wc));
        }
    }

    const auto push_index = [&](uint32_t local_index) {
        out_indices.push_back(static_cast<uint16_t>(base_index + local_index));
    };

    for (uint32_t row = 0; row < divisions; ++row) {
        const uint32_t row_vertices = divisions - row + 1u;
        for (uint32_t col = 0; col + 1u < row_vertices; ++col) {
            const uint32_t a = vertex_index(row, col);
            const uint32_t b = vertex_index(row, col + 1u);
            const uint32_t c = vertex_index(row + 1u, col);
            push_index(a);
            push_index(b);
            push_index(c);

            if (col + 1u < row_vertices - 1u) {
                const uint32_t d = vertex_index(row + 1u, col + 1u);
                push_index(b);
                push_index(d);
                push_index(c);
            }
        }
    }
}

uint32_t water_subdivision_budget()
{
    static const int32_t budget = [] {
        const char* env = std::getenv("ROLLNW_VIEWER_WATER_SUBDIV_BUDGET");
        if (!env) {
            return 8192;
        }
        const int value = std::atoi(env);
        return value > 0 ? value : 8192;
    }();
    return static_cast<uint32_t>(budget);
}

uint32_t water_subdivision_divisions(float max_edge)
{
    static const int32_t max_div = [] {
        const char* env = std::getenv("ROLLNW_VIEWER_WATER_SUBDIV_MAX");
        if (!env) {
            return 8;
        }
        const int value = std::atoi(env);
        return std::clamp(value, 0, 32);
    }();
    if (max_div <= 0) {
        return 1;
    }
    const uint32_t divisions = static_cast<uint32_t>(std::clamp(std::ceil(max_edge / 1.5f), 1.0f, static_cast<float>(max_div)));
    return divisions;
}

void subdivide_water_mesh(std::vector<Vertex>& vertices, std::vector<uint16_t>& indices)
{
    if (vertices.empty() || indices.size() < 3) {
        return;
    }

    const size_t triangle_count = indices.size() / 3;
    std::vector<uint32_t> divisions_per_triangle;
    divisions_per_triangle.reserve(triangle_count);

    auto triangle_vertex_count = [](uint32_t divisions) {
        return (divisions + 1u) * (divisions + 2u) / 2u;
    };

    size_t estimated_vertices = 0;
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const uint16_t ia = indices[i];
        const uint16_t ib = indices[i + 1];
        const uint16_t ic = indices[i + 2];
        if (ia >= vertices.size() || ib >= vertices.size() || ic >= vertices.size()) {
            divisions_per_triangle.push_back(1);
            estimated_vertices += triangle_vertex_count(1);
            continue;
        }

        const std::array<Vertex, 3> tri{vertices[ia], vertices[ib], vertices[ic]};
        const float max_edge = std::max({
            glm::length(tri[1].position - tri[0].position),
            glm::length(tri[2].position - tri[1].position),
            glm::length(tri[0].position - tri[2].position),
        });
        const uint32_t divisions = water_subdivision_divisions(max_edge);
        divisions_per_triangle.push_back(divisions);
        estimated_vertices += triangle_vertex_count(divisions);
    }

    const uint32_t budget = water_subdivision_budget();
    uint32_t allowed_max = 32;
    while (estimated_vertices > budget && allowed_max > 1) {
        allowed_max /= 2;
        estimated_vertices = 0;
        for (auto& divisions : divisions_per_triangle) {
            divisions = std::min(divisions, allowed_max);
            estimated_vertices += triangle_vertex_count(divisions);
        }
    }

    std::vector<Vertex> subdivided_vertices;
    std::vector<uint16_t> subdivided_indices;
    subdivided_vertices.reserve(std::min(estimated_vertices, static_cast<size_t>(budget)));
    subdivided_indices.reserve(indices.size() * 8);

    size_t triangle_index = 0;
    for (size_t i = 0; i + 2 < indices.size(); i += 3, ++triangle_index) {
        const uint16_t ia = indices[i];
        const uint16_t ib = indices[i + 1];
        const uint16_t ic = indices[i + 2];
        if (ia >= vertices.size() || ib >= vertices.size() || ic >= vertices.size()) {
            continue;
        }
        const std::array<Vertex, 3> tri{vertices[ia], vertices[ib], vertices[ic]};
        append_subdivided_triangle(tri, divisions_per_triangle[triangle_index], subdivided_vertices, subdivided_indices);
    }

    if (!subdivided_vertices.empty() && !subdivided_indices.empty()) {
        vertices = std::move(subdivided_vertices);
        indices = std::move(subdivided_indices);
    }
}

MaterialMode classify_nwn_material_impl(const NwnMaterialClassificationInput& input)
{
    if (looks_like_water_material(input.node, input.bitmap_name, input.model_class)) {
        return MaterialMode::water;
    }

    const bool web_texture = looks_like_web_texture(input.bitmap_name);
    if (input.has_txi) {
        if (input.txi_blending == "punchthrough") {
            return MaterialMode::cutout;
        }
        if (input.txi_blending == "lighten") {
            return MaterialMode::cutout;
        }
        if (input.txi_blending == "additive") {
            return MaterialMode::transparent;
        }
        if (input.txi_blending == "normal") {
            if (input.alpha_profile == NwnMaterialAlphaProfile::binary) {
                return MaterialMode::cutout;
            }
            if (input.alpha_profile == NwnMaterialAlphaProfile::graded) {
                return MaterialMode::transparent;
            }
        }
        if (input.txi_decal && input.alpha_profile != NwnMaterialAlphaProfile::opaque) {
            return MaterialMode::cutout;
        }
    }

    if (web_texture && (input.alpha_profile != NwnMaterialAlphaProfile::opaque || input.has_color_key)) {
        return MaterialMode::cutout;
    }

    if (input.has_color_key && input.alpha_profile == NwnMaterialAlphaProfile::opaque) {
        switch (input.model_class) {
        case nwm::ModelClass::tile:
            break;
        case nwm::ModelClass::effect:
        case nwm::ModelClass::gui:
        case nwm::ModelClass::invalid:
        case nwm::ModelClass::character:
        case nwm::ModelClass::door:
        case nwm::ModelClass::item:
        default:
            return MaterialMode::cutout;
        }
    }

    if (input.node) {
        if (mesh_alpha_value(input.node) < 0.999f) {
            return MaterialMode::transparent;
        }
        const auto renderhint = ascii_lower(input.node->renderhint);
        const auto materialname = ascii_lower(input.node->materialname);
        if (renderhint.find("additive") != std::string::npos
            || renderhint.find("trans") != std::string::npos
            || materialname.find("add") != std::string::npos) {
            return MaterialMode::transparent;
        }
        if (input.node->transparencyhint > 0) {
            switch (input.alpha_profile) {
            case NwnMaterialAlphaProfile::binary:
                return MaterialMode::cutout;
            case NwnMaterialAlphaProfile::graded:
                return MaterialMode::transparent;
            case NwnMaterialAlphaProfile::opaque:
            default:
                return MaterialMode::opaque;
            }
        }
    }

    switch (input.alpha_profile) {
    case NwnMaterialAlphaProfile::binary:
        return MaterialMode::cutout;
    case NwnMaterialAlphaProfile::graded:
        switch (input.model_class) {
        case nwm::ModelClass::effect:
        case nwm::ModelClass::gui:
            return MaterialMode::transparent;
        case nwm::ModelClass::tile:
        case nwm::ModelClass::invalid:
        case nwm::ModelClass::character:
        case nwm::ModelClass::door:
        case nwm::ModelClass::item:
        default:
            return MaterialMode::opaque;
        }
    case NwnMaterialAlphaProfile::opaque:
    default:
        return MaterialMode::opaque;
    }
}

MaterialMode classify_material(const nwm::TrimeshNode* node, std::string_view bitmap_name,
    nwm::ModelClass model_class)
{
    const auto texture = analyze_texture(bitmap_name);
    const auto txi = load_txi_material_info(bitmap_name);
    return classify_nwn_material_impl(NwnMaterialClassificationInput{
        .node = node,
        .bitmap_name = bitmap_name,
        .model_class = model_class,
        .alpha_profile = texture.alpha_profile,
        .has_color_key = texture.has_color_key,
        .has_txi = txi.has_txi,
        .txi_blending = txi.blending,
        .txi_decal = txi.decal,
    });
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
    mesh.alpha_cutout_threshold = 0.5f;

    const auto mtr = load_mtr_material_info(node);
    if (mtr.renderhint) {
        mesh.renderhint = *mtr.renderhint;
    }
    if (mtr.diffuse_texture) {
        mesh.bitmap_name = *mtr.diffuse_texture;
    }
    mesh.normal_map_name = mtr.normal_texture.value_or(std::string{});
    mesh.specular_map_name = mtr.specular_texture.value_or(std::string{});
    mesh.roughness_map_name = mtr.roughness_texture.value_or(std::string{});
    mesh.emissive_map_name = mtr.emissive_texture.value_or(std::string{});
    mesh.material_uses_fallback = material_uses_fallback_resources(mesh);

    const bool water_material = looks_like_water_material(node, mesh.bitmap_name, model_class);
    const auto* dangly_node = dynamic_cast<const nwm::DanglymeshNode*>(node);
    const bool foliage_dangly = dangly_node
        && dangly_deform_policy_for(dangly_node) == DanglyDeformPolicy::foliage_sway_cpu;
    auto txi = load_txi_material_info(mesh.bitmap_name);
    TextureAnalysis texture{};
    if (!water_material) {
        texture = analyze_texture(mesh.bitmap_name);
        if (txi.has_txi && txi.alphamean > 0.0f && txi.alphamean < 1.0f) {
            mesh.alpha_cutout_threshold = txi.alphamean;
        } else if ((foliage_dangly || looks_like_foliage_texture(mesh.bitmap_name))
            && texture.alpha_profile == NwnMaterialAlphaProfile::graded
            && texture.alpha_coverage_50 - texture.alpha_coverage_75 < 0.03f) {
            // Mostly-binary foliage alpha benefits from a stronger cutout threshold.
            mesh.alpha_cutout_threshold = 0.75f;
        }
        if (txi.blending == "lighten") {
            mesh.color_key = glm::vec3(0.0f);
            mesh.color_key_threshold = 0.18f;
        } else if (texture.alpha_profile == NwnMaterialAlphaProfile::opaque) {
            mesh.color_key = texture.color_key;
            mesh.color_key_threshold = texture.color_key_threshold;
        }
    }
    mesh.material_mode = water_material ? MaterialMode::water : classify_material(node, mesh.bitmap_name, model_class);
    if (!water_material
        && foliage_dangly
        && texture.alpha_profile != NwnMaterialAlphaProfile::opaque
        && mesh.material_mode == MaterialMode::opaque) {
        mesh.material_mode = MaterialMode::cutout;
    }
    mesh.roughness = nwn_shininess_to_legacy_roughness(node->shininess);
    mesh.specular_strength = nwn_specular_to_legacy_strength(node->specular);
    // Do not translate legacy Blinn shininess directly into the common PBR
    // asset. Diffuse-only NWN meshes use a neutral roughness; authored MTR
    // roughness maps/scalars keep explicit PBR intent.
    mesh.common_pbr_roughness = !mesh.roughness_map_name.empty() ? 1.0f : kLegacyDefaultRoughness;
    if (mtr.roughness) {
        mesh.roughness = *mtr.roughness;
        mesh.common_pbr_roughness = *mtr.roughness;
    }
    if (mtr.specular_strength) {
        mesh.specular_strength = *mtr.specular_strength;
    }
    if (mtr.alpha_cutout_threshold) {
        mesh.alpha_cutout_threshold = *mtr.alpha_cutout_threshold;
    }
    if (mtr.transparency && *mtr.transparency) {
        mesh.material_mode = MaterialMode::transparent;
    }
    if (mtr.two_sided) {
        mesh.two_sided_lighting = *mtr.two_sided;
    }

    mesh.emissive = mesh_self_illum_value(node);
    if (mtr.emissive) {
        mesh.emissive = *mtr.emissive;
    }
    if (!mesh.emissive_map_name.empty() && glm::length2(mesh.emissive) < 1.0e-6f) {
        mesh.emissive = glm::vec3(1.0f);
    }
    if (glm::length2(mesh.emissive) < 1.0e-6f) {
        const auto renderhint_lower = ascii_lower(mesh.renderhint);
        const auto materialname_lower = ascii_lower(mesh.materialname);
        const bool additive_renderhint = renderhint_lower.find("additive") != std::string::npos
            || materialname_lower.find("add") != std::string::npos;
        const bool additive_txi = txi.blending == "additive";
        if (additive_renderhint || additive_txi) {
            // NWN additive materials are self-illuminated and should not be lit only by scene lights.
            mesh.emissive = glm::vec3(1.0f);
        }
    }

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
    mesh.local_bounds = Bounds{.min = min_v, .max = max_v};
    const glm::vec3 span = max_v - min_v;
    const float horizontal_span = std::max(span.x, span.y);
    mesh.is_ground_overlay = (mesh.material_mode == MaterialMode::transparent
                                 || mesh.material_mode == MaterialMode::water)
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

uint32_t saturated_model_asset_import_count(size_t count) noexcept
{
    return count > std::numeric_limits<uint32_t>::max()
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(count);
}

uint32_t saturated_model_asset_import_add(uint32_t lhs, size_t rhs) noexcept
{
    const auto max = std::numeric_limits<uint32_t>::max();
    return rhs >= static_cast<size_t>(max - lhs)
        ? max
        : lhs + static_cast<uint32_t>(rhs);
}

glm::mat4 source_node_local_transform(const nwm::Node& node)
{
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};

    const auto pos = node.get_controller(nwm::ControllerType::Position, false);
    if (pos.data.size() >= 3) {
        position = glm::vec3{pos.data[0], pos.data[1], pos.data[2]};
    }

    const auto ori = node.get_controller(nwm::ControllerType::Orientation, false);
    if (ori.data.size() >= 4) {
        rotation = glm::quat{ori.data[3], ori.data[0], ori.data[1], ori.data[2]};
    }

    const auto scale_value = node.get_controller(nwm::ControllerType::Scale, false);
    if (!scale_value.data.empty()) {
        if (scale_value.data.size() >= 3) {
            scale = glm::vec3{scale_value.data[0], scale_value.data[1], scale_value.data[2]};
        } else {
            scale = glm::vec3{scale_value.data[0]};
        }
    }

    auto result = glm::translate(glm::mat4{1.0f}, position);
    result *= glm::toMat4(rotation);
    return glm::scale(result, scale);
}

void initialize_nwn_model_asset_nodes(const nwm::Model& model, nw::render::ModelAsset& asset)
{
    asset.nodes.resize(model.nodes.size());
    std::unordered_map<const nwm::Node*, uint32_t> source_indices;
    source_indices.reserve(model.nodes.size());
    for (size_t i = 0; i < model.nodes.size(); ++i) {
        source_indices.emplace(model.nodes[i].get(), static_cast<uint32_t>(i));
    }

    for (size_t i = 0; i < model.nodes.size(); ++i) {
        const auto* source = model.nodes[i].get();
        auto& node = asset.nodes[i];
        node.local_transform = source_node_local_transform(*source);
        node.world_transform = node.local_transform;
        node.parent = -1;
        if (source->parent) {
            const auto it = source_indices.find(source->parent);
            if (it != source_indices.end() && it->second <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
                node.parent = static_cast<int32_t>(it->second);
            }
        }
    }

    std::vector<uint8_t> visited(asset.nodes.size(), 0);
    std::vector<uint8_t> visiting(asset.nodes.size(), 0);
    std::function<const glm::mat4&(size_t)> compute_world = [&](size_t index) -> const glm::mat4& {
        auto& node = asset.nodes[index];
        if (visited[index]) {
            return node.world_transform;
        }
        if (visiting[index]) {
            node.parent = -1;
            node.world_transform = node.local_transform;
            visited[index] = 1;
            return node.world_transform;
        }

        visiting[index] = 1;
        if (node.parent >= 0 && static_cast<size_t>(node.parent) < asset.nodes.size()) {
            const auto& parent_world = compute_world(static_cast<size_t>(node.parent));
            node.world_transform = parent_world * node.local_transform;
        } else {
            node.parent = -1;
            node.world_transform = node.local_transform;
        }
        visiting[index] = 0;
        visited[index] = 1;
        return node.world_transform;
    };

    for (size_t i = 0; i < asset.nodes.size(); ++i) {
        compute_world(i);
    }
}

void append_nwn_model_asset_sockets(const nwm::Model& model, nw::render::ModelAsset& asset)
{
    asset.sockets.reserve(model.nodes.size());
    for (size_t i = 0; i < model.nodes.size() && i < asset.nodes.size(); ++i) {
        const auto* source = model.nodes[i].get();
        if (!source || source->name.empty() || i >= nw::render::kInvalidModelNodeIndex) {
            continue;
        }

        if (source->type == nwm::NodeType::dummy) {
            append_model_asset_socket_if_missing(asset, i, std::string_view{source->name});
        }

        // NWN single-body creatures sometimes expose equipment anchors as named
        // mesh nodes such as rhand_g/lhand_g instead of dummy nodes. Lower those
        // source-specific names into the common socket table here so runtime
        // attachment binding stays index-based.
        append_model_asset_socket_if_missing(asset, i, nwn_equipped_item_socket_alias(source->name));
    }
}

void append_nwn_model_asset_animations(const nwm::Mdl& mdl, nw::render::ModelAsset& asset)
{
    if (mdl.model.nodes.empty() || asset.skeletons.size() >= std::numeric_limits<uint32_t>::max()) {
        return;
    }

    bool has_animations = false;
    for (const nwm::Mdl* source = &mdl; source; source = source->model.supermodel.get()) {
        has_animations = has_animations || !source->model.animations.empty();
    }
    if (!has_animations) {
        return;
    }

    // Bridge phase: the common asset owns one mesh-source skeleton keyed by
    // source node index. Clips from the source model and its supermodel chain
    // are imported against that skeleton. Missing node tracks stay as explicit
    // bind-pose rows during sampling; no renderer policy is inferred from
    // source model names.
    std::vector<int32_t> joint_to_source_node;
    auto skeleton = build_nwn_skeleton(mdl, joint_to_source_node, mdl.model.name);
    if (skeleton.joints.empty()) {
        return;
    }

    const uint32_t skeleton_index = static_cast<uint32_t>(asset.skeletons.size());
    asset.skeletons.push_back(std::move(skeleton));
    const auto& asset_skeleton = asset.skeletons.back();
    const size_t animation_count_before = asset.animations.size();
    std::vector<std::string> imported_clip_names;
    for (const nwm::Mdl* source = &mdl; source; source = source->model.supermodel.get()) {
        for (const auto& animation : source->model.animations) {
            if (!animation) {
                continue;
            }

            const std::string clip_name = animation->name.c_str();
            if (!clip_name.empty()
                && std::find(imported_clip_names.begin(), imported_clip_names.end(), clip_name)
                    != imported_clip_names.end()) {
                continue;
            }

            asset.animations.push_back(build_nwn_clip(*animation, asset_skeleton, skeleton_index));
            if (!clip_name.empty()) {
                imported_clip_names.push_back(clip_name);
            }
        }
    }

    if (asset.animations.size() == animation_count_before) {
        asset.skeletons.pop_back();
    }
}

bool particle_curve_has_keys(const nw::render::ParticleCurveF32& curve) noexcept
{
    return !curve.keys.empty();
}

bool particle_gradient_has_keys(const nw::render::ParticleGradient& gradient) noexcept
{
    return !gradient.keys.empty();
}

bool particle_spawn_over_time_has_keys(const nw::render::ParticleSpawnOverTimeDef& spawn) noexcept
{
    return particle_curve_has_keys(spawn.alpha_start)
        || particle_curve_has_keys(spawn.alpha_end)
        || particle_curve_has_keys(spawn.lifetime)
        || particle_curve_has_keys(spawn.speed)
        || particle_curve_has_keys(spawn.speed_random)
        || particle_curve_has_keys(spawn.mass)
        || particle_curve_has_keys(spawn.rotation_rate)
        || particle_curve_has_keys(spawn.spread)
        || particle_curve_has_keys(spawn.sheet_frame_begin)
        || particle_curve_has_keys(spawn.sheet_frame_end)
        || particle_curve_has_keys(spawn.sheet_fps)
        || particle_curve_has_keys(spawn.sheet_random_start)
        || particle_curve_has_keys(spawn.size_start_x)
        || particle_curve_has_keys(spawn.size_end_x)
        || particle_curve_has_keys(spawn.size_start_y)
        || particle_curve_has_keys(spawn.size_end_y)
        || particle_gradient_has_keys(spawn.color_start)
        || particle_gradient_has_keys(spawn.color_end);
}

bool particle_import_has_animation_payload(const nwm::ParticleImportResult& import) noexcept
{
    if (!import.effect_events.empty()) {
        return true;
    }
    for (const auto& emitter : import.effect.emitters) {
        if (particle_curve_has_keys(emitter.emission.rate_over_time)
            || particle_spawn_over_time_has_keys(emitter.spawn_over_time)) {
            return true;
        }
    }
    return false;
}

float nwn_particle_animation_length(const nwm::Animation& animation) noexcept
{
    return std::isfinite(animation.length) && animation.length > 0.0f ? animation.length : 0.0f;
}

std::vector<nw::render::ModelAssetParticleEvent> nwn_model_asset_particle_events(
    const std::vector<nwm::ParticleImportEffectEvent>& source)
{
    std::vector<nw::render::ModelAssetParticleEvent> events;
    events.reserve(source.size());
    for (const auto& event : source) {
        events.push_back({
            .time = event.time,
            .burst_count = event.burst_count,
        });
    }
    return events;
}

void append_nwn_model_asset_particle_system(
    nwm::ParticleImportResult import,
    std::string_view animation_name,
    float animation_length,
    nw::render::ModelAsset& asset,
    NwnModelAssetImportStats& stats)
{
    stats.particle_import_warning_count = saturated_model_asset_import_add(
        stats.particle_import_warning_count, import.warnings.size());

    if (import.effect.emitters.empty()) {
        return;
    }
    if (asset.particle_systems.size() >= std::numeric_limits<uint32_t>::max()) {
        ++stats.particle_system_overflow_count;
        return;
    }

    nw::render::ModelAssetParticleSystem particles{};
    particles.name = import.effect.name.empty() ? asset.name : import.effect.name;
    particles.animation_name = std::string{animation_name};
    particles.effect = std::move(import.effect);
    particles.effect_events = nwn_model_asset_particle_events(import.effect_events);
    particles.animation_length = animation_length;

    stats.particle_event_count = saturated_model_asset_import_add(
        stats.particle_event_count, particles.effect_events.size());
    asset.particle_systems.push_back(std::move(particles));
}

void append_nwn_model_asset_particles(const nwm::Mdl& mdl, nw::render::ModelAsset& asset, NwnModelAssetImportStats& stats)
{
    auto base_import = nwm::import_particle_effect(mdl, {}, false);
    if (base_import.effect.emitters.empty()) {
        stats.particle_import_warning_count = saturated_model_asset_import_add(
            stats.particle_import_warning_count, base_import.warnings.size());
        return;
    }

    append_nwn_model_asset_particle_system(std::move(base_import), {}, 0.0f, asset, stats);

    std::vector<std::string> imported_animation_names;
    imported_animation_names.reserve(mdl.model.animations.size());
    for (const auto& animation : mdl.model.animations) {
        if (!animation || animation->name.empty()) {
            continue;
        }

        const std::string animation_name = animation->name.c_str();
        if (std::find(imported_animation_names.begin(), imported_animation_names.end(), animation_name)
            != imported_animation_names.end()) {
            continue;
        }
        imported_animation_names.push_back(animation_name);

        auto animation_import = nwm::import_particle_effect(mdl, animation_name, false);
        if (!particle_import_has_animation_payload(animation_import)) {
            stats.particle_import_warning_count = saturated_model_asset_import_add(
                stats.particle_import_warning_count, animation_import.warnings.size());
            continue;
        }

        append_nwn_model_asset_particle_system(
            std::move(animation_import),
            animation_name,
            nwn_particle_animation_length(*animation),
            asset,
            stats);
    }
}

bool texture_resource_is_plt(std::string_view name)
{
    return !name.empty() && nw::kernel::resman().contains({nw::Resref{name}, nw::ResourceType::plt});
}

uint32_t append_nwn_model_asset_texture_source(std::string_view raw_name,
    std::unordered_map<std::string, uint32_t>& texture_source_indices,
    nw::render::ModelAsset& asset,
    NwnModelAssetImportStats& stats)
{
    const auto name = clean_mtr_resource_name(raw_name);
    if (name.empty()) {
        return nw::render::kInvalidModelAssetTextureSourceIndex;
    }

    const auto existing = texture_source_indices.find(name);
    if (existing != texture_source_indices.end()) {
        return existing->second;
    }

    auto data = nw::kernel::resman().demand_in_order(
        nw::Resref{name}, {nw::ResourceType::dds, nw::ResourceType::tga, nw::ResourceType::plt});
    if (data.bytes.size() == 0) {
        if (texture_resource_is_plt(name)) {
            ++stats.unsupported_plt_texture_count;
        } else {
            ++stats.missing_texture_source_count;
        }
        return nw::render::kInvalidModelAssetTextureSourceIndex;
    }
    if (asset.texture_sources.size() >= nw::render::kInvalidModelAssetTextureSourceIndex) {
        ++stats.texture_source_overflow_count;
        return nw::render::kInvalidModelAssetTextureSourceIndex;
    }

    nw::render::ModelAssetTextureSource source{};
    source.kind = nw::render::ModelAssetTextureSourceKind::encoded_bytes;
    source.resource = data.name;
    source.encoded_bytes.assign(data.bytes.data(), data.bytes.data() + data.bytes.size());

    const uint32_t index = static_cast<uint32_t>(asset.texture_sources.size());
    asset.texture_sources.push_back(std::move(source));
    texture_source_indices.emplace(name, index);
    return index;
}

nw::render::ModelAssetMaterialTextureSources append_nwn_model_asset_material_texture_sources(const Mesh& mesh,
    std::unordered_map<std::string, uint32_t>& texture_source_indices,
    nw::render::ModelAsset& asset,
    NwnModelAssetImportStats& stats)
{
    nw::render::ModelAssetMaterialTextureSources sources{};
    sources.albedo = append_nwn_model_asset_texture_source(mesh.bitmap_name, texture_source_indices, asset, stats);
    sources.normal = append_nwn_model_asset_texture_source(mesh.normal_map_name, texture_source_indices, asset, stats);
    sources.metallic_roughness = append_nwn_model_asset_texture_source(
        mesh.roughness_map_name, texture_source_indices, asset, stats);
    sources.emissive = append_nwn_model_asset_texture_source(mesh.emissive_map_name, texture_source_indices, asset, stats);
    if (!clean_mtr_resource_name(mesh.specular_map_name).empty()) {
        ++stats.unsupported_specular_texture_count;
    }
    return sources;
}

nw::render::Material nwn_model_asset_material_from_mesh(const Mesh& mesh)
{
    nw::render::Material material{};
    material.albedo = glm::vec4{1.0f, 1.0f, 1.0f, mesh.opacity};
    material.roughness = mesh.common_pbr_roughness;
    material.specular_strength = std::clamp(mesh.specular_strength, 0.0f, 1.0f);
    material.emissive = mesh.emissive;
    material.material_uses_fallback = mesh.material_uses_fallback;
    material.alpha_mode = mesh.material_mode;
    material.alpha_cutoff = mesh.alpha_cutout_threshold;
    material.color_key_threshold = glm::vec4{
        mesh.color_key.x,
        mesh.color_key.y,
        mesh.color_key.z,
        mesh.color_key_threshold,
    };
    material.double_sided = mesh.two_sided_lighting;
    return material;
}

bool model_asset_source_is_plt(const nw::render::ModelAsset& asset, uint32_t source_index)
{
    return source_index != nw::render::kInvalidModelAssetTextureSourceIndex
        && source_index < asset.texture_sources.size()
        && asset.texture_sources[source_index].resource.type == nw::ResourceType::plt;
}

void append_nwn_model_asset_material(
    const Mesh& mesh,
    std::unordered_map<std::string, uint32_t>& texture_source_indices,
    nw::render::ModelAsset& asset,
    NwnModelAssetImportStats& stats)
{
    auto material = nwn_model_asset_material_from_mesh(mesh);
    auto sources = append_nwn_model_asset_material_texture_sources(mesh, texture_source_indices, asset, stats);
    material.albedo_uses_plt = model_asset_source_is_plt(asset, sources.albedo);
    asset.materials.push_back(std::move(material));
    asset.material_texture_sources.push_back(sources);
}

std::vector<Vertex> build_nwn_model_asset_vertices(
    const nwm::TrimeshNode* node,
    const Mesh& mesh,
    NwnModelAssetImportStats& stats)
{
    std::vector<Vertex> vertices;
    const auto* dangly = dynamic_cast<const DanglyMesh*>(&mesh);
    if (dangly && dangly->cpu_vertices_.size() == node->vertices.size()) {
        vertices = dangly->cpu_vertices_;
    } else {
        vertices.reserve(node->vertices.size());
        for (const auto& vertex : node->vertices) {
            vertices.push_back(convert_vertex(vertex));
        }
    }

    if (has_invalid_normals(vertices)) {
        ++stats.normal_repair_count;
        recompute_static_vertex_normals(node, vertices);
    }
    if (has_invalid_tangents(vertices)) {
        ++stats.tangent_repair_count;
        recompute_vertex_tangents(node, vertices);
    }

    return vertices;
}

constexpr uint8_t kInvalidSkinSlotRemap = std::numeric_limits<uint8_t>::max();

bool build_nwn_model_asset_skin(
    const nwm::SkinNode* source,
    uint32_t source_node_index,
    const nw::render::ModelAsset& asset,
    nw::render::Skin& out_skin,
    std::array<uint8_t, nw::render::kModelMaxSkinBones>& out_slot_remap)
{
    out_skin = {};
    out_skin.skeleton = 0;
    out_slot_remap.fill(kInvalidSkinSlotRemap);
    if (!source || source_node_index >= asset.nodes.size()) {
        return false;
    }

    std::array<uint8_t, nw::render::kModelMaxSkinBones> used_slots{};
    for (const auto& vertex : source->vertices) {
        for (int lane = 0; lane < 4; ++lane) {
            if (vertex.weights[lane] <= 0.0f) {
                continue;
            }
            const int32_t source_slot = vertex.bones[lane];
            if (source_slot < 0 || static_cast<size_t>(source_slot) >= source->bone_nodes.size()) {
                return false;
            }
            const int16_t source_bone = source->bone_nodes[static_cast<size_t>(source_slot)];
            if (source_bone < 0 || static_cast<size_t>(source_bone) >= asset.nodes.size()) {
                return false;
            }
            used_slots[static_cast<size_t>(source_slot)] = 1u;
        }
    }

    const glm::mat4& mesh_bind = asset.nodes[source_node_index].world_transform;
    for (size_t source_slot = 0; source_slot < used_slots.size(); ++source_slot) {
        if (used_slots[source_slot] == 0u) {
            continue;
        }
        if (!nw::render::model_skin_bone_count_supported(out_skin.joints.size() + 1u)) {
            return false;
        }

        const int16_t source_bone = source->bone_nodes[source_slot];
        if (source_bone < 0 || static_cast<size_t>(source_bone) >= asset.nodes.size()) {
            return false;
        }

        out_slot_remap[source_slot] = static_cast<uint8_t>(out_skin.joints.size());
        out_skin.joints.push_back(static_cast<int32_t>(source_bone));
        out_skin.inverse_bind_matrices.push_back(
            glm::inverse(asset.nodes[static_cast<size_t>(source_bone)].world_transform) * mesh_bind);
    }

    return !out_skin.joints.empty();
}

nw::render::SkinnedVertex build_nwn_model_asset_skin_vertex(
    const nwm::SkinVertex& source,
    const std::array<uint8_t, nw::render::kModelMaxSkinBones>& slot_remap)
{
    glm::ivec4 joints{0};
    glm::vec4 weights{0.0f};
    for (int lane = 0; lane < 4; ++lane) {
        const float weight = source.weights[lane];
        const int32_t source_slot = source.bones[lane];
        if (weight <= 0.0f || source_slot < 0 || static_cast<size_t>(source_slot) >= slot_remap.size()) {
            continue;
        }
        const uint8_t remapped = slot_remap[static_cast<size_t>(source_slot)];
        if (remapped == kInvalidSkinSlotRemap) {
            continue;
        }
        joints[lane] = static_cast<int32_t>(remapped);
        weights[lane] = std::clamp(weight, 0.0f, 1.0f);
    }

    return nw::render::SkinnedVertex{
        .position = source.position,
        .normal = source.normal,
        .texcoord = source.tex_coords,
        .tangent = source.tangent,
        .joint_indices = pack_u8x4(joints),
        .joint_weights = pack_unorm8x4(weights),
    };
}

std::vector<nw::render::SkinnedVertex> build_nwn_model_asset_skinned_vertices(
    const nwm::SkinNode* source,
    const std::array<uint8_t, nw::render::kModelMaxSkinBones>& slot_remap,
    NwnModelAssetImportStats& stats)
{
    std::vector<nw::render::SkinnedVertex> vertices;
    if (!source) {
        return vertices;
    }

    vertices.reserve(source->vertices.size());
    for (const auto& vertex : source->vertices) {
        vertices.push_back(build_nwn_model_asset_skin_vertex(vertex, slot_remap));
    }

    if (has_invalid_normals(vertices)) {
        ++stats.normal_repair_count;
        recompute_static_vertex_normals_for_indices(source->indices, vertices);
    }
    if (has_invalid_tangents(vertices)) {
        ++stats.tangent_repair_count;
        recompute_vertex_tangents_for_indices(source->indices, vertices);
    }

    return vertices;
}

std::vector<uint32_t> nwn_model_asset_u32_indices(const std::vector<uint16_t>& source)
{
    std::vector<uint32_t> result;
    result.reserve(source.size());
    for (const uint16_t index : source) {
        result.push_back(index);
    }
    return result;
}

template <typename TVertex>
nw::render::Bounds transformed_vertex_bounds(const std::vector<TVertex>& vertices, const glm::mat4& transform)
{
    nw::render::Bounds bounds{};
    bool first = true;
    for (const auto& vertex : vertices) {
        expand_bounds(bounds, transform_point(transform, vertex.position), first);
        first = false;
    }
    return bounds;
}

uint32_t append_nwn_model_asset_deformer(const DanglyMesh* dangly,
    uint32_t source_node_index,
    nw::render::ModelAsset& asset,
    NwnModelAssetImportStats& stats)
{
    if (!dangly || dangly->rest_positions_.empty() || source_node_index >= nw::render::kInvalidModelNodeIndex) {
        return nw::render::kInvalidModelDeformerIndex;
    }
    if (asset.deformers.size() >= nw::render::kInvalidModelDeformerIndex) {
        ++stats.deformer_overflow_count;
        return nw::render::kInvalidModelDeformerIndex;
    }

    const uint32_t index = static_cast<uint32_t>(asset.deformers.size());
    auto deformer = dangly->make_deformer_record(source_node_index);
    switch (deformer.kind) {
    case nw::render::ModelDeformerKind::secondary_motion_chain:
        ++stats.secondary_motion_deformer_count;
        break;
    case nw::render::ModelDeformerKind::legacy_reference_cpu:
        ++stats.legacy_reference_deformer_count;
        break;
    case nw::render::ModelDeformerKind::vertex_shader_sway:
    case nw::render::ModelDeformerKind::gpu_vertex_sim:
        break;
    }
    asset.deformers.push_back(std::move(deformer));
    return index;
}

void merge_nwn_model_asset_bounds(nw::render::ModelAsset& asset)
{
    bool first = true;
    for (const auto& primitive : asset.primitives) {
        if (first) {
            asset.bounds = primitive.bounds;
            first = false;
        } else {
            asset.bounds.min = glm::min(asset.bounds.min, primitive.bounds.min);
            asset.bounds.max = glm::max(asset.bounds.max, primitive.bounds.max);
        }
    }
}

bool append_nwn_model_asset_primitive(const nwm::TrimeshNode* source,
    Mesh& mesh,
    uint32_t source_node_index,
    std::unordered_map<std::string, uint32_t>& texture_source_indices,
    nw::render::ModelAsset& asset,
    NwnModelAssetImportStats& stats)
{
    if (!source || source->vertices.empty() || source->indices.empty() || source_node_index >= asset.nodes.size()) {
        ++stats.skipped_empty_mesh_count;
        return false;
    }
    if (source_node_index > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
        ++stats.primitive_overflow_count;
        return false;
    }
    if (asset.materials.size() >= std::numeric_limits<uint32_t>::max()
        || asset.primitives.size() >= std::numeric_limits<uint32_t>::max()) {
        ++stats.primitive_overflow_count;
        return false;
    }

    auto vertices = build_nwn_model_asset_vertices(source, mesh, stats);
    std::vector<uint16_t> indices16 = source->indices;
    inset_transparent_subrect_uvs(mesh, vertices);
    if (mesh.material_mode == MaterialMode::water) {
        subdivide_water_mesh(vertices, indices16);
    }

    nw::render::ModelAssetPrimitive primitive{};
    primitive.vertices = std::move(vertices);
    primitive.indices = nwn_model_asset_u32_indices(indices16);
    primitive.material = static_cast<uint32_t>(asset.materials.size());
    primitive.node = static_cast<int32_t>(source_node_index);
    primitive.casts_shadow = should_register_shadow_caster(source, mesh);
    primitive.transform = asset.nodes[source_node_index].world_transform;
    primitive.bounds = transformed_vertex_bounds(primitive.vertices, primitive.transform);
    primitive.deformer = append_nwn_model_asset_deformer(
        dynamic_cast<const DanglyMesh*>(&mesh), source_node_index, asset, stats);

    append_nwn_model_asset_material(mesh, texture_source_indices, asset, stats);
    asset.primitives.push_back(std::move(primitive));
    return true;
}

bool append_nwn_model_asset_skin_primitive(const nwm::SkinNode* source,
    Mesh& mesh,
    uint32_t source_node_index,
    std::unordered_map<std::string, uint32_t>& texture_source_indices,
    nw::render::ModelAsset& asset,
    NwnModelAssetImportStats& stats)
{
    if (!source || source->vertices.empty() || source->indices.empty() || source_node_index >= asset.nodes.size()) {
        ++stats.skipped_empty_mesh_count;
        return false;
    }
    if (source_node_index > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
        ++stats.primitive_overflow_count;
        return false;
    }
    if (asset.materials.size() >= std::numeric_limits<uint32_t>::max()
        || asset.primitives.size() >= std::numeric_limits<uint32_t>::max()
        || asset.skins.size() >= std::numeric_limits<uint32_t>::max()) {
        ++stats.primitive_overflow_count;
        return false;
    }

    nw::render::Skin skin{};
    std::array<uint8_t, nw::render::kModelMaxSkinBones> slot_remap{};
    if (!build_nwn_model_asset_skin(source, source_node_index, asset, skin, slot_remap)) {
        return false;
    }

    auto vertices = build_nwn_model_asset_skinned_vertices(source, slot_remap, stats);
    inset_transparent_subrect_uvs(mesh, vertices);

    nw::render::ModelAssetPrimitive primitive{};
    primitive.skinned_vertices = std::move(vertices);
    primitive.indices = nwn_model_asset_u32_indices(source->indices);
    primitive.material = static_cast<uint32_t>(asset.materials.size());
    primitive.skin = static_cast<uint32_t>(asset.skins.size());
    primitive.node = static_cast<int32_t>(source_node_index);
    primitive.skinned = true;
    primitive.casts_shadow = should_register_shadow_caster(source, mesh);
    primitive.transform = asset.nodes[source_node_index].world_transform;
    primitive.bounds = transformed_vertex_bounds(primitive.skinned_vertices, primitive.transform);

    asset.skins.push_back(std::move(skin));
    append_nwn_model_asset_material(mesh, texture_source_indices, asset, stats);
    asset.primitives.push_back(std::move(primitive));
    return true;
}

void append_nwn_model_asset_meshes(const nwm::Model& model, nw::render::ModelAsset& asset, NwnModelAssetImportStats& stats)
{
    std::unordered_map<std::string, uint32_t> texture_source_indices;
    texture_source_indices.reserve(model.nodes.size());

    for (size_t i = 0; i < model.nodes.size(); ++i) {
        const auto* source_node = model.nodes[i].get();
        if (!source_node) {
            continue;
        }

        if (source_node->type & nwm::NodeFlags::skin) {
            const auto* skin = static_cast<const nwm::SkinNode*>(source_node);
            if (should_create_mesh_node(skin, model.classification)) {
                Mesh mesh;
                initialize_mesh_material(mesh, skin, model.classification);
                if (!append_nwn_model_asset_skin_primitive(
                        skin, mesh, static_cast<uint32_t>(i), texture_source_indices, asset, stats)) {
                    ++stats.skipped_skin_mesh_count;
                }
            }
            continue;
        }

        if (!(source_node->type & nwm::NodeFlags::mesh) || (source_node->type & nwm::NodeFlags::aabb)) {
            continue;
        }

        const auto* trimesh = static_cast<const nwm::TrimeshNode*>(source_node);
        if (!should_create_mesh_node(trimesh, model.classification)) {
            if (trimesh->vertices.empty() || trimesh->indices.empty()) {
                ++stats.skipped_empty_mesh_count;
            }
            continue;
        }

        if (source_node->type & nwm::NodeFlags::dangly) {
            const auto* dangly_node = static_cast<const nwm::DanglymeshNode*>(source_node);
            DanglyMesh mesh;
            mesh.initialize_dangly(dangly_node);
            initialize_mesh_material(mesh, dangly_node, model.classification);
            append_nwn_model_asset_primitive(
                dangly_node, mesh, static_cast<uint32_t>(i), texture_source_indices, asset, stats);
            continue;
        }

        Mesh mesh;
        initialize_mesh_material(mesh, trimesh, model.classification);
        append_nwn_model_asset_primitive(
            trimesh, mesh, static_cast<uint32_t>(i), texture_source_indices, asset, stats);
    }
}

} // namespace

MaterialMode classify_nwn_material(const NwnMaterialClassificationInput& input)
{
    return classify_nwn_material_impl(input);
}

NwnModelAssetImportResult import_nwn_model_asset(const nwm::Mdl& mdl)
{
    NwnModelAssetImportResult result{};
    if (!mdl.valid()) {
        return result;
    }

    auto asset = std::make_unique<nw::render::ModelAsset>();
    asset->source_kind = nw::render::ModelAssetSourceKind::nwn_legacy;
    asset->name = std::string(mdl.model.name);
    asset->nodes.reserve(mdl.model.nodes.size());
    asset->sockets.reserve(mdl.model.nodes.size());
    asset->primitives.reserve(mdl.model.nodes.size());
    asset->materials.reserve(mdl.model.nodes.size());
    asset->material_texture_sources.reserve(mdl.model.nodes.size());

    result.stats.source_node_count = saturated_model_asset_import_count(mdl.model.nodes.size());
    initialize_nwn_model_asset_nodes(mdl.model, *asset);
    append_nwn_model_asset_sockets(mdl.model, *asset);
    append_nwn_model_asset_animations(mdl, *asset);
    append_nwn_model_asset_particles(mdl, *asset, result.stats);
    append_nwn_model_asset_meshes(mdl.model, *asset, result.stats);
    merge_nwn_model_asset_bounds(*asset);
    asset->shadow = nw::render::summarize_model_asset_shadows(*asset);

    result.stats.material_count = saturated_model_asset_import_count(asset->materials.size());
    result.stats.primitive_count = saturated_model_asset_import_count(asset->primitives.size());
    result.stats.texture_source_count = saturated_model_asset_import_count(asset->texture_sources.size());
    result.stats.socket_count = saturated_model_asset_import_count(asset->sockets.size());
    result.stats.deformer_count = saturated_model_asset_import_count(asset->deformers.size());
    result.stats.particle_system_count = saturated_model_asset_import_count(asset->particle_systems.size());

    if (asset->empty()) {
        return result;
    }

    const auto validation = nw::render::validate_model_asset(*asset);
    if (!validation.passed()) {
        LOG_F(ERROR,
            "NWN model '{}': decoded ModelAsset failed validation: primitives={} invalid={} invalid_asset_rows={} invalid_material_texture_bindings={}",
            asset->name,
            validation.primitive_count,
            validation.invalid_primitive_count(),
            validation.invalid_asset_row_count(),
            validation.invalid_material_texture_binding_count);
        return result;
    }

    result.asset = std::move(asset);
    return result;
}

NwnRenderModelImportResult import_nwn_render_model(
    const nwm::Mdl& mdl, const nw::render::ModelAssetTextureUploadDesc& texture_upload)
{
    NwnRenderModelImportResult result{};

    auto imported = import_nwn_model_asset(mdl);
    result.import_stats = imported.stats;
    if (!imported.asset) {
        return result;
    }

    auto uploaded = nw::render::upload_model_asset(*imported.asset, texture_upload.ctx);
    result.geometry_upload_stats = uploaded.stats;
    if (!uploaded.model) {
        return result;
    }

    result.texture_upload_stats = nw::render::upload_model_asset_material_textures(
        *imported.asset, texture_upload, *uploaded.model);
    result.model = std::move(uploaded.model);
    return result;
}

DanglyDeformPolicy dangly_deform_policy_for(const nwm::DanglymeshNode* node)
{
    return select_dangly_deform_policy(node);
}

std::string_view dangly_deform_policy_name(DanglyDeformPolicy policy) noexcept
{
    return dangly_deform_policy_name_impl(policy);
}

nw::render::ModelDeformerKind model_deformer_kind_for(DanglyDeformPolicy policy) noexcept
{
    return model_deformer_kind_for_impl(policy);
}

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

const glm::mat4& Node::refresh_model_transform_cache(const glm::mat4& parent_transform, uint64_t parent_revision) const
{
    const bool cache_current = cached_model_transform_valid_
        && cached_parent_model_transform_revision_ == parent_revision
        && cached_has_transform_ == has_transform_
        && vec3_equal(cached_position_, position_)
        && quat_equal(cached_rotation_, rotation_)
        && vec3_equal(cached_scale_, scale_);

    if (cache_current) {
        return cached_model_transform_;
    }

    cached_model_transform_ = has_transform_ ? parent_transform * get_local_transform() : parent_transform;
    cached_parent_model_transform_revision_ = parent_revision;
    cached_position_ = position_;
    cached_rotation_ = rotation_;
    cached_scale_ = scale_;
    cached_has_transform_ = has_transform_;
    cached_model_transform_revision_ = next_cache_revision(cached_model_transform_revision_);
    cached_model_transform_valid_ = true;
    return cached_model_transform_;
}

const glm::mat4& Node::refresh_model_transform_cache() const
{
    static const glm::mat4 identity{1.0f};
    if (!parent_) {
        return refresh_model_transform_cache(identity, 0);
    }

    const auto& parent_transform = parent_->refresh_model_transform_cache();
    return refresh_model_transform_cache(parent_transform, parent_->model_transform_cache_revision());
}

// ============================================================================
// Mesh
// ============================================================================

Mesh::~Mesh()
{
    if (owns_gpu_buffers && vertices.valid()) {
        nw::gfx::destroy_buffer(vertices);
    }
    if (owns_gpu_buffers && indices.valid()) {
        nw::gfx::destroy_buffer(indices);
    }
}

const glm::mat4& Mesh::refresh_normal_matrix_cache(
    const glm::mat4& world_transform, uint64_t model_revision, uint64_t root_revision) const
{
    const bool cache_current = cached_normal_matrix_valid_
        && cached_normal_model_transform_revision_ == model_revision
        && cached_normal_root_transform_revision_ == root_revision;

    if (cache_current) {
        return cached_normal_matrix_;
    }

    cached_normal_matrix_ = normal_matrix_for(world_transform);
    cached_normal_model_transform_revision_ = model_revision;
    cached_normal_root_transform_revision_ = root_revision;
    cached_normal_matrix_valid_ = true;
    return cached_normal_matrix_;
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

bool DanglyMesh::uses_foliage_sway() const noexcept
{
    return deform_policy_ == DanglyDeformPolicy::foliage_sway_cpu;
}

nw::render::ModelDeformer DanglyMesh::make_deformer_record(uint32_t source_node_index) const noexcept
{
    nw::render::ModelDeformer result{};
    result.kind = model_deformer_kind_for(deform_policy_);
    result.source_node_index = source_node_index;
    result.vertex_count = rest_positions_.size() > std::numeric_limits<uint32_t>::max()
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(rest_positions_.size());
    result.pivot = foliage_pivot_;
    result.axis = glm::length2(foliage_axis_) > 1.0e-6f ? glm::normalize(foliage_axis_) : glm::vec3{0.0f, 0.0f, 1.0f};
    result.amplitude = std::max(0.0f, foliage_amplitude_);
    result.displacement = std::max(0.0f, displacement_);
    result.period = std::max(0.0f, period_);
    result.tightness = std::max(0.0f, tightness_);
    result.phase = phase_;

    if (!freedom_.empty()) {
        float total = 0.0f;
        result.weight_min = freedom_.front();
        result.weight_max = freedom_.front();
        for (float value : freedom_) {
            const float clamped = std::clamp(value, 0.0f, 1.0f);
            result.weight_min = std::min(result.weight_min, clamped);
            result.weight_max = std::max(result.weight_max, clamped);
            total += clamped;
        }
        result.weight_average = total / static_cast<float>(freedom_.size());
    }

    if (source_node_index == nw::render::kInvalidModelNodeIndex || !constraints_valid_ || rest_positions_.empty()
        || freedom_.size() != rest_positions_.size()) {
        result.kind = nw::render::ModelDeformerKind::legacy_reference_cpu;
    }

    return result;
}

void DanglyMesh::initialize_dangly(const nwm::DanglymeshNode* node)
{
    initialize_dangly(node, dangly_deform_policy_for(node));
}

void DanglyMesh::initialize_dangly(const nwm::DanglymeshNode* node, DanglyDeformPolicy policy)
{
    cpu_vertices_.clear();
    rest_positions_.clear();
    offsets_.clear();
    velocities_.clear();
    freedom_.clear();
    deform_policy_ = policy;
    deformer_index_ = nw::render::kInvalidModelDeformerIndex;
    constraints_valid_ = false;
    two_sided_lighting = uses_foliage_sway();

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
    constraints_valid_ = valid_constraints;
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

    foliage_pivot_ = pinned_center_;
    glm::vec3 axis = loose_center_ - pinned_center_;

    if (uses_foliage_sway() && !rest_positions_.empty()) {
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
    foliage_amplitude_ = std::clamp(foliage_extent_ * 0.45f * kNwnFoliageMotionScale, 0.012f, 0.035f);

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
            "Dangly mesh {}: displacement={} period={} tightness={} freedom[min={}, avg={}, max={}] policy={} amp={}",
            node->name,
            displacement_,
            period_,
            tightness_,
            min_freedom,
            total_freedom / static_cast<float>(freedom_.size()),
            max_freedom,
            dangly_deform_policy_name(deform_policy_),
            foliage_amplitude_);
    }

    if (uses_foliage_sway() && cpu_vertices_.size() == 3) {
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

    if (uses_foliage_sway()) {
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
    const float body_motion_scale = 0.35f / (1.0f + tightness_ * 0.2f);
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
    const float wind_strength = 0.18f + 0.32f * gust;
    const float inertia_strength = glm::min(glm::length(motion_delta_world) * 5.0f, 0.12f);
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
        const float max_offset = displacement_ * freedom * body_motion_scale * debug_scale;
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
        const float flutter_amount = 0.025f * flutter * effective_freedom * std::sin(vertex_phase + phase_ * 0.5f);

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
    const float motion_scale = dangly_debug_scale() * kNwnFoliageMotionScale;
    const float max_angle = 0.22f * motion_scale * (0.75f + 0.25f * gust);

    const bool rigid_card = cpu_vertices_.size() == 3;
    const float rigid_angle = max_angle * sway + 0.03f * motion_scale * flutter;

    for (size_t i = 0; i < cpu_vertices_.size(); ++i) {
        const glm::vec3 local = rest_positions_[i] - foliage_pivot_;
        const float height = std::clamp(glm::dot(local, foliage_axis_) / std::max(foliage_extent_, 0.001f), 0.0f, 1.0f);
        const float freedom = i < freedom_.size() ? freedom_[i] : height;
        const float angle = rigid_card
            ? rigid_angle
            : (max_angle * sway * std::max(height, freedom) + 0.03f * motion_scale * flutter * height);
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
    const float motion_scale = dangly_debug_scale() * kNwnFoliageMotionScale;
    const float max_angle = 0.34f * motion_scale * (0.8f + 0.2f * gust);
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
        rotated += lift_local * (0.025f * motion_scale * flutter * height * foliage_extent_);
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

    if (uses_foliage_sway() && cpu_vertices_.size() == 3) {
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

uint32_t ModelInstance::socket_index(std::string_view name) const noexcept
{
    if (name.empty()) {
        return nw::render::kInvalidModelNodeIndex;
    }
    for (size_t i = 0; i < sockets_.size(); ++i) {
        if (nw::string::icmp(sockets_[i].name, name)) {
            return i >= nw::render::kInvalidModelNodeIndex
                ? nw::render::kInvalidModelNodeIndex
                : static_cast<uint32_t>(i);
        }
    }
    return nw::render::kInvalidModelNodeIndex;
}

const nw::render::ModelSocket* ModelInstance::socket(uint32_t index) const noexcept
{
    return index < sockets_.size() ? &sockets_[index] : nullptr;
}

const Node* ModelInstance::socket_node(uint32_t index) const noexcept
{
    const auto* record = socket(index);
    if (!record || record->source_node_index >= source_nodes_.size()) {
        return nullptr;
    }
    return source_nodes_[record->source_node_index];
}

const nw::render::ModelDeformer* ModelInstance::deformer(uint32_t index) const noexcept
{
    return index < deformers_.size() ? &deformers_[index] : nullptr;
}

const Node* ModelInstance::deformer_node(uint32_t index) const noexcept
{
    const auto* record = deformer(index);
    if (!record || record->source_node_index >= source_nodes_.size()) {
        return nullptr;
    }
    return source_nodes_[record->source_node_index];
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
        build_socket_records();
        build_deformer_records();
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
        if (should_create_mesh_node(n, mdl_->model.classification)) {
            auto mesh = std::make_unique<SkinMesh>();
            mesh->bone_nodes = n->bone_nodes;
            initialize_mesh_material(*mesh, n, mdl_->model.classification);
            result = std::move(mesh);
        }
    } else if (node->type & nwm::NodeFlags::dangly) {
        auto n = static_cast<const nwm::DanglymeshNode*>(node);
        if (should_create_mesh_node(n, mdl_->model.classification)) {
            auto mesh = std::make_unique<DanglyMesh>();
            mesh->initialize_dangly(n);
            initialize_mesh_material(*mesh, n, mdl_->model.classification);
            result = std::move(mesh);
        }
    } else if (node->type & nwm::NodeFlags::mesh && !(node->type & nwm::NodeFlags::aabb)) {
        auto n = static_cast<const nwm::TrimeshNode*>(node);
        if (should_create_mesh_node(n, mdl_->model.classification)) {
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

void ModelInstance::build_socket_records()
{
    sockets_.clear();
    sockets_.reserve(source_nodes_.size());
    for (size_t i = 0; i < source_nodes_.size(); ++i) {
        const auto* node = source_nodes_[i];
        if (!node || !node->orig_ || node->orig_->name.empty() || i >= nw::render::kInvalidModelNodeIndex) {
            continue;
        }

        if (node->orig_->type == nwm::NodeType::dummy) {
            append_sidecar_socket_if_missing(
                sockets_,
                *node,
                i,
                source_nodes_.size(),
                std::string_view{node->orig_->name});
        }

        // NWN source compatibility only: some single-body creature MDLs use
        // mesh nodes as equipped item anchors. Lower that authoring quirk into
        // socket rows at load time so runtime attachment code consumes indices.
        append_sidecar_socket_if_missing(
            sockets_,
            *node,
            i,
            source_nodes_.size(),
            nwn_equipped_item_socket_alias(node->orig_->name));
    }
}

void ModelInstance::build_deformer_records()
{
    deformers_.clear();
    deformers_.reserve(source_nodes_.size());
    for (size_t i = 0; i < source_nodes_.size(); ++i) {
        auto* node = source_nodes_[i];
        auto* dangly = dynamic_cast<DanglyMesh*>(node);
        if (dangly) {
            dangly->deformer_index_ = nw::render::kInvalidModelDeformerIndex;
        }
        if (!dangly || dangly->rest_positions_.empty() || i >= nw::render::kInvalidModelNodeIndex
            || deformers_.size() >= nw::render::kInvalidModelDeformerIndex) {
            continue;
        }

        dangly->deformer_index_ = static_cast<uint32_t>(deformers_.size());
        deformers_.push_back(dangly->make_deformer_record(static_cast<uint32_t>(i)));
    }
}

void ModelInstance::build_ozz_data(const nwm::Mdl* skeleton_source)
{
    if (nodes_.empty()) return;

    skeleton_source = skeleton_source ? skeleton_source : mdl_;
    if (!skeleton_source) {
        return;
    }

    nwn_backend_ = nw::render::make_animation_backend(nw::render::AnimationBackendKind::ozz);
    if (skeleton_source == mdl_) {
        nwn_skeleton_ = build_nwn_skeleton(*this, joint_to_source_node_);
    } else {
        nwn_skeleton_ = build_nwn_skeleton(*skeleton_source, joint_to_source_node_, skeleton_source->model.name);
    }
    if (nwn_skeleton_.joints.empty() || !nwn_backend_->build_skeleton(0, nwn_skeleton_)) {
        nwn_backend_.reset();
        joint_target_nodes_.clear();
        nwn_skeleton_source_ = nullptr;
        return;
    }
    joint_target_nodes_.clear();
    joint_target_nodes_.reserve(nwn_skeleton_.joints.size());
    for (const auto& joint : nwn_skeleton_.joints) {
        joint_target_nodes_.push_back(find(joint.name));
    }
    nwn_skeleton_source_ = skeleton_source;
    nwn_clips_.clear();
    nwn_pose_ = {};
    nwn_clip_index_ = -1;
    nwn_clip_name_to_index_.clear();
    nwn_clip_has_translation_.clear();
    nwn_clip_has_rotation_.clear();
    nwn_clip_has_scale_.clear();
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
    return current_bounds(root_transform());
}

Bounds ModelInstance::current_bounds(const glm::mat4& root) const
{
    Bounds result{};
    bool first = true;

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
            std::array<glm::mat4, nw::render::kModelMaxSkinBones> skin_matrices{};
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
        const nwm::Mdl* skeleton_source = animation_source_ ? animation_source_ : mdl_;
        if (!nwn_backend_ || nwn_skeleton_source_ != skeleton_source) {
            build_ozz_data(skeleton_source);
        }
        LOG_F(INFO, "Loaded animation: {}", name);

        const char* backend_name = "custom";
        if (nwn_backend_) {
            nwn_clip_has_translation_.assign(nwn_skeleton_.joints.size(), 0);
            nwn_clip_has_rotation_.assign(nwn_skeleton_.joints.size(), 0);
            nwn_clip_has_scale_.assign(nwn_skeleton_.joints.size(), 0);
            std::unordered_map<std::string_view, uint32_t> name_to_joint;
            name_to_joint.reserve(nwn_skeleton_.joints.size());
            for (uint32_t ji = 0; ji < static_cast<uint32_t>(nwn_skeleton_.joints.size()); ++ji) {
                name_to_joint[nwn_skeleton_.joints[ji].name] = ji;
            }
            for (const auto& anim_node_ptr : anim_->nodes) {
                if (!anim_node_ptr) {
                    continue;
                }
                auto it = name_to_joint.find(std::string_view(anim_node_ptr->name));
                if (it == name_to_joint.end()) {
                    continue;
                }
                const auto ji = it->second;
                nwn_clip_has_translation_[ji] = anim_node_ptr->get_controller(nwm::ControllerType::Position, true).key ? 1 : 0;
                nwn_clip_has_rotation_[ji] = anim_node_ptr->get_controller(nwm::ControllerType::Orientation, true).key ? 1 : 0;
                nwn_clip_has_scale_[ji] = anim_node_ptr->get_controller(nwm::ControllerType::Scale, true).key ? 1 : 0;
            }

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
    const bool has_local_scale = this->local_scale_ != 1.0f;
    const glm::mat4 local_scale = has_local_scale
        ? glm::scale(glm::mat4(1.0f), glm::vec3(this->local_scale_))
        : glm::mat4(1.0f);
    if (!transform_context_ || transform_anchor_.empty()) {
        return has_local_scale ? base * local_scale : base;
    }

    auto* anchor = transform_context_->socket_node(transform_anchor_socket_index_);
    if (!anchor) {
        anchor = transform_context_->find(transform_anchor_);
    }
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

    // Scale the attached model, not the destination anchor translation.
    if (has_local_scale) {
        anchor_transform = anchor_transform * local_scale;
    }

    if (this->anchor_uses_root_bind_offset && !nodes_.empty()) {
        const Node* local_anchor = socket_node(transform_source_anchor_socket_index_);
        if (!local_anchor && !this->transform_source_anchor_.empty()) {
            local_anchor = find(this->transform_source_anchor_);
        }
        if (local_anchor) {
            anchor_transform = anchor_transform * glm::inverse(local_anchor->bind_pose_);
        } else {
            auto bind_translation = glm::vec3(nodes_.front()->bind_pose_[3]);
            anchor_transform = anchor_transform * glm::translate(glm::mat4(1.0f), -bind_translation);
        }
    }

    return anchor_transform;
}

uint64_t ModelInstance::root_transform_cache_revision(const glm::mat4& model_root) const
{
    if (cached_root_transform_valid_ && mat4_equal(cached_root_transform_, model_root)) {
        return cached_root_transform_revision_;
    }

    cached_root_transform_ = model_root;
    cached_root_transform_revision_ = next_cache_revision(cached_root_transform_revision_);
    cached_root_transform_valid_ = true;
    return cached_root_transform_revision_;
}

const glm::mat4& ModelInstance::refresh_root_normal_matrix_cache(
    const glm::mat4& model_root, uint64_t root_revision) const
{
    const bool cache_current = cached_root_normal_matrix_valid_
        && cached_root_normal_transform_revision_ == root_revision;

    if (cache_current) {
        return cached_root_normal_matrix_;
    }

    cached_root_normal_matrix_ = normal_matrix_for(model_root);
    cached_root_normal_transform_revision_ = root_revision;
    cached_root_normal_matrix_valid_ = true;
    return cached_root_normal_matrix_;
}

void ModelInstance::update(int32_t dt_ms)
{
    if (anim_) {
        // Update animation cursor with wrapping.
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
                const bool cross_skeleton = nwn_skeleton_source_ && nwn_skeleton_source_ != mdl_;
                for (size_t ji = 0; ji < joint_target_nodes_.size(); ++ji) {
                    Node* bone = joint_target_nodes_[ji];
                    if (!bone) continue;
                    const auto& local = nwn_pose_.local[ji];
                    if (cross_skeleton) {
                        const auto bind_local = node_bind_local_transform(*bone);
                        bone->position_ = (ji < nwn_clip_has_translation_.size() && nwn_clip_has_translation_[ji])
                            ? local.translation
                            : bind_local.translation;
                        bone->rotation_ = (ji < nwn_clip_has_rotation_.size() && nwn_clip_has_rotation_[ji])
                            ? local.rotation
                            : bind_local.rotation;
                        bone->scale_ = (ji < nwn_clip_has_scale_.size() && nwn_clip_has_scale_[ji])
                            ? local.scale
                            : bind_local.scale;
                    } else {
                        bone->position_ = local.translation;
                        bone->rotation_ = local.rotation;
                        bone->scale_ = local.scale;
                    }
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
        if (created && should_register_shadow_caster(trimesh, *mesh)) {
            model->shadow_casters_.push_back(mesh);
        }
        if (created && !trimesh->indices.empty()) {
            model->material_pass_mask |= material_pass_mask(mesh->material_mode);
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
        log_error_context();
        return nullptr;
    }

    auto model = load(mdl, root_name);
    if (!model) {
        LOG_F(ERROR, "Failed to load model: {}", resref);
        log_error_context();
    }
    return model;
}

bool ModelLoader::create_mesh_buffers(Mesh& mesh, const nwm::TrimeshNode* node)
{
    if (!ctx_ || !node || node->vertices.empty() || node->indices.empty()) {
        return false;
    }

    auto* dangly = dynamic_cast<DanglyMesh*>(&mesh);
    std::vector<Vertex> vertices;
    if (dangly && dangly->cpu_vertices_.size() == node->vertices.size()) {
        vertices = dangly->cpu_vertices_;
    } else {
        vertices.reserve(node->vertices.size());
        for (const auto& vertex : node->vertices) {
            vertices.push_back(convert_vertex(vertex));
        }
    }

    std::vector<uint16_t> indices = validated_triangle_indices(node->indices, vertices.size(), node->name);
    if (indices.empty()) {
        return false;
    }

    if (has_invalid_normals(vertices)) {
        recompute_static_vertex_normals_for_indices(indices, vertices);
    }
    if (has_invalid_tangents(vertices)) {
        recompute_vertex_tangents_for_indices(indices, vertices);
    }

    inset_transparent_subrect_uvs(mesh, vertices);
    if (mesh.material_mode == MaterialMode::water) {
        subdivide_water_mesh(vertices, indices);
    }
    if (dangly && dangly->cpu_vertices_.size() == vertices.size()) {
        dangly->cpu_vertices_ = vertices;
    }

    nw::gfx::BufferDesc vertex_desc{};
    vertex_desc.size = vertices.size() * sizeof(Vertex);
    vertex_desc.usage = nw::gfx::BufferUsage::Vertex;
    vertex_desc.cpu_visible = true;
    mesh.vertices = nw::gfx::create_buffer(ctx_, vertex_desc);

    nw::gfx::BufferDesc index_desc{};
    index_desc.size = indices.size() * sizeof(uint16_t);
    index_desc.usage = nw::gfx::BufferUsage::Index;
    index_desc.cpu_visible = true;
    mesh.indices = nw::gfx::create_buffer(ctx_, index_desc);

    auto* vertex_buffer = nw::gfx::map_buffer(mesh.vertices);
    auto* index_buffer = nw::gfx::map_buffer(mesh.indices);
    if (!vertex_buffer || !index_buffer) {
        return false;
    }

    std::memcpy(vertex_buffer, vertices.data(), vertex_desc.size);
    std::memcpy(index_buffer, indices.data(), index_desc.size);
    nw::gfx::unmap_buffer(mesh.vertices);
    nw::gfx::unmap_buffer(mesh.indices);

    mesh.vertex_count = static_cast<uint32_t>(vertices.size());
    mesh.index_count = static_cast<uint32_t>(indices.size());
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

    std::vector<uint16_t> indices = validated_triangle_indices(node->indices, vertices.size(), node->name);
    if (indices.empty()) {
        return false;
    }

    if (has_invalid_normals(vertices)) {
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
        recompute_static_vertex_normals_for_indices(indices, static_vertices);
        for (size_t i = 0; i < vertices.size(); ++i) {
            vertices[i].normal = static_vertices[i].normal;
        }
    }

    if (has_invalid_tangents(vertices)) {
        recompute_vertex_tangents_for_indices(indices, vertices);
    }

    inset_transparent_subrect_uvs(mesh, vertices);

    nw::gfx::BufferDesc vertex_desc{};
    vertex_desc.size = vertices.size() * sizeof(SkinnedVertex);
    vertex_desc.usage = nw::gfx::BufferUsage::Vertex;
    vertex_desc.cpu_visible = true;
    mesh.vertices = nw::gfx::create_buffer(ctx_, vertex_desc);

    nw::gfx::BufferDesc index_desc{};
    index_desc.size = indices.size() * sizeof(uint16_t);
    index_desc.usage = nw::gfx::BufferUsage::Index;
    index_desc.cpu_visible = true;
    mesh.indices = nw::gfx::create_buffer(ctx_, index_desc);

    auto* vertex_buffer = nw::gfx::map_buffer(mesh.vertices);
    auto* index_buffer = nw::gfx::map_buffer(mesh.indices);
    if (!vertex_buffer || !index_buffer) {
        return false;
    }

    std::memcpy(vertex_buffer, vertices.data(), vertex_desc.size);
    std::memcpy(index_buffer, indices.data(), index_desc.size);
    nw::gfx::unmap_buffer(mesh.vertices);
    nw::gfx::unmap_buffer(mesh.indices);

    mesh.vertex_count = static_cast<uint32_t>(vertices.size());
    mesh.index_count = static_cast<uint32_t>(indices.size());
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

void clear_model_loader_resource_caches()
{
    mtr_material_cache().clear();
    texture_analysis_cache().clear();
}

ModelLoaderResourceCacheStats model_loader_resource_cache_stats()
{
    return ModelLoaderResourceCacheStats{
        .mtr_material_count = mtr_material_cache().size(),
        .texture_analysis_count = texture_analysis_cache().size(),
    };
}

} // namespace nw::render::nwn
