#pragma once

#include <nw/resources/assets.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nw::render::viewer {

enum class PreviewLoadResourceStatus {
    found,
    missing,
};

enum class PreviewLoadEventSeverity {
    info,
    warning,
    error,
};

struct PreviewLoadResource {
    nw::Resource resource;
    PreviewLoadResourceStatus status = PreviewLoadResourceStatus::found;
    std::vector<std::string> origins;
};

struct PreviewLoadEvent {
    PreviewLoadEventSeverity severity = PreviewLoadEventSeverity::info;
    std::string category;
    std::string message;
};

struct PreviewLoadParticleSystemReport {
    std::string owner;
    size_t emitter_count = 0;
    size_t material_count = 0;
    size_t alpha_material_count = 0;
    size_t cutout_material_count = 0;
    size_t additive_material_count = 0;
    size_t named_texture_count = 0;
    size_t missing_texture_count = 0;
    size_t alpha_texture_count = 0;
    size_t opaque_texture_count = 0;
    uint32_t max_particles_total = 0;
    size_t import_warning_count = 0;
    size_t compile_warning_count = 0;
    size_t effect_event_count = 0;
};

struct PreviewLoadMaterialReport {
    std::string owner;
    size_t material_count = 0;
    size_t fallback_material_count = 0;
    size_t plt_albedo_count = 0;
    size_t plt_enabled_count = 0;
    size_t emissive_material_count = 0;
    size_t double_sided_count = 0;
    size_t opaque_count = 0;
    size_t cutout_count = 0;
    size_t transparent_count = 0;
    size_t water_count = 0;
    size_t unknown_alpha_mode_count = 0;
    size_t color_key_count = 0;
    size_t albedo_source_count = 0;
    size_t normal_source_count = 0;
    size_t surface_source_count = 0;
    size_t emissive_source_count = 0;
    size_t albedo_bound_count = 0;
    size_t normal_bound_count = 0;
    size_t surface_bound_count = 0;
    size_t emissive_bound_count = 0;
    size_t invalid_scalar_count = 0;
    float roughness_min = 0.0f;
    float roughness_max = 0.0f;
    float specular_strength_min = 0.0f;
    float specular_strength_max = 0.0f;
    float normal_scale_min = 0.0f;
    float normal_scale_max = 0.0f;
};

struct PreviewLoadGeometryReport {
    std::string owner;
    size_t primitive_count = 0;
    size_t static_primitive_count = 0;
    size_t skinned_primitive_count = 0;
    size_t deformed_primitive_count = 0;
    size_t shadow_caster_count = 0;
    size_t vertex_count = 0;
    size_t index_count = 0;
    size_t node_count = 0;
    size_t socket_count = 0;
    size_t skin_count = 0;
    size_t skeleton_count = 0;
    size_t animation_count = 0;
    size_t deformer_count = 0;
    size_t particle_system_count = 0;
    size_t normal_repair_count = 0;
    size_t tangent_repair_count = 0;
    size_t water_name_heuristic_count = 0;
    size_t foliage_name_heuristic_count = 0;
    size_t skipped_empty_mesh_count = 0;
    size_t skipped_skin_mesh_count = 0;
    size_t primitive_overflow_count = 0;
};

struct PreviewLoadReport {
    std::string source;
    std::string kind;
    std::vector<std::string> model_names;
    std::vector<PreviewLoadResource> resources;
    std::vector<PreviewLoadMaterialReport> materials;
    std::vector<PreviewLoadGeometryReport> geometries;
    std::vector<PreviewLoadParticleSystemReport> particles;
    std::vector<PreviewLoadEvent> events;

    size_t missing_resource_count() const;
    size_t warning_count() const;
    size_t error_count() const;
};

PreviewLoadReport build_preview_load_report(std::string_view source, std::string_view animation_name = {});
std::string_view preview_load_resource_status_label(PreviewLoadResourceStatus status) noexcept;
std::string_view preview_load_event_severity_label(PreviewLoadEventSeverity severity) noexcept;

} // namespace nw::render::viewer
