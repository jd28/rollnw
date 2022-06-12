#pragma once

#include "constants.hpp"
#include "rules.hpp"

#include "../GameProfile.hpp"

namespace nwn1 {

struct Profile : nw::GameProfile {
    virtual ~Profile() = default;

    virtual bool load_constants() const override;
    virtual bool load_components() const override;
    virtual bool load_rules() const override;
};

} // namespace nwn1
