#include "modifiers.hpp"

#include "constants.hpp"
#include "rules.hpp"

#include <nw/components/CreatureStats.hpp>
#include <nw/kernel/Kernel.hpp>

namespace nwn1 {

void load_modifiers()
{
    LOG_F(INFO, "[nwn1] loading modifiers...");

    nw::kernel::rules().add(mod::ability(
        ability_strength,
        epic_great_strength,
        "dnd-3.0-epic-great-strength",
        nw::ModifierSource::feat));

    nw::kernel::rules().add(mod::ability(
        ability_dexterity,
        epic_great_dexterity,
        "dnd-3.0-epic-great-dexterity",
        nw::ModifierSource::feat));

    nw::kernel::rules().add(mod::ability(
        ability_constitution,
        epic_great_constitution,
        "dnd-3.0-epic-great-constitution",
        nw::ModifierSource::feat));

    nw::kernel::rules().add(mod::ability(
        ability_intelligence,
        epic_great_intelligence,
        "dnd-3.0-epic-great-intelligence",
        nw::ModifierSource::feat));

    nw::kernel::rules().add(mod::ability(
        ability_wisdom,
        epic_great_wisdom,
        "dnd-3.0-epic-great-wisdom",
        nw::ModifierSource::feat));

    nw::kernel::rules().add(mod::ability(
        ability_charisma,
        epic_great_charisma,
        "dnd-3.0-epic-great-charisma",
        nw::ModifierSource::feat));

    nw::kernel::rules().add(mod::armor_class(
        ac_natural,
        dragon_disciple_ac,
        "dnd-3.0-dragon-disciple-ac",
        nw::ModifierSource::class_));

    nw::kernel::rules().add(mod::armor_class(
        ac_natural,
        pale_master_ac,
        "dnd-3.0-palemaster-ac",
        nw::ModifierSource::class_));

    nw::kernel::rules().add(mod::hitpoints(
        toughness,
        "dnd-3.0-toughness",
        nw::ModifierSource::feat));

    nw::kernel::rules().add(mod::hitpoints(
        epic_toughness,
        "dnd-3.0-epic-toughness",
        nw::ModifierSource::feat));
}

} // namespace nwn1
