#pragma once

#include "../util/enum_flags.hpp"

namespace nw {

enum struct SituationFlag {
    none = 0,
    blind = 0x1,
    coup_de_grace = 0x2,
    death_attack = 0x4,
    flanked = 0x8,
    flat_footed = 0x10,
    sneak_attack = 0x20,
};

DEFINE_ENUM_FLAGS(SituationFlag)

enum struct SituationType {
    none = 0,
    blind = 1,
    coup_de_grace = 2,
    death_attack = 3,
    flanked = 4,
    flat_footed = 5,
    sneak_attack = 6,
};

struct SituationInfo {
};

} // namespace nw
