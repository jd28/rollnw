#include "modifiers.hpp"

#include "constants.hpp"
#include "rules.hpp"

#include <nw/components/CreatureStats.hpp>
#include <nw/kernel/Kernel.hpp>

namespace nwn1 {

void load_modifiers()
{
    LOG_F(INFO, "[nwn1] loading modifiers...");

    nw::kernel::rules().add(mod::armor_class(
        ac_natural,
        dragon_disciple_ac,
        nw::Requirement{}, //{qual::class_level(class_type_dragon_disciple, 1)},
        {},
        "dnd-3.0-dragon-disciple-ac",
        nw::ModifierSource::class_));

    nw::kernel::rules().add(mod::armor_class(
        ac_natural,
        pale_master_ac,
        nw::Requirement{}, // nw::Requirement{qual::class_level(class_type_pale_master, 1)},
        {},
        "dnd-3.0-palemaster-ac",
        nw::ModifierSource::class_));

    nw::kernel::rules().add(mod::hitpoints(
        epic_toughness,
        nw::Requirement{},
        {},
        "dnd-3.0-epic-toughness",
        nw::ModifierSource::feat));
}

} // namespace nwn1
