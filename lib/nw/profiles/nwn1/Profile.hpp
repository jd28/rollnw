#pragma once

#include "../../kernel/GameProfile.hpp"

namespace nwn1 {

/// NWN1 Game Profile
struct Profile : nw::GameProfile {
    virtual ~Profile() = default;

    /// Register NWN1 native smalls modules and add nwn1 script search path
    void load_custom_services() override;

    /// Load rules
    /// @see ``rules.hpp`` and ``rules.cpp``
    bool load_rules() const override;

    /// Load containers into resman
    bool load_resources() override;
};

} // namespace nwn1
