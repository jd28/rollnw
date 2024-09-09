#pragma once

namespace nw {
struct CombatMode;
struct Creature;
struct ModifierResult;
struct ModifierType;
}

namespace nwn1 {

// == Modifiers ===============================================================
// ============================================================================

bool combat_mode_can_always_use(nw::CombatMode, const nw::Creature*);

nw::ModifierResult combat_mode_expertise_mod(nw::CombatMode mode, nw::ModifierType type, const nw::Creature* obj);
nw::ModifierResult combat_mode_flurry_mod(nw::CombatMode mode, nw::ModifierType type, const nw::Creature* obj);
nw::ModifierResult combat_mode_power_attach_mod(nw::CombatMode mode, nw::ModifierType type, const nw::Creature* obj);
nw::ModifierResult combat_mode_rapid_shot_mod(nw::CombatMode mode, nw::ModifierType type, const nw::Creature* obj);

void load_modifiers();

}
