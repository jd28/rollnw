#include "model_renderer.hpp"

#include "model_gpu_backend.hpp"
#include "model_loader.hpp"
#include "render_asset_cache.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace nw::render::nwn {

constexpr size_t kModelRendererMaxBones = 64;

struct DrawItem {
    Mesh* mesh = nullptr;
    glm::mat4 world{1.0f};
    glm::mat4 model_root{1.0f};
    glm::vec3 sort_position{0.0f};
    size_t order = 0;
};

static void collect_draws_recursive(std::vector<DrawItem>& draws, Node* node,
    const glm::mat4& parent_transform, const glm::mat4& model_root)
{
    glm::mat4 local = glm::mat4(1.f);
    if (node->has_transform_) {
        local = glm::translate(glm::mat4(1.f), node->position_);
        local = local * glm::mat4_cast(node->rotation_);
        local = glm::scale(local, glm::vec3(node->scale_));
    }

    glm::mat4 world = parent_transform * local;

    if (node->is_mesh) {
        auto* mesh = static_cast<Mesh*>(node);
        if (mesh->vertices.valid() && mesh->index_count > 0) {
            draws.push_back(DrawItem{
                .mesh = mesh,
                .world = world,
                .model_root = model_root,
                .sort_position = glm::vec3(world[3]),
                .order = draws.size(),
            });
        }
    }

    for (auto& child : node->children_) {
        collect_draws_recursive(draws, child, world, model_root);
    }
}

static void collect_model_draws(std::vector<DrawItem>& draws, const ModelInstance& model,
    const glm::mat4& model_root)
{
    for (const auto& node : model.nodes_) {
        if (!node->parent_) {
            collect_draws_recursive(draws, node.get(), model_root, model_root);
            break;
        }
    }
}

