#include "preview_scene.hpp"

#include "preview_object.hpp"
#include "preview_plt.hpp"
#include "tile_light.hpp"

#include <nw/model/Mdl.hpp>
#include <nw/render/model_instance_animation.hpp>
#include <nw/render/model_instance_attachment.hpp>
#include <nw/render/particle_json.hpp>

#include <nw/formats/Tileset.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/ModelCache.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/kernel/TilesetRegistry.hpp>
#include <nw/kernel/TwoDACache.hpp>
#include <nw/log.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Encounter.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Trigger.hpp>
#include <nw/profiles/nwn1/constants.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/serialization/Gff.hpp>
#include <nw/util/error_context.hpp>
#include <nw/util/string.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

#include <fmt/format.h>

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <iterator>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <unordered_map>

namespace nw::render::viewer {

namespace {

struct RenderModelPreviewLoad {
    std::unique_ptr<nw::render::RenderModel> model;
    std::vector<PreviewLoadEvent> events;
    uint32_t static_deformer_count = 0;
};

RenderModelPreviewLoad load_nwn_render_model_preview(
    PreviewRenderResources& resources, std::string_view source);

void add_preview_load_event(std::vector<PreviewLoadEvent>& events,
    PreviewLoadEventSeverity severity,
    std::string category,
    std::string message)
{
    events.push_back({
        .severity = severity,
        .category = std::move(category),
        .message = std::move(message),
    });
}

void append_preview_load_events(std::vector<PreviewLoadEvent>& target, std::vector<PreviewLoadEvent>& events)
{
    target.insert(target.end(),
        std::make_move_iterator(events.begin()),
        std::make_move_iterator(events.end()));
    events.clear();
}

void append_scene_load_events(PreviewScene& scene, std::vector<PreviewLoadEvent>& events)
{
    scene.load_report.events.insert(scene.load_report.events.end(), events.begin(), events.end());
    append_preview_load_events(scene.source_load_events, events);
}

bool environment_flag_enabled(const char* name)
{
    const char* value = std::getenv(name);
    return value
        && (std::strcmp(value, "1") == 0
            || std::strcmp(value, "true") == 0
            || std::strcmp(value, "TRUE") == 0
            || std::strcmp(value, "yes") == 0
            || std::strcmp(value, "YES") == 0
            || std::strcmp(value, "on") == 0
            || std::strcmp(value, "ON") == 0);
}

bool viewer_tile_light_debug_shapes_enabled()
{
    static const bool enabled = environment_flag_enabled("ROLLNW_VIEWER_TILE_LIGHT_DEBUG");
    return enabled;
}

void log_preview_warning_context()
{
    if (nw::error_context_stack) {
        LOG_F(WARNING, "\n{}", nw::get_error_context());
    }
}

void log_preview_error_context()
{
    if (nw::error_context_stack) {
        LOG_F(ERROR, "\n{}", nw::get_error_context());
    }
}

std::string preview_resource_key(const nw::Resource& resource)
{
    return resource.resref.string() + "." + std::string(nw::ResourceType::to_string(resource.type));
}

std::string clean_preview_resource_token(std::string_view token)
{
    while (!token.empty() && (token.front() == '"' || token.front() == '\'')) {
        token.remove_prefix(1);
    }
    while (!token.empty() && (token.back() == '"' || token.back() == '\'' || token.back() == ',' || token.back() == ';')) {
        token.remove_suffix(1);
    }

    std::string cleaned{token};
    std::replace(cleaned.begin(), cleaned.end(), '\\', '/');
    const auto slash = cleaned.find_last_of('/');
    if (slash != std::string::npos) {
        cleaned = cleaned.substr(slash + 1);
    }
    return cleaned;
}

bool is_null_preview_resource_name(std::string_view name)
{
    return name.empty()
        || nw::string::icmp(name, "null")
        || nw::string::icmp(name, "none")
        || nw::string::icmp(name, "****");
}

bool looks_like_preview_resref(std::string_view token)
{
    if (token.empty() || token.size() > 64) {
        return false;
    }

    bool has_alpha = false;
    for (char ch : token) {
        const auto uch = static_cast<unsigned char>(ch);
        if (std::isalpha(uch)) {
            has_alpha = true;
        }
        if (!std::isalnum(uch) && ch != '_' && ch != '-' && ch != '.') {
            return false;
        }
    }
    return has_alpha;
}

class PreviewLoadReportBuilder {
public:
    explicit PreviewLoadReportBuilder(PreviewLoadReport& report)
        : report_{report}
    {
    }

    void add_resource(nw::Resource resource, PreviewLoadResourceStatus status, std::string origin)
    {
        if (!resource.valid()) {
            return;
        }

        const auto key = preview_resource_key(resource);
        auto [it, inserted] = resource_indices_.emplace(key, report_.resources.size());
        if (inserted) {
            report_.resources.push_back({
                .resource = resource,
                .status = status,
                .origins = {},
            });
        }

        auto& item = report_.resources[it->second];
        if (status == PreviewLoadResourceStatus::missing) {
            item.status = PreviewLoadResourceStatus::missing;
        }
        if (std::find(item.origins.begin(), item.origins.end(), origin) == item.origins.end()) {
            item.origins.push_back(std::move(origin));
        }
    }

    void add_existing_or_missing_resource(nw::Resource resource, std::string origin)
    {
        add_resource(resource,
            nw::kernel::resman().contains(resource)
                ? PreviewLoadResourceStatus::found
                : PreviewLoadResourceStatus::missing,
            std::move(origin));
    }

    void add_event(PreviewLoadEventSeverity severity, std::string category, std::string message)
    {
        report_.events.push_back({
            .severity = severity,
            .category = std::move(category),
            .message = std::move(message),
        });
    }

private:
    PreviewLoadReport& report_;
    std::unordered_map<std::string, size_t> resource_indices_;
};

bool material_report_exists(const PreviewLoadReport& report, std::string_view owner)
{
    return std::any_of(report.materials.begin(), report.materials.end(), [owner](const auto& item) {
        return item.owner == owner;
    });
}

bool geometry_report_exists(const PreviewLoadReport& report, std::string_view owner)
{
    return std::any_of(report.geometries.begin(), report.geometries.end(), [owner](const auto& item) {
        return item.owner == owner;
    });
}

bool material_has_emissive(const nw::render::Material& material)
{
    return material.emissive.x != 0.0f || material.emissive.y != 0.0f || material.emissive.z != 0.0f;
}

bool material_texture_source_valid(uint32_t source_index) noexcept
{
    return source_index != nw::render::kInvalidModelAssetTextureSourceIndex;
}

void update_material_scalar_ranges(
    PreviewLoadMaterialReport& material_report,
    const nw::render::Material& material,
    bool& scalar_ranges_initialized)
{
    const bool valid_scalars = std::isfinite(material.roughness)
        && std::isfinite(material.specular_strength)
        && std::isfinite(material.normal_scale);
    if (!valid_scalars) {
        ++material_report.invalid_scalar_count;
        return;
    }

    if (!scalar_ranges_initialized) {
        material_report.roughness_min = material.roughness;
        material_report.roughness_max = material.roughness;
        material_report.specular_strength_min = material.specular_strength;
        material_report.specular_strength_max = material.specular_strength;
        material_report.normal_scale_min = material.normal_scale;
        material_report.normal_scale_max = material.normal_scale;
        scalar_ranges_initialized = true;
        return;
    }

    material_report.roughness_min = std::min(material_report.roughness_min, material.roughness);
    material_report.roughness_max = std::max(material_report.roughness_max, material.roughness);
    material_report.specular_strength_min = std::min(material_report.specular_strength_min, material.specular_strength);
    material_report.specular_strength_max = std::max(material_report.specular_strength_max, material.specular_strength);
    material_report.normal_scale_min = std::min(material_report.normal_scale_min, material.normal_scale);
    material_report.normal_scale_max = std::max(material_report.normal_scale_max, material.normal_scale);
}

void add_material_texture_source_counts(
    PreviewLoadMaterialReport& material_report,
    std::span<const nw::render::ModelAssetMaterialTextureSources> sources)
{
    for (const auto& source : sources) {
        if (material_texture_source_valid(source.albedo)) {
            ++material_report.albedo_source_count;
        }
        if (material_texture_source_valid(source.normal)) {
            ++material_report.normal_source_count;
        }
        if (material_texture_source_valid(source.metallic_roughness)
            || material_texture_source_valid(source.occlusion)) {
            ++material_report.surface_source_count;
        }
        if (material_texture_source_valid(source.emissive)) {
            ++material_report.emissive_source_count;
        }
    }
}

void add_material_to_report(
    PreviewLoadMaterialReport& material_report,
    const nw::render::Material& material,
    bool& scalar_ranges_initialized)
{
    if (material.material_uses_fallback) {
        ++material_report.fallback_material_count;
    }
    if (material.albedo_uses_plt) {
        ++material_report.plt_albedo_count;
    }
    if (material.plt_enabled) {
        ++material_report.plt_enabled_count;
    }
    if (material_has_emissive(material)) {
        ++material_report.emissive_material_count;
    }
    if (material.double_sided) {
        ++material_report.double_sided_count;
    }
    if (material.color_key_threshold.w > 0.0f) {
        ++material_report.color_key_count;
    }
    if (nw::gfx::bindless_texture_index_valid(material.albedo_index)) {
        ++material_report.albedo_bound_count;
    }
    if (nw::gfx::bindless_texture_index_valid(material.normal_index)) {
        ++material_report.normal_bound_count;
    }
    if (nw::gfx::bindless_texture_index_valid(material.surface_index)) {
        ++material_report.surface_bound_count;
    }
    if (nw::gfx::bindless_texture_index_valid(material.emissive_index)) {
        ++material_report.emissive_bound_count;
    }
    update_material_scalar_ranges(material_report, material, scalar_ranges_initialized);

    switch (material.alpha_mode) {
    case nw::render::MaterialMode::opaque:
        ++material_report.opaque_count;
        break;
    case nw::render::MaterialMode::cutout:
        ++material_report.cutout_count;
        break;
    case nw::render::MaterialMode::transparent:
        ++material_report.transparent_count;
        break;
    case nw::render::MaterialMode::water:
        ++material_report.water_count;
        break;
    default:
        ++material_report.unknown_alpha_mode_count;
        break;
    }
}

void add_model_asset_material_report(
    PreviewLoadReport& report,
    std::string_view owner,
    const nw::render::ModelAsset& asset)
{
    const std::string owner_text = owner.empty() ? std::string{"<unnamed>"} : std::string{owner};
    if (material_report_exists(report, owner_text)) {
        return;
    }

    PreviewLoadMaterialReport material_report{
        .owner = owner_text,
        .material_count = asset.materials.size(),
    };
    add_material_texture_source_counts(material_report, asset.material_texture_sources);

    bool scalar_ranges_initialized = false;
    for (const auto& material : asset.materials) {
        add_material_to_report(material_report, material, scalar_ranges_initialized);
    }

    report.materials.push_back(std::move(material_report));
}

void add_model_asset_geometry_report(
    PreviewLoadReport& report,
    std::string_view owner,
    const nw::render::ModelAsset& asset,
    const nw::render::nwn::NwnModelAssetImportStats* import_stats)
{
    const std::string owner_text = owner.empty() ? std::string{"<unnamed>"} : std::string{owner};
    if (geometry_report_exists(report, owner_text)) {
        return;
    }

    PreviewLoadGeometryReport geometry_report{
        .owner = owner_text,
        .primitive_count = asset.primitives.size(),
        .node_count = asset.nodes.size(),
        .socket_count = asset.sockets.size(),
        .skin_count = asset.skins.size(),
        .skeleton_count = asset.skeletons.size(),
        .animation_count = asset.animations.size(),
        .deformer_count = asset.deformers.size(),
        .particle_system_count = asset.particle_systems.size(),
    };

    for (const auto& primitive : asset.primitives) {
        if (primitive.uses_skinned_vertices()) {
            ++geometry_report.skinned_primitive_count;
        } else {
            ++geometry_report.static_primitive_count;
        }
        if (primitive.deformer != nw::render::kInvalidModelDeformerIndex) {
            ++geometry_report.deformed_primitive_count;
        }
        if (primitive.casts_shadow) {
            ++geometry_report.shadow_caster_count;
        }
        geometry_report.vertex_count += primitive.vertex_count();
        geometry_report.index_count += primitive.index_count();
    }

    if (import_stats) {
        geometry_report.normal_repair_count = import_stats->normal_repair_count;
        geometry_report.tangent_repair_count = import_stats->tangent_repair_count;
        geometry_report.water_name_heuristic_count = import_stats->water_name_heuristic_count;
        geometry_report.foliage_name_heuristic_count = import_stats->foliage_name_heuristic_count;
        geometry_report.skipped_empty_mesh_count = import_stats->skipped_empty_mesh_count;
        geometry_report.skipped_skin_mesh_count = import_stats->skipped_skin_mesh_count;
        geometry_report.primitive_overflow_count = import_stats->primitive_overflow_count;
    }

    report.geometries.push_back(std::move(geometry_report));
}

void add_render_model_material_report(
    PreviewLoadReport& report,
    std::string_view owner,
    const nw::render::RenderModel& model,
    const nw::render::ModelInstance* instance,
    const nw::render::ModelMaterialOverrideStore& material_overrides)
{
    const std::string owner_text = owner.empty() ? std::string{"<unnamed>"} : std::string{owner};
    if (material_report_exists(report, owner_text)) {
        return;
    }

    PreviewLoadMaterialReport material_report{
        .owner = owner_text,
        .material_count = model.materials.size(),
    };
    bool scalar_ranges_initialized = false;
    for (size_t material_index = 0; material_index < model.materials.size(); ++material_index) {
        const auto* material = &model.materials[material_index];
        if (instance && material_index < instance->material_override_handles.size()) {
            if (const auto* override = material_overrides.get(instance->material_override_handles[material_index])) {
                material = &override->material;
            }
        }
        add_material_to_report(material_report, *material, scalar_ranges_initialized);
    }

    report.materials.push_back(std::move(material_report));
}

void add_render_model_geometry_report(
    PreviewLoadReport& report,
    std::string_view owner,
    const nw::render::RenderModel& model)
{
    const std::string owner_text = owner.empty() ? std::string{"<unnamed>"} : std::string{owner};
    if (geometry_report_exists(report, owner_text)) {
        return;
    }

    PreviewLoadGeometryReport geometry_report{
        .owner = owner_text,
        .primitive_count = model.primitives.size(),
        .node_count = model.nodes.size(),
        .socket_count = model.sockets.size(),
        .skin_count = model.skins.size(),
        .skeleton_count = model.skeletons.size(),
        .animation_count = model.animations.size(),
        .deformer_count = model.deformers.size(),
        .particle_system_count = model.particle_systems.size(),
    };

    for (const auto& primitive : model.primitives) {
        if (primitive.skinned) {
            ++geometry_report.skinned_primitive_count;
        } else {
            ++geometry_report.static_primitive_count;
        }
        if (primitive.deformer != nw::render::kInvalidModelDeformerIndex) {
            ++geometry_report.deformed_primitive_count;
        }
        if (primitive.casts_shadow) {
            ++geometry_report.shadow_caster_count;
        }
        geometry_report.vertex_count += primitive.vertex_count;
        geometry_report.index_count += primitive.index_count;
    }

    report.geometries.push_back(std::move(geometry_report));
}

std::string mesh_texture_name(const nw::model::TrimeshNode& node)
{
    if (!node.bitmap.empty() && !is_null_preview_resource_name(node.bitmap)) {
        return std::string(node.bitmap);
    }
    if (!node.textures[0].empty() && !is_null_preview_resource_name(node.textures[0])) {
        return std::string(node.textures[0]);
    }
    return {};
}

void add_texture_family(PreviewLoadReportBuilder& builder, std::string_view raw_name, std::string origin)
{
    const auto cleaned = clean_preview_resource_token(raw_name);
    if (is_null_preview_resource_name(cleaned) || !looks_like_preview_resref(cleaned)) {
        return;
    }

    const std::filesystem::path path{cleaned};
    if (!path.extension().empty()) {
        const auto type = nw::ResourceType::from_extension(path.extension().string());
        if (type != nw::ResourceType::invalid) {
            builder.add_existing_or_missing_resource({path.stem().string(), type}, std::move(origin));
        }
        return;
    }

    static constexpr std::array texture_types{
        nw::ResourceType::dds,
        nw::ResourceType::tga,
        nw::ResourceType::plt,
    };

    bool found_texture = false;
    for (auto type : texture_types) {
        nw::Resource texture{cleaned, type};
        if (nw::kernel::resman().contains(texture)) {
            builder.add_resource(texture, PreviewLoadResourceStatus::found, origin);
            found_texture = true;
        }
    }

    nw::Resource txi{cleaned, nw::ResourceType::txi};
    if (nw::kernel::resman().contains(txi)) {
        builder.add_resource(txi, PreviewLoadResourceStatus::found, origin);
    }
    nw::Resource shd{cleaned, nw::ResourceType::shd};
    if (nw::kernel::resman().contains(shd)) {
        builder.add_resource(shd, PreviewLoadResourceStatus::found, origin);
    }

    if (!found_texture) {
        builder.add_resource({cleaned, nw::ResourceType::dds}, PreviewLoadResourceStatus::missing, origin);
        builder.add_resource({cleaned, nw::ResourceType::tga}, PreviewLoadResourceStatus::missing, std::move(origin));
    }
}

void add_material_resource(PreviewLoadReportBuilder& builder, std::string_view raw_name, std::string origin)
{
    const auto cleaned = clean_preview_resource_token(raw_name);
    if (is_null_preview_resource_name(cleaned) || !looks_like_preview_resref(cleaned)) {
        return;
    }

    const std::filesystem::path path{cleaned};
    if (!path.extension().empty()) {
        const auto type = nw::ResourceType::from_extension(path.extension().string());
        if (type == nw::ResourceType::mtr) {
            builder.add_existing_or_missing_resource({path.stem().string(), type}, std::move(origin));
        }
        return;
    }

    nw::Resource material{cleaned, nw::ResourceType::mtr};
    if (nw::kernel::resman().contains(material)) {
        builder.add_resource(material, PreviewLoadResourceStatus::found, std::move(origin));
    }
}

void scan_preview_node_dependencies(PreviewLoadReportBuilder& builder, const nw::model::Node* node, std::string_view model_name)
{
    if (!node) {
        return;
    }

    const auto origin = std::string(model_name) + ":" + std::string(node->name);
    if (const auto* trimesh = dynamic_cast<const nw::model::TrimeshNode*>(node)) {
        add_texture_family(builder, mesh_texture_name(*trimesh), origin);
        for (const auto& texture : trimesh->textures) {
            add_texture_family(builder, texture, origin);
        }
        add_material_resource(builder, trimesh->materialname, origin);
    } else if (const auto* emitter = dynamic_cast<const nw::model::EmitterNode*>(node)) {
        add_texture_family(builder, emitter->texture, origin);
    } else if (const auto* light = dynamic_cast<const nw::model::LightNode*>(node)) {
        for (const auto& texture : light->textures) {
            add_texture_family(builder, texture, origin);
        }
    } else if (const auto* reference = dynamic_cast<const nw::model::ReferenceNode*>(node)) {
        if (!is_null_preview_resource_name(reference->refmodel)) {
            builder.add_existing_or_missing_resource({reference->refmodel, nw::ResourceType::mdl}, origin);
        }
    }

    for (const auto* child : node->children) {
        scan_preview_node_dependencies(builder, child, model_name);
    }
}

void scan_preview_model_dependencies(
    PreviewLoadReportBuilder& builder,
    const nw::model::Mdl& mdl,
    std::set<std::string>& scanned_models,
    bool scan_render_dependencies = true)
{
    const std::string model_name = mdl.model.name.c_str();
    if (model_name.empty()) {
        return;
    }

    builder.add_existing_or_missing_resource({model_name, nw::ResourceType::mdl}, "model");
    if (!scanned_models.insert(model_name).second) {
        return;
    }

    if (!is_null_preview_resource_name(mdl.model.supermodel_name)) {
        builder.add_existing_or_missing_resource({mdl.model.supermodel_name, nw::ResourceType::mdl},
            model_name + ":supermodel");
        if (auto* supermodel = nw::kernel::models().load(mdl.model.supermodel_name)) {
            scan_preview_model_dependencies(builder, *supermodel, scanned_models, false);
        }
    }

    if (!scan_render_dependencies) {
        return;
    }

    for (const auto& node : mdl.model.nodes) {
        if (node && !node->parent) {
            scan_preview_node_dependencies(builder, node.get(), model_name);
        }
    }
}

std::string preview_particle_owner_name(const SceneParticleSystem& scene_particles)
{
    if (scene_particles.owner && scene_particles.owner->mdl_ && !scene_particles.owner->mdl_->model.name.empty()) {
        return scene_particles.owner->mdl_->model.name.c_str();
    }
    if (!scene_particles.import.effect.name.empty()) {
        return scene_particles.import.effect.name;
    }
    return "<none>";
}

uint32_t add_saturating_u32(uint32_t lhs, uint32_t rhs) noexcept
{
    const uint64_t total = static_cast<uint64_t>(lhs) + static_cast<uint64_t>(rhs);
    return total > std::numeric_limits<uint32_t>::max()
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(total);
}

uint32_t static_model_index_u32(size_t model_index) noexcept
{
    return model_index > std::numeric_limits<uint32_t>::max()
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(model_index);
}

size_t render_model_skinned_primitive_count(const nw::render::RenderModel& model) noexcept
{
    return static_cast<size_t>(std::count_if(model.primitives.begin(), model.primitives.end(), [](const auto& prim) {
        return prim.skinned;
    }));
}

size_t render_model_skin_matrix_row_count(const nw::render::ModelInstance& instance) noexcept
{
    size_t result = 0;
    for (const auto& matrices : instance.animation.skin_matrices) {
        result += matrices.size();
    }
    return result;
}

size_t valid_model_attachment_node_count(const nw::render::ModelInstance& instance) noexcept
{
    return static_cast<size_t>(std::count_if(
        instance.attachment_node_transform_valid.begin(),
        instance.attachment_node_transform_valid.end(),
        [](uint8_t valid) noexcept {
            return valid != 0u;
        }));
}

uint32_t selected_runtime_clip_index(
    const nw::render::RenderModel& model,
    const nw::render::ModelInstance* instance) noexcept
{
    if (!instance || model.animations.empty()
        || model.animations.size() > std::numeric_limits<uint32_t>::max()) {
        return std::numeric_limits<uint32_t>::max();
    }
    return instance->animation.clip % static_cast<uint32_t>(model.animations.size());
}

std::string selected_runtime_clip_name(
    const nw::render::RenderModel& model,
    uint32_t clip_index)
{
    if (clip_index == std::numeric_limits<uint32_t>::max()
        || static_cast<size_t>(clip_index) >= model.animations.size()) {
        return {};
    }
    const auto& clip = model.animations[clip_index];
    if (!clip.name.empty()) {
        return clip.name;
    }
    return fmt::format("clip {}", clip_index);
}

bool render_model_particles_match_owner(
    const SceneParticleSystem& scene_particles,
    nw::render::ModelInstanceHandle owner_handle,
    uint32_t owner_model_index) noexcept
{
    if (scene_particles.owner_kind != nw::render::ModelInstanceKind::render_model
        || scene_particles.owner_model_index != owner_model_index) {
        return false;
    }
    if (owner_handle.valid() && scene_particles.owner_instance_handle.valid()
        && scene_particles.owner_instance_handle != owner_handle) {
        return false;
    }
    return true;
}

} // namespace

size_t PreviewLoadReport::missing_resource_count() const
{
    return static_cast<size_t>(std::count_if(resources.begin(), resources.end(), [](const auto& item) {
        return item.status == PreviewLoadResourceStatus::missing;
    }));
}

size_t PreviewLoadReport::warning_count() const
{
    return static_cast<size_t>(std::count_if(events.begin(), events.end(), [](const auto& event) {
        return event.severity == PreviewLoadEventSeverity::warning;
    }));
}

size_t PreviewLoadReport::error_count() const
{
    return static_cast<size_t>(std::count_if(events.begin(), events.end(), [](const auto& event) {
        return event.severity == PreviewLoadEventSeverity::error;
    }));
}

PreviewRuntimeModelReports build_preview_runtime_model_reports(
    const PreviewScene& scene,
    const nw::render::PreparedModelSurfaceDrawList* prepared_surfaces)
{
    PreviewRuntimeModelReports reports{};
    reports.models.reserve(scene.static_models.size());
    reports.prepared_skin_table_available = prepared_surfaces != nullptr;
    if (prepared_surfaces) {
        reports.prepared_skin_table_stats = prepared_surfaces->render_model_skins.stats;
    }

    for (size_t model_index = 0; model_index < scene.static_models.size(); ++model_index) {
        const auto& model = scene.static_models[model_index];
        if (!model) {
            continue;
        }

        const auto* instance = scene.static_model_instance(model_index);
        const auto owner_model_index = static_model_index_u32(model_index);
        const auto owner_handle = model_index < scene.static_model_instance_handles.size()
            ? scene.static_model_instance_handles[model_index]
            : nw::render::ModelInstanceHandle{};

        PreviewRuntimeModelReport row{
            .owner = model->name.empty() ? std::string{"<unnamed>"} : model->name,
            .kind = nw::render::ModelInstanceKind::render_model,
            .model_index = owner_model_index,
            .instance_handle_valid = owner_handle.valid(),
            .instance_present = instance != nullptr,
            .visible = instance ? instance->visible : false,
            .primitive_count = model->primitives.size(),
            .skinned_primitive_count = render_model_skinned_primitive_count(*model),
            .skin_count = model->skins.size(),
            .skeleton_count = model->skeletons.size(),
            .animation_count = model->animations.size(),
            .particle_system_count = model->particle_systems.size(),
        };

        if (instance) {
            row.selected_clip_index = selected_runtime_clip_index(*model, instance);
            row.selected_clip_name = selected_runtime_clip_name(*model, row.selected_clip_index);
            row.clip_time = instance->animation.time;
            row.animation_enabled = instance->animation.enabled;
            row.animation_backend_ready = instance->animation.backend != nullptr;
            row.animation_looping = instance->animation.looping;
            row.skin_matrix_table_count = instance->animation.skin_matrices.size();
            row.skin_matrix_row_count = render_model_skin_matrix_row_count(*instance);
            row.attachment_node_count = instance->attachment_node_world_transforms.size();
            row.valid_attachment_node_count = valid_model_attachment_node_count(*instance);
        }

        for (const auto& scene_particles : scene.particles) {
            if (!render_model_particles_match_owner(scene_particles, owner_handle, owner_model_index)) {
                continue;
            }
            ++row.scene_particle_system_count;
            row.scene_particle_emitter_count += scene_particles.system.emitters.size();
            row.scene_particle_event_count += scene_particles.import.effect_events.size();
            row.scene_particle_live_particle_count += scene_particles.system.particles.core.position.size();
            row.scene_particle_max_particles_total = add_saturating_u32(
                row.scene_particle_max_particles_total,
                scene_particles.compiled.effect.max_particles_total);
            for (const auto& emitter : scene_particles.system.emitters) {
                if (emitter.active) {
                    ++row.scene_particle_active_emitter_count;
                }
            }
        }

        ++reports.render_model_count;
        if (row.skinned_primitive_count > 0) {
            ++reports.skinned_model_count;
        }
        if (row.animation_count > 0) {
            ++reports.animated_model_count;
        }
        if (row.animation_enabled) {
            ++reports.animation_enabled_model_count;
        }
        if (row.animation_backend_ready) {
            ++reports.animation_backend_ready_model_count;
        }
        reports.skin_matrix_row_count += row.skin_matrix_row_count;
        reports.scene_particle_system_count += row.scene_particle_system_count;
        reports.scene_particle_emitter_count += row.scene_particle_emitter_count;
        reports.scene_particle_live_particle_count += row.scene_particle_live_particle_count;
        reports.models.push_back(std::move(row));
    }

    return reports;
}

std::string_view preview_load_resource_status_label(PreviewLoadResourceStatus status) noexcept
{
    switch (status) {
    case PreviewLoadResourceStatus::found:
        return "found";
    case PreviewLoadResourceStatus::missing:
        return "missing";
    }
    return "unknown";
}

std::string_view preview_load_event_severity_label(PreviewLoadEventSeverity severity) noexcept
{
    switch (severity) {
    case PreviewLoadEventSeverity::info:
        return "info";
    case PreviewLoadEventSeverity::warning:
        return "warning";
    case PreviewLoadEventSeverity::error:
        return "error";
    }
    return "unknown";
}

Lighting studio_preview_lighting()
{
    Lighting result{};
    result.ambient = glm::vec3{0.16f, 0.16f, 0.18f};
    result.key_direction = glm::normalize(glm::vec3{-0.42f, 0.72f, 0.55f});
    result.key_color = glm::vec3{1.0f, 0.96f, 0.90f};
    result.key_intensity = 1.75f;
    result.fill_direction = glm::normalize(glm::vec3{0.65f, 0.58f, 0.25f});
    result.fill_color = glm::vec3{0.55f, 0.66f, 0.85f};
    result.fill_intensity = 0.38f;
    result.rim_direction = glm::normalize(glm::vec3{0.25f, -0.78f, 0.42f});
    result.rim_color = glm::vec3{0.78f, 0.88f, 1.0f};
    result.rim_intensity = 0.28f;
    return result;
}

LightingSpace studio_preview_lighting_space() noexcept
{
    return LightingSpace::camera_relative;
}

// Möller-Trumbore ray-triangle intersection returning the hit t value, or -1 if no hit.
static float ray_triangle_intersect(glm::vec3 orig, glm::vec3 dir, glm::vec3 v0, glm::vec3 v1, glm::vec3 v2)
{
    constexpr float kEps = 1e-7f;
    const glm::vec3 e1 = v1 - v0;
    const glm::vec3 e2 = v2 - v0;
    const glm::vec3 h = glm::cross(dir, e2);
    const float a = glm::dot(e1, h);
    if (std::fabs(a) < kEps) return -1.0f;
    const float f = 1.0f / a;
    const glm::vec3 s = orig - v0;
    const float u = f * glm::dot(s, h);
    if (u < 0.0f || u > 1.0f) return -1.0f;
    const glm::vec3 q = glm::cross(s, e1);
    const float v = f * glm::dot(dir, q);
    if (v < 0.0f || u + v > 1.0f) return -1.0f;
    const float t = f * glm::dot(e2, q);
    return t >= kEps ? t : -1.0f;
}

nw::render::ParticleCollisionQuery ModelMeshCollisionProvider::trace_particle(glm::vec3 from, glm::vec3 to, float /*radius*/) const
{
    const glm::vec3 dir = to - from;
    const float seg_len = glm::length(dir);
    if (seg_len < 1e-7f || triangles.empty()) return {};
    const glm::vec3 norm_dir = dir / seg_len;

    float best_t = seg_len + 1.0f;
    glm::vec3 best_normal{0.0f, 1.0f, 0.0f};
    bool hit = false;

    for (const auto& tri : triangles) {
        const float t = ray_triangle_intersect(from, norm_dir, tri.v0, tri.v1, tri.v2);
        if (t >= 0.0f && t <= seg_len && t < best_t) {
            best_t = t;
            best_normal = glm::normalize(glm::cross(tri.v1 - tri.v0, tri.v2 - tri.v0));
            hit = true;
        }
    }

    if (!hit) return {};
    return {.hit = true, .position = from + norm_dir * best_t, .normal = best_normal};
}

static std::unique_ptr<ModelMeshCollisionProvider> build_mesh_collision(
    const ModelInstance& model, glm::vec3& out_aabb_min, glm::vec3& out_aabb_max)
{
    auto provider = std::make_unique<ModelMeshCollisionProvider>();
    const glm::mat4 root = model.root_transform();
    out_aabb_min = glm::vec3{std::numeric_limits<float>::max()};
    out_aabb_max = glm::vec3{std::numeric_limits<float>::lowest()};

    for (const auto& node_ptr : model.nodes_) {
        if (!node_ptr->is_mesh || !node_ptr->orig_) continue;
        const auto* trim = dynamic_cast<const nw::model::TrimeshNode*>(node_ptr->orig_);
        if (!trim || trim->vertices.empty() || trim->indices.empty()) continue;

        const glm::mat4 world = root * node_ptr->get_transform();
        const size_t tri_count = trim->indices.size() / 3;
        for (size_t i = 0; i < tri_count; ++i) {
            const glm::vec3 v0 = glm::vec3(world * glm::vec4(trim->vertices[trim->indices[i * 3 + 0]].position, 1.0f));
            const glm::vec3 v1 = glm::vec3(world * glm::vec4(trim->vertices[trim->indices[i * 3 + 1]].position, 1.0f));
            const glm::vec3 v2 = glm::vec3(world * glm::vec4(trim->vertices[trim->indices[i * 3 + 2]].position, 1.0f));
            provider->triangles.push_back({v0, v1, v2});
            out_aabb_min = glm::min(out_aabb_min, glm::min(v0, glm::min(v1, v2)));
            out_aabb_max = glm::max(out_aabb_max, glm::max(v0, glm::max(v1, v2)));
        }
    }

    return provider->triangles.empty() ? nullptr : std::move(provider);
}

namespace {

bool model_has_animation(const nw::model::Mdl& mdl, std::string_view animation)
{
    const auto* current = &mdl;
    while (current) {
        if (current->model.find_animation(animation) != nullptr) {
            return true;
        }
        current = current->model.supermodel.get();
    }
    return false;
}

float local_model_animation_length(const nw::model::Mdl& mdl, std::string_view animation)
{
    if (animation.empty()) {
        return 0.0f;
    }
    const auto* anim = mdl.model.find_animation(animation);
    return anim ? anim->length : 0.0f;
}

std::string_view find_first_animation(const nw::model::Mdl& mdl, std::initializer_list<std::string_view> names)
{
    for (const auto name : names) {
        if (model_has_animation(mdl, name)) {
            return name;
        }
    }
    return {};
}

} // namespace

std::string_view preferred_model_animation_name(const nw::model::Mdl& mdl, PreferredModelAnimationContext context)
{
    switch (mdl.model.classification) {
    case nw::model::ModelClass::character: {
        if (auto animation = find_first_animation(mdl, {"cpause1", "pause1"}); !animation.empty()) {
            return animation;
        }
        break;
    }
    case nw::model::ModelClass::door: {
        if (auto animation = find_first_animation(mdl, {"closed", "opened1", "open"}); !animation.empty()) {
            return animation;
        }
        break;
    }
    case nw::model::ModelClass::effect:
    case nw::model::ModelClass::item:
    case nw::model::ModelClass::tile:
    case nw::model::ModelClass::gui:
    case nw::model::ModelClass::invalid:
        break;
    }

    switch (context) {
    case PreferredModelAnimationContext::sequence_effect:
        if (auto animation = find_first_animation(mdl, {"default", "on", "cast01", "impact", "pause1"}); !animation.empty()) {
            return animation;
        }
        break;
    case PreferredModelAnimationContext::particle_preview:
        if (auto animation = find_first_animation(mdl, {"default", "on", "impact", "opened1", "open", "equip"}); !animation.empty()) {
            return animation;
        }
        break;
    case PreferredModelAnimationContext::hold:
        if (auto animation = find_first_animation(mdl, {"default", "on", "impact"}); !animation.empty()) {
            return animation;
        }
        break;
    }

    if (!mdl.model.animations.empty() && mdl.model.animations.front()) {
        return mdl.model.animations.front()->name;
    }
    return {};
}

PreviewSceneLoadOptions default_preview_scene_load_options()
{
    return {};
}

Bounds estimate_particle_bounds(const nw::render::ParticleEffectDef& effect)
{
    float radius = 1.0f;
    for (const auto& emitter : effect.emitters) {
        const float region_x = 0.5f * emitter.region.size.x * 0.01f;
        const float region_y = 0.5f * emitter.region.size.y * 0.01f;
        const float size = std::max({
            emitter.initial.size_x.min,
            emitter.initial.size_x.max,
            emitter.initial.size_y.min,
            emitter.initial.size_y.max,
        });
        const float speed = std::max(emitter.initial.speed.min, emitter.initial.speed.max);
        const float lifetime = std::max(emitter.initial.lifetime.min, emitter.initial.lifetime.max);
        const float path = std::max({
            emitter.targeting.kill_radius,
            emitter.targeting.source_tangent,
            emitter.targeting.target_tangent,
        });
        radius = std::max(radius, region_x + region_y + size + speed * lifetime + path);
    }

    return Bounds{
        .min = {-radius, -radius, -radius},
        .max = {radius, radius, radius},
    };
}

static Bounds transform_bounds(const Bounds& bounds, const glm::mat4& transform)
{
    const std::array<glm::vec3, 8> corners{
        glm::vec3{bounds.min.x, bounds.min.y, bounds.min.z},
        glm::vec3{bounds.max.x, bounds.min.y, bounds.min.z},
        glm::vec3{bounds.min.x, bounds.max.y, bounds.min.z},
        glm::vec3{bounds.max.x, bounds.max.y, bounds.min.z},
        glm::vec3{bounds.min.x, bounds.min.y, bounds.max.z},
        glm::vec3{bounds.max.x, bounds.min.y, bounds.max.z},
        glm::vec3{bounds.min.x, bounds.max.y, bounds.max.z},
        glm::vec3{bounds.max.x, bounds.max.y, bounds.max.z},
    };

    Bounds result{
        .min = glm::vec3{std::numeric_limits<float>::max()},
        .max = glm::vec3{std::numeric_limits<float>::lowest()},
    };
    for (const auto& corner : corners) {
        const glm::vec3 transformed = glm::vec3(transform * glm::vec4(corner, 1.0f));
        result.min = glm::min(result.min, transformed);
        result.max = glm::max(result.max, transformed);
    }
    return result;
}

std::optional<Bounds> live_particle_bounds(const std::vector<SceneParticleSystem>& particles)
{
    Bounds result{};
    bool first = true;
    for (const auto& scene_particles : particles) {
        const auto& core = scene_particles.system.particles.core;
        for (size_t i = 0; i < core.position.size(); ++i) {
            const glm::vec3 half_extent{
                std::max(core.size_x[i], 0.001f),
                std::max(core.size_y[i], 0.001f),
                std::max(std::max(core.size_x[i], core.size_y[i]), 0.001f),
            };
            const glm::vec3 min = core.position[i] - half_extent;
            const glm::vec3 max = core.position[i] + half_extent;
            if (first) {
                result = Bounds{.min = min, .max = max};
                first = false;
            } else {
                result.min = glm::min(result.min, min);
                result.max = glm::max(result.max, max);
            }
        }
    }

    if (first) {
        return std::nullopt;
    }
    return result;
}

const nw::render::ModelInstance* scene_particle_owner_instance(
    const PreviewScene& scene,
    const SceneParticleSystem& scene_particles) noexcept
{
    if (!scene_particles.owner_instance_handle.valid()) {
        return nullptr;
    }
    const auto* instance = scene.model_instances.get(scene_particles.owner_instance_handle);
    if (!instance || instance->kind != scene_particles.owner_kind) {
        return nullptr;
    }
    if (scene_particles.owner_kind == nw::render::ModelInstanceKind::nwn_legacy
        && instance->nwn_legacy_model_index != scene_particles.owner_model_index) {
        return nullptr;
    }
    if (scene_particles.owner_kind == nw::render::ModelInstanceKind::render_model
        && instance->render_model_index != scene_particles.owner_model_index) {
        return nullptr;
    }
    return instance;
}

const nw::render::ModelInstance* scene_particle_attachment_owner_instance(
    const PreviewScene& scene,
    const SceneParticleSystem& scene_particles,
    const nw::render::ParticleEmitterAttachmentBinding& binding) noexcept
{
    if (!binding.owner_instance_handle.valid()) {
        return nullptr;
    }
    const auto* instance = scene.model_instances.get(binding.owner_instance_handle);
    if (!instance || instance->kind != scene_particles.owner_kind) {
        return nullptr;
    }
    if (scene_particles.owner_kind == nw::render::ModelInstanceKind::nwn_legacy
        && instance->nwn_legacy_model_index != binding.owner_model_index) {
        return nullptr;
    }
    if (scene_particles.owner_kind == nw::render::ModelInstanceKind::render_model
        && instance->render_model_index != binding.owner_model_index) {
        return nullptr;
    }
    return instance;
}

bool scene_particle_owner_visible(const PreviewScene& scene, const SceneParticleSystem& scene_particles) noexcept
{
    if (scene_particles.owner_instance_handle.valid()) {
        const auto* instance = scene_particle_owner_instance(scene, scene_particles);
        return instance && instance->visible;
    }
    return scene_particles.owner ? scene_particles.owner->render_enabled : true;
}

glm::mat4 scene_particle_owner_root(const PreviewScene& scene, const SceneParticleSystem& scene_particles)
{
    if (const auto* instance = scene_particle_owner_instance(scene, scene_particles)) {
        return instance->root_transform;
    }
    return scene_particles.owner ? scene_particles.owner->root_transform() : glm::mat4{1.0f};
}

glm::mat4 scene_particle_attachment_owner_root(
    const PreviewScene& scene,
    const SceneParticleSystem& scene_particles,
    const nw::render::ParticleEmitterAttachmentBinding* binding)
{
    if (binding) {
        if (const auto* instance = scene_particle_attachment_owner_instance(scene, scene_particles, *binding)) {
            return instance->root_transform;
        }
    }
    return scene_particle_owner_root(scene, scene_particles);
}

bool scene_particle_common_attachment_world_transform(
    const PreviewScene& scene,
    const SceneParticleSystem& scene_particles,
    const nw::render::ParticleEmitterAttachmentBinding* binding,
    nw::render::ModelAttachmentPointIndex attachment_point,
    glm::mat4& out_transform) noexcept
{
    if (attachment_point == nw::render::kInvalidModelAttachmentPointIndex) {
        return false;
    }

    const auto* instance = binding
        ? scene_particle_attachment_owner_instance(scene, scene_particles, *binding)
        : scene_particle_owner_instance(scene, scene_particles);
    if (!instance
        || attachment_point >= instance->attachment_node_world_transforms.size()
        || attachment_point >= instance->attachment_node_transform_valid.size()
        || instance->attachment_node_transform_valid[attachment_point] == 0u) {
        return false;
    }

    out_transform = instance->attachment_node_world_transforms[attachment_point];
    return true;
}

const nw::render::nwn::Node* scene_particle_owner_source_node(
    const SceneParticleSystem& scene_particles, uint32_t source_node_index) noexcept
{
    if (!scene_particles.owner || source_node_index == nw::model::kInvalidParticleImportNodeIndex
        || source_node_index >= scene_particles.owner->source_nodes_.size()) {
        return nullptr;
    }
    return scene_particles.owner->source_nodes_[source_node_index];
}

const nw::render::nwn::Node* scene_particle_owner_node(
    const SceneParticleSystem& scene_particles,
    uint32_t source_node_index,
    std::string_view fallback_name) noexcept
{
    if (const auto* node = scene_particle_owner_source_node(scene_particles, source_node_index)) {
        return node;
    }
    if (!scene_particles.owner || fallback_name.empty()) {
        return nullptr;
    }
    return scene_particles.owner->find(fallback_name);
}

const nw::render::ParticleEmitterAttachmentBinding* scene_particle_emitter_attachment(
    const SceneParticleSystem& scene_particles,
    size_t init_index) noexcept
{
    if (init_index >= scene_particles.system.emitter_attachments.size()) {
        return nullptr;
    }
    const auto& binding = scene_particles.system.emitter_attachments[init_index];
    if (binding.emitter == nw::render::kInvalidModelNodeIndex
        || binding.emitter >= scene_particles.system.emitters.size()) {
        return nullptr;
    }
    return &binding;
}

const nw::render::ParticleEmitterAttachmentFrame* scene_particle_emitter_attachment_frame(
    const SceneParticleSystem& scene_particles,
    size_t init_index) noexcept
{
    if (init_index >= scene_particles.system.emitter_attachment_frames.size()) {
        return nullptr;
    }
    const auto& frame = scene_particles.system.emitter_attachment_frames[init_index];
    if (frame.emitter == nw::render::kInvalidModelNodeIndex
        || frame.emitter >= scene_particles.system.emitters.size()) {
        return nullptr;
    }
    return &frame;
}

void build_scene_particle_emitter_attachments(SceneParticleSystem& scene_particles)
{
    scene_particles.system.emitter_attachments.clear();
    const size_t count = std::min(scene_particles.compiled.effect.emitters.size(), scene_particles.system.emitters.size());
    scene_particles.system.emitter_attachments.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const auto& attachment = scene_particles.compiled.effect.emitters[i].attachment;
        if (i >= scene_particles.system.emitters.size() || i >= nw::render::kInvalidModelNodeIndex) {
            scene_particles.system.emitter_attachments.push_back(nw::render::ParticleEmitterAttachmentBinding{});
            continue;
        }

        scene_particles.system.emitter_attachments.push_back(nw::render::ParticleEmitterAttachmentBinding{
            .emitter = static_cast<uint32_t>(i),
            .owner_instance_handle = scene_particles.owner_instance_handle,
            .owner_model_index = scene_particles.owner_model_index,
            .emitter_attachment_point = attachment.emitter_attachment_point,
            .emitter_source_node_index = attachment.emitter_source_node_index,
            .target_attachment_point = attachment.target_attachment_point,
            .target_source_node_index = attachment.target_source_node_index,
        });
    }
}

