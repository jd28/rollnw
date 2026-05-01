#include "preview_scene.hpp"

#include "preview_object.hpp"
#include "preview_plt.hpp"

#include <nw/model/Mdl.hpp>
#include <nw/render/particle_json.hpp>

#include <nw/formats/Tileset.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/kernel/TilesetRegistry.hpp>
#include <nw/kernel/TwoDACache.hpp>
#include <nw/log.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/profiles/nwn1/constants.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/serialization/Gff.hpp>
#include <nw/util/string.hpp>

#include <algorithm>

#include <fmt/format.h>

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>

namespace mudl {

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
    if (token == "robe") {
        return "torso_g";
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

static void add_equipped_item_model(PreviewScene& scene, Renderer& renderer, const nw::Item& item,
    nw::EquipIndex slot, const ModelInstance* placement_context, float local_scale = 1.0f)
{
    auto anchor = anchor_name_for_equipped_item(slot);
    if (!placement_context || anchor.empty()) {
        return;
    }

    auto* baseitem = nw::kernel::rules().baseitems.get(item.baseitem);
    if (!baseitem) {
        return;
    }

    auto item_plt = item.model_to_plt_colors();
    auto try_add = [&](std::string_view resref) {
        if (resref.empty() || !nw::kernel::resman().contains({nw::Resref{resref}, nw::ResourceType::mdl})) {
            return;
        }

        auto model = load_model_with_plt(renderer, resref);
        if (!model) {
            return;
        }
        if (nw::kernel::resman().contains({nw::Resref{resref}, nw::ResourceType::plt})) {
            apply_plt_to_model(renderer, *model, item_plt);
        }
        model->scene_animation_enabled = false;
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
            try_add(fmt::format("{}_{:03d}", baseitem->item_class.view(), item.model_parts[nw::ItemModelParts::model1]));
        }
        break;
    case nw::ItemModelType::composite:
        if (item.model_parts[nw::ItemModelParts::model1] > 0) {
            try_add(fmt::format("{}_b_{:03d}", baseitem->item_class.view(), item.model_parts[nw::ItemModelParts::model1]));
        }
        if (item.model_parts[nw::ItemModelParts::model2] > 0) {
            try_add(fmt::format("{}_m_{:03d}", baseitem->item_class.view(), item.model_parts[nw::ItemModelParts::model2]));
        }
        if (item.model_parts[nw::ItemModelParts::model3] > 0) {
            try_add(fmt::format("{}_t_{:03d}", baseitem->item_class.view(), item.model_parts[nw::ItemModelParts::model3]));
        }
        break;
    case nw::ItemModelType::armor:
        break;
    }
}

static std::unique_ptr<PreviewScene> load_dynamic_creature_scene(Renderer& renderer, const std::filesystem::path& path)
{
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

    if (nw::string::icmp(path.extension().string(), ".bic")) {
        nw::Player player;
        if (!load_player_from_file(path, player)) {
            return {};
        }
        player.update_appearance(player.appearance.id);
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
            base_rig = load_model_with_plt(renderer, *base_rig_resref, &plt_colors);
        }
        if (base_rig) {
            base_rig->render_enabled = false;
            scene->add(std::move(base_rig));
        } else {
            LOG_F(WARNING, "No dynamic creature base rig resolved for appearance {}", appearance_id.idx());
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

            if (robe_part > 0 && robe_hides_body_part(robe_part, part.token)) {
                continue;
            }

            if (chest_item && chest_item->model_type == nw::ItemModelType::armor) {
                if (auto armor_part = armor_item_part_for_token(part.token)) {
                    uint16_t armor_model_part = chest_item->model_parts[*armor_part];
                    nw::PltColors armor_part_colors = chest_item->part_to_plt_colors(*armor_part);

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
                auto model = load_model_with_plt(renderer, *part_resref, &part_colors);
                if (model && placement_context) {
                    if (!anchor.empty()) {
                        make_static_scene_attachment(*model);
                        model->anchor_uses_root_bind_offset = false;
                        model->set_transform_anchor(placement_context, anchor);
                    }
                }
                maybe_add_model(*scene, std::move(model));
            }
        }

        if (chest_item && chest_item->model_type == nw::ItemModelType::armor) {
            auto robe_part = chest_item->model_parts[nw::ItemModelParts::armor_robe];
            if (robe_part > 0) {
                auto robe_colors = chest_item->part_to_plt_colors(nw::ItemModelParts::armor_robe);
                if (auto robe_resref = resolve_creature_part_model(sex, race, phenotype, {"robe"}, robe_part)) {
                    auto robe = load_model_with_plt(renderer, *robe_resref, &robe_colors);
                    if (robe && placement_context) {
                        make_static_scene_attachment(*robe);
                        robe->anchor_uses_root_bind_offset = false;
                        robe->set_transform_anchor(placement_context, "torso_g");
                    }
                    maybe_add_model(*scene, std::move(robe));
                }
            }
        }
    } else {
        maybe_add_model(*scene, load_model_with_plt(renderer, app->model_name, &plt_colors));
    }

    auto add_attachment = [&](const char* table_name, uint32_t row) {
        if (row == 0) {
            return;
        }
        auto* tda = nw::kernel::twodas().get(table_name);
        std::string model_name;
        if (tda && tda->get_to(row, "MODEL", model_name) && model_name != "c_nulltail") {
            auto anchor = anchor_name_for_attachment(table_name);
            auto source_anchor = source_anchor_name_for_attachment(table_name);
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
            maybe_add_model(*scene, std::move(model));
        }
    };
    add_attachment("wingmodel", wings);
    add_attachment("tailmodel", tail);

    if (cloak_item && cloak_item->model_type == nw::ItemModelType::layered) {
        auto* cloakmodel = nw::kernel::twodas().get("cloakmodel");
        int cloak_model = 0;
        if (cloakmodel && cloakmodel->get_to(cloak_item->model_parts[nw::ItemModelParts::model1], "MODEL", cloak_model) && cloak_model > 0) {
            auto cloak_colors = cloak_item->part_to_plt_colors(nw::ItemModelParts::model1);
            if (auto cloak_resref = resolve_creature_part_model(sex, race, phenotype, {"cloak"}, static_cast<uint16_t>(cloak_model))) {
                LOG_F(INFO, "dynamic creature cloak -> {}", *cloak_resref);
                auto cloak = load_model_with_plt(renderer, *cloak_resref, &cloak_colors);
                if (cloak) {
                    make_static_scene_attachment(*cloak);
                }
                maybe_add_model(*scene, std::move(cloak));
            }
        }
    }

    auto* placement_context = scene->models.empty() ? nullptr : scene->models.front().get();
    if (righthand_item) {
        add_equipped_item_model(*scene, renderer, *righthand_item, nw::EquipIndex::righthand, placement_context);
    }
    if (lefthand_item) {
        add_equipped_item_model(*scene, renderer, *lefthand_item, nw::EquipIndex::lefthand, placement_context);
    }
    if (head_item) {
        add_equipped_item_model(*scene, renderer, *head_item, nw::EquipIndex::head, placement_context, helmet_scale);
    }

    if (scene->models.empty()) {
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
    return scene;
}

static std::unique_ptr<PreviewScene> load_item_scene(Renderer& renderer, const std::filesystem::path& path)
{
    nw::Item item;
    if (!load_item_from_file(path, item)) {
        return {};
    }

    auto* baseitem = nw::kernel::rules().baseitems.get(item.baseitem);
    if (!baseitem) {
        return {};
    }

    auto scene = std::make_unique<PreviewScene>();
    auto item_plt = item.model_to_plt_colors();
    auto try_add = [&](std::string_view resref) {
        if (!resref.empty() && nw::kernel::resman().contains({nw::Resref{resref}, nw::ResourceType::mdl})) {
            auto model = load_model_with_plt(renderer, resref);
            if (model && nw::kernel::resman().contains({nw::Resref{resref}, nw::ResourceType::plt})) {
                apply_plt_to_model(renderer, *model, item_plt);
            }
            maybe_add_model(*scene, std::move(model));
        }
    };

    switch (item.model_type) {
    case nw::ItemModelType::simple:
    case nw::ItemModelType::layered:
        if (item.model_parts[nw::ItemModelParts::model1] > 0) {
            try_add(fmt::format("{}_{:03d}", baseitem->item_class.view(), item.model_parts[nw::ItemModelParts::model1]));
        }
        break;
    case nw::ItemModelType::composite:
        if (item.model_parts[nw::ItemModelParts::model1] > 0) {
            try_add(fmt::format("{}_b_{:03d}", baseitem->item_class.view(), item.model_parts[nw::ItemModelParts::model1]));
        }
        if (item.model_parts[nw::ItemModelParts::model2] > 0) {
            try_add(fmt::format("{}_m_{:03d}", baseitem->item_class.view(), item.model_parts[nw::ItemModelParts::model2]));
        }
        if (item.model_parts[nw::ItemModelParts::model3] > 0) {
            try_add(fmt::format("{}_t_{:03d}", baseitem->item_class.view(), item.model_parts[nw::ItemModelParts::model3]));
        }
        break;
    case nw::ItemModelType::armor:
        LOG_F(WARNING, "Standalone armor item preview is not supported yet; preview armor through a creature/player file instead");
        break;
    }

    if (scene->models.empty() && baseitem->default_model.valid()) {
        try_add(baseitem->default_model.resref.view());
    }

    if (scene->models.empty()) {
        return {};
    }
    return scene;
}

std::unique_ptr<PreviewScene> load_preview_scene(Renderer& renderer, std::string_view source)
{
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
            return {};
        }
        scene->add(std::move(model));
        return scene;
    }

