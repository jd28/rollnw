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
using Bounds = nw::render::Bounds;
using MaterialMode = nw::render::MaterialMode;
using Vertex = nw::render::Vertex;

namespace {

constexpr float kNwnFoliageMotionScale = 0.1f;
constexpr float kNwnDefaultRoughness = 0.78f;
constexpr float kNwnDefaultSpecularStrength = 0.12f;

struct NwnMeshImportData {
    std::string bitmap_name;
    std::string normal_map_name;
    std::string specular_map_name;
    std::string roughness_map_name;
    std::string emissive_map_name;
    std::string renderhint;
    std::string materialname;
    std::vector<Vertex> source_vertices;
    std::optional<nw::render::ModelDeformer> deformer;
    Bounds local_bounds{};
    int transparencyhint = 0;
    MaterialMode material_mode = MaterialMode::opaque;
    float opacity = 1.0f;
    float alpha_cutout_threshold = 0.5f;
    glm::vec3 color_key{0.0f};
    float color_key_threshold = 0.0f;
    glm::vec3 emissive{0.0f};
    float roughness = kNwnDefaultRoughness;
    float common_pbr_roughness = kNwnDefaultRoughness;
    float specular_strength = kNwnDefaultSpecularStrength;
    bool albedo_prefers_plt = false;
    bool material_uses_fallback = false;
    bool two_sided_lighting = false;
};

float inherited_animation_translation_scale(const nwm::Mdl& mdl)
{
    const float scale = mdl.model.animationscale;
    if (std::isfinite(scale) && scale > 0.0f) {
        return scale;
    }

    LOG_F(WARNING, "NWN model '{}': invalid animation scale {}; using 1.0", mdl.model.name, scale);
    return 1.0f;
}

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

float hash_phase(std::string_view name)
{
    uint32_t value = 2166136261u;
    for (unsigned char c : name) {
        value ^= c;
        value *= 16777619u;
    }
    return static_cast<float>(value & 0xffffu) / 65535.0f * glm::two_pi<float>();
}

bool casts_shadow_material(MaterialMode mode)
{
    return mode != MaterialMode::transparent && mode != MaterialMode::water;
}

bool should_register_shadow_caster(const nwm::TrimeshNode* node, const NwnMeshImportData& mesh)
{
    return node && node->render && node->shadow && casts_shadow_material(mesh.material_mode);
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
        return DanglyDeformPolicy::secondary_motion_chain;
    }

    // NWN dangly nodes do not distinguish foliage cards from other spring-like
    // attachments. Keep these content-name hints isolated as an importer
    // compatibility rule, not a renderer-wide wind contract.
    const bool foliage_hint = has_foliage_token(ascii_lower(node->name))
        || has_foliage_token(ascii_lower(node->bitmap))
        || has_foliage_token(ascii_lower(node->textures[0]));
    return foliage_hint
        ? DanglyDeformPolicy::foliage_sway
        : DanglyDeformPolicy::secondary_motion_chain;
}

std::string_view dangly_deform_policy_name_impl(DanglyDeformPolicy policy) noexcept
{
    switch (policy) {
    case DanglyDeformPolicy::secondary_motion_chain:
        return "secondary_motion_chain";
    case DanglyDeformPolicy::foliage_sway:
        return "foliage_sway";
    }
    return "unknown";
}