void resolve_scene_particle_emitter_attachment_frames(const PreviewScene& scene, SceneParticleSystem& scene_particles)
{
    auto& frames = scene_particles.system.emitter_attachment_frames;
    frames.clear();
    const size_t count = std::min({
        scene_particles.compiled.effect.emitters.size(),
        scene_particles.system.emitters.size(),
    });
    frames.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const auto& attachment = scene_particles.compiled.effect.emitters[i].attachment;
        const auto* binding = scene_particle_emitter_attachment(scene_particles, i);
        const uint32_t emitter_index = binding ? binding->emitter : static_cast<uint32_t>(i);
        if (emitter_index >= scene_particles.system.emitters.size()) {
            frames.push_back(nw::render::ParticleEmitterAttachmentFrame{});
            continue;
        }

        const glm::mat4 emitter_root = scene_particle_attachment_owner_root(scene, scene_particles, binding);
        const nw::render::ModelAttachmentPointIndex emitter_attachment_point = binding
            ? binding->emitter_attachment_point
            : attachment.emitter_attachment_point;
        const uint32_t emitter_source_node_index = binding
            ? binding->emitter_source_node_index
            : attachment.emitter_source_node_index;
        const nw::render::ModelAttachmentPointIndex target_attachment_point = binding
            ? binding->target_attachment_point
            : attachment.target_attachment_point;
        const uint32_t target_source_node_index = binding
            ? binding->target_source_node_index
            : attachment.target_source_node_index;

        auto& frame = frames.emplace_back(nw::render::ParticleEmitterAttachmentFrame{
            .emitter = emitter_index,
            .owner_root_transform = emitter_root,
        });

        glm::mat4 common_emitter_transform{1.0f};
        if (scene_particle_common_attachment_world_transform(
                scene, scene_particles, binding, emitter_attachment_point, common_emitter_transform)) {
            frame.emitter_world_transform = common_emitter_transform;
            frame.has_emitter_world_transform = true;
        } else if (const auto* animated_emitter = scene_particle_owner_node(
                       scene_particles, emitter_source_node_index, attachment.emitter_node_name)) {
            frame.emitter_world_transform = emitter_root * animated_emitter->get_transform();
            frame.has_emitter_world_transform = true;
        }

        if (target_attachment_point != nw::render::kInvalidModelAttachmentPointIndex
            || target_source_node_index != nw::model::kInvalidParticleImportNodeIndex
            || !attachment.target_node_name.empty()) {
            glm::mat4 common_target_transform{1.0f};
            if (scene_particle_common_attachment_world_transform(
                    scene, scene_particles, binding, target_attachment_point, common_target_transform)) {
                frame.target_point = glm::vec3(common_target_transform[3]);
                frame.has_target_point = true;
            } else if (const auto* animated_target = scene_particle_owner_node(
                           scene_particles, target_source_node_index, attachment.target_node_name)) {
                frame.target_point = glm::vec3(emitter_root * animated_target->get_transform()[3]);
                frame.has_target_point = true;
            } else if (attachment.has_default_target_offset) {
                frame.target_point = glm::vec3(emitter_root * glm::vec4(attachment.default_target_offset, 1.0f));
                frame.has_target_point = true;
            }
        } else if (attachment.has_default_target_offset) {
            frame.target_point = glm::vec3(emitter_root * glm::vec4(attachment.default_target_offset, 1.0f));
            frame.has_target_point = true;
        }
    }
}

void sync_scene_particle_emitters(
    const PreviewScene& scene,
    SceneParticleSystem& scene_particles,
    bool reset_previous_positions = false)
{
    if (!scene_particles.owner && !scene_particles.owner_instance_handle.valid()) {
        return;
    }

    resolve_scene_particle_emitter_attachment_frames(scene, scene_particles);
    const size_t count = std::min(
        scene_particles.compiled.effect.emitters.size(),
        scene_particles.system.emitter_attachment_frames.size());
    for (size_t i = 0; i < count; ++i) {
        const auto& attachment = scene_particles.compiled.effect.emitters[i].attachment;
        const auto* frame = scene_particle_emitter_attachment_frame(scene_particles, i);
        if (!frame) {
            continue;
        }
        auto& emitter = scene_particles.system.emitters[frame->emitter];

        const glm::vec3 previous_position = glm::vec3(emitter.world_transform[3]);
        if (attachment.has_default_transform) {
            if (frame->has_emitter_world_transform) {
                emitter.world_transform = frame->emitter_world_transform;
            } else {
                emitter.world_transform = frame->owner_root_transform * attachment.default_transform;
            }
        } else {
            if (frame->has_emitter_world_transform) {
                emitter.world_transform = frame->emitter_world_transform;
            } else {
                emitter.world_transform = frame->owner_root_transform;
                if (attachment.has_default_position) {
                    emitter.world_transform *= glm::translate(glm::mat4{1.0f}, attachment.default_position);
                }
                if (attachment.has_default_orientation) {
                    emitter.world_transform *= glm::mat4_cast(attachment.default_orientation);
                }
            }
        }
        if (reset_previous_positions) {
            emitter.prev_world_pos = glm::vec3(emitter.world_transform[3]);
        } else if (previous_position == glm::vec3{0.0f} && emitter.prev_world_pos == glm::vec3{0.0f}) {
            emitter.prev_world_pos = glm::vec3(emitter.world_transform[3]);
        }
        if (frame->has_target_point) {
            emitter.target_point = frame->target_point;
        }
    }
}

nw::model::ParticleImportResult model_asset_particle_import(
    const nw::render::ModelAssetParticleSystem& particles)
{
    nw::model::ParticleImportResult import{};
    import.effect = particles.effect;
    if (import.effect.name.empty()) {
        import.effect.name = particles.name;
    }
    import.effect_events.reserve(particles.effect_events.size());
    for (const auto& event : particles.effect_events) {
        import.effect_events.push_back({
            .time = event.time,
            .burst_count = event.burst_count,
        });
    }
    return import;
}

std::string_view render_model_selected_particle_animation_name(
    const nw::render::RenderModel& model,
    const nw::render::ModelInstance* instance) noexcept
{
    if (!instance || model.animations.empty()) {
        return {};
    }

    const uint32_t clip = instance->animation.clip % static_cast<uint32_t>(model.animations.size());
    return model.animations[clip].name;
}

bool render_model_particle_system_selected(
    const nw::render::RenderModel& model,
    const nw::render::ModelAssetParticleSystem& particles,
    std::string_view selected_animation_name) noexcept
{
    if (particles.animation_name.empty()) {
        return true;
    }
    if (!selected_animation_name.empty()) {
        return particles.animation_name == selected_animation_name;
    }

    // No animation selector exists for this asset, so keep the old inclusive
    // behavior instead of silently dropping authored particle payloads.
    return model.animations.empty();
}

void append_render_model_particle_systems(
    PreviewScene& scene,
    const nw::render::RenderModel& model,
    nw::render::ModelInstanceHandle owner_handle,
    uint32_t owner_model_index,
    std::string_view selected_animation_name)
{
    for (const auto& particle_system : model.particle_systems) {
        if (particle_system.effect.emitters.empty()
            || !render_model_particle_system_selected(model, particle_system, selected_animation_name)) {
            continue;
        }
        scene.particles.push_back(SceneParticleSystem{
            .owner = nullptr,
            .owner_kind = nw::render::ModelInstanceKind::render_model,
            .owner_model_index = owner_model_index,
            .owner_instance_handle = owner_handle,
            .import = model_asset_particle_import(particle_system),
            .compiled = {},
            .system = {},
            .collision = nullptr,
            .particle_animation_length = particle_system.animation_length,
        });

        auto& scene_particles = scene.particles.back();
        scene_particles.compiled = nw::render::compile_particle_effect(scene_particles.import.effect);
        scene_particles.system = nw::render::create_particle_system(scene_particles.compiled.effect);
        scene_particles.system.effect = &scene_particles.compiled.effect;
        nw::render::apply_particle_attachment_defaults(scene_particles.system);
        build_scene_particle_emitter_attachments(scene_particles);
        scene_particles.owner_visible_last = scene_particle_owner_visible(scene, scene_particles);
        if (!scene_particles.owner_visible_last) {
            for (auto& emitter : scene_particles.system.emitters) {
                emitter.active = false;
            }
        }
        sync_scene_particle_emitters(scene, scene_particles, true);
        nw::render::build_particle_render_packets(scene_particles.system);
    }
}

bool PreviewScene::load_animation(std::string_view name)
{
    bool had_animatable_model = false;
    bool loaded = false;
    for (auto& model : models) {
        if (!model->scene_animation_enabled) {
            continue;
        }
        had_animatable_model = true;
        bool model_loaded = model->load_animation(name);
        LOG_F(INFO, "scene animation {} on {}: {}",
            name,
            model->mdl_ ? model->mdl_->model.name : "<unknown>",
            model_loaded ? "yes" : "no");
        loaded = model_loaded || loaded;
    }
    if (had_animatable_model) {
        rebuild_particles(loaded ? name : std::string_view{});
    }
    return loaded;
}

bool PreviewScene::load_default_animations(PreferredModelAnimationContext context)
{
    bool had_animatable_model = false;
    bool loaded = false;
    for (auto& model : models) {
        if (!model || !model->scene_animation_enabled) {
            continue;
        }

        had_animatable_model = true;
        const auto* preference_source = model->mdl_ ? model->mdl_ : model->animation_source_;
        if (!preference_source) {
            continue;
        }

        const auto animation = preferred_model_animation_name(*preference_source, context);
        if (animation.empty()) {
            continue;
        }

        const bool model_loaded = model->load_animation(animation);
        LOG_F(INFO, "scene default animation {} on {}: {}",
            animation,
            model->mdl_ ? model->mdl_->model.name : "<unknown>",
            model_loaded ? "yes" : "no");
        loaded = model_loaded || loaded;
    }

    if (had_animatable_model) {
        rebuild_particles({});
    }
    return loaded;
}

void PreviewScene::rebuild_load_report(std::string_view source, std::string_view kind)
{
    const std::string source_text{source};
    const std::string kind_text{kind};
    load_report = {};
    load_report.source = source_text;
    load_report.kind = kind_text;

    PreviewLoadReportBuilder builder{load_report};
    auto add_model_name = [this](std::string_view model_name) {
        if (model_name.empty()) {
            return;
        }
        if (std::find(load_report.model_names.begin(), load_report.model_names.end(), model_name)
            == load_report.model_names.end()) {
            load_report.model_names.emplace_back(model_name);
        }
    };

    std::set<std::string> scanned_models;
    for (const auto& model : models) {
        if (!model || !model->mdl_) {
            continue;
        }
        const std::string model_name = model->mdl_->model.name.c_str();
        add_model_name(model_name);
        scan_preview_model_dependencies(builder, *model->mdl_, scanned_models);
    }
    for (size_t model_index = 0; model_index < static_models.size(); ++model_index) {
        const auto& model = static_models[model_index];
        if (model) {
            add_model_name(model->name);
            add_render_model_material_report(
                load_report, model->name, *model, static_model_instance(model_index), material_overrides);
            add_render_model_geometry_report(load_report, model->name, *model);
        }
    }

    for (const auto& scene_particles : particles) {
        load_report.particles.push_back({
            .owner = preview_particle_owner_name(scene_particles),
            .emitter_count = scene_particles.import.effect.emitters.size(),
            .max_particles_total = scene_particles.compiled.effect.max_particles_total,
            .import_warning_count = scene_particles.import.warnings.size(),
            .compile_warning_count = scene_particles.compiled.warnings.size(),
            .effect_event_count = scene_particles.import.effect_events.size(),
        });
        for (const auto& warning : scene_particles.import.warnings) {
            builder.add_event(PreviewLoadEventSeverity::warning,
                "particle_import",
                fmt::format("{}: {}.{}: {}",
                    preview_particle_owner_name(scene_particles),
                    warning.emitter,
                    warning.field,
                    warning.message));
        }
        for (const auto& warning : scene_particles.compiled.warnings) {
            builder.add_event(PreviewLoadEventSeverity::warning,
                "particle_compile",
                fmt::format("{}: emitter {}: {}",
                    preview_particle_owner_name(scene_particles),
                    warning.emitter,
                    warning.message));
        }
    }

    for (const auto& resource : load_report.resources) {
        if (resource.status != PreviewLoadResourceStatus::missing) {
            continue;
        }
        builder.add_event(PreviewLoadEventSeverity::warning,
            "resource",
            fmt::format("Missing resource '{}'", resource.resource.filename()));
    }
    load_report.events.insert(load_report.events.end(), source_load_events.begin(), source_load_events.end());
}

void PreviewScene::rebuild_particles(std::string_view animation_name)
{
    sync_model_instance_runtime_state(*this);

    particles.clear();
    size_t render_model_particle_count = 0;
    for (const auto& model : static_models) {
        if (model) {
            render_model_particle_count += model->particle_systems.size();
        }
    }
    particles.reserve(models.size() + render_model_particle_count);

    for (size_t model_index = 0; model_index < models.size(); ++model_index) {
        const auto& model = models[model_index];
        if (!model || !model->mdl_) {
            continue;
        }

        auto particle_animation = model->scene_animation_enabled ? animation_name : std::string_view{};
        if (particle_animation.empty() && model->anim_) {
            particle_animation = model->anim_->name;
        }
        if (particle_animation.empty()) {
            particle_animation = preferred_model_animation_name(
                *model->mdl_, PreferredModelAnimationContext::particle_preview);
        }
        auto import = nw::model::import_particle_effect(*model->mdl_, particle_animation, false);
        if (import.effect.emitters.empty()) {
            continue;
        }

        const bool has_dynamic_attachment_anchor = model->transform_context_ != nullptr;
        glm::vec3 aabb_min{0.0f}, aabb_max{0.0f};
        auto collision = has_dynamic_attachment_anchor
            ? nullptr
            : build_mesh_collision(*model, aabb_min, aabb_max);
        particles.push_back(SceneParticleSystem{
            .owner = model.get(),
            .owner_model_index = static_cast<uint32_t>(
                std::min<size_t>(model_index, std::numeric_limits<uint32_t>::max())),
            .owner_instance_handle = model_index < model_instance_handles.size()
                ? model_instance_handles[model_index]
                : nw::render::ModelInstanceHandle{},
            .import = std::move(import),
            .compiled = {},
            .system = {},
            .collision = std::move(collision),
            .mesh_aabb_min = aabb_min,
            .mesh_aabb_max = aabb_max,
            .particle_animation_length = local_model_animation_length(*model->mdl_, particle_animation),
        });
    }

    for (auto& scene_particles : particles) {
        scene_particles.compiled = nw::render::compile_particle_effect(scene_particles.import.effect);
        scene_particles.system = nw::render::create_particle_system(scene_particles.compiled.effect);
        scene_particles.system.effect = &scene_particles.compiled.effect;
        nw::render::apply_particle_attachment_defaults(scene_particles.system);
        build_scene_particle_emitter_attachments(scene_particles);
        if (scene_particles.owner && scene_particles.owner->anim_) {
            scene_particles.animation_time = static_cast<float>(scene_particles.owner->anim_cursor_) * 0.001f;
            scene_particles.animation_time_initialized = scene_particles.animation_time > 1.0e-6f;
        }
        scene_particles.owner_visible_last = scene_particle_owner_visible(*this, scene_particles);
        if (!scene_particles.owner_visible_last) {
            for (auto& emitter : scene_particles.system.emitters) {
                emitter.active = false;
            }
        }
        sync_scene_particle_emitters(*this, scene_particles, true);
        nw::render::build_particle_render_packets(scene_particles.system);
    }

    for (size_t model_index = 0; model_index < static_models.size(); ++model_index) {
        const auto& model = static_models[model_index];
        if (!model || model_index > std::numeric_limits<uint32_t>::max()
            || model_index >= static_model_instance_handles.size()) {
            continue;
        }
        append_render_model_particle_systems(
            *this,
            *model,
            static_model_instance_handles[model_index],
            static_cast<uint32_t>(model_index),
            animation_name.empty()
                ? render_model_selected_particle_animation_name(*model, static_model_instance(model_index))
                : animation_name);
    }

    if (!load_report.source.empty() || !load_report.kind.empty()) {
        rebuild_load_report(load_report.source, load_report.kind);
    }
}

void PreviewScene::update(int32_t dt_ms)
{
    for (auto& model : models) {
        if (!model->scene_animation_enabled) {
            continue;
        }
        model->update(dt_ms);
    }
    sync_model_instance_runtime_state(*this);
    refresh_scene_dynamic_local_light_render_data(*this);

    const float dt = std::max(0.0f, static_cast<float>(dt_ms) * 0.001f);
    for (auto& scene_particles : particles) {
        const bool owner_visible = scene_particle_owner_visible(*this, scene_particles);
        if (owner_visible && !scene_particles.owner_visible_last) {
            for (auto& emitter : scene_particles.system.emitters) {
                emitter.active = true;
            }
            sync_scene_particle_emitters(*this, scene_particles, true);
        }
        float current_time = scene_particles.animation_time;
        bool has_animation_time = false;
        float animation_length = 0.0f;
        if (owner_visible && scene_particles.owner && scene_particles.owner->anim_) {
            current_time = static_cast<float>(scene_particles.owner->anim_cursor_) * 0.001f;
            has_animation_time = true;
            animation_length = scene_particles.owner->anim_->length;
        } else if (owner_visible && scene_particles.particle_animation_length > 0.0f) {
            current_time = std::fmod(scene_particles.animation_time + dt, scene_particles.particle_animation_length);
            if (current_time < 0.0f) {
                current_time += scene_particles.particle_animation_length;
            }
            has_animation_time = true;
            animation_length = scene_particles.particle_animation_length;
        }
        if (has_animation_time) {
            nw::model::apply_particle_import_events(scene_particles.import,
                scene_particles.system,
                scene_particles.animation_time,
                current_time,
                animation_length,
                !scene_particles.animation_time_initialized);
            scene_particles.animation_time = current_time;
            scene_particles.animation_time_initialized = true;
            for (auto& emitter : scene_particles.system.emitters) {
                emitter.time = current_time;
            }
        }
        if (owner_visible) {
            sync_scene_particle_emitters(*this, scene_particles);
        }
        std::vector<bool> hidden_emitter_active;
        if (!owner_visible) {
            hidden_emitter_active.reserve(scene_particles.system.emitters.size());
            for (auto& emitter : scene_particles.system.emitters) {
                hidden_emitter_active.push_back(emitter.active);
                emitter.active = false;
            }
        }
        if (scene_particles.collision) {
            nw::render::ParticleSimulationContext ctx;
            ctx.collision = scene_particles.collision.get();
            nw::render::tick_particle_system(scene_particles.system, dt, ctx);

            // Kill particles that escape the model mesh bounds — the scene geometry
            // would occlude them in NWN but we have no walls/floor/ceiling to do that.
            constexpr float kMargin = 0.5f;
            const float z_min = scene_particles.mesh_aabb_min.z - kMargin;
            const float z_max = scene_particles.mesh_aabb_max.z + kMargin;
            auto& core = scene_particles.system.particles.core;
            size_t i = 0;
            while (i < core.position.size()) {
                const float z = core.position[i].z;
                if (z < z_min || z > z_max) {
                    nw::render::kill_particle(scene_particles.system, i);
                    continue;
                }
                ++i;
            }
        } else {
            nw::render::tick_particle_system(scene_particles.system, dt);
        }
        if (!owner_visible) {
            for (size_t i = 0; i < hidden_emitter_active.size() && i < scene_particles.system.emitters.size(); ++i) {
                scene_particles.system.emitters[i].active = hidden_emitter_active[i];
            }
        }
        scene_particles.owner_visible_last = owner_visible;
        nw::render::build_particle_render_packets(scene_particles.system);
    }
}

