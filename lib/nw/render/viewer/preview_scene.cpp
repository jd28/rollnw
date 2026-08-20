#include "preview_scene.hpp"

#include "preview_model_animation.hpp"
#include "preview_nwn_creature.hpp"
#include "preview_object.hpp"
#include "scene_debug.hpp"
#include "scene_lights.hpp"
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
#include <nw/objects/Creature.hpp>
#include <nw/objects/Encounter.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectComponentSystem.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Trigger.hpp>
#include <nw/objects/Waypoint.hpp>
#include <nw/profiles/nwn1/constants.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/resources/assets.hpp>
#include <nw/serialization/Gff.hpp>
#include <nw/smalls/runtime.hpp>
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
#include <iterator>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <span>
#include <unordered_map>
#include <utility>

namespace nw::render::viewer {

SceneParticleSystem::SceneParticleSystem(SceneParticleSystem&& other) noexcept
    : owner_model_index(other.owner_model_index)
    , owner_instance_handle(other.owner_instance_handle)
    , import(std::move(other.import))
    , compiled(std::move(other.compiled))
    , system(std::move(other.system))
    , animation_time(other.animation_time)
    , particle_animation_length(other.particle_animation_length)
    , animation_time_initialized(other.animation_time_initialized)
    , owner_visible_last(other.owner_visible_last)
{
    if (system.effect) {
        system.effect = &compiled.effect;
    }
    other.system.effect = nullptr;
}

SceneParticleSystem& SceneParticleSystem::operator=(SceneParticleSystem&& other) noexcept
{
    if (this == &other) {
        return *this;
    }

    const bool has_compiled_effect = other.system.effect != nullptr;
    owner_model_index = other.owner_model_index;
    owner_instance_handle = other.owner_instance_handle;
    import = std::move(other.import);
    compiled = std::move(other.compiled);
    system = std::move(other.system);
    animation_time = other.animation_time;
    particle_animation_length = other.particle_animation_length;
    animation_time_initialized = other.animation_time_initialized;
    owner_visible_last = other.owner_visible_last;

    system.effect = has_compiled_effect ? &compiled.effect : nullptr;
    other.system.effect = nullptr;
    return *this;
}

PreviewScene::~PreviewScene()
{
    active_object = nw::ObjectHandle{};
    if (!owns_root_object || root_object.type == nw::ObjectType::invalid) {
        root_object = nw::ObjectHandle{};
        return;
    }

    if (root_object.type == nw::ObjectType::area) {
        if (auto* area = nw::kernel::objects().get<nw::Area>(root_object)) {
            area->clear();
        }
    }
    nw::kernel::objects().destroy(root_object);
    root_object = nw::ObjectHandle{};
}

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

nw::Location object_spatial_location(const nw::ObjectBase& object)
{
    return nw::kernel::objects().components().location(object.handle());
}

glm::mat4 area_object_render_placement_transform(
    nw::ObjectType type, nw::Location location, glm::vec3 scale)
{
    if (type == nw::ObjectType::waypoint) {
        // Waypoint marker geometry uses local +Y as forward; area headings use +X.
        constexpr float k_epsilon = 1.0e-5f;
        if (std::abs(location.orientation.x) > k_epsilon
            || std::abs(location.orientation.y) > k_epsilon) {
            location.orientation = {
                location.orientation.y,
                -location.orientation.x,
                location.orientation.z,
            };
        } else {
            location.orientation = {0.0f, -1.0f, location.orientation.z};
        }
    }

    return area_object_placement_transform(location, scale);
}

glm::mat4 object_spatial_placement(const nw::ObjectBase& object)
{
    const auto& components = nw::kernel::objects().components();
    const auto* spatial = components.find_spatial(object.handle());
    return area_object_render_placement_transform(
        object.handle().type,
        components.location(object.handle()),
        spatial ? spatial->scale : glm::vec3{1.0f});
}

void append_scene_load_events(PreviewScene& scene, std::vector<PreviewLoadEvent>& events)
{
    scene.load_report.events.insert(scene.load_report.events.end(), events.begin(), events.end());
    append_preview_load_events(scene.source_load_events, events);
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
    if (scene_particles.owner_model_index != owner_model_index) {
        return false;
    }
    if (owner_handle.valid() && scene_particles.owner_instance_handle.valid()
        && scene_particles.owner_instance_handle != owner_handle) {
        return false;
    }
    return true;
}

} // namespace

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
        if (auto animation = find_first_animation(mdl, {"default", "pause1", "on", "impact"}); !animation.empty()) {
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
    if (!instance) {
        return nullptr;
    }
    if (instance->render_model_index != scene_particles.owner_model_index) {
        return nullptr;
    }
    return instance;
}

