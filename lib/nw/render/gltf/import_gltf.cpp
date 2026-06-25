#include "import_gltf.hpp"

#include <stb/stb_image.h>

#include <cgltf.h>

#include <nw/log.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <unordered_map>

namespace nw::render::gltf {

namespace {

const glm::mat4 kGltfToMudl = glm::rotate(glm::mat4(1.0f), glm::half_pi<float>(), glm::vec3{1.0f, 0.0f, 0.0f});

using TextureHandle = nw::gfx::Handle<nw::gfx::Texture>;

struct TextureKey {
    const cgltf_texture* texture = nullptr;
    nw::gfx::Fmt format = nw::gfx::Fmt::RGBA8;

    bool operator==(const TextureKey& other) const noexcept
    {
        return texture == other.texture && format == other.format;
    }
};

struct TextureKeyHash {
    size_t operator()(const TextureKey& key) const noexcept
    {
        return std::hash<const void*>{}(key.texture) ^ (static_cast<size_t>(key.format) << 1u);
    }
};

struct LoadedImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
};

struct SurfaceTextureImport {
    nw::gfx::BindlessTextureIndex index = nw::gfx::kInvalidBindlessTextureIndex;
    bool material_uses_fallback = false;
};

uint32_t pack_u8x4(uint32_t x, uint32_t y, uint32_t z, uint32_t w)
{
    return (x & 0xffu) | ((y & 0xffu) << 8u) | ((z & 0xffu) << 16u) | ((w & 0xffu) << 24u);
}

uint32_t pack_skin_joint_indices(const std::vector<cgltf_uint>& joints)
{
    return pack_u8x4(
        nw::render::clamp_model_skin_joint_index(static_cast<uint32_t>(joints[0])),
        nw::render::clamp_model_skin_joint_index(static_cast<uint32_t>(joints[1])),
        nw::render::clamp_model_skin_joint_index(static_cast<uint32_t>(joints[2])),
        nw::render::clamp_model_skin_joint_index(static_cast<uint32_t>(joints[3])));
}

std::array<uint8_t, 4> pack_weights(const std::array<float, 4>& weights)
{
    std::array<uint8_t, 4> packed{};
    float sum = weights[0] + weights[1] + weights[2] + weights[3];
    if (sum <= 1.0e-6f) {
        packed[0] = 255;
        return packed;
    }

    int total = 0;
    for (size_t i = 0; i < 4; ++i) {
        packed[i] = static_cast<uint8_t>(std::clamp(std::lround((weights[i] / sum) * 255.0f), 0l, 255l));
        total += packed[i];
    }
    if (total != 255) {
        int delta = 255 - total;
        packed[0] = static_cast<uint8_t>(std::clamp(static_cast<int>(packed[0]) + delta, 0, 255));
    }
    return packed;
}

bool skin_accessors_supported_by_renderer(
    const cgltf_accessor* joints_acc, const cgltf_accessor* weights_acc, cgltf_size vertex_count, size_t skin_joint_count)
{
    if (!joints_acc || !weights_acc || joints_acc->count < vertex_count || weights_acc->count < vertex_count
        || !nw::render::model_skin_bone_count_supported(skin_joint_count)) {
        return false;
    }

    std::vector<cgltf_uint> joint_values(4);
    std::vector<float> weight_values(4);
    for (cgltf_size i = 0; i < vertex_count; ++i) {
        if (!cgltf_accessor_read_uint(joints_acc, i, joint_values.data(), 4)) {
            return false;
        }
        if (!cgltf_accessor_read_float(weights_acc, i, weight_values.data(), 4)) {
            return false;
        }
        for (cgltf_uint joint : joint_values) {
            const uint32_t joint_index = static_cast<uint32_t>(joint);
            if (joint_index > nw::render::kModelMaxSkinBoneIndex || joint_index >= skin_joint_count) {
                return false;
            }
        }
    }
    return true;
}

uint32_t full_mip_count(uint32_t width, uint32_t height)
{
    uint32_t levels = 1;
    while (width > 1 || height > 1) {
        width = std::max(width / 2u, 1u);
        height = std::max(height / 2u, 1u);
        ++levels;
    }
    return levels;
}

glm::mat4 load_matrix(const cgltf_node* node)
{
    glm::mat4 local{1.0f};
    if (node->has_matrix) {
        std::memcpy(&local, node->matrix, sizeof(local));
        return local;
    }

    if (node->has_translation) {
        local = glm::translate(local, glm::vec3(node->translation[0], node->translation[1], node->translation[2]));
    }
    if (node->has_rotation) {
        local *= glm::mat4_cast(glm::quat(node->rotation[3], node->rotation[0], node->rotation[1], node->rotation[2]));
    }
    if (node->has_scale) {
        local = glm::scale(local, glm::vec3(node->scale[0], node->scale[1], node->scale[2]));
    }
    return local;
}

void expand_bounds(nw::render::Bounds& bounds, const glm::vec3& pos, bool& first)
{
    if (first) {
        bounds.min = pos;
        bounds.max = pos;
        first = false;
        return;
    }
    bounds.min = glm::min(bounds.min, pos);
    bounds.max = glm::max(bounds.max, pos);
}

template <typename Model>
void import_node_hierarchy(const cgltf_data* data, const cgltf_node* node, int32_t parent_index,
    const glm::mat4& parent_world, Model& model)
{
    const int32_t node_index = static_cast<int32_t>(node - data->nodes);
    if (node_index < 0) return;

    auto local = load_matrix(node);
    model.nodes[node_index].parent = parent_index;
    model.nodes[node_index].local_transform = local;
    model.nodes[node_index].world_transform = parent_world * local;

    for (cgltf_size i = 0; i < node->children_count; ++i) {
        import_node_hierarchy(data, node->children[i], node_index, model.nodes[node_index].world_transform, model);
    }
}

template <typename Model>
void import_skins(const cgltf_data* data, Model& model)
{
    model.skins.reserve(data->skins_count);
    std::vector<float> values(16);
    for (cgltf_size i = 0; i < data->skins_count; ++i) {
        const auto& src = data->skins[i];
        nw::render::Skin skin{};
        skin.skeleton = static_cast<uint32_t>(i);
        skin.joints.reserve(src.joints_count);
        skin.inverse_bind_matrices.reserve(src.joints_count);
        bool warned_inverse_bind_read = false;
        for (cgltf_size j = 0; j < src.joints_count; ++j) {
            int32_t joint_index = static_cast<int32_t>(src.joints[j] - data->nodes);
            skin.joints.push_back(joint_index);
            glm::mat4 ibm{1.0f};
            if (src.inverse_bind_matrices
                && j < src.inverse_bind_matrices->count
                && cgltf_accessor_read_float(src.inverse_bind_matrices, j, values.data(), 16)) {
                for (int col = 0; col < 4; ++col) {
                    for (int row = 0; row < 4; ++row) {
                        ibm[col][row] = values[col * 4 + row];
                    }
                }
            } else {
                if (src.inverse_bind_matrices && !warned_inverse_bind_read) {
                    LOG_F(WARNING, "glTF '{}': skin {} inverse bind matrices could not be read; falling back to node transforms",
                        model.name, i);
                    warned_inverse_bind_read = true;
                }
                if (joint_index >= 0 && static_cast<size_t>(joint_index) < model.nodes.size()) {
                    ibm = glm::inverse(model.nodes[joint_index].world_transform);
                }
            }
            skin.inverse_bind_matrices.push_back(ibm);
        }
        model.skins.push_back(std::move(skin));
    }
}

template <typename Model>
void build_skeletons(const cgltf_data* data, Model& model)
{
    model.skeletons.reserve(model.skins.size());
    for (const auto& skin : model.skins) {
        nw::render::Skeleton skeleton{};
        skeleton.joints.reserve(skin.joints.size());

        std::unordered_map<int32_t, int32_t> node_to_joint;
        for (size_t i = 0; i < skin.joints.size(); ++i) {
            node_to_joint[skin.joints[i]] = static_cast<int32_t>(i);
        }

        for (size_t i = 0; i < skin.joints.size(); ++i) {
            const int32_t node_index = skin.joints[i];
            nw::render::Joint joint{};
            joint.node = node_index;
            joint.name = (node_index >= 0 && static_cast<size_t>(node_index) < data->nodes_count && data->nodes[node_index].name)
                ? data->nodes[node_index].name
                : "joint_" + std::to_string(i);
            if (i < skin.inverse_bind_matrices.size()) {
                joint.inverse_bind_matrix = skin.inverse_bind_matrices[i];
            }

            if (node_index >= 0 && static_cast<size_t>(node_index) < data->nodes_count) {
                const auto& node = model.nodes[static_cast<size_t>(node_index)];
                int32_t parent_node = node.parent;
                auto it = node_to_joint.find(parent_node);
                joint.parent = it != node_to_joint.end() ? it->second : -1;
                if (joint.parent >= 0) {
                    const glm::mat4 parent_world = model.nodes[static_cast<size_t>(skin.joints[static_cast<size_t>(joint.parent)])].world_transform;
                    joint.bind_local = nw::render::Transform::from_matrix(glm::inverse(parent_world) * node.world_transform);
                } else {
                    joint.root_correction = (parent_node >= 0 && static_cast<size_t>(parent_node) < model.nodes.size())
                        ? model.nodes[static_cast<size_t>(parent_node)].world_transform
                        : glm::mat4(1.0f);
                    joint.bind_local = nw::render::Transform::from_matrix(glm::inverse(joint.root_correction) * node.world_transform);
                }
            }
            skeleton.joints.push_back(std::move(joint));
        }

        nw::render::build_eval_order(skeleton);
        model.skeletons.push_back(std::move(skeleton));
    }
}

nw::render::InterpolationMode map_interpolation(cgltf_interpolation_type interpolation)
{
    switch (interpolation) {
    case cgltf_interpolation_type_step:
        return nw::render::InterpolationMode::step;
    default:
        return nw::render::InterpolationMode::linear;
    }
}

template <typename Model>
void import_animations(const cgltf_data* data, Model& model)
{
    model.animations.clear();
    if (model.skeletons.empty()) return;

    for (cgltf_size anim_index = 0; anim_index < data->animations_count; ++anim_index) {
        const auto& anim = data->animations[anim_index];

        for (uint32_t skeleton_index = 0; skeleton_index < model.skeletons.size(); ++skeleton_index) {
            const auto& skeleton = model.skeletons[skeleton_index];
            std::unordered_map<int32_t, uint32_t> node_to_joint;
            for (uint32_t joint_index = 0; joint_index < skeleton.joints.size(); ++joint_index) {
                node_to_joint[skeleton.joints[joint_index].node] = joint_index;
            }

            nw::render::AnimationClip clip{};
            clip.name = anim.name ? anim.name : std::string("animation_") + std::to_string(anim_index);
            if (model.skeletons.size() > 1) {
                clip.name += "#skin" + std::to_string(skeleton_index);
            }
            clip.skeleton = skeleton_index;
            clip.tracks.resize(skeleton.joints.size());

            bool used = false;
            std::vector<float> times;
            std::vector<float> values(4);
            for (cgltf_size channel_index = 0; channel_index < anim.channels_count; ++channel_index) {
                const auto& channel = anim.channels[channel_index];
                if (!channel.target_node || !channel.sampler || !channel.sampler->input || !channel.sampler->output) continue;
                const int32_t node_index = static_cast<int32_t>(channel.target_node - data->nodes);
                auto it = node_to_joint.find(node_index);
                if (it == node_to_joint.end()) continue;

                auto mode = map_interpolation(channel.sampler->interpolation);
                if (channel.sampler->output->count != channel.sampler->input->count) {
                    LOG_F(WARNING, "glTF '{}': animation '{}' channel {} input/output key counts differ; skipping channel",
                        model.name, clip.name, channel_index);
                    continue;
                }

                times.resize(channel.sampler->input->count);
                bool times_ok = true;
                float channel_duration = 0.0f;
                for (cgltf_size i = 0; i < channel.sampler->input->count; ++i) {
                    if (!cgltf_accessor_read_float(channel.sampler->input, i, &times[i], 1)) {
                        times_ok = false;
                        break;
                    }
                    channel_duration = std::max(channel_duration, times[i]);
                }
                if (!times_ok) {
                    LOG_F(WARNING, "glTF '{}': animation '{}' channel {} input times could not be read; skipping channel",
                        model.name, clip.name, channel_index);
                    continue;
                }

                switch (channel.target_path) {
                case cgltf_animation_path_type_translation: {
                    std::vector<nw::render::Keyframe<glm::vec3>> translations;
                    translations.reserve(times.size());
                    bool values_ok = true;
                    for (cgltf_size i = 0; i < channel.sampler->output->count; ++i) {
                        if (!cgltf_accessor_read_float(channel.sampler->output, i, values.data(), 3)) {
                            values_ok = false;
                            break;
                        }
                        translations.push_back({times[i], glm::vec3(values[0], values[1], values[2])});
                    }
                    if (!values_ok) {
                        LOG_F(WARNING, "glTF '{}': animation '{}' channel {} translations could not be read; skipping channel",
                            model.name, clip.name, channel_index);
                        break;
                    }
                    auto& track = clip.tracks[it->second];
                    track.translation_mode = mode;
                    track.translations = std::move(translations);
                    clip.duration = std::max(clip.duration, channel_duration);
                    used = true;
                    break;
                }
                case cgltf_animation_path_type_rotation: {
                    std::vector<nw::render::Keyframe<glm::quat>> rotations;
                    rotations.reserve(times.size());
                    bool values_ok = true;
                    for (cgltf_size i = 0; i < channel.sampler->output->count; ++i) {
                        if (!cgltf_accessor_read_float(channel.sampler->output, i, values.data(), 4)) {
                            values_ok = false;
                            break;
                        }
                        rotations.push_back({times[i], glm::normalize(glm::quat(values[3], values[0], values[1], values[2]))});
                    }
                    if (!values_ok) {
                        LOG_F(WARNING, "glTF '{}': animation '{}' channel {} rotations could not be read; skipping channel",
                            model.name, clip.name, channel_index);
                        break;
                    }
                    auto& track = clip.tracks[it->second];
                    track.rotation_mode = mode;
                    track.rotations = std::move(rotations);
                    clip.duration = std::max(clip.duration, channel_duration);
                    used = true;
                    break;
                }
                case cgltf_animation_path_type_scale: {
                    std::vector<nw::render::Keyframe<glm::vec3>> scales;
                    scales.reserve(times.size());
                    bool values_ok = true;
                    for (cgltf_size i = 0; i < channel.sampler->output->count; ++i) {
                        if (!cgltf_accessor_read_float(channel.sampler->output, i, values.data(), 3)) {
                            values_ok = false;
                            break;
                        }
                        scales.push_back({times[i], glm::vec3(values[0], values[1], values[2])});
                    }
                    if (!values_ok) {
                        LOG_F(WARNING, "glTF '{}': animation '{}' channel {} scales could not be read; skipping channel",
                            model.name, clip.name, channel_index);
                        break;
                    }
                    auto& track = clip.tracks[it->second];
                    track.scale_mode = mode;
                    track.scales = std::move(scales);
                    clip.duration = std::max(clip.duration, channel_duration);
                    used = true;
                    break;
                }
                default:
                    break;
                }
            }

            if (used) {
                model.animations.push_back(std::move(clip));
            }
        }
    }
}

TextureHandle load_texture_from_memory(nw::gfx::Context* ctx, const uint8_t* bytes, size_t size, nw::gfx::Fmt fmt)
{
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes, static_cast<int>(size), &width, &height, &channels, 4);
    if (!pixels) {
        return {};
    }