void PreviewScene::set_particle_target_point(
    nw::render::ModelInstanceHandle owner_handle, uint32_t owner_model_index, const glm::vec3& target_point)
{
    for (auto& scene_particles : particles) {
        if (scene_particles.owner_instance_handle != owner_handle
            || scene_particles.owner_model_index != owner_model_index) {
            continue;
        }
        for (auto& emitter : scene_particles.system.emitters) {
            emitter.target_point = target_point;
        }
    }
}

void PreviewScene::set_particle_target_point(const ModelInstance* owner, const glm::vec3& target_point)
{
    for (size_t i = 0; i < models.size() && i < model_instance_handles.size(); ++i) {
        if (models[i].get() == owner) {
            const uint32_t owner_model_index = i >= std::numeric_limits<uint32_t>::max()
                ? std::numeric_limits<uint32_t>::max()
                : static_cast<uint32_t>(i);
            set_particle_target_point(model_instance_handles[i], owner_model_index, target_point);
            return;
        }
    }

    for (auto& scene_particles : particles) {
        if (scene_particles.owner != owner) {
            continue;
        }
        for (auto& emitter : scene_particles.system.emitters) {
            emitter.target_point = target_point;
        }
    }
}

Bounds PreviewScene::current_bounds() const
{
    if (models.empty() && static_models.empty()) {
        if (auto particle_bounds = live_particle_bounds(particles)) {
            return *particle_bounds;
        }
    }

    Bounds result{};
    bool first = true;
    for (size_t model_index = 0; model_index < models.size(); ++model_index) {
        if (!models[model_index]) continue;
        const auto* instance = nwn_model_instance(model_index);
        if (!instance) continue;
        auto current = instance->current_bounds;
        if (first) {
            result = current;
            first = false;
        } else {
            result.min = glm::min(result.min, current.min);
            result.max = glm::max(result.max, current.max);
        }
    }
    for (size_t model_index = 0; model_index < static_models.size(); ++model_index) {
        const auto& model = static_models[model_index];
        if (!model) continue;
        auto current = model->bounds;
        if (model_index < static_model_instance_handles.size()) {
            if (const auto* instance = model_instances.get(static_model_instance_handles[model_index])) {
                current = instance->current_bounds;
            }
        }
        if (first) {
            result = current;
            first = false;
        } else {
            result.min = glm::min(result.min, current.min);
            result.max = glm::max(result.max, current.max);
        }
    }
    return first ? bounds : result;
}

static bool model_instance_contains_water(const ModelInstance& model) noexcept
{
    return model.has_water_pass();
}

static bool render_model_contains_water(const nw::render::RenderModel& model) noexcept
{
    for (const auto& material : model.materials) {
        if (material.alpha_mode == nw::render::MaterialMode::water) {
            return true;
        }
    }
    return false;
}

static nw::render::ModelInstanceShadowSummary render_model_shadow_summary(const nw::render::RenderModel& model) noexcept
{
    const auto summary = model.shadow.valid
        ? model.shadow
        : nw::render::summarize_render_model_shadows(model);

    nw::render::ModelInstanceShadowSummary result{};
    result.bounds = summary.bounds;
    result.caster_count = summary.caster_count;
    result.casts_shadow = summary.casts_shadow;
    return result;
}

static nw::render::ModelInstanceShadowSummary render_model_shadow_summary(
    const nw::render::RenderModel& model,
    const Bounds& world_bounds) noexcept
{
    auto result = render_model_shadow_summary(model);
    result.bounds = world_bounds;
    return result;
}

static nw::render::ModelInstanceShadowSummary nwn_model_shadow_summary(
    const ModelInstance& model,
    const Bounds& world_bounds) noexcept
{
    nw::render::ModelInstanceShadowSummary result{};
    result.bounds = world_bounds;
    result.casts_shadow = model.has_shadow_casters();
    result.caster_count = static_cast<uint32_t>(
        std::min<size_t>(model.shadow_casters().size(), std::numeric_limits<uint32_t>::max()));
    return result;
}

static nw::render::ModelInstanceShadowSummary nwn_model_shadow_summary(const ModelInstance& model) noexcept
{
    return nwn_model_shadow_summary(model, model.current_bounds());
}

static uint32_t scene_nwn_model_index_for_pointer(const PreviewScene& scene, const ModelInstance* model) noexcept
{
    if (!model) {
        return std::numeric_limits<uint32_t>::max();
    }
    for (size_t i = 0; i < scene.models.size() && i < scene.model_instance_handles.size(); ++i) {
        if (scene.models[i].get() == model) {
            return static_cast<uint32_t>(std::min<size_t>(i, std::numeric_limits<uint32_t>::max()));
        }
    }
    return std::numeric_limits<uint32_t>::max();
}

static const SceneModelAttachmentBinding* scene_model_attachment_binding_for_child(
    const PreviewScene& scene,
    nw::render::ModelInstanceKind child_kind,
    uint32_t child_model_index) noexcept
{
    const auto& binding_indices = child_kind == nw::render::ModelInstanceKind::render_model
        ? scene.static_model_attachment_binding_indices
        : scene.model_attachment_binding_indices;
    if (child_model_index >= binding_indices.size()) {
        return nullptr;
    }

    const uint32_t binding_index = binding_indices[child_model_index];
    if (binding_index == kInvalidSceneModelAttachmentBindingIndex
        || binding_index >= scene.model_attachments.size()) {
        return nullptr;
    }

    const auto& binding = scene.model_attachments[binding_index];
    if (binding.child_kind == child_kind && binding.child_model_index == child_model_index) {
        return &binding;
    }
    return nullptr;
}

static std::optional<glm::mat4> scene_model_attachment_root_transform(
    const PreviewScene& scene,
    const ModelInstance& model,
    const SceneModelAttachmentBinding& binding)
{
    if (binding.owner_kind != nw::render::ModelInstanceKind::nwn_legacy
        || binding.child_kind != nw::render::ModelInstanceKind::nwn_legacy
        || binding.owner_model_index >= scene.models.size()
        || binding.owner_model_index >= scene.model_instance_handles.size()) {
        return std::nullopt;
    }
    const auto* owner_instance = scene.model_instances.get(binding.owner_instance_handle);
    if (!owner_instance || owner_instance->kind != nw::render::ModelInstanceKind::nwn_legacy
        || owner_instance->nwn_legacy_model_index != binding.owner_model_index) {
        return std::nullopt;
    }

    const auto& owner_model = scene.models[binding.owner_model_index];
    if (!owner_model) {
        return std::nullopt;
    }
    const auto* anchor = owner_model->socket_node(binding.owner_socket_index);
    if (!anchor) {
        return std::nullopt;
    }

    const bool has_local_scale = model.local_scale_ != 1.0f;
    const glm::mat4 local_scale = has_local_scale
        ? glm::scale(glm::mat4(1.0f), glm::vec3(model.local_scale_))
        : glm::mat4(1.0f);
    glm::mat4 anchor_transform = owner_instance->root_transform * model.placement_transform_;

    if (model.anchor_position_only) {
        glm::vec3 anchor_world = glm::vec3(owner_instance->root_transform * anchor->get_transform()[3]);
        glm::mat3 root_basis{owner_instance->root_transform};
        for (int i = 0; i < 3; ++i) {
            const float length = glm::length(root_basis[i]);
            if (length > 0.0f) {
                root_basis[i] /= length;
            }
        }
        const glm::quat root_rotation = glm::normalize(glm::quat_cast(root_basis));
        anchor_transform = glm::translate(glm::mat4(1.0f), anchor_world)
            * glm::mat4_cast(root_rotation)
            * model.placement_transform_;
    } else {
        anchor_transform = anchor_transform * anchor->get_transform();
    }

    if (has_local_scale) {
        anchor_transform = anchor_transform * local_scale;
    }

    if (model.anchor_uses_root_bind_offset && !model.nodes_.empty()) {
        if (const auto* local_anchor = model.socket_node(binding.child_source_socket_index)) {
            anchor_transform = anchor_transform * glm::inverse(local_anchor->bind_pose_);
        } else {
            const auto bind_translation = glm::vec3(model.nodes_.front()->bind_pose_[3]);
            anchor_transform = anchor_transform * glm::translate(glm::mat4(1.0f), -bind_translation);
        }
    }

    return anchor_transform;
}

static std::optional<glm::mat4> scene_render_model_attachment_root_transform(
    const PreviewScene& scene,
    const nw::render::RenderModel& child_model,
    const SceneModelAttachmentBinding& binding)
{
    if (binding.owner_kind != nw::render::ModelInstanceKind::render_model
        || binding.child_kind != nw::render::ModelInstanceKind::render_model
        || binding.owner_model_index >= scene.static_models.size()
        || binding.owner_model_index >= scene.static_model_instance_handles.size()) {
        return std::nullopt;
    }

    const auto* owner_instance = scene.model_instances.get(binding.owner_instance_handle);
    if (!owner_instance || owner_instance->kind != nw::render::ModelInstanceKind::render_model
        || owner_instance->render_model_index != binding.owner_model_index) {
        return std::nullopt;
    }

    const auto& owner_model = scene.static_models[binding.owner_model_index];
    if (!owner_model) {
        return std::nullopt;
    }

    glm::mat4 root_transform{1.0f};
    if (!nw::render::build_render_model_attachment_root_transform(
            nw::render::RenderModelAttachmentRootTransformInput{
                .owner_instance = owner_instance,
                .owner_model = owner_model.get(),
                .child_model = &child_model,
                .owner_socket_index = binding.owner_socket_index,
                .child_source_socket_index = binding.child_source_socket_index,
            },
            root_transform)) {
        return std::nullopt;
    }

    if (binding.child_local_scale != 1.0f) {
        root_transform *= glm::scale(glm::mat4{1.0f}, glm::vec3{binding.child_local_scale});
    }

    return root_transform;
}

static void sync_common_nwn_attachment_node_transforms(
    nw::render::ModelInstance& instance,
    const ModelInstance& model)
{
    const size_t count = model.source_nodes_.size();
    instance.attachment_node_world_transforms.resize(count);
    instance.attachment_node_transform_valid.assign(count, 0u);
    for (size_t i = 0; i < count; ++i) {
        const auto* node = model.source_nodes_[i];
        if (!node) {
            continue;
        }
        instance.attachment_node_world_transforms[i] = instance.root_transform * node->get_transform();
        instance.attachment_node_transform_valid[i] = 1u;
    }
}

static nw::render::ModelInstance make_common_nwn_model_instance(const ModelInstance& model, size_t model_index)
{
    nw::render::ModelInstance result{};
    result.kind = nw::render::ModelInstanceKind::nwn_legacy;
    result.visible = model.render_enabled;
    result.root_transform = model.root_transform();
    result.current_bounds = model.current_bounds();
    result.shadow = nwn_model_shadow_summary(model);
    result.nwn_legacy_model_index = static_cast<uint32_t>(
        std::min<size_t>(model_index, std::numeric_limits<uint32_t>::max()));
    sync_common_nwn_attachment_node_transforms(result, model);
    return result;
}

static nw::render::ModelInstance make_common_render_model_instance(
    const nw::render::RenderModel& model, size_t model_index)
{
    nw::render::ModelInstance result{};
    result.kind = nw::render::ModelInstanceKind::render_model;
    result.visible = true;
    result.root_transform = glm::mat4{1.0f};
    result.current_bounds = model.bounds;
    result.shadow = render_model_shadow_summary(model, result.current_bounds);
    result.render_model_index = static_cast<uint32_t>(
        std::min<size_t>(model_index, std::numeric_limits<uint32_t>::max()));
    result.animation.enabled = !model.animations.empty() && !model.skeletons.empty();
    nw::render::publish_render_model_static_node_world_transforms(result, model);
    return result;
}

PreviewSceneRuntimeSyncStats sync_model_instance_runtime_state(PreviewScene& scene)
{
    PreviewSceneRuntimeSyncStats stats{};
    for (size_t model_index = 0; model_index < scene.models.size(); ++model_index) {
        const auto& model = scene.models[model_index];
        auto* instance = scene.nwn_model_instance(model_index);
        if (!model || !instance) {
            continue;
        }
        ++stats.nwn_model_count;

        const uint32_t common_model_index = static_cast<uint32_t>(
            std::min<size_t>(model_index, std::numeric_limits<uint32_t>::max()));
        const auto* attachment = scene_model_attachment_binding_for_child(
            scene, nw::render::ModelInstanceKind::nwn_legacy, common_model_index);
        glm::mat4 root_transform{1.0f};
        bool has_root_transform = false;
        if (attachment) {
            ++stats.nwn_attachment_binding_count;
            if (auto attachment_root = scene_model_attachment_root_transform(scene, *model, *attachment)) {
                root_transform = *attachment_root;
                has_root_transform = true;
                ++stats.nwn_attachment_root_resolved_count;
            } else {
                ++stats.nwn_attachment_root_failed_count;
            }
        }
        if (!has_root_transform) {
            root_transform = model->root_transform();
        }

        instance->visible = model->render_enabled;
        instance->root_transform = root_transform;
        instance->current_bounds = model->current_bounds(root_transform);
        instance->shadow = nwn_model_shadow_summary(*model, instance->current_bounds);
        sync_common_nwn_attachment_node_transforms(*instance, *model);
    }

    for (size_t model_index = 0; model_index < scene.static_models.size(); ++model_index) {
        const auto& model = scene.static_models[model_index];
        auto* instance = scene.static_model_instance(model_index);
        if (!model || !instance) {
            continue;
        }
        ++stats.render_model_count;

        const uint32_t common_model_index = static_cast<uint32_t>(
            std::min<size_t>(model_index, std::numeric_limits<uint32_t>::max()));
        const auto* attachment = scene_model_attachment_binding_for_child(
            scene, nw::render::ModelInstanceKind::render_model, common_model_index);
        if (attachment) {
            ++stats.render_model_attachment_binding_count;
            if (auto attachment_root = scene_render_model_attachment_root_transform(scene, *model, *attachment)) {
                instance->root_transform = *attachment_root;
                ++stats.render_model_attachment_root_resolved_count;
            } else {
                ++stats.render_model_attachment_root_failed_count;
            }
        }
        if (!instance->animation.enabled) {
            nw::render::publish_render_model_static_node_world_transforms(*instance, *model);
        }
        instance->current_bounds = transform_bounds(model->bounds, instance->root_transform);
        instance->shadow = render_model_shadow_summary(*model, instance->current_bounds);
    }
    return stats;
}

bool PreviewScene::contains_water() const noexcept
{
    return has_water;
}

nw::render::ModelInstance* PreviewScene::nwn_model_instance(size_t model_index) noexcept
{
    if (model_index >= models.size() || model_index >= model_instance_handles.size()
        || model_index > std::numeric_limits<uint32_t>::max()) {
        return nullptr;
    }

    auto* instance = model_instances.get(model_instance_handles[model_index]);
    if (!instance || instance->kind != nw::render::ModelInstanceKind::nwn_legacy
        || instance->nwn_legacy_model_index != static_cast<uint32_t>(model_index)) {
        return nullptr;
    }
    return instance;
}

const nw::render::ModelInstance* PreviewScene::nwn_model_instance(size_t model_index) const noexcept
{
    if (model_index >= models.size() || model_index >= model_instance_handles.size()
        || model_index > std::numeric_limits<uint32_t>::max()) {
        return nullptr;
    }

    const auto* instance = model_instances.get(model_instance_handles[model_index]);
    if (!instance || instance->kind != nw::render::ModelInstanceKind::nwn_legacy
        || instance->nwn_legacy_model_index != static_cast<uint32_t>(model_index)) {
        return nullptr;
    }
    return instance;
}

nw::render::ModelInstance* PreviewScene::static_model_instance(size_t model_index) noexcept
{
    if (model_index >= static_models.size() || model_index >= static_model_instance_handles.size()
        || model_index > std::numeric_limits<uint32_t>::max()) {
        return nullptr;
    }

    auto* instance = model_instances.get(static_model_instance_handles[model_index]);
    if (!instance || instance->kind != nw::render::ModelInstanceKind::render_model
        || instance->render_model_index != static_cast<uint32_t>(model_index)) {
        return nullptr;
    }
    return instance;
}

const nw::render::ModelInstance* PreviewScene::static_model_instance(size_t model_index) const noexcept
{
    if (model_index >= static_models.size() || model_index >= static_model_instance_handles.size()
        || model_index > std::numeric_limits<uint32_t>::max()) {
        return nullptr;
    }

    const auto* instance = model_instances.get(static_model_instance_handles[model_index]);
    if (!instance || instance->kind != nw::render::ModelInstanceKind::render_model
        || instance->render_model_index != static_cast<uint32_t>(model_index)) {
        return nullptr;
    }
    return instance;
}

void PreviewScene::add(std::unique_ptr<ModelInstance> model)
{
    if (!model) {
        return;
    }
    if (models.empty()) {
        bounds = model->bounds;
    } else {
        bounds.min = glm::min(bounds.min, model->bounds.min);
        bounds.max = glm::max(bounds.max, model->bounds.max);
    }
    vertex_count += model->vertex_count;
    index_count += model->index_count;
    has_water = has_water || model_instance_contains_water(*model);
    const size_t model_index = models.size();
    auto handle = model_instances.create(make_common_nwn_model_instance(*model, model_index));
    model_instance_handles.push_back(handle);
    model_attachment_binding_indices.push_back(kInvalidSceneModelAttachmentBindingIndex);
    const uint32_t owner_model_index = scene_nwn_model_index_for_pointer(*this, model->transform_context_);
    if (owner_model_index != std::numeric_limits<uint32_t>::max()
        && owner_model_index < model_instance_handles.size()
        && model_index <= std::numeric_limits<uint32_t>::max()) {
        const uint32_t binding_index = static_cast<uint32_t>(
            std::min<size_t>(model_attachments.size(), std::numeric_limits<uint32_t>::max()));
        model_attachments.push_back(SceneModelAttachmentBinding{
            .child_model_index = static_cast<uint32_t>(model_index),
            .child_instance_handle = handle,
            .owner_model_index = owner_model_index,
            .owner_instance_handle = model_instance_handles[owner_model_index],
            .owner_socket_index = model->transform_anchor_socket_index_,
            .child_source_socket_index = model->transform_source_anchor_socket_index_,
        });
        model_attachment_binding_indices[model_index] = binding_index;
    }
    models.push_back(std::move(model));
    area_model_info.emplace_back();
}

void PreviewScene::add_attached(std::unique_ptr<ModelInstance> model, uint32_t owner_model_index,
    std::string_view owner_socket, std::string_view child_source_socket)
{
    if (!model) {
        return;
    }

    // Bridge setup path: callers own scene model indices, while the NWN sidecar
    // still stores the pointer/string fallback until attachment evaluation is
    // fully common-runtime. Invalid owner/anchor adds the model unattached.
    if (!owner_socket.empty() && owner_model_index < models.size()) {
        if (const auto* owner = models[owner_model_index].get()) {
            model->set_transform_anchor(owner, owner_socket, child_source_socket);
        }
    }

    add(std::move(model));
}

void PreviewScene::add(std::unique_ptr<nw::render::RenderModel> model)
{
    if (!model) {
        return;
    }
    if (models.empty() && static_models.empty()) {
        bounds = model->bounds;
    } else {
        bounds.min = glm::min(bounds.min, model->bounds.min);
        bounds.max = glm::max(bounds.max, model->bounds.max);
    }
    for (const auto& prim : model->primitives) {
        vertex_count += prim.vertices.valid() ? prim.index_count : 0;
        index_count += prim.index_count;
    }
    has_water = has_water || render_model_contains_water(*model);
    const size_t model_index = static_models.size();
    const auto owner_handle = model_instances.create(make_common_render_model_instance(*model, model_index));
    static_model_instance_handles.push_back(owner_handle);
    static_model_attachment_binding_indices.push_back(kInvalidSceneModelAttachmentBindingIndex);

    if (model_index <= std::numeric_limits<uint32_t>::max()) {
        particles.reserve(particles.size() + model->particle_systems.size());
        const auto* instance = model_instances.get(owner_handle);
        append_render_model_particle_systems(
            *this,
            *model,
            owner_handle,
            static_cast<uint32_t>(model_index),
            render_model_selected_particle_animation_name(*model, instance));
    }
    static_models.push_back(std::move(model));
    static_area_model_info.emplace_back();
}

void PreviewScene::add_attached(std::unique_ptr<nw::render::RenderModel> model, uint32_t owner_model_index,
    std::string_view owner_socket, std::string_view child_source_socket, float child_local_scale)
{
    if (!model) {
        return;
    }

    const size_t child_model_index = static_models.size();
    nw::render::ModelInstanceHandle owner_handle;
    uint32_t owner_socket_index = nw::render::kInvalidModelNodeIndex;
    if (!owner_socket.empty()
        && owner_model_index < static_models.size()
        && owner_model_index < static_model_instance_handles.size()) {
        if (const auto& owner = static_models[owner_model_index]) {
            owner_socket_index = owner->socket_index(owner_socket);
            owner_handle = static_model_instance_handles[owner_model_index];
        }
    }
    const uint32_t child_source_socket_index = model->socket_index(child_source_socket);

    add(std::move(model));

    if (owner_socket_index == nw::render::kInvalidModelNodeIndex
        || !owner_handle.valid()
        || child_model_index > std::numeric_limits<uint32_t>::max()
        || child_model_index >= static_model_instance_handles.size()) {
        return;
    }

    const uint32_t binding_index = static_cast<uint32_t>(
        std::min<size_t>(model_attachments.size(), std::numeric_limits<uint32_t>::max()));
    model_attachments.push_back(SceneModelAttachmentBinding{
        .child_kind = nw::render::ModelInstanceKind::render_model,
        .child_model_index = static_cast<uint32_t>(child_model_index),
        .child_instance_handle = static_model_instance_handles[child_model_index],
        .owner_kind = nw::render::ModelInstanceKind::render_model,
        .owner_model_index = owner_model_index,
        .owner_instance_handle = owner_handle,
        .owner_socket_index = owner_socket_index,
        .child_source_socket_index = child_source_socket_index,
        .child_local_scale = child_local_scale,
    });
    static_model_attachment_binding_indices[child_model_index] = binding_index;
}

void PreviewScene::add_particle_effect(nw::render::ParticleEffectDef effect)
{
    particles.push_back(SceneParticleSystem{
        .owner = nullptr,
        .owner_instance_handle = nw::render::ModelInstanceHandle{},
        .import = {
            .effect = std::move(effect),
            .warnings = {},
            .emitter_inits = {},
            .effect_events = {},
        },
        .compiled = {},
        .system = {},
        .collision = nullptr,
    });

    auto& scene_particles = particles.back();
    scene_particles.compiled = nw::render::compile_particle_effect(scene_particles.import.effect);
    scene_particles.system = nw::render::create_particle_system(scene_particles.compiled.effect);
    scene_particles.system.effect = &scene_particles.compiled.effect;
    build_scene_particle_emitter_attachments(scene_particles);
    for (size_t i = 0; i < scene_particles.compiled.effect.emitters.size(); ++i) {
        const auto& emitter = scene_particles.compiled.effect.emitters[i];
        if (emitter.emission.mode == nw::render::ParticleEmissionMode::event_burst
            || emitter.emission.trigger_on_effect_events) {
            // Standalone particle JSON has no authored effect-event timeline, so
            // synthesize one initial pulse to make trigger-driven effects previewable.
            nw::render::trigger_particle_emitter(scene_particles.system, static_cast<uint16_t>(i));
        }
    }
    nw::render::build_particle_render_packets(scene_particles.system);

    const auto effect_bounds = estimate_particle_bounds(scene_particles.import.effect);
    if (models.empty() && static_models.empty() && particles.size() == 1) {
        bounds = effect_bounds;
    } else {
        bounds.min = glm::min(bounds.min, effect_bounds.min);
        bounds.max = glm::max(bounds.max, effect_bounds.max);
    }
}

int32_t compute_particle_prime_ms(const PreviewScene& scene, bool explicit_animation)
{
    auto first_effect_event_ms = [](const auto& events) -> std::optional<int32_t> {
        if (events.empty()) {
            return std::nullopt;
        }
        return std::max(0, static_cast<int32_t>(std::ceil(events.front().time * 1000.0f)));
    };

    auto first_positive_track_key = [](const auto& keys) -> const nw::render::ParticleCurveKeyF32* {
        for (const auto& key : keys) {
            if (key.value > 0.0f) {
                return &key;
            }
        }
        return nullptr;
    };

    if (explicit_animation) {
        int32_t prime_ms = 16;
        for (const auto& scene_particles : scene.particles) {
            const auto effect_event_ms = first_effect_event_ms(scene_particles.import.effect_events);
            const auto count = std::min(scene_particles.compiled.effect.emitters.size(), scene_particles.import.effect.emitters.size());
            for (size_t i = 0; i < count; ++i) {
                const auto& emitter = scene_particles.compiled.effect.emitters[i];
                const auto& authored = scene_particles.import.effect.emitters[i];
                int32_t activation_ms = 0;

                if (emitter.emission.mode == nw::render::ParticleEmissionMode::event_burst
                    || emitter.emission.trigger_on_effect_events) {
                    if (effect_event_ms) {
                        activation_ms = *effect_event_ms;
                    }
                } else if (const auto* active_key = first_positive_track_key(emitter.emission_rate_track.keys)) {
                    activation_ms = std::max(0, static_cast<int32_t>(std::ceil(active_key->time * 1000.0f)));
                }

                float visible_rate = emitter.emission.rate;
                if (const auto* active_key = first_positive_track_key(emitter.emission_rate_track.keys)) {
                    visible_rate = active_key->value;
                }

                int32_t settle_ms = 33;
                if (emitter.emission.mode == nw::render::ParticleEmissionMode::continuous
                    && emitter.emission.metric == nw::render::ParticleSpawnMetric::per_second
                    && visible_rate > 0.0f) {
                    const int32_t interval_ms = static_cast<int32_t>(std::ceil(1000.0f / visible_rate));
                    const float lifetime = std::max(authored.initial.lifetime.min, authored.initial.lifetime.max);
                    const int32_t lifetime_ms = static_cast<int32_t>(std::ceil(std::max(0.0f, lifetime) * 1000.0f * 0.25f));
                    settle_ms = std::max({150, interval_ms + 16, lifetime_ms});
                }

                prime_ms = std::max(prime_ms, activation_ms + settle_ms);
            }
        }
        return std::min(prime_ms, 3000);
    }

    int32_t prime_ms = 250;
    bool has_fade_in_continuous = false;
    for (const auto& scene_particles : scene.particles) {
        const auto count = std::min(scene_particles.compiled.effect.emitters.size(), scene_particles.import.effect.emitters.size());
        for (size_t i = 0; i < count; ++i) {
            const auto& emitter = scene_particles.compiled.effect.emitters[i];
            const auto& authored = scene_particles.import.effect.emitters[i];
            if (emitter.emission.mode != nw::render::ParticleEmissionMode::continuous) {
                continue;
            }
            if (emitter.emission.metric != nw::render::ParticleSpawnMetric::per_second) {
                continue;
            }

            float rate = emitter.emission.rate;
            if (!emitter.emission_rate_track.keys.empty()) {
                rate = emitter.emission_rate_track.keys.front().value;
            }
            if (rate <= 0.0f) {
                continue;
            }

            const int32_t interval_ms = static_cast<int32_t>(std::ceil(1000.0f / rate));
            const float lifetime = std::max(authored.initial.lifetime.min, authored.initial.lifetime.max);
            bool fades_in_over_life = false;
            if (!authored.over_life.alpha.keys.empty()) {
                const float alpha_start = authored.over_life.alpha.keys.front().value;
                const float alpha_end = authored.over_life.alpha.keys.back().value;
                fades_in_over_life = alpha_end > alpha_start + 1.0e-4f;
            }
            has_fade_in_continuous = has_fade_in_continuous || fades_in_over_life;
            const float settle_lifetime = fades_in_over_life ? lifetime * 0.75f : lifetime * 0.5f;
            const int32_t visible_settle_ms = static_cast<int32_t>(std::ceil(std::max(0.0f, settle_lifetime) * 1000.0f));
            prime_ms = std::max(prime_ms, std::max(interval_ms + 16, visible_settle_ms));
        }
    }

    return std::min(prime_ms, has_fade_in_continuous ? 3000 : 1500);
}

static uint16_t resolve_body_part_value(uint16_t value, uint16_t mirror_value)
{
    if (value == 255) {
        return mirror_value != 0 && mirror_value != 255 ? mirror_value : 0;
    }
    return value;
}

static uint16_t resolve_armor_part_value(uint16_t value, uint16_t mirror_value)
{
    if (value == 0 || value == 255) {
        return mirror_value != 0 && mirror_value != 255 ? mirror_value : value;
    }
    return value;
}

static bool body_part_tattoo_model_uses_tattoo_palette(std::string_view token)
{
    return token == "bicepl"
        || token == "bicepr"
        || token == "chest"
        || token == "forel"
        || token == "forer"
        || token == "legl"
        || token == "legr"
        || token == "shinl"
        || token == "shinr";
}

static uint16_t resolve_armor_skin_tattoo_model_part(std::string_view token, uint16_t creature_model_part,
    uint16_t armor_model_part)
{
    constexpr uint16_t skin_model_part = 1;
    constexpr uint16_t tattoo_model_part = 2;

    if (armor_model_part == skin_model_part
        && creature_model_part == tattoo_model_part
        && body_part_tattoo_model_uses_tattoo_palette(token)) {
        return tattoo_model_part;
    }
    return armor_model_part;
}

static nw::BodyParts normalized_body_parts(nw::BodyParts body_parts)
{
    body_parts.bicep_left = resolve_body_part_value(body_parts.bicep_left, body_parts.bicep_right);
    body_parts.bicep_right = resolve_body_part_value(body_parts.bicep_right, body_parts.bicep_left);
    body_parts.forearm_left = resolve_body_part_value(body_parts.forearm_left, body_parts.forearm_right);
    body_parts.forearm_right = resolve_body_part_value(body_parts.forearm_right, body_parts.forearm_left);
    body_parts.hand_left = resolve_body_part_value(body_parts.hand_left, body_parts.hand_right);
    body_parts.hand_right = resolve_body_part_value(body_parts.hand_right, body_parts.hand_left);
    body_parts.foot_left = resolve_body_part_value(body_parts.foot_left, body_parts.foot_right);
    body_parts.foot_right = resolve_body_part_value(body_parts.foot_right, body_parts.foot_left);
    body_parts.shin_left = resolve_body_part_value(body_parts.shin_left, body_parts.shin_right);
    body_parts.shin_right = resolve_body_part_value(body_parts.shin_right, body_parts.shin_left);
    body_parts.thigh_left = resolve_body_part_value(body_parts.thigh_left, body_parts.thigh_right);
    body_parts.thigh_right = resolve_body_part_value(body_parts.thigh_right, body_parts.thigh_left);
    return body_parts;
}

static nw::Item* equipped_item(const nw::Equips& equips, nw::EquipIndex slot)
{
    auto idx = static_cast<size_t>(slot);
    if (idx >= equips.equips.size()) {
        return nullptr;
    }
    const auto& equip = equips.equips[idx];
    return nw::equip_item_ptr(equip);
}

