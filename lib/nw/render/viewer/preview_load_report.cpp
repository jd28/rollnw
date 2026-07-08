#include "preview_load_report.hpp"

#include "preview_nwn_creature.hpp"
#include "preview_object.hpp"
#include "preview_plt.hpp"
#include "preview_scene.hpp"

#include <nw/formats/Image.hpp>
#include <nw/formats/Tileset.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/ModelCache.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/kernel/TwoDACache.hpp>
#include <nw/model/Mdl.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/render/particle_json.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/serialization/Gff.hpp>
#include <nw/util/string.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <unordered_map>

#include <fmt/format.h>

namespace nw::render::viewer {

namespace {

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
    const int phenotype = resolve_creature_phenotype(creature.appearance.phenotype);
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

} // namespace

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

} // namespace nw::render::viewer