static void draw_mesh_item(ModelRenderContext& render_ctx, nw::gfx::CommandList* cmd,
    const DrawItem& item, const nw::render::SceneConstants& base_sc,
    nw::gfx::BindlessTextureIndex fallback_texture_index, bool offscreen_pass,
    const nw::gfx::UniformSpan& shadow_uniforms)
{
    auto* mesh = item.mesh;
    if (!mesh) {
        return;
    }

    if (!mesh->texture.valid() && !mesh->bitmap_name.empty()) {
        if (mesh->uses_plt) {
            mesh->texture = render_ctx.assets->get_or_load_raw_plt_texture(mesh->bitmap_name);
        } else {
            const bool premultiply_alpha = mesh->material_mode == MaterialMode::transparent;
            mesh->texture = render_ctx.assets->get_or_load_texture(
                mesh->bitmap_name, premultiply_alpha, render_ctx.gpu->fallback_texture());
        }
        mesh->texture_index = nw::gfx::get_bindless_texture_index(render_ctx.gfx, mesh->texture);
    }

    auto pipeline = offscreen_pass && render_ctx.gpu->static_offscreen_pipeline().valid()
        ? render_ctx.gpu->static_offscreen_pipeline()
        : render_ctx.gpu->static_pipeline();
    auto vertex_stride = sizeof(Vertex);
    const bool is_transparent = mesh->material_mode == MaterialMode::transparent;
    const auto populate_common = [&](auto& sc) {
        const glm::mat4 model_matrix = mesh->is_skin ? item.model_root : item.world;
        sc.model = model_matrix;
        glm::mat3 nm = glm::mat3(glm::transpose(glm::inverse(model_matrix)));
        sc.normal_matrix = glm::mat4(nm);
        sc.texture_index = mesh->texture_index != 0 ? mesh->texture_index : fallback_texture_index;
        sc.alpha_cutout = mesh->material_mode == MaterialMode::cutout ? 1 : 0;
        sc.alpha_cutout_threshold = mesh->alpha_cutout_threshold;
        sc.pad_alpha.x = mesh->opacity;
        sc.color_key_rg = glm::vec2(mesh->color_key.x, mesh->color_key.y);
        sc.color_key_b = mesh->color_key.z;
        sc.color_key_threshold = mesh->color_key_threshold;
        sc.plt_enabled = mesh->uses_plt ? 1u : 0u;
        sc.plt_colors0 = glm::uvec4(mesh->plt_colors.data[0], mesh->plt_colors.data[1], mesh->plt_colors.data[2], mesh->plt_colors.data[3]);
        sc.plt_colors1 = glm::uvec4(mesh->plt_colors.data[4], mesh->plt_colors.data[5], mesh->plt_colors.data[6], mesh->plt_colors.data[7]);
        sc.plt_colors2 = glm::uvec4(mesh->plt_colors.data[8], mesh->plt_colors.data[9], 0u, 0u);
    };

    if (mesh->is_skin && render_ctx.gpu->skinned_pipeline().valid()) {
        nw::render::SceneConstants sc = base_sc;
        populate_common(sc);
        if (is_transparent) {
            if (offscreen_pass && render_ctx.gpu->skinned_offscreen_transparent_pipeline().valid()) {
                pipeline = render_ctx.gpu->skinned_offscreen_transparent_pipeline();
            } else {
                pipeline = render_ctx.gpu->skinned_transparent_pipeline().valid()
                    ? render_ctx.gpu->skinned_transparent_pipeline()
                    : render_ctx.gpu->skinned_pipeline();
            }
        } else if (offscreen_pass && render_ctx.gpu->skinned_offscreen_pipeline().valid()) {
            pipeline = render_ctx.gpu->skinned_offscreen_pipeline();
        } else {
            pipeline = render_ctx.gpu->skinned_pipeline();
        }
        vertex_stride = sizeof(SkinnedVertex);

        auto* skin = static_cast<SkinMesh*>(mesh);
        std::array<glm::mat4, kModelRendererMaxBones> bones{};
        for (auto& bone : bones) {
            bone = glm::mat4(1.0f);
        }
        skin->fill_skin_matrices(bones.data(), bones.size());

        auto bone_span = render_ctx.gpu->upload_bones(cmd, bones.data(), static_cast<uint32_t>(bones.size()));
        auto uniforms = nw::gfx::allocate_uniform_span(render_ctx.gfx, sizeof(nw::render::SceneConstants));
        if (!uniforms.data || !bone_span.buffer.valid()) {
            return;
        }
        std::memcpy(uniforms.data, &sc, sizeof(sc));
        nw::gfx::cmd_bind_pipeline(cmd, pipeline);
        nw::gfx::cmd_bind_vertex_buffer(cmd, mesh->vertices, static_cast<uint32_t>(vertex_stride));
        nw::gfx::cmd_bind_index_buffer(cmd, mesh->indices, sizeof(uint16_t));
        nw::gfx::cmd_bind_resources(cmd, pipeline, uniforms,
            nw::gfx::StorageSpan{render_ctx.gpu->plt_palette_buffer()}, bone_span, shadow_uniforms);
        nw::gfx::cmd_draw_indexed(cmd, mesh->index_count);
        return;
    } else if (is_transparent) {
        if (offscreen_pass && render_ctx.gpu->static_offscreen_transparent_pipeline().valid()) {
            pipeline = render_ctx.gpu->static_offscreen_transparent_pipeline();
        } else if (render_ctx.gpu->static_transparent_pipeline().valid()) {
            pipeline = render_ctx.gpu->static_transparent_pipeline();
        }
    }

    nw::render::SceneConstants sc = base_sc;
    populate_common(sc);

    auto uniforms = nw::gfx::allocate_uniform_span(render_ctx.gfx, sizeof(nw::render::SceneConstants));
    if (!uniforms.data) {
        return;
    }

    std::memcpy(uniforms.data, &sc, sizeof(sc));
    nw::gfx::cmd_bind_pipeline(cmd, pipeline);
    nw::gfx::cmd_bind_vertex_buffer(cmd, mesh->vertices, static_cast<uint32_t>(vertex_stride));
    nw::gfx::cmd_bind_index_buffer(cmd, mesh->indices, sizeof(uint16_t));
    nw::gfx::cmd_bind_resources(cmd, pipeline, uniforms,
        nw::gfx::StorageSpan{render_ctx.gpu->plt_palette_buffer()}, {}, shadow_uniforms);
    nw::gfx::cmd_draw_indexed(cmd, mesh->index_count);
}