static void resolve_preview_equipment(nw::Creature& creature, const std::filesystem::path& path)
{
    const auto path_text = path.string();
    ERRARE("[viewer] resolving dynamic creature equipment for '{}'", std::string_view{path_text});

    std::array<nw::Resref, 18> requested{};
    for (size_t i = 0; i < creature.equipment.equips.size(); ++i) {
        const auto& equip = creature.equipment.equips[i];
        if (equip.is<nw::Resref>() && equip.as<nw::Resref>().length()) {
            requested[i] = equip.as<nw::Resref>();
        }
    }

    if (!creature.equipment.instantiate()) {
        LOG_F(WARNING, "Dynamic creature preview '{}': failed to instantiate equipment", path.string());
        log_preview_warning_context();
    }

    for (size_t i = 0; i < requested.size(); ++i) {
        if (!requested[i].length()) {
            continue;
        }
        const auto slot = static_cast<nw::EquipIndex>(i);
        if (!equipped_item(creature.equipment, slot)) {
            ERRARE("[viewer] resolving equipment slot '{}' requested '{}.uti'",
                nw::equip_index_to_string(slot),
                requested[i].view());
            LOG_F(WARNING, "Dynamic creature preview '{}': missing equipped item '{}.uti' for slot '{}'",
                path.string(),
                requested[i],
                nw::equip_index_to_string(slot));
            log_preview_warning_context();
        }
    }
}

static std::optional<nw::ItemModelParts::type> armor_item_part_for_token(std::string_view token)
{
    using Parts = nw::ItemModelParts;
    if (token == "belt") {
        return Parts::armor_belt;
    }
    if (token == "bicepl") {
        return Parts::armor_lbicep;
    }
    if (token == "bicepr") {
        return Parts::armor_rbicep;
    }
    if (token == "chest") {
        return Parts::armor_torso;
    }
    if (token == "forel") {
        return Parts::armor_lfarm;
    }
    if (token == "forer") {
        return Parts::armor_rfarm;
    }
    if (token == "footl") {
        return Parts::armor_lfoot;
    }
    if (token == "footr") {
        return Parts::armor_rfoot;
    }
    if (token == "handl") {
        return Parts::armor_lhand;
    }
    if (token == "handr") {
        return Parts::armor_rhand;
    }
    if (token == "neck") {
        return Parts::armor_neck;
    }
    if (token == "pelvis") {
        return Parts::armor_pelvis;
    }
    if (token == "shinl") {
        return Parts::armor_lshin;
    }
    if (token == "shinr") {
        return Parts::armor_rshin;
    }
    if (token == "shol") {
        return Parts::armor_lshoul;
    }
    if (token == "shor") {
        return Parts::armor_rshoul;
    }
    if (token == "legl") {
        return Parts::armor_lthigh;
    }
    if (token == "legr") {
        return Parts::armor_rthigh;
    }
    return std::nullopt;
}

static std::string anchor_name_for_equipped_item(nw::EquipIndex slot)
{
    switch (slot) {
    case nw::EquipIndex::head:
        return "head_g";
    case nw::EquipIndex::righthand:
        return "rhand";
    case nw::EquipIndex::lefthand:
        return "lhand";
    default:
        return {};
    }
}

static std::string anchor_name_for_attachment(std::string_view table_name)
{
    if (table_name == "wingmodel") {
        return "wings";
    }
    if (table_name == "tailmodel") {
        return "tail";
    }
    return {};
}

static std::string source_anchor_name_for_attachment(std::string_view table_name)
{
    if (table_name == "wingmodel") {
        return "wings";
    }
    if (table_name == "tailmodel") {
        return "tail";
    }
    return {};
}

static float appearance_wing_tail_scale(const nw::StaticTwoDA* appearance_tda, nw::Appearance appearance_id)
{
    if (!appearance_tda || appearance_id == nw::Appearance::invalid()) {
        return 1.0f;
    }

    float scale = 1.0f;
    appearance_tda->get_to(*appearance_id, "WING_TAIL_SCALE", scale, false);
    return scale;
}

static float appearance_helmet_scale(const nw::StaticTwoDA* appearance_tda, nw::Appearance appearance_id, uint8_t gender)
{
    if (!appearance_tda || appearance_id == nw::Appearance::invalid()) {
        return 1.0f;
    }

    float scale = 1.0f;
    const auto* column = gender == 1 ? "HELMET_SCALE_F" : "HELMET_SCALE_M";
    appearance_tda->get_to(*appearance_id, column, scale, false);
    return scale;
}

NwnAppearanceHandItemVisualPolicy resolve_nwn_appearance_hand_item_visual_policy(
    const nw::StaticTwoDA* appearance_tda,
    nw::Appearance appearance_id)
{
    NwnAppearanceHandItemVisualPolicy result;
    if (!appearance_tda
        || appearance_id == nw::Appearance::invalid()
        || appearance_id.idx() >= appearance_tda->rows()) {
        return result;
    }

    int32_t has_arms = 1;
    if (appearance_tda->get_to(*appearance_id, "HASARMS", has_arms, false) && has_arms == 0) {
        result.visible = false;
        result.reason = NwnAppearanceHandItemVisualPolicyReason::hidden_no_arms;
        return result;
    }

    const size_t weapon_scale_column = appearance_tda->column_index("WEAPONSCALE");
    if (weapon_scale_column == nw::StaticTwoDA::npos) {
        return result;
    }

    nw::StringView raw_scale;
    if (!appearance_tda->get_to(*appearance_id, weapon_scale_column, raw_scale)) {
        result.visible = false;
        result.reason = NwnAppearanceHandItemVisualPolicyReason::hidden_null_weapon_scale;
        return result;
    }

    const auto scale = nw::string::from<float>(raw_scale);
    if (!scale || !std::isfinite(*scale) || *scale <= 0.0f) {
        result.visible = false;
        result.reason = NwnAppearanceHandItemVisualPolicyReason::hidden_invalid_weapon_scale;
        return result;
    }

    result.scale = *scale;
    return result;
}

NwnWingAttachmentVisualPolicy resolve_nwn_wing_attachment_visual_policy(
    nw::Appearance appearance_id,
    uint32_t wing_row)
{
    constexpr uint32_t any_appearance_row = std::numeric_limits<uint32_t>::max();
    struct PolicyRow {
        uint32_t appearance_row;
        uint32_t wing_row;
        NwnWingAttachmentVisualPolicyReason reason;
    };

    constexpr std::array policy_rows = {
        PolicyRow{
            .appearance_row = any_appearance_row,
            .wing_row = 1u,
            .reason = NwnWingAttachmentVisualPolicyReason::strip_non_render_meshes,
        },
    };

    if (wing_row == 0) {
        return {};
    }

    const uint32_t appearance_row = appearance_id == nw::Appearance::invalid()
        ? any_appearance_row
        : static_cast<uint32_t>(appearance_id.idx());
    for (const auto& row : policy_rows) {
        if (row.wing_row != wing_row) {
            continue;
        }
        if (row.appearance_row != any_appearance_row && row.appearance_row != appearance_row) {
            continue;
        }
        return {
            .strip_non_render_meshes = row.reason == NwnWingAttachmentVisualPolicyReason::strip_non_render_meshes,
            .reason = row.reason,
        };
    }
    return {};
}

size_t apply_nwn_wing_attachment_visual_policy(
    nw::render::nwn::ModelInstance& model,
    NwnWingAttachmentVisualPolicy policy)
{
    if (!policy.strip_non_render_meshes) {
        return 0;
    }

    size_t stripped_mesh_count = 0;
    for (const auto& node : model.nodes_) {
        if (!node || !node->orig_ || !node->is_mesh || node->is_skin) {
            continue;
        }
        const auto* source_mesh = dynamic_cast<const nw::model::TrimeshNode*>(node->orig_);
        if (!source_mesh || source_mesh->render) {
            continue;
        }
        if (auto* mesh = dynamic_cast<nw::render::nwn::Mesh*>(node.get())) {
            mesh->vertices = {};
            mesh->indices = {};
            mesh->index_count = 0;
            ++stripped_mesh_count;
        }
    }
    return stripped_mesh_count;
}

size_t count_nwn_wing_attachment_visual_policy_stripped_meshes(
    const nw::model::Mdl& mdl,
    NwnWingAttachmentVisualPolicy policy)
{
    if (!policy.strip_non_render_meshes) {
        return 0;
    }

    size_t stripped_mesh_count = 0;
    for (const auto& node : mdl.model.nodes) {
        if (!node || !(node->type & nw::model::NodeFlags::mesh) || (node->type & nw::model::NodeFlags::skin)) {
            continue;
        }
        const auto* source_mesh = dynamic_cast<const nw::model::TrimeshNode*>(node.get());
        if (source_mesh && !source_mesh->render) {
            ++stripped_mesh_count;
        }
    }
    return stripped_mesh_count;
}

static std::string_view robe_hide_column(std::string_view token)
{
    if (token == "belt") {
        return "HIDEBELT";
    }
    if (token == "bicepl") {
        return "HIDEBICEPL";
    }
    if (token == "bicepr") {
        return "HIDEBICEPR";
    }
    if (token == "footl") {
        return "HIDEFOOTL";
    }
    if (token == "footr") {
        return "HIDEFOOTR";
    }
    if (token == "forel") {
        return "HIDEFOREL";
    }
    if (token == "forer") {
        return "HIDEFORER";
    }
    if (token == "handl") {
        return "HIDEHANDL";
    }
    if (token == "handr") {
        return "HIDEHANDR";
    }
    if (token == "head") {
        return "HIDEHEAD";
    }
    if (token == "neck") {
        return "HIDENECK";
    }
    if (token == "pelvis") {
        return "HIDEPELVIS";
    }
    if (token == "shinl") {
        return "HIDESHINL";
    }
    if (token == "shinr") {
        return "HIDESHINR";
    }
    if (token == "shol") {
        return "HIDESHOL";
    }
    if (token == "shor") {
        return "HIDESHOR";
    }
    if (token == "legl") {
        return "HIDELEGL";
    }
    if (token == "legr") {
        return "HIDELEGR";
    }
    if (token == "chest") {
        return "HIDECHEST";
    }
    return {};
}

static bool robe_hides_body_part(uint16_t robe_part, std::string_view token)
{
    auto column = robe_hide_column(token);
    if (column.empty()) {
        return false;
    }

    auto* tda = nw::kernel::twodas().get("parts_robe");
    if (!tda || robe_part >= tda->rows()) {
        return false;
    }

    int hidden = 0;
    return tda->get_to(static_cast<size_t>(robe_part), column, hidden, false) && hidden != 0;
}

static std::optional<std::string> resolve_creature_part_model(char sex, std::string_view race, int phenotype,
    std::initializer_list<std::string_view> part_tokens, uint16_t part)
{
    if (part == 0 || part == 255 || race.empty()) {
        return std::nullopt;
    }

    for (auto token : part_tokens) {
        auto resref = fmt::format("p{}{}{}_{}{:03d}", sex, race, phenotype, token, part);
        if (nw::kernel::resman().contains({resref, nw::ResourceType::mdl})) {
            return resref;
        }
    }
    return std::nullopt;
}

static std::optional<std::string> resolve_creature_cloak_model(char sex, std::string_view race, int phenotype, uint16_t part)
{
    return resolve_creature_part_model(sex, race, phenotype, {"cloak_", "cloak"}, part);
}

static std::optional<std::string_view> mirrored_part_token(std::string_view token)
{
    if (token == "bicepl") {
        return "bicepr";
    }
    if (token == "bicepr") {
        return "bicepl";
    }
    if (token == "footl") {
        return "footr";
    }
    if (token == "footr") {
        return "footl";
    }
    if (token == "forel") {
        return "forer";
    }
    if (token == "forer") {
        return "forel";
    }
    if (token == "handl") {
        return "handr";
    }
    if (token == "handr") {
        return "handl";
    }
    if (token == "shinl") {
        return "shinr";
    }
    if (token == "shinr") {
        return "shinl";
    }
    if (token == "shol") {
        return "shor";
    }
    if (token == "shor") {
        return "shol";
    }
    if (token == "legl") {
        return "legr";
    }
    if (token == "legr") {
        return "legl";
    }
    return std::nullopt;
}

static bool prefers_symmetric_mirrored_armor_model(std::string_view token)
{
    return token == "legr";
}

static std::optional<std::string> resolve_creature_base_rig(const nw::AppearanceInfo& appearance, std::string_view race, char sex)
{
    nw::String label = appearance.label;
    nw::string::tolower(&label);
    for (auto& ch : label) {
        if (ch == ' ' || ch == '-') {
            ch = '_';
        }
    }

    const std::array<std::string, 3> candidates = {
        fmt::format("p{}{}0", sex, race),
        fmt::format("a_{}", label.c_str()),
        fmt::format("a_{}a", sex),
    };

    for (const auto& candidate : candidates) {
        if (nw::kernel::resman().contains({nw::Resref{candidate}, nw::ResourceType::mdl})) {
            return candidate;
        }
    }
    return std::nullopt;
}

static std::string anchor_name_for_part(std::string_view token)
{
    if (token == "belt") {
        return "belt_g";
    }
    if (token == "bicepl") {
        return "lbicep_g";
    }
    if (token == "bicepr") {
        return "rbicep_g";
    }
    if (token == "chest") {
        return "torso_g";
    }
    if (token == "forel") {
        return "lforearm_g";
    }
    if (token == "forer") {
        return "rforearm_g";
    }
    if (token == "footl") {
        return "lfoot_g";
    }
    if (token == "footr") {
        return "rfoot_g";
    }
    if (token == "handl") {
        return "lhand_g";
    }
    if (token == "handr") {
        return "rhand_g";
    }
    if (token == "pelvis") {
        return "pelvis_g";
    }
    if (token == "legl") {
        return "lthigh_g";
    }
    if (token == "legr") {
        return "rthigh_g";
    }
    if (token == "neck") {
        return "neck_g";
    }
    if (token == "head") {
        return "head_g";
    }
    if (token == "shinl") {
        return "lshin_g";
    }
    if (token == "shinr") {
        return "rshin_g";
    }
    if (token == "shol") {
        return "lshoulder_g";
    }
    if (token == "shor") {
        return "rshoulder_g";
    }
    return {};
}

static void maybe_add_model(PreviewScene& scene, std::unique_ptr<ModelInstance> model)
{
    if (model) {
        scene.add(std::move(model));
    }
}

static size_t append_scene_authored_model_lights(PreviewScene& scene);

static void make_static_scene_attachment(ModelInstance& model)
{
    model.scene_animation_enabled = false;
    model.accepts_external_animation_source = false;
}

struct AreaSharedMeshBuffers {
    nw::gfx::Handle<nw::gfx::Buffer> vertices;
    nw::gfx::Handle<nw::gfx::Buffer> indices;
    uint32_t vertex_count = 0;
    uint32_t index_count = 0;
};

using AreaGeometryCache = std::unordered_map<const nw::model::Node*, AreaSharedMeshBuffers>;

void replace_area_mesh_buffers_with_shared(Mesh& mesh, const AreaSharedMeshBuffers& shared)
{
    if (mesh.owns_gpu_buffers) {
        if (mesh.vertices.valid()) {
            nw::gfx::destroy_buffer(mesh.vertices);
        }
        if (mesh.indices.valid()) {
            nw::gfx::destroy_buffer(mesh.indices);
        }
    }
    mesh.vertices = shared.vertices;
    mesh.indices = shared.indices;
    mesh.vertex_count = shared.vertex_count;
    mesh.index_count = shared.index_count;
    mesh.owns_gpu_buffers = false;
}

void share_static_area_model_geometry(AreaGeometryCache& cache, ModelInstance& model)
{
    for (auto& node : model.nodes_) {
        auto* mesh = dynamic_cast<Mesh*>(node.get());
        if (!mesh || mesh->is_skin || dynamic_cast<nw::render::nwn::DanglyMesh*>(mesh)) {
            continue;
        }
        if (!mesh->orig_ || !mesh->vertices.valid() || !mesh->indices.valid() || mesh->index_count == 0) {
            continue;
        }

        const auto it = cache.find(mesh->orig_);
        if (it == cache.end()) {
            cache.emplace(mesh->orig_, AreaSharedMeshBuffers{
                                           .vertices = mesh->vertices,
                                           .indices = mesh->indices,
                                           .vertex_count = mesh->vertex_count,
                                           .index_count = mesh->index_count,
                                       });
            continue;
        }
        if (mesh->vertex_count == it->second.vertex_count && mesh->index_count == it->second.index_count) {
            replace_area_mesh_buffers_with_shared(*mesh, it->second);
        }
    }
}

static void set_scene_animation_source(PreviewScene& scene, const nw::model::Mdl* source)
{
    if (!source) {
        return;
    }

    for (auto& instance : scene.models) {
        if (!instance->scene_animation_enabled) {
            continue;
        }
        if (!instance->accepts_external_animation_source) {
            continue;
        }
        instance->set_animation_source(source);
    }
}

static void preserve_creature_identity_plt_colors(nw::PltColors& colors, const nw::PltColors& creature_colors)
{
    colors.data[nw::plt_layer_skin] = creature_colors.data[nw::plt_layer_skin];
    colors.data[nw::plt_layer_hair] = creature_colors.data[nw::plt_layer_hair];
    colors.data[nw::plt_layer_tattoo1] = creature_colors.data[nw::plt_layer_tattoo1];
    colors.data[nw::plt_layer_tattoo2] = creature_colors.data[nw::plt_layer_tattoo2];
}

static glm::uvec4 render_model_plt_color_row(const nw::PltColors& colors, size_t begin) noexcept
{
    return glm::uvec4{
        begin + 0u < colors.data.size() ? colors.data[begin + 0u] : 0u,
        begin + 1u < colors.data.size() ? colors.data[begin + 1u] : 0u,
        begin + 2u < colors.data.size() ? colors.data[begin + 2u] : 0u,
        begin + 3u < colors.data.size() ? colors.data[begin + 3u] : 0u,
    };
}

static void set_render_model_material_plt_colors(nw::render::Material& material, const nw::PltColors& colors) noexcept
{
    material.plt_enabled = true;
    material.plt_colors0 = render_model_plt_color_row(colors, 0u);
    material.plt_colors1 = render_model_plt_color_row(colors, 4u);
    material.plt_colors2 = render_model_plt_color_row(colors, 8u);
}

static bool apply_render_model_plt_material_overrides(PreviewScene& scene, uint32_t model_index,
    const nw::PltColors& colors)
{
    if (model_index >= scene.static_models.size()
        || model_index >= scene.static_model_instance_handles.size()
        || !scene.static_models[model_index]) {
        return false;
    }

    auto* instance = scene.model_instances.get(scene.static_model_instance_handles[model_index]);
    if (!instance) {
        return false;
    }

    const auto& model = *scene.static_models[model_index];
    instance->material_override_handles.resize(model.materials.size());

    bool applied = false;
    for (size_t material_index = 0; material_index < model.materials.size(); ++material_index) {
        const auto& material = model.materials[material_index];
        if (!material.albedo_uses_plt
            || material.material_uses_fallback
            || !nw::gfx::bindless_texture_index_valid(material.albedo_index)) {
            continue;
        }

        auto override_material = material;
        set_render_model_material_plt_colors(override_material, colors);
        auto& handle = instance->material_override_handles[material_index];
        if (scene.material_overrides.valid(handle)) {
            scene.material_overrides.destroy(handle);
        }
        handle = scene.material_overrides.create(nw::render::ModelMaterialOverride{
            .material = std::move(override_material),
        });
        applied = true;
    }

    return applied;
}

struct HumanoidBodyPartSpec {
    uint16_t nw::BodyParts::* field;
    std::string_view token;
};

static constexpr std::array<HumanoidBodyPartSpec, 19> kHumanoidBodyPartSpecs{{
    {&nw::BodyParts::belt, "belt"},
    {&nw::BodyParts::bicep_left, "bicepl"},
    {&nw::BodyParts::bicep_right, "bicepr"},
    {&nw::BodyParts::foot_left, "footl"},
    {&nw::BodyParts::foot_right, "footr"},
    {&nw::BodyParts::forearm_left, "forel"},
    {&nw::BodyParts::forearm_right, "forer"},
    {&nw::BodyParts::hand_left, "handl"},
    {&nw::BodyParts::hand_right, "handr"},
    {&nw::BodyParts::head, "head"},
    {&nw::BodyParts::neck, "neck"},
    {&nw::BodyParts::pelvis, "pelvis"},
    {&nw::BodyParts::shin_left, "shinl"},
    {&nw::BodyParts::shin_right, "shinr"},
    {&nw::BodyParts::shoulder_left, "shol"},
    {&nw::BodyParts::shoulder_right, "shor"},
    {&nw::BodyParts::thigh_left, "legl"},
    {&nw::BodyParts::thigh_right, "legr"},
    {&nw::BodyParts::torso, "chest"},
}};

struct ResolvedHumanoidBodyPartModel {
    std::string_view token;
    std::string anchor;
    nw::PltColors colors{};
    std::optional<std::string> resref;
    uint16_t model_part = 0;
    bool missing_requested_part = false;
};

static std::vector<ResolvedHumanoidBodyPartModel> resolve_humanoid_body_part_models(
    char sex,
    std::string_view race,
    int phenotype,
    nw::BodyParts body_parts,
    const nw::PltColors& plt_colors,
    const nw::Item* chest_item,
    const nw::Item* head_item)
{
    std::vector<ResolvedHumanoidBodyPartModel> result;
    result.reserve(kHumanoidBodyPartSpecs.size());

    for (const auto& part : kHumanoidBodyPartSpecs) {
        auto model_part = body_parts.*(part.field);
        const uint16_t creature_model_part = model_part;
        nw::PltColors part_colors = plt_colors;
        bool prefer_mirrored_part_model = false;
        const uint16_t robe_part = chest_item && chest_item->model_type == nw::ItemModelType::armor
            ? static_cast<uint16_t>(chest_item->model_parts[nw::ItemModelParts::armor_robe])
            : 0;

        if (head_item && part.token == "head") {
            continue;
        }
        if (robe_part > 0 && robe_hides_body_part(robe_part, part.token)) {
            continue;
        }

        if (chest_item && chest_item->model_type == nw::ItemModelType::armor) {
            if (auto armor_part = armor_item_part_for_token(part.token)) {
                uint16_t armor_model_part = chest_item->model_parts[*armor_part];
                nw::PltColors armor_part_colors = chest_item->part_to_plt_colors(*armor_part);
                preserve_creature_identity_plt_colors(armor_part_colors, plt_colors);

                if (auto mirror = mirrored_part_token(part.token)) {
                    if (auto mirror_armor_part = armor_item_part_for_token(*mirror)) {
                        const uint16_t raw_armor_model_part = chest_item->model_parts[*armor_part];
                        const uint16_t mirrored_model_part = chest_item->model_parts[*mirror_armor_part];
                        armor_model_part = resolve_armor_part_value(armor_model_part, mirrored_model_part);
                        const bool inherited_from_mirror = (raw_armor_model_part == 0 || raw_armor_model_part == 255)
                            && mirrored_model_part != 0 && mirrored_model_part != 255;
                        const bool symmetric_right_side = prefers_symmetric_mirrored_armor_model(part.token)
                            && armor_model_part != 0 && armor_model_part != 255
                            && armor_model_part == mirrored_model_part;
                        prefer_mirrored_part_model = prefers_symmetric_mirrored_armor_model(part.token)
                            && (inherited_from_mirror || symmetric_right_side);

                        if (inherited_from_mirror) {
                            prefer_mirrored_part_model = true;
                            armor_part_colors = chest_item->part_to_plt_colors(*mirror_armor_part);
                            preserve_creature_identity_plt_colors(armor_part_colors, plt_colors);
                        }
                    }
                }

                armor_model_part = resolve_armor_skin_tattoo_model_part(
                    part.token, creature_model_part, armor_model_part);
                if (armor_model_part > 0 && armor_model_part != 255) {
                    model_part = armor_model_part;
                    part_colors = armor_part_colors;
                }
            }
        }

        auto part_resref = resolve_creature_part_model(sex, race, phenotype, {part.token}, model_part);
        if (auto mirror = mirrored_part_token(part.token)) {
            if (prefer_mirrored_part_model) {
                part_resref = resolve_creature_part_model(sex, race, phenotype, {*mirror, part.token}, model_part);
            } else if (!part_resref) {
                part_resref = resolve_creature_part_model(sex, race, phenotype, {*mirror, part.token}, model_part);
            }
        }

        result.push_back(ResolvedHumanoidBodyPartModel{
            .token = part.token,
            .anchor = anchor_name_for_part(part.token),
            .colors = part_colors,
            .resref = std::move(part_resref),
            .model_part = model_part,
            .missing_requested_part = !part_resref && model_part > 0 && model_part != 255,
        });
    }

    return result;
}

struct ResolvedItemModelPart {
    std::string resref;
    nw::ItemModelParts::type part = nw::ItemModelParts::model1;
};

static std::vector<ResolvedItemModelPart> resolve_item_model_parts(
    const nw::Item& item,
    const nw::BaseItemInfo& baseitem)
{
    std::vector<ResolvedItemModelPart> result;
    result.reserve(3);

    auto add = [&](std::string resref, nw::ItemModelParts::type part) {
        if (!resref.empty()) {
            result.push_back(ResolvedItemModelPart{
                .resref = std::move(resref),
                .part = part,
            });
        }
    };

    switch (item.model_type) {
    case nw::ItemModelType::simple:
    case nw::ItemModelType::layered:
        if (item.model_parts[nw::ItemModelParts::model1] > 0) {
            add(fmt::format("{}_{:03d}", baseitem.item_class.view(), item.model_parts[nw::ItemModelParts::model1]),
                nw::ItemModelParts::model1);
        }
        break;
    case nw::ItemModelType::composite:
        if (item.model_parts[nw::ItemModelParts::model1] > 0) {
            add(fmt::format("{}_b_{:03d}", baseitem.item_class.view(), item.model_parts[nw::ItemModelParts::model1]),
                nw::ItemModelParts::model1);
        }
        if (item.model_parts[nw::ItemModelParts::model2] > 0) {
            add(fmt::format("{}_m_{:03d}", baseitem.item_class.view(), item.model_parts[nw::ItemModelParts::model2]),
                nw::ItemModelParts::model2);
        }
        if (item.model_parts[nw::ItemModelParts::model3] > 0) {
            add(fmt::format("{}_t_{:03d}", baseitem.item_class.view(), item.model_parts[nw::ItemModelParts::model3]),
                nw::ItemModelParts::model3);
        }
        break;
    case nw::ItemModelType::armor:
        break;
    }

    return result;
}

struct CreatureAttachmentModelLookup {
    std::string_view table_name;
    uint32_t row = 0;
    std::string model_name;
    std::string owner_socket;
    std::string source_socket;
    std::string warning;
    bool requested = false;
    bool resolved = false;
    bool null_model = false;
};

static CreatureAttachmentModelLookup resolve_creature_attachment_model_lookup(
    std::string_view table_name,
    uint32_t row)
{
    CreatureAttachmentModelLookup result{
        .table_name = table_name,
        .row = row,
        .model_name = {},
        .owner_socket = anchor_name_for_attachment(table_name),
        .source_socket = source_anchor_name_for_attachment(table_name),
        .warning = {},
        .requested = row != 0,
        .resolved = false,
        .null_model = false,
    };
    if (!result.requested) {
        return result;
    }

    auto* tda = nw::kernel::twodas().get(table_name);
    if (!tda) {
        result.warning = fmt::format("Dynamic creature attachment table '{}' was not loaded", table_name);
        return result;
    }
    if (!tda->get_to(row, "MODEL", result.model_name) || result.model_name.empty()) {
        result.warning = fmt::format("Dynamic creature attachment '{}' row {} has no MODEL", table_name, row);
        return result;
    }
    if (result.model_name == "c_nulltail") {
        result.null_model = true;
        return result;
    }

    result.resolved = true;
    return result;
}

static void add_equipped_item_model(PreviewScene& scene, PreviewRenderResources& resources, const nw::Item& item,
    nw::EquipIndex slot, uint32_t placement_model_index, float local_scale = 1.0f,
    const nw::PltColors* creature_colors = nullptr)
{
    const auto slot_name = nw::equip_index_to_string(slot);
    const auto item_resref = item.common.resref.string();
    ERRARE("[viewer] resolving equipped item '{}.uti' in slot '{}'",
        std::string_view{item_resref},
        slot_name);

    auto anchor = anchor_name_for_equipped_item(slot);
    if (placement_model_index >= scene.models.size() || anchor.empty()) {
        LOG_F(WARNING, "Dynamic creature equipped item '{}' in slot '{}' has no attachment anchor/context",
            item.common.resref,
            slot_name);
        log_preview_warning_context();
        return;
    }

    auto* baseitem = nw::kernel::rules().baseitems.get(item.baseitem);
    if (!baseitem) {
        LOG_F(WARNING, "Dynamic creature equipped item '{}' in slot '{}' has invalid baseitem {}",
            item.common.resref,
            slot_name,
            *item.baseitem);
        log_preview_warning_context();
        return;
    }

    auto try_add = [&](std::string_view resref, nw::ItemModelParts::type part) {
        if (resref.empty()) {
            return;
        }

        ERRARE("[viewer] resolving equipped item model '{}'", resref);
        if (!nw::kernel::resman().contains({nw::Resref{resref}, nw::ResourceType::mdl})) {
            LOG_F(WARNING, "Dynamic creature equipped model '{}' was not found for slot '{}' item '{}'",
                resref,
                slot_name,
                item.common.resref);
            log_preview_warning_context();
            return;
        }

        auto model = load_model_with_plt(resources, resref);
        if (!model) {
            LOG_F(WARNING, "Dynamic creature equipped model '{}' failed to load for slot '{}' item '{}'",
                resref,
                slot_name,
                item.common.resref);
            log_preview_warning_context();
            return;
        }
        if (nw::kernel::resman().contains({nw::Resref{resref}, nw::ResourceType::plt})) {
            auto item_plt = item.part_to_plt_colors(part);
            if (creature_colors) {
                preserve_creature_identity_plt_colors(item_plt, *creature_colors);
            }
            apply_plt_to_model(resources, *model, item_plt);
        }
        model->scene_animation_enabled = false;
        if (slot == nw::EquipIndex::head) {
            model->anchor_uses_root_bind_offset = false;
        }
        model->local_scale_ = local_scale;
        LOG_F(INFO, "dynamic creature equipped {} -> {}",
            nw::equip_index_to_string(slot), resref);
        scene.add_attached(std::move(model), placement_model_index, anchor);
    };

    for (const auto& model_part : resolve_item_model_parts(item, *baseitem)) {
        try_add(model_part.resref, model_part.part);
    }
}

