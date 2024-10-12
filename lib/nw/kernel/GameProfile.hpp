#pragma once

namespace nw {

/// Abstract base class for game profiles
struct GameProfile {
    virtual ~GameProfile() = default;

    /// Load any custom services that the game profile might need. Default: No-op
    void load_custom_services() { }

    /// Loads game specific rules
    virtual bool load_rules() const = 0;

    /// Loads default resources in to the resource manager.
    virtual bool load_resources() = 0;
};

} // namespace nw