    nw::gfx::TextureDesc desc{};
    desc.width = static_cast<uint32_t>(width);
    desc.height = static_cast<uint32_t>(height);
    desc.mip_levels = full_mip_count(desc.width, desc.height);
    desc.format = fmt;
    auto texture = nw::gfx::create_texture(ctx, desc);
    if (texture.valid()) {
        nw::gfx::upload_texture_rgba8(ctx, texture, pixels, static_cast<size_t>(width) * height * 4);
    }
    stbi_image_free(pixels);
    return texture;
}

LoadedImage load_image_pixels(const cgltf_image* image, const std::filesystem::path& base_dir)
{
    LoadedImage out{};
    if (!image) return out;

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = nullptr;
    if (image->buffer_view && image->buffer_view->buffer && image->buffer_view->buffer->data) {
        const auto* data = static_cast<const uint8_t*>(image->buffer_view->buffer->data) + image->buffer_view->offset;
        pixels = stbi_load_from_memory(data, static_cast<int>(image->buffer_view->size), &width, &height, &channels, 4);
    } else if (image->uri) {
        auto path = base_dir / image->uri;
        pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    }

    if (!pixels) return out;
    out.width = width;
    out.height = height;
    out.pixels.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
    stbi_image_free(pixels);
    return out;
}