static bool add_render_model_equipped_item_model(PreviewScene& scene, PreviewRenderResources& resources,
    const nw::Item& item, nw::EquipIndex slot, uint32_t owner_model_index, float local_scale,
    std::vector<PreviewLoadEvent>* load_events, const nw::PltColors* creature_colors = nullptr)
{
    const auto slot_name = nw::equip_index_to_string(slot);
    const auto item_resref = item.common.resref.string();
    ERRARE("[viewer] resolving RenderModel equipped item '{}.uti' in slot '{}'",
        std::string_view{item_resref},
        slot_name);

    auto skip_with_warning = [&](std::string message) {
        LOG_F(WARNING, "{}", message);
        log_preview_warning_context();
        if (load_events) {
            add_preview_load_event(*load_events,
                PreviewLoadEventSeverity::warning,
                "nwn_render_model_creature_items",
                std::move(message));
        }
        return false;
    };

    const auto anchor = anchor_name_for_equipped_item(slot);
    if (anchor.empty()
        || owner_model_index >= scene.static_models.size()
        || owner_model_index >= scene.static_model_instance_handles.size()) {
        return skip_with_warning(fmt::format(
            "RenderModel creature equipped item '{}' in slot '{}' has no attachment anchor/context",
            item.common.resref,
            slot_name));
    }

    const auto& owner = scene.static_models[owner_model_index];
    if (!owner || owner->socket_index(anchor) == nw::render::kInvalidModelNodeIndex) {
        return skip_with_warning(fmt::format(
            "RenderModel creature equipped item '{}' in slot '{}' missing owner socket '{}'",
            item.common.resref,
            slot_name,
            anchor));
    }

    auto* baseitem = nw::kernel::rules().baseitems.get(item.baseitem);
    if (!baseitem) {
        return skip_with_warning(fmt::format(
            "RenderModel creature equipped item '{}' in slot '{}' has invalid baseitem {}",
            item.common.resref,
            slot_name,
            *item.baseitem));
    }

    bool resolved_part = false;
    bool added = false;
    for (const auto& model_part : resolve_item_model_parts(item, *baseitem)) {
        if (model_part.resref.empty()) {
            continue;
        }
        resolved_part = true;

        ERRARE("[viewer] resolving RenderModel equipped item model '{}'", std::string_view{model_part.resref});
        if (!nw::kernel::resman().contains({nw::Resref{model_part.resref}, nw::ResourceType::mdl})) {
            skip_with_warning(fmt::format(
                "RenderModel creature equipped model '{}' was not found for slot '{}' item '{}'",
                model_part.resref,
                slot_name,
                item.common.resref));
            continue;
        }

        auto load = load_nwn_render_model_preview(resources, model_part.resref);
        if (load_events) {
            append_preview_load_events(*load_events, load.events);
        }
        if (!load.model) {
            skip_with_warning(fmt::format(
                "RenderModel creature equipped model '{}' failed to load for slot '{}' item '{}'",
                model_part.resref,
                slot_name,
                item.common.resref));
            continue;
        }

        LOG_F(INFO, "dynamic RenderModel creature equipped {} -> {}",
            nw::equip_index_to_string(slot), model_part.resref);
        const uint32_t child_model_index = static_cast<uint32_t>(scene.static_models.size());
        scene.add_attached(std::move(load.model), owner_model_index, anchor, {}, local_scale);
        if (child_model_index < scene.static_models.size()) {
            auto item_plt = item.part_to_plt_colors(model_part.part);
            if (creature_colors) {
                preserve_creature_identity_plt_colors(item_plt, *creature_colors);
            }
            apply_render_model_plt_material_overrides(scene, child_model_index, item_plt);
        }
        added = true;
    }

    if (!added && !resolved_part) {
        return skip_with_warning(fmt::format(
            "RenderModel creature equipped item '{}' in slot '{}' produced no attachable models",
            item.common.resref,
            slot_name));
    }

    return added;
}

static bool add_render_model_creature_attachment(
    PreviewScene& scene,
    PreviewRenderResources& resources,
    std::string_view table_name,
    uint32_t row,
    float local_scale,
    const nw::PltColors& plt_colors,
    std::string_view origin,
    std::vector<PreviewLoadEvent>* load_events)
{
    const auto lookup = resolve_creature_attachment_model_lookup(table_name, row);
    if (!lookup.requested || lookup.null_model) {
        return true;
    }

    auto skip_with_warning = [&](std::string message) {
        LOG_F(WARNING, "{}", message);
        log_preview_warning_context();
        if (load_events) {
            add_preview_load_event(*load_events,
                PreviewLoadEventSeverity::warning,
                "nwn_render_model_creature_addons",
                std::move(message));
        }
        return false;
    };

    if (!lookup.warning.empty()) {
        return skip_with_warning(fmt::format("{}: {}", origin, lookup.warning));
    }
    if (scene.static_models.empty() || !scene.static_models.front()) {
        return skip_with_warning(fmt::format(
            "{}: attachment '{}' row {} has no RenderModel owner", origin, table_name, row));
    }

    if (lookup.owner_socket.empty()
        || scene.static_models.front()->socket_index(lookup.owner_socket) == nw::render::kInvalidModelNodeIndex) {
        return skip_with_warning(fmt::format(
            "{}: attachment '{}' row {} model '{}' missing owner socket '{}'",
            origin,
            table_name,
            row,
            lookup.model_name,
            lookup.owner_socket));
    }

    ERRARE("[viewer] resolving RenderModel creature attachment model '{}'", std::string_view{lookup.model_name});
    auto load = load_nwn_render_model_preview(resources, lookup.model_name);
    if (load_events) {
        append_preview_load_events(*load_events, load.events);
    }
    if (!load.model) {
        return skip_with_warning(fmt::format(
            "{}: attachment '{}' row {} model '{}' failed to load through RenderModel",
            origin,
            table_name,
            row,
            lookup.model_name));
    }
    if (!lookup.source_socket.empty()
        && load.model->socket_index(lookup.source_socket) == nw::render::kInvalidModelNodeIndex) {
        return skip_with_warning(fmt::format(
            "{}: attachment '{}' row {} model '{}' missing child socket '{}'",
            origin,
            table_name,
            row,
            lookup.model_name,
            lookup.source_socket));
    }

    LOG_F(INFO, "dynamic RenderModel creature attachment {} row {} -> {}", table_name, row, lookup.model_name);
    const uint32_t child_model_index = static_cast<uint32_t>(scene.static_models.size());
    scene.add_attached(std::move(load.model), 0u, lookup.owner_socket, lookup.source_socket, local_scale);
    if (child_model_index < scene.static_models.size()) {
        apply_render_model_plt_material_overrides(scene, child_model_index, plt_colors);
    }
    return true;
}

static bool add_dynamic_creature_scene_models(
    PreviewScene& scene,
    PreviewRenderResources& resources,
    nw::Appearance appearance_id,
    uint8_t gender,
    uint32_t wings,
    uint32_t tail,
    nw::BodyParts body_parts,
    const nw::PltColors& plt_colors,
    nw::Item* chest_item,
    nw::Item* cloak_item,
    nw::Item* head_item,
    nw::Item* righthand_item,
    nw::Item* lefthand_item,
    std::string_view origin,
    PreviewSceneLoadOptions options,
    std::vector<PreviewLoadEvent>* load_events)
{
    auto* app = nw::kernel::rules().appearances.get(appearance_id);
    auto* appearance_tda = nw::kernel::twodas().get("appearance");
    if (!app || !appearance_tda) {
        return false;
    }
    const float wing_tail_scale = appearance_wing_tail_scale(appearance_tda, appearance_id);
    const float helmet_scale = appearance_helmet_scale(appearance_tda, appearance_id, gender);
    const auto hand_item_policy = resolve_nwn_appearance_hand_item_visual_policy(appearance_tda, appearance_id);

    std::string race;
    appearance_tda->get_to(*appearance_id, "RACE", race);
    int phenotype = 0;
    auto* phenotype_tda = nw::kernel::twodas().get("phenotype");
    if (phenotype_tda) {
        phenotype = 0;
    }

    const char sex = gender == 1 ? 'f' : 'm';
    body_parts = normalized_body_parts(body_parts);
    std::unique_ptr<ModelInstance> base_rig;
    const bool use_render_model_creature_body = options.nwn_model_path == NwnModelPreviewPath::render_model
        && app->model_type != "P";
    if (app->model_type == "P") {
        if (options.nwn_model_path == NwnModelPreviewPath::render_model) {
            const auto message = fmt::format(
                "{}: RenderModel creature path requested for humanoid appearance {}; using legacy sidecar "
                "body-part/equipment assembly until PLT, socket, and cross-skeleton policy are common data",
                origin,
                appearance_id.idx());
            LOG_F(WARNING, "{}", message);
            log_preview_warning_context();
            if (load_events) {
                add_preview_load_event(*load_events,
                    PreviewLoadEventSeverity::warning,
                    "nwn_render_model_humanoid_fallback",
                    message);
            }
        }

        // Experimental NWN humanoid preview assembly stays isolated here on purpose.
        auto base_rig_resref = resolve_creature_base_rig(*app, race, sex);
        if (base_rig_resref) {
            ERRARE("[viewer] resolving dynamic creature base rig '{}'", std::string_view{*base_rig_resref});
            base_rig = load_model_with_plt(resources, *base_rig_resref, &plt_colors);
        }
        if (base_rig) {
            base_rig->render_enabled = false;
            scene.add(std::move(base_rig));
        } else {
            LOG_F(WARNING, "No dynamic creature base rig resolved for appearance {}", appearance_id.idx());
            log_preview_warning_context();
        }

        const uint32_t placement_model_index = scene.models.empty()
            ? std::numeric_limits<uint32_t>::max()
            : 0u;

        const auto resolved_body_parts = resolve_humanoid_body_part_models(
            sex, race, phenotype, body_parts, plt_colors, chest_item, head_item);
        for (const auto& part : resolved_body_parts) {
            if (part.resref) {
                ERRARE("[viewer] resolving dynamic creature body part '{}' model '{}'",
                    part.token,
                    std::string_view{*part.resref});
                auto model = load_model_with_plt(resources, *part.resref, &part.colors);
                if (!model) {
                    LOG_F(WARNING, "Dynamic creature body part '{}' model '{}' failed to load",
                        part.token,
                        *part.resref);
                    log_preview_warning_context();
                } else if (placement_model_index < scene.models.size() && !part.anchor.empty()) {
                    make_static_scene_attachment(*model);
                    model->anchor_uses_root_bind_offset = false;
                    scene.add_attached(std::move(model), placement_model_index, part.anchor);
                } else {
                    maybe_add_model(scene, std::move(model));
                }
            } else if (part.missing_requested_part) {
                ERRARE("[viewer] resolving dynamic creature body part '{}' model part {}",
                    part.token,
                    part.model_part);
                LOG_F(WARNING, "Dynamic creature body part '{}' model part {} was not found",
                    part.token,
                    part.model_part);
                log_preview_warning_context();
            }
        }

        if (chest_item && chest_item->model_type == nw::ItemModelType::armor) {
            auto robe_part = chest_item->model_parts[nw::ItemModelParts::armor_robe];
            if (robe_part > 0) {
                auto robe_colors = chest_item->part_to_plt_colors(nw::ItemModelParts::armor_robe);
                preserve_creature_identity_plt_colors(robe_colors, plt_colors);
                if (auto robe_resref = resolve_creature_part_model(sex, race, phenotype, {"robe"}, robe_part)) {
                    ERRARE("[viewer] resolving dynamic creature robe model '{}'", std::string_view{*robe_resref});
                    auto robe = load_model_with_plt(resources, *robe_resref, &robe_colors);
                    if (robe) {
                        robe->accepts_external_animation_source = false;
                    }
                    if (!robe) {
                        LOG_F(WARNING, "Dynamic creature robe model '{}' failed to load", *robe_resref);
                        log_preview_warning_context();
                    }
                    maybe_add_model(scene, std::move(robe));
                } else {
                    ERRARE("[viewer] resolving dynamic creature robe model part {}", robe_part);
                    LOG_F(WARNING, "Dynamic creature robe model part {} was not found", robe_part);
                    log_preview_warning_context();
                }
            }
        }
    } else {
        ERRARE("[viewer] resolving creature model '{}'", std::string_view{app->model_name});
        if (use_render_model_creature_body) {
            auto load = load_nwn_render_model_preview(resources, app->model_name);
            if (load_events) {
                append_preview_load_events(*load_events, load.events);
            }
            const uint32_t body_model_index = static_cast<uint32_t>(scene.static_models.size());
            scene.add(std::move(load.model));
            if (body_model_index < scene.static_models.size()) {
                apply_render_model_plt_material_overrides(scene, body_model_index, plt_colors);
            }
        } else {
            auto model = load_model_with_plt(resources, app->model_name, &plt_colors);
            if (!model) {
                LOG_F(WARNING, "Creature model '{}' failed to load", app->model_name);
                log_preview_warning_context();
            }
            maybe_add_model(scene, std::move(model));
        }
    }

    if (use_render_model_creature_body) {
        const bool loaded_wings = add_render_model_creature_attachment(
            scene, resources, "wingmodel", wings, wing_tail_scale, plt_colors, origin, load_events);
        const bool loaded_tail = add_render_model_creature_attachment(
            scene, resources, "tailmodel", tail, wing_tail_scale, plt_colors, origin, load_events);
        const bool loaded_righthand = !hand_item_policy.visible
            || !righthand_item
            || add_render_model_equipped_item_model(
                scene, resources, *righthand_item, nw::EquipIndex::righthand, 0u, hand_item_policy.scale, load_events, &plt_colors);
        const bool loaded_lefthand = !hand_item_policy.visible
            || !lefthand_item
            || add_render_model_equipped_item_model(
                scene, resources, *lefthand_item, nw::EquipIndex::lefthand, 0u, hand_item_policy.scale, load_events, &plt_colors);
        const bool loaded_head = !head_item
            || add_render_model_equipped_item_model(
                scene, resources, *head_item, nw::EquipIndex::head, 0u, helmet_scale, load_events, &plt_colors);
        const bool has_legacy_visual_addons = (wings != 0 && !loaded_wings)
            || (tail != 0 && !loaded_tail)
            || cloak_item
            || !loaded_head
            || !loaded_righthand
            || !loaded_lefthand;
        if (has_legacy_visual_addons) {
            const auto message = fmt::format(
                "{}: RenderModel creature path skipped or failed some remaining legacy cloak/head/hand/add-on data "
                "until socket, PLT, and item policy are common data",
                origin);
            LOG_F(WARNING, "{}", message);
            log_preview_warning_context();
            if (load_events) {
                add_preview_load_event(*load_events,
                    PreviewLoadEventSeverity::warning,
                    "nwn_render_model_creature_addons",
                    message);
            }
        }
        if (scene.static_models.empty()) {
            LOG_F(ERROR, "Dynamic creature preview '{}' produced no renderable models", origin);
            log_preview_error_context();
            return false;
        }
        return true;
    }

    auto add_attachment = [&](const char* table_name, uint32_t row) {
        const auto lookup = resolve_creature_attachment_model_lookup(table_name, row);
        if (!lookup.requested || lookup.null_model) {
            return;
        }
        ERRARE("[viewer] resolving creature attachment '{}' row {}", std::string_view{table_name}, row);
        if (!lookup.warning.empty()) {
            LOG_F(WARNING, "{}", lookup.warning);
            log_preview_warning_context();
            return;
        }
        if (lookup.resolved) {
            ERRARE("[viewer] resolving creature attachment model '{}'", std::string_view{lookup.model_name});
            auto model = load_model_with_plt(resources, lookup.model_name, &plt_colors);
            const uint32_t placement_model_index = scene.models.empty()
                ? std::numeric_limits<uint32_t>::max()
                : 0u;
            if (!model) {
                LOG_F(WARNING, "Dynamic creature attachment '{}' row {} model '{}' failed to load",
                    table_name,
                    row,
                    lookup.model_name);
                log_preview_warning_context();
            } else {
                if (table_name == std::string_view{"wingmodel"}) {
                    const auto policy = resolve_nwn_wing_attachment_visual_policy(appearance_id, row);
                    const size_t stripped_mesh_count = apply_nwn_wing_attachment_visual_policy(*model, policy);
                    if (load_events && policy.strip_non_render_meshes) {
                        add_preview_load_event(*load_events,
                            PreviewLoadEventSeverity::info,
                            "nwn_wing_attachment_policy",
                            fmt::format(
                                "{}: wingmodel row {} stripped_non_render_meshes={}",
                                origin,
                                row,
                                stripped_mesh_count));
                    }
                }
            }
            if (model && placement_model_index < scene.models.size() && !lookup.owner_socket.empty()) {
                make_static_scene_attachment(*model);
                model->local_scale_ = wing_tail_scale;
                model->anchor_uses_root_bind_offset = true;
                scene.add_attached(std::move(model), placement_model_index, lookup.owner_socket, lookup.source_socket);
            } else if (model) {
                maybe_add_model(scene, std::move(model));
            }
        }
    };
    add_attachment("wingmodel", wings);
    add_attachment("tailmodel", tail);

    if (cloak_item && cloak_item->model_type == nw::ItemModelType::layered) {
        ERRARE("[viewer] resolving dynamic creature cloak item '{}.uti'", cloak_item->common.resref.view());
        auto* cloakmodel = nw::kernel::twodas().get("cloakmodel");
        int cloak_model = 0;
        if (!cloakmodel) {
            LOG_F(WARNING, "Dynamic creature cloak model table was not loaded");
            log_preview_warning_context();
        } else if (cloakmodel->get_to(cloak_item->model_parts[nw::ItemModelParts::model1], "MODEL", cloak_model) && cloak_model > 0) {
            auto cloak_colors = cloak_item->part_to_plt_colors(nw::ItemModelParts::model1);
            if (auto cloak_resref = resolve_creature_cloak_model(sex, race, phenotype, static_cast<uint16_t>(cloak_model))) {
                LOG_F(INFO, "dynamic creature cloak -> {}", *cloak_resref);
                ERRARE("[viewer] resolving dynamic creature cloak model '{}'", std::string_view{*cloak_resref});
                auto cloak = load_model_with_plt(resources, *cloak_resref, &cloak_colors);
                if (cloak) {
                    cloak->accepts_external_animation_source = false;
                }
                if (!cloak) {
                    LOG_F(WARNING, "Dynamic creature cloak model '{}' failed to load", *cloak_resref);
                    log_preview_warning_context();
                }
                maybe_add_model(scene, std::move(cloak));
            } else {
                ERRARE("[viewer] resolving dynamic creature cloak model part {}", cloak_model);
                LOG_F(WARNING, "Dynamic creature cloak model part {} was not found", cloak_model);
                log_preview_warning_context();
            }
        } else {
            LOG_F(WARNING, "Dynamic creature cloak item '{}' model part {} has no cloakmodel.2da MODEL",
                cloak_item->common.resref,
                cloak_item->model_parts[nw::ItemModelParts::model1]);
            log_preview_warning_context();
        }
    }

    const uint32_t placement_model_index = scene.models.empty()
        ? std::numeric_limits<uint32_t>::max()
        : 0u;
    if (righthand_item && hand_item_policy.visible) {
        add_equipped_item_model(
            scene, resources, *righthand_item, nw::EquipIndex::righthand, placement_model_index, hand_item_policy.scale, &plt_colors);
    }
    if (lefthand_item && hand_item_policy.visible) {
        add_equipped_item_model(
            scene, resources, *lefthand_item, nw::EquipIndex::lefthand, placement_model_index, hand_item_policy.scale, &plt_colors);
    }
    if (head_item) {
        add_equipped_item_model(scene, resources, *head_item, nw::EquipIndex::head, placement_model_index, helmet_scale, &plt_colors);
    }

    if (scene.models.empty() && scene.static_models.empty()) {
        LOG_F(ERROR, "Dynamic creature preview '{}' produced no renderable models", origin);
        log_preview_error_context();
        return false;
    }

    if (!scene.models.empty()) {
        auto* animation_context = scene.models.front().get();
        auto* source = animation_context ? animation_context->mdl_ : nullptr;
        if (source && source->model.supermodel) {
            source = source->model.supermodel.get();
        }
        set_scene_animation_source(scene, source);
    }
    return true;
}

static std::unique_ptr<PreviewScene> load_dynamic_creature_scene(
    PreviewRenderResources& resources, const std::filesystem::path& path, PreviewSceneLoadOptions options)
{
    const auto path_text = path.string();
    ERRARE("[viewer] loading dynamic creature preview '{}'", std::string_view{path_text});

    auto scene = std::make_unique<PreviewScene>();
    std::vector<PreviewLoadEvent> load_events;
    nw::PltColors plt_colors{};
    nw::Appearance appearance_id = nw::Appearance::invalid();
    uint8_t gender = 0;
    uint32_t wings = 0;
    uint32_t tail = 0;
    nw::BodyParts body_parts{};
    nw::Item* chest_item = nullptr;
    nw::Item* cloak_item = nullptr;
    nw::Item* head_item = nullptr;
    nw::Item* righthand_item = nullptr;
    nw::Item* lefthand_item = nullptr;

    const nw::Resource resource = nw::Resource::from_path(path, false);
    if (resource.type == nw::ResourceType::bic) {
        nw::Player player;
        if (!load_player_from_file(path, player)) {
            return {};
        }
        player.update_appearance(player.appearance.id);
        resolve_preview_equipment(player, path);
        appearance_id = player.appearance.id;
        gender = player.gender;
        wings = player.appearance.wings;
        tail = player.appearance.tail;
        body_parts = player.appearance.body_parts;
        plt_colors = creature_plt_colors(player.appearance);
        chest_item = equipped_item(player.equipment, nw::EquipIndex::chest);
        cloak_item = equipped_item(player.equipment, nw::EquipIndex::cloak);
        head_item = equipped_item(player.equipment, nw::EquipIndex::head);
        righthand_item = equipped_item(player.equipment, nw::EquipIndex::righthand);
        lefthand_item = equipped_item(player.equipment, nw::EquipIndex::lefthand);
    } else {
        nw::Creature creature;
        if (!load_creature_from_file(path, creature)) {
            return {};
        }
        creature.update_appearance(creature.appearance.id);
        resolve_preview_equipment(creature, path);
        appearance_id = creature.appearance.id;
        gender = creature.gender;
        wings = creature.appearance.wings;
        tail = creature.appearance.tail;
        body_parts = creature.appearance.body_parts;
        plt_colors = creature_plt_colors(creature.appearance);
        chest_item = equipped_item(creature.equipment, nw::EquipIndex::chest);
        cloak_item = equipped_item(creature.equipment, nw::EquipIndex::cloak);
        head_item = equipped_item(creature.equipment, nw::EquipIndex::head);
        righthand_item = equipped_item(creature.equipment, nw::EquipIndex::righthand);
        lefthand_item = equipped_item(creature.equipment, nw::EquipIndex::lefthand);
    }

    if (!add_dynamic_creature_scene_models(*scene,
            resources,
            appearance_id,
            gender,
            wings,
            tail,
            body_parts,
            plt_colors,
            chest_item,
            cloak_item,
            head_item,
            righthand_item,
            lefthand_item,
            path_text,
            options,
            &load_events)) {
        return {};
    }
    append_scene_authored_model_lights(*scene);
    scene->rebuild_load_report(path_text, "dynamic_creature");
    append_scene_load_events(*scene, load_events);
    return scene;
}

static bool add_item_scene_models(
    PreviewScene& scene,
    PreviewRenderResources& resources,
    const nw::Item& item,
    std::string_view origin,
    bool use_default_fallback)
{
    auto* baseitem = nw::kernel::rules().baseitems.get(item.baseitem);
    if (!baseitem) {
        LOG_F(ERROR, "Item preview '{}' has invalid baseitem {}", origin, *item.baseitem);
        log_preview_error_context();
        return false;
    }

    const size_t initial_model_count = scene.models.size();
    auto try_add = [&](std::string_view resref, nw::ItemModelParts::type part) {
        if (resref.empty()) {
            return;
        }
        ERRARE("[viewer] resolving item preview model '{}'", resref);
        if (!nw::kernel::resman().contains({nw::Resref{resref}, nw::ResourceType::mdl})) {
            LOG_F(WARNING, "Item preview '{}' model '{}' was not found",
                origin,
                resref);
            log_preview_warning_context();
            return;
        }
        auto model = load_model_with_plt(resources, resref);
        if (!model) {
            LOG_F(WARNING, "Item preview '{}' model '{}' failed to load",
                origin,
                resref);
            log_preview_warning_context();
            return;
        }
        if (nw::kernel::resman().contains({nw::Resref{resref}, nw::ResourceType::plt})) {
            auto item_plt = item.part_to_plt_colors(part);
            apply_plt_to_model(resources, *model, item_plt);
        }
        maybe_add_model(scene, std::move(model));
    };

    switch (item.model_type) {
    case nw::ItemModelType::simple:
    case nw::ItemModelType::layered:
        if (item.model_parts[nw::ItemModelParts::model1] > 0) {
            try_add(fmt::format("{}_{:03d}", baseitem->item_class.view(), item.model_parts[nw::ItemModelParts::model1]),
                nw::ItemModelParts::model1);
        }
        break;
    case nw::ItemModelType::composite:
        if (item.model_parts[nw::ItemModelParts::model1] > 0) {
            try_add(fmt::format("{}_b_{:03d}", baseitem->item_class.view(), item.model_parts[nw::ItemModelParts::model1]),
                nw::ItemModelParts::model1);
        }
        if (item.model_parts[nw::ItemModelParts::model2] > 0) {
            try_add(fmt::format("{}_m_{:03d}", baseitem->item_class.view(), item.model_parts[nw::ItemModelParts::model2]),
                nw::ItemModelParts::model2);
        }
        if (item.model_parts[nw::ItemModelParts::model3] > 0) {
            try_add(fmt::format("{}_t_{:03d}", baseitem->item_class.view(), item.model_parts[nw::ItemModelParts::model3]),
                nw::ItemModelParts::model3);
        }
        break;
    case nw::ItemModelType::armor:
        LOG_F(WARNING, "Standalone armor item preview is not supported yet; preview armor through a creature/player file instead");
        break;
    }

    if (use_default_fallback && scene.models.size() == initial_model_count && baseitem->default_model.valid()) {
        try_add(baseitem->default_model.resref.view(), nw::ItemModelParts::model1);
    }

    return scene.models.size() > initial_model_count;
}

static std::unique_ptr<PreviewScene> load_item_scene(PreviewRenderResources& resources, const std::filesystem::path& path)
{
    const auto path_text = path.string();
    ERRARE("[viewer] loading item preview '{}'", std::string_view{path_text});

    nw::Item item;
    if (!load_item_from_file(path, item)) {
        return {};
    }

    auto scene = std::make_unique<PreviewScene>();
    if (!add_item_scene_models(*scene, resources, item, path_text, true)) {
        LOG_F(ERROR, "Item preview '{}' produced no renderable models", path.string());
        log_preview_error_context();
        return {};
    }
    append_scene_authored_model_lights(*scene);
    scene->rebuild_load_report(path_text, "item");
    return scene;
}

static std::unique_ptr<PreviewScene> load_blueprint_model_scene(PreviewRenderResources& resources,
    const std::filesystem::path& path,
    std::string_view preview_type,
    std::string_view lookup_context,
    std::string_view model_resref)
{
    const auto path_text = path.string();
    ERRARE("[viewer] loading {} preview '{}' {}",
        preview_type,
        std::string_view{path_text},
        lookup_context);

    if (model_resref.empty()) {
        LOG_F(ERROR, "{} preview '{}' {} has no model",
            preview_type, path.string(), lookup_context);
        log_preview_error_context();
        return {};
    }

    if (!nw::kernel::resman().contains({nw::Resref{model_resref}, nw::ResourceType::mdl})) {
        LOG_F(ERROR, "{} preview '{}' model '{}' for {} was not found",
            preview_type, path.string(), model_resref, lookup_context);
        log_preview_error_context();
        return {};
    }

    auto scene = std::make_unique<PreviewScene>();
    ERRARE("[viewer] resolving {} preview model '{}'", preview_type, model_resref);
    maybe_add_model(*scene, load_model_with_plt(resources, model_resref));
    if (scene->models.empty()) {
        LOG_F(ERROR, "{} preview '{}' failed to load model '{}' for {}",
            preview_type, path.string(), model_resref, lookup_context);
        log_preview_error_context();
        return {};
    }
    append_scene_authored_model_lights(*scene);
    scene->rebuild_load_report(path_text, preview_type);
    return scene;
}

static std::optional<nw::DoorModelReference> resolve_door_model_reference(const nw::Door& door,
    const std::filesystem::path& path)
{
    auto lookup = nw::resolve_door_model(door);
    if (!lookup.valid()) {
        LOG_F(ERROR, "Door preview '{}': {}", path.string(), lookup.error);
        log_preview_error_context();
        return {};
    }
    return lookup;
}

static std::string door_model_lookup_context(const nw::DoorModelReference& lookup)
{
    return fmt::format("{} {} in {}.2da", lookup.selector, lookup.row, lookup.table);
}

static std::string area_object_origin(
    std::string_view area,
    std::string_view kind,
    size_t index,
    const nw::Common& common)
{
    if (common.tag) {
        return fmt::format("{}:{}:{}:{}", area, kind, index, common.tag.view());
    }
    if (!common.resref.empty()) {
        return fmt::format("{}:{}:{}:{}", area, kind, index, common.resref.view());
    }
    return fmt::format("{}:{}:{}", area, kind, index);
}

static glm::mat4 area_object_placement_transform(const nw::Location& location)
{
    constexpr float k_epsilon = 1.0e-5f;
    float angle = 0.0f;
    if (std::abs(location.orientation.x) > k_epsilon || std::abs(location.orientation.y) > k_epsilon) {
        angle = std::atan2(location.orientation.y, location.orientation.x);
    }

    glm::mat4 placement = glm::translate(glm::mat4{1.0f}, location.position);
    placement *= glm::toMat4(glm::angleAxis(angle, glm::vec3{0.0f, 0.0f, 1.0f}));
    return placement;
}

static std::optional<float> model_scalar_controller(const nw::model::Node& node, uint32_t type)
{
    const auto value = node.get_controller(type);
    if (value.key && !value.data.empty()) {
        return value.data[0];
    }
    return std::nullopt;
}

static std::optional<glm::vec3> model_vec3_controller(const nw::model::Node& node, uint32_t type)
{
    const auto value = node.get_controller(type);
    if (value.key && value.data.size() >= 3) {
        return glm::vec3{value.data[0], value.data[1], value.data[2]};
    }
    return std::nullopt;
}

static glm::quat model_quat_controller(
    const nw::model::Node& node,
    uint32_t type,
    const glm::quat& fallback = glm::quat{1.0f, 0.0f, 0.0f, 0.0f})
{
    const auto value = node.get_controller(type);
    if (value.key && value.data.size() >= 4) {
        const glm::quat result{value.data[3], value.data[0], value.data[1], value.data[2]};
        if (glm::dot(result, result) >= 1.0e-12f) {
            return glm::normalize(result);
        }
    }
    return fallback;
}

static glm::mat4 model_node_local_transform(const nw::model::Node& node)
{
    glm::mat4 result = glm::translate(
        glm::mat4{1.0f},
        model_vec3_controller(node, nw::model::ControllerType::Position).value_or(glm::vec3{0.0f}));
    result *= glm::mat4_cast(model_quat_controller(node, nw::model::ControllerType::Orientation));
    result = glm::scale(
        result,
        model_vec3_controller(node, nw::model::ControllerType::Scale).value_or(glm::vec3{1.0f}));
    return result;
}

static glm::mat4 model_node_transform(const nw::model::Node& node)
{
    glm::mat4 result = model_node_local_transform(node);
    for (auto* parent = node.parent; parent; parent = parent->parent) {
        result = model_node_local_transform(*parent) * result;
    }
    return result;
}

static void append_debug_triangle(
    PreviewScene& scene,
    const glm::vec3& a,
    const glm::vec3& b,
    const glm::vec3& c,
    const glm::vec4& color)
{
    const auto base = static_cast<uint32_t>(scene.debug_shape_vertices.size());
    scene.debug_shape_vertices.push_back({a, color});
    scene.debug_shape_vertices.push_back({b, color});
    scene.debug_shape_vertices.push_back({c, color});
    scene.debug_shape_indices.push_back(base);
    scene.debug_shape_indices.push_back(base + 1);
    scene.debug_shape_indices.push_back(base + 2);
}

static void append_debug_segment(
    PreviewScene& scene,
    const glm::vec3& a,
    const glm::vec3& b,
    const glm::vec4& color,
    float width)
{
    const glm::vec2 delta{b.x - a.x, b.y - a.y};
    const float length = glm::length(delta);
    if (length <= 1.0e-5f) {
        return;
    }

    const glm::vec2 side = glm::vec2{-delta.y, delta.x} * (0.5f * width / length);
    const auto base = static_cast<uint32_t>(scene.debug_shape_vertices.size());
    scene.debug_shape_vertices.push_back({{a.x + side.x, a.y + side.y, a.z}, color});
    scene.debug_shape_vertices.push_back({{b.x + side.x, b.y + side.y, b.z}, color});
    scene.debug_shape_vertices.push_back({{b.x - side.x, b.y - side.y, b.z}, color});
    scene.debug_shape_vertices.push_back({{a.x - side.x, a.y - side.y, a.z}, color});
    scene.debug_shape_indices.insert(scene.debug_shape_indices.end(), {
                                                                          base,
                                                                          base + 1,
                                                                          base + 2,
                                                                          base,
                                                                          base + 2,
                                                                          base + 3,
                                                                      });
}

