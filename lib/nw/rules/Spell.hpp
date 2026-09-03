#pragma once

#include "../util/enum_flags.hpp"
#include "rule_type.hpp"

namespace nw {

enum struct SpellFlags : uint8_t {
    none = 0x0,
    readied = 0x01,
    spontaneous = 0x02,
    unlimited = 0x04,

    any = 0xFF,
};

DEFINE_ENUM_FLAGS(SpellFlags);

DECLARE_RULE_TYPE(MetaMagic);
DECLARE_RULE_TYPE(MetaMagicCode);
DECLARE_RULE_TYPE(MetaMagicMask);

constexpr MetaMagicCode metamagic_none = MetaMagicCode::make(0);
constexpr MetaMagicCode metamagic_any = MetaMagicCode::make(0xFF);
constexpr MetaMagicMask metamagic_mask_none = MetaMagicMask::make(0);

DECLARE_RULE_TYPE(SpellSchool);

DECLARE_RULE_TYPE(Spell);

} // namespace nw
