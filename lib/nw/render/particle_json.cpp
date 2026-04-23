#include "particle_json.hpp"

#include <nlohmann/json.hpp>
#include <string_view>

namespace nw::render {

namespace {

glm::vec2 vec2_from_json(const nlohmann::json& j, const glm::vec2& fallback = glm::vec2{0.0f})
{
    if (!j.is_array() || j.size() < 2) { return fallback; }
    return glm::vec2{j[0].get<float>(), j[1].get<float>()};
}

glm::vec4 vec4_from_json(const nlohmann::json& j, const glm::vec4& fallback = glm::vec4{0.0f})
{
    if (!j.is_array() || j.size() < 4) { return fallback; }
    return glm::vec4{j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()};
}

template <typename Enum>
Enum enum_from_string(std::string_view value, std::initializer_list<std::pair<std::string_view, Enum>> map, Enum fallback)
{
    for (const auto& entry : map) {
        if (entry.first == value) { return entry.second; }
    }
    return fallback;
}

template <typename Enum>
std::string_view enum_to_string(Enum value, std::initializer_list<std::pair<Enum, std::string_view>> map, std::string_view fallback)
{
    for (const auto& entry : map) {
        if (entry.first == value) { return entry.second; }
    }
    return fallback;
}

void assign_parse_error(std::string* error, std::string_view message)
{
    if (error) { *error = std::string{message}; }
}

} // namespace

void to_json(nlohmann::json& j, const ParticleRangeF32& value)
{
    j = nlohmann::json{{"min", value.min}, {"max", value.max}};
}

void from_json(const nlohmann::json& j, ParticleRangeF32& value)
{
    j.at("min").get_to(value.min);
    j.at("max").get_to(value.max);
}

void to_json(nlohmann::json& j, const ParticleCurveKeyF32& value)
{
    j = nlohmann::json::object();
    j["time"] = value.time;
    j["value"] = value.value;
}

void from_json(const nlohmann::json& j, ParticleCurveKeyF32& value)
{
    j.at("time").get_to(value.time);
    j.at("value").get_to(value.value);
}

void to_json(nlohmann::json& j, const ParticleCurveF32& value)
{
    j = nlohmann::json{{"keys", value.keys}};
}

void from_json(const nlohmann::json& j, ParticleCurveF32& value)
{
    if (j.is_array()) {
        value.keys = j.get<std::vector<ParticleCurveKeyF32>>();
        return;
    }
    if (j.contains("keys")) { j.at("keys").get_to(value.keys); }
}

void to_json(nlohmann::json& j, const ParticleGradientKey& value)
{
    j = nlohmann::json::object();
    j["time"] = value.time;
    j["value"] = nlohmann::json::array({value.value.x, value.value.y, value.value.z, value.value.w});
}

void from_json(const nlohmann::json& j, ParticleGradientKey& value)
{
    j.at("time").get_to(value.time);
    value.value = vec4_from_json(j.at("value"), value.value);
}

void to_json(nlohmann::json& j, const ParticleGradient& value)
{
    j = nlohmann::json{{"keys", value.keys}};
}

void from_json(const nlohmann::json& j, ParticleGradient& value)
{
    if (j.is_array()) {
        value.keys = j.get<std::vector<ParticleGradientKey>>();
        return;
    }
    if (j.contains("keys")) { j.at("keys").get_to(value.keys); }
}

void to_json(nlohmann::json& j, const ParticleSpawnRegion& value)
{
    j = nlohmann::json{
        {"type", enum_to_string(value.type, {
            {ParticleSpawnRegionType::point, "point"},
            {ParticleSpawnRegionType::rect, "rect"},
        }, "point")},
        {"size", nlohmann::json::array({value.size.x, value.size.y})},
    };
}

void from_json(const nlohmann::json& j, ParticleSpawnRegion& value)
{
    value.type = enum_from_string(j.value("type", "point"), {
        {"point", ParticleSpawnRegionType::point},
        {"rect", ParticleSpawnRegionType::rect},
    }, ParticleSpawnRegionType::point);
    if (j.contains("size")) { value.size = vec2_from_json(j.at("size"), value.size); }
}

void to_json(nlohmann::json& j, const ParticleSpriteSheet& value)
{
    j = nlohmann::json{
        {"columns", value.columns},
        {"rows", value.rows},
        {"frame_begin", value.frame_begin},
        {"frame_end", value.frame_end},
        {"frames_per_second", value.frames_per_second},
        {"random_start", value.random_start},
    };
}

void from_json(const nlohmann::json& j, ParticleSpriteSheet& value)
{
    value.columns = j.value("columns", value.columns);
    value.rows = j.value("rows", value.rows);
    value.frame_begin = j.value("frame_begin", value.frame_begin);
    value.frame_end = j.value("frame_end", value.frame_end);
    value.frames_per_second = j.value("frames_per_second", value.frames_per_second);
    value.random_start = j.value("random_start", value.random_start);
}

void to_json(nlohmann::json& j, const ParticleMaterialDef& value)
{
    j = nlohmann::json{
        {"name", value.name},
        {"texture", value.texture},
        {"mesh", value.mesh},
        {"blend", enum_to_string(value.blend, {
            {ParticleBlendMode::alpha, "alpha"},
            {ParticleBlendMode::cutout, "cutout"},
            {ParticleBlendMode::additive, "additive"},
        }, "alpha")},
        {"double_sided", value.double_sided},
        {"sheet", value.sheet},
    };
}

void from_json(const nlohmann::json& j, ParticleMaterialDef& value)
{
    value.name = j.value("name", value.name);
    value.texture = j.value("texture", value.texture);
    value.mesh = j.value("mesh", value.mesh);
    value.blend = enum_from_string(j.value("blend", "alpha"), {
        {"alpha", ParticleBlendMode::alpha},
        {"cutout", ParticleBlendMode::cutout},
        {"additive", ParticleBlendMode::additive},
    }, ParticleBlendMode::alpha);
    value.double_sided = j.value("double_sided", value.double_sided);
    if (j.contains("sheet")) { j.at("sheet").get_to(value.sheet); }
}

void to_json(nlohmann::json& j, const ParticleInitialStateDef& value)
{
    j = nlohmann::json{
        {"lifetime", value.lifetime},
        {"speed", value.speed},
        {"rotation", value.rotation},
        {"rotation_rate", value.rotation_rate},
        {"size_x", value.size_x},
        {"size_y", value.size_y},
        {"spread_radians", value.spread_radians},
        {"mass", value.mass},
        {"velocity_inheritance", value.velocity_inheritance},
        {"color", nlohmann::json::array({value.color.x, value.color.y, value.color.z, value.color.w})},
    };
}

void from_json(const nlohmann::json& j, ParticleInitialStateDef& value)
{
    if (j.contains("lifetime")) { j.at("lifetime").get_to(value.lifetime); }
    if (j.contains("speed")) { j.at("speed").get_to(value.speed); }
    if (j.contains("rotation")) { j.at("rotation").get_to(value.rotation); }
    if (j.contains("rotation_rate")) { j.at("rotation_rate").get_to(value.rotation_rate); }
    if (j.contains("size_x")) { j.at("size_x").get_to(value.size_x); }
    if (j.contains("size_y")) { j.at("size_y").get_to(value.size_y); }
    value.spread_radians = j.value("spread_radians", value.spread_radians);
    value.mass = j.value("mass", value.mass);
    value.velocity_inheritance = j.value("velocity_inheritance", value.velocity_inheritance);
    if (j.contains("color")) { value.color = vec4_from_json(j.at("color"), value.color); }
}

void to_json(nlohmann::json& j, const ParticleOverLifeDef& value)
{
    j = nlohmann::json{
        {"alpha", value.alpha},
        {"size_x", value.size_x},
        {"size_y", value.size_y},
        {"rotation", value.rotation},
        {"color", value.color},
    };
}

void from_json(const nlohmann::json& j, ParticleOverLifeDef& value)
{
    if (j.contains("alpha")) { j.at("alpha").get_to(value.alpha); }
    if (j.contains("size_x")) { j.at("size_x").get_to(value.size_x); }
    if (j.contains("size_y")) { j.at("size_y").get_to(value.size_y); }
    if (j.contains("rotation")) { j.at("rotation").get_to(value.rotation); }
    if (j.contains("color")) { j.at("color").get_to(value.color); }
}

void to_json(nlohmann::json& j, const ParticleTargetingDef& value)
{
    j = nlohmann::json{
        {"mode", enum_to_string(value.mode, {
            {ParticleTargetingMode::none, "none"},
            {ParticleTargetingMode::point_gravity, "point_gravity"},
            {ParticleTargetingMode::point_bezier, "point_bezier"},
            {ParticleTargetingMode::beam_lightning, "beam_lightning"},
        }, "none")},
        {"transition_factor", value.transition_factor},
        {"gravity", value.gravity},
        {"drag", value.drag},
        {"kill_radius", value.kill_radius},
        {"source_tangent", value.source_tangent},
        {"target_tangent", value.target_tangent},
    };
}

void from_json(const nlohmann::json& j, ParticleTargetingDef& value)
{
    value.mode = enum_from_string(j.value("mode", "none"), {
        {"none", ParticleTargetingMode::none},
        {"point_gravity", ParticleTargetingMode::point_gravity},
        {"point_bezier", ParticleTargetingMode::point_bezier},
        {"beam_lightning", ParticleTargetingMode::beam_lightning},
    }, ParticleTargetingMode::none);
    value.transition_factor = j.value("transition_factor", value.transition_factor);
    value.gravity = j.value("gravity", value.gravity);
    value.drag = j.value("drag", value.drag);
    value.kill_radius = j.value("kill_radius", value.kill_radius);
    value.source_tangent = j.value("source_tangent", value.source_tangent);
    value.target_tangent = j.value("target_tangent", value.target_tangent);
}

void to_json(nlohmann::json& j, const ParticleBeamDef& value)
{
    j = nlohmann::json{
        {"update_interval", value.update_interval},
        {"jitter_radius", value.jitter_radius},
        {"noise_scale", value.noise_scale},
        {"subdivisions", value.subdivisions},
    };
}

void from_json(const nlohmann::json& j, ParticleBeamDef& value)
{
    value.update_interval = j.value("update_interval", value.update_interval);
    value.jitter_radius = j.value("jitter_radius", value.jitter_radius);
    value.noise_scale = j.value("noise_scale", value.noise_scale);
    value.subdivisions = j.value("subdivisions", value.subdivisions);
}

void to_json(nlohmann::json& j, const ParticleForceEventDef& value)
{
    j = nlohmann::json{
        {"radius", value.radius},
        {"length", value.length},
    };
}

void from_json(const nlohmann::json& j, ParticleForceEventDef& value)
{
    value.radius = j.value("radius", value.radius);
    value.length = j.value("length", value.length);
}

void to_json(nlohmann::json& j, const ParticleCollisionDef& value)
{
    j = nlohmann::json{
        {"enabled", value.enabled},
        {"bounce", value.bounce},
        {"splat", value.splat},
        {"bounce_coefficient", value.bounce_coefficient},
    };
}

void from_json(const nlohmann::json& j, ParticleCollisionDef& value)
{
    value.enabled = j.value("enabled", value.enabled);
    value.bounce = j.value("bounce", value.bounce);
    value.splat = j.value("splat", value.splat);
    value.bounce_coefficient = j.value("bounce_coefficient", value.bounce_coefficient);
}

void to_json(nlohmann::json& j, const ParticleSpawnOverTimeDef& value)
{
    j = nlohmann::json{
        {"alpha_start", value.alpha_start},
        {"alpha_end", value.alpha_end},
        {"lifetime", value.lifetime},
        {"speed", value.speed},
        {"speed_random", value.speed_random},
        {"mass", value.mass},
        {"rotation_rate", value.rotation_rate},
        {"spread", value.spread},
        {"sheet_frame_begin", value.sheet_frame_begin},
        {"sheet_frame_end", value.sheet_frame_end},
        {"sheet_fps", value.sheet_fps},
        {"sheet_random_start", value.sheet_random_start},
        {"size_start_x", value.size_start_x},
        {"size_end_x", value.size_end_x},
        {"size_start_y", value.size_start_y},
        {"size_end_y", value.size_end_y},
        {"color_start", value.color_start},
        {"color_end", value.color_end},
    };
}

void from_json(const nlohmann::json& j, ParticleSpawnOverTimeDef& value)
{
    if (j.contains("alpha_start")) { j.at("alpha_start").get_to(value.alpha_start); }
    if (j.contains("alpha_end")) { j.at("alpha_end").get_to(value.alpha_end); }
    if (j.contains("lifetime")) { j.at("lifetime").get_to(value.lifetime); }
    if (j.contains("speed")) { j.at("speed").get_to(value.speed); }
    if (j.contains("speed_random")) { j.at("speed_random").get_to(value.speed_random); }
    if (j.contains("mass")) { j.at("mass").get_to(value.mass); }
    if (j.contains("rotation_rate")) { j.at("rotation_rate").get_to(value.rotation_rate); }
    if (j.contains("spread")) { j.at("spread").get_to(value.spread); }
    if (j.contains("sheet_frame_begin")) { j.at("sheet_frame_begin").get_to(value.sheet_frame_begin); }
    if (j.contains("sheet_frame_end")) { j.at("sheet_frame_end").get_to(value.sheet_frame_end); }
    if (j.contains("sheet_fps")) { j.at("sheet_fps").get_to(value.sheet_fps); }
    if (j.contains("sheet_random_start")) { j.at("sheet_random_start").get_to(value.sheet_random_start); }
    if (j.contains("size_start_x")) { j.at("size_start_x").get_to(value.size_start_x); }
    if (j.contains("size_end_x")) { j.at("size_end_x").get_to(value.size_end_x); }
    if (j.contains("size_start_y")) { j.at("size_start_y").get_to(value.size_start_y); }
    if (j.contains("size_end_y")) { j.at("size_end_y").get_to(value.size_end_y); }
    if (j.contains("color_start")) { j.at("color_start").get_to(value.color_start); }
    if (j.contains("color_end")) { j.at("color_end").get_to(value.color_end); }
}

void to_json(nlohmann::json& j, const ParticleRenderDef& value)
{
    j = nlohmann::json{
        {"mode", enum_to_string(value.mode, {
            {ParticleRenderMode::billboard, "billboard"},
            {ParticleRenderMode::billboard_local_z, "billboard_local_z"},
            {ParticleRenderMode::billboard_world_z, "billboard_world_z"},
            {ParticleRenderMode::aligned_world_z, "aligned_world_z"},
            {ParticleRenderMode::velocity_aligned, "velocity_aligned"},
            {ParticleRenderMode::stretched, "stretched"},
            {ParticleRenderMode::linked_chain, "linked_chain"},
            {ParticleRenderMode::beam, "beam"},
            {ParticleRenderMode::mesh, "mesh"},
        }, "billboard")},
        {"semantic", enum_to_string(value.semantic, {
            {ParticleRenderSemantic::none, "none"},
            {ParticleRenderSemantic::projectile_body_sprite, "projectile_body_sprite"},
        }, "none")},
        {"material", value.material},
        {"opacity_scale", value.opacity_scale},
        {"blur_length", value.blur_length},
        {"deadspace_radians", value.deadspace_radians},
        {"tint_to_scene_ambient", value.tint_to_scene_ambient},
        {"sort_order", value.sort_order},
    };
}

void from_json(const nlohmann::json& j, ParticleRenderDef& value)
{
    value.mode = enum_from_string(j.value("mode", "billboard"), {
        {"billboard", ParticleRenderMode::billboard},
        {"billboard_local_z", ParticleRenderMode::billboard_local_z},
        {"billboard_world_z", ParticleRenderMode::billboard_world_z},
        {"aligned_world_z", ParticleRenderMode::aligned_world_z},
        {"velocity_aligned", ParticleRenderMode::velocity_aligned},
        {"stretched", ParticleRenderMode::stretched},
        {"linked_chain", ParticleRenderMode::linked_chain},
        {"beam", ParticleRenderMode::beam},
        {"mesh", ParticleRenderMode::mesh},
    }, ParticleRenderMode::billboard);
    value.semantic = enum_from_string(j.value("semantic", "none"), {
        {"none", ParticleRenderSemantic::none},
        {"projectile_body_sprite", ParticleRenderSemantic::projectile_body_sprite},
    }, ParticleRenderSemantic::none);
    value.material = j.value("material", value.material);
    value.opacity_scale = j.value("opacity_scale", value.opacity_scale);
    value.blur_length = j.value("blur_length", value.blur_length);
    value.deadspace_radians = j.value("deadspace_radians", value.deadspace_radians);
    value.tint_to_scene_ambient = j.value("tint_to_scene_ambient", value.tint_to_scene_ambient);
    value.sort_order = j.value("sort_order", value.sort_order);
}

void to_json(nlohmann::json& j, const ParticleEmissionDef& value)
{
    j = nlohmann::json{
        {"mode", enum_to_string(value.mode, {
            {ParticleEmissionMode::continuous, "continuous"},
            {ParticleEmissionMode::single_shot, "single_shot"},
            {ParticleEmissionMode::event_burst, "event_burst"},
            {ParticleEmissionMode::beam_continuous, "beam_continuous"},
        }, "continuous")},
        {"metric", enum_to_string(value.metric, {
            {ParticleSpawnMetric::per_second, "per_second"},
            {ParticleSpawnMetric::per_distance, "per_distance"},
        }, "per_second")},
        {"rate", value.rate},
        {"rate_over_time", value.rate_over_time},
        {"looping", value.looping},
        {"trigger_on_effect_events", value.trigger_on_effect_events},
        {"effect_event_period", value.effect_event_period},
    };
}

void from_json(const nlohmann::json& j, ParticleEmissionDef& value)
{
    value.mode = enum_from_string(j.value("mode", "continuous"), {
        {"continuous", ParticleEmissionMode::continuous},
        {"single_shot", ParticleEmissionMode::single_shot},
        {"event_burst", ParticleEmissionMode::event_burst},
        {"beam_continuous", ParticleEmissionMode::beam_continuous},
    }, ParticleEmissionMode::continuous);
    value.metric = enum_from_string(j.value("metric", "per_second"), {
        {"per_second", ParticleSpawnMetric::per_second},
        {"per_distance", ParticleSpawnMetric::per_distance},
    }, ParticleSpawnMetric::per_second);
    value.rate = j.value("rate", value.rate);
    if (j.contains("rate_over_time")) { j.at("rate_over_time").get_to(value.rate_over_time); }
    value.looping = j.value("looping", value.looping);
    value.trigger_on_effect_events = j.value("trigger_on_effect_events", value.trigger_on_effect_events);
    value.effect_event_period = j.value("effect_event_period", value.effect_event_period);
}

void to_json(nlohmann::json& j, const ParticleEmitterDef& value)
{
    j = nlohmann::json{
        {"name", value.name},
        {"emission", value.emission},
        {"region", value.region},
        {"simulation_space", enum_to_string(value.simulation_space, {
            {ParticleSimulationSpace::world, "world"},
            {ParticleSimulationSpace::local, "local"},
            {ParticleSimulationSpace::emitter_attached, "emitter_attached"},
            {ParticleSimulationSpace::spawn_attached, "spawn_attached"},
        }, "world")},
        {"affected_by_wind", value.affected_by_wind},
        {"initial", value.initial},
        {"spawn_over_time", value.spawn_over_time},
        {"over_life", value.over_life},
        {"targeting", value.targeting},
        {"beam", value.beam},
        {"force_event", value.force_event},
        {"collision", value.collision},
        {"render", value.render},
        {"max_particles", value.max_particles},
    };
}

void from_json(const nlohmann::json& j, ParticleEmitterDef& value)
{
    value.name = j.value("name", value.name);
    if (j.contains("emission")) { j.at("emission").get_to(value.emission); }
    if (j.contains("region")) { j.at("region").get_to(value.region); }
    value.simulation_space = enum_from_string(j.value("simulation_space", "world"), {
        {"world", ParticleSimulationSpace::world},
        {"local", ParticleSimulationSpace::local},
        {"emitter_attached", ParticleSimulationSpace::emitter_attached},
        {"spawn_attached", ParticleSimulationSpace::spawn_attached},
    }, ParticleSimulationSpace::world);
    value.affected_by_wind = j.value("affected_by_wind", value.affected_by_wind);
    if (j.contains("initial")) { j.at("initial").get_to(value.initial); }
    if (j.contains("spawn_over_time")) { j.at("spawn_over_time").get_to(value.spawn_over_time); }
    if (j.contains("over_life")) { j.at("over_life").get_to(value.over_life); }
    if (j.contains("targeting")) { j.at("targeting").get_to(value.targeting); }
    if (j.contains("beam")) { j.at("beam").get_to(value.beam); }
    if (j.contains("force_event")) { j.at("force_event").get_to(value.force_event); }
    if (j.contains("collision")) { j.at("collision").get_to(value.collision); }
    if (j.contains("render")) { j.at("render").get_to(value.render); }
    value.max_particles = j.value("max_particles", value.max_particles);
}

void to_json(nlohmann::json& j, const ParticleEffectDef& value)
{
    j = nlohmann::json{
        {"$type", "ParticleEffect"},
        {"name", value.name},
        {"materials", value.materials},
        {"emitters", value.emitters},
    };
}

void from_json(const nlohmann::json& j, ParticleEffectDef& value)
{
    value.name = j.value("name", value.name);
    if (j.contains("materials")) { j.at("materials").get_to(value.materials); }
    if (j.contains("emitters")) { j.at("emitters").get_to(value.emitters); }
}

bool try_parse_particle_effect_json(const nlohmann::json& j, ParticleEffectDef& value, std::string* error)
{
    try {
        if (!j.is_object()) {
            assign_parse_error(error, "particle effect JSON root must be an object");
            return false;
        }

        if (!j.contains("$type")) {
            assign_parse_error(error, "particle effect JSON missing required \"$type\": \"ParticleEffect\"");
            return false;
        }

        const auto type = j.at("$type").get<std::string>();
        if (type != "ParticleEffect") {
            assign_parse_error(error, std::string{"unexpected particle JSON $type: "} + type);
            return false;
        }

        value = j.get<ParticleEffectDef>();
        return true;
    } catch (const std::exception& e) {
        assign_parse_error(error, e.what());
        return false;
    }
}

bool try_load_particle_effect_json(std::istream& input, ParticleEffectDef& value, std::string* error)
{
    try {
        nlohmann::json j;
        input >> j;
        return try_parse_particle_effect_json(j, value, error);
    } catch (const std::exception& e) {
        assign_parse_error(error, e.what());
        return false;
    }
}

} // namespace nw::render
