#pragma once

#include "GffInputArchive.hpp"

#include <nlohmann/json.hpp>

namespace nw {

/// Game serialization profiles
enum struct SerializationProfile {
    any,
    blueprint,
    instance,
    savegame
};

} // namespace nw