static void draw_shadow_mesh_item(ModelRenderContext& render_ctx, nw::gfx::CommandList* cmd,
    const DrawItem& item, const nw::render::SceneConstants& base_sc,
    nw::gfx::BindlessTextureIndex fallback_texture_index)
{
    auto* mesh = item.mesh;
    if (!mesh || mesh->material_mode == MaterialMode::transparent) {
        return;
    }

    if (!mesh->texture.valid() && !mesh->bitmap_name.empty()) {
        if (mesh->uses_plt) {
            mesh->texture = render_ctx.assets->get_or_load_raw_plt_texture(mesh->bitmap_name);
        } else {
            mesh->texture = render_ctx.assets->get_or_load_texture(
                mesh->bitmap_name, false, render_ctx.gpu->fallback_texture());
        }
        mesh->texture_index = nw::gfx::get_bindless_texture_index(render_ctx.gfx, mesh->texture);
    }

    const bool is_cutout = mesh->material_mode == MaterialMode::cutout;
    auto pipeline = is_cutout && render_ctx.gpu->static_shadow_cutout_pipeline().valid()
        ? render_ctx.gpu->static_shadow_cutout_pipeline()
        : render_ctx.gpu->static_shadow_pipeline();
    auto vertex_stride = sizeof(Vertex);

    const auto populate_common = [&](auto& sc) {
        const glm::mat4 model_matrix = mesh->is_skin ? item.model_root : item.world;
        sc.model = model_matrix;
        glm::mat3 nm = glm::mat3(glm::transpose(glm::inverse(model_matrix)));
        sc.normal_matrix = glm::mat4(nm);
        sc.texture_index = mesh->texture_index != 0 ? mesh->texture_index : fallback_texture_index;
        sc.alpha_cutout = mesh->material_mode == MaterialMode::cutout ? 1 : 0;
        sc.alpha_cutout_threshold = mesh->alpha_cutout_threshold;
        sc.pad_alpha.x = mesh->opacity;
    };

    if (mesh->is_skin && render_ctx.gpu->skinned_shadow_pipeline().valid()) {
        nw::render::SceneConstants sc = base_sc;
        populate_common(sc);
        pipeline = is_cutout && render_ctx.gpu->skinned_shadow_cutout_pipeline().valid()
            ? render_ctx.gpu->skinned_shadow_cutout_pipeline()
            : render_ctx.gpu->skinned_shadow_pipeline();
        vertex_stride = sizeof(SkinnedVertex);

        auto* skin = static_cast<SkinMesh*>(mesh);
        std::array<glm::mat4, kModelRendererMaxBones> bones{};
        for (auto& bone : bones) {
            bone = glm::mat4(1.0f);
        }
        skin->fill_skin_matrices(bones.data(), bones.size());

        auto bone_span = render_ctx.gpu->upload_bones(cmd, bones.data(), static_cast<uint32_t>(bones.size()));
        auto uniforms = nw::gfx::allocate_uniform_span(render_ctx.gfx, sizeof(nw::render::SceneConstants));
        if (!uniforms.data || !bone_span.buffer.valid()) {
            return;
        }
        std::memcpy(uniforms.data, &sc, sizeof(sc));
        nw::gfx::cmd_bind_pipeline(cmd, pipeline);
        nw::gfx::cmd_bind_vertex_buffer(cmd, mesh->vertices, static_cast<uint32_t>(vertex_stride));
        nw::gfx::cmd_bind_index_buffer(cmd, mesh->indices, sizeof(uint16_t));
        nw::gfx::cmd_bind_resources(cmd, pipeline, uniforms,
            nw::gfx::StorageSpan{render_ctx.gpu->plt_palette_buffer()}, bone_span);
        nw::gfx::cmd_draw_indexed(cmd, mesh->index_count);
        return;
    }

    nw::render::SceneConstants sc = base_sc;
    populate_common(sc);
    auto uniforms = nw::gfx::allocate_uniform_span(render_ctx.gfx, sizeof(nw::render::SceneConstants));
    if (!uniforms.data) {
        return;
    }
    std::memcpy(uniforms.data, &sc, sizeof(sc));
    nw::gfx::cmd_bind_pipeline(cmd, pipeline);
    nw::gfx::cmd_bind_vertex_buffer(cmd, mesh->vertices, static_cast<uint32_t>(vertex_stride));
    nw::gfx::cmd_bind_index_buffer(cmd, mesh->indices, sizeof(uint16_t));
    nw::gfx::cmd_bind_resources(cmd, pipeline, uniforms);
    nw::gfx::cmd_draw_indexed(cmd, mesh->index_count);
}