static void append_debug_shape_range(PreviewScene& scene, DebugShapeCategory category, size_t first_index)
{
    const size_t last_index = scene.debug_shape_indices.size();
    if (last_index <= first_index || first_index > std::numeric_limits<uint32_t>::max()) {
        return;
    }

    scene.debug_shape_ranges.push_back(DebugShapeRange{
        .category = category,
        .first_index = static_cast<uint32_t>(first_index),
        .index_count = static_cast<uint32_t>(std::min<size_t>(
            last_index - first_index,
            std::numeric_limits<uint32_t>::max())),
    });
}

static SceneTileLightSlots scene_tile_light_slots(const nw::AreaTile& tile) noexcept
{
    return SceneTileLightSlots{
        .main1 = tile.mainlight1,
        .main2 = tile.mainlight2,
        .source1 = tile.srclight1,
        .source2 = tile.srclight2,
    };
}

static float color_max_channel(const glm::vec3& color) noexcept
{
    return std::max(color.x, std::max(color.y, color.z));
}

static glm::vec3 tile_light_hue(uint8_t index)
{
    static const std::array<glm::vec3, 7> colors{
        glm::vec3{1.0f, 0.82f, 0.32f},
        glm::vec3{0.32f, 1.0f, 0.36f},
        glm::vec3{0.25f, 0.95f, 1.0f},
        glm::vec3{0.34f, 0.52f, 1.0f},
        glm::vec3{0.72f, 0.38f, 1.0f},
        glm::vec3{1.0f, 0.32f, 0.28f},
        glm::vec3{1.0f, 0.58f, 0.22f},
    };
    return colors[std::min<size_t>(index, colors.size() - 1)];
}

static glm::vec3 tile_main_light_debug_color(uint8_t color_id)
{
    static constexpr std::array<float, 4> white_strengths{0.0f, 0.45f, 0.76f, 1.0f};
    static constexpr std::array<float, 4> hue_strengths{0.36f, 0.56f, 0.78f, 1.0f};
    if (color_id <= 3) {
        return glm::vec3{white_strengths[color_id]};
    }

    const uint8_t hue_index = static_cast<uint8_t>((color_id - 4) / 4);
    const uint8_t strength_index = static_cast<uint8_t>((color_id - 4) % 4);
    return tile_light_hue(hue_index) * hue_strengths[strength_index];
}

static bool has_visible_light_color(const glm::vec3& color) noexcept
{
    return color_max_channel(color) > 1.0e-4f;
}

static glm::vec3 model_light_authored_color(const nw::model::LightNode& light)
{
    if (auto authored_color = model_vec3_controller(light, nw::model::ControllerType::Color)) {
        if (has_visible_light_color(*authored_color)) {
            return *authored_color;
        }
    }
    if (has_visible_light_color(light.color)) {
        return light.color;
    }
    return glm::vec3{0.0f};
}

static glm::vec3 model_light_debug_color(const nw::model::LightNode& light, const SceneTileLightSlots& slots)
{
    glm::vec3 color{0.0f};
    if (has_tile_light_slots(slots)) {
        color = tile_slot_color_for_model_light(light, slots);
    }
    if (!has_visible_light_color(color)) {
        color = model_light_authored_color(light);
    }

    return glm::clamp(color, glm::vec3{0.0f}, glm::vec3{1.0f});
}

struct SceneLocalLightTuning {
    float radius_scale = 1.0f;
    float intensity_scale = 1.0f;
};

struct SceneModelLightAppendOptions {
    SceneTileLightSlots tile_slots{};
    int tile_x = -1;
    int tile_y = -1;
    uint8_t tile_orientation = 0;
    SceneLocalLightSource source = SceneLocalLightSource::authored_model;
    uint32_t model_index = kInvalidSceneLocalLightModelIndex;
    int32_t model_source_node_index = -1;
    SceneLocalLightTuning tuning{};
};

static SceneLocalLightTuning scene_local_light_tuning(const PreviewScene& scene) noexcept
{
    const bool interior = (scene.area_flags & nw::AreaFlags::interior) != nw::AreaFlags::none;
    const bool underground = (scene.area_flags & nw::AreaFlags::underground) != nw::AreaFlags::none;
    const bool night = scene.area_weather.is_night != 0 || underground;

    if (interior) {
        return SceneLocalLightTuning{.radius_scale = 0.82f, .intensity_scale = 0.52f};
    }
    if (underground) {
        return SceneLocalLightTuning{.radius_scale = 0.82f, .intensity_scale = 0.52f};
    }
    if (night) {
        return SceneLocalLightTuning{.radius_scale = 0.80f, .intensity_scale = 0.50f};
    }
    return SceneLocalLightTuning{.radius_scale = 0.62f, .intensity_scale = 0.28f};
}

static SceneLocalLightTuning scene_authored_model_light_tuning(const PreviewScene& scene) noexcept
{
    if (!scene.is_area) {
        return SceneLocalLightTuning{.radius_scale = 1.0f, .intensity_scale = 0.55f};
    }
    return scene_local_light_tuning(scene);
}

static SceneLocalLightTuning scene_placeable_table_light_tuning(const PreviewScene& scene) noexcept
{
    if (!scene.is_area) {
        return SceneLocalLightTuning{.radius_scale = 1.0f, .intensity_scale = 1.0f};
    }

    const bool interior = (scene.area_flags & nw::AreaFlags::interior) != nw::AreaFlags::none;
    const bool underground = (scene.area_flags & nw::AreaFlags::underground) != nw::AreaFlags::none;
    const bool night = scene.area_weather.is_night != 0 || underground;
    if (interior || underground || night) {
        return SceneLocalLightTuning{.radius_scale = 1.0f, .intensity_scale = 0.95f};
    }
    return SceneLocalLightTuning{.radius_scale = 0.92f, .intensity_scale = 0.72f};
}

static glm::vec3 placeable_table_light_color(const nw::Placeable& placeable, const nw::PlaceableInfo& info) noexcept
{
    int32_t light_color = info.light_color;
    if (light_color < 0 && placeable.light_color >= 0) {
        light_color = placeable.light_color;
    }
    if (light_color < 0) {
        return glm::vec3{0.0f};
    }

    return tile_main_light_debug_color(static_cast<uint8_t>(std::clamp(light_color, 0, 31)));
}

static SceneLocalLightTuning scene_light_tuning_for_source(
    const PreviewScene& scene,
    SceneLocalLightSource source) noexcept
{
    switch (source) {
    case SceneLocalLightSource::authored_model:
        return scene_authored_model_light_tuning(scene);
    case SceneLocalLightSource::tile_model:
        return scene_local_light_tuning(scene);
    case SceneLocalLightSource::placeable_table:
        return scene_placeable_table_light_tuning(scene);
    }
    return scene_authored_model_light_tuning(scene);
}

static SceneLocalLight scene_local_light_from_model_light(
    const nw::model::LightNode& light,
    const glm::mat4& root_transform,
    const SceneModelLightAppendOptions& options)
{
    const glm::mat4 model_transform = model_node_transform(light);
    auto world_position = glm::vec3(root_transform * model_transform[3]);
    const bool has_authored_tile_slots = has_tile_light_slots(options.tile_slots);
    const bool uses_main_slot = model_light_is_main_tile_slot(light);
    if (has_authored_tile_slots) {
        const float tile_base_z = glm::vec3(root_transform[3]).z;
        const float z_offset = uses_main_slot ? 1.6f : 1.1f;
        world_position.z = std::min(world_position.z, tile_base_z + z_offset);
    }
    const float minimum_radius = has_authored_tile_slots ? (uses_main_slot ? 6.0f : 3.4f) : 2.6f;
    const float maximum_radius = has_authored_tile_slots ? (uses_main_slot ? 9.2f : 5.4f) : 32.0f;
    const float minimum_intensity = has_authored_tile_slots ? (uses_main_slot ? 0.30f : 0.46f) : 0.05f;
    const float maximum_intensity = has_authored_tile_slots ? (uses_main_slot ? 1.0f : 1.35f) : 2.0f;
    const float contribution_scale = has_authored_tile_slots && uses_main_slot ? 0.45f : 1.0f;

    const float radius = model_scalar_controller(light, nw::model::ControllerType::Radius)
                             .value_or(model_scalar_controller(light, nw::model::ControllerType::ShadowRadius).value_or(minimum_radius));
    const float multiplier = model_scalar_controller(light, nw::model::ControllerType::Multiplier)
                                 .value_or(light.multiplier > 0.0f ? light.multiplier : 1.0f);
    const float base_radius = std::clamp(radius, minimum_radius, maximum_radius);
    const float base_intensity = std::clamp(std::max(multiplier, minimum_intensity), 0.0f, maximum_intensity)
        * contribution_scale;

    const bool ambient_contribution = light.ambientonly != 0 || (has_authored_tile_slots && uses_main_slot);
    return SceneLocalLight{
        .position = world_position,
        .radius = base_radius * options.tuning.radius_scale,
        .color = model_light_debug_color(light, options.tile_slots),
        .intensity = base_intensity * options.tuning.intensity_scale,
        .base_radius = base_radius,
        .base_intensity = base_intensity,
        .source = options.source,
        .model_index = options.model_index,
        .model_source_node_index = options.model_source_node_index,
        .tile_light_slots = options.tile_slots,
        .tile_x = static_cast<uint16_t>(
            options.tile_x >= 0
                ? std::clamp(options.tile_x, 0, static_cast<int>(std::numeric_limits<uint16_t>::max()))
                : 0),
        .tile_y = static_cast<uint16_t>(
            options.tile_y >= 0
                ? std::clamp(options.tile_y, 0, static_cast<int>(std::numeric_limits<uint16_t>::max()))
                : 0),
        .tile_orientation = options.tile_orientation,
        .dynamic = static_cast<uint8_t>(light.dynamic ? 1 : 0),
        .affect_dynamic = static_cast<uint8_t>(light.affectdynamic != 0 ? 1 : 0),
        .ambient_contribution = static_cast<uint8_t>(ambient_contribution ? 1 : 0),
        .casts_shadow = static_cast<uint8_t>(light.shadow != 0 ? 1 : 0),
        .fading = static_cast<uint8_t>(light.fadinglight != 0 ? 1 : 0),
    };
}

static nw::render::LocalLight render_local_light_from_scene_light(const SceneLocalLight& light) noexcept
{
    return nw::render::LocalLight{
        .position = light.position,
        .radius = light.radius,
        .color = light.color,
        .intensity = light.intensity,
        .contribution = light.ambient_contribution != 0
            ? nw::render::LocalLightContribution::ambient
            : nw::render::LocalLightContribution::diffuse,
        .vertical_scale = light.ambient_contribution != 0 ? 0.45f : 1.0f,
        .casts_shadow = light.casts_shadow != 0 && light.ambient_contribution == 0,
    };
}

static void apply_scene_light_tuning(PreviewScene& scene, SceneLocalLight& light) noexcept
{
    if (light.base_radius <= 0.0f) {
        light.base_radius = light.radius;
    }
    if (light.base_intensity <= 0.0f) {
        light.base_intensity = light.intensity;
    }

    const SceneLocalLightTuning tuning = scene_light_tuning_for_source(scene, light.source);
    light.radius = light.base_radius * tuning.radius_scale;
    light.intensity = light.base_intensity * tuning.intensity_scale;
}

static void append_scene_local_light(PreviewScene& scene, SceneLocalLight light)
{
    apply_scene_light_tuning(scene, light);
    scene.render_local_lights.push_back(render_local_light_from_scene_light(light));
    scene.local_lights.push_back(light);
    if (scene.is_area && viewer_tile_light_debug_shapes_enabled()) {
        LOG_F(INFO,
            "local_light source={} pos=[{:.2f},{:.2f},{:.2f}] radius={:.2f} intensity={:.2f} color=[{:.2f},{:.2f},{:.2f}] ambient={} shadow={}",
            static_cast<int>(light.source),
            light.position.x,
            light.position.y,
            light.position.z,
            light.radius,
            light.intensity,
            light.color.r,
            light.color.g,
            light.color.b,
            light.ambient_contribution,
            light.casts_shadow);
    }
}

static void append_debug_light_marker(PreviewScene& scene, const SceneLocalLight& light);

static size_t append_placeable_table_light(
    PreviewScene& scene,
    const nw::Placeable& placeable,
    const nw::PlaceableInfo& info)
{
    constexpr float k_base_radius = 6.0f;
    constexpr float k_base_intensity = 0.86f;

    const glm::vec3 color = placeable_table_light_color(placeable, info);
    if (!has_visible_light_color(color)) {
        if (viewer_tile_light_debug_shapes_enabled()) {
            LOG_F(INFO,
                "placeable_table_light skipped color=[{:.2f},{:.2f},{:.2f}] light_color={}",
                color.r,
                color.g,
                color.b,
                info.light_color);
        }
        return 0;
    }

    const SceneLocalLightTuning tuning = scene_placeable_table_light_tuning(scene);
    const glm::mat4 placement = area_object_placement_transform(placeable.common.location);
    const glm::vec3 local_offset{info.light_offset_x, info.light_offset_y, info.light_offset_z};
    const glm::vec3 world_position = glm::vec3(placement * glm::vec4{local_offset, 1.0f});

    SceneLocalLight light{
        .position = world_position,
        .radius = k_base_radius * tuning.radius_scale,
        .color = color,
        .intensity = k_base_intensity * tuning.intensity_scale,
        .base_radius = k_base_radius,
        .base_intensity = k_base_intensity,
        .source = SceneLocalLightSource::placeable_table,
        .dynamic = 0,
        .affect_dynamic = 1,
    };
    append_scene_local_light(scene, light);
    if (viewer_tile_light_debug_shapes_enabled()) {
        const size_t first_debug_index = scene.debug_shape_indices.size();
        append_debug_light_marker(scene, light);
        append_debug_shape_range(scene, DebugShapeCategory::general, first_debug_index);
    }
    return 1;
}

static size_t append_model_light_nodes(
    PreviewScene& scene,
    const nw::model::Mdl& mdl,
    const glm::mat4& root_transform,
    const SceneModelLightAppendOptions& options)
{
    size_t result = 0;
    for (size_t node_index = 0; node_index < mdl.model.nodes.size(); ++node_index) {
        const auto& node = mdl.model.nodes[node_index];
        const auto* light = dynamic_cast<const nw::model::LightNode*>(node.get());
        if (!light) {
            continue;
        }

        auto light_options = options;
        if (node_index <= static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
            light_options.model_source_node_index = static_cast<int32_t>(node_index);
        }
        if (viewer_tile_light_debug_shapes_enabled()) {
            LOG_F(INFO,
                "model_light_node model={} node={} tile_slots=[{},{},{},{}] source={}",
                mdl.model.name,
                light->name,
                options.tile_slots.main1,
                options.tile_slots.main2,
                options.tile_slots.source1,
                options.tile_slots.source2,
                static_cast<int>(options.source));
        }
        const auto scene_light = scene_local_light_from_model_light(*light, root_transform, light_options);
        append_scene_local_light(scene, scene_light);
        if (viewer_tile_light_debug_shapes_enabled()) {
            const size_t first_debug_index = scene.debug_shape_indices.size();
            append_debug_light_marker(scene, scene_light);
            append_debug_shape_range(scene, DebugShapeCategory::general, first_debug_index);
        }
        ++result;
    }
    return result;
}

static size_t append_model_authored_lights(PreviewScene& scene, size_t model_index)
{
    if (model_index >= scene.models.size()
        || model_index > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
        return 0;
    }

    const auto& model = scene.models[model_index];
    const auto* instance = scene.nwn_model_instance(model_index);
    if (!model || !model->mdl_ || !instance || !instance->visible) {
        return 0;
    }

    return append_model_light_nodes(scene, *model->mdl_, instance->root_transform, SceneModelLightAppendOptions{
                                                                                       .model_index = static_cast<uint32_t>(model_index),
                                                                                       .tuning = scene_authored_model_light_tuning(scene),
                                                                                   });
}

static size_t append_scene_authored_model_lights(PreviewScene& scene)
{
    size_t result = 0;
    for (size_t model_index = 0; model_index < scene.models.size(); ++model_index) {
        result += append_model_authored_lights(scene, model_index);
    }
    return result;
}

static void append_debug_light_marker(PreviewScene& scene, const SceneLocalLight& light)
{
    constexpr float k_marker_z_offset = 0.1f;
    constexpr float k_marker_width = 0.055f;

    const glm::vec3 center = light.position + glm::vec3{0.0f, 0.0f, k_marker_z_offset};
    const glm::vec3 rgb = glm::clamp(light.color * std::clamp(light.intensity, 0.35f, 1.4f),
        glm::vec3{0.0f},
        glm::vec3{1.0f});
    const glm::vec4 color{rgb, 0.86f};

    append_debug_segment(
        scene,
        center + glm::vec3{-0.35f, 0.0f, 0.0f},
        center + glm::vec3{0.35f, 0.0f, 0.0f},
        color,
        k_marker_width);
    append_debug_segment(
        scene,
        center + glm::vec3{0.0f, -0.35f, 0.0f},
        center + glm::vec3{0.0f, 0.35f, 0.0f},
        color,
        k_marker_width);
}

void refresh_scene_local_light_render_data(PreviewScene& scene)
{
    scene.render_local_lights.clear();
    scene.render_local_lights.reserve(scene.local_lights.size());
    for (auto& light : scene.local_lights) {
        apply_scene_light_tuning(scene, light);
        scene.render_local_lights.push_back(render_local_light_from_scene_light(light));
    }
}

static bool scene_light_tracks_model_node(const SceneLocalLight& light) noexcept
{
    return light.source == SceneLocalLightSource::authored_model
        && light.model_index != kInvalidSceneLocalLightModelIndex
        && light.model_source_node_index >= 0;
}

static bool disable_scene_render_light(PreviewScene& scene, size_t light_index) noexcept
{
    if (light_index >= scene.render_local_lights.size()) {
        return false;
    }

    auto& light = scene.render_local_lights[light_index];
    const bool changed = light.radius != 0.0f
        || light.intensity != 0.0f
        || light.casts_shadow
        || light.shadow_slot != -1;
    light.radius = 0.0f;
    light.intensity = 0.0f;
    light.casts_shadow = false;
    light.shadow_slot = -1;
    return changed;
}

static bool scene_light_model_node_position(
    const PreviewScene& scene,
    const SceneLocalLight& light,
    glm::vec3& out_position) noexcept
{
    if (light.model_index >= scene.models.size() || light.model_source_node_index < 0) {
        return false;
    }

    const auto& model = scene.models[light.model_index];
    if (!model) {
        return false;
    }

    const auto* node = model->node_from_source_index(light.model_source_node_index);
    if (!node) {
        return false;
    }

    const auto* instance = scene.nwn_model_instance(light.model_index);
    if (!instance || !instance->visible) {
        return false;
    }

    out_position = glm::vec3(instance->root_transform * node->get_transform()[3]);
    return std::isfinite(out_position.x) && std::isfinite(out_position.y) && std::isfinite(out_position.z);
}

bool refresh_scene_dynamic_local_light_render_data(PreviewScene& scene)
{
    bool changed = false;
    if (scene.render_local_lights.size() != scene.local_lights.size()) {
        refresh_scene_local_light_render_data(scene);
        changed = true;
    }

    for (size_t i = 0; i < scene.local_lights.size(); ++i) {
        auto& light = scene.local_lights[i];
        glm::vec3 position{0.0f};
        if (!scene_light_model_node_position(scene, light, position)) {
            if (scene_light_tracks_model_node(light) && disable_scene_render_light(scene, i)) {
                changed = true;
            }
            continue;
        }

        const glm::vec3 delta = position - light.position;
        const bool moved = glm::dot(delta, delta) > 1.0e-8f;
        const bool disabled = i < scene.render_local_lights.size()
            && (scene.render_local_lights[i].radius == 0.0f || scene.render_local_lights[i].intensity == 0.0f);
        if (!moved && !disabled) {
            continue;
        }

        if (moved) {
            light.position = position;
        }
        if (i < scene.render_local_lights.size()) {
            scene.render_local_lights[i] = render_local_light_from_scene_light(light);
        }
        changed = true;
    }
    return changed;
}

static size_t append_tile_model_lights(
    PreviewScene& scene,
    const nw::model::Mdl& mdl,
    const glm::mat4& tile_placement,
    const nw::AreaTile& tile,
    int tile_x,
    int tile_y)
{
    const SceneTileLightSlots slots = scene_tile_light_slots(tile);
    return append_model_light_nodes(scene, mdl, tile_placement, SceneModelLightAppendOptions{
                                                                    .tile_slots = slots,
                                                                    .tile_x = tile_x,
                                                                    .tile_y = tile_y,
                                                                    .tile_orientation = static_cast<uint8_t>(std::clamp(tile.orientation, 0, 255)),
                                                                    .source = SceneLocalLightSource::tile_model,
                                                                    .tuning = scene_local_light_tuning(scene),
                                                                });
}

static std::vector<glm::vec3> normalize_debug_polygon_points(
    std::span<const glm::vec3> geometry,
    const glm::mat4& placement,
    float z_offset)
{
    std::vector<glm::vec3> points;
    points.reserve(geometry.size());
    for (const auto& local_point : geometry) {
        auto point = glm::vec3(placement * glm::vec4{local_point, 1.0f});
        point.z += z_offset;
        if (!points.empty() && glm::length(glm::vec2{point.x - points.back().x, point.y - points.back().y}) <= 1.0e-5f) {
            continue;
        }
        points.push_back(point);
    }

    if (points.size() > 1) {
        const auto& first = points.front();
        const auto& last = points.back();
        if (glm::length(glm::vec2{first.x - last.x, first.y - last.y}) <= 1.0e-5f) {
            points.pop_back();
        }
    }
    return points;
}

static void append_debug_polygon(
    PreviewScene& scene,
    std::span<const glm::vec3> geometry,
    const nw::Location& location,
    const glm::vec4& outline_color,
    float z_offset,
    float outline_width)
{
    const auto points = normalize_debug_polygon_points(geometry, area_object_placement_transform(location), z_offset);
    if (points.size() < 2) {
        return;
    }

    for (size_t i = 0; i < points.size(); ++i) {
        append_debug_segment(scene, points[i], points[(i + 1) % points.size()], outline_color, outline_width);
    }
}

static void append_debug_spawn_marker(PreviewScene& scene, const nw::SpawnPoint& spawn_point)
{
    constexpr float k_marker_z_offset = 0.16f;
    constexpr float k_marker_width = 0.08f;
    const glm::vec4 color{1.0f, 0.22f, 0.18f, 0.95f};
    const glm::vec3 origin = spawn_point.position + glm::vec3{0.0f, 0.0f, k_marker_z_offset};
    const glm::vec3 forward{std::cos(spawn_point.orientation), std::sin(spawn_point.orientation), 0.0f};
    const glm::vec3 right{-forward.y, forward.x, 0.0f};

    append_debug_segment(scene, origin - forward * 0.25f, origin + forward * 0.65f, color, k_marker_width);
    append_debug_segment(scene, origin - right * 0.25f, origin + right * 0.25f, color, k_marker_width);
    append_debug_triangle(
        scene,
        origin + forward * 0.85f,
        origin + forward * 0.5f + right * 0.2f,
        origin + forward * 0.5f - right * 0.2f,
        color);
}

static bool append_trigger_debug_geometry(PreviewScene& scene, const nw::Trigger& trigger)
{
    if (trigger.geometry.empty()) {
        return false;
    }

    const size_t first_debug_index = scene.debug_shape_indices.size();
    constexpr float k_floor_z_offset = 0.08f;
    constexpr float k_outline_width = 0.07f;
    const glm::vec4 outline_color{0.12f, 0.92f, 1.0f, 0.9f};
    append_debug_polygon(scene, trigger.geometry, trigger.common.location, outline_color, k_floor_z_offset, k_outline_width);

    if (trigger.highlight_height > 0.25f) {
        append_debug_polygon(
            scene,
            trigger.geometry,
            trigger.common.location,
            {0.12f, 0.92f, 1.0f, 0.55f},
            k_floor_z_offset + trigger.highlight_height,
            k_outline_width * 0.8f);
    }
    append_debug_shape_range(scene, DebugShapeCategory::trigger, first_debug_index);
    return scene.debug_shape_indices.size() > first_debug_index;
}

static bool append_encounter_debug_geometry(PreviewScene& scene, const nw::Encounter& encounter)
{
    const size_t first_debug_index = scene.debug_shape_indices.size();
    if (!encounter.geometry.empty()) {
        constexpr float k_floor_z_offset = 0.12f;
        constexpr float k_outline_width = 0.07f;
        append_debug_polygon(
            scene,
            encounter.geometry,
            encounter.common.location,
            {1.0f, 0.18f, 0.72f, 0.9f},
            k_floor_z_offset,
            k_outline_width);
    }

    for (const auto& spawn_point : encounter.spawn_points) {
        append_debug_spawn_marker(scene, spawn_point);
    }
    append_debug_shape_range(scene, DebugShapeCategory::encounter, first_debug_index);
    return scene.debug_shape_indices.size() > first_debug_index;
}

static const nw::PlaceableInfo* resolve_area_placeable_info(
    const nw::Placeable& placeable,
    std::string_view origin)
{
    if (placeable.appearance > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
        LOG_F(WARNING,
            "Area placeable '{}' appearance {} is invalid",
            origin,
            placeable.appearance);
        return {};
    }

    const auto* info = nw::kernel::rules().placeables.get(
        nw::PlaceableType::make(static_cast<int32_t>(placeable.appearance)));
    if (!info) {
        LOG_F(WARNING,
            "Area placeable '{}' appearance {} was not found in placeables.2da",
            origin,
            placeable.appearance);
        return nullptr;
    }

    return info;
}

static std::unique_ptr<ModelInstance> load_area_object_model(
    PreviewRenderResources& resources,
    std::string_view model_resref,
    const nw::Location& location,
    std::string_view origin)
{
    if (model_resref.empty()) {
        return {};
    }

    auto model = load_model_with_plt(resources, model_resref);
    if (!model) {
        LOG_F(WARNING, "Failed to load area object model '{}' for {}", model_resref, origin);
        return {};
    }

    make_static_scene_attachment(*model);
    model->set_placement_transform(area_object_placement_transform(location));
    return model;
}

static void set_scene_root_placement(PreviewScene& scene, const glm::mat4& placement)
{
    for (auto& model : scene.models) {
        if (model && !model->transform_context_) {
            model->set_placement_transform(placement);
        }
    }
    for (size_t model_index = 0; model_index < scene.static_models.size(); ++model_index) {
        auto* instance = scene.static_model_instance(model_index);
        if (!instance) {
            continue;
        }
        instance->root_transform = placement;
    }
    sync_model_instance_runtime_state(scene);
}

static void move_render_model_runtime_state(
    PreviewScene& target,
    PreviewScene& source,
    uint32_t source_model_index,
    uint32_t target_model_index)
{
    auto* source_instance = source.static_model_instance(source_model_index);
    auto* target_instance = target.static_model_instance(target_model_index);
    if (!source_instance || !target_instance) {
        return;
    }

    target_instance->visible = source_instance->visible;
    target_instance->root_transform = source_instance->root_transform;
    target_instance->current_bounds = source_instance->current_bounds;
    target_instance->shadow = source_instance->shadow;
    target_instance->attachment_node_world_transforms = std::move(source_instance->attachment_node_world_transforms);
    target_instance->attachment_node_transform_valid = std::move(source_instance->attachment_node_transform_valid);
    target_instance->animation = std::move(source_instance->animation);
    target_instance->kind = nw::render::ModelInstanceKind::render_model;
    target_instance->render_model_index = target_model_index;
    target_instance->nwn_legacy_model_index = nw::render::kInvalidModelInstanceIndex;

    // Material override handles are scene-owned stable handles, so the target
    // scene must clone the override rows instead of copying stale source handles.
    target_instance->material_override_handles.clear();
    target_instance->material_override_handles.resize(source_instance->material_override_handles.size());
    for (size_t material_index = 0; material_index < source_instance->material_override_handles.size(); ++material_index) {
        const auto* source_override = source.material_overrides.get(
            source_instance->material_override_handles[material_index]);
        if (!source_override) {
            continue;
        }
        target_instance->material_override_handles[material_index] = target.material_overrides.create(*source_override);
    }
}

static void append_render_model_attachment_bindings(
    PreviewScene& target,
    const PreviewScene& source,
    std::span<const uint32_t> model_index_map)
{
    for (const auto& binding : source.model_attachments) {
        if (binding.child_kind != nw::render::ModelInstanceKind::render_model
            || binding.owner_kind != nw::render::ModelInstanceKind::render_model
            || binding.child_model_index >= model_index_map.size()
            || binding.owner_model_index >= model_index_map.size()) {
            continue;
        }

        const uint32_t target_child_index = model_index_map[binding.child_model_index];
        const uint32_t target_owner_index = model_index_map[binding.owner_model_index];
        if (target_child_index == nw::render::kInvalidModelInstanceIndex
            || target_owner_index == nw::render::kInvalidModelInstanceIndex
            || target_child_index >= target.static_model_instance_handles.size()
            || target_owner_index >= target.static_model_instance_handles.size()) {
            continue;
        }

        const uint32_t binding_index = static_cast<uint32_t>(
            std::min<size_t>(target.model_attachments.size(), std::numeric_limits<uint32_t>::max()));
        target.model_attachments.push_back(SceneModelAttachmentBinding{
            .child_kind = nw::render::ModelInstanceKind::render_model,
            .child_model_index = target_child_index,
            .child_instance_handle = target.static_model_instance_handles[target_child_index],
            .owner_kind = nw::render::ModelInstanceKind::render_model,
            .owner_model_index = target_owner_index,
            .owner_instance_handle = target.static_model_instance_handles[target_owner_index],
            .owner_socket_index = binding.owner_socket_index,
            .child_source_socket_index = binding.child_source_socket_index,
            .child_local_scale = binding.child_local_scale,
        });
        target.static_model_attachment_binding_indices[target_child_index] = binding_index;
    }
}

static size_t append_scene_models(
    PreviewScene& target,
    PreviewScene& source,
    AreaRenderSourceInfo area_info = {})
{
    const size_t nwn_model_count = source.models.size();
    for (auto& model : source.models) {
        const auto* moved_model = model.get();
        target.add(std::move(model));
        if (!target.area_model_info.empty()) {
            target.area_model_info.back() = area_info;
        }
        if (moved_model && !target.models.empty()) {
            append_model_authored_lights(target, target.models.size() - 1);
        }
    }
    source.models.clear();
    source.model_attachment_binding_indices.clear();

    // AreaRenderScene currently records only legacy sidecar rows. RenderModel
    // rows are still moved into the target scene so the generic RenderModel
    // path can draw and animate them; area cache/culling integration is a
    // separate protocol expansion because it needs its own model-index space.
    std::vector<uint32_t> static_model_index_map(
        source.static_models.size(), nw::render::kInvalidModelInstanceIndex);
    size_t render_model_count = 0;
    for (size_t source_model_index = 0; source_model_index < source.static_models.size(); ++source_model_index) {
        auto& model = source.static_models[source_model_index];
        if (!model || source_model_index > std::numeric_limits<uint32_t>::max()) {
            continue;
        }
        const uint32_t target_model_index = static_cast<uint32_t>(
            std::min<size_t>(target.static_models.size(), std::numeric_limits<uint32_t>::max()));
        target.add(std::move(model));
        if (target_model_index < target.static_models.size()) {
            static_model_index_map[source_model_index] = target_model_index;
            if (target_model_index < target.static_area_model_info.size()) {
                target.static_area_model_info[target_model_index] = area_info;
            }
            move_render_model_runtime_state(
                target,
                source,
                static_cast<uint32_t>(source_model_index),
                target_model_index);
            ++render_model_count;
        }
    }
    append_render_model_attachment_bindings(target, source, static_model_index_map);
    if (render_model_count > 0) {
        sync_model_instance_runtime_state(target);
    }
    source.static_models.clear();
    source.static_model_instance_handles.clear();
    source.static_model_attachment_binding_indices.clear();
    source.static_area_model_info.clear();
    source.material_overrides.clear();
    source.area_model_info.clear();
    source.local_lights.clear();
    source.render_local_lights.clear();
    return nwn_model_count + render_model_count;
}

