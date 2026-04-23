#pragma once

#include "renderer.hpp"

#include <nw/model/mdl_particle_import.hpp>
#include <nw/render/gltf/import_gltf.hpp>
#include <nw/render/nwn/model_loader.hpp>
#include <nw/render/particle_compile.hpp>
#include <nw/render/particle_def.hpp>
#include <nw/render/particle_system.hpp>

#include <nw/objects/Area.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

namespace mudl {

using nw::render::gltf::ImportGltfDesc;
using nw::render::gltf::import_gltf;
using nw::render::nwn::Bounds;
using nw::render::nwn::ModelInstance;

enum class PreferredModelAnimationContext {
    sequence_effect,
    particle_preview,
    hold,
};

struct MeshTriangle {
    glm::vec3 v0, v1, v2;
};

struct ModelMeshCollisionProvider : public nw::render::ParticleCollisionProvider {
    std::vector<MeshTriangle> triangles;
    nw::render::ParticleCollisionQuery trace_particle(glm::vec3 from, glm::vec3 to, float radius) const override;
};

struct SceneParticleSystem {
    const ModelInstance* owner = nullptr;
    nw::model::ParticleImportResult import;
    nw::render::ParticleCompileResult compiled;
    nw::render::ParticleSystemInstance system;
    std::unique_ptr<ModelMeshCollisionProvider> collision;
    // World-space mesh AABB used to kill particles that escape the model geometry.
    // Valid when collision is non-null.
    glm::vec3 mesh_aabb_min{0.0f};
    glm::vec3 mesh_aabb_max{0.0f};
    float animation_time = 0.0f;
    bool animation_time_initialized = false;
    bool owner_visible_last = true;
};

struct PreviewScene {
    std::vector<std::unique_ptr<ModelInstance>> models;
    std::vector<std::unique_ptr<nw::render::RenderModel>> static_models;
    std::vector<SceneParticleSystem> particles;
    size_t vertex_count = 0;
    size_t index_count = 0;
    Bounds bounds{};
    bool is_area = false;
    int area_width = 0;
    int area_height = 0;
    float area_overlay_z = 0.0f;
    nw::AreaFlags area_flags = nw::AreaFlags::none;
    nw::AreaWeather area_weather{};

    bool load_animation(std::string_view name);
    void rebuild_particles(std::string_view animation_name = {});
    void update(int32_t dt_ms);
    void set_particle_target_point(const ModelInstance* owner, const glm::vec3& target_point);
    Bounds current_bounds() const;
    void add(std::unique_ptr<ModelInstance> model);
    void add(std::unique_ptr<nw::render::RenderModel> model);
    void add_particle_effect(nw::render::ParticleEffectDef effect);
};

std::unique_ptr<PreviewScene> load_preview_scene(Renderer& renderer, std::string_view source);
std::unique_ptr<PreviewScene> load_preview_scene(Renderer& renderer, std::span<const std::string> sources);
std::unique_ptr<PreviewScene> load_area_scene(Renderer& renderer, std::string_view area_resref);
std::string_view preferred_model_animation_name(
    const nw::model::Mdl& mdl, PreferredModelAnimationContext context);

} // namespace mudl