    if (ext == ".json" && std::filesystem::exists(path)) {
        std::ifstream in{path};
        if (!in) {
            LOG_F(ERROR, "Failed to open particle JSON '{}'", path.string());
            return {};
        }

        nw::render::ParticleEffectDef effect;
        std::string parse_error;
        if (!nw::render::try_load_particle_effect_json(in, effect, &parse_error)) {
            LOG_F(ERROR, "Failed to load particle JSON '{}': {}", path.string(), parse_error);
            return {};
        }

        auto scene = std::make_unique<PreviewScene>();
        scene->add_particle_effect(std::move(effect));
        return scene;
    }

    if (is_object_preview_path(source)) {
        if (ext == ".bic" || ext == ".utc") {
            return load_dynamic_creature_scene(renderer, path);
        }
        if (ext == ".uti") {
            return load_item_scene(renderer, path);
        }
    }

    auto scene = std::make_unique<PreviewScene>();
    maybe_add_model(*scene, load_model_with_plt(renderer, source));
    if (scene->models.empty()) {
        return {};
    }
    scene->rebuild_particles();
    return scene;
}

std::unique_ptr<PreviewScene> load_preview_scene(Renderer& renderer, std::span<const std::string> sources)
{
    auto scene = std::make_unique<PreviewScene>();
    for (const auto& source : sources) {
        maybe_add_model(*scene, load_model_with_plt(renderer, source));
    }
    if (scene->models.empty()) {
        return {};
    }
    scene->rebuild_particles();
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
    return scene;
}

} // namespace mudl