static std::unique_ptr<PreviewScene> load_area_creature_scene(
    PreviewRenderResources& resources,
    nw::Creature& creature,
    std::string_view origin,
    PreviewSceneLoadOptions options)
{
    if (!creature.instantiate()) {
        LOG_F(WARNING, "Area creature '{}' failed to instantiate", origin);
        return {};
    }
    creature.update_appearance(creature.appearance.id);

    auto scene = std::make_unique<PreviewScene>();
    if (!add_dynamic_creature_scene_models(*scene,
            resources,
            creature.appearance.id,
            creature.gender,
            creature.appearance.wings,
            creature.appearance.tail,
            creature.appearance.body_parts,
            creature_plt_colors(creature.appearance),
            equipped_item(creature.equipment, nw::EquipIndex::chest),
            equipped_item(creature.equipment, nw::EquipIndex::cloak),
            equipped_item(creature.equipment, nw::EquipIndex::head),
            equipped_item(creature.equipment, nw::EquipIndex::righthand),
            equipped_item(creature.equipment, nw::EquipIndex::lefthand),
            origin,
            options,
            nullptr)) {
        return {};
    }

    set_scene_root_placement(*scene, area_object_placement_transform(creature.common.location));
    return scene;
}

static std::unique_ptr<PreviewScene> load_area_item_scene(
    PreviewRenderResources& resources,
    nw::Item& item,
    std::string_view origin)
{
    if (!item.instantiate()) {
        LOG_F(WARNING, "Area item '{}' failed to instantiate", origin);
        return {};
    }

    auto scene = std::make_unique<PreviewScene>();
    if (!add_item_scene_models(*scene, resources, item, origin, true)) {
        return {};
    }

    set_scene_root_placement(*scene, area_object_placement_transform(item.common.location));
    return scene;
}

static std::unique_ptr<PreviewScene> load_door_scene(PreviewRenderResources& resources, const std::filesystem::path& path)
{
    const auto path_text = path.string();
    ERRARE("[viewer] loading door preview '{}'", std::string_view{path_text});

    nw::Door door;
    if (!load_door_from_file(path, door)) {
        return {};
    }

    auto model_ref = resolve_door_model_reference(door, path);
    if (!model_ref) {
        return {};
    }

    return load_blueprint_model_scene(resources, path, "Door",
        door_model_lookup_context(*model_ref), model_ref->model.view());
}

static std::unique_ptr<PreviewScene> load_placeable_scene(PreviewRenderResources& resources, const std::filesystem::path& path)
{
    const auto path_text = path.string();
    ERRARE("[viewer] loading placeable preview '{}'", std::string_view{path_text});

    nw::Placeable placeable;
    if (!load_placeable_from_file(path, placeable)) {
        return {};
    }

    if (placeable.appearance > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
        LOG_F(ERROR, "Placeable preview '{}' appearance {} is invalid",
            path.string(), placeable.appearance);
        log_preview_error_context();
        return {};
    }

    const auto* info = nw::kernel::rules().placeables.get(
        nw::PlaceableType::make(static_cast<int32_t>(placeable.appearance)));
    if (!info) {
        LOG_F(ERROR, "Placeable preview '{}' appearance {} was not found in placeables.2da",
            path.string(), placeable.appearance);
        log_preview_error_context();
        return {};
    }

    const auto lookup_context = fmt::format("appearance {}", placeable.appearance);
    auto scene = load_blueprint_model_scene(resources, path, "Placeable", lookup_context, info->model.view());
    if (scene) {
        append_placeable_table_light(*scene, placeable, *info);
    }
    return scene;
}

static void add_source_resource(PreviewLoadReportBuilder& builder, const std::filesystem::path& path)
{
    const nw::Resource resource = nw::Resource::from_path(path, false);
    if (resource.valid()) {
        builder.add_resource(resource, PreviewLoadResourceStatus::found, "source");
    }
}

static void add_unique_report_model_name(PreviewLoadReport& report, std::string_view name)
{
    if (name.empty()) {
        return;
    }
    if (std::find(report.model_names.begin(), report.model_names.end(), name) == report.model_names.end()) {
        report.model_names.emplace_back(name);
    }
}

static void add_particle_material_resources(
    PreviewLoadReportBuilder& builder,
    const nw::render::ParticleEffectDef& effect,
    std::string_view owner)
{
    for (const auto& material : effect.materials) {
        const auto material_name = material.name.empty() ? std::string{"material"} : material.name;
        const auto origin = fmt::format("{}:particle:{}", owner, material_name);
        add_texture_family(builder, material.texture, origin);
        if (!is_null_preview_resource_name(material.mesh)) {
            builder.add_existing_or_missing_resource({material.mesh, nw::ResourceType::mdl}, origin);
        }
    }
}

static std::optional<bool> particle_texture_has_alpha(std::string_view raw_name)
{
    const auto cleaned = clean_preview_resource_token(raw_name);
    if (is_null_preview_resource_name(cleaned) || !looks_like_preview_resref(cleaned)) {
        return std::nullopt;
    }

    std::filesystem::path path{cleaned};
    const auto name = path.extension().empty() ? cleaned : path.stem().string();
    auto image = std::unique_ptr<nw::Image>{nw::kernel::resman().texture(nw::Resref{name})};
    if (!image || !image->valid()) {
        return std::nullopt;
    }
    return image->channels() >= 4;
}

static PreviewLoadParticleSystemReport make_particle_report_row(
    std::string_view owner,
    const nw::model::ParticleImportResult& import,
    const nw::render::ParticleCompileResult& compiled)
{
    PreviewLoadParticleSystemReport result{
        .owner = std::string{owner},
        .emitter_count = import.effect.emitters.size(),
        .material_count = import.effect.materials.size(),
        .max_particles_total = compiled.effect.max_particles_total,
        .import_warning_count = import.warnings.size(),
        .compile_warning_count = compiled.warnings.size(),
        .effect_event_count = import.effect_events.size(),
    };

    for (const auto& material : import.effect.materials) {
        switch (material.blend) {
        case nw::render::ParticleBlendMode::alpha:
            ++result.alpha_material_count;
            break;
        case nw::render::ParticleBlendMode::cutout:
            ++result.cutout_material_count;
            break;
        case nw::render::ParticleBlendMode::additive:
            ++result.additive_material_count;
            break;
        }

        const auto cleaned = clean_preview_resource_token(material.texture);
        if (is_null_preview_resource_name(cleaned) || !looks_like_preview_resref(cleaned)) {
            continue;
        }
        ++result.named_texture_count;
        const auto alpha = particle_texture_has_alpha(cleaned);
        if (!alpha) {
            ++result.missing_texture_count;
        } else if (*alpha) {
            ++result.alpha_texture_count;
        } else {
            ++result.opaque_texture_count;
        }
    }

    return result;
}

static void add_particle_report(
    PreviewLoadReport& report,
    PreviewLoadReportBuilder& builder,
    std::string_view owner,
    const nw::model::ParticleImportResult& import,
    const nw::render::ParticleCompileResult& compiled)
{
    report.particles.push_back(make_particle_report_row(owner, import, compiled));
    add_particle_material_resources(builder, import.effect, owner);

    for (const auto& warning : import.warnings) {
        builder.add_event(PreviewLoadEventSeverity::warning,
            "particle_import",
            fmt::format("{}: {}.{}: {}",
                owner,
                warning.emitter,
                warning.field,
                warning.message));
    }
    for (const auto& warning : compiled.warnings) {
        builder.add_event(PreviewLoadEventSeverity::warning,
            "particle_compile",
            fmt::format("{}: emitter {}: {}",
                owner,
                warning.emitter,
                warning.message));
    }
}

static void add_model_particle_report(
    PreviewLoadReport& report,
    PreviewLoadReportBuilder& builder,
    const nw::model::Mdl& mdl,
    std::string_view animation_name)
{
    auto particle_animation = animation_name;
    if (particle_animation.empty()) {
        particle_animation = preferred_model_animation_name(
            mdl, PreferredModelAnimationContext::particle_preview);
    }

    auto import = nw::model::import_particle_effect(mdl, particle_animation, false);
    if (import.effect.emitters.empty()) {
        return;
    }
    auto compiled = nw::render::compile_particle_effect(import.effect);
    const std::string owner = mdl.model.name.empty() ? std::string{"<unknown>"} : std::string{mdl.model.name.c_str()};
    add_particle_report(report, builder, owner, import, compiled);
}

static void add_nwn_model_asset_reports(
    PreviewLoadReport& report,
    const nw::model::Mdl& mdl,
    std::string_view owner)
{
    const std::string owner_text = owner.empty() ? std::string{mdl.model.name.c_str()} : std::string{owner};
    if (material_report_exists(report, owner_text) && geometry_report_exists(report, owner_text)) {
        return;
    }

    auto imported = nw::render::nwn::import_nwn_model_asset(mdl);
    if (!imported.asset) {
        return;
    }
    add_model_asset_material_report(report, owner_text, *imported.asset);
    add_model_asset_geometry_report(report, owner_text, *imported.asset, &imported.stats);
}

static void add_report_model(
    PreviewLoadReport& report,
    PreviewLoadReportBuilder& builder,
    std::set<std::string>& scanned_models,
    std::string_view raw_resref,
    std::string origin,
    std::string_view animation_name)
{
    auto model_resref = clean_preview_resource_token(raw_resref);
    if (is_null_preview_resource_name(model_resref)) {
        return;
    }

    const std::filesystem::path source_path{model_resref};
    nw::String ext = source_path.extension().string();
    nw::string::tolower(&ext);
    if (ext == ".mdl" && std::filesystem::exists(source_path)) {
        nw::model::Mdl mdl{source_path};
        if (!mdl.valid()) {
            builder.add_event(PreviewLoadEventSeverity::error,
                "model",
                fmt::format("Failed to parse local model '{}'", source_path.string()));
            return;
        }
        add_unique_report_model_name(report,
            mdl.model.name.empty() ? source_path.stem().string() : std::string_view{mdl.model.name});
        scan_preview_model_dependencies(builder, mdl, scanned_models);
        add_nwn_model_asset_reports(report,
            mdl,
            mdl.model.name.empty() ? source_path.stem().string() : std::string_view{mdl.model.name});
        add_model_particle_report(report, builder, mdl, animation_name);
        return;
    }
    if (ext == ".mdl") {
        model_resref = source_path.stem().string();
    }

    nw::Resource model_resource{model_resref, nw::ResourceType::mdl};
    if (!nw::kernel::resman().contains(model_resource)) {
        builder.add_resource(model_resource, PreviewLoadResourceStatus::missing, std::move(origin));
        return;
    }

    auto* mdl = nw::kernel::models().load(model_resref);
    if (!mdl) {
        builder.add_resource(model_resource, PreviewLoadResourceStatus::missing, std::move(origin));
        builder.add_event(PreviewLoadEventSeverity::error,
            "model",
            fmt::format("Failed to load model '{}'", model_resref));
        return;
    }

    add_unique_report_model_name(report,
        mdl->model.name.empty() ? std::string_view{model_resref} : std::string_view{mdl->model.name});
    scan_preview_model_dependencies(builder, *mdl, scanned_models);
    add_nwn_model_asset_reports(report,
        *mdl,
        mdl->model.name.empty() ? std::string_view{model_resref} : std::string_view{mdl->model.name});
    add_model_particle_report(report, builder, *mdl, animation_name);
}

static void add_area_door_report(
    PreviewLoadReport& report,
    PreviewLoadReportBuilder& builder,
    std::set<std::string>& scanned_models,
    const nw::Door& door,
    std::string_view origin,
    std::string_view animation_name)
{
    auto model_ref = nw::resolve_door_model(door);
    if (!model_ref.valid()) {
        builder.add_event(PreviewLoadEventSeverity::warning,
            "door",
            fmt::format("{}: {}", origin, model_ref.error));
        return;
    }

    add_report_model(report, builder, scanned_models, model_ref.model.view(),
        fmt::format("{}:{}", origin, door_model_lookup_context(model_ref)), animation_name);
}

static void add_area_placeable_report(
    PreviewLoadReport& report,
    PreviewLoadReportBuilder& builder,
    std::set<std::string>& scanned_models,
    const nw::Placeable& placeable,
    std::string_view origin,
    std::string_view animation_name)
{
    if (placeable.appearance > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
        builder.add_event(PreviewLoadEventSeverity::warning,
            "placeable",
            fmt::format("{}: appearance {} is invalid", origin, placeable.appearance));
        return;
    }

    const auto* info = nw::kernel::rules().placeables.get(
        nw::PlaceableType::make(static_cast<int32_t>(placeable.appearance)));
    if (!info) {
        builder.add_event(PreviewLoadEventSeverity::warning,
            "placeable",
            fmt::format("{}: appearance {} was not found in placeables.2da", origin, placeable.appearance));
        return;
    }
    if (info->model.empty()) {
        builder.add_event(PreviewLoadEventSeverity::warning,
            "placeable",
            fmt::format("{}: appearance {} has no model in placeables.2da", origin, placeable.appearance));
        return;
    }

    add_report_model(report, builder, scanned_models, info->model.view(),
        fmt::format("{}:appearance:{}", origin, placeable.appearance), animation_name);
}

static void add_item_model_report(
    PreviewLoadReport& report,
    PreviewLoadReportBuilder& builder,
    std::set<std::string>& scanned_models,
    const nw::Item& item,
    std::string_view origin_prefix,
    bool use_default_fallback,
    std::string_view animation_name)
{
    const std::string item_resref = item.common.resref.string();
    if (!item_resref.empty()) {
        builder.add_existing_or_missing_resource({item_resref, nw::ResourceType::uti},
            fmt::format("{}:item", origin_prefix));
    }

    auto* baseitem = nw::kernel::rules().baseitems.get(item.baseitem);
    if (!baseitem) {
        builder.add_event(PreviewLoadEventSeverity::error,
            "item",
            fmt::format("{} item '{}' has invalid baseitem {}",
                origin_prefix,
                item.common.resref,
                *item.baseitem));
        return;
    }

    auto try_add = [&](std::string_view resref, nw::ItemModelParts::type part) {
        if (resref.empty()) {
            return false;
        }
        const bool found = nw::kernel::resman().contains({nw::Resref{resref}, nw::ResourceType::mdl});
        add_report_model(report,
            builder,
            scanned_models,
            resref,
            fmt::format("{}:{}:part{}", origin_prefix, item.common.resref.view(), static_cast<int>(part)),
            animation_name);
        return found;
    };

    bool found_item_model = false;
    for (const auto& model_part : resolve_item_model_parts(item, *baseitem)) {
        found_item_model = try_add(model_part.resref, model_part.part) || found_item_model;
    }

    if (item.model_type == nw::ItemModelType::armor) {
        if (use_default_fallback) {
            builder.add_event(PreviewLoadEventSeverity::warning,
                "item",
                fmt::format("{} standalone armor item '{}' is only fully resolved through a creature/player preview",
                    origin_prefix,
                    item.common.resref));
        }
    }

    if (use_default_fallback && !found_item_model && baseitem->default_model.valid()) {
        (void)try_add(baseitem->default_model.resref.view(), nw::ItemModelParts::model1);
    }
}

static void report_preview_equipment(
    nw::Creature& creature,
    PreviewLoadReportBuilder& builder,
    const std::filesystem::path& path)
{
    std::array<nw::Resref, 18> requested{};
    for (size_t i = 0; i < creature.equipment.equips.size(); ++i) {
        const auto& equip = creature.equipment.equips[i];
        if (equip.is<nw::Resref>() && equip.as<nw::Resref>().length()) {
            requested[i] = equip.as<nw::Resref>();
            builder.add_existing_or_missing_resource(
                {requested[i], nw::ResourceType::uti},
                fmt::format("{}:equipment:{}", path.string(), nw::equip_index_to_string(static_cast<nw::EquipIndex>(i))));
        }
    }

    if (!creature.equipment.instantiate()) {
        builder.add_event(PreviewLoadEventSeverity::warning,
            "equipment",
            fmt::format("Dynamic creature preview '{}': failed to instantiate equipment", path.string()));
    }

    for (size_t i = 0; i < requested.size(); ++i) {
        if (!requested[i].length()) {
            continue;
        }
        const auto slot = static_cast<nw::EquipIndex>(i);
        if (!equipped_item(creature.equipment, slot)) {
            builder.add_event(PreviewLoadEventSeverity::warning,
                "equipment",
                fmt::format("Dynamic creature preview '{}': missing equipped item '{}.uti' for slot '{}'",
                    path.string(),
                    requested[i],
                    nw::equip_index_to_string(slot)));
        }
    }
}

static void add_dynamic_creature_report(
    PreviewLoadReport& report,
    PreviewLoadReportBuilder& builder,
    std::set<std::string>& scanned_models,
    nw::Creature& creature,
    const std::filesystem::path& path,
    std::string_view animation_name)
{
    creature.update_appearance(creature.appearance.id);
    report_preview_equipment(creature, builder, path);

    nw::Appearance appearance_id = creature.appearance.id;
    uint8_t gender = creature.gender;
    uint32_t wings = creature.appearance.wings;
    uint32_t tail = creature.appearance.tail;
    nw::BodyParts body_parts = creature.appearance.body_parts;
    nw::Item* chest_item = equipped_item(creature.equipment, nw::EquipIndex::chest);
    nw::Item* cloak_item = equipped_item(creature.equipment, nw::EquipIndex::cloak);
    nw::Item* head_item = equipped_item(creature.equipment, nw::EquipIndex::head);
    nw::Item* righthand_item = equipped_item(creature.equipment, nw::EquipIndex::righthand);
    nw::Item* lefthand_item = equipped_item(creature.equipment, nw::EquipIndex::lefthand);

    auto* app = nw::kernel::rules().appearances.get(appearance_id);
    auto* appearance_tda = nw::kernel::twodas().get("appearance");
    if (!app || !appearance_tda) {
        builder.add_event(PreviewLoadEventSeverity::error,
            "appearance",
            fmt::format("Dynamic creature preview '{}' has invalid appearance {}", path.string(), appearance_id.idx()));
        return;
    }

    std::string race;
    appearance_tda->get_to(*appearance_id, "RACE", race);
    int phenotype = 0;
    const char sex = gender == 1 ? 'f' : 'm';
    body_parts = normalized_body_parts(body_parts);

    if (app->model_type == "P") {
        if (auto base_rig_resref = resolve_creature_base_rig(*app, race, sex)) {
            add_report_model(report, builder, scanned_models, *base_rig_resref,
                fmt::format("{}:base_rig", path.string()), animation_name);
        } else {
            builder.add_event(PreviewLoadEventSeverity::warning,
                "creature",
                fmt::format("No dynamic creature base rig resolved for appearance {}", appearance_id.idx()));
        }

        const auto resolved_body_parts = resolve_humanoid_body_part_models(
            sex, race, phenotype, body_parts, creature_plt_colors(creature.appearance), chest_item, head_item);
        for (const auto& part : resolved_body_parts) {
            if (part.resref) {
                add_report_model(report, builder, scanned_models, *part.resref,
                    fmt::format("{}:body_part:{}", path.string(), part.token), animation_name);
            } else if (part.missing_requested_part) {
                builder.add_event(PreviewLoadEventSeverity::warning,
                    "creature",
                    fmt::format("Dynamic creature body part '{}' model part {} was not found",
                        part.token,
                        part.model_part));
            }
        }

        if (chest_item && chest_item->model_type == nw::ItemModelType::armor) {
            auto robe_part = chest_item->model_parts[nw::ItemModelParts::armor_robe];
            if (robe_part > 0) {
                if (auto robe_resref = resolve_creature_part_model(sex, race, phenotype, {"robe"}, robe_part)) {
                    add_report_model(report, builder, scanned_models, *robe_resref,
                        fmt::format("{}:robe", path.string()), animation_name);
                } else {
                    builder.add_event(PreviewLoadEventSeverity::warning,
                        "creature",
                        fmt::format("Dynamic creature robe model part {} was not found", robe_part));
                }
            }
        }
    } else {
        add_report_model(report, builder, scanned_models, app->model_name,
            fmt::format("{}:appearance", path.string()), animation_name);
    }

    auto add_attachment = [&](const char* table_name, uint32_t row) {
        const auto lookup = resolve_creature_attachment_model_lookup(table_name, row);
        if (!lookup.requested || lookup.null_model) {
            return;
        }
        if (!lookup.warning.empty()) {
            builder.add_event(PreviewLoadEventSeverity::warning,
                "attachment",
                lookup.warning);
            return;
        }
        if (lookup.resolved) {
            add_report_model(report, builder, scanned_models, lookup.model_name,
                fmt::format("{}:attachment:{}:{}", path.string(), table_name, row), animation_name);
            if (table_name == std::string_view{"wingmodel"}) {
                const auto policy = resolve_nwn_wing_attachment_visual_policy(appearance_id, row);
                if (policy.strip_non_render_meshes) {
                    size_t stripped_mesh_count = 0;
                    if (auto* mdl = nw::kernel::models().load(lookup.model_name)) {
                        stripped_mesh_count = count_nwn_wing_attachment_visual_policy_stripped_meshes(*mdl, policy);
                    }
                    builder.add_event(PreviewLoadEventSeverity::info,
                        "nwn_wing_attachment_policy",
                        fmt::format(
                            "{}: wingmodel row {} stripped_non_render_meshes={}",
                            path.string(),
                            row,
                            stripped_mesh_count));
                }
            }
        }
    };
    add_attachment("wingmodel", wings);
    add_attachment("tailmodel", tail);

    if (cloak_item && cloak_item->model_type == nw::ItemModelType::layered) {
        auto* cloakmodel = nw::kernel::twodas().get("cloakmodel");
        int cloak_model = 0;
        if (!cloakmodel) {
            builder.add_event(PreviewLoadEventSeverity::warning,
                "cloak",
                "Dynamic creature cloak model table was not loaded");
        } else if (cloakmodel->get_to(cloak_item->model_parts[nw::ItemModelParts::model1], "MODEL", cloak_model)
            && cloak_model > 0) {
            if (auto cloak_resref = resolve_creature_cloak_model(sex, race, phenotype, static_cast<uint16_t>(cloak_model))) {
                add_report_model(report, builder, scanned_models, *cloak_resref,
                    fmt::format("{}:cloak", path.string()), animation_name);
            } else {
                builder.add_event(PreviewLoadEventSeverity::warning,
                    "cloak",
                    fmt::format("Dynamic creature cloak model part {} was not found", cloak_model));
            }
        } else {
            builder.add_event(PreviewLoadEventSeverity::warning,
                "cloak",
                fmt::format("Dynamic creature cloak item '{}' model part {} has no cloakmodel.2da MODEL",
                    cloak_item->common.resref,
                    cloak_item->model_parts[nw::ItemModelParts::model1]));
        }
    }

    if (righthand_item) {
        add_item_model_report(report, builder, scanned_models, *righthand_item,
            fmt::format("{}:equipment:{}", path.string(), nw::equip_index_to_string(nw::EquipIndex::righthand)),
            false, animation_name);
    }
    if (lefthand_item) {
        add_item_model_report(report, builder, scanned_models, *lefthand_item,
            fmt::format("{}:equipment:{}", path.string(), nw::equip_index_to_string(nw::EquipIndex::lefthand)),
            false, animation_name);
    }
    if (head_item) {
        add_item_model_report(report, builder, scanned_models, *head_item,
            fmt::format("{}:equipment:{}", path.string(), nw::equip_index_to_string(nw::EquipIndex::head)),
            false, animation_name);
    }
}

static void finalize_load_report(PreviewLoadReport& report, PreviewLoadReportBuilder& builder)
{
    for (const auto& resource : report.resources) {
        if (resource.status != PreviewLoadResourceStatus::missing) {
            continue;
        }
        builder.add_event(PreviewLoadEventSeverity::warning,
            "resource",
            fmt::format("Missing resource '{}'", resource.resource.filename()));
    }
}

PreviewLoadReport build_preview_load_report(std::string_view source, std::string_view animation_name)
{
    PreviewLoadReport report{};
    report.source = std::string{source};
    report.kind = "model";

    PreviewLoadReportBuilder builder{report};
    std::set<std::string> scanned_models;

    auto path = std::filesystem::path{source};
    nw::String ext = path.extension().string();
    nw::string::tolower(&ext);

    if ((ext == ".gltf" || ext == ".glb") && std::filesystem::exists(path)) {
        report.kind = "gltf";
        builder.add_event(PreviewLoadEventSeverity::info,
            "gltf",
            "glTF dependency reports do not require preview resource initialization, but only NWN resource dependency scanning is currently implemented");
        finalize_load_report(report, builder);
        return report;
    }

    if (is_object_preview_path(source)) {
        add_source_resource(builder, path);
        const nw::Resource resource = nw::Resource::from_path(path, false);
        if (resource.type == nw::ResourceType::bic) {
            report.kind = "dynamic_creature";
            nw::Player player;
            if (load_player_from_file(path, player)) {
                add_dynamic_creature_report(report, builder, scanned_models, player, path, animation_name);
            } else {
                builder.add_event(PreviewLoadEventSeverity::error,
                    "source",
                    fmt::format("Failed to load player preview '{}'", path.string()));
            }
            finalize_load_report(report, builder);
            return report;
        }
        if (resource.type == nw::ResourceType::utc) {
            report.kind = "dynamic_creature";
            nw::Creature creature;
            if (load_creature_from_file(path, creature)) {
                add_dynamic_creature_report(report, builder, scanned_models, creature, path, animation_name);
            } else {
                builder.add_event(PreviewLoadEventSeverity::error,
                    "source",
                    fmt::format("Failed to load creature preview '{}'", path.string()));
            }
            finalize_load_report(report, builder);
            return report;
        }
        if (resource.type == nw::ResourceType::uti) {
            report.kind = "item";
            nw::Item item;
            if (load_item_from_file(path, item)) {
                add_item_model_report(report, builder, scanned_models, item, path.string(), true, animation_name);
            } else {
                builder.add_event(PreviewLoadEventSeverity::error,
                    "source",
                    fmt::format("Failed to load item preview '{}'", path.string()));
            }
            finalize_load_report(report, builder);
            return report;
        }
        if (resource.type == nw::ResourceType::utd) {
            report.kind = "Door";
            nw::Door door;
            if (load_door_from_file(path, door)) {
                if (auto model_ref = resolve_door_model_reference(door, path)) {
                    add_report_model(report, builder, scanned_models, model_ref->model.view(),
                        fmt::format("{}:{}", path.string(), door_model_lookup_context(*model_ref)), animation_name);
                }
            } else {
                builder.add_event(PreviewLoadEventSeverity::error,
                    "source",
                    fmt::format("Failed to load door preview '{}'", path.string()));
            }
            finalize_load_report(report, builder);
            return report;
        }
        if (resource.type == nw::ResourceType::utp) {
            report.kind = "Placeable";
            nw::Placeable placeable;
            if (load_placeable_from_file(path, placeable)) {
                if (placeable.appearance > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
                    builder.add_event(PreviewLoadEventSeverity::error,
                        "placeable",
                        fmt::format("Placeable preview '{}' appearance {} is invalid",
                            path.string(),
                            placeable.appearance));
                } else if (const auto* info = nw::kernel::rules().placeables.get(
                               nw::PlaceableType::make(static_cast<int32_t>(placeable.appearance)))) {
                    add_report_model(report, builder, scanned_models, info->model.view(),
                        fmt::format("{}:appearance:{}", path.string(), placeable.appearance), animation_name);
                } else {
                    builder.add_event(PreviewLoadEventSeverity::error,
                        "placeable",
                        fmt::format("Placeable preview '{}' appearance {} was not found in placeables.2da",
                            path.string(),
                            placeable.appearance));
                }
            } else {
                builder.add_event(PreviewLoadEventSeverity::error,
                    "source",
                    fmt::format("Failed to load placeable preview '{}'", path.string()));
            }
            finalize_load_report(report, builder);
            return report;
        }
    }

    if (ext == ".json" && std::filesystem::exists(path)) {
        report.kind = "particle_json";
        std::ifstream in{path};
        if (!in) {
            builder.add_event(PreviewLoadEventSeverity::error,
                "source",
                fmt::format("Failed to open particle JSON '{}'", path.string()));
            finalize_load_report(report, builder);
            return report;
        }

        nw::render::ParticleEffectDef effect;
        std::string parse_error;
        if (!nw::render::try_load_particle_effect_json(in, effect, &parse_error)) {
            builder.add_event(PreviewLoadEventSeverity::error,
                "source",
                fmt::format("Failed to load particle JSON '{}': {}", path.string(), parse_error));
            finalize_load_report(report, builder);
            return report;
        }

        nw::model::ParticleImportResult import{};
        import.effect = std::move(effect);
        auto compiled = nw::render::compile_particle_effect(import.effect);
        add_particle_report(report, builder, path.string(), import, compiled);
        finalize_load_report(report, builder);
        return report;
    }

    const nw::Resref area{source};
    if (nw::kernel::resman().contains({area, nw::ResourceType::are})
        && !nw::kernel::resman().contains({area, nw::ResourceType::mdl})) {
        report.kind = "area";
        builder.add_existing_or_missing_resource({area, nw::ResourceType::are}, "area");
        builder.add_existing_or_missing_resource({area, nw::ResourceType::git}, "area");
        builder.add_existing_or_missing_resource({area, nw::ResourceType::gic}, "area");

        nw::Gff are{nw::kernel::resman().demand({area, nw::ResourceType::are})};
        nw::Resref tileset_resref;
        if (!are.valid() || !are.toplevel().get_to("Tileset", tileset_resref) || tileset_resref.empty()) {
            builder.add_event(PreviewLoadEventSeverity::error,
                "area",
                fmt::format("Area '{}' is missing a valid tileset", source));
            finalize_load_report(report, builder);
            return report;
        }
        builder.add_existing_or_missing_resource({tileset_resref, nw::ResourceType::set}, "area:tileset");

        nw::ObjectManager::AreaLoadProfile profile{};
        auto* loaded_area = nw::kernel::objects().make_area(area, &profile);
        if (!loaded_area) {
            builder.add_event(PreviewLoadEventSeverity::error,
                "area",
                fmt::format("Failed to load area '{}'", source));
            finalize_load_report(report, builder);
            return report;
        }

        bool instantiated = false;
        try {
            instantiated = loaded_area->instantiate() && loaded_area->tileset;
        } catch (const std::exception& ex) {
            builder.add_event(PreviewLoadEventSeverity::error,
                "area",
                fmt::format("Failed to instantiate area '{}': {}", source, ex.what()));
        }

        if (instantiated) {
            std::set<std::string> tile_models;
            for (const auto& tile : loaded_area->tiles) {
                if (tile.id < 0 || static_cast<size_t>(tile.id) >= loaded_area->tileset->tiles.size()) {
                    continue;
                }
                const auto& tile_def = loaded_area->tileset->tiles[static_cast<size_t>(tile.id)];
                if (!tile_def.model.empty() && tile_models.insert(tile_def.model).second) {
                    add_report_model(report, builder, scanned_models, tile_def.model,
                        fmt::format("{}:tile", source), animation_name);
                }
            }
            for (size_t i = 0; i < loaded_area->creatures.size(); ++i) {
                auto* creature = loaded_area->creatures[i];
                if (!creature) {
                    continue;
                }
                const auto origin = area_object_origin(source, "creature", i, creature->common);
                add_dynamic_creature_report(report, builder, scanned_models, *creature,
                    std::filesystem::path{origin}, animation_name);
            }
            for (size_t i = 0; i < loaded_area->doors.size(); ++i) {
                const auto* door = loaded_area->doors[i];
                if (!door) {
                    continue;
                }
                add_area_door_report(report, builder, scanned_models, *door,
                    area_object_origin(source, "door", i, door->common), animation_name);
            }
            for (size_t i = 0; i < loaded_area->items.size(); ++i) {
                const auto* item = loaded_area->items[i];
                if (!item) {
                    continue;
                }
                add_item_model_report(report, builder, scanned_models, *item,
                    area_object_origin(source, "item", i, item->common), true, animation_name);
            }
            for (size_t i = 0; i < loaded_area->placeables.size(); ++i) {
                const auto* placeable = loaded_area->placeables[i];
                if (!placeable) {
                    continue;
                }
                add_area_placeable_report(report, builder, scanned_models, *placeable,
                    area_object_origin(source, "placeable", i, placeable->common), animation_name);
            }
        } else if (report.error_count() == 0) {
            builder.add_event(PreviewLoadEventSeverity::error,
                "area",
                fmt::format("Failed to instantiate area '{}'", source));
        }

        nw::kernel::objects().destroy(loaded_area->handle());
        finalize_load_report(report, builder);
        return report;
    }

    add_report_model(report, builder, scanned_models, source, "source", animation_name);
    if (report.model_names.empty() && report.error_count() == 0) {
        builder.add_event(PreviewLoadEventSeverity::error,
            "source",
            fmt::format("Preview source '{}' produced no model report", source));
    }
    finalize_load_report(report, builder);
    return report;
}

