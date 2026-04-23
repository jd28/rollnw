#include "mdl_particle_import.hpp"

#include "../util/string.hpp"

#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>

namespace nw::model {

namespace {

using namespace nw::render;

const Animation* selected_particle_animation(const Mdl& mdl, StringView animation_name)
{
    if (animation_name.empty()) { return nullptr; }
    return mdl.model.find_animation(animation_name);
}

std::optional<float> get_scalar_controller(const Node& node, uint32_t type)
{
    auto value = node.get_controller(type);
    if (value.key && !value.data.empty()) { return value.data[0]; }
    return std::nullopt;
}

std::optional<glm::vec3> get_vec3_controller(const Node& node, uint32_t type)
{
    auto value = node.get_controller(type);
    if (value.key && value.data.size() >= 3) {
        return glm::vec3{value.data[0], value.data[1], value.data[2]};
    }
    return std::nullopt;
}

float scalar_or(const Node& node, uint32_t type, float fallback = 0.0f)
{
    if (auto value = get_scalar_controller(node, type)) { return *value; }
    return fallback;
}

glm::vec3 vec3_or(const Node& node, uint32_t type, const glm::vec3& fallback = glm::vec3{0.0f})
{
    if (auto value = get_vec3_controller(node, type)) { return *value; }
    return fallback;
}

glm::quat quat_or(const Node& node, uint32_t type, const glm::quat& fallback = glm::quat{1.0f, 0.0f, 0.0f, 0.0f})
{
    auto value = node.get_controller(type);
    if (value.key && value.data.size() >= 4) {
        // Binary MDL stores orientation as packed quaternion (x, y, z, w) — same convention as
        // model_loader.cpp and nwn_animation.cpp. Text MDL "orientation 0 0 0 0" means undefined;
        // the zero-length guard maps that to identity (fallback).
        const glm::quat q{value.data[3], value.data[0], value.data[1], value.data[2]};
        if (glm::dot(q, q) < 1.0e-12f) { return fallback; }
        return glm::normalize(q);
    }
    return fallback;
}

glm::mat4 node_local_transform(const Node& node)
{
    glm::mat4 result = glm::translate(glm::mat4{1.0f}, vec3_or(node, ControllerType::Position, glm::vec3{0.0f}));
    result *= glm::mat4_cast(quat_or(node, ControllerType::Orientation));
    result = glm::scale(result, vec3_or(node, ControllerType::Scale, glm::vec3{1.0f}));
    return result;
}

glm::mat4 node_model_transform(const Node& node)
{
    glm::mat4 result = node_local_transform(node);
    for (auto* parent = node.parent; parent; parent = parent->parent) {
        result = node_local_transform(*parent) * result;
    }
    return result;
}

ParticleEmissionMode map_emission_mode(const EmitterNode& emitter, ParticleImportResult& result)
{
    if (string::icmp(emitter.update, "fountain")) return ParticleEmissionMode::continuous;
    if (string::icmp(emitter.update, "single")) return ParticleEmissionMode::single_shot;
    if (string::icmp(emitter.update, "explosion")) return ParticleEmissionMode::event_burst;
    if (string::icmp(emitter.update, "lightning")) return ParticleEmissionMode::beam_continuous;

    result.warnings.push_back({emitter.name.c_str(), "update", "unsupported update mode; defaulting to continuous"});
    return ParticleEmissionMode::continuous;
}

ParticleRenderMode map_render_mode(const EmitterNode& emitter, ParticleImportResult& result)
{
    if (string::icmp(emitter.render, "normal")) return ParticleRenderMode::billboard;
    if (string::icmp(emitter.render, "linked")) return ParticleRenderMode::linked_chain;
    if (string::icmp(emitter.render, "billboard_to_local_z")) return ParticleRenderMode::billboard_local_z;
    if (string::icmp(emitter.render, "billboard_to_world_z")) return ParticleRenderMode::billboard_world_z;
    if (string::icmp(emitter.render, "aligned_to_world_z")) return ParticleRenderMode::aligned_world_z;
    if (string::icmp(emitter.render, "aligned_to_particle_dir")) return ParticleRenderMode::velocity_aligned;
    if (string::icmp(emitter.render, "motion_blur")) return ParticleRenderMode::stretched;

    result.warnings.push_back({emitter.name.c_str(), "render", "unsupported render mode; defaulting to billboard"});
    return ParticleRenderMode::billboard;
}

ParticleBlendMode map_blend_mode(const EmitterNode& emitter, ParticleImportResult& result)
{
    if (string::icmp(emitter.blend, "normal")) return ParticleBlendMode::alpha;
    if (string::icmp(emitter.blend, "punch-through")) return ParticleBlendMode::cutout;
    if (string::icmp(emitter.blend, "lighten")) return ParticleBlendMode::additive;

    result.warnings.push_back({emitter.name.c_str(), "blend", "unsupported blend mode; defaulting to alpha"});
    return ParticleBlendMode::alpha;
}

ParticleSimulationSpace map_simulation_space(const EmitterNode& emitter)
{
    const bool inherit = (emitter.flags & EmitterFlag::Inherit) != 0;
    const bool inherit_local = (emitter.flags & EmitterFlag::InheritLocal) != 0;

    if (inherit_local) return ParticleSimulationSpace::spawn_attached;
    if (inherit) return ParticleSimulationSpace::emitter_attached;
    return ParticleSimulationSpace::world;
}

ParticleTargetingMode map_targeting_mode(const EmitterNode& emitter)
{
    if ((emitter.flags & EmitterFlag::P2P) == 0) { return ParticleTargetingMode::none; }
    if (string::icmp(emitter.update, "lightning")) return ParticleTargetingMode::beam_lightning;
    if (string::icmp(emitter.p2p_type, "gravity")) return ParticleTargetingMode::point_gravity;
    if (string::icmp(emitter.p2p_type, "bezier")) return ParticleTargetingMode::point_bezier;
    if ((emitter.flags & EmitterFlag::P2PSel) != 0) return ParticleTargetingMode::point_bezier;
    return ParticleTargetingMode::point_gravity;
}

uint32_t get_or_add_material(const ParticleMaterialDef& material, ParticleEffectDef& effect)
{
    for (size_t i = 0; i < effect.materials.size(); ++i) {
        const auto& existing = effect.materials[i];
        if (existing.texture == material.texture
            && existing.mesh == material.mesh
            && existing.blend == material.blend
            && existing.double_sided == material.double_sided
            && existing.sheet.columns == material.sheet.columns
            && existing.sheet.rows == material.sheet.rows
            && existing.sheet.frame_begin == material.sheet.frame_begin
            && existing.sheet.frame_end == material.sheet.frame_end
            && existing.sheet.frames_per_second == material.sheet.frames_per_second
            && existing.sheet.random_start == material.sheet.random_start) {
            return static_cast<uint32_t>(i);
        }
    }

    effect.materials.push_back(material);
    return static_cast<uint32_t>(effect.materials.size() - 1);
}

ParticleCurveF32 make_linear_curve(float start, float end)
{
    ParticleCurveF32 result;
    result.keys.push_back({0.0f, start});
    result.keys.push_back({1.0f, end});
    return result;
}

ParticleGradient make_linear_gradient(const glm::vec4& start, const glm::vec4& end)
{
    ParticleGradient result;
    result.keys.push_back({0.0f, start});
    result.keys.push_back({1.0f, end});
    return result;
}

float sanitize_percent(float value, float fallback)
{
    if (!std::isfinite(value)) { return fallback; }
    return std::clamp(value, 0.0f, 1.0f);
}

std::array<float, 3> over_life_percentages(const EmitterNode& emitter)
{
    float start = sanitize_percent(scalar_or(emitter, ControllerType::PercentStart, 0.0f), 0.0f);
    float mid = sanitize_percent(scalar_or(emitter, ControllerType::PercentMid, 0.5f), 0.5f);
    float end = sanitize_percent(scalar_or(emitter, ControllerType::PercentEnd, 1.0f), 1.0f);

    mid = std::max(mid, start);
    end = std::max(end, mid);
    return {start, mid, end};
}

ParticleCurveF32 make_three_stage_curve(float start, float mid, float end, const std::array<float, 3>& percent)
{
    ParticleCurveF32 result;
    result.keys.push_back({percent[0], start});
    result.keys.push_back({percent[1], mid});
    result.keys.push_back({percent[2], end});
    return result;
}

ParticleGradient make_three_stage_gradient(
    const glm::vec4& start, const glm::vec4& mid, const glm::vec4& end, const std::array<float, 3>& percent)
{
    ParticleGradient result;
    result.keys.push_back({percent[0], start});
    result.keys.push_back({percent[1], mid});
    result.keys.push_back({percent[2], end});
    return result;
}

bool is_explosion_emitter(const EmitterNode& emitter)
{
    return string::icmp(emitter.update, "explosion");
}

std::optional<glm::vec3> find_reference_target_position(const EmitterNode& emitter)
{
    for (const auto* child : emitter.children) {
        if (!child || child->type != NodeType::reference) { continue; }
        return glm::vec3(node_model_transform(*child)[3]);
    }

    return std::nullopt;
}

template <typename F>
bool visit_matching_animation_nodes(
    const Mdl& mdl, const EmitterNode& emitter, StringView animation_name, bool fallback_all_animations, F&& visitor)
{
    if (const auto* anim = selected_particle_animation(mdl, animation_name)) {
        for (const auto& node_ptr : anim->nodes) {
            if (!node_ptr || !string::icmp(node_ptr->name, emitter.name)) { continue; }
            if (visitor(*node_ptr)) { return true; }
        }
        return false;
    } else if (!animation_name.empty() || !fallback_all_animations) {
        return false;
    }

    for (const auto& anim : mdl.model.animations) {
        if (!anim) { continue; }
        for (const auto& node_ptr : anim->nodes) {
            if (!node_ptr || !string::icmp(node_ptr->name, emitter.name)) { continue; }
            if (visitor(*node_ptr)) { return true; }
        }
    }

    return false;
}

void maybe_import_birthrate_curve(
    const Mdl& mdl, const EmitterNode& emitter, StringView animation_name, bool fallback_all_animations, ParticleEmissionDef& out)
{
    visit_matching_animation_nodes(mdl, emitter, animation_name, fallback_all_animations, [&](const Node& anim_node) {
        auto value = anim_node.get_controller(ControllerType::BirthRate);
        if (!value.key || value.time.empty() || value.data.empty()) { return false; }

        out.rate_over_time.keys.clear();
        const size_t count = std::min(value.time.size(), value.data.size());
        for (size_t i = 0; i < count; ++i) {
            out.rate_over_time.keys.push_back({value.time[i], value.data[i]});
        }
        return true;
    });
}

float peak_scalar_curve(const ParticleCurveF32& curve, float fallback)
{
    float result = fallback;
    for (const auto& key : curve.keys) {
        result = std::max(result, key.value);
    }
    return result;
}

float nwn_size_y_or(float value, float fallback_x)
{
    return std::abs(value) <= 1.0e-6f ? fallback_x : value;
}

float nwn_size_y_controller_or(const Node& node, uint32_t type, float fallback_x, bool preserve_explicit_zero)
{
    if (auto value = get_scalar_controller(node, type)) {
        if (preserve_explicit_zero) { return *value; }
        return nwn_size_y_or(*value, fallback_x);
    }
    return fallback_x;
}

float estimate_effect_event_period(std::span<const ParticleImportEffectEvent> events)
{
    if (events.size() < 2) { return 0.0f; }

    float period = std::numeric_limits<float>::max();
    for (size_t i = 1; i < events.size(); ++i) {
        const float delta = events[i].time - events[i - 1].time;
        if (delta > 1.0e-6f) {
            period = std::min(period, delta);
        }
    }
    return period == std::numeric_limits<float>::max() ? 0.0f : period;
}

bool should_trigger_continuous_emitter_from_effect_events(
    const ParticleEmitterDef& emitter, const ParticleMaterialDef& material, float effect_event_period)
{
    if (effect_event_period <= 0.0f) { return false; }
    if (emitter.emission.mode != ParticleEmissionMode::continuous) { return false; }
    if (emitter.render.mode != ParticleRenderMode::velocity_aligned) { return false; }
    if (material.blend != ParticleBlendMode::additive) { return false; }
    if (emitter.simulation_space != ParticleSimulationSpace::emitter_attached) { return false; }
    if (emitter.initial.speed.max > 1.0e-6f) { return false; }
    if (emitter.initial.velocity_inheritance > 1.0e-6f) { return false; }
    if (emitter.initial.spread_radians < 6.0f) { return false; }
    return true;
}

bool should_lower_projectile_body_sprite(
    const ParticleEmitterDef& emitter, const ParticleMaterialDef& material, float effect_event_period)
{
    return should_trigger_continuous_emitter_from_effect_events(emitter, material, effect_event_period);
}

void maybe_import_scalar_spawn_curve(
    const Mdl& mdl, const EmitterNode& emitter, StringView animation_name, bool fallback_all_animations, uint32_t controller,
    ParticleCurveF32& out)
{
    visit_matching_animation_nodes(mdl, emitter, animation_name, fallback_all_animations, [&](const Node& anim_node) {
            auto value = anim_node.get_controller(controller);
            if (!value.key || value.time.empty() || value.data.empty()) { return false; }
            out.keys.clear();
            const size_t count = std::min(value.time.size(), value.data.size());
            for (size_t i = 0; i < count; ++i) {
                out.keys.push_back({value.time[i], value.data[i]});
            }
            return true;
        });
}

void collect_animation_effect_events(const Animation& animation, const Mdl& mdl, std::map<float, uint32_t>& grouped)
{
    for (const auto& event : animation.events) {
        if (string::icmp(event.name, "detonate")) {
            grouped[event.time] += 1u;
        }
    }

    for (const auto& node_ptr : animation.nodes) {
        if (!node_ptr || node_ptr->type != NodeType::emitter) { continue; }

        const Node* emitter = nullptr;
        for (const auto& model_node : mdl.model.nodes) {
            if (model_node && string::icmp(model_node->name, node_ptr->name)) {
                emitter = model_node.get();
                break;
            }
        }
        if (!emitter || emitter->type != NodeType::emitter) { continue; }
        if (!is_explosion_emitter(static_cast<const EmitterNode&>(*emitter))) { continue; }

        auto detonate = node_ptr->get_controller(ControllerType::Detonate);
        if (!detonate.key || detonate.data.empty()) { continue; }

        const size_t count = detonate.time.empty()
            ? static_cast<size_t>(1)
            : std::min(detonate.time.size(), detonate.data.size());
        for (size_t i = 0; i < count; ++i) {
            const float event_time = detonate.time.empty() ? 0.0f : detonate.time[i];
            const auto burst_count = std::max<uint32_t>(1u, static_cast<uint32_t>(std::lround(std::max(0.0f, detonate.data[i]))));
            auto& grouped_count = grouped[event_time];
            grouped_count = std::max(grouped_count, burst_count);
        }
    }
}

float maybe_import_effect_events(const Mdl& mdl, StringView animation_name, ParticleImportResult& result)
{
    std::map<float, uint32_t> grouped;
    float event_length = 0.0f;

    if (const auto* anim = selected_particle_animation(mdl, animation_name)) {
        collect_animation_effect_events(*anim, mdl, grouped);
        event_length = anim->length;
    } else if (!animation_name.empty()) {
        return 0.0f;
    } else {
        for (const auto& anim : mdl.model.animations) {
            if (!anim) { continue; }
            collect_animation_effect_events(*anim, mdl, grouped);
        }
    }

    result.effect_events.clear();
    result.effect_events.reserve(grouped.size());
    for (const auto& [time, burst_count] : grouped) {
        result.effect_events.push_back({time, burst_count});
    }
    return event_length;
}

void maybe_import_color_spawn_curve(
    const Mdl& mdl, const EmitterNode& emitter, StringView animation_name, bool fallback_all_animations, uint32_t controller,
    ParticleGradient& out)
{
    visit_matching_animation_nodes(mdl, emitter, animation_name, fallback_all_animations, [&](const Node& anim_node) {
            auto value = anim_node.get_controller(controller);
            if (!value.key || value.time.empty() || value.data.size() < 3) { return false; }
            out.keys.clear();
            const size_t count = std::min(value.time.size(), value.data.size() / 3);
            for (size_t i = 0; i < count; ++i) {
                out.keys.push_back({
                    value.time[i],
                    glm::vec4{value.data[i * 3 + 0], value.data[i * 3 + 1], value.data[i * 3 + 2], 1.0f},
                });
            }
            return true;
        });
}

} // namespace

ParticleImportResult import_particle_effect(const Mdl& mdl, StringView animation_name, bool fallback_all_animations)
{
    ParticleImportResult result;
    result.effect.name = mdl.model.name.c_str();

    if (!animation_name.empty() && !mdl.model.find_animation(animation_name)) {
        result.warnings.push_back({"", "animation", "requested animation was not found; falling back to all animations"});
    }

    const float effect_event_length = maybe_import_effect_events(mdl, animation_name, result);
    const float effect_event_period = estimate_effect_event_period(result.effect_events);

    for (const auto& node_ptr : mdl.model.nodes) {
        if (!node_ptr || node_ptr->type != NodeType::emitter) { continue; }

        const auto& emitter = static_cast<const EmitterNode&>(*node_ptr);
        ParticleMaterialDef material;
        material.name = emitter.name.c_str();
        material.texture = emitter.texture.c_str();
        material.mesh = emitter.chunkname.c_str();
        material.blend = map_blend_mode(emitter, result);
        material.double_sided = emitter.twosidedtex != 0;
        material.sheet.columns = std::max<uint16_t>(1, static_cast<uint16_t>(std::max(1u, emitter.xgrid)));
        material.sheet.rows = std::max<uint16_t>(1, static_cast<uint16_t>(std::max(1u, emitter.ygrid)));
        material.sheet.frame_begin = static_cast<uint16_t>(std::max(0.0f, scalar_or(emitter, ControllerType::FrameStart, 0.0f)));
        material.sheet.frame_end = static_cast<uint16_t>(std::max(0.0f, scalar_or(emitter, ControllerType::FrameEnd, static_cast<float>(material.sheet.columns * material.sheet.rows - 1))));
        material.sheet.frames_per_second = scalar_or(emitter, ControllerType::FPS, 0.0f);
        material.sheet.random_start = (emitter.flags & EmitterFlag::Random) != 0;

        ParticleEmitterDef out;
        out.name = emitter.name.c_str();
        out.emission.mode = map_emission_mode(emitter, result);
        out.emission.metric = emitter.spawntype == 1 ? ParticleSpawnMetric::per_distance : ParticleSpawnMetric::per_second;
        out.emission.rate = scalar_or(emitter, ControllerType::BirthRate, 0.0f);
        maybe_import_birthrate_curve(mdl, emitter, animation_name, fallback_all_animations, out.emission);
        out.emission.looping = emitter.loop != 0 || out.emission.mode != ParticleEmissionMode::single_shot;
        out.region.size = {
            scalar_or(emitter, ControllerType::XSize, 0.0f),
            scalar_or(emitter, ControllerType::YSize, 0.0f),
        };
        out.region.type = (out.region.size.x == 0.0f && out.region.size.y == 0.0f)
            ? ParticleSpawnRegionType::point
            : ParticleSpawnRegionType::rect;
        out.simulation_space = map_simulation_space(emitter);
        out.affected_by_wind = (emitter.flags & EmitterFlag::AffectedByWind) != 0;
        out.initial.lifetime = {scalar_or(emitter, ControllerType::LifeExp, 0.0f), scalar_or(emitter, ControllerType::LifeExp, 0.0f)};
        const float velocity = scalar_or(emitter, ControllerType::Velocity, 0.0f);
        const float randvel = scalar_or(emitter, ControllerType::RandVel, 0.0f);
        out.initial.speed = {velocity, velocity + randvel};
        out.initial.rotation_rate = {scalar_or(emitter, ControllerType::ParticleRot, 0.0f), scalar_or(emitter, ControllerType::ParticleRot, 0.0f)};
        out.initial.size_x = {scalar_or(emitter, ControllerType::SizeStart, 0.0f), scalar_or(emitter, ControllerType::SizeStart, 0.0f)};
        const float size_start_y = nwn_size_y_controller_or(
            emitter, ControllerType::SizeStart_Y, out.initial.size_x.min, false);
        out.initial.size_y = {size_start_y, size_start_y};
        out.initial.spread_radians = scalar_or(emitter, ControllerType::Spread, 0.0f);
        out.initial.mass = scalar_or(emitter, ControllerType::Mass, 0.0f);
        out.initial.velocity_inheritance = (emitter.flags & EmitterFlag::InheritVel) != 0 ? 1.0f : 0.0f;

        const glm::vec3 color_start = vec3_or(emitter, ControllerType::ColorStart, glm::vec3{1.0f});
        const glm::vec3 color_end = vec3_or(emitter, ControllerType::ColorEnd, color_start);
        const glm::vec3 color_mid = vec3_or(emitter, ControllerType::ColorMid, glm::mix(color_start, color_end, 0.5f));
        const float alpha_start = scalar_or(emitter, ControllerType::AlphaStart, 1.0f);
        const float alpha_end = scalar_or(emitter, ControllerType::AlphaEnd, alpha_start);
        const float alpha_mid = scalar_or(emitter, ControllerType::AlphaMid, 0.5f * (alpha_start + alpha_end));
        out.initial.color = {color_start, alpha_start};
        const float size_end_x = scalar_or(emitter, ControllerType::SizeEnd, out.initial.size_x.min);
        const float size_mid_x = scalar_or(emitter, ControllerType::SizeMid, 0.5f * (out.initial.size_x.min + size_end_x));
        const float size_end_y = nwn_size_y_controller_or(
            emitter, ControllerType::SizeEnd_Y, size_end_x, false);
        const float size_mid_y = nwn_size_y_controller_or(
            emitter, ControllerType::SizeMid_Y, size_mid_x, false);
        const auto over_life_percent = over_life_percentages(emitter);
        maybe_import_scalar_spawn_curve(mdl, emitter, animation_name, fallback_all_animations, ControllerType::AlphaStart, out.spawn_over_time.alpha_start);
        maybe_import_scalar_spawn_curve(mdl, emitter, animation_name, fallback_all_animations, ControllerType::AlphaEnd, out.spawn_over_time.alpha_end);
        maybe_import_scalar_spawn_curve(mdl, emitter, animation_name, fallback_all_animations, ControllerType::LifeExp, out.spawn_over_time.lifetime);
        maybe_import_scalar_spawn_curve(mdl, emitter, animation_name, fallback_all_animations, ControllerType::Velocity, out.spawn_over_time.speed);
        maybe_import_scalar_spawn_curve(mdl, emitter, animation_name, fallback_all_animations, ControllerType::RandVel, out.spawn_over_time.speed_random);
        maybe_import_scalar_spawn_curve(mdl, emitter, animation_name, fallback_all_animations, ControllerType::Mass, out.spawn_over_time.mass);
        maybe_import_scalar_spawn_curve(mdl, emitter, animation_name, fallback_all_animations, ControllerType::ParticleRot, out.spawn_over_time.rotation_rate);
        maybe_import_scalar_spawn_curve(mdl, emitter, animation_name, fallback_all_animations, ControllerType::Spread, out.spawn_over_time.spread);
        maybe_import_scalar_spawn_curve(mdl, emitter, animation_name, fallback_all_animations, ControllerType::FrameStart, out.spawn_over_time.sheet_frame_begin);
        maybe_import_scalar_spawn_curve(mdl, emitter, animation_name, fallback_all_animations, ControllerType::FrameEnd, out.spawn_over_time.sheet_frame_end);
        maybe_import_scalar_spawn_curve(mdl, emitter, animation_name, fallback_all_animations, ControllerType::FPS, out.spawn_over_time.sheet_fps);
        maybe_import_scalar_spawn_curve(mdl, emitter, animation_name, fallback_all_animations, ControllerType::random, out.spawn_over_time.sheet_random_start);
        maybe_import_scalar_spawn_curve(mdl, emitter, animation_name, fallback_all_animations, ControllerType::SizeStart, out.spawn_over_time.size_start_x);
        maybe_import_scalar_spawn_curve(mdl, emitter, animation_name, fallback_all_animations, ControllerType::SizeEnd, out.spawn_over_time.size_end_x);
        maybe_import_scalar_spawn_curve(mdl, emitter, animation_name, fallback_all_animations, ControllerType::SizeStart_Y, out.spawn_over_time.size_start_y);
        maybe_import_scalar_spawn_curve(mdl, emitter, animation_name, fallback_all_animations, ControllerType::SizeEnd_Y, out.spawn_over_time.size_end_y);
        maybe_import_color_spawn_curve(mdl, emitter, animation_name, fallback_all_animations, ControllerType::ColorStart, out.spawn_over_time.color_start);
        maybe_import_color_spawn_curve(mdl, emitter, animation_name, fallback_all_animations, ControllerType::ColorEnd, out.spawn_over_time.color_end);
        out.over_life.alpha = make_three_stage_curve(alpha_start, alpha_mid, alpha_end, over_life_percent);
        out.over_life.size_x = make_three_stage_curve(out.initial.size_x.min, size_mid_x, size_end_x, over_life_percent);
        out.over_life.size_y = make_three_stage_curve(out.initial.size_y.min, size_mid_y, size_end_y, over_life_percent);
        out.over_life.color = make_three_stage_gradient(
            glm::vec4{color_start, 1.0f}, glm::vec4{color_mid, 1.0f}, glm::vec4{color_end, 1.0f}, over_life_percent);

        out.targeting.mode = map_targeting_mode(emitter);
        out.targeting.transition_factor = scalar_or(emitter, ControllerType::CombineTime, 0.0f);
        out.targeting.gravity = scalar_or(emitter, ControllerType::Grav, 0.0f);
        out.targeting.drag = scalar_or(emitter, ControllerType::Drag, 0.0f);
        out.targeting.kill_radius = scalar_or(emitter, ControllerType::Threshold, 0.0f);
        out.targeting.source_tangent = scalar_or(emitter, ControllerType::P2P_Bezier2, 0.0f);
        out.targeting.target_tangent = scalar_or(emitter, ControllerType::P2P_Bezier3, 0.0f);
        out.beam.update_interval = scalar_or(emitter, ControllerType::LightningDelay, 0.0f);
        out.beam.jitter_radius = scalar_or(emitter, ControllerType::LightningRadius, 0.0f);
        out.beam.noise_scale = scalar_or(emitter, ControllerType::LightningScale, 0.0f);
        out.beam.subdivisions = static_cast<uint16_t>(std::max(0.0f, scalar_or(emitter, ControllerType::LightningSubDiv, 0.0f)));
        out.force_event.radius = emitter.blastradius;
        out.force_event.length = emitter.blastlength;
        out.collision.enabled = (emitter.flags & (EmitterFlag::Bounce | EmitterFlag::Splat)) != 0;
        out.collision.bounce = (emitter.flags & EmitterFlag::Bounce) != 0;
        out.collision.splat = (emitter.flags & EmitterFlag::Splat) != 0;
        out.collision.bounce_coefficient = scalar_or(emitter, ControllerType::Bounce_Co, 0.0f);

        out.render.mode = map_render_mode(emitter, result);
        if (out.emission.mode == ParticleEmissionMode::beam_continuous) {
            out.render.mode = ParticleRenderMode::beam;
        } else if (!material.mesh.empty()) {
            out.render.mode = ParticleRenderMode::mesh;
        }
        out.render.opacity_scale = emitter.opacity == 0.0f ? 1.0f : emitter.opacity;
        out.render.blur_length = scalar_or(emitter, ControllerType::BlurLength, 0.0f);
        out.render.deadspace_radians = emitter.deadspace;
        out.render.tint_to_scene_ambient = (emitter.flags & EmitterFlag::IsTinted) != 0;
        out.render.sort_order = static_cast<int32_t>(emitter.renderorder);
        out.render.material = get_or_add_material(material, result.effect);
        if (should_lower_projectile_body_sprite(out, material, effect_event_period)) {
            out.render.semantic = ParticleRenderSemantic::projectile_body_sprite;
        }
        if (should_trigger_continuous_emitter_from_effect_events(out, material, effect_event_period)) {
            out.emission.trigger_on_effect_events = true;
            out.emission.effect_event_period = effect_event_period;
        }

        if (out.emission.mode == ParticleEmissionMode::beam_continuous) {
            out.max_particles = 1;
        } else if (out.emission.mode == ParticleEmissionMode::event_burst) {
            const float peak_emission_rate = peak_scalar_curve(out.emission.rate_over_time, out.emission.rate);
            const float burst_lifetime = std::max(out.initial.lifetime.max, 1.0f);
            out.max_particles = std::max<uint32_t>(
                static_cast<uint32_t>(peak_emission_rate * burst_lifetime + 1.0f),
                std::max<uint32_t>(static_cast<uint32_t>(peak_emission_rate), 1u));
        } else {
            const float peak_emission_rate = peak_scalar_curve(out.emission.rate_over_time, out.emission.rate);
            const float lifetime_budget = out.emission.trigger_on_effect_events
                ? out.initial.lifetime.max
                : std::max(out.initial.lifetime.max, 1.0f);
            const float estimated = out.emission.metric == ParticleSpawnMetric::per_second
                ? peak_emission_rate * lifetime_budget
                : std::max(peak_emission_rate * 4.0f, 1.0f);
            out.max_particles = std::max<uint32_t>(static_cast<uint32_t>(estimated + 1.0f), 1);
        }

        if ((emitter.flags & EmitterFlag::InheritPart) != 0) {
            result.warnings.push_back({
                emitter.name.c_str(),
                "inherit_part",
                "inherit_part is present but currently deferred; no proven neutral lowering has been implemented yet",
            });
        }

        const uint32_t emitter_index = static_cast<uint32_t>(result.effect.emitters.size());
        result.effect.emitters.push_back(std::move(out));

        ParticleImportEmitterInit init;
        init.emitter = emitter_index;
        init.emitter_node_name = emitter.name.c_str();
        init.has_default_transform = true;
        init.default_transform = node_model_transform(emitter);
        init.has_default_position = true;
        init.default_position = glm::vec3(init.default_transform[3]);
        init.has_default_orientation = true;
        init.default_orientation = quat_or(emitter, ControllerType::Orientation);
        if (result.effect.emitters.back().targeting.mode != ParticleTargetingMode::none) {
            if (auto target_offset = find_reference_target_position(emitter)) {
                init.has_default_target_offset = true;
                init.default_target_offset = *target_offset;
                for (const auto* child : emitter.children) {
                    if (!child || child->type != NodeType::reference) { continue; }
                    init.target_node_name = child->name.c_str();
                    break;
                }
            } else {
                result.warnings.push_back({emitter.name.c_str(), "reference", "targeted emitter is missing a reference child"});
            }
        }
        result.emitter_inits.push_back(init);
    }

    if (effect_event_length > 0.0f && !result.effect_events.empty()) {
        uint32_t bursts_per_cycle = 0;
        for (const auto& event : result.effect_events) {
            bursts_per_cycle += event.burst_count;
        }

        const float burst_rate = static_cast<float>(bursts_per_cycle) / effect_event_length;
        for (auto& emitter : result.effect.emitters) {
            if (emitter.emission.mode != ParticleEmissionMode::event_burst) { continue; }

            const float peak_emission_rate = peak_scalar_curve(emitter.emission.rate_over_time, emitter.emission.rate);
            const float burst_lifetime = std::max(emitter.initial.lifetime.max, 1.0f);
            const uint32_t estimated = static_cast<uint32_t>(peak_emission_rate * burst_rate * burst_lifetime + 1.0f);
            emitter.max_particles = std::max(emitter.max_particles, std::max<uint32_t>(estimated, 1u));
        }
    }

    return result;
}

void apply_particle_import_inits(const ParticleImportResult& import, nw::render::ParticleSystemInstance& system)
{
    for (const auto& init : import.emitter_inits) {
        if (init.emitter >= system.emitters.size()) { continue; }

        auto& emitter = system.emitters[init.emitter];
        if (init.has_default_transform) {
            emitter.world_transform = init.default_transform;
            emitter.prev_world_pos = glm::vec3(init.default_transform[3]);
        } else if (init.has_default_position) {
            emitter.world_transform = glm::translate(glm::mat4{1.0f}, init.default_position);
            emitter.prev_world_pos = init.default_position;
            if (init.has_default_orientation) {
                emitter.world_transform *= glm::mat4_cast(init.default_orientation);
            }
        } else if (init.has_default_orientation) {
            emitter.world_transform = glm::mat4_cast(init.default_orientation);
        }
        if (init.has_default_target_offset) {
            emitter.target_point = init.default_target_offset;
        }
    }
}

void apply_particle_import_events(const ParticleImportResult& import, nw::render::ParticleSystemInstance& system,
    float previous_time, float current_time, float animation_length, bool include_start_time)
{
    if (import.effect_events.empty() || !system.effect || animation_length <= 0.0f) { return; }

    const auto trigger_range = [&](float start, float end, bool include_start) {
        for (const auto& event : import.effect_events) {
            if (((include_start && event.time >= start) || (!include_start && event.time > start)) && event.time <= end) {
                for (size_t emitter_id = 0; emitter_id < system.effect->emitters.size(); ++emitter_id) {
                    const auto& emission = system.effect->emitters[emitter_id].emission;
                    if (emission.mode == nw::render::ParticleEmissionMode::event_burst || emission.trigger_on_effect_events) {
                        nw::render::trigger_particle_emitter(system, static_cast<uint16_t>(emitter_id), event.burst_count);
                    }
                }
            }
        }
    };

    previous_time = std::clamp(previous_time, 0.0f, animation_length);
    current_time = std::clamp(current_time, 0.0f, animation_length);

    if (current_time < previous_time) {
        trigger_range(previous_time, animation_length, include_start_time);
        // On wrap, replay events at t == 0 for the new cycle start.
        trigger_range(0.0f, current_time, true);
    } else {
        trigger_range(previous_time, current_time, include_start_time);
    }
}

} // namespace nw::model
