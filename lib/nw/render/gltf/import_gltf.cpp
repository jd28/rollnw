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

uint32_t pack_u8x4(uint32_t x, uint32_t y, uint32_t z, uint32_t w)
{
    return (x & 0xffu) | ((y & 0xffu) << 8u) | ((z & 0xffu) << 16u) | ((w & 0xffu) << 24u);
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

void import_node_hierarchy(const cgltf_data* data, const cgltf_node* node, int32_t parent_index,
    const glm::mat4& parent_world, nw::render::RenderModel& model)
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

void import_skins(const cgltf_data* data, nw::render::RenderModel& model)
{
    model.skins.reserve(data->skins_count);
    std::vector<float> values(16);
    for (cgltf_size i = 0; i < data->skins_count; ++i) {
        const auto& src = data->skins[i];
        nw::render::Skin skin{};
        skin.joints.reserve(src.joints_count);
        skin.inverse_bind_matrices.reserve(src.joints_count);
        for (cgltf_size j = 0; j < src.joints_count; ++j) {
            int32_t joint_index = static_cast<int32_t>(src.joints[j] - data->nodes);
            skin.joints.push_back(joint_index);
            glm::mat4 ibm{1.0f};
            if (src.inverse_bind_matrices) {
                cgltf_accessor_read_float(src.inverse_bind_matrices, j, values.data(), 16);
                for (int col = 0; col < 4; ++col) {
                    for (int row = 0; row < 4; ++row) {
                        ibm[col][row] = values[col * 4 + row];
                    }
                }
            } else if (joint_index >= 0 && static_cast<size_t>(joint_index) < model.nodes.size()) {
                ibm = glm::inverse(model.nodes[joint_index].world_transform);
            }
            skin.inverse_bind_matrices.push_back(ibm);
        }
        model.skins.push_back(std::move(skin));
    }
}

void build_skeletons(const cgltf_data* data, nw::render::RenderModel& model)
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

void import_animations(const cgltf_data* data, nw::render::RenderModel& model)
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

                auto& track = clip.tracks[it->second];
                auto mode = map_interpolation(channel.sampler->interpolation);
                times.resize(channel.sampler->input->count);
                for (cgltf_size i = 0; i < channel.sampler->input->count; ++i) {
                    cgltf_accessor_read_float(channel.sampler->input, i, &times[i], 1);
                    clip.duration = std::max(clip.duration, times[i]);
                }

                switch (channel.target_path) {
                case cgltf_animation_path_type_translation:
                    track.translation_mode = mode;
                    track.translations.reserve(times.size());
                    for (cgltf_size i = 0; i < channel.sampler->output->count; ++i) {
                        cgltf_accessor_read_float(channel.sampler->output, i, values.data(), 3);
                        track.translations.push_back({times[i], glm::vec3(values[0], values[1], values[2])});
                    }
                    used = true;
                    break;
                case cgltf_animation_path_type_rotation:
                    track.rotation_mode = mode;
                    track.rotations.reserve(times.size());
                    for (cgltf_size i = 0; i < channel.sampler->output->count; ++i) {
                        cgltf_accessor_read_float(channel.sampler->output, i, values.data(), 4);
                        track.rotations.push_back({times[i], glm::normalize(glm::quat(values[3], values[0], values[1], values[2]))});
                    }
                    used = true;
                    break;
                case cgltf_animation_path_type_scale:
                    track.scale_mode = mode;
                    track.scales.reserve(times.size());
                    for (cgltf_size i = 0; i < channel.sampler->output->count; ++i) {
                        cgltf_accessor_read_float(channel.sampler->output, i, values.data(), 3);
                        track.scales.push_back({times[i], glm::vec3(values[0], values[1], values[2])});
                    }
                    used = true;
                    break;
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

bool create_buffer_from_bytes(nw::gfx::Context* ctx, const void* data, size_t size, nw::gfx::BufferUsage usage,
    nw::gfx::Handle<nw::gfx::Buffer>& out)
{
    nw::gfx::BufferDesc desc{};
    desc.size = size;
    desc.usage = usage;
    desc.cpu_visible = true;
    out = nw::gfx::create_buffer(ctx, desc);
    if (!out.valid()) return false;
    void* mapped = nw::gfx::map_buffer(out);
    if (!mapped) {
        nw::gfx::destroy_buffer(out);
        out = {};
        return false;
    }
    std::memcpy(mapped, data, size);
    nw::gfx::unmap_buffer(out);
    return true;
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
    return it != tex_map.end() ? it->second : fallback;
}

nw::gfx::BindlessTextureIndex create_surface_texture(const cgltf_material* mat, const std::filesystem::path& base_dir,
    nw::render::RenderModel& model, const ImportGltfDesc& desc)
{
    const cgltf_texture* mr_tex = mat && mat->pbr_metallic_roughness.metallic_roughness_texture.texture
        ? mat->pbr_metallic_roughness.metallic_roughness_texture.texture
        : nullptr;
    const cgltf_texture* ao_tex = mat && mat->occlusion_texture.texture ? mat->occlusion_texture.texture : nullptr;
    if (!mr_tex && !ao_tex) {
        return desc.fallback_surface;
    }

    LoadedImage mr = mr_tex ? load_image_pixels(mr_tex->image, base_dir) : LoadedImage{};
    LoadedImage ao = ao_tex ? load_image_pixels(ao_tex->image, base_dir) : LoadedImage{};

    int width = mr.width ? mr.width : ao.width;
    int height = mr.height ? mr.height : ao.height;
    if (width <= 0 || height <= 0) {
        return desc.fallback_surface;
    }
    if ((mr.width && (mr.width != width || mr.height != height)) || (ao.width && (ao.width != width || ao.height != height))) {
        return desc.fallback_surface;
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
        return desc.fallback_surface;
    }
    nw::gfx::upload_texture_rgba8(desc.ctx, handle, pixels.data(), pixels.size());
    model.textures.push_back(handle);
    return nw::gfx::get_bindless_texture_index(desc.ctx, handle);
}

nw::render::Material import_material(const cgltf_material* mat,
    const std::unordered_map<TextureKey, nw::gfx::BindlessTextureIndex, TextureKeyHash>& tex_map,
    const ImportGltfDesc& desc, nw::gfx::BindlessTextureIndex surface_index)
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
    m.surface_index = surface_index;
    m.emissive_index = lookup_texture_index(tex_map, mat->emissive_texture.texture, nw::gfx::Fmt::RGBA8Srgb, desc.fallback_emissive);

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

bool read_indices(const cgltf_accessor* accessor, std::vector<uint32_t>& out)
{
    if (!accessor) return false;
    out.resize(accessor->count);
    for (cgltf_size i = 0; i < accessor->count; ++i) {
        out[i] = static_cast<uint32_t>(cgltf_accessor_read_index(accessor, i));
    }
    return true;
}

void import_primitive(const cgltf_primitive& primitive, uint32_t material_index, uint32_t skin_index, int32_t node_index,
    const glm::mat4& world,
    const ImportGltfDesc& desc, nw::render::RenderModel& model)
{
    if (primitive.type != cgltf_primitive_type_triangles) return;

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
    if (!pos_acc) return;

    std::vector<nw::render::Vertex> vertices(pos_acc->count);
    std::vector<float> values(4);
    for (cgltf_size i = 0; i < pos_acc->count; ++i) {
        cgltf_accessor_read_float(pos_acc, i, values.data(), 3);
        vertices[i].position = glm::vec3(values[0], values[1], values[2]);
        if (norm_acc) {
            cgltf_accessor_read_float(norm_acc, i, values.data(), 3);
            vertices[i].normal = glm::vec3(values[0], values[1], values[2]);
        }
        if (uv_acc) {
            cgltf_accessor_read_float(uv_acc, i, values.data(), 2);
            vertices[i].texcoord = glm::vec2(values[0], values[1]);
        }
        if (tan_acc) {
            cgltf_accessor_read_float(tan_acc, i, values.data(), 4);
            vertices[i].tangent = glm::vec4(values[0], values[1], values[2], values[3]);
        }
    }

    std::vector<uint32_t> indices;
    if (primitive.indices) {
        read_indices(primitive.indices, indices);
    } else {
        indices.resize(vertices.size());
        for (uint32_t i = 0; i < indices.size(); ++i)
            indices[i] = i;
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

    nw::render::Primitive out{};
    out.transform = world;
    out.skin = skin_index;
    out.node = node_index;
    out.skinned = joints_acc && weights_acc && skin_index != std::numeric_limits<uint32_t>::max();
    bool first = true;
    std::vector<cgltf_uint> joint_values(4);
    std::vector<float> weight_values(4);
    for (cgltf_size i = 0; i < pos_acc->count; ++i) {
        glm::vec3 world_pos{0.0f};
        if (out.skinned && skin_index < model.skins.size()) {
            const auto& skin = model.skins[skin_index];
            cgltf_accessor_read_uint(joints_acc, i, joint_values.data(), 4);
            cgltf_accessor_read_float(weights_acc, i, weight_values.data(), 4);
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
                if (joint < 0 || static_cast<size_t>(joint) >= model.nodes.size()) continue;
                glm::vec4 transformed = model.nodes[static_cast<size_t>(joint)].world_transform
                    * skin.inverse_bind_matrices[slot] * glm::vec4(vertices[i].position, 1.0f);
                world_pos += glm::vec3(transformed) * weight;
            }
        } else {
            world_pos = glm::vec3(world * glm::vec4(vertices[i].position, 1.0f));
        }
        expand_bounds(out.bounds, world_pos, first);
    }

    std::vector<uint16_t> indices16;
    const void* index_data = nullptr;
    size_t index_bytes = 0;
    if (vertices.size() <= std::numeric_limits<uint16_t>::max()) {
        indices16.resize(indices.size());
        for (size_t i = 0; i < indices.size(); ++i)
            indices16[i] = static_cast<uint16_t>(indices[i]);
        out.index_stride = 2;
        index_data = indices16.data();
        index_bytes = indices16.size() * sizeof(uint16_t);
    } else {
        out.index_stride = 4;
        index_data = indices.data();
        index_bytes = indices.size() * sizeof(uint32_t);
    }

    bool buffer_ok = false;
    if (out.skinned) {
        std::vector<nw::render::SkinnedVertex> skinned_vertices(vertices.size());
        for (cgltf_size i = 0; i < pos_acc->count; ++i) {
            skinned_vertices[i].position = vertices[i].position;
            skinned_vertices[i].normal = vertices[i].normal;
            skinned_vertices[i].texcoord = vertices[i].texcoord;
            skinned_vertices[i].tangent = vertices[i].tangent;
            cgltf_accessor_read_uint(joints_acc, i, joint_values.data(), 4);
            cgltf_accessor_read_float(weights_acc, i, weight_values.data(), 4);
            auto packed_weights = pack_weights({weight_values[0], weight_values[1], weight_values[2], weight_values[3]});
            skinned_vertices[i].joint_indices = pack_u8x4(
                std::min<uint32_t>(joint_values[0], 255),
                std::min<uint32_t>(joint_values[1], 255),
                std::min<uint32_t>(joint_values[2], 255),
                std::min<uint32_t>(joint_values[3], 255));
            skinned_vertices[i].joint_weights = pack_u8x4(
                packed_weights[0], packed_weights[1], packed_weights[2], packed_weights[3]);
        }

        buffer_ok = create_buffer_from_bytes(desc.ctx, skinned_vertices.data(), skinned_vertices.size() * sizeof(nw::render::SkinnedVertex),
                        nw::gfx::BufferUsage::Vertex, out.vertices)
            && create_buffer_from_bytes(desc.ctx, index_data, index_bytes, nw::gfx::BufferUsage::Index, out.indices);
    } else {
        buffer_ok = create_buffer_from_bytes(desc.ctx, vertices.data(), vertices.size() * sizeof(nw::render::Vertex), nw::gfx::BufferUsage::Vertex, out.vertices)
            && create_buffer_from_bytes(desc.ctx, index_data, index_bytes, nw::gfx::BufferUsage::Index, out.indices);
    }

    if (!buffer_ok) {
        return;
    }

    out.index_count = static_cast<uint32_t>(indices.size());
    out.vertex_count = static_cast<uint32_t>(vertices.size());
    out.material = material_index;
    model.primitives.push_back(out);
}

void walk_node(const cgltf_data* data, const cgltf_node* node, const glm::mat4& parent,
    const ImportGltfDesc& desc, nw::render::RenderModel& model)
{
    glm::mat4 world = parent * load_matrix(node);
    if (node->mesh) {
        for (cgltf_size i = 0; i < node->mesh->primitives_count; ++i) {
            uint32_t material_index = 0;
            if (node->mesh->primitives[i].material && !model.materials.empty()) {
                material_index = static_cast<uint32_t>(node->mesh->primitives[i].material - data->materials);
            }
            const int32_t node_index = static_cast<int32_t>(node - data->nodes);
            uint32_t skin_index = std::numeric_limits<uint32_t>::max();
            if (node->skin) {
                skin_index = static_cast<uint32_t>(node->skin - data->skins);
            }
            import_primitive(node->mesh->primitives[i], material_index, skin_index, node_index, world, desc, model);
        }
    }
    for (cgltf_size i = 0; i < node->children_count; ++i) {
        walk_node(data, node->children[i], world, desc, model);
    }
}

} // namespace

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
            model->textures.push_back(handle);
            tex_map[key] = nw::gfx::get_bindless_texture_index(desc.ctx, handle);
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
                model->textures.push_back(handle);
                tex_map[TextureKey{&texture, nw::gfx::Fmt::RGBA8Srgb}] = nw::gfx::get_bindless_texture_index(desc.ctx, handle);
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
            auto surface_index = create_surface_texture(&data->materials[i], base_dir, *model, desc);
            model->materials.push_back(import_material(&data->materials[i], tex_map, desc, surface_index));
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
    bool joint_first_bound = model->primitives.empty();
    for (const auto& skin : model->skins) {
        for (const auto joint : skin.joints) {
            if (joint < 0 || static_cast<size_t>(joint) >= model->nodes.size()) {
                continue;
            }
            glm::vec3 world_pos = glm::vec3(model->nodes[static_cast<size_t>(joint)].world_transform[3]);
            expand_bounds(model->bounds, world_pos, joint_first_bound);
        }
    }

    for (cgltf_size i = 0; i < data->scenes_count; ++i) {
        const cgltf_scene* scene = &data->scenes[i];
        if (data->scene && scene != data->scene) continue;
        for (cgltf_size j = 0; j < scene->nodes_count; ++j) {
            walk_node(data, scene->nodes[j], kGltfToMudl, desc, *model);
        }
    }

    // Merge primitive bounds into any existing joint-expanded bounds.
    bool first_bound = model->skins.empty();
    for (const auto& prim : model->primitives) {
        if (first_bound) {
            model->bounds = prim.bounds;
            first_bound = false;
        } else {
            model->bounds.min = glm::min(model->bounds.min, prim.bounds.min);
            model->bounds.max = glm::max(model->bounds.max, prim.bounds.max);
        }
    }

    cleanup();
    if (model->primitives.empty()) {
        return {};
    }
    return model;
}

} // namespace nw::render::gltf
