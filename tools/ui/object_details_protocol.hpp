#pragma once

#include <cstdint>

namespace nw::toolset {

// Editor kinds cross the SmallS presentation boundary as integer values.
enum class ObjectDetailsEditorKind : uint8_t {
    read_only,
    boolean,
    integer,
    door_state,
    sound_position,
    sound_volume,
    locstring,
    area_weather_boolean,
};

// Stable field identifiers for the area-weather rows in the Details edit
// protocol. AreaWeather remains editor-agnostic storage.
enum class ObjectDetailsAreaWeatherField : uint8_t {
    day_night_cycle,
    is_night,
    sun_shadows,
    moon_shadows,
    count,
};

} // namespace nw::toolset
