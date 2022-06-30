#pragma once

namespace nw {

/// Abstract base class for game profiles
struct GameProfile {
    virtual ~GameProfile() = default;

    /// Loads game specific rules
    virtual bool load_rules() const = 0;
};

} // namespace nw
