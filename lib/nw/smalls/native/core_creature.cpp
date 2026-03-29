#include "../runtime.hpp"

#include "../../kernel/Rules.hpp"
#include "../../objects/CombatInfo.hpp"
#include "../../objects/ObjectManager.hpp"
#include "../../scriptapi.hpp"

namespace nw::smalls {

namespace {

nw::Creature* as_creature(nw::ObjectHandle obj)
{
    auto* base = nw::kernel::objects().get_object_base(obj);
    if (!base) {
        return nullptr;
    }
    return base->as_creature();
}

nw::Item* as_item(nw::ObjectHandle obj)
{
    auto* base = nw::kernel::objects().get_object_base(obj);
    if (!base) {
        return nullptr;
    }
    return base->as_item();
}

} // namespace

void register_core_creature(Runtime& rt)
{
    if (rt.get_native_module("core.creature")) {
        return;
    }

    rt.module("core.creature")
        .function("get_total_levels", +[](nw::ObjectHandle obj) -> int32_t {
            auto* creature = as_creature(obj);
            return creature ? creature->levels.level() : 0; })
        .function("progression_version", +[](nw::ObjectHandle obj) -> int32_t {
            auto* creature = as_creature(obj);
            if (!creature) {
                return 0;
            }

            uint32_t v = static_cast<uint32_t>(creature->levels.level());
            v = (v * 16777619u) ^ static_cast<uint32_t>(creature->stats.feats().size());
            for (const auto& ent : creature->levels.entries) {
                if (ent.id == nw::Class::invalid()) {
                    break;
                }
                v = (v * 16777619u) ^ static_cast<uint32_t>(*ent.id);
                v = (v * 16777619u) ^ static_cast<uint32_t>(ent.level);
            }

            return static_cast<int32_t>(v); })
        .function("get_level_by_class", +[](nw::ObjectHandle obj, int32_t class_type) -> int32_t {
            auto* creature = as_creature(obj);
            if (!creature) {
                return 0;
            }
            return creature->levels.level_by_class(nw::Class::make(class_type)); })
        .function("get_class_count", +[](nw::ObjectHandle obj) -> int32_t {
            auto* creature = as_creature(obj);
            if (!creature) { return 0; }

            int32_t result = 0;
            for (const auto& ent : creature->levels.entries) {
                if (ent.id == nw::Class::invalid()) {
                    break;
                }
                ++result;
            }
            return result; })
        .function("get_class_at", +[](nw::ObjectHandle obj, int32_t index) -> int32_t {
            auto* creature = as_creature(obj);
            if (!creature || index < 0 || index >= int32_t(creature->levels.entries.size())) {
                return *nw::Class::invalid();
            }
            return *creature->levels.entries[index].id; })
        .function("get_class_levels_at", +[](nw::ObjectHandle obj, int32_t index) -> int32_t {
            auto* creature = as_creature(obj);
            if (!creature || index < 0 || index >= int32_t(creature->levels.entries.size())) {
                return 0;
            }
            return creature->levels.entries[index].level; })
        .function("get_levelup_class_position", +[](nw::ObjectHandle obj, int32_t level_index) -> int32_t {
            auto* creature = as_creature(obj);
            if (!creature || level_index < 0 || level_index >= int32_t(creature->history.entries.size())) {
                return -1;
            }
            auto pos = creature->levels.position(creature->history.entries[level_index].class_);
            if (pos == nw::LevelStats::npos) {
                return -1;
            }
            return int32_t(pos); })
        .function("can_equip_item", +[](nw::ObjectHandle creature, nw::ObjectHandle item, int32_t slot) -> bool { return nw::can_equip_item(as_creature(creature), as_item(item), static_cast<nw::EquipIndex>(slot)); })
        .function("equip_item_in_slot", +[](nw::ObjectHandle creature, nw::ObjectHandle item, int32_t slot) -> bool { return nw::equip_item_in_slot(as_creature(creature), as_item(item), static_cast<nw::EquipIndex>(slot)); })
        .function("get_equipped_item", +[](nw::ObjectHandle creature, int32_t slot) -> nw::ObjectHandle {
            auto* item = nw::get_equipped_item(as_creature(creature), static_cast<nw::EquipIndex>(slot));
            return item ? item->handle() : nw::ObjectHandle{}; })
        .function("unequip_item_in_slot", +[](nw::ObjectHandle creature, int32_t slot) -> nw::ObjectHandle {
            auto* item = nw::unequip_item_in_slot(as_creature(creature), static_cast<nw::EquipIndex>(slot));
            return item ? item->handle() : nw::ObjectHandle{}; })
        .function("inventory_add_item", +[](nw::ObjectHandle creature, nw::ObjectHandle item) -> bool {
            auto* cre = as_creature(creature);
            auto* it = as_item(item);
            if (!cre || !it) { return false; }
            return cre->inventory.add_item(it); })
        .function("inventory_remove_item", +[](nw::ObjectHandle creature, nw::ObjectHandle item) -> bool {
            auto* cre = as_creature(creature);
            auto* it = as_item(item);
            if (!cre || !it) { return false; }
            return cre->inventory.remove_item(it); })

        // == Feats ===================================================================
        // ============================================================================
        .function("has_feat", +[](nw::ObjectHandle obj, int32_t feat) -> bool {
            auto* creature = as_creature(obj);
            return creature && creature->stats.has_feat(nw::Feat::make(feat)); })

        // Returns the highest feat value in [start, stop] the creature has, or -1 if none.
        // Uses a single binary search on the sorted feat list — O(log n) vs O(range * log n)
        // for looping has_feat.
        .function("highest_feat_in_range", +[](nw::ObjectHandle obj, int32_t start, int32_t stop) -> int32_t {
            auto* creature = as_creature(obj);
            if (!creature) return -1;
            const auto& feats = creature->stats.feats();
            auto it = std::upper_bound(feats.begin(), feats.end(), nw::Feat::make(stop));
            if (it != feats.begin()) {
                --it;
                if (*(*it) >= start) return *(*it);
            }
            return -1; })

        // Returns highest - start + 1 for the highest feat in [start, stop], or 0 if none.
        .function("count_feats_in_range", +[](nw::ObjectHandle obj, int32_t start, int32_t stop) -> int32_t {
            auto* creature = as_creature(obj);
            if (!creature) return 0;
            const auto& feats = creature->stats.feats();
            auto it = std::upper_bound(feats.begin(), feats.end(), nw::Feat::make(stop));
            if (it != feats.begin()) {
                --it;
                if (*(*it) >= start) return *(*it) - start + 1;
            }
            return 0; })

        // The end.
        .finalize();
}

} // namespace nw::smalls