namespace {

nw::render::ModelAssetTextureUploadDesc preview_model_asset_texture_upload_desc(PreviewRenderResources& resources)
{
    return nw::render::ModelAssetTextureUploadDesc{
        .ctx = resources.context(),
        .fallback_albedo = resources.fallback_albedo_index(),
        .fallback_normal = resources.fallback_normal_index(),
        .fallback_surface = resources.fallback_surface_index(),
        .fallback_emissive = resources.fallback_emissive_index(),
    };
}

ImportGltfDesc preview_import_gltf_desc(PreviewRenderResources& resources)
{
    const auto texture_upload = preview_model_asset_texture_upload_desc(resources);
    return ImportGltfDesc{
        .ctx = texture_upload.ctx,
        .fallback_albedo = texture_upload.fallback_albedo,
        .fallback_normal = texture_upload.fallback_normal,
        .fallback_surface = texture_upload.fallback_surface,
        .fallback_emissive = texture_upload.fallback_emissive,
    };
}

void collect_model_asset_upload_events(
    std::vector<PreviewLoadEvent>& events,
    std::string_view source,
    std::string_view category_prefix,
    const nw::render::ModelAssetUploadStats& geometry,
    const nw::render::ModelAssetTextureUploadStats& textures)
{
    if (!geometry.passed()) {
        add_preview_load_event(events,
            PreviewLoadEventSeverity::warning,
            fmt::format("{}_geometry_upload", category_prefix),
            fmt::format(
                "{}: primitives={} uploaded={} invalid={} invalid_asset_rows={} invalid_material_texture_bindings={} missing_context={} buffer_failures={}",
                source,
                geometry.primitive_count,
                geometry.uploaded_primitive_count,
                geometry.invalid_primitive_count,
                geometry.invalid_asset_row_count,
                geometry.invalid_material_texture_binding_count,
                geometry.missing_context_count,
                geometry.buffer_failure_count));
    }

    const bool texture_upload_incomplete = !textures.passed();
    if (texture_upload_incomplete || textures.fallback_material_count != 0) {
        add_preview_load_event(events,
            texture_upload_incomplete ? PreviewLoadEventSeverity::warning : PreviewLoadEventSeverity::info,
            fmt::format("{}_texture_upload", category_prefix),
            fmt::format(
                "{}: materials={} sources={} uploaded={} fallback_materials={} invalid_bindings={} missing_context={} missing_payloads={} decode_failures={} size_mismatches={} create_failures={} upload_failures={} bindless_failures={}",
                source,
                textures.material_count,
                textures.texture_source_count,
                textures.uploaded_texture_count,
                textures.fallback_material_count,
                textures.invalid_material_texture_binding_count,
                textures.missing_context_count,
                textures.missing_source_payload_count,
                textures.decode_failure_count,
                textures.surface_size_mismatch_count,
                textures.texture_create_failure_count,
                textures.texture_upload_failure_count,
                textures.bindless_failure_count));
    }
}

void collect_nwn_render_model_import_events(
    std::vector<PreviewLoadEvent>& events,
    std::string_view source,
    const nw::render::nwn::NwnRenderModelImportResult& result)
{
    add_preview_load_event(events,
        PreviewLoadEventSeverity::info,
        "nwn_render_model_path",
        fmt::format(
            "{}: static ModelAsset preview path; PLT recolor, emitters, full attachment policy, and cross-skeleton policy remain on the legacy path",
            source));

    const auto& import = result.import_stats;
    if (!import.complete()) {
        add_preview_load_event(events,
            PreviewLoadEventSeverity::warning,
            "nwn_render_model_import",
            fmt::format(
                "{}: skipped_empty_meshes={} skipped_skin_meshes={} unsupported_specular_textures={} unsupported_plt_textures={} missing_textures={} texture_source_overflows={} deformer_overflows={} primitive_overflows={}",
                source,
                import.skipped_empty_mesh_count,
                import.skipped_skin_mesh_count,
                import.unsupported_specular_texture_count,
                import.unsupported_plt_texture_count,
                import.missing_texture_source_count,
                import.texture_source_overflow_count,
                import.deformer_overflow_count,
                import.primitive_overflow_count));
    }
    if (import.water_name_heuristic_count != 0 || import.foliage_name_heuristic_count != 0) {
        add_preview_load_event(events,
            PreviewLoadEventSeverity::info,
            "nwn_render_model_name_policy",
            fmt::format(
                "{}: name_policy water_heuristics={} foliage_heuristics={}",
                source,
                import.water_name_heuristic_count,
                import.foliage_name_heuristic_count));
    }

    collect_model_asset_upload_events(
        events, source, "nwn_render_model", result.geometry_upload_stats, result.texture_upload_stats);
}

void collect_gltf_model_asset_import_events(
    std::vector<PreviewLoadEvent>& events,
    std::string_view source,
    const nw::render::gltf::GltfRenderModelImportResult& result)
{
    add_preview_load_event(events,
        PreviewLoadEventSeverity::info,
        "gltf_model_asset_path",
        fmt::format("{}: glTF ModelAsset preview path", source));

    if (!result.asset_imported) {
        add_preview_load_event(events,
            PreviewLoadEventSeverity::error,
            "gltf_model_asset_import",
            fmt::format("{}: CPU ModelAsset import failed", source));
        return;
    }

    collect_model_asset_upload_events(
        events, source, "gltf_model_asset", result.geometry_upload_stats, result.texture_upload_stats);
}

void log_model_asset_upload_gaps(
    std::string_view label,
    std::string_view source,
    const nw::render::ModelAssetUploadStats& geometry,
    const nw::render::ModelAssetTextureUploadStats& textures)
{
    if (!geometry.passed()) {
        LOG_F(WARNING,
            "{} geometry upload '{}' incomplete: primitives={} uploaded={} invalid={} "
            "invalid_asset_rows={} invalid_material_texture_bindings={} missing_context={} buffer_failures={}",
            label,
            source,
            geometry.primitive_count,
            geometry.uploaded_primitive_count,
            geometry.invalid_primitive_count,
            geometry.invalid_asset_row_count,
            geometry.invalid_material_texture_binding_count,
            geometry.missing_context_count,
            geometry.buffer_failure_count);
        log_preview_warning_context();
    }

    if (!textures.passed()) {
        LOG_F(WARNING,
            "{} texture upload '{}' incomplete: materials={} sources={} uploaded={} "
            "fallback_materials={} invalid_bindings={} missing_context={} missing_payloads={} "
            "decode_failures={} size_mismatches={} create_failures={} upload_failures={} bindless_failures={}",
            label,
            source,
            textures.material_count,
            textures.texture_source_count,
            textures.uploaded_texture_count,
            textures.fallback_material_count,
            textures.invalid_material_texture_binding_count,
            textures.missing_context_count,
            textures.missing_source_payload_count,
            textures.decode_failure_count,
            textures.surface_size_mismatch_count,
            textures.texture_create_failure_count,
            textures.texture_upload_failure_count,
            textures.bindless_failure_count);
        log_preview_warning_context();
    }
}

void log_nwn_render_model_import_gaps(
    std::string_view source, const nw::render::nwn::NwnRenderModelImportResult& result)
{
    const auto& import = result.import_stats;
    if (!import.complete()) {
        LOG_F(WARNING,
            "NWN RenderModel import '{}' has gaps: skipped_empty_meshes={} skipped_skin_meshes={} "
            "unsupported_specular_textures={} unsupported_plt_textures={} missing_textures={} "
            "texture_source_overflows={} deformer_overflows={} primitive_overflows={}",
            source,
            import.skipped_empty_mesh_count,
            import.skipped_skin_mesh_count,
            import.unsupported_specular_texture_count,
            import.unsupported_plt_texture_count,
            import.missing_texture_source_count,
            import.texture_source_overflow_count,
            import.deformer_overflow_count,
            import.primitive_overflow_count);
        log_preview_warning_context();
    }
    if (import.water_name_heuristic_count != 0 || import.foliage_name_heuristic_count != 0) {
        LOG_F(INFO,
            "NWN RenderModel import '{}' used name policy: water_heuristics={} foliage_heuristics={}",
            source,
            import.water_name_heuristic_count,
            import.foliage_name_heuristic_count);
    }

    log_model_asset_upload_gaps(
        "NWN RenderModel", source, result.geometry_upload_stats, result.texture_upload_stats);
}

RenderModelPreviewLoad load_gltf_model_asset_preview(PreviewRenderResources& resources, const std::filesystem::path& path)
{
    auto result = nw::render::gltf::import_gltf_render_model_from_asset(path, preview_import_gltf_desc(resources));

    const auto path_text = path.string();
    if (result.asset_imported) {
        log_model_asset_upload_gaps(
            "glTF ModelAsset RenderModel", path_text, result.geometry_upload_stats, result.texture_upload_stats);
    }

    RenderModelPreviewLoad load{};
    collect_gltf_model_asset_import_events(load.events, path_text, result);
    if (!result.model) {
        LOG_F(ERROR, "Failed to import glTF ModelAsset preview '{}'", path_text);
        log_preview_error_context();
        return load;
    }

    load.model = std::move(result.model);
    return load;
}

RenderModelPreviewLoad load_nwn_render_model_preview(
    PreviewRenderResources& resources, const nw::model::Mdl& mdl, std::string_view source)
{
    auto result = nw::render::nwn::import_nwn_render_model(
        mdl, preview_model_asset_texture_upload_desc(resources));
    log_nwn_render_model_import_gaps(source, result);
    RenderModelPreviewLoad load{};
    collect_nwn_render_model_import_events(load.events, source, result);
    load.static_deformer_count = result.import_stats.secondary_motion_deformer_count
        + result.import_stats.legacy_reference_deformer_count;
    if (!result.model) {
        LOG_F(ERROR, "Failed to import NWN RenderModel preview '{}'", source);
        log_preview_error_context();
        return load;
    }
    load.model = std::move(result.model);
    return load;
}

RenderModelPreviewLoad load_nwn_render_model_preview(
    PreviewRenderResources& resources, std::string_view source)
{
    auto source_path = std::filesystem::path{source};
    nw::String ext = source_path.extension().string();
    nw::string::tolower(&ext);

    if (ext == ".mdl" && std::filesystem::exists(source_path)) {
        const auto path_text = source_path.string();
        ERRARE("[viewer] loading local NWN RenderModel '{}'", std::string_view{path_text});
        nw::model::Mdl mdl{source_path};
        if (!mdl.valid()) {
            LOG_F(ERROR, "Failed to parse local NWN model '{}'", path_text);
            log_preview_error_context();
            return {};
        }
        return load_nwn_render_model_preview(resources, mdl, path_text);
    }

    ERRARE("[viewer] loading NWN RenderModel '{}'", source);
    auto* mdl = nw::kernel::models().load(source);
    if (!mdl) {
        LOG_F(ERROR, "Failed to load NWN RenderModel source '{}'", source);
        log_preview_error_context();
        return {};
    }
    return load_nwn_render_model_preview(resources, *mdl, source);
}

std::vector<PreviewLoadEvent> maybe_add_nwn_preview_model(
    PreviewScene& scene, PreviewRenderResources& resources, std::string_view source, PreviewSceneLoadOptions options)
{
    if (options.nwn_model_path == NwnModelPreviewPath::render_model) {
        auto load = load_nwn_render_model_preview(resources, source);
        if (load.static_deformer_count != 0u) {
            add_preview_load_event(load.events,
                PreviewLoadEventSeverity::warning,
                "nwn_render_model_static_deformer",
                fmt::format(
                    "{}: rendering {} legacy deformed primitives as static source geometry on the common path",
                    source,
                    load.static_deformer_count));
        }
        scene.add(std::move(load.model));
        return std::move(load.events);
    }

    maybe_add_model(scene, load_model_with_plt(resources, source));
    return {};
}

bool scene_has_preview_model(const PreviewScene& scene) noexcept
{
    return !scene.models.empty() || !scene.static_models.empty();
}

} // namespace

std::unique_ptr<PreviewScene> load_preview_scene(PreviewRenderResources& resources, std::string_view source)
{
    return load_preview_scene(resources, source, default_preview_scene_load_options());
}

std::unique_ptr<PreviewScene> load_preview_scene(
    PreviewRenderResources& resources, std::string_view source, PreviewSceneLoadOptions options)
{
    ERRARE("[viewer] loading preview source '{}'", source);

    auto path = std::filesystem::path{source};
    nw::String ext = path.extension().string();
    nw::string::tolower(&ext);

    if ((ext == ".gltf" || ext == ".glb") && std::filesystem::exists(path)) {
        auto scene = std::make_unique<PreviewScene>();
        auto load = load_gltf_model_asset_preview(resources, path);
        auto model = std::move(load.model);
        if (!model) {
            LOG_F(ERROR, "Failed to load glTF preview '{}'", path.string());
            log_preview_error_context();
            return {};
        }
        scene->add(std::move(model));
        scene->rebuild_load_report(path.string(), "gltf");
        append_scene_load_events(*scene, load.events);
        return scene;
    }

    if (is_object_preview_path(source)) {
        const nw::Resource resource = nw::Resource::from_path(path, false);
        if (resource.type == nw::ResourceType::bic || resource.type == nw::ResourceType::utc) {
            return load_dynamic_creature_scene(resources, path, options);
        }
        if (resource.type == nw::ResourceType::uti) {
            return load_item_scene(resources, path);
        }
        if (resource.type == nw::ResourceType::utd) {
            return load_door_scene(resources, path);
        }
        if (resource.type == nw::ResourceType::utp) {
            return load_placeable_scene(resources, path);
        }
    }

    if (ext == ".json" && std::filesystem::exists(path)) {
        std::ifstream in{path};
        if (!in) {
            LOG_F(ERROR, "Failed to open particle JSON '{}'", path.string());
            log_preview_error_context();
            return {};
        }

        nw::render::ParticleEffectDef effect;
        std::string parse_error;
        if (!nw::render::try_load_particle_effect_json(in, effect, &parse_error)) {
            LOG_F(ERROR, "Failed to load particle JSON '{}': {}", path.string(), parse_error);
            log_preview_error_context();
            return {};
        }

        auto scene = std::make_unique<PreviewScene>();
        scene->add_particle_effect(std::move(effect));
        scene->rebuild_load_report(path.string(), "particle_json");
        return scene;
    }

    auto scene = std::make_unique<PreviewScene>();
    auto load_events = maybe_add_nwn_preview_model(*scene, resources, source, options);
    if (!scene_has_preview_model(*scene)) {
        LOG_F(ERROR, "Preview source '{}' produced no renderable models", source);
        log_preview_error_context();
        return {};
    }
    append_scene_authored_model_lights(*scene);
    scene->rebuild_particles();
    scene->rebuild_load_report(source, "model");
    append_scene_load_events(*scene, load_events);
    return scene;
}

std::unique_ptr<PreviewScene> load_preview_scene(PreviewRenderResources& resources, std::span<const std::string> sources)
{
    return load_preview_scene(resources, sources, default_preview_scene_load_options());
}

std::unique_ptr<PreviewScene> load_preview_scene(
    PreviewRenderResources& resources, std::span<const std::string> sources, PreviewSceneLoadOptions options)
{
    ERRARE("[viewer] loading multi-source preview ({} sources)", sources.size());

    auto scene = std::make_unique<PreviewScene>();
    std::vector<PreviewLoadEvent> load_events;
    for (const auto& source : sources) {
        ERRARE("[viewer] loading preview source '{}'", std::string_view{source});
        auto source_events = maybe_add_nwn_preview_model(*scene, resources, source, options);
        load_events.insert(load_events.end(),
            std::make_move_iterator(source_events.begin()),
            std::make_move_iterator(source_events.end()));
    }
    if (!scene_has_preview_model(*scene)) {
        LOG_F(ERROR, "Multi-source preview produced no renderable models");
        log_preview_error_context();
        return {};
    }
    append_scene_authored_model_lights(*scene);
    scene->rebuild_particles();
    scene->rebuild_load_report("<multi-source>", "multi_source");
    append_scene_load_events(*scene, load_events);
    return scene;
}

std::unique_ptr<PreviewScene> load_area_scene(PreviewRenderResources& resources, std::string_view area_resref)
{
    return load_area_scene(resources, area_resref, default_preview_scene_load_options());
}

std::unique_ptr<PreviewScene> load_area_scene(
    PreviewRenderResources& resources, std::string_view area_resref, PreviewSceneLoadOptions options)
{
    const nw::Resref area{area_resref};
    if (!nw::kernel::resman().contains({area, nw::ResourceType::are})) {
        LOG_F(ERROR, "Area does not exist: {}", area_resref);
        return {};
    }

    nw::Gff are{nw::kernel::resman().demand({area, nw::ResourceType::are})};
    if (!are.valid()) {
        LOG_F(ERROR, "Area resource is invalid: {}", area_resref);
        return {};
    }

    nw::Resref tileset_resref;
    if (!are.toplevel().get_to("Tileset", tileset_resref) || tileset_resref.empty()) {
        LOG_F(ERROR, "Area is missing a valid tileset: {}", area_resref);
        return {};
    }

    if (!nw::kernel::tilesets().get(tileset_resref.view())) {
        LOG_F(ERROR, "Area tileset does not exist: {} tileset={}", area_resref, tileset_resref);
        return {};
    }

    nw::ObjectManager::AreaLoadProfile profile{};
    auto* loaded_area = nw::kernel::objects().make_area(area, &profile);
    if (!loaded_area) {
        LOG_F(ERROR, "Failed to load area: {}", area_resref);
        return {};
    }

    try {
        if (!loaded_area->instantiate() || !loaded_area->tileset) {
            LOG_F(ERROR, "Failed to instantiate area or tileset: {}", area_resref);
            nw::kernel::objects().destroy(loaded_area->handle());
            return {};
        }
    } catch (const std::exception& ex) {
        LOG_F(ERROR, "Failed to instantiate area {}: {}", area_resref, ex.what());
        nw::kernel::objects().destroy(loaded_area->handle());
        return {};
    }

    auto scene = std::make_unique<PreviewScene>();
    scene->is_area = true;
    scene->area_width = loaded_area->width;
    scene->area_height = loaded_area->height;
    scene->area_flags = loaded_area->flags;
    scene->area_weather = loaded_area->weather;
    constexpr float k_tile_size = 10.0f;
    const float height_step = loaded_area->tileset->tile_height;
    float min_x = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float min_y = std::numeric_limits<float>::max();
    float max_y = std::numeric_limits<float>::lowest();
    float max_tile_z = std::numeric_limits<float>::lowest();
    std::map<std::string, int> tile_model_counts;
    std::map<std::array<uint8_t, 4>, int> tile_light_slot_counts;
    AreaGeometryCache static_geometry_cache;
    size_t loaded_creature_models = 0;
    size_t loaded_door_models = 0;
    size_t loaded_encounter_debug_shapes = 0;
    size_t loaded_item_models = 0;
    size_t loaded_placeable_models = 0;
    size_t loaded_area_object_model_lights = 0;
    size_t loaded_tile_model_lights = 0;
    size_t tiles_with_light_slots = 0;
    size_t loaded_trigger_debug_shapes = 0;

    for (int y = 0; y < loaded_area->height; ++y) {
        for (int x = 0; x < loaded_area->width; ++x) {
            const size_t index = static_cast<size_t>(y * loaded_area->width + x);
            if (index >= loaded_area->tiles.size()) {
                continue;
            }

            const auto& tile = loaded_area->tiles[index];
            if (tile.id < 0 || static_cast<size_t>(tile.id) >= loaded_area->tileset->tiles.size()) {
                continue;
            }

            const auto& tile_def = loaded_area->tileset->tiles[static_cast<size_t>(tile.id)];
            if (tile_def.model.empty()) {
                continue;
            }
            ++tile_model_counts[tile_def.model];

            auto model = load_model_with_plt(resources, tile_def.model);
            if (!model) {
                continue;
            }

            const float world_x = static_cast<float>(x) * k_tile_size + 5.0f;
            const float world_y = static_cast<float>(y) * k_tile_size + 5.0f;
            const float world_z = static_cast<float>(tile.height) * height_step;
            min_x = std::min(min_x, world_x);
            max_x = std::max(max_x, world_x);
            min_y = std::min(min_y, world_y);
            max_y = std::max(max_y, world_y);
            max_tile_z = std::max(max_tile_z, world_z);
            const float angle = glm::radians(90.0f * static_cast<float>(tile.orientation));
            glm::mat4 placement = glm::translate(glm::mat4{1.0f}, glm::vec3{world_x, world_y, world_z});
            placement *= glm::toMat4(glm::angleAxis(angle, glm::vec3{0.0f, 0.0f, 1.0f}));

            const auto light_slots = scene_tile_light_slots(tile);
            if (has_tile_light_slots(light_slots)) {
                ++tiles_with_light_slots;
                ++tile_light_slot_counts[{
                    light_slots.main1,
                    light_slots.main2,
                    light_slots.source1,
                    light_slots.source2,
                }];
            }
            if (model->mdl_) {
                loaded_tile_model_lights += append_tile_model_lights(*scene, *model->mdl_, placement, tile, x, y);
            }

            model->scene_animation_enabled = false;
            share_static_area_model_geometry(static_geometry_cache, *model);
            model->set_placement_transform(placement);
            scene->add(std::move(model));
            if (!scene->area_model_info.empty()) {
                scene->area_model_info.back() = AreaRenderSourceInfo{
                    .kind = AreaRenderRecordKind::tile,
                    .tile_x = static_cast<int16_t>(x),
                    .tile_y = static_cast<int16_t>(y),
                    .tile_orientation = static_cast<uint8_t>(tile.orientation),
                    .static_candidate = true,
                };
            }
        }
    }

    for (size_t i = 0; i < loaded_area->creatures.size(); ++i) {
        auto* creature = loaded_area->creatures[i];
        if (!creature) {
            continue;
        }

        const auto origin = area_object_origin(area_resref, "creature", i, creature->common);
        auto creature_scene = load_area_creature_scene(resources, *creature, origin, options);
        if (creature_scene) {
            const size_t light_count_before = scene->local_lights.size();
            loaded_creature_models += append_scene_models(*scene, *creature_scene, AreaRenderSourceInfo{
                                                                                       .kind = AreaRenderRecordKind::creature,
                                                                                   });
            loaded_area_object_model_lights += scene->local_lights.size() - light_count_before;
        }
    }

    for (size_t i = 0; i < loaded_area->doors.size(); ++i) {
        const auto* door = loaded_area->doors[i];
        if (!door) {
            continue;
        }

        const auto origin = area_object_origin(area_resref, "door", i, door->common);
        auto model_ref = nw::resolve_door_model(*door);
        if (!model_ref.valid()) {
            LOG_F(WARNING, "Area door '{}' has no render model: {}", origin, model_ref.error);
            continue;
        }

        auto model = load_area_object_model(resources, model_ref.model.view(), door->common.location, origin);
        if (model) {
            share_static_area_model_geometry(static_geometry_cache, *model);
            scene->add(std::move(model));
            if (!scene->area_model_info.empty()) {
                scene->area_model_info.back() = AreaRenderSourceInfo{
                    .kind = AreaRenderRecordKind::door,
                    .static_candidate = true,
                };
            }
            loaded_area_object_model_lights += append_model_authored_lights(*scene, scene->models.size() - 1);
            ++loaded_door_models;
        }
    }

    for (size_t i = 0; i < loaded_area->items.size(); ++i) {
        auto* item = loaded_area->items[i];
        if (!item) {
            continue;
        }

        const auto origin = area_object_origin(area_resref, "item", i, item->common);
        auto item_scene = load_area_item_scene(resources, *item, origin);
        if (item_scene) {
            const size_t light_count_before = scene->local_lights.size();
            loaded_item_models += append_scene_models(*scene, *item_scene, AreaRenderSourceInfo{
                                                                               .kind = AreaRenderRecordKind::item,
                                                                           });
            loaded_area_object_model_lights += scene->local_lights.size() - light_count_before;
        }
    }

    for (size_t i = 0; i < loaded_area->placeables.size(); ++i) {
        const auto* placeable = loaded_area->placeables[i];
        if (!placeable) {
            continue;
        }

        const auto origin = area_object_origin(area_resref, "placeable", i, placeable->common);
        const auto* placeable_info = resolve_area_placeable_info(*placeable, origin);
        if (!placeable_info) {
            continue;
        }
        loaded_area_object_model_lights += append_placeable_table_light(*scene, *placeable, *placeable_info);
        if (placeable_info->model.empty()) {
            LOG_F(WARNING,
                "Area placeable '{}' appearance {} has no model in placeables.2da",
                origin,
                placeable->appearance);
            continue;
        }

        auto model = load_area_object_model(resources, placeable_info->model.view(), placeable->common.location, origin);
        if (model) {
            share_static_area_model_geometry(static_geometry_cache, *model);
            scene->add(std::move(model));
            if (!scene->area_model_info.empty()) {
                scene->area_model_info.back() = AreaRenderSourceInfo{
                    .kind = AreaRenderRecordKind::placeable,
                    .static_candidate = true,
                };
            }
            loaded_area_object_model_lights += append_model_authored_lights(*scene, scene->models.size() - 1);
            ++loaded_placeable_models;
        }
    }

    for (const auto* trigger : loaded_area->triggers) {
        if (trigger && append_trigger_debug_geometry(*scene, *trigger)) {
            ++loaded_trigger_debug_shapes;
        }
    }

    for (const auto* encounter : loaded_area->encounters) {
        if (encounter && append_encounter_debug_geometry(*scene, *encounter)) {
            ++loaded_encounter_debug_shapes;
        }
    }

    float min_local_light_radius = 0.0f;
    float max_local_light_radius = 0.0f;
    float min_local_light_intensity = 0.0f;
    float max_local_light_intensity = 0.0f;
    float max_local_light_color = 0.0f;
    size_t colored_local_lights = 0;
    if (!scene->local_lights.empty()) {
        min_local_light_radius = std::numeric_limits<float>::max();
        min_local_light_intensity = std::numeric_limits<float>::max();
        for (const auto& light : scene->local_lights) {
            min_local_light_radius = std::min(min_local_light_radius, light.radius);
            max_local_light_radius = std::max(max_local_light_radius, light.radius);
            min_local_light_intensity = std::min(min_local_light_intensity, light.intensity);
            max_local_light_intensity = std::max(max_local_light_intensity, light.intensity);
            const float color = color_max_channel(light.color);
            max_local_light_color = std::max(max_local_light_color, color);
            if (color > 1.0e-4f) {
                ++colored_local_lights;
            }
        }
    }
    const SceneLocalLightTuning local_light_tuning = scene_local_light_tuning(*scene);
    const bool scene_interior = (scene->area_flags & nw::AreaFlags::interior) != nw::AreaFlags::none;
    const bool scene_underground = (scene->area_flags & nw::AreaFlags::underground) != nw::AreaFlags::none;

    LOG_F(INFO,
        "Loaded area {} tileset={} : {}x{} tiles={} loaded_models={} flags interior={} underground={} cycle={} night={} tile_lights={} object_lights={} light_slot_tiles={} local_lights={} colored_lights={} light_color_max={:.2f} light_radius=[{:.2f}..{:.2f}] light_intensity=[{:.2f}..{:.2f}] light_scale[r={:.2f}, i={:.2f}] creatures={}/{} doors={}/{} items={}/{} placeables={}/{} triggers={}/{} encounters={}/{} debug_indices={} demand={}ms deserialize={}ms placement[x={}..{}, y={}..{}]",
        area_resref,
        loaded_area->tileset_resref,
        loaded_area->width,
        loaded_area->height,
        loaded_area->tiles.size(),
        scene->models.size() + scene->static_models.size(),
        scene_interior,
        scene_underground,
        scene->area_weather.day_night_cycle,
        scene->area_weather.is_night,
        loaded_tile_model_lights,
        loaded_area_object_model_lights,
        tiles_with_light_slots,
        scene->local_lights.size(),
        colored_local_lights,
        max_local_light_color,
        min_local_light_radius,
        max_local_light_radius,
        min_local_light_intensity,
        max_local_light_intensity,
        local_light_tuning.radius_scale,
        local_light_tuning.intensity_scale,
        loaded_creature_models,
        profile.creatures,
        loaded_door_models,
        profile.doors,
        loaded_item_models,
        profile.items,
        loaded_placeable_models,
        profile.placeables,
        loaded_trigger_debug_shapes,
        profile.triggers,
        loaded_encounter_debug_shapes,
        profile.encounters,
        scene->debug_shape_indices.size(),
        profile.demand_ms,
        profile.deserialize_ms,
        min_x,
        max_x,
        min_y,
        max_y);
    for (const auto& [model, count] : tile_model_counts) {
        LOG_F(INFO, "Area tile model {} count={}", model, count);
    }
    size_t logged_light_slot_counts = 0;
    for (const auto& [slots, count] : tile_light_slot_counts) {
        if (logged_light_slot_counts >= 16) {
            LOG_F(INFO, "Area tile light slot combinations truncated after 16 of {}",
                tile_light_slot_counts.size());
            break;
        }
        LOG_F(INFO, "Area tile light slots main=({}, {}) source=({}, {}) count={}",
            slots[0],
            slots[1],
            slots[2],
            slots[3],
            count);
        ++logged_light_slot_counts;
    }

    nw::kernel::objects().destroy(loaded_area->handle());

    if (!scene_has_preview_model(*scene)) {
        return {};
    }
    scene->area_overlay_z = max_tile_z == std::numeric_limits<float>::lowest() ? 0.0f : max_tile_z;
    scene->area_render_scene = std::make_unique<AreaRenderScene>();
    scene->area_render_scene->rebuild(*scene, resources.context());
    const auto& area_cache_stats = scene->area_render_scene->stats();
    LOG_F(INFO,
        "Area render cache {} records={} static={} dynamic={} prepared_draws={} static_geometry[meshes={} vertices={} indices={} bytes={}] max_prepared_record={} chunks={}/{} max_chunk={} pass[o/w/t]={}/{}/{} shadow_casters={} source[tile/creature/door/item/placeable/unknown]={}/{}/{}/{}/{}/{}",
        area_resref,
        area_cache_stats.record_count,
        area_cache_stats.static_record_count,
        area_cache_stats.dynamic_record_count,
        area_cache_stats.prepared_draw_count,
        area_cache_stats.static_geometry_mesh_count,
        area_cache_stats.static_geometry_vertex_count,
        area_cache_stats.static_geometry_index_count,
        area_cache_stats.static_geometry_bytes,
        area_cache_stats.max_prepared_draws_per_record,
        area_cache_stats.nonempty_chunk_count,
        area_cache_stats.chunk_count,
        area_cache_stats.max_records_per_chunk,
        area_cache_stats.opaque_cutout_record_count,
        area_cache_stats.water_record_count,
        area_cache_stats.transparent_record_count,
        area_cache_stats.shadow_caster_record_count,
        area_cache_stats.tile_record_count,
        area_cache_stats.creature_record_count,
        area_cache_stats.door_record_count,
        area_cache_stats.item_record_count,
        area_cache_stats.placeable_record_count,
        area_cache_stats.unknown_record_count);
    scene->rebuild_load_report(area_resref, "area");
    return scene;
}

} // namespace nw::render::viewer