nw::render::ModelDeformerKind model_deformer_kind_for_impl(DanglyDeformPolicy policy) noexcept
{
    switch (policy) {
    case DanglyDeformPolicy::secondary_motion_chain:
        return nw::render::ModelDeformerKind::secondary_motion_chain;
    case DanglyDeformPolicy::foliage_sway:
        return nw::render::ModelDeformerKind::vertex_shader_sway;
    }
    return nw::render::ModelDeformerKind::secondary_motion_chain;
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

float nwn_shininess_to_roughness(float shininess) noexcept
{
    if (!std::isfinite(shininess) || shininess <= 0.0f) {
        return kNwnDefaultRoughness;
    }

    const float spec_power = std::clamp(shininess, 4.0f, 52.0f);
    return std::clamp(1.0f - ((spec_power - 4.0f) / 48.0f), 0.18f, 0.95f);
}

float nwn_specular_to_strength(const glm::vec3& specular) noexcept
{
    if (!std::isfinite(specular.x) || !std::isfinite(specular.y) || !std::isfinite(specular.z)) {
        return kNwnDefaultSpecularStrength;
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
        info.roughness = nwn_shininess_to_roughness(value);
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
        info.specular_strength = nwn_specular_to_strength(value);
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
    bool has_alphamean = false;
    bool decal = false;
};

struct TextureAnalysis {
    NwnMaterialAlphaProfile alpha_profile = NwnMaterialAlphaProfile::opaque;
    int width = 0;
    int height = 0;
};

constexpr float mostly_binary_alpha_coverage_gap = 0.03f;

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

bool should_create_mesh_node(const nwm::TrimeshNode* node, nwm::ModelClass model_class)
{
    if (!node || node->vertices.empty() || node->indices.empty()) {
        return false;
    }
    if (node->render) {
        return true;
    }

    // Some tile sets mark visible detail meshes as render=0. Preserve that
    // exception only when the mesh has source imagery to render.
    return model_class == nwm::ModelClass::tile
        && source_texture_exists(resolve_bitmap_name(node));
}

bool should_create_mesh_node(const nwm::SkinNode* node, nwm::ModelClass model_class)
{
    if (!node || node->vertices.empty() || node->indices.empty()) {
        return false;
    }
    if (node->render) {
        return true;
    }

    return model_class == nwm::ModelClass::tile
        && source_texture_exists(resolve_bitmap_name(node));
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

bool material_uses_fallback_resources(const NwnMeshImportData& mesh)
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
    result.has_alphamean = txi.get_to("alphamean", result.alphamean);
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

        if (has_partial) {
            const float coverage_gap = static_cast<float>(coverage_50 - coverage_75)
                / static_cast<float>(pixel_count);
            result.alpha_profile = has_zero && coverage_gap < mostly_binary_alpha_coverage_gap
                ? NwnMaterialAlphaProfile::mostly_binary
                : NwnMaterialAlphaProfile::graded;
        } else {
            result.alpha_profile = has_zero ? NwnMaterialAlphaProfile::binary : NwnMaterialAlphaProfile::opaque;
        }
    }

    cache[std::string(bitmap_name)] = result;
    return result;
}

template <typename TVertex>
void inset_transparent_subrect_uvs(const NwnMeshImportData& mesh, std::vector<TVertex>& vertices)
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
        const bool valid_alphamean = input.txi_has_alphamean
            && std::isfinite(input.txi_alphamean)
            && input.txi_alphamean > 0.0f
            && input.txi_alphamean < 1.0f;
        if (valid_alphamean) {
            switch (input.alpha_profile) {
            case NwnMaterialAlphaProfile::binary:
            case NwnMaterialAlphaProfile::mostly_binary:
                return MaterialMode::cutout;
            case NwnMaterialAlphaProfile::graded:
                return MaterialMode::transparent;
            case NwnMaterialAlphaProfile::opaque:
            default:
                break;
            }
        }
    }

    if (web_texture && input.alpha_profile != NwnMaterialAlphaProfile::opaque) {
        return MaterialMode::cutout;
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
            case NwnMaterialAlphaProfile::mostly_binary:
                return MaterialMode::cutout;
            case NwnMaterialAlphaProfile::graded:
                return MaterialMode::transparent;
            case NwnMaterialAlphaProfile::opaque:
            default:
                return MaterialMode::opaque;
            }
        }
    }

    if (input.model_class == nwm::ModelClass::tile && input.is_planar_quad) {
        switch (input.alpha_profile) {
        case NwnMaterialAlphaProfile::binary:
        case NwnMaterialAlphaProfile::mostly_binary:
            return MaterialMode::cutout;
        case NwnMaterialAlphaProfile::graded:
            return MaterialMode::transparent;
        case NwnMaterialAlphaProfile::opaque:
        default:
            break;
        }
    }

    switch (input.alpha_profile) {
    case NwnMaterialAlphaProfile::binary:
        return MaterialMode::cutout;
    case NwnMaterialAlphaProfile::mostly_binary:
        if (input.model_class == nwm::ModelClass::tile
            || input.model_class == nwm::ModelClass::character) {
            return MaterialMode::cutout;
        }
        [[fallthrough]];
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

void initialize_mesh_material(NwnMeshImportData& mesh, const nwm::TrimeshNode* node, nwm::ModelClass model_class,
    std::string_view model_resref, NwnModelAssetImportStats* stats = nullptr)
{
    if (!node) {
        return;
    }

    mesh.bitmap_name = resolve_nwn_model_albedo_resref(model_resref, resolve_bitmap_name(node));
    const auto humanoid_palette = nwn_humanoid_palette_resref(model_resref);
    mesh.albedo_prefers_plt = mesh.bitmap_name == humanoid_palette
        && source_resource_exists(humanoid_palette, nw::ResourceType::plt);
    mesh.renderhint = std::string(node->renderhint);
    mesh.materialname = std::string(node->materialname);
    mesh.transparencyhint = node->transparencyhint;
    mesh.opacity = mesh_alpha_value(node);
    mesh.alpha_cutout_threshold = 0.5f;

    const auto mtr = load_mtr_material_info(node);
    if (mtr.renderhint) {
        mesh.renderhint = *mtr.renderhint;
    }
    if (mtr.diffuse_texture) {
        mesh.bitmap_name = *mtr.diffuse_texture;
        mesh.albedo_prefers_plt = false;
    }
    mesh.normal_map_name = mtr.normal_texture.value_or(std::string{});
    mesh.specular_map_name = mtr.specular_texture.value_or(std::string{});
    mesh.roughness_map_name = mtr.roughness_texture.value_or(std::string{});
    mesh.emissive_map_name = mtr.emissive_texture.value_or(std::string{});
    mesh.material_uses_fallback = material_uses_fallback_resources(mesh);

    glm::vec3 min_v{0.0f};
    glm::vec3 max_v{0.0f};
    size_t source_vertex_count = 0;
    bool has_finite_position = false;
    bool bounds_valid = true;
    const auto update_bounds = [&](const glm::vec3& position) {
        ++source_vertex_count;
        if (!finite_vec3(position)) {
            bounds_valid = false;
            return;
        }
        if (!has_finite_position) {
            min_v = position;
            max_v = position;
            has_finite_position = true;
            return;
        }
        min_v = glm::min(min_v, position);
        max_v = glm::max(max_v, position);
    };
    if (const auto* skin = dynamic_cast<const nwm::SkinNode*>(node)) {
        for (const auto& vertex : skin->vertices) {
            update_bounds(vertex.position);
        }
    } else {
        for (const auto& vertex : node->vertices) {
            update_bounds(vertex.position);
        }
    }
    bounds_valid = bounds_valid && has_finite_position;
    if (bounds_valid) {
        mesh.local_bounds = Bounds{.min = min_v, .max = max_v};
    } else {
        mesh.local_bounds = {};
        min_v = {};
        max_v = {};
    }
    const glm::vec3 span = max_v - min_v;
    const float horizontal_span = std::max(span.x, span.y);
    const bool valid_quad_indices = node->indices.size() == 6
        && std::all_of(node->indices.begin(), node->indices.end(), [source_vertex_count](uint16_t index) {
               return index < source_vertex_count;
           });
    const bool is_planar_tile_quad = model_class == nwm::ModelClass::tile
        && source_vertex_count == 4
        && valid_quad_indices
        && bounds_valid
        && horizontal_span >= 0.5f
        && span.z <= 0.2f
        && span.z <= horizontal_span * 0.1f;

    const bool water_material = looks_like_water_material(node, mesh.bitmap_name, model_class);
    const auto* dangly_node = dynamic_cast<const nwm::DanglymeshNode*>(node);
    const bool foliage_dangly = dangly_node
        && dangly_deform_policy_for(dangly_node) == DanglyDeformPolicy::foliage_sway;
    const bool foliage_texture_hint = looks_like_foliage_texture(mesh.bitmap_name);
    if (stats) {
        if (water_material) {
            ++stats->water_name_heuristic_count;
        }
        if (foliage_dangly || foliage_texture_hint) {
            ++stats->foliage_name_heuristic_count;
        }
    }
    auto txi = load_txi_material_info(mesh.bitmap_name);
    TextureAnalysis texture{};
    if (!water_material) {
        texture = analyze_texture(mesh.bitmap_name);
        if (txi.has_alphamean && std::isfinite(txi.alphamean)
            && txi.alphamean > 0.0f && txi.alphamean < 1.0f) {
            mesh.alpha_cutout_threshold = txi.alphamean;
        } else if ((foliage_dangly || foliage_texture_hint)
            && texture.alpha_profile == NwnMaterialAlphaProfile::mostly_binary) {
            // Mostly-binary foliage alpha benefits from a stronger cutout threshold.
            mesh.alpha_cutout_threshold = 0.75f;
        }
        if (txi.blending == "lighten") {
            mesh.color_key = glm::vec3(0.0f);
            mesh.color_key_threshold = 0.18f;
        }
    }
    if (water_material) {
        mesh.material_mode = MaterialMode::water;
    } else {
        mesh.material_mode = classify_nwn_material_impl(NwnMaterialClassificationInput{
            .node = node,
            .bitmap_name = mesh.bitmap_name,
            .model_class = model_class,
            .alpha_profile = texture.alpha_profile,
            .is_planar_quad = is_planar_tile_quad,
            .has_txi = txi.has_txi,
            .txi_blending = txi.blending,
            .txi_decal = txi.decal,
            .txi_has_alphamean = txi.has_alphamean,
            .txi_alphamean = txi.alphamean,
        });
    }
    if (!water_material
        && foliage_dangly
        && texture.alpha_profile != NwnMaterialAlphaProfile::opaque
        && mesh.material_mode == MaterialMode::opaque) {
        mesh.material_mode = MaterialMode::cutout;
    }
    mesh.roughness = nwn_shininess_to_roughness(node->shininess);
    mesh.specular_strength = nwn_specular_to_strength(node->specular);
    // Do not translate NWN Blinn shininess directly into the common PBR
    // asset. Diffuse-only NWN meshes use a neutral roughness; authored MTR
    // roughness maps/scalars keep explicit PBR intent.
    mesh.common_pbr_roughness = !mesh.roughness_map_name.empty() ? 1.0f : kNwnDefaultRoughness;
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
}

void initialize_dangly_import_data(
    NwnMeshImportData& mesh,
    const nwm::DanglymeshNode* node,
    NwnModelAssetImportStats& stats)
{
    if (!node) {
        return;
    }

    const auto policy = select_dangly_deform_policy(node);
    const bool foliage_sway = policy == DanglyDeformPolicy::foliage_sway;
    mesh.two_sided_lighting = foliage_sway;
    mesh.source_vertices.reserve(node->vertices.size());

    std::vector<glm::vec3> rest_positions;
    rest_positions.reserve(node->vertices.size());
    for (const auto& vertex : node->vertices) {
        mesh.source_vertices.push_back(convert_vertex(vertex));
        rest_positions.push_back(vertex.position);
    }

    const bool constraints_valid = node->constraints.size() == node->vertices.size();
    if (!constraints_valid && !node->constraints.empty()) {
        LOG_F(WARNING,
            "Dangly mesh {} has {} constraints for {} vertices; dropping its deformer",
            node->name,
            node->constraints.size(),
            node->vertices.size());
    }

    std::vector<float> freedom;
    freedom.reserve(node->vertices.size());
    for (size_t i = 0; i < node->vertices.size(); ++i) {
        freedom.push_back(constraints_valid
                ? std::clamp(node->constraints[i] / 255.0f, 0.0f, 1.0f)
                : 0.0f);
    }

    glm::vec3 pinned_center{0.0f};
    glm::vec3 loose_center{0.0f};
    size_t pinned_count = 0;
    size_t loose_count = 0;
    for (size_t i = 0; i < rest_positions.size(); ++i) {
        if (freedom[i] <= 0.05f) {
            pinned_center += rest_positions[i];
            ++pinned_count;
        } else {
            loose_center += rest_positions[i];
            ++loose_count;
        }
    }
    if (pinned_count > 0) {
        pinned_center /= static_cast<float>(pinned_count);
    }
    if (loose_count > 0) {
        loose_center /= static_cast<float>(loose_count);
    } else {
        loose_center = pinned_center;
    }

    glm::vec3 pivot = pinned_center;
    glm::vec3 axis = loose_center - pinned_center;
    if (foliage_sway && !rest_positions.empty()) {
        if (rest_positions.size() == 3) {
            std::array<size_t, 3> order = {0, 1, 2};
            std::sort(order.begin(), order.end(), [&](size_t lhs, size_t rhs) {
                return rest_positions[lhs].z < rest_positions[rhs].z;
            });
            pivot = (rest_positions[order[0]] + rest_positions[order[1]]) * 0.5f;
            axis = rest_positions[order[2]] - pivot;
        } else {
            size_t root_index = 0;
            for (size_t i = 1; i < rest_positions.size(); ++i) {
                if (rest_positions[i].z < rest_positions[root_index].z) {
                    root_index = i;
                }
            }

            pivot = rest_positions[root_index];
            glm::vec3 tip_center{0.0f};
            size_t tip_count = 0;
            for (size_t i = 0; i < rest_positions.size(); ++i) {
                if (i != root_index) {
                    tip_center += rest_positions[i];
                    ++tip_count;
                }
            }
            if (tip_count > 0) {
                axis = tip_center / static_cast<float>(tip_count) - pivot;
            }
        }
    }

    if (glm::length2(axis) < 1.0e-6f) {
        axis = glm::vec3{0.0f, 0.0f, 1.0f};
    }
    const float extent = std::max(glm::length(axis), 0.05f);

    if (foliage_sway && mesh.source_vertices.size() == 3) {
        float min_u = mesh.source_vertices.front().texcoord.x;
        float max_u = min_u;
        for (const auto& vertex : mesh.source_vertices) {
            min_u = std::min(min_u, vertex.texcoord.x);
            max_u = std::max(max_u, vertex.texcoord.x);
        }
        if (max_u > 1.25f && min_u >= 0.5f) {
            for (auto& vertex : mesh.source_vertices) {
                vertex.texcoord.x -= 0.5f;
                vertex.texcoord.y = 1.0f - vertex.texcoord.y;
            }
        }
    }

    if (!constraints_valid || rest_positions.empty() || freedom.size() != rest_positions.size()) {
        ++stats.unsupported_deformer_count;
        return;
    }

    nw::render::ModelDeformer deformer{};
    deformer.kind = model_deformer_kind_for_impl(policy);
    deformer.vertex_count = rest_positions.size() > std::numeric_limits<uint32_t>::max()
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(rest_positions.size());
    deformer.pivot = pivot;
    deformer.axis = glm::normalize(axis);
    deformer.amplitude = std::clamp(extent * 0.45f * kNwnFoliageMotionScale, 0.012f, 0.035f);
    deformer.displacement = std::max(0.0f, node->displacement);
    deformer.period = std::max(0.0f, node->period);
    deformer.tightness = std::max(0.0f, node->tightness);
    deformer.phase = hash_phase(node->name);

    float total = 0.0f;
    deformer.weight_min = freedom.front();
    deformer.weight_max = freedom.front();
    for (float value : freedom) {
        const float clamped = std::clamp(value, 0.0f, 1.0f);
        deformer.weight_min = std::min(deformer.weight_min, clamped);
        deformer.weight_max = std::max(deformer.weight_max, clamped);
        total += clamped;
    }
    deformer.weight_average = total / static_cast<float>(freedom.size());
    mesh.deformer = deformer;
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

std::optional<float> source_light_scalar(const nwm::LightNode& light, uint32_t type)
{
    const auto controller = light.get_controller(type, false);
    if (!controller.key || controller.data.empty()) {
        return std::nullopt;
    }
    return controller.data[0];
}

std::optional<glm::vec3> source_light_color(const nwm::LightNode& light)
{
    const auto controller = light.get_controller(nwm::ControllerType::Color, false);
    if (!controller.key || controller.data.size() < 3) {
        return std::nullopt;
    }
    return glm::vec3{controller.data[0], controller.data[1], controller.data[2]};
}

bool finite_light_color(const glm::vec3& color) noexcept
{
    return std::isfinite(color.x) && std::isfinite(color.y) && std::isfinite(color.z);
}

bool visible_light_color(const glm::vec3& color) noexcept
{
    return std::max(color.x, std::max(color.y, color.z)) > 1.0e-4f;
}

struct SourceTileLightSlot {
    bool valid = false;
    bool source = false;
    bool second = false;
};

SourceTileLightSlot source_tile_light_slot(std::string_view name) noexcept
{
    if (name.size() < 3) {
        return {};
    }

    const char category = static_cast<char>(std::tolower(
        static_cast<unsigned char>(name[name.size() - 3])));
    const char marker = static_cast<char>(std::tolower(
        static_cast<unsigned char>(name[name.size() - 2])));
    const char lane = name.back();
    if ((category != 'm' && category != 's')
        || marker != 'l'
        || (lane != '1' && lane != '2')) {
        return {};
    }
    return SourceTileLightSlot{
        .valid = true,
        .source = category == 's',
        .second = lane == '2',
    };
}

float source_light_classification_radius(const nwm::LightNode& light) noexcept
{
    const float radius = source_light_scalar(light, nwm::ControllerType::Radius)
                             .value_or(source_light_scalar(light, nwm::ControllerType::ShadowRadius)
                                     .value_or(light.flareradius));
    return std::isfinite(radius) ? radius : 0.0f;
}

void append_nwn_model_asset_lights(
    const nwm::Model& model,
    nw::render::ModelAsset& asset,
    NwnModelAssetImportStats& stats)
{
    asset.lights.reserve(model.nodes.size());
    for (size_t node_index = 0; node_index < model.nodes.size(); ++node_index) {
        const auto* light = dynamic_cast<const nwm::LightNode*>(model.nodes[node_index].get());
        if (!light || node_index >= nw::render::kInvalidModelNodeIndex) {
            continue;
        }

        glm::vec3 color = source_light_color(*light).value_or(light->color);
        if (!finite_light_color(color)) {
            color = glm::vec3{0.0f};
        } else if (!visible_light_color(color) && finite_light_color(light->color)) {
            color = light->color;
        }

        const auto slot = source_tile_light_slot(light->name);
        const float classification_radius = source_light_classification_radius(*light);
        const bool main_contribution = !slot.source && classification_radius >= 8.0f;
        uint8_t external_color_slot = nw::render::kModelLightNoExternalColor;
        if (slot.valid) {
            external_color_slot = static_cast<uint8_t>(
                (main_contribution ? 0u : 2u) + (slot.second ? 1u : 0u));
        }

        float radius = source_light_scalar(*light, nwm::ControllerType::Radius)
                           .value_or(source_light_scalar(*light, nwm::ControllerType::ShadowRadius)
                                   .value_or(0.0f));
        if (!std::isfinite(radius) || radius < 0.0f) {
            radius = 0.0f;
        }

        float intensity = source_light_scalar(*light, nwm::ControllerType::Multiplier)
                              .value_or(light->multiplier > 0.0f ? light->multiplier : 1.0f);
        if (!std::isfinite(intensity)) {
            intensity = 1.0f;
        } else {
            intensity = std::max(0.0f, intensity);
        }

        asset.lights.push_back(nw::render::ModelLight{
            .node = static_cast<uint32_t>(node_index),
            .color = color,
            .radius = radius,
            .intensity = intensity,
            .external_color_slot = external_color_slot,
            .main_contribution = main_contribution,
            .dynamic = light->dynamic,
            .affect_dynamic = light->affectdynamic != 0,
            .ambient = light->ambientonly != 0,
            .casts_shadow = light->shadow != 0,
            .fading = light->fadinglight != 0,
        });
    }
    stats.light_count = saturated_model_asset_import_count(asset.lights.size());
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

    // Authored dummy sockets are authoritative. Collect them before lowering
    // mesh-backed compatibility aliases so traversal order cannot replace a
    // precise child socket with its parent mesh transform.
    for (size_t i = 0; i < model.nodes.size() && i < asset.nodes.size(); ++i) {
        const auto* source = model.nodes[i].get();
        if (!source || source->name.empty() || i >= nw::render::kInvalidModelNodeIndex) {
            continue;
        }

        if (source->type == nwm::NodeType::dummy) {
            append_model_asset_socket_if_missing(asset, i, std::string_view{source->name});
        }
    }

    for (size_t i = 0; i < model.nodes.size() && i < asset.nodes.size(); ++i) {
        const auto* source = model.nodes[i].get();
        if (!source || source->name.empty() || i >= nw::render::kInvalidModelNodeIndex) {
            continue;
        }

        // NWN single-body creatures sometimes expose equipment anchors as named
        // mesh nodes such as rhand_g/lhand_g instead of dummy nodes. Lower those
        // source-specific names only when no authored dummy has claimed the
        // common socket name.
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
    const float inherited_translation_scale = mdl.model.supermodel
        ? inherited_animation_translation_scale(mdl)
        : 1.0f;
    for (const nwm::Mdl* source = &mdl; source; source = source->model.supermodel.get()) {
        const float translation_scale = source == &mdl ? 1.0f : inherited_translation_scale;
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

            asset.animations.push_back(
                build_nwn_clip(*animation, asset_skeleton, skeleton_index, translation_scale));
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
    NwnModelAssetImportStats& stats,
    bool prefer_plt = false)
{
    const auto name = clean_mtr_resource_name(raw_name);
    if (name.empty()) {
        return nw::render::kInvalidModelAssetTextureSourceIndex;
    }

    std::string cache_key;
    cache_key.reserve(name.size() + 4);
    cache_key.append(prefer_plt ? "plt:" : "any:");
    cache_key.append(name);
    const auto existing = texture_source_indices.find(cache_key);
    if (existing != texture_source_indices.end()) {
        return existing->second;
    }

    auto data = prefer_plt && texture_resource_is_plt(name)
        ? nw::kernel::resman().demand({nw::Resref{name}, nw::ResourceType::plt})
        : nw::kernel::resman().demand_in_order(
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
    texture_source_indices.emplace(std::move(cache_key), index);
    return index;
}

nw::render::ModelAssetMaterialTextureSources append_nwn_model_asset_material_texture_sources(const NwnMeshImportData& mesh,
    std::unordered_map<std::string, uint32_t>& texture_source_indices,
    nw::render::ModelAsset& asset,
    NwnModelAssetImportStats& stats)
{
    nw::render::ModelAssetMaterialTextureSources sources{};
    sources.albedo_srgb = false;
    sources.albedo = append_nwn_model_asset_texture_source(
        mesh.bitmap_name, texture_source_indices, asset, stats, mesh.albedo_prefers_plt);
    sources.normal = append_nwn_model_asset_texture_source(mesh.normal_map_name, texture_source_indices, asset, stats);
    sources.metallic_roughness = append_nwn_model_asset_texture_source(
        mesh.roughness_map_name, texture_source_indices, asset, stats);
    sources.emissive = append_nwn_model_asset_texture_source(mesh.emissive_map_name, texture_source_indices, asset, stats);
    if (!clean_mtr_resource_name(mesh.specular_map_name).empty()) {
        ++stats.unsupported_specular_texture_count;
    }
    return sources;
}

nw::render::Material nwn_model_asset_material_from_mesh(const NwnMeshImportData& mesh)
{
    nw::render::Material material{};
    material.lighting_model = nw::render::MaterialLightingModel::nwn_diffuse;
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
    const NwnMeshImportData& mesh,
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
    const NwnMeshImportData& mesh,
    NwnModelAssetImportStats& stats)
{
    std::vector<Vertex> vertices;
    if (mesh.source_vertices.size() == node->vertices.size()) {
        vertices = mesh.source_vertices;
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
            if (source_bone != nw::render::kModelSkinIdentityJoint
                && (source_bone < 0 || static_cast<size_t>(source_bone) >= asset.nodes.size())) {
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
        if (source_bone != nw::render::kModelSkinIdentityJoint
            && (source_bone < 0 || static_cast<size_t>(source_bone) >= asset.nodes.size())) {
            return false;
        }

        out_slot_remap[source_slot] = static_cast<uint8_t>(out_skin.joints.size());
        out_skin.joints.push_back(static_cast<int32_t>(source_bone));
        out_skin.inverse_bind_matrices.push_back(source_bone == nw::render::kModelSkinIdentityJoint
                ? glm::mat4{1.0f}
                : glm::inverse(asset.nodes[static_cast<size_t>(source_bone)].world_transform) * mesh_bind);
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

uint32_t append_nwn_model_asset_deformer(const NwnMeshImportData& mesh,
    uint32_t source_node_index,
    nw::render::ModelAsset& asset,
    NwnModelAssetImportStats& stats)
{
    if (!mesh.deformer || source_node_index >= nw::render::kInvalidModelNodeIndex) {
        return nw::render::kInvalidModelDeformerIndex;
    }
    if (asset.deformers.size() >= nw::render::kInvalidModelDeformerIndex) {
        ++stats.deformer_overflow_count;
        return nw::render::kInvalidModelDeformerIndex;
    }

    const uint32_t index = static_cast<uint32_t>(asset.deformers.size());
    auto deformer = *mesh.deformer;
    deformer.source_node_index = source_node_index;
    switch (deformer.kind) {
    case nw::render::ModelDeformerKind::secondary_motion_chain:
        ++stats.secondary_motion_deformer_count;
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

    if (!first) {
        return;
    }

    for (const auto& particles : asset.particle_systems) {
        const auto particle_bounds = nw::render::particle_emitter_spawn_bounds(particles.effect.emitters);
        if (!particle_bounds) {
            continue;
        }
        if (first) {
            asset.bounds = *particle_bounds;
            first = false;
        } else {
            asset.bounds.min = glm::min(asset.bounds.min, particle_bounds->min);
            asset.bounds.max = glm::max(asset.bounds.max, particle_bounds->max);
        }
    }
}

bool append_nwn_model_asset_primitive(const nwm::TrimeshNode* source,
    NwnMeshImportData& mesh,
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
    primitive.deformer = append_nwn_model_asset_deformer(mesh, source_node_index, asset, stats);

    append_nwn_model_asset_material(mesh, texture_source_indices, asset, stats);
    asset.primitives.push_back(std::move(primitive));
    return true;
}

bool append_nwn_model_asset_skin_primitive(const nwm::SkinNode* source,
    NwnMeshImportData& mesh,
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
                NwnMeshImportData mesh;
                initialize_mesh_material(mesh, skin, model.classification, model.name, &stats);
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
            NwnMeshImportData mesh;
            initialize_dangly_import_data(mesh, dangly_node, stats);
            initialize_mesh_material(mesh, dangly_node, model.classification, model.name, &stats);
            append_nwn_model_asset_primitive(
                dangly_node, mesh, static_cast<uint32_t>(i), texture_source_indices, asset, stats);
            continue;
        }

        NwnMeshImportData mesh;
        initialize_mesh_material(mesh, trimesh, model.classification, model.name, &stats);
        append_nwn_model_asset_primitive(
            trimesh, mesh, static_cast<uint32_t>(i), texture_source_indices, asset, stats);
    }
}

} // namespace

std::string nwn_humanoid_palette_resref(std::string_view model_resref)
{
    const auto separator = model_resref.find('_');
    if (model_resref.size() < 5
        || model_resref[0] != 'p'
        || (model_resref[1] != 'm' && model_resref[1] != 'f')
        || separator == std::string_view::npos
        || separator < 4
        || separator + 1 >= model_resref.size()) {
        return {};
    }

    for (size_t i = 3; i < separator; ++i) {
        if (model_resref[i] < '0' || model_resref[i] > '9') {
            return {};
        }
    }

    std::string result;
    result.reserve(model_resref.size());
    result.append(model_resref.substr(0, 2));
    result.append("h0");
    result.append(model_resref.substr(separator));
    return result;
}

std::string resolve_nwn_model_albedo_resref(
    std::string_view model_resref, std::string_view albedo_resref)
{
    // A selected humanoid body-part model identifies its PLT. Its MDL bitmap
    // may name shared geometry data from another part and is only the fallback.
    auto palette_resref = nwn_humanoid_palette_resref(model_resref);
    if (texture_resource_is_plt(palette_resref)) {
        return palette_resref;
    }

    return std::string(albedo_resref);
}

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
    asset->source_kind = nw::render::ModelAssetSourceKind::nwn;
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
    append_nwn_model_asset_lights(mdl.model, *asset, result.stats);
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
