#pragma once

namespace nw {
struct Creature;
struct GffStruct;
} // namespace nw

namespace nwn1 {

/// Import legacy NWN1 ClassList spellbook rows into the native ability-loadout
/// component. This is a GFF conversion boundary, not runtime ownership.
[[nodiscard]] bool import_creature_ability_loadout_from_gff(
    nw::Creature& creature,
    const nw::GffStruct& archive);

} // namespace nwn1
