#include "creature_ability_loadout.hpp"

#include "../../kernel/Rules.hpp"
#include "../../log.hpp"
#include "../../objects/Creature.hpp"
#include "../../objects/ObjectComponentSystem.hpp"
#include "../../objects/ObjectManager.hpp"
#include "../../serialization/Gff.hpp"
#include "legacy_spellbook_gff.hpp"

#include <nlohmann/json.hpp>

namespace nwn1 {

namespace {

constexpr size_t creature_class_slot_count = 8;

void clear_ability_loadout(nw::ObjectHandle owner)
{
    nw::kernel::objects().components().from_json_ability_loadout(owner, nlohmann::json::array());
}

void import_spellbook_as_ability_loadout(
    nw::ObjectHandle owner,
    nw::Class class_id,
    const LegacySpellBook& spellbook)
{
    if (class_id == nw::Class::invalid()) { return; }

    auto& components = nw::kernel::objects().components();
    const int32_t source = *class_id;
    const size_t levels = nw::kernel::rules().maximum_spell_levels();
    for (size_t tier = 0; tier < levels; ++tier) {
        for (const nw::Spell spell : spellbook.known[tier]) {
            if (spell != nw::Spell::invalid()) {
                components.add_unslotted_ability(owner, source, static_cast<int32_t>(tier), *spell);
            }
        }

        if (spellbook.memorized[tier].empty()) { continue; }

        components.set_slotted_ability_count(owner, source, static_cast<int32_t>(tier),
            spellbook.memorized[tier].size());
        for (size_t slot = 0; slot < spellbook.memorized[tier].size(); ++slot) {
            const LegacySpellBookEntry& entry = spellbook.memorized[tier][slot];
            if (entry.spell == nw::Spell::invalid()) { continue; }

            components.set_slotted_ability(owner, source, static_cast<int32_t>(tier),
                static_cast<int32_t>(slot), *entry.spell, *entry.meta,
                static_cast<uint32_t>(entry.flags));
        }
    }
}

bool validate_creature_class_list(const nw::GffStruct& archive)
{
    const size_t sz = archive["ClassList"].size();
    if (sz > creature_class_slot_count) {
        LOG_F(ERROR, "level stats: attempting a creature with more than {} classes", creature_class_slot_count);
        return false;
    }
    return true;
}

} // namespace

bool import_creature_ability_loadout_from_gff(
    nw::Creature& creature,
    const nw::GffStruct& archive)
{
    if (!validate_creature_class_list(archive)) { return false; }

    clear_ability_loadout(creature.handle());

    const auto classes = archive["ClassList"];
    for (size_t i = 0; i < classes.size(); ++i) {
        nw::Class class_id = nw::Class::invalid();
        int32_t temp = 0;
        if (classes[i].get_to("Class", temp)) {
            class_id = nw::Class::make(temp);
        }

        LegacySpellBook spellbook;
        if (!deserialize_legacy_spellbook(spellbook, classes[i])) { return false; }
        import_spellbook_as_ability_loadout(creature.handle(), class_id, spellbook);
    }

    return true;
}

} // namespace nwn1
