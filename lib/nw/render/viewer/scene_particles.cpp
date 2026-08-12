#include "scene_particles.hpp"

#include "area_render_scene.hpp"
#include "preview_scene.hpp"

#include <nw/render/model_asset.hpp>
#include <nw/render/model_gpu_backend.hpp>
#include <nw/render/model_renderer.hpp>
#include <nw/render/nwn/render_asset_cache.hpp>
#include <nw/render/particle_renderer.hpp>
#include <nw/render/particle_system.hpp>
#include <nw/render/render_service.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>
#include <vector>

namespace nw::render::viewer {

namespace {

void add_saturating(uint32_t& target, uint32_t value) noexcept
{
    if (std::numeric_limits<uint32_t>::max() - target < value) {
        target = std::numeric_limits<uint32_t>::max();
        return;
    }
    target += value;
}

class SceneParticleResourceProvider : public nw::render::ParticleResourceProvider {
public:
    explicit SceneParticleResourceProvider(nw::render::RenderService& service) noexcept
        : service_(service)
        , particle_mesh_texture_upload_{
              .ctx = service.model_render_context().gfx,
              .fallback_albedo = service.model_backend().fallback_albedo_index(),
              .fallback_normal = service.model_backend().fallback_normal_index(),
              .fallback_surface = service.model_backend().fallback_surface_index(),
              .fallback_emissive = service.model_backend().fallback_emissive_index(),
          }
    {
    }

    nw::gfx::Handle<nw::gfx::Texture> get_texture(std::string_view name, bool premultiply_alpha) override
    {
        return service_.asset_cache().get_or_load_texture(
            nw::Resref{name}, premultiply_alpha, service_.model_backend().fallback_texture());
    }

    const nw::Image* get_source_image(std::string_view name) override
    {
        return service_.asset_cache().get_or_load_source_image(nw::Resref{name});
    }

    bool source_image_is_white_alpha_mask(std::string_view name) override
    {
        return service_.asset_cache().source_image_is_white_alpha_mask(nw::Resref{name});
    }

    bool texture_rows_flipped(std::string_view name, bool premultiply_alpha) override
    {
        return service_.asset_cache().texture_rows_flipped_on_upload(nw::Resref{name}, premultiply_alpha);
    }

    nw::gfx::Handle<nw::gfx::Texture> fallback_texture() const override
    {
        return service_.model_backend().fallback_texture();
    }

