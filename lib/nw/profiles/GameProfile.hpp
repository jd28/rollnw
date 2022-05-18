#pragma once

namespace nw {

/// Abstract base class for game profiles
struct GameProfile {
    virtual ~GameProfile() = default;

    /// Load constants
    virtual bool load_constants() const = 0;

    /// Loads game specific components
    virtual bool load_compontents() const = 0;

    /// Loads game specific rules
    virtual bool load_rules() const = 0;
};

} // namespace nw
