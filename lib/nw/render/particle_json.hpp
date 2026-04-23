#pragma once

#include "particle_def.hpp"

#include <iosfwd>
#include <nlohmann/json_fwd.hpp>
#include <string>

namespace nw::render {

void to_json(nlohmann::json& j, const ParticleRangeF32& value);
void from_json(const nlohmann::json& j, ParticleRangeF32& value);

void to_json(nlohmann::json& j, const ParticleCurveKeyF32& value);
void from_json(const nlohmann::json& j, ParticleCurveKeyF32& value);

void to_json(nlohmann::json& j, const ParticleCurveF32& value);
void from_json(const nlohmann::json& j, ParticleCurveF32& value);

void to_json(nlohmann::json& j, const ParticleGradientKey& value);
void from_json(const nlohmann::json& j, ParticleGradientKey& value);

void to_json(nlohmann::json& j, const ParticleGradient& value);
void from_json(const nlohmann::json& j, ParticleGradient& value);

void to_json(nlohmann::json& j, const ParticleSpawnRegion& value);
void from_json(const nlohmann::json& j, ParticleSpawnRegion& value);

void to_json(nlohmann::json& j, const ParticleSpriteSheet& value);
void from_json(const nlohmann::json& j, ParticleSpriteSheet& value);

void to_json(nlohmann::json& j, const ParticleMaterialDef& value);
void from_json(const nlohmann::json& j, ParticleMaterialDef& value);

void to_json(nlohmann::json& j, const ParticleInitialStateDef& value);
void from_json(const nlohmann::json& j, ParticleInitialStateDef& value);

void to_json(nlohmann::json& j, const ParticleOverLifeDef& value);
void from_json(const nlohmann::json& j, ParticleOverLifeDef& value);

void to_json(nlohmann::json& j, const ParticleTargetingDef& value);
void from_json(const nlohmann::json& j, ParticleTargetingDef& value);

void to_json(nlohmann::json& j, const ParticleBeamDef& value);
void from_json(const nlohmann::json& j, ParticleBeamDef& value);

void to_json(nlohmann::json& j, const ParticleForceEventDef& value);
void from_json(const nlohmann::json& j, ParticleForceEventDef& value);

void to_json(nlohmann::json& j, const ParticleCollisionDef& value);
void from_json(const nlohmann::json& j, ParticleCollisionDef& value);

void to_json(nlohmann::json& j, const ParticleSpawnOverTimeDef& value);
void from_json(const nlohmann::json& j, ParticleSpawnOverTimeDef& value);

void to_json(nlohmann::json& j, const ParticleRenderDef& value);
void from_json(const nlohmann::json& j, ParticleRenderDef& value);

void to_json(nlohmann::json& j, const ParticleEmissionDef& value);
void from_json(const nlohmann::json& j, ParticleEmissionDef& value);

void to_json(nlohmann::json& j, const ParticleEmitterDef& value);
void from_json(const nlohmann::json& j, ParticleEmitterDef& value);

void to_json(nlohmann::json& j, const ParticleEffectDef& value);
void from_json(const nlohmann::json& j, ParticleEffectDef& value);

bool try_parse_particle_effect_json(const nlohmann::json& j, ParticleEffectDef& value, std::string* error = nullptr);
bool try_load_particle_effect_json(std::istream& input, ParticleEffectDef& value, std::string* error = nullptr);

} // namespace nw::render