TextureHandle load_image(nw::gfx::Context* ctx, const cgltf_image* image,
    const std::filesystem::path& base_dir, nw::gfx::Fmt fmt)
{
    if (!image) return {};
    if (image->buffer_view && image->buffer_view->buffer && image->buffer_view->buffer->data) {
        const auto* data = static_cast<const uint8_t*>(image->buffer_view->buffer->data) + image->buffer_view->offset;
        return load_texture_from_memory(ctx, data, image->buffer_view->size, fmt);
    }

    if (image->uri) {
        auto path = base_dir / image->uri;
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
        if (!pixels) {
            return {};
        }
        nw::gfx::TextureDesc desc{};
        desc.width = static_cast<uint32_t>(width);
        desc.height = static_cast<uint32_t>(height);
        desc.mip_levels = full_mip_count(desc.width, desc.height);
        desc.format = fmt;
        auto texture = nw::gfx::create_texture(ctx, desc);
        if (texture.valid()) {
            nw::gfx::upload_texture_rgba8(ctx, texture, pixels, static_cast<size_t>(width) * height * 4);
        }
        stbi_image_free(pixels);
        return texture;
    }

    return {};
}

void generate_flat_normals(std::vector<nw::render::Vertex>& vertices, const std::vector<uint32_t>& indices)
{
    for (auto& v : vertices)
        v.normal = glm::vec3{0.0f};
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        auto& v0 = vertices[indices[i + 0]];
        auto& v1 = vertices[indices[i + 1]];
        auto& v2 = vertices[indices[i + 2]];
        auto n = glm::cross(v1.position - v0.position, v2.position - v0.position);
        if (glm::dot(n, n) > 1e-10f) {
            v0.normal += n;
            v1.normal += n;
            v2.normal += n;
        }
    }
    for (auto& v : vertices) {
        if (glm::dot(v.normal, v.normal) > 1e-10f)
            v.normal = glm::normalize(v.normal);
        else
            v.normal = glm::vec3{0.0f, 0.0f, 1.0f};
    }
}

void generate_tangents(std::vector<nw::render::Vertex>& vertices, const std::vector<uint32_t>& indices)
{
    std::vector<glm::vec3> tan1(vertices.size(), glm::vec3{0.0f});
    std::vector<glm::vec3> tan2(vertices.size(), glm::vec3{0.0f});
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        auto i0 = indices[i + 0];
        auto i1 = indices[i + 1];
        auto i2 = indices[i + 2];
        const auto& v0 = vertices[i0];
        const auto& v1 = vertices[i1];
        const auto& v2 = vertices[i2];
        float x1 = v1.position.x - v0.position.x;
        float x2 = v2.position.x - v0.position.x;
        float y1 = v1.position.y - v0.position.y;
        float y2 = v2.position.y - v0.position.y;
        float z1 = v1.position.z - v0.position.z;
        float z2 = v2.position.z - v0.position.z;
        float s1 = v1.texcoord.x - v0.texcoord.x;
        float s2 = v2.texcoord.x - v0.texcoord.x;
        float t1 = v1.texcoord.y - v0.texcoord.y;
        float t2 = v2.texcoord.y - v0.texcoord.y;
        float denom = s1 * t2 - s2 * t1;
        if (std::abs(denom) < 1e-10f) continue;
        float r = 1.0f / denom;
        glm::vec3 sdir{(t2 * x1 - t1 * x2) * r, (t2 * y1 - t1 * y2) * r, (t2 * z1 - t1 * z2) * r};
        glm::vec3 tdir{(s1 * x2 - s2 * x1) * r, (s1 * y2 - s2 * y1) * r, (s1 * z2 - s2 * z1) * r};
        tan1[i0] += sdir;
        tan1[i1] += sdir;
        tan1[i2] += sdir;
        tan2[i0] += tdir;
        tan2[i1] += tdir;
        tan2[i2] += tdir;
    }
    for (size_t i = 0; i < vertices.size(); ++i) {
        auto n = vertices[i].normal;
        auto t = tan1[i];
        if (glm::dot(t, t) < 1e-10f) {
            vertices[i].tangent = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f};
            continue;
        }
        t = glm::normalize(t - n * glm::dot(n, t));
        float handedness = (glm::dot(glm::cross(n, t), tan2[i]) < 0.0f) ? -1.0f : 1.0f;
        vertices[i].tangent = glm::vec4{t, handedness};
    }
}

