#pragma once

#include "../formats/TwoDA.hpp"
#include "../resources/Resource.hpp"
#include "../rules/BaseItem.hpp"
#include "../rules/Class.hpp"
#include "../rules/Feat.hpp"
#include "../rules/Race.hpp"
#include "../rules/Skill.hpp"
#include "../rules/Spell.hpp"
#include "../rules/system.hpp"
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

    /// Gets constant
    virtual Constant get_constant(std::string_view name) const;

    /// Registers constant
    /// @note If constant by name is already registered, it will return that.
    virtual Constant register_constant(std::string_view name, int value);

private:
    absl::flat_hash_map<InternedString, RuleVariant, InternedStringHash, InternedStringEq> constants_;
};

} // namespace nw::kernel
