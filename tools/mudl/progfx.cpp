#include "progfx.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/util/string.hpp>

namespace mudl {
namespace {

bool resource_exists(std::string_view resref)
{
    return !resref.empty() && nw::kernel::resman().contains({nw::Resref{resref}, nw::ResourceType::mdl});
}

std::string read_string(const nw::StaticTwoDA& table, size_t row, std::string_view column)
{
    nw::String value;
    if (!table.get_to(row, column, value, false) || value.empty()) {
        return {};
    }
    return value;
}

VfxProjectileOrientationKind parse_projectile_orientation(int value)
{
    switch (value) {
    case 1: return VfxProjectileOrientationKind::target;
    case 2: return VfxProjectileOrientationKind::path;
    default: return VfxProjectileOrientationKind::none;
    }
}

VfxProjectilePathKind parse_projectile_path(int value)
{
    switch (value) {
    case 1: return VfxProjectilePathKind::homing;
    case 2: return VfxProjectilePathKind::ballistic;
    case 3: return VfxProjectilePathKind::high_ballistic;
    case 4: return VfxProjectilePathKind::burst_up;
    case 5: return VfxProjectilePathKind::accelerating;
    case 6: return VfxProjectilePathKind::spiral;
    case 7: return VfxProjectilePathKind::linked;
    case 8: return VfxProjectilePathKind::bounce;
    case 9: return VfxProjectilePathKind::burst;
    case 10: return VfxProjectilePathKind::linked_burst_up;
    case 11: return VfxProjectilePathKind::triple_ballistic_hit;
    case 12: return VfxProjectilePathKind::triple_ballistic_miss;
    case 13: return VfxProjectilePathKind::double_ballistic;
    default: return VfxProjectilePathKind::default_path;
    }
}

ProgFxTravelTiming parse_travel_timing(std::string_view value)
{
    if (nw::string::icmp(value, "linear")) {
        return ProgFxTravelTiming::linear;
    }
    if (nw::string::icmp(value, "linear2")) {
        return ProgFxTravelTiming::linear2;
    }
    return ProgFxTravelTiming::log;
}

} // namespace

std::optional<ProgFxDef> parse_progfx_def(const nw::StaticTwoDA& progfx, size_t row,
    std::string_view source_label)
{
    int type = 0;
    if (!progfx.get_to(row, "Type", type, false)) {
        return std::nullopt;
    }

    ProgFxDef result;
    result.row = row;
    result.type = type;
    result.label = read_string(progfx, row, "Label");
    if (result.label.empty()) {
        result.label = std::string(source_label);
    }

    switch (type) {
    case 7:
        result.model = read_string(progfx, row, "Param1");
        result.animation = read_string(progfx, row, "Param2");
        result.step_kind = VfxSequenceStepKind::beam;
        break;
    case 10: {
        result.model = read_string(progfx, row, "Param1");
        int orientation = 0;
        int path = 0;
        progfx.get_to(row, "Param3", orientation, false);
        progfx.get_to(row, "Param4", path, false);
        result.projectile_orientation = parse_projectile_orientation(orientation);
        result.projectile_path = parse_projectile_path(path);
        result.projectile_timing = parse_travel_timing(read_string(progfx, row, "Param5"));
        result.step_kind = VfxSequenceStepKind::projectile;
        break;
    }
    case 11: {
        result.model = read_string(progfx, row, "Param1");
        result.initial_sound = read_string(progfx, row, "Param2");
        result.impact_sound = read_string(progfx, row, "Param3");
        int path = 0;
        progfx.get_to(row, "Param4", path, false);
        result.projectile_path = parse_projectile_path(path);
        result.step_kind = VfxSequenceStepKind::projectile;
        break;
    }
    case 12:
        result.node = read_string(progfx, row, "Param1");
        result.model = read_string(progfx, row, "Param2");
        result.step_kind = VfxSequenceStepKind::attached_model;
        break;
    case 1:
        result.model = read_string(progfx, row, "Param1");
        result.step_kind = VfxSequenceStepKind::model;
        break;
    case 4:
        result.model = read_string(progfx, row, "Param7");
        result.step_kind = VfxSequenceStepKind::model;
        break;
    default:
        return std::nullopt;
    }

    if (!resource_exists(result.model)) {
        return std::nullopt;
    }

    return result;
}

} // namespace mudl
