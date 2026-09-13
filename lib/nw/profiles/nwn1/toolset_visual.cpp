#include "toolset_visual.hpp"

#include "scriptbridge.hpp"

#include "../../kernel/Kernel.hpp"
#include "../../objects/ObjectManager.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace nwn1 {
namespace {

std::optional<SoundToolsetVisualState>
read_sound_toolset_visual_state(nw::ObjectHandle sound)
{
    if (sound.type != nw::ObjectType::sound
        || !nw::kernel::objects().valid(sound)) {
        return std::nullopt;
    }
    const nw::Vector<nw::smalls::Value> args{
        bridge::make_object_arg(sound)};
    const auto value = bridge::call_nwn1_module_value(
        "nwn1.toolset_visual", "sound_visual_state", args);
    if (!value || value->storage != nw::smalls::ValueStorage::heap
        || value->data.hptr.value == 0) {
        return std::nullopt;
    }

    auto& runtime = nw::kernel::runtime();
    const auto distance_min = runtime.read_struct_field(
        value->data.hptr, value->type_id, "distance_min");
    const auto distance_max = runtime.read_struct_field(
        value->data.hptr, value->type_id, "distance_max");
    const auto elevation = runtime.read_struct_field(
        value->data.hptr, value->type_id, "elevation");
    const auto positional = runtime.read_struct_field(
        value->data.hptr, value->type_id, "positional");
    const auto random_position = runtime.read_struct_field(
        value->data.hptr, value->type_id, "random_position");
    const auto valid = runtime.read_struct_field(
        value->data.hptr, value->type_id, "valid");
    if (distance_min.type_id != runtime.float_type()
        || distance_max.type_id != runtime.float_type()
        || elevation.type_id != runtime.float_type()
        || positional.type_id != runtime.bool_type()
        || random_position.type_id != runtime.bool_type()
        || valid.type_id != runtime.bool_type()
        || !valid.data.bval
        || !std::isfinite(distance_min.data.fval)
        || !std::isfinite(distance_max.data.fval)
        || !std::isfinite(elevation.data.fval)
        || distance_min.data.fval < 0.0f
        || distance_max.data.fval < distance_min.data.fval) {
        return std::nullopt;
    }
    return SoundToolsetVisualState{
        .distance_min = distance_min.data.fval,
        .distance_max = distance_max.data.fval,
        .elevation = elevation.data.fval,
        .positional = positional.data.bval,
        .random_position = random_position.data.bval,
    };
}

} // namespace

SoundToolsetVisualBatchStats sound_toolset_visual_states(
    std::span<const nw::ObjectHandle> sounds,
    std::span<SoundToolsetVisualState> states,
    std::span<uint8_t> valid)
{
    SoundToolsetVisualBatchStats stats{
        .input_count = sounds.size(),
    };
    std::fill(states.begin(), states.end(), SoundToolsetVisualState{});
    std::fill(valid.begin(), valid.end(), uint8_t{0});
    if (states.size() != sounds.size() || valid.size() != sounds.size()) {
        stats.rejected_count = sounds.size();
        return stats;
    }
    for (size_t index = 0; index < sounds.size(); ++index) {
        const auto state = read_sound_toolset_visual_state(sounds[index]);
        if (!state) {
            ++stats.rejected_count;
            continue;
        }
        states[index] = *state;
        valid[index] = 1;
        ++stats.output_count;
    }
    return stats;
}

std::optional<SoundToolsetVisualState>
sound_toolset_visual_state(nw::ObjectHandle sound)
{
    const std::array sounds{sound};
    std::array<SoundToolsetVisualState, 1> states{};
    std::array<uint8_t, 1> valid{};
    const auto stats = sound_toolset_visual_states(
        sounds, states, valid);
    return stats.output_count == 1 && valid[0]
        ? std::optional{states[0]}
        : std::nullopt;
}

bool replace_sound_toolset_radius(
    nw::ObjectHandle sound, float expected, float replacement)
{
    if (sound.type != nw::ObjectType::sound
        || !nw::kernel::objects().valid(sound)
        || !std::isfinite(expected)
        || !std::isfinite(replacement)
        || replacement < 0.0f) {
        return false;
    }
    const nw::Vector<nw::smalls::Value> args{
        bridge::make_object_arg(sound),
        nw::smalls::Value::make_float(expected),
        nw::smalls::Value::make_float(replacement),
    };
    return bridge::call_nwn1_module_bool(
        "nwn1.toolset_visual", "replace_sound_distance_max", args)
        .value_or(false);
}

} // namespace nwn1