static void draw_material_pass(ModelRenderContext& render_ctx, nw::gfx::CommandList* cmd,
    const std::vector<DrawItem>& draws, MaterialMode mode, const nw::render::SceneConstants& base_sc,
    nw::gfx::BindlessTextureIndex fallback_texture_index, const glm::vec3& camera_position,
    bool offscreen_pass, const nw::gfx::UniformSpan& shadow_uniforms)
{
    if (mode == MaterialMode::transparent) {
        std::vector<const DrawItem*> sorted;
        sorted.reserve(draws.size());
        for (const auto& draw : draws) {
            if (draw.mesh && draw.mesh->material_mode == mode) {
                sorted.push_back(&draw);
            }
        }
        std::sort(sorted.begin(), sorted.end(), [&](const DrawItem* lhs, const DrawItem* rhs) {
            const int lhs_hint = lhs->mesh ? lhs->mesh->transparencyhint : 0;
            const int rhs_hint = rhs->mesh ? rhs->mesh->transparencyhint : 0;
            if (lhs_hint != rhs_hint) {
                return lhs_hint < rhs_hint;
            }
            const bool lhs_ground = lhs->mesh && lhs->mesh->is_ground_overlay;
            const bool rhs_ground = rhs->mesh && rhs->mesh->is_ground_overlay;
            if (lhs_ground != rhs_ground) {
                return lhs_ground;
            }
            if (lhs_ground && rhs_ground) {
                return lhs->order < rhs->order;
            }
            const auto lhs_delta = lhs->sort_position - camera_position;
            const auto rhs_delta = rhs->sort_position - camera_position;
            const float lhs_dist2 = glm::dot(lhs_delta, lhs_delta);
            const float rhs_dist2 = glm::dot(rhs_delta, rhs_delta);
            if (lhs_dist2 != rhs_dist2) {
                return lhs_dist2 > rhs_dist2;
            }
            return lhs->order < rhs->order;
        });
        for (const auto* draw : sorted) {
            draw_mesh_item(render_ctx, cmd, *draw, base_sc, fallback_texture_index, offscreen_pass, shadow_uniforms);
        }
        return;
    }

    for (const auto& draw : draws) {
        if (draw.mesh && draw.mesh->material_mode == mode) {
            draw_mesh_item(render_ctx, cmd, draw, base_sc, fallback_texture_index, offscreen_pass, shadow_uniforms);
        }
    }
}

void render_model_instance(ModelRenderContext& render_ctx, nw::gfx::CommandList* cmd, const ModelInstance& model,
    const nw::render::RenderContext& ctx, nw::render::RenderPassSelection pass)
{
    render_model_instance_with_root(render_ctx, cmd, model, model.root_transform(), ctx, pass);
}

