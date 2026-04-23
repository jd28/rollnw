#pragma once

#include "mudl_commands.hpp"

#include <nw/formats/StaticTwoDA.hpp>

#include <optional>
#include <string>

namespace mudl {

enum class ProgFxTravelTiming {
    log,
    linear,
    linear2,
};

struct ProgFxDef {
    size_t row = 0;
    int type = 0;
    std::string label;
    std::string model;
    std::string animation;
    std::string node;
    std::string initial_sound;
    std::string impact_sound;
    VfxSequenceStepKind step_kind = VfxSequenceStepKind::model;
    VfxProjectilePathKind projectile_path = VfxProjectilePathKind::default_path;
    VfxProjectileOrientationKind projectile_orientation = VfxProjectileOrientationKind::none;
    ProgFxTravelTiming projectile_timing = ProgFxTravelTiming::log;
};

std::optional<ProgFxDef> parse_progfx_def(const nw::StaticTwoDA& progfx, size_t row,
    std::string_view source_label);

} // namespace mudl
