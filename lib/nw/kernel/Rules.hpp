#pragma once

#include "../formats/TwoDA.hpp"
#include "../resources/Resource.hpp"
#include "../rules/BaseItem.hpp"
#include "../rules/Class.hpp"
#include "../rules/Feat.hpp"
#include "../rules/Race.hpp"
#include "../rules/Skill.hpp"
#include "../rules/Spell.hpp"
#include "Strings.hpp"

#include <absl/container/flat_hash_map.h>

#include <cstdint>
#include <limits>
#include <vector>

namespace nw::kernel {

struct Rules {
    virtual ~Rules();

    // Intializes rules system
    virtual bool initialize();

    /// Clears rules system of all rules and cached 2da files
    virtual void clear();
};

} // namespace nw::kernel