    nw::render::RenderModel* get_particle_mesh(std::string_view resref)
    {
        return service_.asset_cache().get_or_load_particle_mesh(
            nw::Resref{resref}, particle_mesh_texture_upload_);
    }

private:
    nw::render::RenderService& service_;
    nw::render::ModelAssetTextureUploadDesc particle_mesh_texture_upload_{};
};

bool particle_mesh_columns_contain(
    const nw::render::ParticleCoreStorage& core, size_t particle_index) noexcept
{
    return particle_index < core.position.size()
        && particle_index < core.size_x.size()
        && particle_index < core.size_y.size()
        && particle_index < core.rotation.size();
}

bool particle_mesh_data_finite(
    const nw::render::ParticleCoreStorage& core, size_t particle_index) noexcept
{
    const auto& position = core.position[particle_index];
    return std::isfinite(position.x)
        && std::isfinite(position.y)
        && std::isfinite(position.z)
        && std::isfinite(core.size_x[particle_index])
        && std::isfinite(core.size_y[particle_index])
        && std::isfinite(core.rotation[particle_index]);
}

glm::mat4 particle_mesh_root(
    const nw::render::ParticleCoreStorage& core,
    size_t particle_index)
{
    const float sx = std::max(core.size_x[particle_index], 0.001f);
    const float sy = std::max(core.size_y[particle_index], 0.001f);
    const float uniform_scale = std::max(0.5f * (sx + sy), 0.001f);
    const float angle = core.rotation[particle_index] * 2.0f * 3.14159265358979323846f;

    glm::mat4 model_root = glm::translate(glm::mat4(1.0f), core.position[particle_index]);
    model_root = model_root * glm::mat4_cast(glm::angleAxis(angle, glm::vec3{0.0f, 0.0f, 1.0f}));
    model_root = model_root * glm::scale(glm::mat4(1.0f), glm::vec3(uniform_scale));
    return model_root;
}

struct SceneMeshParticleBatch {
    nw::render::ModelRenderContext render_ctx;
    SceneParticleResourceProvider& resources;
    nw::gfx::CommandList* cmd = nullptr;
    const nw::render::RenderContext& ctx;
    std::vector<glm::mat4> roots;
};

// One packet is a batch of indices into the particle SoA. The cached mesh is an
// immutable RenderModel; only the per-particle root transform changes here.
void render_scene_mesh_particles(
    SceneMeshParticleBatch& batch,
    const SceneParticleSystem& scene_particles,
    const nw::render::ParticleRenderPacket& packet,
    const nw::render::ParticleMaterialDef& source_material,
    ParticleRenderStats& stats)
{
    add_saturating(stats.mesh_packet_count, 1u);
    add_saturating(stats.mesh_particle_count, packet.count);
    if (source_material.mesh.empty()) {
        add_saturating(stats.mesh_missing_resref_packet_count, 1u);
        add_saturating(stats.mesh_dropped_particle_count, packet.count);
        return;
    }

    auto* chunk_model = batch.resources.get_particle_mesh(source_material.mesh);
    if (!chunk_model || chunk_model->primitives.empty()) {
        add_saturating(stats.mesh_missing_model_packet_count, 1u);
        add_saturating(stats.mesh_dropped_particle_count, packet.count);
        return;
    }

    const auto& core = scene_particles.system.particles.core;
    const auto particle_indices = particle_render_packet_indices(scene_particles.system, packet);
    if (particle_indices.size() < packet.count) {
        const auto missing_count = packet.count - static_cast<uint32_t>(particle_indices.size());
        add_saturating(stats.mesh_invalid_particle_index_count, missing_count);
        add_saturating(stats.mesh_dropped_particle_count, missing_count);
    }
    batch.roots.clear();
    batch.roots.reserve(particle_indices.size());
    for (const uint32_t particle_index : particle_indices) {
        if (!particle_mesh_columns_contain(core, particle_index)) {
            add_saturating(stats.mesh_invalid_particle_index_count, 1u);
            add_saturating(stats.mesh_dropped_particle_count, 1u);
            continue;
        }
        if (!particle_mesh_data_finite(core, particle_index)) {
            add_saturating(stats.mesh_invalid_particle_data_count, 1u);
            add_saturating(stats.mesh_dropped_particle_count, 1u);
            continue;
        }
        batch.roots.push_back(particle_mesh_root(core, particle_index));
    }
    if (!batch.roots.empty()) {
        nw::render::render_render_model_instances(
            batch.render_ctx,
            batch.cmd,
            *chunk_model,
            batch.roots,
            batch.ctx);
        add_saturating(stats.mesh_submitted_particle_count, static_cast<uint32_t>(batch.roots.size()));
    }
}

} // namespace

bool particle_system_visible_for_render_filter(
    const SceneParticleSystem& scene_particles,
    const ParticleRenderFilter& filter) noexcept
{
    if (!filter.cull_by_owner_area_record || !filter.area_scene || !filter.area_frame) {
        return true;
    }

    const uint32_t record_index = filter.area_scene->record_index_for_render_model(
        scene_particles.owner_model_index);
    if (record_index == kInvalidAreaRenderRecordIndex) {
        return true;
    }
    return filter.area_frame->record_visible(record_index);
}

ParticleRenderStats render_scene_particles(
    nw::render::RenderService& service,
    nw::gfx::CommandList* cmd,
    const PreviewScene& scene,
    const nw::render::RenderContext& ctx,
    ParticleRenderFilter filter)
{
    if (!cmd) {
        return {};
    }

    SceneParticleResourceProvider resources{service};
    SceneMeshParticleBatch mesh_batch{service.model_render_context(), resources, cmd, ctx};
    ParticleRenderStats stats{};
    for (const auto& scene_particles : scene.particles) {
        if (!particle_system_visible_for_render_filter(scene_particles, filter)) {
            ++stats.culled_system_count;
            continue;
        }

        const auto* effect = scene_particles.system.effect;
        if (!effect) {
            continue;
        }
        ++stats.submitted_system_count;

        const auto packets = scene_particles.system.render_packets.span();
        for (const auto& packet : packets) {
            if (packet.material >= effect->materials.size()) {
                add_saturating(stats.invalid_material_packet_count, 1u);
                if (packet.mode == nw::render::ParticleRenderMode::mesh) {
                    add_saturating(stats.mesh_packet_count, 1u);
                    add_saturating(stats.mesh_particle_count, packet.count);
                    add_saturating(stats.mesh_dropped_particle_count, packet.count);
                }
                continue;
            }

            const auto& source_material = packet.material < scene_particles.import.effect.materials.size()
                ? scene_particles.import.effect.materials[packet.material]
                : nw::render::ParticleMaterialDef{};
            if (packet.mode == nw::render::ParticleRenderMode::mesh) {
                render_scene_mesh_particles(
                    mesh_batch,
                    scene_particles,
                    packet,
                    source_material,
                    stats);
            }
        }

        service.particle_renderer().render(
            cmd,
            scene_particles.system,
            scene_particles.import.effect.materials,
            ctx,
            resources);
    }
    return stats;
}

} // namespace nw::render::viewer