nw::gfx::BindlessTextureIndex lookup_texture_index(
    const std::unordered_map<TextureKey, nw::gfx::BindlessTextureIndex, TextureKeyHash>& tex_map,
    const cgltf_texture* tex, nw::gfx::Fmt format, nw::gfx::BindlessTextureIndex fallback)
{
    if (!tex) return fallback;
    auto it = tex_map.find(TextureKey{tex, format});
    return it != tex_map.end() && nw::gfx::bindless_texture_index_valid(it->second) ? it->second : fallback;
}

bool texture_reference_uses_fallback(
    const std::unordered_map<TextureKey, nw::gfx::BindlessTextureIndex, TextureKeyHash>& tex_map,
    const cgltf_texture* tex, nw::gfx::Fmt format)
{
    return tex && !tex_map.contains(TextureKey{tex, format});
}

SurfaceTextureImport create_surface_texture(const cgltf_material* mat, const std::filesystem::path& base_dir,
    nw::render::RenderModel& model, const ImportGltfDesc& desc)
{
    const cgltf_texture* mr_tex = mat && mat->pbr_metallic_roughness.metallic_roughness_texture.texture
        ? mat->pbr_metallic_roughness.metallic_roughness_texture.texture
        : nullptr;
    const cgltf_texture* ao_tex = mat && mat->occlusion_texture.texture ? mat->occlusion_texture.texture : nullptr;
    if (!mr_tex && !ao_tex) {
        return SurfaceTextureImport{.index = desc.fallback_surface};
    }

    LoadedImage mr = mr_tex ? load_image_pixels(mr_tex->image, base_dir) : LoadedImage{};
    LoadedImage ao = ao_tex ? load_image_pixels(ao_tex->image, base_dir) : LoadedImage{};
    const bool source_payload_missing = (mr_tex && mr.pixels.empty()) || (ao_tex && ao.pixels.empty());

    int width = mr.width ? mr.width : ao.width;
    int height = mr.height ? mr.height : ao.height;
    if (width <= 0 || height <= 0) {
        return SurfaceTextureImport{.index = desc.fallback_surface, .material_uses_fallback = true};
    }
    if ((mr.width && (mr.width != width || mr.height != height)) || (ao.width && (ao.width != width || ao.height != height))) {
        return SurfaceTextureImport{.index = desc.fallback_surface, .material_uses_fallback = true};
    }

    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4, 255);
    for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
        pixels[i * 4 + 0] = ao.pixels.empty() ? 255 : ao.pixels[i * 4 + 0];
        pixels[i * 4 + 1] = mr.pixels.empty() ? 128 : mr.pixels[i * 4 + 1];
        pixels[i * 4 + 2] = mr.pixels.empty() ? 0 : mr.pixels[i * 4 + 2];
        pixels[i * 4 + 3] = 255;
    }

    nw::gfx::TextureDesc desc_tex{};
    desc_tex.width = static_cast<uint32_t>(width);
    desc_tex.height = static_cast<uint32_t>(height);
    desc_tex.mip_levels = full_mip_count(desc_tex.width, desc_tex.height);
    desc_tex.format = nw::gfx::Fmt::RGBA8;
    auto handle = nw::gfx::create_texture(desc.ctx, desc_tex);
    if (!handle.valid()) {
        return SurfaceTextureImport{.index = desc.fallback_surface, .material_uses_fallback = true};
    }
    nw::gfx::upload_texture_rgba8(desc.ctx, handle, pixels.data(), pixels.size());
    const auto texture_index = nw::gfx::get_bindless_texture_index(desc.ctx, handle);
    if (!nw::gfx::bindless_texture_index_valid(texture_index)) {
        nw::gfx::destroy_texture(desc.ctx, handle);
        return SurfaceTextureImport{.index = desc.fallback_surface, .material_uses_fallback = true};
    }
    model.textures.push_back(handle);
    return SurfaceTextureImport{
        .index = texture_index,
        .material_uses_fallback = source_payload_missing,
    };
}

nw::render::Material import_material(const cgltf_material* mat,
    const std::unordered_map<TextureKey, nw::gfx::BindlessTextureIndex, TextureKeyHash>& tex_map,
    const ImportGltfDesc& desc, const SurfaceTextureImport& surface)
{
    nw::render::Material m{};
    if (!mat) {
        m.albedo_index = desc.fallback_albedo;
        m.normal_index = desc.fallback_normal;
        m.surface_index = desc.fallback_surface;
        m.emissive_index = desc.fallback_emissive;
        return m;
    }

    const auto& pbr = mat->pbr_metallic_roughness;
    m.albedo = glm::vec4(pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2], pbr.base_color_factor[3]);
    m.roughness = pbr.roughness_factor;
    m.metallic = pbr.metallic_factor;
    m.emissive = glm::vec3(mat->emissive_factor[0], mat->emissive_factor[1], mat->emissive_factor[2]);
    m.normal_scale = mat->normal_texture.scale;
    m.occlusion_strength = mat->occlusion_texture.scale;

    m.albedo_index = lookup_texture_index(tex_map, pbr.base_color_texture.texture, nw::gfx::Fmt::RGBA8Srgb, desc.fallback_albedo);
    m.normal_index = lookup_texture_index(tex_map, mat->normal_texture.texture, nw::gfx::Fmt::RGBA8, desc.fallback_normal);
    m.surface_index = surface.index;
    m.emissive_index = lookup_texture_index(tex_map, mat->emissive_texture.texture, nw::gfx::Fmt::RGBA8Srgb, desc.fallback_emissive);
    m.material_uses_fallback = texture_reference_uses_fallback(tex_map, pbr.base_color_texture.texture, nw::gfx::Fmt::RGBA8Srgb)
        || texture_reference_uses_fallback(tex_map, mat->normal_texture.texture, nw::gfx::Fmt::RGBA8)
        || texture_reference_uses_fallback(tex_map, mat->emissive_texture.texture, nw::gfx::Fmt::RGBA8Srgb)
        || surface.material_uses_fallback;

    switch (mat->alpha_mode) {
    case cgltf_alpha_mode_mask:
        m.alpha_mode = nw::render::MaterialMode::cutout;
        break;
    case cgltf_alpha_mode_blend:
        m.alpha_mode = nw::render::MaterialMode::transparent;
        break;
    default:
        m.alpha_mode = nw::render::MaterialMode::opaque;
        break;
    }
    m.alpha_cutoff = mat->alpha_cutoff;
    m.double_sided = mat->double_sided;
    return m;
}

