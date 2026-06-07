#include "preview_scene.hpp"

#include "preview_object.hpp"
#include "preview_plt.hpp"

#include <nw/model/Mdl.hpp>
#include <nw/render/particle_json.hpp>

#include <nw/formats/Tileset.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/ModelCache.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/kernel/TilesetRegistry.hpp>
#include <nw/kernel/TwoDACache.hpp>
#include <nw/log.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/profiles/nwn1/constants.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/serialization/Gff.hpp>
#include <nw/util/error_context.hpp>
#include <nw/util/string.hpp>

#include <algorithm>
#include <cctype>

#include <fmt/format.h>

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <unordered_map>

namespace nw::render::viewer {

namespace {

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

std::string_view preview_load_resource_status_label(PreviewLoadResourceStatus status) noexcept
{
    switch (status) {
    case PreviewLoadResourceStatus::found: return "found";
    case PreviewLoadResourceStatus::missing: return "missing";
    }
    return "unknown";
}

std::string_view preview_load_event_severity_label(PreviewLoadEventSeverity severity) noexcept
{
    switch (severity) {
    case PreviewLoadEventSeverity::info: return "info";
    case PreviewLoadEventSeverity::warning: return "warning";
    case PreviewLoadEventSeverity::error: return "error";
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

void sync_scene_particle_emitters(SceneParticleSystem& scene_particles, bool reset_previous_positions = false)
{
    if (!scene_particles.owner) {
        return;
    }

    const glm::mat4 root = scene_particles.owner->root_transform();
    const size_t count = std::min(scene_particles.import.emitter_inits.size(), scene_particles.system.emitters.size());
    for (size_t i = 0; i < count; ++i) {
        const auto& init = scene_particles.import.emitter_inits[i];
        const auto& emitter_def = scene_particles.import.effect.emitters[i];
        auto& emitter = scene_particles.system.emitters[i];
        glm::mat4 world_transform{1.0f};
        bool has_world_transform = false;

        const auto* animated_emitter = init.emitter_node_name.empty()
            ? scene_particles.owner->find(emitter_def.name)
            : scene_particles.owner->find(init.emitter_node_name);
        if (animated_emitter) {
            world_transform = root * animated_emitter->get_transform();
            has_world_transform = true;
        }

        const glm::vec3 previous_position = glm::vec3(emitter.world_transform[3]);
        if (init.has_default_transform) {
            if (has_world_transform) {
                emitter.world_transform = world_transform;
            } else {
                emitter.world_transform = root * init.default_transform;
            }
        } else {
            if (has_world_transform) {
                emitter.world_transform = world_transform;
            } else {
                emitter.world_transform = root;
                if (init.has_default_position) {
                    emitter.world_transform *= glm::translate(glm::mat4{1.0f}, init.default_position);
                }
                if (init.has_default_orientation) {
                    emitter.world_transform *= glm::mat4_cast(init.default_orientation);
                }
            }
        }
        if (reset_previous_positions) {
            emitter.prev_world_pos = glm::vec3(emitter.world_transform[3]);
        } else if (previous_position == glm::vec3{0.0f} && emitter.prev_world_pos == glm::vec3{0.0f}) {
            emitter.prev_world_pos = glm::vec3(emitter.world_transform[3]);
        }
        if (!init.target_node_name.empty()) {
            if (const auto* animated_target = scene_particles.owner->find(init.target_node_name)) {
                emitter.target_point = root * animated_target->get_transform()[3];
            } else if (init.has_default_target_offset) {
                emitter.target_point = root * glm::vec4(init.default_target_offset, 1.0f);
            }
        } else if (init.has_default_target_offset) {
            emitter.target_point = root * glm::vec4(init.default_target_offset, 1.0f);
        }
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

void PreviewScene::rebuild_load_report(std::string_view source, std::string_view kind)
{
    const std::string source_text{source};
    const std::string kind_text{kind};
    load_report = {};
    load_report.source = source_text;
    load_report.kind = kind_text;

    PreviewLoadReportBuilder builder{load_report};
    std::set<std::string> scanned_models;
    for (const auto& model : models) {
        if (!model || !model->mdl_) {
            continue;
        }
        const std::string model_name = model->mdl_->model.name.c_str();
        if (!model_name.empty()
            && std::find(load_report.model_names.begin(), load_report.model_names.end(), model_name)
                == load_report.model_names.end()) {
            load_report.model_names.push_back(model_name);
        }
        scan_preview_model_dependencies(builder, *model->mdl_, scanned_models);
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
}

void PreviewScene::rebuild_particles(std::string_view animation_name)
{
    particles.clear();
    particles.reserve(models.size());

    for (const auto& model : models) {
        if (!model || !model->mdl_) {
            continue;
        }

        auto particle_animation = animation_name;
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

        glm::vec3 aabb_min{0.0f}, aabb_max{0.0f};
        auto collision = build_mesh_collision(*model, aabb_min, aabb_max);
        particles.push_back(SceneParticleSystem{
            .owner = model.get(),
            .import = std::move(import),
            .compiled = {},
            .system = {},
            .collision = std::move(collision),
            .mesh_aabb_min = aabb_min,
            .mesh_aabb_max = aabb_max,
        });
    }

    for (auto& scene_particles : particles) {
        scene_particles.compiled = nw::render::compile_particle_effect(scene_particles.import.effect);
        scene_particles.system = nw::render::create_particle_system(scene_particles.compiled.effect);
        scene_particles.system.effect = &scene_particles.compiled.effect;
        nw::model::apply_particle_import_inits(scene_particles.import, scene_particles.system);
        if (scene_particles.owner && scene_particles.owner->anim_) {
            scene_particles.animation_time = static_cast<float>(scene_particles.owner->anim_cursor_) * 0.001f;
            scene_particles.animation_time_initialized = scene_particles.animation_time > 1.0e-6f;
        }
        scene_particles.owner_visible_last = !scene_particles.owner || scene_particles.owner->render_enabled;
        if (!scene_particles.owner_visible_last) {
            for (auto& emitter : scene_particles.system.emitters) {
                emitter.active = false;
            }
        }
        sync_scene_particle_emitters(scene_particles, true);
        nw::render::build_particle_render_packets(scene_particles.system);
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

    const float dt = std::max(0.0f, static_cast<float>(dt_ms) * 0.001f);
    for (auto& scene_particles : particles) {
        const bool owner_visible = !scene_particles.owner || scene_particles.owner->render_enabled;
        if (owner_visible && !scene_particles.owner_visible_last) {
            for (auto& emitter : scene_particles.system.emitters) {
                emitter.active = true;
            }
            sync_scene_particle_emitters(scene_particles, true);
        }
        float current_time = scene_particles.animation_time;
        bool has_animation_time = false;
        if (owner_visible && scene_particles.owner && scene_particles.owner->anim_) {
            current_time = static_cast<float>(scene_particles.owner->anim_cursor_) * 0.001f;
            has_animation_time = true;
            nw::model::apply_particle_import_events(scene_particles.import,
                scene_particles.system,
                scene_particles.animation_time,
                current_time,
                scene_particles.owner->anim_->length,
                !scene_particles.animation_time_initialized);
            scene_particles.animation_time = current_time;
            scene_particles.animation_time_initialized = true;
        }
        if (has_animation_time) {
            for (auto& emitter : scene_particles.system.emitters) {
                emitter.time = current_time;
            }
        }
        if (owner_visible) {
            sync_scene_particle_emitters(scene_particles);
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

void PreviewScene::set_particle_target_point(const ModelInstance* owner, const glm::vec3& target_point)
{
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
    for (const auto& model : models) {
        auto current = model->current_bounds();
        if (first) {
            result = current;
            first = false;
        } else {
            result.min = glm::min(result.min, current.min);
            result.max = glm::max(result.max, current.max);
        }
    }
    for (const auto& model : static_models) {
        if (!model) continue;
        if (first) {
            result = model->bounds;
            first = false;
        } else {
            result.min = glm::min(result.min, model->bounds.min);
            result.max = glm::max(result.max, model->bounds.max);
        }
    }
    return first ? bounds : result;
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
    models.push_back(std::move(model));
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
    static_models.push_back(std::move(model));
}

void PreviewScene::add_particle_effect(nw::render::ParticleEffectDef effect)
{
    particles.push_back(SceneParticleSystem{
        .owner = nullptr,
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
    return equip.is<nw::Item*>() ? equip.as<nw::Item*>() : nullptr;
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

static void make_static_scene_attachment(ModelInstance& model)
{
    model.scene_animation_enabled = false;
    model.accepts_external_animation_source = false;
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

static void add_equipped_item_model(PreviewScene& scene, Renderer& renderer, const nw::Item& item,
    nw::EquipIndex slot, const ModelInstance* placement_context, float local_scale = 1.0f,
    const nw::PltColors* creature_colors = nullptr)
{
    const auto slot_name = nw::equip_index_to_string(slot);
    const auto item_resref = item.common.resref.string();
    ERRARE("[viewer] resolving equipped item '{}.uti' in slot '{}'",
        std::string_view{item_resref},
        slot_name);

    auto anchor = anchor_name_for_equipped_item(slot);
    if (!placement_context || anchor.empty()) {
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

        auto model = load_model_with_plt(renderer, resref);
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
            apply_plt_to_model(renderer, *model, item_plt);
        }
        model->scene_animation_enabled = false;
        if (slot == nw::EquipIndex::head) {
            model->anchor_uses_root_bind_offset = false;
        }
        model->local_scale_ = local_scale;
        model->set_transform_anchor(placement_context, anchor);
        LOG_F(INFO, "dynamic creature equipped {} -> {}",
            nw::equip_index_to_string(slot), resref);
        scene.add(std::move(model));
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
        break;
    }
}

static std::unique_ptr<PreviewScene> load_dynamic_creature_scene(Renderer& renderer, const std::filesystem::path& path)
{
    const auto path_text = path.string();
    ERRARE("[viewer] loading dynamic creature preview '{}'", std::string_view{path_text});

    auto scene = std::make_unique<PreviewScene>();
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

    auto* app = nw::kernel::rules().appearances.get(appearance_id);
    auto* appearance_tda = nw::kernel::twodas().get("appearance");
    if (!app || !appearance_tda) {
        return {};
    }
    const float wing_tail_scale = appearance_wing_tail_scale(appearance_tda, appearance_id);
    const float helmet_scale = appearance_helmet_scale(appearance_tda, appearance_id, gender);

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
    if (app->model_type == "P") {
        // Experimental NWN humanoid preview assembly stays isolated here on purpose.
        auto base_rig_resref = resolve_creature_base_rig(*app, race, sex);
        if (base_rig_resref) {
            ERRARE("[viewer] resolving dynamic creature base rig '{}'", std::string_view{*base_rig_resref});
            base_rig = load_model_with_plt(renderer, *base_rig_resref, &plt_colors);
        }
        if (base_rig) {
            base_rig->render_enabled = false;
            scene->add(std::move(base_rig));
        } else {
            LOG_F(WARNING, "No dynamic creature base rig resolved for appearance {}", appearance_id.idx());
            log_preview_warning_context();
        }

        auto* placement_context = scene->models.empty() ? nullptr : scene->models.front().get();

        const struct PartSpec {
            uint16_t nw::BodyParts::* field;
            std::string_view token;
        } parts[] = {
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
        };

        for (const auto& part : parts) {
            auto model_part = body_parts.*(part.field);
            nw::PltColors part_colors = plt_colors;
            bool prefer_mirrored_part_model = false;
            auto anchor = anchor_name_for_part(part.token);
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

            if (part_resref) {
                ERRARE("[viewer] resolving dynamic creature body part '{}' model '{}'",
                    part.token,
                    std::string_view{*part_resref});
                auto model = load_model_with_plt(renderer, *part_resref, &part_colors);
                if (model && placement_context) {
                    if (!anchor.empty()) {
                        make_static_scene_attachment(*model);
                        model->anchor_uses_root_bind_offset = false;
                        model->set_transform_anchor(placement_context, anchor);
                    }
                }
                if (!model) {
                    LOG_F(WARNING, "Dynamic creature body part '{}' model '{}' failed to load",
                        part.token,
                        *part_resref);
                    log_preview_warning_context();
                }
                maybe_add_model(*scene, std::move(model));
            } else if (model_part > 0 && model_part != 255) {
                ERRARE("[viewer] resolving dynamic creature body part '{}' model part {}",
                    part.token,
                    model_part);
                LOG_F(WARNING, "Dynamic creature body part '{}' model part {} was not found",
                    part.token,
                    model_part);
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
                    auto robe = load_model_with_plt(renderer, *robe_resref, &robe_colors);
                    if (robe) {
                        robe->accepts_external_animation_source = false;
                    }
                    if (!robe) {
                        LOG_F(WARNING, "Dynamic creature robe model '{}' failed to load", *robe_resref);
                        log_preview_warning_context();
                    }
                    maybe_add_model(*scene, std::move(robe));
                } else {
                    ERRARE("[viewer] resolving dynamic creature robe model part {}", robe_part);
                    LOG_F(WARNING, "Dynamic creature robe model part {} was not found", robe_part);
                    log_preview_warning_context();
                }
            }
        }
    } else {
        ERRARE("[viewer] resolving creature model '{}'", std::string_view{app->model_name});
        auto model = load_model_with_plt(renderer, app->model_name, &plt_colors);
        if (!model) {
            LOG_F(WARNING, "Creature model '{}' failed to load", app->model_name);
            log_preview_warning_context();
        }
        maybe_add_model(*scene, std::move(model));
    }

    auto add_attachment = [&](const char* table_name, uint32_t row) {
        if (row == 0) {
            return;
        }
        ERRARE("[viewer] resolving creature attachment '{}' row {}", std::string_view{table_name}, row);
        auto* tda = nw::kernel::twodas().get(table_name);
        std::string model_name;
        if (!tda) {
            LOG_F(WARNING, "Dynamic creature attachment table '{}' was not loaded", table_name);
            log_preview_warning_context();
            return;
        }
        if (!tda->get_to(row, "MODEL", model_name) || model_name.empty()) {
            LOG_F(WARNING, "Dynamic creature attachment '{}' row {} has no MODEL", table_name, row);
            log_preview_warning_context();
            return;
        }
        if (model_name != "c_nulltail") {
            auto anchor = anchor_name_for_attachment(table_name);
            auto source_anchor = source_anchor_name_for_attachment(table_name);
            ERRARE("[viewer] resolving creature attachment model '{}'", std::string_view{model_name});
            auto model = load_model_with_plt(renderer, model_name, &plt_colors);
            auto* placement_context = scene->models.empty() ? nullptr : scene->models.front().get();
            if (model && placement_context && !anchor.empty()) {
                make_static_scene_attachment(*model);
                if (table_name == std::string_view{"wingmodel"}) {
                    for (const auto& node : model->nodes_) {
                        const auto* orig = node->orig_;
                        if (!orig || !node->is_mesh || node->is_skin) {
                            continue;
                        }

                        std::string_view name = orig->name;
                        if (name.starts_with("gargoyle_") || name.starts_with("wing_shadow")) {
                            if (auto* mesh = dynamic_cast<Mesh*>(node.get())) {
                                mesh->vertices = {};
                                mesh->indices = {};
                                mesh->index_count = 0;
                            }
                        }
                    }
                }
                model->local_scale_ = wing_tail_scale;
                model->anchor_uses_root_bind_offset = true;
                model->set_transform_anchor(placement_context, anchor, source_anchor);
            }
            if (!model) {
                LOG_F(WARNING, "Dynamic creature attachment '{}' row {} model '{}' failed to load",
                    table_name,
                    row,
                    model_name);
                log_preview_warning_context();
            }
            maybe_add_model(*scene, std::move(model));
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
                auto cloak = load_model_with_plt(renderer, *cloak_resref, &cloak_colors);
                if (cloak) {
                    cloak->accepts_external_animation_source = false;
                }
                if (!cloak) {
                    LOG_F(WARNING, "Dynamic creature cloak model '{}' failed to load", *cloak_resref);
                    log_preview_warning_context();
                }
                maybe_add_model(*scene, std::move(cloak));
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

    auto* placement_context = scene->models.empty() ? nullptr : scene->models.front().get();
    if (righthand_item) {
        add_equipped_item_model(*scene, renderer, *righthand_item, nw::EquipIndex::righthand, placement_context, 1.0f, &plt_colors);
    }
    if (lefthand_item) {
        add_equipped_item_model(*scene, renderer, *lefthand_item, nw::EquipIndex::lefthand, placement_context, 1.0f, &plt_colors);
    }
    if (head_item) {
        add_equipped_item_model(*scene, renderer, *head_item, nw::EquipIndex::head, placement_context, helmet_scale, &plt_colors);
    }

    if (scene->models.empty()) {
        LOG_F(ERROR, "Dynamic creature preview '{}' produced no renderable models", path.string());
        log_preview_error_context();
        return {};
    }
    if (!scene->models.empty()) {
        auto* animation_context = scene->models.front().get();
        auto* source = animation_context ? animation_context->mdl_ : nullptr;
        if (source && source->model.supermodel) {
            source = source->model.supermodel.get();
        }
        set_scene_animation_source(*scene, source);
    }
    scene->rebuild_load_report(path_text, "dynamic_creature");
    return scene;
}

static std::unique_ptr<PreviewScene> load_item_scene(Renderer& renderer, const std::filesystem::path& path)
{
    const auto path_text = path.string();
    ERRARE("[viewer] loading item preview '{}'", std::string_view{path_text});

    nw::Item item;
    if (!load_item_from_file(path, item)) {
        return {};
    }

    auto* baseitem = nw::kernel::rules().baseitems.get(item.baseitem);
    if (!baseitem) {
        LOG_F(ERROR, "Item preview '{}' has invalid baseitem {}", path.string(), *item.baseitem);
        log_preview_error_context();
        return {};
    }

    auto scene = std::make_unique<PreviewScene>();
    auto try_add = [&](std::string_view resref, nw::ItemModelParts::type part) {
        if (resref.empty()) {
            return;
        }
        ERRARE("[viewer] resolving item preview model '{}'", resref);
        if (!nw::kernel::resman().contains({nw::Resref{resref}, nw::ResourceType::mdl})) {
            LOG_F(WARNING, "Item preview '{}' model '{}' was not found",
                path.string(),
                resref);
            log_preview_warning_context();
            return;
        }
        auto model = load_model_with_plt(renderer, resref);
        if (!model) {
            LOG_F(WARNING, "Item preview '{}' model '{}' failed to load",
                path.string(),
                resref);
            log_preview_warning_context();
            return;
        }
        if (nw::kernel::resman().contains({nw::Resref{resref}, nw::ResourceType::plt})) {
            auto item_plt = item.part_to_plt_colors(part);
            apply_plt_to_model(renderer, *model, item_plt);
        }
        maybe_add_model(*scene, std::move(model));
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

    if (scene->models.empty() && baseitem->default_model.valid()) {
        try_add(baseitem->default_model.resref.view(), nw::ItemModelParts::model1);
    }

    if (scene->models.empty()) {
        LOG_F(ERROR, "Item preview '{}' produced no renderable models", path.string());
        log_preview_error_context();
        return {};
    }
    scene->rebuild_load_report(path_text, "item");
    return scene;
}

static std::unique_ptr<PreviewScene> load_blueprint_model_scene(Renderer& renderer,
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
    maybe_add_model(*scene, load_model_with_plt(renderer, model_resref));
    if (scene->models.empty()) {
        LOG_F(ERROR, "{} preview '{}' failed to load model '{}' for {}",
            preview_type, path.string(), model_resref, lookup_context);
        log_preview_error_context();
        return {};
    }
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

static std::unique_ptr<PreviewScene> load_door_scene(Renderer& renderer, const std::filesystem::path& path)
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

    return load_blueprint_model_scene(renderer, path, "Door",
        door_model_lookup_context(*model_ref), model_ref->model.view());
}

static std::unique_ptr<PreviewScene> load_placeable_scene(Renderer& renderer, const std::filesystem::path& path)
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
    return load_blueprint_model_scene(renderer, path, "Placeable", lookup_context, info->model.view());
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

static void add_particle_report(
    PreviewLoadReport& report,
    PreviewLoadReportBuilder& builder,
    std::string_view owner,
    const nw::model::ParticleImportResult& import,
    const nw::render::ParticleCompileResult& compiled)
{
    report.particles.push_back({
        .owner = std::string{owner},
        .emitter_count = import.effect.emitters.size(),
        .max_particles_total = compiled.effect.max_particles_total,
        .import_warning_count = import.warnings.size(),
        .compile_warning_count = compiled.warnings.size(),
        .effect_event_count = import.effect_events.size(),
    });
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
    add_model_particle_report(report, builder, *mdl, animation_name);
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
    switch (item.model_type) {
    case nw::ItemModelType::simple:
    case nw::ItemModelType::layered:
        if (item.model_parts[nw::ItemModelParts::model1] > 0) {
            found_item_model = try_add(
                fmt::format("{}_{:03d}", baseitem->item_class.view(), item.model_parts[nw::ItemModelParts::model1]),
                nw::ItemModelParts::model1) || found_item_model;
        }
        break;
    case nw::ItemModelType::composite:
        if (item.model_parts[nw::ItemModelParts::model1] > 0) {
            found_item_model = try_add(
                fmt::format("{}_b_{:03d}", baseitem->item_class.view(), item.model_parts[nw::ItemModelParts::model1]),
                nw::ItemModelParts::model1) || found_item_model;
        }
        if (item.model_parts[nw::ItemModelParts::model2] > 0) {
            found_item_model = try_add(
                fmt::format("{}_m_{:03d}", baseitem->item_class.view(), item.model_parts[nw::ItemModelParts::model2]),
                nw::ItemModelParts::model2) || found_item_model;
        }
        if (item.model_parts[nw::ItemModelParts::model3] > 0) {
            found_item_model = try_add(
                fmt::format("{}_t_{:03d}", baseitem->item_class.view(), item.model_parts[nw::ItemModelParts::model3]),
                nw::ItemModelParts::model3) || found_item_model;
        }
        break;
    case nw::ItemModelType::armor:
        if (use_default_fallback) {
            builder.add_event(PreviewLoadEventSeverity::warning,
                "item",
                fmt::format("{} standalone armor item '{}' is only fully resolved through a creature/player preview",
                    origin_prefix,
                    item.common.resref));
        }
        break;
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

        const struct PartSpec {
            uint16_t nw::BodyParts::* field;
            std::string_view token;
        } parts[] = {
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
        };

        for (const auto& part : parts) {
            auto model_part = body_parts.*(part.field);
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
                        }
                    }
                    if (armor_model_part > 0 && armor_model_part != 255) {
                        model_part = armor_model_part;
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

            if (part_resref) {
                add_report_model(report, builder, scanned_models, *part_resref,
                    fmt::format("{}:body_part:{}", path.string(), part.token), animation_name);
            } else if (model_part > 0 && model_part != 255) {
                builder.add_event(PreviewLoadEventSeverity::warning,
                    "creature",
                    fmt::format("Dynamic creature body part '{}' model part {} was not found",
                        part.token,
                        model_part));
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
        if (row == 0) {
            return;
        }
        auto* tda = nw::kernel::twodas().get(table_name);
        std::string model_name;
        if (!tda) {
            builder.add_event(PreviewLoadEventSeverity::warning,
                "attachment",
                fmt::format("Dynamic creature attachment table '{}' was not loaded", table_name));
            return;
        }
        if (!tda->get_to(row, "MODEL", model_name) || model_name.empty()) {
            builder.add_event(PreviewLoadEventSeverity::warning,
                "attachment",
                fmt::format("Dynamic creature attachment '{}' row {} has no MODEL", table_name, row));
            return;
        }
        if (model_name != "c_nulltail") {
            add_report_model(report, builder, scanned_models, model_name,
                fmt::format("{}:attachment:{}:{}", path.string(), table_name, row), animation_name);
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
            "glTF dependency reports do not require renderer initialization, but only NWN resource dependency scanning is currently implemented");
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

std::unique_ptr<PreviewScene> load_preview_scene(Renderer& renderer, std::string_view source)
{
    ERRARE("[viewer] loading preview source '{}'", source);

    auto path = std::filesystem::path{source};
    nw::String ext = path.extension().string();
    nw::string::tolower(&ext);

    if ((ext == ".gltf" || ext == ".glb") && std::filesystem::exists(path)) {
        auto scene = std::make_unique<PreviewScene>();
        ImportGltfDesc desc{};
        desc.ctx = renderer.context();
        desc.fallback_albedo = renderer.fallback_albedo_index();
        desc.fallback_normal = renderer.fallback_normal_index();
        desc.fallback_surface = renderer.fallback_surface_index();
        desc.fallback_emissive = renderer.fallback_emissive_index();
        auto model = import_gltf(path, desc);
        if (!model) {
            LOG_F(ERROR, "Failed to load glTF preview '{}'", path.string());
            log_preview_error_context();
            return {};
        }
        scene->add(std::move(model));
        scene->rebuild_load_report(path.string(), "gltf");
        return scene;
    }

    if (is_object_preview_path(source)) {
        const nw::Resource resource = nw::Resource::from_path(path, false);
        if (resource.type == nw::ResourceType::bic || resource.type == nw::ResourceType::utc) {
            return load_dynamic_creature_scene(renderer, path);
        }
        if (resource.type == nw::ResourceType::uti) {
            return load_item_scene(renderer, path);
        }
        if (resource.type == nw::ResourceType::utd) {
            return load_door_scene(renderer, path);
        }
        if (resource.type == nw::ResourceType::utp) {
            return load_placeable_scene(renderer, path);
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
    maybe_add_model(*scene, load_model_with_plt(renderer, source));
    if (scene->models.empty()) {
        LOG_F(ERROR, "Preview source '{}' produced no renderable models", source);
        log_preview_error_context();
        return {};
    }
    scene->rebuild_particles();
    scene->rebuild_load_report(source, "model");
    return scene;
}

std::unique_ptr<PreviewScene> load_preview_scene(Renderer& renderer, std::span<const std::string> sources)
{
    ERRARE("[viewer] loading multi-source preview ({} sources)", sources.size());

    auto scene = std::make_unique<PreviewScene>();
    for (const auto& source : sources) {
        ERRARE("[viewer] loading preview source '{}'", std::string_view{source});
        maybe_add_model(*scene, load_model_with_plt(renderer, source));
    }
    if (scene->models.empty()) {
        LOG_F(ERROR, "Multi-source preview produced no renderable models");
        log_preview_error_context();
        return {};
    }
    scene->rebuild_particles();
    scene->rebuild_load_report("<multi-source>", "multi_source");
    return scene;
}

std::unique_ptr<PreviewScene> load_area_scene(Renderer& renderer, std::string_view area_resref)
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

            auto model = load_model_with_plt(renderer, tile_def.model);
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

            model->scene_animation_enabled = false;
            model->set_placement_transform(placement);
            scene->add(std::move(model));
        }
    }

    LOG_F(INFO,
        "Loaded area {} tileset={} : {}x{} tiles={} loaded_models={} demand={}ms deserialize={}ms placement[x={}..{}, y={}..{}]",
        area_resref,
        loaded_area->tileset_resref,
        loaded_area->width,
        loaded_area->height,
        loaded_area->tiles.size(),
        scene->models.size(),
        profile.demand_ms,
        profile.deserialize_ms,
        min_x,
        max_x,
        min_y,
        max_y);
    for (const auto& [model, count] : tile_model_counts) {
        LOG_F(INFO, "Area tile model {} count={}", model, count);
    }

    nw::kernel::objects().destroy(loaded_area->handle());

    if (scene->models.empty()) {
        return {};
    }
    scene->area_overlay_z = max_tile_z == std::numeric_limits<float>::lowest() ? 0.0f : max_tile_z;
    scene->rebuild_load_report(area_resref, "area");
    return scene;
}

} // namespace nw::render::viewer