void render_model_instance_with_root(ModelRenderContext& render_ctx, nw::gfx::CommandList* cmd,
    const ModelInstance& model, const glm::mat4& model_root, const nw::render::RenderContext& ctx,
    nw::render::RenderPassSelection pass)
{
    if (!render_ctx.gfx || !render_ctx.gpu || !render_ctx.assets || !cmd
        || !model.render_enabled || model.nodes_.empty()) {
        return;
    }

    nw::render::SceneConstants base_sc{};
    base_sc.view = ctx.view;
    base_sc.projection = ctx.projection;
    base_sc.texture_index = 0;
    base_sc.camera_pos = glm::vec4(ctx.camera_position, 0.0f);

    auto camera_forward = glm::normalize(ctx.camera_target - ctx.camera_position);
    auto camera_plane_right = glm::cross(camera_forward, glm::vec3{0.0f, 0.0f, 1.0f});
    if (glm::length(camera_plane_right) < 0.0001f) {
        camera_plane_right = glm::vec3{1.0f, 0.0f, 0.0f};
    } else {
        camera_plane_right = glm::normalize(camera_plane_right);
    }
    auto up = glm::normalize(glm::cross(camera_plane_right, camera_forward));

    auto camera_relative_light = [&](const glm::vec3& dir) {
        auto world_l = glm::normalize(camera_plane_right * dir.x + (-camera_forward) * dir.y + up * dir.z);
        return -world_l;
    };

    auto world_space_light = [](const glm::vec3& dir) {
        if (glm::dot(dir, dir) < 1.0e-8f) {
            return glm::vec3{0.0f, 0.0f, 1.0f};
        }
        return -glm::normalize(dir);
    };

    const auto encode_light = [&](const glm::vec3& dir) {
        return ctx.lighting_space == nw::render::LightingSpace::world_space
            ? world_space_light(dir)
            : camera_relative_light(dir);
    };

    base_sc.ambient = glm::vec4(ctx.lighting.ambient, 0.0f);
    base_sc.key_dir_intensity = glm::vec4(encode_light(ctx.lighting.key_direction), ctx.lighting.key_intensity);
    base_sc.key_color = glm::vec4(ctx.lighting.key_color, 0.0f);
    base_sc.fill_dir_intensity = glm::vec4(encode_light(ctx.lighting.fill_direction), ctx.lighting.fill_intensity);
    base_sc.fill_color = glm::vec4(ctx.lighting.fill_color, 0.0f);
    base_sc.rim_dir_intensity = glm::vec4(encode_light(ctx.lighting.rim_direction), ctx.lighting.rim_intensity);
    base_sc.rim_color = glm::vec4(ctx.lighting.rim_color, 0.0f);
    base_sc.fog_color = glm::vec4(ctx.fog.color, 0.0f);
    base_sc.fog_range = glm::vec2(ctx.fog.start_distance, ctx.fog.end_distance);
    base_sc.fog_amount = ctx.fog.amount;
    base_sc.fog_enabled = ctx.fog.enabled ? 1u : 0u;

    auto shadow_uniforms = nw::gfx::allocate_uniform_span(render_ctx.gfx, sizeof(nw::render::ShadowConstants));
    if (shadow_uniforms.data) {
        nw::render::ShadowConstants shadow_constants{};
        if (ctx.shadow.enabled) {
            shadow_constants.world_to_shadow = ctx.shadow.world_to_shadow;
            shadow_constants.shadow_split_distances = glm::vec4(
                ctx.shadow.split_distances[0],
                ctx.shadow.split_distances[1],
                ctx.shadow.split_distances[2],
                0.0f);
            for (size_t i = 0; i < nw::render::kShadowCascadeCount; ++i) {
                shadow_constants.shadow_texture_indices[static_cast<glm::length_t>(i)] =
                    nw::gfx::get_bindless_texture_index(render_ctx.gfx, ctx.shadow.depth_textures[i]);
            }
            shadow_constants.shadow_strength = ctx.shadow.strength;
            shadow_constants.debug_mode = ctx.shadow.debug_mode;
        }
        std::memcpy(shadow_uniforms.data, &shadow_constants, sizeof(shadow_constants));
    }

    const auto fallback_texture_index = nw::gfx::get_bindless_texture_index(
        render_ctx.gfx, render_ctx.gpu->fallback_texture());
    std::vector<DrawItem> draws;
    collect_model_draws(draws, model, model_root);

    if (pass == nw::render::RenderPassSelection::opaque_cutout || pass == nw::render::RenderPassSelection::all) {
        draw_material_pass(render_ctx, cmd, draws, MaterialMode::opaque, base_sc, fallback_texture_index,
            ctx.camera_position, ctx.offscreen_pass, shadow_uniforms);
        draw_material_pass(render_ctx, cmd, draws, MaterialMode::cutout, base_sc, fallback_texture_index,
            ctx.camera_position, ctx.offscreen_pass, shadow_uniforms);
    }
    if (pass == nw::render::RenderPassSelection::transparent || pass == nw::render::RenderPassSelection::all) {
        draw_material_pass(render_ctx, cmd, draws, MaterialMode::transparent, base_sc, fallback_texture_index,
            ctx.camera_position, ctx.offscreen_pass, shadow_uniforms);
    }
}

void render_shadow_model_instance(ModelRenderContext& render_ctx, nw::gfx::CommandList* cmd,
    const ModelInstance& model, const glm::mat4& light_view, const glm::mat4& light_projection)
{
    if (!render_ctx.gfx || !render_ctx.gpu || !render_ctx.gpu->static_shadow_pipeline().valid() || !cmd
        || !model.render_enabled
        || model.nodes_.empty()) {
        return;
    }

    nw::render::SceneConstants base_sc{};
    base_sc.view = light_view;
    base_sc.projection = light_projection;

    const auto fallback_texture_index = nw::gfx::get_bindless_texture_index(
        render_ctx.gfx, render_ctx.gpu->fallback_texture());
    const auto model_root = model.root_transform();
    std::vector<DrawItem> draws;
    collect_model_draws(draws, model, model_root);

    for (const auto& draw : draws) {
        if (draw.mesh && draw.mesh->material_mode != MaterialMode::transparent) {
            draw_shadow_mesh_item(render_ctx, cmd, draw, base_sc, fallback_texture_index);
        }
    }
}

} // namespace nw::render::nwn