nw::render::Material import_model_asset_material(const cgltf_material* mat)
{
    nw::render::Material m{};
    if (!mat) {
        return m;
    }

    const auto& pbr = mat->pbr_metallic_roughness;
    m.albedo = glm::vec4(pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2], pbr.base_color_factor[3]);
    m.roughness = pbr.roughness_factor;
    m.metallic = pbr.metallic_factor;
    m.emissive = glm::vec3(mat->emissive_factor[0], mat->emissive_factor[1], mat->emissive_factor[2]);
    m.normal_scale = mat->normal_texture.scale;
    m.occlusion_strength = mat->occlusion_texture.scale;

    switch (mat->alpha_mode) {
    case cgltf_alpha_mode_mask:
        m.alpha_mode = nw::render::MaterialMode::cutout;
        break;
    case cgltf_alpha_mode_blend:
        m.alpha_mode = nw::render::MaterialMode::transparent;
        break;
    default:
        m.alpha_mode = nw::render::MaterialMode::opaque;
        break;
    }
    m.alpha_cutoff = mat->alpha_cutoff;
    m.double_sided = mat->double_sided;
    return m;
}

uint32_t import_model_asset_texture_source(const cgltf_texture* texture,
    const std::filesystem::path& base_dir,
    std::unordered_map<const cgltf_image*, uint32_t>& image_map,
    nw::render::ModelAsset& asset)
{
    if (!texture || !texture->image) {
        return nw::render::kInvalidModelAssetTextureSourceIndex;
    }

    const cgltf_image* image = texture->image;
    auto it = image_map.find(image);
    if (it != image_map.end()) {
        return it->second;
    }

    nw::render::ModelAssetTextureSource source{};
    if (image->buffer_view && image->buffer_view->buffer && image->buffer_view->buffer->data) {
        const auto* data = static_cast<const uint8_t*>(image->buffer_view->buffer->data) + image->buffer_view->offset;
        source.kind = nw::render::ModelAssetTextureSourceKind::encoded_bytes;
        source.encoded_bytes.assign(data, data + image->buffer_view->size);
    } else if (image->uri) {
        source.kind = nw::render::ModelAssetTextureSourceKind::external_file;
        source.path = (base_dir / image->uri).string();
    } else {
        return nw::render::kInvalidModelAssetTextureSourceIndex;
    }

    if (asset.texture_sources.size() >= nw::render::kInvalidModelAssetTextureSourceIndex) {
        return nw::render::kInvalidModelAssetTextureSourceIndex;
    }

    const uint32_t index = static_cast<uint32_t>(asset.texture_sources.size());
    asset.texture_sources.push_back(std::move(source));
    image_map[image] = index;
    return index;
}

nw::render::ModelAssetMaterialTextureSources import_model_asset_material_texture_sources(
    const cgltf_material* mat,
    const std::filesystem::path& base_dir,
    std::unordered_map<const cgltf_image*, uint32_t>& image_map,
    nw::render::ModelAsset& asset)
{
    nw::render::ModelAssetMaterialTextureSources sources{};
    if (!mat) {
        return sources;
    }

    sources.albedo = import_model_asset_texture_source(
        mat->pbr_metallic_roughness.base_color_texture.texture, base_dir, image_map, asset);
    sources.normal = import_model_asset_texture_source(mat->normal_texture.texture, base_dir, image_map, asset);
    sources.metallic_roughness = import_model_asset_texture_source(
        mat->pbr_metallic_roughness.metallic_roughness_texture.texture, base_dir, image_map, asset);
    sources.occlusion = import_model_asset_texture_source(mat->occlusion_texture.texture, base_dir, image_map, asset);
    sources.emissive = import_model_asset_texture_source(mat->emissive_texture.texture, base_dir, image_map, asset);
    return sources;
}

bool read_indices(const cgltf_accessor* accessor, std::vector<uint32_t>& out)
{
    if (!accessor) return false;
    out.resize(accessor->count);
    const cgltf_size read_count = cgltf_accessor_unpack_indices(accessor, out.data(), sizeof(uint32_t), accessor->count);
    if (read_count != accessor->count) {
        out.clear();
        return false;
    }
    return true;
}

bool indices_reference_vertices(const std::vector<uint32_t>& indices, size_t vertex_count) noexcept
{
    for (const uint32_t index : indices) {
        if (index >= vertex_count) {
            return false;
        }
    }
    return true;
}

