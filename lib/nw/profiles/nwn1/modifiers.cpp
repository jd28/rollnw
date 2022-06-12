#include "modifiers.hpp"

#include "../../kernel/Kernel.hpp"
#include "../../objects/components/CreatureStats.hpp"
#include "constants.hpp"
#include "rules.hpp"

namespace nwn1 {

void load_modifiers()
{
    nw::kernel::rules().add(mod::ac_natural(
        dragon_disciple_ac,
        nw::Requirement{qual::class_level(class_type_dragon_disciple, 1)},
        {},
        "dnd-3.0-dragon-disciple-ac",
        nw::ModifierSource::class_));

    nw::kernel::rules().add(mod::ac_natural(
        pale_master_ac,
        nw::Requirement{qual::class_level(class_type_pale_master, 1)},
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
