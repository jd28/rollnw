#pragma once

#include <cstdint>

namespace nw::gfx {
struct CommandList;
} // namespace nw::gfx

namespace nw::render {
struct RenderContext;
struct RenderService;
} // namespace nw::render

namespace nw::render::viewer {

class AreaRenderFrame;
class AreaRenderScene;
struct PreviewScene;
struct SceneParticleSystem;

struct ParticleRenderFilter {
    const AreaRenderScene* area_scene = nullptr;
    const AreaRenderFrame* area_frame = nullptr;
    bool cull_by_owner_area_record = false;
};

struct ParticleRenderStats {
    uint32_t submitted_system_count = 0;
    uint32_t culled_system_count = 0;
    uint32_t invalid_material_packet_count = 0;
    uint32_t mesh_packet_count = 0;
    uint32_t mesh_particle_count = 0;
    uint32_t mesh_submitted_particle_count = 0;
    uint32_t mesh_dropped_particle_count = 0;
    uint32_t mesh_missing_resref_packet_count = 0;
    uint32_t mesh_missing_model_packet_count = 0;
    uint32_t mesh_invalid_particle_index_count = 0;
};

[[nodiscard]] bool particle_system_visible_for_render_filter(
    const SceneParticleSystem& scene_particles,
    const ParticleRenderFilter& filter) noexcept;

ParticleRenderStats render_scene_particles(
    nw::render::RenderService& service,
    nw::gfx::CommandList* cmd,
    const PreviewScene& scene,
    const nw::render::RenderContext& ctx,
    ParticleRenderFilter filter = {});

} // namespace nw::render::viewer