bool decode_model_asset_primitive(const cgltf_primitive& primitive, uint32_t material_index, uint32_t skin_index, int32_t node_index,
    const glm::mat4& world,
    const std::vector<nw::render::Skin>& skins,
    const std::vector<nw::render::Node>& nodes,
    const std::string& model_name,
    size_t primitive_index,
    nw::render::ModelAssetPrimitive& out)
{
    if (primitive.type != cgltf_primitive_type_triangles) return false;

    const cgltf_accessor* pos_acc = nullptr;
    const cgltf_accessor* norm_acc = nullptr;
    const cgltf_accessor* uv_acc = nullptr;
    const cgltf_accessor* tan_acc = nullptr;
    const cgltf_accessor* joints_acc = nullptr;
    const cgltf_accessor* weights_acc = nullptr;
    for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
        const auto& attr = primitive.attributes[i];
        switch (attr.type) {
        case cgltf_attribute_type_position:
            pos_acc = attr.data;
            break;
        case cgltf_attribute_type_normal:
            norm_acc = attr.data;
            break;
        case cgltf_attribute_type_texcoord:
            if (attr.index == 0) uv_acc = attr.data;
            break;
        case cgltf_attribute_type_tangent:
            tan_acc = attr.data;
            break;
        case cgltf_attribute_type_joints:
            if (attr.index == 0) joints_acc = attr.data;
            break;
        case cgltf_attribute_type_weights:
            if (attr.index == 0) weights_acc = attr.data;
            break;
        default:
            break;
        }
    }
    if (!pos_acc || pos_acc->count == 0) return false;
    if (norm_acc && norm_acc->count < pos_acc->count) {
        LOG_F(WARNING, "glTF '{}': primitive {} normal accessor is shorter than positions; generating normals",
            model_name, primitive_index);
        norm_acc = nullptr;
    }
    if (uv_acc && uv_acc->count < pos_acc->count) {
        LOG_F(WARNING, "glTF '{}': primitive {} texcoord accessor is shorter than positions; using default texcoords",
            model_name, primitive_index);
        uv_acc = nullptr;
    }
    if (tan_acc && tan_acc->count < pos_acc->count) {
        LOG_F(WARNING, "glTF '{}': primitive {} tangent accessor is shorter than positions; regenerating tangents",
            model_name, primitive_index);
        tan_acc = nullptr;
    }

    std::vector<nw::render::Vertex> vertices(pos_acc->count);
    std::vector<float> values(4);
    for (cgltf_size i = 0; i < pos_acc->count; ++i) {
        if (!cgltf_accessor_read_float(pos_acc, i, values.data(), 3)) {
            LOG_F(WARNING, "glTF '{}': primitive {} positions could not be read; dropping primitive",
                model_name, primitive_index);
            return false;
        }
        vertices[i].position = glm::vec3(values[0], values[1], values[2]);
        if (norm_acc) {
            if (!cgltf_accessor_read_float(norm_acc, i, values.data(), 3)) {
                LOG_F(WARNING, "glTF '{}': primitive {} normals could not be read; generating normals",
                    model_name, primitive_index);
                norm_acc = nullptr;
            } else {
                vertices[i].normal = glm::vec3(values[0], values[1], values[2]);
            }
        }
        if (uv_acc) {
            if (!cgltf_accessor_read_float(uv_acc, i, values.data(), 2)) {
                LOG_F(WARNING, "glTF '{}': primitive {} texcoords could not be read; using default texcoords",
                    model_name, primitive_index);
                for (auto& vertex : vertices) {
                    vertex.texcoord = {};
                }
                uv_acc = nullptr;
            } else {
                vertices[i].texcoord = glm::vec2(values[0], values[1]);
            }
        }
        if (tan_acc) {
            if (!cgltf_accessor_read_float(tan_acc, i, values.data(), 4)) {
                LOG_F(WARNING, "glTF '{}': primitive {} tangents could not be read; regenerating tangents",
                    model_name, primitive_index);
                tan_acc = nullptr;
            } else {
                vertices[i].tangent = glm::vec4(values[0], values[1], values[2], values[3]);
            }
        }
    }

    std::vector<uint32_t> indices;
    if (primitive.indices) {
        if (!read_indices(primitive.indices, indices)) {
            LOG_F(WARNING, "glTF '{}': primitive {} indices could not be read; dropping primitive",
                model_name, primitive_index);
            return false;
        }
    } else {
        indices.resize(vertices.size());
        for (size_t i = 0; i < indices.size(); ++i)
            indices[i] = static_cast<uint32_t>(i);
    }
    if (indices.empty()) {
        return false;
    }
    if (!indices_reference_vertices(indices, vertices.size())) {
        LOG_F(WARNING, "glTF '{}': primitive {} index references a missing vertex; dropping primitive",
            model_name, primitive_index);
        return false;
    }

    if (!norm_acc) {
        generate_flat_normals(vertices, indices);
    }
    if (!tan_acc) {
        if (uv_acc)
            generate_tangents(vertices, indices);
        else {
            for (auto& v : vertices)
                v.tangent = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f};
        }
    }

    out.transform = world;
    out.skin = skin_index;
    out.node = node_index;
    out.skinned = joints_acc && weights_acc && skin_index != std::numeric_limits<uint32_t>::max();
    if (out.skinned) {
        const size_t skin_joint_count = skin_index < skins.size() ? skins[skin_index].joints.size() : 0;
        const bool skin_supported = skin_index < skins.size()
            && skin_accessors_supported_by_renderer(joints_acc, weights_acc, pos_acc->count, skin_joint_count);
        if (!skin_supported) {
            LOG_F(WARNING,
                "glTF '{}': primitive {} skin {} has unsupported or unreadable skin attributes; importing primitive as static bind pose",
                model_name,
                primitive_index,
                skin_index);
            out.skinned = false;
            out.skin = std::numeric_limits<uint32_t>::max();
        }
    }
    bool first = true;
    std::vector<cgltf_uint> joint_values(4);
    std::vector<float> weight_values(4);
    for (cgltf_size i = 0; i < pos_acc->count; ++i) {
        glm::vec3 world_pos{0.0f};
        if (out.skinned && skin_index < skins.size()) {
            const auto& skin = skins[skin_index];
            if (!cgltf_accessor_read_uint(joints_acc, i, joint_values.data(), 4)
                || !cgltf_accessor_read_float(weights_acc, i, weight_values.data(), 4)) {
                LOG_F(WARNING, "glTF '{}': primitive {} skin attributes could not be read; dropping primitive",
                    model_name, primitive_index);
                return false;
            }
            float weight_sum = weight_values[0] + weight_values[1] + weight_values[2] + weight_values[3];
            if (weight_sum <= 1.0e-6f) {
                weight_values[0] = 1.0f;
                weight_values[1] = weight_values[2] = weight_values[3] = 0.0f;
                weight_sum = 1.0f;
            }
            for (int lane = 0; lane < 4; ++lane) {
                const float weight = weight_values[static_cast<size_t>(lane)] / weight_sum;
                if (weight <= 1.0e-6f) continue;
                const uint32_t slot = static_cast<uint32_t>(joint_values[static_cast<size_t>(lane)]);
                if (slot >= skin.joints.size() || slot >= skin.inverse_bind_matrices.size()) continue;
                const int32_t joint = skin.joints[slot];
                if (joint < 0 || static_cast<size_t>(joint) >= nodes.size()) continue;
                glm::vec4 transformed = nodes[static_cast<size_t>(joint)].world_transform
                    * skin.inverse_bind_matrices[slot] * glm::vec4(vertices[i].position, 1.0f);
                world_pos += glm::vec3(transformed) * weight;
            }
        } else {
            world_pos = glm::vec3(world * glm::vec4(vertices[i].position, 1.0f));
        }
        expand_bounds(out.bounds, world_pos, first);
    }

    if (out.skinned) {
        out.skinned_vertices.resize(vertices.size());
        for (cgltf_size i = 0; i < pos_acc->count; ++i) {
            out.skinned_vertices[i].position = vertices[i].position;
            out.skinned_vertices[i].normal = vertices[i].normal;
            out.skinned_vertices[i].texcoord = vertices[i].texcoord;
            out.skinned_vertices[i].tangent = vertices[i].tangent;
            if (!cgltf_accessor_read_uint(joints_acc, i, joint_values.data(), 4)
                || !cgltf_accessor_read_float(weights_acc, i, weight_values.data(), 4)) {
                LOG_F(WARNING, "glTF '{}': primitive {} skin attributes could not be read; dropping primitive",
                    model_name, primitive_index);
                return false;
            }
            auto packed_weights = pack_weights({weight_values[0], weight_values[1], weight_values[2], weight_values[3]});
            out.skinned_vertices[i].joint_indices = pack_skin_joint_indices(joint_values);
            out.skinned_vertices[i].joint_weights = pack_u8x4(
                packed_weights[0], packed_weights[1], packed_weights[2], packed_weights[3]);
        }
    } else {
        out.vertices = std::move(vertices);
    }

    out.indices = std::move(indices);
    out.material = material_index;
    return true;
}

