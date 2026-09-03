#pragma once

#include "../../kernel/GameProfile.hpp"

namespace nwn1 {

/// NWN1 Game Profile
struct Profile : nw::GameProfile {
    virtual ~Profile() = default;

    /// Load rules
    /// @see ``rules.hpp`` and ``rules.cpp``
    bool load_rules() const override;
};

} // namespace nwn1