const nw::render::ModelInstance* scene_particle_attachment_owner_instance(
    const PreviewScene& scene,
    const nw::render::ParticleEmitterAttachmentBinding& binding) noexcept
{
    if (!binding.owner_instance_handle.valid()) {
        return nullptr;
    }
    const auto* instance = scene.model_instances.get(binding.owner_instance_handle);
    if (!instance) {
        return nullptr;
    }
    if (instance->render_model_index != binding.owner_model_index) {
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
    return true;
}

glm::mat4 scene_particle_owner_root(const PreviewScene& scene, const SceneParticleSystem& scene_particles)
{
    if (const auto* instance = scene_particle_owner_instance(scene, scene_particles)) {
        return instance->root_transform;
    }
    return glm::mat4{1.0f};
}

glm::mat4 scene_particle_attachment_owner_root(
    const PreviewScene& scene,
    const SceneParticleSystem& scene_particles,
    const nw::render::ParticleEmitterAttachmentBinding* binding)
{
    if (binding) {
        if (const auto* instance = scene_particle_attachment_owner_instance(scene, *binding)) {
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
        ? scene_particle_attachment_owner_instance(scene, *binding)
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
            .target_attachment_point = attachment.target_attachment_point,
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
        const nw::render::ModelAttachmentPointIndex target_attachment_point = binding
            ? binding->target_attachment_point
            : attachment.target_attachment_point;

        auto& frame = frames.emplace_back(nw::render::ParticleEmitterAttachmentFrame{
            .emitter = emitter_index,
            .owner_root_transform = emitter_root,
        });

        glm::mat4 common_emitter_transform{1.0f};
        if (scene_particle_common_attachment_world_transform(
                scene, scene_particles, binding, emitter_attachment_point, common_emitter_transform)) {
            frame.emitter_world_transform = common_emitter_transform;
            frame.has_emitter_world_transform = true;
        } else if (attachment.has_default_transform) {
            frame.emitter_world_transform = emitter_root * attachment.default_transform;
            frame.has_emitter_world_transform = true;
        }

        if (target_attachment_point != nw::render::kInvalidModelAttachmentPointIndex) {
            glm::mat4 common_target_transform{1.0f};
            if (scene_particle_common_attachment_world_transform(
                    scene, scene_particles, binding, target_attachment_point, common_target_transform)) {
                frame.target_point = glm::vec3(common_target_transform[3]);
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
    if (!scene_particles.owner_instance_handle.valid()) {
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

bool render_model_has_particle_animation(
    const nw::render::RenderModel& model,
    std::string_view selected_animation_name) noexcept
{
    return !selected_animation_name.empty()
        && std::any_of(model.particle_systems.begin(), model.particle_systems.end(),
            [selected_animation_name](const nw::render::ModelAssetParticleSystem& particles) {
                return !particles.effect.emitters.empty()
                    && particles.animation_name == selected_animation_name;
            });
}

void append_render_model_particle_systems(
    PreviewScene& scene,
    const nw::render::RenderModel& model,
    nw::render::ModelInstanceHandle owner_handle,
    uint32_t owner_model_index,
    std::string_view selected_animation_name)
{
    const bool use_animation_particles = render_model_has_particle_animation(model, selected_animation_name);
    for (const auto& particle_system : model.particle_systems) {
        const bool selected = use_animation_particles
            ? particle_system.animation_name == selected_animation_name
            : particle_system.animation_name.empty();
        if (particle_system.effect.emitters.empty() || !selected) {
            continue;
        }
        auto& scene_particles = scene.particles.emplace_back();
        scene_particles.owner_model_index = owner_model_index;
        scene_particles.owner_instance_handle = owner_handle;
        scene_particles.import = model_asset_particle_import(particle_system);
        scene_particles.particle_animation_length = particle_system.animation_length;
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
    particles.reserve(render_model_particle_count);

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
        if (owner_visible && scene_particles.particle_animation_length > 0.0f) {
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
        nw::render::tick_particle_system(scene_particles.system, dt);
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

Bounds PreviewScene::current_bounds() const
{
    if (static_models.empty()) {
        if (auto particle_bounds = live_particle_bounds(particles)) {
            return *particle_bounds;
        }
    }

    Bounds result{};
    bool first = true;
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

static std::span<const nw::render::ModelSocket> scene_model_sockets(
    const PreviewScene& scene,
    nw::render::ModelInstanceHandle handle,
    const nw::render::ModelInstance& instance) noexcept
{
    if (instance.render_model_index < scene.static_models.size()
        && instance.render_model_index < scene.static_model_instance_handles.size()
        && scene.static_model_instance_handles[instance.render_model_index] == handle) {
        const auto& model = scene.static_models[instance.render_model_index];
        if (model) {
            return model->sockets;
        }
    }
    return {};
}

static nw::render::ModelInstance make_common_render_model_instance(
    const nw::render::RenderModel& model, size_t model_index)
{
    nw::render::ModelInstance result{};
    result.visible = true;
    result.root_transform = glm::mat4{1.0f};
    result.current_bounds = model.bounds;
    result.shadow = render_model_shadow_summary(model, result.current_bounds);
    result.render_model_index = static_cast<uint32_t>(
        std::min<size_t>(model_index, std::numeric_limits<uint32_t>::max()));
    result.scene_animation_enabled = !model.animations.empty();
    result.animation.enabled = !model.animations.empty() && !model.skeletons.empty();
    nw::render::publish_render_model_static_node_world_transforms(result, model);
    return result;
}

PreviewSceneRuntimeSyncStats sync_model_instance_runtime_state(PreviewScene& scene)
{
    PreviewSceneRuntimeSyncStats stats{};
    for (size_t model_index = 0; model_index < scene.static_models.size(); ++model_index) {
        const auto& model = scene.static_models[model_index];
        auto* instance = scene.static_model_instance(model_index);
        if (!model || !instance) {
            continue;
        }
        ++stats.render_model_count;

        if (!instance->animation.enabled) {
            nw::render::publish_render_model_static_node_world_transforms(*instance, *model);
        }
        instance->current_bounds = transform_bounds(model->bounds, instance->root_transform);
        instance->shadow = render_model_shadow_summary(*model, instance->current_bounds);
    }

    auto& attachment_inputs = scene.attachment_transform_inputs;
    auto& attachment_outputs = scene.attachment_transform_outputs;
    attachment_inputs.clear();
    attachment_inputs.reserve(scene.model_attachments.size());
    for (const auto& binding : scene.model_attachments) {
        auto* child_instance = scene.model_instances.get(binding.child_instance_handle);
        const auto* owner_instance = scene.model_instances.get(binding.owner_instance_handle);
        if (child_instance) {
            ++stats.render_model_attachment_binding_count;
        }
        attachment_inputs.push_back(nw::render::ModelAttachmentRootTransformInput{
            .owner_instance = owner_instance,
            .owner_sockets = owner_instance
                ? scene_model_sockets(scene, binding.owner_instance_handle, *owner_instance)
                : std::span<const nw::render::ModelSocket>{},
            .child_sockets = child_instance
                ? scene_model_sockets(scene, binding.child_instance_handle, *child_instance)
                : std::span<const nw::render::ModelSocket>{},
            .owner_socket_index = binding.owner_socket_index,
            .child_source_socket_index = binding.child_source_socket_index,
            .child_local_transform = binding.child_local_transform,
            .child_root_bind_translation = binding.child_root_bind_translation,
            .child_local_scale = binding.child_local_scale,
            .orientation = binding.orientation,
            .source_offset = binding.source_offset,
        });
    }

    attachment_outputs.resize(attachment_inputs.size());
    nw::render::build_model_attachment_root_transforms(attachment_inputs, attachment_outputs);
    for (size_t i = 0; i < attachment_outputs.size(); ++i) {
        auto* instance = scene.model_instances.get(scene.model_attachments[i].child_instance_handle);
        if (!instance) {
            continue;
        }
        if (!attachment_outputs[i].valid) {
            ++stats.render_model_attachment_root_failed_count;
            continue;
        }

        instance->root_transform = attachment_outputs[i].root_transform;
        if (instance->render_model_index < scene.static_models.size()) {
            const auto& model = scene.static_models[instance->render_model_index];
            if (model) {
                if (!instance->animation.enabled) {
                    nw::render::publish_render_model_static_node_world_transforms(*instance, *model);
                }
                instance->current_bounds = transform_bounds(model->bounds, instance->root_transform);
                instance->shadow = render_model_shadow_summary(*model, instance->current_bounds);
            }
            ++stats.render_model_attachment_root_resolved_count;
        }
    }
    return stats;
}

AreaObjectSpatialUpdateStats update_area_object_spatial_states(
    PreviewScene& scene, std::span<const nw::ObjectSpatialState> spatial_states)
{
    AreaObjectSpatialUpdateStats stats{
        .input_count = static_cast<uint32_t>(
            std::min<size_t>(spatial_states.size(), std::numeric_limits<uint32_t>::max())),
    };
    if (spatial_states.empty()) {
        return stats;
    }

    const auto valid_spatial = [](const nw::ObjectSpatialState& row) {
        const auto finite = [](glm::vec3 value) {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        };
        return row.owner.type != nw::ObjectType::invalid
            && finite(row.position)
            && finite(row.orientation)
            && finite(row.scale)
            && row.scale.x > 0.0f
            && row.scale.y > 0.0f
            && row.scale.z > 0.0f;
    };
    stats.rejected_input_count = static_cast<uint32_t>(std::min<size_t>(
        std::count_if(spatial_states.begin(), spatial_states.end(),
            [&valid_spatial](const auto& row) { return !valid_spatial(row); }),
        std::numeric_limits<uint32_t>::max()));
    const auto find_spatial = [&spatial_states, &valid_spatial](
                                  nw::ObjectHandle object) -> const nw::ObjectSpatialState* {
        const auto it = std::find_if(
            spatial_states.begin(), spatial_states.end(), [object, &valid_spatial](const auto& row) {
                return row.owner == object && valid_spatial(row);
            });
        return it == spatial_states.end() ? nullptr : &*it;
    };
    const auto placement_for = [](const nw::ObjectSpatialState& row) {
        nw::Location location;
        location.area = row.area;
        location.position = row.position;
        location.orientation = row.orientation;
        return area_object_render_placement_transform(row.owner.type, location, row.scale);
    };

    for (size_t model_index = 0; model_index < scene.static_models.size(); ++model_index) {
        if (model_index >= scene.static_area_model_info.size()
            || model_index >= scene.static_model_attachment_binding_indices.size()
            || scene.static_model_attachment_binding_indices[model_index] != kInvalidSceneModelAttachmentBindingIndex) {
            continue;
        }
        auto* instance = scene.static_model_instance(model_index);
        const auto* spatial = find_spatial(scene.static_area_model_info[model_index].object);
        if (!instance || !spatial) {
            continue;
        }
        instance->root_transform = placement_for(*spatial);
        ++stats.render_model_root_count;
    }

    if (stats.render_model_root_count != 0) {
        sync_model_instance_runtime_state(scene);
        refresh_scene_dynamic_local_light_render_data(scene);
        if (scene.area_render_scene) {
            scene.area_render_scene->refresh_light_indices(scene);
        }
    }
    return stats;
}

bool PreviewScene::contains_water() const noexcept
{
    return has_water;
}

nw::render::ModelInstance* PreviewScene::static_model_instance(size_t model_index) noexcept
{
    if (model_index >= static_models.size() || model_index >= static_model_instance_handles.size()
        || model_index > std::numeric_limits<uint32_t>::max()) {
        return nullptr;
    }

    auto* instance = model_instances.get(static_model_instance_handles[model_index]);
    if (!instance || instance->render_model_index != static_cast<uint32_t>(model_index)) {
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
    if (!instance || instance->render_model_index != static_cast<uint32_t>(model_index)) {
        return nullptr;
    }
    return instance;
}

void PreviewScene::add(std::unique_ptr<nw::render::RenderModel> model)
{
    add(std::shared_ptr<nw::render::RenderModel>{std::move(model)});
}

void PreviewScene::add(std::shared_ptr<nw::render::RenderModel> model)
{
    if (!model) {
        return;
    }
    if (static_models.empty()) {
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
    has_gltf_models = has_gltf_models || model->source_kind == nw::render::ModelAssetSourceKind::gltf;
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

    add(std::move(model));

    if (child_model_index > std::numeric_limits<uint32_t>::max()) {
        return;
    }

    const RenderModelAttachmentSetup attachment{
        .child_model_index = static_cast<uint32_t>(child_model_index),
        .owner_model_index = owner_model_index,
        .owner_socket = owner_socket,
        .child_source_socket = child_source_socket,
        .child_local_scale = child_local_scale,
    };
    attach_render_models(std::span<const RenderModelAttachmentSetup>{&attachment, 1});
}

RenderModelAttachmentSetupStats PreviewScene::attach_render_models(
    std::span<const RenderModelAttachmentSetup> attachments)
{
    RenderModelAttachmentSetupStats stats{
        .input_count = static_cast<uint32_t>(
            std::min<size_t>(attachments.size(), std::numeric_limits<uint32_t>::max())),
    };
    const auto finite_transform = [](const glm::mat4& transform) {
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                if (!std::isfinite(transform[column][row])) {
                    return false;
                }
            }
        }
        return true;
    };

    for (const auto& attachment : attachments) {
        if (attachment.child_model_index >= static_models.size()
            || attachment.child_model_index >= static_model_instance_handles.size()
            || attachment.child_model_index >= static_model_attachment_binding_indices.size()
            || attachment.owner_model_index >= static_models.size()
            || attachment.owner_model_index >= static_model_instance_handles.size()
            || attachment.child_model_index == attachment.owner_model_index
            || !static_models[attachment.child_model_index]
            || !static_models[attachment.owner_model_index]) {
            ++stats.invalid_model_count;
            continue;
        }
        if (!finite_transform(attachment.child_local_transform)) {
            ++stats.invalid_transform_count;
            continue;
        }
        if (!std::isfinite(attachment.child_local_scale) || attachment.child_local_scale <= 0.0f) {
            ++stats.invalid_scale_count;
            continue;
        }

        const auto& owner_model = *static_models[attachment.owner_model_index];
        const uint32_t owner_socket_index = owner_model.socket_index(attachment.owner_socket);
        if (owner_socket_index == nw::render::kInvalidModelNodeIndex) {
            ++stats.missing_owner_socket_count;
            continue;
        }

        const auto& child_model = *static_models[attachment.child_model_index];
        const uint32_t child_source_socket_index = child_model.socket_index(attachment.child_source_socket);
        const glm::vec3 root_bind_translation = child_model.nodes.empty()
            ? glm::vec3{0.0f}
            : glm::vec3{child_model.nodes.front().world_transform[3]};
        const nw::render::ModelInstanceAttachmentBinding binding{
            .child_instance_handle = static_model_instance_handles[attachment.child_model_index],
            .owner_instance_handle = static_model_instance_handles[attachment.owner_model_index],
            .owner_socket_index = owner_socket_index,
            .child_source_socket_index = child_source_socket_index,
            .child_local_transform = attachment.child_local_transform,
            .child_root_bind_translation = root_bind_translation,
            .child_local_scale = attachment.child_local_scale,
            .orientation = attachment.orientation,
            .source_offset = attachment.source_offset,
        };

        uint32_t& binding_index = static_model_attachment_binding_indices[attachment.child_model_index];
        if (binding_index < model_attachments.size()) {
            model_attachments[binding_index] = binding;
            ++stats.attached_count;
            continue;
        }
        if (model_attachments.size() >= kInvalidSceneModelAttachmentBindingIndex) {
            ++stats.binding_limit_count;
            continue;
        }
        binding_index = static_cast<uint32_t>(model_attachments.size());
        model_attachments.push_back(binding);
        ++stats.attached_count;
    }
    return stats;
}

void PreviewScene::add_particle_effect(nw::render::ParticleEffectDef effect)
{
    auto& scene_particles = particles.emplace_back();
    scene_particles.import.effect = std::move(effect);
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

    if (const auto effect_bounds = nw::render::particle_emitter_spawn_bounds(scene_particles.import.effect.emitters)) {
        if (static_models.empty() && particles.size() == 1) {
            bounds = *effect_bounds;
        } else {
            bounds.min = glm::min(bounds.min, effect_bounds->min);
            bounds.max = glm::max(bounds.max, effect_bounds->max);
        }
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

struct PreviewObjectDeleter {
    void operator()(nw::ObjectBase* object) const noexcept
    {
        if (object) {
            nw::kernel::objects().destroy(object->handle());
        }
    }
};

template <typename T>
using PreviewObjectPtr = std::unique_ptr<T, PreviewObjectDeleter>;

using PreviewCreatureObject = PreviewObjectPtr<nw::Creature>;

template <typename T, typename Load>
static PreviewObjectPtr<T> load_managed_preview_object(const std::filesystem::path& path, Load load)
{
    PreviewObjectPtr<T> result{nw::kernel::objects().make<T>()};
    if (!result || !load(path, *result)) {
        return {};
    }
    return result;
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

static bool add_render_model_humanoid_body_rows(
    PreviewScene& scene,
    PreviewRenderResources& resources,
    const nw::ObjectVisualState* visual,
    const nw::PltColors& creature_colors,
    nw::ObjectVisualRenderMode render_mode,
    std::string_view origin,
    std::vector<PreviewLoadEvent>* load_events)
{
    auto warn = [&](std::string message) {
        LOG_F(WARNING, "{}", message);
        log_preview_warning_context();
        if (load_events) {
            add_preview_load_event(*load_events,
                PreviewLoadEventSeverity::warning,
                "nwn_render_model_humanoid_body",
                std::move(message));
        }
    };

    if (!visual || scene.static_models.empty() || !scene.static_models.front()) {
        warn(fmt::format("{}: humanoid body rows have no RenderModel base rig", origin));
        return false;
    }

    bool found_row = false;
    bool loaded_any = false;
    for (const auto& row : visual->models) {
        if (!visual_row_visible_for_mode(row, render_mode)
            || !visual_row_is_humanoid_body_part(row)) {
            continue;
        }
        found_row = true;

        const auto part_name = visual_row_body_part_name(row);
        if (row.model.empty()) {
            if (visual_row_requests_model_part(row)) {
                warn(fmt::format(
                    "{}: humanoid body part '{}' model part {} was not found",
                    origin,
                    part_name,
                    row.model_part));
            }
            continue;
        }

        const bool attached = !row.attach_to.empty();
        if (attached
            && scene.static_models.front()->socket_index(row.attach_to.view())
                == nw::render::kInvalidModelNodeIndex) {
            warn(fmt::format(
                "{}: humanoid body part '{}' model '{}' missing base-rig socket '{}'",
                origin,
                part_name,
                row.model,
                row.attach_to));
            continue;
        }

        ERRARE("[viewer] resolving RenderModel creature body part '{}' model '{}'",
            std::string_view{part_name},
            row.model.view());
        auto load = load_nwn_render_model_preview(resources, row.model.view());
        if (load_events) {
            append_preview_load_events(*load_events, load.events);
        }
        if (!load.model) {
            warn(fmt::format(
                "{}: humanoid body part '{}' model '{}' failed to load through RenderModel",
                origin,
                part_name,
                row.model));
            continue;
        }

        if (scene.static_models.size() >= nw::render::kInvalidModelInstanceIndex) {
            warn(fmt::format(
                "{}: humanoid body part '{}' exceeded the RenderModel instance limit",
                origin,
                part_name));
            continue;
        }
        const uint32_t model_index = static_cast<uint32_t>(scene.static_models.size());
        if (attached) {
            if (scene.model_attachments.size() >= kInvalidSceneModelAttachmentBindingIndex) {
                warn(fmt::format(
                    "{}: humanoid body part '{}' exceeded the attachment record limit",
                    origin,
                    part_name));
                continue;
            }
            scene.add_attached(std::move(load.model), 0u, row.attach_to.view());
            if (auto* instance = scene.static_model_instance(model_index)) {
                instance->scene_animation_enabled = false;
                instance->animation.enabled = false;
            }
        } else {
            scene.add(std::move(load.model));
        }
        if (model_index >= scene.static_models.size()) {
            warn(fmt::format(
                "{}: humanoid body part '{}' could not allocate a RenderModel instance",
                origin,
                part_name));
            continue;
        }

        apply_render_model_plt_material_overrides(
            scene, model_index, visual_row_plt_colors(row, creature_colors));
        loaded_any = true;
    }

    if (!found_row) {
        warn(fmt::format("{}: humanoid produced no body visual rows", origin));
    }
    return loaded_any;
}

static bool add_render_model_equipped_visual_models(PreviewScene& scene,
    PreviewRenderResources& resources,
    const nw::ObjectVisualState* visual,
    const nw::Item& item,
    nw::EquipIndex slot,
    uint32_t owner_model_index,
    float local_scale,
    nw::ObjectVisualRenderMode render_mode,
    std::vector<PreviewLoadEvent>* load_events, const nw::PltColors* creature_colors = nullptr)
{
    const auto slot_name = nw::equip_index_to_string(slot);
    const auto item_resref_value = item.resref;
    const auto item_resref = item_resref_value.string();
    ERRARE("[viewer] loading RenderModel equipped item visual rows for '{}.uti' in slot '{}'",
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
    if (!visual
        || anchor.empty()
        || owner_model_index >= scene.static_models.size()
        || owner_model_index >= scene.static_model_instance_handles.size()) {
        return skip_with_warning(fmt::format(
            "RenderModel creature equipped item '{}' in slot '{}' has no attachment anchor/context",
            item_resref_value,
            slot_name));
    }

    auto owner_model = [&]() -> const nw::render::RenderModel* {
        if (owner_model_index >= scene.static_models.size()) {
            return nullptr;
        }
        return scene.static_models[owner_model_index].get();
    };

    const auto* initial_owner = owner_model();
    if (!initial_owner || initial_owner->socket_index(anchor) == nw::render::kInvalidModelNodeIndex) {
        return skip_with_warning(fmt::format(
            "RenderModel creature equipped item '{}' in slot '{}' missing owner socket '{}'",
            item_resref_value,
            slot_name,
            anchor));
    }

    bool resolved_part = false;
    bool added = false;
    for (const auto& row : visual->models) {
        if (!visual_row_visible_for_mode(row, render_mode)
            || !visual_row_matches_slot(row, slot, nw::ObjectVisualModelKind::item_model)
            || row.model.empty()) {
            continue;
        }
        resolved_part = true;
        const auto model_resref = row.model;
        const std::string attachment_anchor = row.attach_to.empty() ? anchor : row.attach_to.string();
        const std::string attachment_source = row.attach_from.empty() ? std::string{} : row.attach_from.string();

        ERRARE("[viewer] resolving RenderModel equipped item model '{}'", model_resref);
        if (!nw::kernel::resman().contains({model_resref, nw::ResourceType::mdl})) {
            skip_with_warning(fmt::format(
                "RenderModel creature equipped model '{}' was not found for slot '{}' item '{}'",
                model_resref,
                slot_name,
                item_resref_value));
            continue;
        }
        const auto* owner = owner_model();
        if (!owner || owner->socket_index(attachment_anchor) == nw::render::kInvalidModelNodeIndex) {
            skip_with_warning(fmt::format(
                "RenderModel creature equipped item '{}' in slot '{}' missing owner socket '{}'",
                item_resref_value,
                slot_name,
                attachment_anchor));
            continue;
        }

        auto load = load_nwn_render_model_preview(resources, model_resref.view());
        if (load_events) {
            append_preview_load_events(*load_events, load.events);
        }
        if (!load.model) {
            skip_with_warning(fmt::format(
                "RenderModel creature equipped model '{}' failed to load for slot '{}' item '{}'",
                model_resref,
                slot_name,
                item_resref_value));
            continue;
        }
        if (!attachment_source.empty()
            && load.model->socket_index(attachment_source) == nw::render::kInvalidModelNodeIndex) {
            skip_with_warning(fmt::format(
                "RenderModel creature equipped model '{}' in slot '{}' missing child socket '{}'",
                model_resref,
                slot_name,
                attachment_source));
            continue;
        }

        LOG_F(INFO, "dynamic RenderModel creature equipped {} -> {}",
            nw::equip_index_to_string(slot), model_resref);
        const uint32_t child_model_index = static_cast<uint32_t>(scene.static_models.size());
        scene.add_attached(std::move(load.model), owner_model_index, attachment_anchor, attachment_source, local_scale);
        if (child_model_index < scene.static_models.size()) {
            if (auto* instance = scene.static_model_instance(child_model_index)) {
                instance->scene_animation_enabled = false;
                instance->animation.enabled = false;
            }
            auto item_plt = creature_colors ? visual_row_plt_colors(row, *creature_colors) : visual_row_plt_colors(row);
            apply_render_model_plt_material_overrides(scene, child_model_index, item_plt);
        }
        added = true;
    }

    if (!added && !resolved_part) {
        return skip_with_warning(fmt::format(
            "RenderModel creature equipped item '{}' in slot '{}' produced no attachable models",
            item_resref_value,
            slot_name));
    }

    return added;
}

static bool add_render_model_cloak_visual_model(
    PreviewScene& scene,
    PreviewRenderResources& resources,
    const nw::ObjectVisualState* visual,
    const nw::Item& cloak,
    const nw::PltColors& creature_colors,
    nw::ObjectVisualRenderMode render_mode,
    std::string_view origin,
    std::vector<PreviewLoadEvent>* load_events)
{
    auto warn = [&](std::string message) {
        LOG_F(WARNING, "{}", message);
        log_preview_warning_context();
        if (load_events) {
            add_preview_load_event(*load_events,
                PreviewLoadEventSeverity::warning,
                "nwn_render_model_creature_cloak",
                std::move(message));
        }
        return false;
    };

    if (!visual) {
        return warn(fmt::format(
            "{}: cloak item '{}' has no creature visual rows",
            origin,
            cloak.resref));
    }

    for (const auto& row : visual->models) {
        if (!visual_row_visible_for_mode(row, render_mode)
            || !visual_row_matches_slot(
                row, nw::EquipIndex::cloak, nw::ObjectVisualModelKind::creature_model_part)) {
            continue;
        }
        if (row.model.empty()) {
            return warn(fmt::format(
                "{}: cloak item '{}' model part {} has no resolved model",
                origin,
                cloak.resref,
                row.source_part));
        }

        ERRARE("[viewer] resolving RenderModel creature cloak model '{}'", row.model.view());
        auto load = load_nwn_render_model_preview(resources, row.model.view());
        if (load_events) {
            append_preview_load_events(*load_events, load.events);
        }
        if (!load.model) {
            return warn(fmt::format(
                "{}: cloak item '{}' model '{}' failed to load through RenderModel",
                origin,
                cloak.resref,
                row.model));
        }

        if (scene.static_models.size() >= nw::render::kInvalidModelInstanceIndex) {
            return warn(fmt::format(
                "{}: cloak item '{}' exceeded the RenderModel instance limit",
                origin,
                cloak.resref));
        }
        const uint32_t model_index = static_cast<uint32_t>(scene.static_models.size());
        scene.add(std::move(load.model));
        if (model_index >= scene.static_models.size()) {
            return warn(fmt::format(
                "{}: cloak item '{}' could not allocate a RenderModel instance",
                origin,
                cloak.resref));
        }
        apply_render_model_plt_material_overrides(
            scene, model_index, visual_row_plt_colors(row, creature_colors));
        return true;
    }

    return warn(fmt::format(
        "{}: cloak item '{}' produced no visible cloak model rows",
        origin,
        cloak.resref));
}

static bool add_render_model_creature_attachment(
    PreviewScene& scene,
    PreviewRenderResources& resources,
    const nw::ObjectVisualModel& row,
    float local_scale,
    const nw::PltColors& plt_colors,
    std::string_view origin,
    std::vector<PreviewLoadEvent>* load_events)
{
    const auto attachment_name = visual_creature_attachment_name(row);
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

    if (row.model.empty()) {
        return skip_with_warning(fmt::format(
            "{}: attachment '{}' row {} did not resolve to a model",
            origin,
            attachment_name,
            row.source_part));
    }
    if (scene.static_models.empty() || !scene.static_models.front()) {
        return skip_with_warning(fmt::format(
            "{}: attachment '{}' row {} has no RenderModel owner", origin, attachment_name, row.source_part));
    }

    const std::string owner_socket = row.attach_to.empty() ? std::string{} : row.attach_to.string();
    std::string source_socket = row.attach_from.empty() ? std::string{} : row.attach_from.string();
    if (owner_socket.empty()
        || scene.static_models.front()->socket_index(owner_socket) == nw::render::kInvalidModelNodeIndex) {
        return skip_with_warning(fmt::format(
            "{}: attachment '{}' row {} model '{}' missing owner socket '{}'",
            origin,
            attachment_name,
            row.source_part,
            row.model,
            owner_socket));
    }

    ERRARE("[viewer] resolving RenderModel creature attachment model '{}'", row.model.view());
    auto load = load_nwn_render_model_preview(resources, row.model.view());
    if (load_events) {
        append_preview_load_events(*load_events, load.events);
    }
    if (!load.model) {
        return skip_with_warning(fmt::format(
            "{}: attachment '{}' row {} model '{}' failed to load through RenderModel",
            origin,
            attachment_name,
            row.source_part,
            row.model));
    }
    if (!source_socket.empty()
        && load.model->socket_index(source_socket) == nw::render::kInvalidModelNodeIndex) {
        if (load_events) {
            add_preview_load_event(*load_events,
                PreviewLoadEventSeverity::info,
                "nwn_render_model_creature_attachment_root",
                fmt::format(
                    "{}: attachment '{}' row {} model '{}' has no child socket '{}'; using model root",
                    origin,
                    attachment_name,
                    row.source_part,
                    row.model,
                    source_socket));
        }
        source_socket.clear();
    }

    LOG_F(INFO, "dynamic RenderModel creature attachment {} row {} -> {}", attachment_name, row.source_part, row.model);
    const uint32_t child_model_index = static_cast<uint32_t>(scene.static_models.size());
    scene.add_attached(std::move(load.model), 0u, owner_socket, source_socket, local_scale);
    if (child_model_index < scene.static_models.size()) {
        apply_render_model_plt_material_overrides(scene, child_model_index, plt_colors);
    }
    return true;
}

static bool preview_equipped_slot_visible(
    nw::EquipIndex slot,
    const NwnAppearanceHandItemVisualPolicy& hand_item_policy) noexcept
{
    return (slot != nw::EquipIndex::righthand && slot != nw::EquipIndex::lefthand)
        || hand_item_policy.visible;
}

static float preview_equipped_slot_scale(
    nw::EquipIndex slot,
    const NwnAppearanceHandItemVisualPolicy& hand_item_policy,
    float helmet_scale) noexcept
{
    if (slot == nw::EquipIndex::head) {
        return helmet_scale;
    }
    if (slot == nw::EquipIndex::righthand || slot == nw::EquipIndex::lefthand) {
        return hand_item_policy.scale;
    }
    return 1.0f;
}

static bool add_dynamic_creature_scene_models(
    PreviewScene& scene,
    PreviewRenderResources& resources,
    const nw::Creature& creature,
    std::string_view origin,
    PreviewSceneLoadOptions options,
    std::vector<PreviewLoadEvent>* load_events)
{
    const nw::ObjectVisualState* visual = object_visual_state(creature);
    const nw::PltColors plt_colors = visual_base_plt_colors(visual);
    const nw::Appearance appearance_id = visual_appearance(visual);
    const uint8_t body_variant = visual_body_variant(visual);
    const nw::Equips& equips = creature.equipment;

    auto model_ref = resolve_creature_model_from_appearance(appearance_id);
    if (!model_ref.valid()) {
        const auto message = fmt::format(
            "Creature appearance {} did not resolve: {}",
            appearance_id.idx(),
            model_ref.error.empty() ? "unknown error" : model_ref.error);
        LOG_F(WARNING, "{}", message);
        log_preview_warning_context();
        if (load_events) {
            add_preview_load_event(*load_events,
                PreviewLoadEventSeverity::warning,
                "creature",
                message);
        }
        return false;
    }
    const float wing_tail_scale = model_ref.wing_tail_scale;
    const float helmet_scale = helmet_scale_from_creature_model(model_ref, body_variant);
    const auto hand_item_policy = hand_item_visual_policy_from_creature_model(model_ref);

    const std::string race{model_ref.race.view()};
    const char sex = body_variant == 1 ? 'f' : 'm';
    if (model_ref.humanoid) {
        auto* app = nw::kernel::rules().appearances.get(appearance_id);
        if (!app) {
            LOG_F(WARNING, "Creature humanoid appearance {} was not found in appearance rules", appearance_id.idx());
            log_preview_warning_context();
            return false;
        }

        auto base_rig_resref = resolve_creature_base_rig(*app, race, sex);
        if (!base_rig_resref) {
            const auto message = fmt::format(
                "{}: no humanoid base rig resolved for appearance {}",
                origin,
                appearance_id.idx());
            LOG_F(WARNING, "{}", message);
            log_preview_warning_context();
            if (load_events) {
                add_preview_load_event(*load_events,
                    PreviewLoadEventSeverity::warning,
                    "creature",
                    message);
            }
            return false;
        }

        ERRARE("[viewer] resolving dynamic creature base rig '{}'", std::string_view{*base_rig_resref});
        auto load = load_nwn_render_model_preview(resources, *base_rig_resref);
        if (load_events) {
            append_preview_load_events(*load_events, load.events);
        }
        if (!load.model) {
            const auto message = fmt::format(
                "{}: humanoid base rig '{}' failed to load through RenderModel",
                origin,
                *base_rig_resref);
            LOG_F(WARNING, "{}", message);
            log_preview_warning_context();
            if (load_events) {
                add_preview_load_event(*load_events,
                    PreviewLoadEventSeverity::warning,
                    "nwn_render_model_humanoid_body",
                    message);
            }
            return false;
        }

        scene.add(std::move(load.model));
        auto* base_rig_instance = scene.static_model_instance(0);
        if (!base_rig_instance) {
            LOG_F(ERROR, "{}: humanoid base rig '{}' did not allocate a RenderModel instance", origin, *base_rig_resref);
            log_preview_error_context();
            return false;
        }
        base_rig_instance->visible = false;
        base_rig_instance->shadow = {};

        if (!add_render_model_humanoid_body_rows(
                scene,
                resources,
                visual,
                plt_colors,
                options.visual_render_mode,
                origin,
                load_events)) {
            return false;
        }
    } else {
        if (!model_ref.has_model()) {
            const auto message = fmt::format(
                "Creature appearance {} did not resolve to a single model: {}",
                appearance_id.idx(),
                model_ref.error.empty() ? "unknown error" : model_ref.error);
            LOG_F(WARNING, "{}", message);
            log_preview_warning_context();
            if (load_events) {
                add_preview_load_event(*load_events,
                    PreviewLoadEventSeverity::warning,
                    "creature",
                    message);
            }
            return false;
        }

        ERRARE("[viewer] resolving creature model '{}'", model_ref.model.view());
        auto load = load_nwn_render_model_preview(resources, model_ref.model.view());
        if (load_events) {
            append_preview_load_events(*load_events, load.events);
        }
        const uint32_t body_model_index = static_cast<uint32_t>(scene.static_models.size());
        scene.add(std::move(load.model));
        if (body_model_index < scene.static_models.size()) {
            apply_render_model_plt_material_overrides(scene, body_model_index, plt_colors);
        }
    }

    bool loaded_attachments = visual != nullptr;
    if (visual) {
        for (const auto& row : visual->models) {
            if (!visual_row_visible_for_mode(row, options.visual_render_mode)
                || !visual_row_is_creature_attachment(row)) {
                continue;
            }
            loaded_attachments = add_render_model_creature_attachment(
                                     scene, resources, row, wing_tail_scale, plt_colors, origin, load_events)
                && loaded_attachments;
        }
    }
    bool loaded_equipped_models = true;
    for (const auto slot : kPreviewAttachedEquipmentSlots) {
        auto* item = equipped_item(equips, slot);
        if (!item || !preview_equipped_slot_visible(slot, hand_item_policy)) {
            continue;
        }
        loaded_equipped_models = add_render_model_equipped_visual_models(
                                     scene,
                                     resources,
                                     visual,
                                     *item,
                                     slot,
                                     0u,
                                     preview_equipped_slot_scale(slot, hand_item_policy, helmet_scale),
                                     options.visual_render_mode,
                                     load_events,
                                     &plt_colors)
            && loaded_equipped_models;
    }
    bool loaded_cloak_model = true;
    if (auto* cloak = equipped_item(equips, nw::EquipIndex::cloak)) {
        loaded_cloak_model = add_render_model_cloak_visual_model(
            scene,
            resources,
            visual,
            *cloak,
            plt_colors,
            options.visual_render_mode,
            origin,
            load_events);
    }
    if (!loaded_attachments || !loaded_equipped_models || !loaded_cloak_model) {
        const auto message = fmt::format(
            "{}: RenderModel creature path skipped or failed some optional attachment or equipment rows",
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

static std::unique_ptr<PreviewScene> load_dynamic_creature_scene(
    PreviewRenderResources& resources, const std::filesystem::path& path, PreviewSceneLoadOptions options)
{
    const auto path_text = path.string();
    ERRARE("[viewer] loading dynamic creature preview '{}'", std::string_view{path_text});

    auto scene = std::make_unique<PreviewScene>();
    std::vector<PreviewLoadEvent> load_events;
    PreviewCreatureObject preview_creature;

    const nw::Resource resource = nw::Resource::from_path(path, false);
    if (resource.type == nw::ResourceType::bic) {
        auto* player = nw::kernel::objects().make<nw::Player>();
        preview_creature.reset(player);
        if (!player || !load_player_from_file(path, *player)) {
            return {};
        }
    } else {
        auto* creature = nw::kernel::objects().make<nw::Creature>();
        preview_creature.reset(creature);
        if (!creature || !load_creature_from_file(path, *creature)) {
            return {};
        }
    }

    resolve_preview_equipment(*preview_creature, path);
    if (!preview_creature->instantiate()) {
        LOG_F(ERROR, "Dynamic creature preview '{}' failed to instantiate", path.string());
        log_preview_error_context();
        return {};
    }

    if (!add_dynamic_creature_scene_models(*scene,
            resources,
            *preview_creature,
            path_text,
            options,
            &load_events)) {
        return {};
    }
    append_scene_authored_model_lights(*scene);
    scene->rebuild_load_report(path_text, "dynamic_creature");
    append_scene_load_events(*scene, load_events);
    scene->root_object = preview_creature->handle();
    scene->active_object = scene->root_object;
    preview_creature.release();
    return scene;
}

static bool add_item_scene_models(
    PreviewScene& scene,
    PreviewRenderResources& resources,
    nw::Item& item,
    std::string_view origin,
    bool use_default_fallback)
{
    if (!update_standalone_item_visual(item, use_default_fallback, origin)) {
        LOG_F(ERROR, "Item preview '{}' failed to produce visual rows", origin);
        log_preview_error_context();
        return false;
    }

    const nw::ObjectVisualState* visual = object_visual_state(item);
    if (!visual) {
        LOG_F(ERROR, "Item preview '{}' has no visual state", origin);
        log_preview_error_context();
        return false;
    }

    for (const auto& row : visual->models) {
        if (!visual_row_visible_for_mode(row, nw::ObjectVisualRenderMode::game)
            || row.kind != nw::ObjectVisualModelKind::item_model) {
            continue;
        }

        const auto resref = row.model.view();
        if (resref.empty()) {
            continue;
        }
        ERRARE("[viewer] resolving item preview model '{}'", resref);
        if (!nw::kernel::resman().contains({nw::Resref{resref}, nw::ResourceType::mdl})) {
            LOG_F(WARNING, "Item preview '{}' model '{}' was not found",
                origin,
                resref);
            log_preview_warning_context();
            continue;
        }
        auto load = load_nwn_render_model_preview(resources, resref);
        append_preview_load_events(scene.source_load_events, load.events);
        if (!load.model) {
            LOG_F(WARNING, "Item preview '{}' model '{}' failed to load",
                origin,
                resref);
            log_preview_warning_context();
            continue;
        }
        const uint32_t model_index = static_cast<uint32_t>(scene.static_models.size());
        scene.add(std::move(load.model));
        if (model_index < scene.static_models.size()) {
            apply_render_model_plt_material_overrides(scene, model_index, visual_row_plt_colors(row));
        }
    }

    return true;
}

static std::unique_ptr<PreviewScene> load_item_scene(PreviewRenderResources& resources, const std::filesystem::path& path)
{
    const auto path_text = path.string();
    ERRARE("[viewer] loading item preview '{}'", std::string_view{path_text});

    auto item = load_managed_preview_object<nw::Item>(path, load_item_from_file);
    if (!item) {
        return {};
    }

    if (!item->instantiate()) {
        LOG_F(ERROR, "Item preview '{}' failed to instantiate", path.string());
        log_preview_error_context();
        return {};
    }

    auto scene = std::make_unique<PreviewScene>();
    if (!add_item_scene_models(*scene, resources, *item, path_text, true)) {
        return {};
    }
    const bool has_renderable_model = !scene->static_models.empty();
    if (!has_renderable_model) {
        LOG_F(WARNING, "Item preview '{}' produced no renderable models", path.string());
        log_preview_warning_context();
    }
    append_scene_authored_model_lights(*scene);
    scene->rebuild_load_report(path_text, "item");
    if (!has_renderable_model) {
        scene->load_report.events.push_back({
            .severity = PreviewLoadEventSeverity::warning,
            .category = "item_model",
            .message = "The live Item has no renderable preview model",
        });
    }
    scene->root_object = item->handle();
    scene->active_object = scene->root_object;
    item.release();
    return scene;
}

static std::unique_ptr<PreviewScene> load_blueprint_model_scene(PreviewRenderResources& resources,
    const std::filesystem::path& path,
    std::string_view preview_type,
    std::string_view lookup_context,
    std::string_view model_resref,
    bool include_authored_lights)
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
    auto load = load_nwn_render_model_preview(resources, model_resref);
    append_preview_load_events(scene->source_load_events, load.events);
    scene->add(std::move(load.model));
    if (scene->static_models.empty()) {
        LOG_F(ERROR, "{} preview '{}' failed to load model '{}' for {}",
            preview_type, path.string(), model_resref, lookup_context);
        log_preview_error_context();
        return {};
    }
    if (include_authored_lights) {
        append_scene_authored_model_lights(*scene);
    }
    scene->rebuild_load_report(path_text, preview_type);
    return scene;
}

static std::unique_ptr<nw::render::RenderModel> load_area_object_model(
    PreviewRenderResources& resources,
    std::string_view model_resref,
    std::string_view origin)
{
    if (model_resref.empty()) {
        return {};
    }

    auto load = load_nwn_render_model_preview(resources, model_resref);
    if (!load.model) {
        LOG_F(WARNING, "Failed to load area object model '{}' for {}", model_resref, origin);
        return {};
    }
    return std::move(load.model);
}

struct AreaStaticModelCache {
    absl::flat_hash_map<nw::Resref, std::shared_ptr<nw::render::RenderModel>> models;
    size_t request_count = 0;
    size_t local_hit_count = 0;
    size_t retained_hit_count = 0;
    size_t import_count = 0;
    size_t failure_count = 0;
};

static std::shared_ptr<nw::render::RenderModel> load_area_static_model(
    AreaStaticModelCache& cache,
    PreviewRenderResources& resources,
    std::string_view model_resref,
    std::string_view origin)
{
    if (model_resref.empty()) {
        return {};
    }

    const nw::Resref key{model_resref};
    const auto cached = cache.models.find(key);
    if (cached != cache.models.end()) {
        ++cache.local_hit_count;
        return cached->second;
    }

    ++cache.request_count;
    auto shared_model = resources.find_area_static_model(key);
    if (shared_model) {
        ++cache.retained_hit_count;
    } else {
        ++cache.import_count;
        auto model = load_area_object_model(resources, model_resref, origin);
        shared_model = std::shared_ptr<nw::render::RenderModel>{std::move(model)};
        resources.store_area_static_model(key, shared_model);
    }
    if (!shared_model) {
        ++cache.failure_count;
    }
    cache.models.emplace(key, shared_model);
    return shared_model;
}

static uint32_t add_placed_render_model(
    PreviewScene& scene,
    std::shared_ptr<nw::render::RenderModel> model,
    const glm::mat4& placement)
{
    if (!model || scene.static_models.size() >= nw::render::kInvalidModelInstanceIndex) {
        return nw::render::kInvalidModelInstanceIndex;
    }

    const uint32_t model_index = static_cast<uint32_t>(scene.static_models.size());
    scene.add(std::move(model));
    auto* instance = scene.static_model_instance(model_index);
    if (!instance || model_index >= scene.static_models.size() || !scene.static_models[model_index]) {
        return nw::render::kInvalidModelInstanceIndex;
    }

    instance->root_transform = placement;
    nw::render::publish_render_model_static_node_world_transforms(
        *instance, *scene.static_models[model_index]);
    instance->current_bounds = transform_bounds(scene.static_models[model_index]->bounds, placement);
    instance->shadow = render_model_shadow_summary(
        *scene.static_models[model_index], instance->current_bounds);
    return model_index;
}

static uint32_t add_placed_render_model(
    PreviewScene& scene,
    std::unique_ptr<nw::render::RenderModel> model,
    const glm::mat4& placement)
{
    return add_placed_render_model(
        scene,
        std::shared_ptr<nw::render::RenderModel>{std::move(model)},
        placement);
}

static void set_render_scene_root_placement(PreviewScene& scene, const glm::mat4& placement)
{
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
    target_instance->render_model_index = target_model_index;

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
        const auto* source_child = source.model_instances.get(binding.child_instance_handle);
        const auto* source_owner = source.model_instances.get(binding.owner_instance_handle);
        if (!source_child || !source_owner
            || source_child->render_model_index >= model_index_map.size()
            || source_owner->render_model_index >= model_index_map.size()) {
            continue;
        }

        const uint32_t target_child_index = model_index_map[source_child->render_model_index];
        const uint32_t target_owner_index = model_index_map[source_owner->render_model_index];
        if (target_child_index == nw::render::kInvalidModelInstanceIndex
            || target_owner_index == nw::render::kInvalidModelInstanceIndex
            || target_child_index >= target.static_model_instance_handles.size()
            || target_owner_index >= target.static_model_instance_handles.size()) {
            continue;
        }
        if (target.model_attachments.size() >= kInvalidSceneModelAttachmentBindingIndex) {
            return;
        }

        const uint32_t binding_index = static_cast<uint32_t>(target.model_attachments.size());
        target.model_attachments.push_back(nw::render::ModelInstanceAttachmentBinding{
            .child_instance_handle = target.static_model_instance_handles[target_child_index],
            .owner_instance_handle = target.static_model_instance_handles[target_owner_index],
            .owner_socket_index = binding.owner_socket_index,
            .child_source_socket_index = binding.child_source_socket_index,
            .child_local_transform = binding.child_local_transform,
            .child_root_bind_translation = binding.child_root_bind_translation,
            .child_local_scale = binding.child_local_scale,
            .orientation = binding.orientation,
            .source_offset = binding.source_offset,
        });
        target.static_model_attachment_binding_indices[target_child_index] = binding_index;
    }
}

static size_t append_render_models(
    PreviewScene& target,
    PreviewScene& source,
    AreaRenderSourceInfo area_info = {},
    bool include_authored_lights = true)
{
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
            if (include_authored_lights) {
                append_render_model_authored_lights(target, target_model_index);
            }
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
    source.local_lights.clear();
    source.render_local_lights.clear();
    return render_model_count;
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
    auto scene = std::make_unique<PreviewScene>();
    if (!add_dynamic_creature_scene_models(*scene,
            resources,
            creature,
            origin,
            options,
            nullptr)) {
        return {};
    }

    set_render_scene_root_placement(*scene, object_spatial_placement(creature));
    return scene;
}

static std::unique_ptr<PreviewScene> load_area_placeable_scene(
    PreviewRenderResources& resources,
    nw::Placeable& placeable,
    std::string_view origin,
    PreviewSceneLoadOptions options)
{
    if (!placeable.instantiate()) {
        LOG_F(WARNING, "Area placeable '{}' failed to instantiate", origin);
        return {};
    }

    const auto* visual = placeable_visual_state(placeable);
    const auto* model_ref = first_valid_visual_model(visual, options.visual_render_mode);
    if (!model_ref) {
        LOG_F(WARNING, "Area placeable '{}': {}", origin, placeable_visual_error(visual));
        return {};
    }

    auto model = load_area_object_model(resources, model_ref->model.view(), origin);
    if (!model) {
        return {};
    }
    auto scene = std::make_unique<PreviewScene>();
    add_placed_render_model(*scene, std::move(model), object_spatial_placement(placeable));
    return scene;
}

void make_area_object_preview_translucent(PreviewScene& scene, float opacity)
{
    for (size_t model_index = 0; model_index < scene.static_models.size(); ++model_index) {
        auto& model = scene.static_models[model_index];
        auto* instance = scene.static_model_instance(model_index);
        if (!model || !instance) {
            continue;
        }
        for (auto& material : model->materials) {
            material.albedo.a *= opacity;
            material.alpha_mode = nw::render::MaterialMode::transparent;
        }
        for (auto& primitive : model->primitives) {
            primitive.casts_shadow = false;
        }
        model->shadow = {};
        model->particle_systems.clear();
        for (const auto handle : instance->material_override_handles) {
            if (auto* material_override = scene.material_overrides.get(handle)) {
                material_override->material.albedo.a *= opacity;
                material_override->material.alpha_mode = nw::render::MaterialMode::transparent;
            }
        }
        instance->shadow = {};
    }
    scene.particles.clear();
    scene.local_lights.clear();
    scene.render_local_lights.clear();
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

    set_render_scene_root_placement(*scene, object_spatial_placement(item));
    return scene;
}

struct WaypointModelLoad {
    nw::Resref model;
    int32_t appearance = -1;
    std::string error;

    [[nodiscard]] bool valid() const noexcept
    {
        return appearance >= 0 && !model.empty() && error.empty();
    }
};

static WaypointModelLoad resolve_waypoint_model(const nw::Waypoint& waypoint)
{
    WaypointModelLoad result;
    auto& runtime = nw::kernel::runtime();
    const auto propset_type = runtime.type_id("nwn1.propsets.WaypointState", false);
    const auto* definition = runtime.get_struct_def(propset_type);
    if (!definition) {
        result.error = "WaypointState propset type is unavailable";
        return result;
    }

    const uint32_t appearance_field = definition->field_index("appearance");
    if (appearance_field == std::numeric_limits<uint32_t>::max()) {
        result.error = "WaypointState has no appearance field";
        return result;
    }

    const auto propset = runtime.find_propset_ref(propset_type, waypoint.handle());
    const auto appearance = runtime.read_struct_value_field(
        propset, definition, appearance_field);
    if (appearance.type_id != runtime.int_type() || appearance.data.ival < 0) {
        result.error = "WaypointState appearance is missing or negative";
        return result;
    }
    result.appearance = appearance.data.ival;

    const auto* table = nw::kernel::twodas().get("waypoint");
    const size_t row = static_cast<size_t>(result.appearance);
    if (!table || row >= table->rows()) {
        result.error = fmt::format(
            "appearance {} is outside waypoint.2da", result.appearance);
        return result;
    }

    const auto model = table->get<nw::String>(row, "RESREF", false);
    if (!model || model->empty()) {
        result.error = fmt::format(
            "appearance {} has no RESREF in waypoint.2da", result.appearance);
        return result;
    }
    result.model = nw::Resref{*model};
    if (result.model.empty()) {
        result.error = fmt::format(
            "appearance {} has an invalid RESREF in waypoint.2da", result.appearance);
    }
    return result;
}

static std::unique_ptr<PreviewScene> load_live_door_scene(
    PreviewRenderResources& resources,
    nw::Door& door,
    const std::filesystem::path& source,
    bool include_authored_lights)
{
    if (!door.instantiate()) {
        LOG_F(ERROR, "Door preview '{}' failed to instantiate", source.string());
        log_preview_error_context();
        return {};
    }

    auto model_ref = resolve_door_model_for_object(door);
    if (!model_ref.valid) {
        LOG_F(ERROR, "Door preview '{}': {}", source.string(), model_ref.error);
        log_preview_error_context();
        return {};
    }

    return load_blueprint_model_scene(resources, source, "Door",
        door_model_lookup_context(model_ref), model_ref.model.view(),
        include_authored_lights);
}

static std::unique_ptr<PreviewScene> load_door_scene(PreviewRenderResources& resources, const std::filesystem::path& path)
{
    const auto path_text = path.string();
    ERRARE("[viewer] loading door preview '{}'", std::string_view{path_text});

    auto door = load_managed_preview_object<nw::Door>(path, load_door_from_file);
    if (!door) {
        LOG_F(ERROR, "Door preview '{}' failed to load a live Door", path.string());
        log_preview_error_context();
        return {};
    }
    auto scene = load_live_door_scene(resources, *door, path, true);
    if (!scene) {
        return {};
    }
    scene->root_object = door->handle();
    scene->active_object = scene->root_object;
    door.release();
    return scene;
}

static std::unique_ptr<PreviewScene> load_placeable_scene(PreviewRenderResources& resources, const std::filesystem::path& path)
{
    const auto path_text = path.string();
    ERRARE("[viewer] loading placeable preview '{}'", std::string_view{path_text});

    PreviewPlaceableVisualLoad visual = load_placeable_visual_from_file(path);
    if (!visual.loaded) {
        LOG_F(ERROR, "Placeable preview '{}': {}", path.string(), visual.error);
        log_preview_error_context();
        return {};
    }

    const auto* model = first_valid_visual_model(std::span<const nw::ObjectVisualModel>{visual.visual.models});
    if (!model) {
        LOG_F(ERROR, "Placeable preview '{}': {}",
            path.string(), visual.error.empty() ? placeable_visual_error(&visual.visual) : visual.error);
        log_preview_error_context();
        return {};
    }

    const auto lookup_context = fmt::format("visual appearance {}", visual.visual.appearance);
    auto scene = load_blueprint_model_scene(
        resources, path, "Placeable", lookup_context, model->model.view(), true);
    if (scene) {
        scene->hold_animation = visual.visual.hold_animation.view();
        append_placeable_table_lights(*scene, nw::Location{}, &visual.visual);
    }
    return scene;
}

namespace {

nw::render::ModelAssetTextureUploadDesc preview_model_asset_texture_upload_desc(PreviewRenderResources& resources)
{
    return nw::render::ModelAssetTextureUploadDesc{
        .ctx = resources.context(),
        .fallback_albedo = resources.fallback_albedo_index(),
        .missing_albedo = resources.missing_albedo_index(),
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
        .missing_albedo = texture_upload.missing_albedo,
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
                "{}: materials={} sources={} uploaded={} fallback_materials={} invalid_bindings={} missing_context={} missing_payloads={} decode_failures={} size_mismatches={} create_failures={} upload_failures={} bindless_failures={} context_mismatches={}",
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
                textures.bindless_failure_count,
                textures.resource_context_mismatch_count));
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
            "{}: imported through ModelAsset -> RenderModel with common materials, animation, deformers, particles, and prepared surfaces",
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
            "decode_failures={} size_mismatches={} create_failures={} upload_failures={} bindless_failures={} context_mismatches={}",
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
            textures.bindless_failure_count,
            textures.resource_context_mismatch_count);
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
    load.static_deformer_count = result.import_stats.secondary_motion_deformer_count;
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
    PreviewScene& scene, PreviewRenderResources& resources, std::string_view source)
{
    auto load = load_nwn_render_model_preview(resources, source);
    if (load.static_deformer_count != 0u) {
        add_preview_load_event(load.events,
            PreviewLoadEventSeverity::warning,
            "nwn_render_model_static_deformer",
            fmt::format(
                "{}: rendering {} unsupported secondary-motion primitives as static source geometry",
                source,
                load.static_deformer_count));
    }
    scene.add(std::move(load.model));
    return std::move(load.events);
}

bool scene_has_preview_model(const PreviewScene& scene) noexcept
{
    return !scene.static_models.empty();
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
    auto load_events = maybe_add_nwn_preview_model(*scene, resources, source);
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
    ERRARE("[viewer] loading multi-source preview ({} sources)", sources.size());

    auto scene = std::make_unique<PreviewScene>();
    std::vector<PreviewLoadEvent> load_events;
    for (const auto& source : sources) {
        ERRARE("[viewer] loading preview source '{}'", std::string_view{source});
        auto source_events = maybe_add_nwn_preview_model(*scene, resources, source);
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

namespace {

std::unique_ptr<PreviewScene> build_area_scene_impl(
    PreviewRenderResources& resources,
    std::string_view area_resref,
    PreviewSceneLoadOptions options,
    nw::Area* loaded_area)
{
    const bool owns_loaded_area = loaded_area == nullptr;
    nw::ObjectManager::AreaLoadProfile profile{};
    if (owns_loaded_area) {
        const nw::Resref area{area_resref};
        const auto format = nw::kernel::resman().module_format();
        const auto area_type = format == nw::ModuleResourceFormat::native_json
            ? nw::ResourceType::caf
            : nw::ResourceType::are;
        if (format == nw::ModuleResourceFormat::invalid
            || !nw::kernel::resman().contains({area, area_type})) {
            LOG_F(ERROR, "Area does not exist: {}", area_resref);
            return {};
        }

        loaded_area = nw::kernel::objects().make_area(area, &profile);
        if (!loaded_area) {
            LOG_F(ERROR, "Failed to load area: {}", area_resref);
            return {};
        }
    } else {
        profile.creatures = static_cast<uint32_t>(loaded_area->creatures.size());
        profile.doors = static_cast<uint32_t>(loaded_area->doors.size());
        profile.encounters = static_cast<uint32_t>(loaded_area->encounters.size());
        profile.items = static_cast<uint32_t>(loaded_area->items.size());
        profile.placeables = static_cast<uint32_t>(loaded_area->placeables.size());
        profile.sounds = static_cast<uint32_t>(loaded_area->sounds.size());
        profile.stores = static_cast<uint32_t>(loaded_area->stores.size());
        profile.triggers = static_cast<uint32_t>(loaded_area->triggers.size());
        profile.waypoints = static_cast<uint32_t>(loaded_area->waypoints.size());
    }

    if (loaded_area->tileset_resref.empty()) {
        LOG_F(ERROR, "Area is missing a valid tileset: {}", area_resref);
        if (owns_loaded_area) {
            loaded_area->clear();
            nw::kernel::objects().destroy(loaded_area->handle());
        }
        return {};
    }

    if (owns_loaded_area || !loaded_area->tileset) {
        try {
            if (!loaded_area->instantiate() || !loaded_area->tileset) {
                LOG_F(ERROR, "Failed to instantiate area or tileset: {}", area_resref);
                if (owns_loaded_area) {
                    loaded_area->clear();
                    nw::kernel::objects().destroy(loaded_area->handle());
                }
                return {};
            }
        } catch (const std::exception& ex) {
            LOG_F(ERROR, "Failed to instantiate area {}: {}", area_resref, ex.what());
            if (owns_loaded_area) {
                loaded_area->clear();
                nw::kernel::objects().destroy(loaded_area->handle());
            }
            return {};
        }
    }

    auto scene = std::make_unique<PreviewScene>();
    scene->root_object = loaded_area->handle();
    scene->owns_root_object = owns_loaded_area;
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
    resources.prepare_area_static_models(nw::kernel::resman().generation());
    AreaStaticModelCache static_model_cache;
    size_t loaded_creature_models = 0;
    size_t loaded_door_models = 0;
    size_t loaded_encounter_debug_shapes = 0;
    size_t loaded_item_models = 0;
    size_t loaded_placeable_models = 0;
    size_t loaded_waypoint_models = 0;
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

            auto model = load_area_static_model(
                static_model_cache,
                resources,
                tile_def.model,
                area_resref);
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
            const uint32_t model_index = add_placed_render_model(*scene, std::move(model), placement);
            if (model_index != nw::render::kInvalidModelInstanceIndex) {
                if (auto* instance = scene->static_model_instance(model_index)) {
                    instance->scene_animation_enabled = false;
                    instance->animation.enabled = false;
                }
                loaded_tile_model_lights += append_tile_render_model_lights(
                    *scene, model_index, tile, x, y);
                scene->static_area_model_info[model_index] = AreaRenderSourceInfo{
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

        const auto origin = area_object_origin(area_resref, "creature", i, *creature);
        auto creature_scene = load_area_creature_scene(resources, *creature, origin, options);
        if (creature_scene) {
            const size_t light_count_before = scene->local_lights.size();
            loaded_creature_models += append_render_models(*scene, *creature_scene, AreaRenderSourceInfo{
                                                                                        .kind = AreaRenderRecordKind::creature,
                                                                                        .object = creature->handle(),
                                                                                    });
            loaded_area_object_model_lights += scene->local_lights.size() - light_count_before;
        }
    }

    for (size_t i = 0; i < loaded_area->doors.size(); ++i) {
        const auto* door = loaded_area->doors[i];
        if (!door) {
            continue;
        }

        const auto origin = area_object_origin(area_resref, "door", i, *door);
        auto model_ref = resolve_door_model_for_object(*door);
        if (!model_ref.valid) {
            LOG_F(WARNING, "Area door '{}' has no render model: {}", origin, model_ref.error);
            continue;
        }

        auto model = load_area_static_model(
            static_model_cache, resources, model_ref.model.view(), origin);
        const uint32_t model_index = add_placed_render_model(
            *scene, std::move(model), object_spatial_placement(*door));
        if (model_index != nw::render::kInvalidModelInstanceIndex) {
            if (model_index < scene->static_area_model_info.size()) {
                scene->static_area_model_info[model_index] = AreaRenderSourceInfo{
                    .kind = AreaRenderRecordKind::door,
                    .object = door->handle(),
                    .static_candidate = true,
                };
            }
            loaded_area_object_model_lights += append_render_model_authored_lights(*scene, model_index);
            ++loaded_door_models;
        }
    }

    for (size_t i = 0; i < loaded_area->items.size(); ++i) {
        auto* item = loaded_area->items[i];
        if (!item) {
            continue;
        }

        const auto origin = area_object_origin(area_resref, "item", i, *item);
        auto item_scene = load_area_item_scene(resources, *item, origin);
        if (item_scene) {
            const size_t light_count_before = scene->local_lights.size();
            loaded_item_models += append_render_models(*scene, *item_scene, AreaRenderSourceInfo{
                                                                                .kind = AreaRenderRecordKind::item,
                                                                                .object = item->handle(),
                                                                            });
            loaded_area_object_model_lights += scene->local_lights.size() - light_count_before;
        }
    }

    for (size_t i = 0; i < loaded_area->placeables.size(); ++i) {
        auto* placeable = loaded_area->placeables[i];
        if (!placeable) {
            continue;
        }

        const auto origin = area_object_origin(area_resref, "placeable", i, *placeable);
        if (!placeable->instantiate()) {
            LOG_F(WARNING, "Area placeable '{}' failed to instantiate", origin);
            continue;
        }

        const auto* visual = placeable_visual_state(*placeable);
        const nw::Location location = object_spatial_location(*placeable);
        loaded_area_object_model_lights += append_placeable_table_lights(*scene, location, visual);
        const auto* model_ref = first_valid_visual_model(visual, options.visual_render_mode);
        if (!model_ref) {
            LOG_F(WARNING,
                "Area placeable '{}': {}",
                origin,
                placeable_visual_error(visual));
            continue;
        }

        auto model = load_area_static_model(
            static_model_cache, resources, model_ref->model.view(), origin);
        const uint32_t model_index = add_placed_render_model(
            *scene, std::move(model), object_spatial_placement(*placeable));
        if (model_index != nw::render::kInvalidModelInstanceIndex) {
            if (model_index < scene->static_area_model_info.size()) {
                scene->static_area_model_info[model_index] = AreaRenderSourceInfo{
                    .kind = AreaRenderRecordKind::placeable,
                    .object = placeable->handle(),
                    .static_candidate = !options.area_object_editing,
                };
            }
            loaded_area_object_model_lights += append_render_model_authored_lights(*scene, model_index);
            ++loaded_placeable_models;
        }
    }

    for (size_t i = 0; i < loaded_area->waypoints.size(); ++i) {
        const auto* waypoint = loaded_area->waypoints[i];
        if (!waypoint) {
            continue;
        }

        const auto origin = area_object_origin(area_resref, "waypoint", i, *waypoint);
        const auto model_ref = resolve_waypoint_model(*waypoint);
        if (!model_ref.valid()) {
            LOG_F(WARNING, "Area waypoint '{}': {}", origin, model_ref.error);
            continue;
        }

        auto model = load_area_static_model(
            static_model_cache, resources, model_ref.model.view(), origin);
        const uint32_t model_index = add_placed_render_model(
            *scene, std::move(model), object_spatial_placement(*waypoint));
        if (model_index != nw::render::kInvalidModelInstanceIndex) {
            if (model_index < scene->static_area_model_info.size()) {
                scene->static_area_model_info[model_index] = AreaRenderSourceInfo{
                    .kind = AreaRenderRecordKind::waypoint,
                    .object = waypoint->handle(),
                    .static_candidate = !options.area_object_editing,
                };
            }
            loaded_area_object_model_lights += append_render_model_authored_lights(*scene, model_index);
            ++loaded_waypoint_models;
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
            const float color = std::max(light.color.x, std::max(light.color.y, light.color.z));
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
        "Loaded area {} tileset={} : {}x{} tiles={} loaded_models={} static_assets={} static_local_hits={} static_retained_hits={} static_imports={} static_asset_failures={} flags interior={} underground={} cycle={} night={} tile_lights={} object_lights={} light_slot_tiles={} local_lights={} colored_lights={} light_color_max={:.2f} light_radius=[{:.2f}..{:.2f}] light_intensity=[{:.2f}..{:.2f}] light_scale[r={:.2f}, i={:.2f}] creatures={}/{} doors={}/{} items={}/{} placeables={}/{} waypoints={}/{} triggers={}/{} encounters={}/{} debug_indices={} demand={}ms deserialize={}ms placement[x={}..{}, y={}..{}]",
        area_resref,
        loaded_area->tileset_resref,
        loaded_area->width,
        loaded_area->height,
        loaded_area->tiles.size(),
        scene->static_models.size(),
        static_model_cache.request_count - static_model_cache.failure_count,
        static_model_cache.local_hit_count,
        static_model_cache.retained_hit_count,
        static_model_cache.import_count,
        static_model_cache.failure_count,
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
        loaded_waypoint_models,
        profile.waypoints,
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

    if (!scene_has_preview_model(*scene)) {
        return {};
    }
    scene->area_overlay_z = max_tile_z == std::numeric_limits<float>::lowest() ? 0.0f : max_tile_z;
    scene->area_render_scene = std::make_unique<AreaRenderScene>();
    scene->area_render_scene->rebuild(*scene);
    const auto& area_cache_stats = scene->area_render_scene->stats();
    LOG_F(INFO,
        "Area render cache {} records={} static={} dynamic={} prepared_draws={} surfaces[ranges={} triangles={} bytes={}] max_prepared_record={} chunks={}/{} max_chunk={} pass[o/w/t]={}/{}/{} shadow_casters={} source[tile/creature/door/item/placeable/waypoint/unknown]={}/{}/{}/{}/{}/{}/{}",
        area_resref,
        area_cache_stats.record_count,
        area_cache_stats.static_record_count,
        area_cache_stats.dynamic_record_count,
        area_cache_stats.prepared_draw_count,
        area_cache_stats.surface_range_count,
        area_cache_stats.surface_triangle_count,
        area_cache_stats.surface_bytes,
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
        area_cache_stats.waypoint_record_count,
        area_cache_stats.unknown_record_count);
    scene->rebuild_load_report(area_resref, "area");
    return scene;
}

} // namespace

std::unique_ptr<PreviewScene> load_area_scene(
    PreviewRenderResources& resources, std::string_view area_resref, PreviewSceneLoadOptions options)
{
    return build_area_scene_impl(resources, area_resref, options, nullptr);
}

std::unique_ptr<PreviewScene> build_live_area_scene(
    PreviewRenderResources& resources,
    nw::Area& area,
    std::string_view source,
    PreviewSceneLoadOptions options)
{
    return build_area_scene_impl(resources, source, options, &area);
}

std::unique_ptr<PreviewScene> build_live_object_scene(
    PreviewRenderResources& resources,
    nw::ObjectHandle object,
    std::string_view source,
    PreviewSceneLoadOptions options)
{
    std::unique_ptr<PreviewScene> scene;
    switch (object.type) {
    case nw::ObjectType::creature:
        if (auto* creature = nw::kernel::objects().get<nw::Creature>(object)) {
            scene = load_area_creature_scene(resources, *creature, source, options);
        }
        break;
    case nw::ObjectType::placeable:
        if (auto* placeable = nw::kernel::objects().get<nw::Placeable>(object)) {
            scene = load_area_placeable_scene(resources, *placeable, source, options);
        }
        break;
    case nw::ObjectType::door:
        if (auto* door = nw::kernel::objects().get<nw::Door>(object)) {
            scene = load_live_door_scene(
                resources, *door, std::filesystem::path{source}, false);
        }
        break;
    default:
        return {};
    }
    if (!scene) {
        return {};
    }

    append_scene_authored_model_lights(*scene);
    scene->rebuild_load_report(source, "live_object");
    scene->root_object = object;
    scene->active_object = object;
    scene->owns_root_object = false;
    return scene;
}

namespace {

bool contains_object(std::span<const nw::ObjectHandle> objects, nw::ObjectHandle object)
{
    return std::find(objects.begin(), objects.end(), object) != objects.end();
}

bool contains_instance_handle(
    std::span<const nw::render::ModelInstanceHandle> handles,
    nw::render::ModelInstanceHandle handle)
{
    return std::find(handles.begin(), handles.end(), handle) != handles.end();
}

bool valid_scene_model_columns(const PreviewScene& scene) noexcept
{
    constexpr size_t max_model_count = std::numeric_limits<uint32_t>::max();
    return scene.static_models.size() <= max_model_count
        && scene.model_attachments.size() < kInvalidSceneModelAttachmentBindingIndex
        && scene.static_models.size() == scene.static_model_instance_handles.size()
        && scene.static_models.size() == scene.static_model_attachment_binding_indices.size()
        && scene.static_models.size() == scene.static_area_model_info.size();
}

void destroy_scene_instance(
    PreviewScene& scene, nw::render::ModelInstanceHandle handle) noexcept
{
    if (auto* instance = scene.model_instances.get(handle)) {
        for (const auto material : instance->material_override_handles) {
            scene.material_overrides.destroy(material);
        }
    }
    scene.model_instances.destroy(handle);
}

void rebuild_scene_model_summaries(PreviewScene& scene)
{
    scene.vertex_count = 0;
    scene.index_count = 0;
    scene.has_water = false;
    scene.has_gltf_models = false;
    bool has_bounds = false;
    const auto append_bounds = [&scene, &has_bounds](const Bounds& bounds) {
        if (!has_bounds) {
            scene.bounds = bounds;
            has_bounds = true;
            return;
        }
        scene.bounds.min = glm::min(scene.bounds.min, bounds.min);
        scene.bounds.max = glm::max(scene.bounds.max, bounds.max);
    };

    for (const auto& model : scene.static_models) {
        if (!model) {
            continue;
        }
        for (const auto& primitive : model->primitives) {
            scene.vertex_count += primitive.vertices.valid() ? primitive.index_count : 0;
            scene.index_count += primitive.index_count;
        }
        scene.has_water = scene.has_water || render_model_contains_water(*model);
        scene.has_gltf_models = scene.has_gltf_models
            || model->source_kind == nw::render::ModelAssetSourceKind::gltf;
        append_bounds(model->bounds);
    }
    if (!has_bounds) {
        scene.bounds = {};
    }
}

struct SceneModelRemoval {
    std::vector<uint32_t> render_index_map;
    std::vector<nw::render::ModelInstanceHandle> handles;
    uint32_t removed_count = 0;
};

std::optional<SceneModelRemoval> remove_object_model_rows(
    PreviewScene& scene, std::span<const nw::ObjectHandle> objects)
{
    if (!valid_scene_model_columns(scene)) {
        return std::nullopt;
    }

    SceneModelRemoval removal;
    removal.render_index_map.assign(scene.static_models.size(), nw::render::kInvalidModelInstanceIndex);
    std::vector<uint8_t> remove_render(scene.static_models.size(), 0u);
    for (size_t i = 0; i < scene.static_models.size(); ++i) {
        remove_render[i] = static_cast<uint8_t>(!scene.is_area
            || contains_object(objects, scene.static_area_model_info[i].object));
        if (remove_render[i] != 0u) {
            removal.handles.push_back(scene.static_model_instance_handles[i]);
        }
    }
    if (removal.handles.empty()) {
        return std::nullopt;
    }

    for (const auto& binding : scene.model_attachments) {
        const bool remove_child = contains_instance_handle(removal.handles, binding.child_instance_handle);
        const bool remove_owner = contains_instance_handle(removal.handles, binding.owner_instance_handle);
        if (remove_child != remove_owner) {
            return std::nullopt;
        }
    }

    std::erase_if(scene.particles, [&removal](const SceneParticleSystem& particles) {
        return contains_instance_handle(removal.handles, particles.owner_instance_handle);
    });
    std::erase_if(scene.model_attachments, [&removal](const auto& binding) {
        return contains_instance_handle(removal.handles, binding.child_instance_handle);
    });

    size_t write = 0;
    for (size_t read = 0; read < scene.static_models.size(); ++read) {
        if (remove_render[read] != 0u) {
            destroy_scene_instance(scene, scene.static_model_instance_handles[read]);
            continue;
        }
        removal.render_index_map[read] = static_cast<uint32_t>(write);
        if (write != read) {
            scene.static_models[write] = std::move(scene.static_models[read]);
            scene.static_model_instance_handles[write] = scene.static_model_instance_handles[read];
            scene.static_area_model_info[write] = scene.static_area_model_info[read];
        }
        if (auto* instance = scene.model_instances.get(scene.static_model_instance_handles[write])) {
            instance->render_model_index = static_cast<uint32_t>(write);
        }
        ++write;
    }
    scene.static_models.resize(write);
    scene.static_model_instance_handles.resize(write);
    scene.static_area_model_info.resize(write);
    scene.static_model_attachment_binding_indices.assign(
        write, kInvalidSceneModelAttachmentBindingIndex);

    for (size_t i = 0; i < scene.model_attachments.size(); ++i) {
        const auto child = scene.model_attachments[i].child_instance_handle;
        const auto render = std::find(scene.static_model_instance_handles.begin(),
            scene.static_model_instance_handles.end(), child);
        if (render != scene.static_model_instance_handles.end()) {
            scene.static_model_attachment_binding_indices[static_cast<size_t>(
                std::distance(scene.static_model_instance_handles.begin(), render))]
                = static_cast<uint32_t>(i);
        }
    }

    for (auto& particles : scene.particles) {
        if (particles.owner_model_index < removal.render_index_map.size()) {
            particles.owner_model_index = removal.render_index_map[particles.owner_model_index];
        }
    }

    std::erase_if(scene.local_lights, [&remove_render](const SceneLocalLight& light) {
        if (light.source != SceneLocalLightSource::authored_model) {
            return false;
        }
        return light.model_index < remove_render.size() && remove_render[light.model_index] != 0u;
    });
    for (auto& light : scene.local_lights) {
        if (light.source != SceneLocalLightSource::authored_model) {
            continue;
        }
        if (light.model_index < removal.render_index_map.size()) {
            light.model_index = removal.render_index_map[light.model_index];
        }
    }

    removal.removed_count = static_cast<uint32_t>(removal.handles.size());
    rebuild_scene_model_summaries(scene);
    return removal;
}

} // namespace

AreaObjectPreviewAppendResult append_area_object_previews(
    PreviewScene& scene,
    PreviewRenderResources& resources,
    std::span<const nw::ObjectHandle> objects,
    float opacity,
    PreviewSceneLoadOptions options)
{
    AreaObjectPreviewAppendResult result;
    if (objects.empty()) {
        result.diagnostic = "Area object preview batch is empty";
        return result;
    }
    if (!scene.is_area || !scene.area_render_scene || scene.root_object.type != nw::ObjectType::area
        || !std::isfinite(opacity) || opacity <= 0.0f || opacity >= 1.0f
        || objects.size() > std::numeric_limits<uint32_t>::max()) {
        result.status = AreaObjectPreviewAppendStatus::invalid_input;
        result.diagnostic = "Area object preview input is invalid";
        return result;
    }

    std::vector<nw::ObjectHandle> ordered{objects.begin(), objects.end()};
    std::sort(ordered.begin(), ordered.end());
    if (std::adjacent_find(ordered.begin(), ordered.end()) != ordered.end()) {
        result.status = AreaObjectPreviewAppendStatus::invalid_input;
        result.diagnostic = "Area object preview batch contains duplicate handles";
        return result;
    }

    std::vector<std::unique_ptr<PreviewScene>> previews;
    previews.reserve(objects.size());
    for (size_t i = 0; i < objects.size(); ++i) {
        const auto object = objects[i];
        const auto* spatial = nw::kernel::objects().components().find_spatial(object);
        if ((object.type != nw::ObjectType::creature && object.type != nw::ObjectType::placeable)
            || !nw::kernel::objects().valid(object) || !spatial
            || spatial->area != scene.root_object.id) {
            result.status = AreaObjectPreviewAppendStatus::invalid_input;
            result.diagnostic = "Area object preview contains an invalid or wrong-area object";
            return result;
        }

        const std::string origin = fmt::format("placement preview {}", i);
        std::unique_ptr<PreviewScene> preview;
        if (object.type == nw::ObjectType::creature) {
            auto* creature = nw::kernel::objects().get<nw::Creature>(object);
            if (creature) {
                preview = load_area_creature_scene(resources, *creature, origin, options);
            }
        } else {
            auto* placeable = nw::kernel::objects().get<nw::Placeable>(object);
            if (placeable) {
                preview = load_area_placeable_scene(resources, *placeable, origin, options);
            }
        }
        if (!preview) {
            result.status = AreaObjectPreviewAppendStatus::failed;
            result.diagnostic = "Area object preview visual construction failed";
            return result;
        }
        if (object.type == nw::ObjectType::creature) {
            prime_scene_hold_animation(*preview);
        }
        make_area_object_preview_translucent(*preview, opacity);
        previews.push_back(std::move(preview));
    }

    const size_t render_model_start = scene.static_model_instance_handles.size();
    size_t appended_models = 0;
    for (size_t i = 0; i < previews.size(); ++i) {
        const auto object = objects[i];
        const auto kind = object.type == nw::ObjectType::creature
            ? AreaRenderRecordKind::creature
            : AreaRenderRecordKind::placeable;
        appended_models += append_render_models(scene,
            *previews[i],
            AreaRenderSourceInfo{.kind = kind, .object = object},
            false);
    }

    for (size_t i = render_model_start; i < scene.static_model_instance_handles.size(); ++i) {
        if (auto* instance = scene.model_instances.get(scene.static_model_instance_handles[i])) {
            instance->shadow = {};
        }
    }

    scene.active_object = objects.front();
    scene.area_render_scene->rebuild(scene);
    result.status = AreaObjectPreviewAppendStatus::success;
    result.object_count = static_cast<uint32_t>(objects.size());
    result.model_count = static_cast<uint32_t>(
        std::min<size_t>(appended_models, std::numeric_limits<uint32_t>::max()));
    return result;
}

ObjectVisualRefreshResult refresh_object_visuals(
    PreviewScene& scene,
    PreviewRenderResources& resources,
    std::span<const nw::ObjectHandle> objects,
    PreviewSceneLoadOptions options)
{
    ObjectVisualRefreshResult result;
    if (objects.empty()) {
        result.diagnostic = "Object visual refresh batch is empty";
        return result;
    }
    if (objects.size() > std::numeric_limits<uint32_t>::max()
        || (!scene.is_area && (objects.size() != 1 || objects.front() != scene.root_object))
        || (scene.is_area && (!scene.area_render_scene || scene.root_object.type != nw::ObjectType::area))) {
        result.status = ObjectVisualRefreshStatus::invalid_input;
        result.diagnostic = "Object visual refresh scene input is invalid";
        return result;
    }

    std::vector<nw::ObjectHandle> ordered{objects.begin(), objects.end()};
    std::sort(ordered.begin(), ordered.end());
    if (std::adjacent_find(ordered.begin(), ordered.end()) != ordered.end()) {
        result.status = ObjectVisualRefreshStatus::invalid_input;
        result.diagnostic = "Object visual refresh batch contains duplicate handles";
        return result;
    }

    std::vector<std::unique_ptr<PreviewScene>> replacements;
    replacements.reserve(objects.size());
    for (size_t i = 0; i < objects.size(); ++i) {
        const auto object = objects[i];
        auto* creature = nw::kernel::objects().get<nw::Creature>(object);
        const bool represented = !scene.is_area
            || std::any_of(scene.static_area_model_info.begin(), scene.static_area_model_info.end(),
                [object](const auto& info) { return info.object == object; });
        if (!creature || !represented) {
            result.status = ObjectVisualRefreshStatus::invalid_input;
            result.diagnostic = "Object visual refresh contains an invalid or unrepresented creature";
            return result;
        }

        auto replacement = load_area_creature_scene(
            resources, *creature, fmt::format("visual refresh {}", i), options);
        if (!replacement) {
            result.status = ObjectVisualRefreshStatus::failed;
            result.diagnostic = "Object visual refresh model construction failed";
            return result;
        }
        prime_scene_hold_animation(*replacement);
        replacements.push_back(std::move(replacement));
    }

    auto removal = remove_object_model_rows(scene, objects);
    if (!removal) {
        result.status = ObjectVisualRefreshStatus::failed;
        result.diagnostic = "Object visual refresh could not isolate existing scene rows";
        return result;
    }

    size_t added_models = 0;
    for (size_t i = 0; i < replacements.size(); ++i) {
        added_models += append_render_models(scene,
            *replacements[i],
            AreaRenderSourceInfo{
                .kind = AreaRenderRecordKind::creature,
                .object = objects[i],
            });
    }

    scene.active_object = objects.front();
    sync_model_instance_runtime_state(scene);
    refresh_scene_local_light_render_data(scene);
    if (scene.area_render_scene) {
        scene.area_render_scene->rebuild(scene);
    }
    if (!scene.load_report.source.empty() || !scene.load_report.kind.empty()) {
        const std::string report_source = scene.load_report.source;
        const std::string report_kind = scene.load_report.kind;
        scene.rebuild_load_report(report_source, report_kind);
    }

    result.status = ObjectVisualRefreshStatus::success;
    result.object_count = static_cast<uint32_t>(objects.size());
    result.removed_model_count = removal->removed_count;
    result.added_model_count = static_cast<uint32_t>(
        std::min<size_t>(added_models, std::numeric_limits<uint32_t>::max()));
    return result;
}

} // namespace nw::render::viewer