void import_primitive(const cgltf_primitive& primitive, uint32_t material_index, uint32_t skin_index, int32_t node_index,
    const glm::mat4& world,
    const ImportGltfDesc& desc, nw::render::RenderModel& model)
{
    nw::render::ModelAssetPrimitive decoded{};
    if (!decode_model_asset_primitive(primitive,
            material_index,
            skin_index,
            node_index,
            world,
            model.skins,
            model.nodes,
            model.name,
            model.primitives.size(),
            decoded)) {
        return;
    }

    nw::render::Primitive out{};
    if (!nw::render::upload_model_asset_primitive(desc.ctx, decoded, out)) {
        LOG_F(WARNING, "glTF '{}': primitive {} GPU upload failed; dropping primitive",
            model.name, model.primitives.size());
        return;
    }
    model.primitives.push_back(out);
}

void import_model_asset_primitive(const cgltf_primitive& primitive, uint32_t material_index, uint32_t skin_index, int32_t node_index,
    const glm::mat4& world, nw::render::ModelAsset& asset)
{
    nw::render::ModelAssetPrimitive out{};
    if (decode_model_asset_primitive(primitive,
            material_index,
            skin_index,
            node_index,
            world,
            asset.skins,
            asset.nodes,
            asset.name,
            asset.primitives.size(),
            out)) {
        asset.primitives.push_back(std::move(out));
    }
}

template <typename ImportPrimitiveFn>
void walk_node_primitives(const cgltf_data* data, const cgltf_node* node, const glm::mat4& parent,
    size_t material_count, ImportPrimitiveFn& import_primitive_fn)
{
    glm::mat4 world = parent * load_matrix(node);
    if (node->mesh) {
        for (cgltf_size i = 0; i < node->mesh->primitives_count; ++i) {
            uint32_t material_index = 0;
            if (node->mesh->primitives[i].material && material_count != 0) {
                material_index = static_cast<uint32_t>(node->mesh->primitives[i].material - data->materials);
            }
            const int32_t node_index = static_cast<int32_t>(node - data->nodes);
            uint32_t skin_index = std::numeric_limits<uint32_t>::max();
            if (node->skin) {
                skin_index = static_cast<uint32_t>(node->skin - data->skins);
            }
            import_primitive_fn(node->mesh->primitives[i], material_index, skin_index, node_index, world);
        }
    }
    for (cgltf_size i = 0; i < node->children_count; ++i) {
        walk_node_primitives(data, node->children[i], world, material_count, import_primitive_fn);
    }
}

template <typename Model>
void expand_model_bounds_from_skin_joints(Model& model)
{
    bool joint_first_bound = model.primitives.empty();
    for (const auto& skin : model.skins) {
        for (const auto joint : skin.joints) {
            if (joint < 0 || static_cast<size_t>(joint) >= model.nodes.size()) {
                continue;
            }
            glm::vec3 world_pos = glm::vec3(model.nodes[static_cast<size_t>(joint)].world_transform[3]);
            expand_bounds(model.bounds, world_pos, joint_first_bound);
        }
    }
}

template <typename Model>
void merge_model_primitive_bounds(Model& model)
{
    bool first_bound = model.skins.empty();
    for (const auto& prim : model.primitives) {
        if (first_bound) {
            model.bounds = prim.bounds;
            first_bound = false;
        } else {
            model.bounds.min = glm::min(model.bounds.min, prim.bounds.min);
            model.bounds.max = glm::max(model.bounds.max, prim.bounds.max);
        }
    }
}

} // namespace

std::unique_ptr<nw::render::ModelAsset> import_gltf_model_asset(const std::filesystem::path& path)
{
    if (path.empty()) return {};

    cgltf_options options{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, path.string().c_str(), &data) != cgltf_result_success || !data) {
        LOG_F(ERROR, "Failed to parse glTF: {}", path.string());
        return {};
    }

    auto cleanup = [&]() {
        if (data) cgltf_free(data);
    };

    if (cgltf_validate(data) != cgltf_result_success) {
        LOG_F(ERROR, "Invalid glTF: {}", path.string());
        cleanup();
        return {};
    }

    if (cgltf_load_buffers(&options, data, path.string().c_str()) != cgltf_result_success) {
        LOG_F(ERROR, "Failed to load glTF buffers: {}", path.string());
        cleanup();
        return {};
    }

    auto asset = std::make_unique<nw::render::ModelAsset>();
    asset->source_kind = nw::render::ModelAssetSourceKind::gltf;
    asset->name = path.filename().string();
    asset->nodes.resize(data->nodes_count);

    std::unordered_map<const cgltf_image*, uint32_t> image_map;
    const auto base_dir = path.parent_path();
    asset->materials.reserve(data->materials_count > 0 ? data->materials_count : 1);
    asset->material_texture_sources.reserve(data->materials_count > 0 ? data->materials_count : 1);
    if (data->materials_count == 0) {
        asset->materials.push_back(import_model_asset_material(nullptr));
        asset->material_texture_sources.push_back(nw::render::ModelAssetMaterialTextureSources{});
    } else {
        for (cgltf_size i = 0; i < data->materials_count; ++i) {
            asset->materials.push_back(import_model_asset_material(&data->materials[i]));
            asset->material_texture_sources.push_back(
                import_model_asset_material_texture_sources(&data->materials[i], base_dir, image_map, *asset));
        }
    }

    for (cgltf_size i = 0; i < data->scenes_count; ++i) {
        const cgltf_scene* scene = &data->scenes[i];
        if (data->scene && scene != data->scene) continue;
        for (cgltf_size j = 0; j < scene->nodes_count; ++j) {
            import_node_hierarchy(data, scene->nodes[j], -1, kGltfToMudl, *asset);
        }
    }

    import_skins(data, *asset);
    build_skeletons(data, *asset);
    import_animations(data, *asset);
    expand_model_bounds_from_skin_joints(*asset);

    auto import_asset_primitive = [&](const cgltf_primitive& primitive,
                                      uint32_t material_index,
                                      uint32_t skin_index,
                                      int32_t node_index,
                                      const glm::mat4& world) {
        import_model_asset_primitive(primitive, material_index, skin_index, node_index, world, *asset);
    };

    for (cgltf_size i = 0; i < data->scenes_count; ++i) {
        const cgltf_scene* scene = &data->scenes[i];
        if (data->scene && scene != data->scene) continue;
        for (cgltf_size j = 0; j < scene->nodes_count; ++j) {
            walk_node_primitives(data, scene->nodes[j], kGltfToMudl, asset->materials.size(), import_asset_primitive);
        }
    }

    merge_model_primitive_bounds(*asset);
    asset->shadow = nw::render::summarize_model_asset_shadows(*asset);
    cleanup();

    if (asset->primitives.empty()) {
        return {};
    }
    const auto validation = nw::render::validate_model_asset(*asset);
    if (!validation.passed()) {
        LOG_F(ERROR,
            "glTF '{}': decoded ModelAsset failed validation: primitives={} invalid={} invalid_asset_rows={} invalid_material_texture_bindings={}",
            asset->name,
            validation.primitive_count,
            validation.invalid_primitive_count(),
            validation.invalid_asset_row_count(),
            validation.invalid_material_texture_binding_count);
        return {};
    }
    return asset;
}

