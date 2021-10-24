#pragma once

#include "../formats/Gff.hpp"

#include <nlohmann/json_fwd.hpp>

namespace nw {

/// Game serialization profiles
enum struct SerializationProfile {
    any,
    blueprint,
    instance,
    savegame
};

} // namespace nw
