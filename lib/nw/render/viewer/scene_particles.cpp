#include "scene_particles.hpp"

#include "area_render_scene.hpp"
#include "preview_scene.hpp"

#include <nw/render/model_gpu_backend.hpp>
#include <nw/render/nwn/model_renderer.hpp>
#include <nw/render/nwn/render_asset_cache.hpp>
#include <nw/render/particle_renderer.hpp>
#include <nw/render/particle_system.hpp>
#include <nw/render/render_service.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <limits>
#include <optional>
#include <string>

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
    {
    }

    nw::gfx::Handle<nw::gfx::Texture> get_texture(std::string_view name, bool premultiply_alpha) override
    {
        return service_.asset_cache().get_or_load_texture(
            std::string{name}, premultiply_alpha, service_.model_backend().fallback_texture());
    }

    const nw::Image* get_source_image(std::string_view name) override
    {
        return service_.asset_cache().get_or_load_source_image(std::string{name});
    }

    bool source_image_is_white_alpha_mask(std::string_view name) override
    {
        return service_.asset_cache().source_image_is_white_alpha_mask(std::string{name});
    }

    bool texture_rows_flipped(std::string_view name, bool premultiply_alpha) override
    {
        return service_.asset_cache().texture_rows_flipped_on_upload(std::string{name}, premultiply_alpha);
    }

    nw::gfx::Handle<nw::gfx::Texture> fallback_texture() const override
    {
        return service_.model_backend().fallback_texture();
    }

    nw::render::nwn::ModelInstance* get_particle_mesh(std::string_view resref)
    {
        return service_.asset_cache().get_or_load_particle_mesh(std::string{resref});
    }

private:
    nw::render::RenderService& service_;
};

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

struct SceneMeshParticleBridge {
    nw::render::RenderService& service;
    SceneParticleResourceProvider& resources;
    nw::gfx::CommandList* cmd = nullptr;
    const nw::render::RenderContext& ctx;
    std::optional<nw::render::nwn::ModelRenderContext> render_ctx{};

    nw::render::nwn::ModelRenderContext& legacy_context()
    {
        if (!render_ctx) {
            render_ctx = service.nwn_model_render_context();
        }
        return *render_ctx;
    }
};

// Bridge-phase transform for mesh particles. The common particle packet owns
// the particle indices and per-particle placement; the NWN sidecar still owns
// the mesh payload until mesh particles are compiled to RenderModel assets.
void render_scene_mesh_particle_packet(
    SceneMeshParticleBridge& bridge,
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

    auto* chunk_model = bridge.resources.get_particle_mesh(source_material.mesh);
    if (!chunk_model || chunk_model->nodes_.empty()) {
        add_saturating(stats.mesh_missing_model_packet_count, 1u);
        add_saturating(stats.mesh_dropped_particle_count, packet.count);
        return;
    }

    const auto& core = scene_particles.system.particles.core;
    for (const uint32_t particle_index : particle_render_packet_indices(scene_particles.system, packet)) {
        if (particle_index >= core.position.size()) {
            add_saturating(stats.mesh_invalid_particle_index_count, 1u);
            add_saturating(stats.mesh_dropped_particle_count, 1u);
            continue;
        }
        nw::render::nwn::render_model_instance_with_root(
            bridge.legacy_context(),
            bridge.cmd,
            *chunk_model,
            particle_mesh_root(core, particle_index),
            bridge.ctx);
        add_saturating(stats.mesh_submitted_particle_count, 1u);
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

    uint32_t record_index = kInvalidAreaRenderRecordIndex;
    switch (scene_particles.owner_kind) {
    case nw::render::ModelInstanceKind::nwn_legacy:
        if (!scene_particles.owner) {
            return true;
        }
        record_index = filter.area_scene->record_index_for_model(scene_particles.owner_model_index);
        break;
    case nw::render::ModelInstanceKind::render_model:
        record_index = filter.area_scene->record_index_for_render_model(scene_particles.owner_model_index);
        break;
    }
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
    SceneMeshParticleBridge mesh_bridge{service, resources, cmd, ctx};
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
                render_scene_mesh_particle_packet(
                    mesh_bridge,
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