GltfRenderModelImportResult import_gltf_render_model_from_asset(
    const std::filesystem::path& path,
    const ImportGltfDesc& desc)
{
    GltfRenderModelImportResult result{};

    auto asset = import_gltf_model_asset(path);
    if (!asset) {
        return result;
    }
    result.asset_imported = true;

    auto uploaded = nw::render::upload_model_asset(*asset, desc.ctx);
    result.geometry_upload_stats = uploaded.stats;
    if (!uploaded.model) {
        return result;
    }

    nw::render::ModelAssetTextureUploadDesc texture_upload{};
    texture_upload.ctx = desc.ctx;
    texture_upload.fallback_albedo = desc.fallback_albedo;
    texture_upload.fallback_normal = desc.fallback_normal;
    texture_upload.fallback_surface = desc.fallback_surface;
    texture_upload.fallback_emissive = desc.fallback_emissive;
    result.texture_upload_stats = nw::render::upload_model_asset_material_textures(
        *asset, texture_upload, *uploaded.model);
    result.model = std::move(uploaded.model);
    return result;
}

std::unique_ptr<nw::render::RenderModel> import_gltf(const std::filesystem::path& path, const ImportGltfDesc& desc)
{
    if (!desc.ctx || path.empty()) return {};

    cgltf_options options{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, path.string().c_str(), &data) != cgltf_result_success || !data) {
        LOG_F(ERROR, "Failed to parse glTF: {}", path.string());
        return {};
    }

    auto cleanup = [&]() {
        if (data) cgltf_free(data);
    };

    if (cgltf_validate(data) != cgltf_result_success) {
        LOG_F(ERROR, "Invalid glTF: {}", path.string());
        cleanup();
        return {};
    }

    if (cgltf_load_buffers(&options, data, path.string().c_str()) != cgltf_result_success) {
        LOG_F(ERROR, "Failed to load glTF buffers: {}", path.string());
        cleanup();
        return {};
    }

    auto model = std::make_unique<nw::render::RenderModel>();
    model->name = path.filename().string();
    model->nodes.resize(data->nodes_count);

    std::unordered_map<TextureKey, nw::gfx::BindlessTextureIndex, TextureKeyHash> tex_map;
    const auto base_dir = path.parent_path();
    auto load_material_texture = [&](const cgltf_texture* texture, nw::gfx::Fmt fmt) {
        if (!texture) return;
        TextureKey key{texture, fmt};
        if (tex_map.contains(key)) return;
        auto handle = load_image(desc.ctx, texture->image, base_dir, fmt);
        if (handle.valid()) {
            const auto texture_index = nw::gfx::get_bindless_texture_index(desc.ctx, handle);
            if (!nw::gfx::bindless_texture_index_valid(texture_index)) {
                nw::gfx::destroy_texture(desc.ctx, handle);
                return;
            }
            model->textures.push_back(handle);
            tex_map[key] = texture_index;
        }
    };

    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        const auto& material = data->materials[i];
        load_material_texture(material.pbr_metallic_roughness.base_color_texture.texture, nw::gfx::Fmt::RGBA8Srgb);
        load_material_texture(material.normal_texture.texture, nw::gfx::Fmt::RGBA8);
        load_material_texture(material.pbr_metallic_roughness.metallic_roughness_texture.texture, nw::gfx::Fmt::RGBA8);
        load_material_texture(material.emissive_texture.texture, nw::gfx::Fmt::RGBA8Srgb);
    }

    if (data->materials_count == 0) {
        for (cgltf_size i = 0; i < data->textures_count; ++i) {
            const auto& texture = data->textures[i];
            auto handle = load_image(desc.ctx, texture.image, base_dir, nw::gfx::Fmt::RGBA8Srgb);
            if (handle.valid()) {
                const auto texture_index = nw::gfx::get_bindless_texture_index(desc.ctx, handle);
                if (!nw::gfx::bindless_texture_index_valid(texture_index)) {
                    nw::gfx::destroy_texture(desc.ctx, handle);
                    continue;
                }
                model->textures.push_back(handle);
                tex_map[TextureKey{&texture, nw::gfx::Fmt::RGBA8Srgb}] = texture_index;
            }
        }
    }

    model->materials.reserve(data->materials_count > 0 ? data->materials_count : 1);
    if (data->materials_count == 0) {
        nw::render::Material material{};
        material.albedo_index = desc.fallback_albedo;
        material.normal_index = desc.fallback_normal;
        material.surface_index = desc.fallback_surface;
        material.emissive_index = desc.fallback_emissive;
        model->materials.push_back(material);
    } else {
        for (cgltf_size i = 0; i < data->materials_count; ++i) {
            const auto surface = create_surface_texture(&data->materials[i], base_dir, *model, desc);
            model->materials.push_back(import_material(&data->materials[i], tex_map, desc, surface));
        }
    }

    for (cgltf_size i = 0; i < data->scenes_count; ++i) {
        const cgltf_scene* scene = &data->scenes[i];
        if (data->scene && scene != data->scene) continue;
        for (cgltf_size j = 0; j < scene->nodes_count; ++j) {
            import_node_hierarchy(data, scene->nodes[j], -1, kGltfToMudl, *model);
        }
    }

    import_skins(data, *model);
    build_skeletons(data, *model);
    import_animations(data, *model);

    // Skinned glTF meshes can have misleading raw mesh bounds relative to the final posed model,
    // especially when the asset carries authoring-space root transforms (for example CesiumMan's
    // Z_UP / Armature chain). Expand the model bounds by retained joint world positions so camera
    // fitting uses a sane envelope for skinned assets.
    expand_model_bounds_from_skin_joints(*model);

    auto import_render_primitive = [&](const cgltf_primitive& primitive,
                                       uint32_t material_index,
                                       uint32_t skin_index,
                                       int32_t node_index,
                                       const glm::mat4& world) {
        import_primitive(primitive, material_index, skin_index, node_index, world, desc, *model);
    };

    for (cgltf_size i = 0; i < data->scenes_count; ++i) {
        const cgltf_scene* scene = &data->scenes[i];
        if (data->scene && scene != data->scene) continue;
        for (cgltf_size j = 0; j < scene->nodes_count; ++j) {
            walk_node_primitives(data, scene->nodes[j], kGltfToMudl, model->materials.size(), import_render_primitive);
        }
    }

    // Merge primitive bounds into any existing joint-expanded bounds.
    merge_model_primitive_bounds(*model);
    model->shadow = nw::render::summarize_render_model_shadows(*model);

    cleanup();
    if (model->primitives.empty()) {
        return {};
    }
    return model;
}

} // namespace nw::render::gltf
