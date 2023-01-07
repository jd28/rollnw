#include "functions.hpp"

#include "kernel/EffectSystem.hpp"
#include "kernel/Rules.hpp"
#include "objects/Creature.hpp"
#include "rules/feats.hpp"

namespace nw {

// == Effects =================================================================
// ============================================================================

bool apply_effect(nw::ObjectBase* obj, nw::Effect* effect)
{
    if (!obj || !effect) { return false; }
    if (nw::kernel::effects().apply(obj, effect)) {
        obj->effects().add(effect);
        return true;
    }
    return false;
}

int effect_extract_int0(const nw::EffectHandle& handle)
{
    return handle.effect ? handle.effect->get_int(0) : 0;
}

bool has_effect_applied(nw::ObjectBase* obj, nw::EffectType type, int subtype)
{
    if (!obj || type == nw::EffectType::invalid()) { return false; }
    auto it = find_first_effect_of(std::begin(obj->effects()), std::end(obj->effects()),
        type, subtype);

    return it != std::end(obj->effects());
}

bool remove_effect(nw::ObjectBase* obj, nw::Effect* effect, bool destroy)
{
    if (nw::kernel::effects().remove(obj, effect)) {
        obj->effects().remove(effect);
        if (destroy) { nw::kernel::effects().destroy(effect); }
        return true;
    }
    return false;
}

int remove_effects_by(nw::ObjectBase* obj, nw::ObjectHandle creator)
{
    int result = 0;
    auto it = std::remove_if(std::begin(obj->effects()), std::end(obj->effects()),
        [&](const nw::EffectHandle& handle) {
            if (handle.creator == creator && nw::kernel::effects().remove(obj, handle.effect)) {
                nw::kernel::effects().destroy(handle.effect);
                ++result;
                return true;
            }
            return false;
        });

    obj->effects().erase(it, std::end(obj->effects()));

    return result;
}

// == Feats ===================================================================
// ============================================================================

int count_feats_in_range(const nw::Creature* obj, nw::Feat start, nw::Feat end)
{
    if (!obj) { return 0; }

    int result = 0;
    int32_t s = *start, e = *end;

    while (e >= s) {
        if (obj->stats.has_feat(nw::Feat::make(e))) {
            ++result;
        }
        --e;
    }
    return result;
}

std::vector<nw::Feat> get_all_available_feats(const nw::Creature* obj)
{
    if (!obj) return {};

    std::vector<nw::Feat> result;

    auto& feats = nw::kernel::rules().feats;
    if (!obj) {
        return result;
    }

    for (size_t i = 0; i < feats.entries.size(); ++i) {
        if (feats.entries[i].valid()
            && !obj->stats.has_feat(nw::Feat::make(uint32_t(i))) // [TODO] Not efficient
            && nw::kernel::rules().meets_requirement(feats.entries[i].requirements, obj)) {
            result.push_back(nw::Feat::make(uint32_t(i)));
        }
    }

    return result;
}

bool knows_feat(const nw::Creature* obj, nw::Feat feat)
{
    if (!obj) { return false; }

    auto it = std::lower_bound(std::begin(obj->stats.feats()), std::end(obj->stats.feats()), feat);
    return it != std::end(obj->stats.feats()) && *it == feat;
}

std::pair<nw::Feat, int> has_feat_successor(const nw::Creature* obj, nw::Feat feat)
{
    nw::Feat highest = nw::Feat::invalid();
    int count = 0;

    auto featarray = &nw::kernel::rules().feats;
    if (!featarray || !obj) {
        return {highest, count};
    }

    auto it = std::lower_bound(std::begin(obj->stats.feats()), std::end(obj->stats.feats()), feat);
    do {
        if (it == std::end(obj->stats.feats()) || *it != feat) {
            break;
        }
        highest = feat;
        ++count;
        const auto next_entry = featarray->get(feat);
        if (!next_entry || next_entry->successor == nw::Feat::invalid()) {
            break;
        }
        feat = next_entry->successor;
        it = std::lower_bound(it, std::end(obj->stats.feats()), feat);
    } while (true);

    return {highest, count};
}

nw::Feat highest_feat_in_range(const nw::Creature* obj, nw::Feat start, nw::Feat end)
{
    if (!obj) { return nw::Feat::invalid(); }

    int32_t s = *start, e = *end;
    while (e >= s) {
        if (obj->stats.has_feat(nw::Feat::make(e))) {
            return nw::Feat::make(e);
        }
        --e;
    }
    return nw::Feat::invalid();
}

// == Item Properties =========================================================
// ============================================================================

bool item_has_property(const Item* item, ItemPropertyType type, int32_t subtype)
{
    if (!item) { return false; }
    for (const auto& ip : item->properties) {
        if (ip.type == *type && (subtype == -1 || ip.subtype == subtype)) {
            return true;
        }
    }
    return false;
}

std::string itemprop_to_string(const nw::ItemProperty& ip)
{
    std::string result;
    if (ip.type == std::numeric_limits<uint16_t>::max()) { return result; }
    auto type = nw::ItemPropertyType::make(ip.type);
    auto def = nw::kernel::effects().ip_definition(type);
    if (!def) { return result; }

    result = nw::kernel::strings().get(def->game_string);

    if (ip.subtype != std::numeric_limits<uint16_t>::max() && def->subtype) {
        if (auto name = def->subtype->get<uint32_t>(ip.subtype, "Name")) {
            result += " " + nw::kernel::strings().get(*name);
        }
    }

    if (ip.cost_value != std::numeric_limits<uint16_t>::max() && def->cost_table) {
        if (auto name = def->cost_table->get<uint32_t>(ip.cost_value, "Name")) {
            result += " " + nw::kernel::strings().get(*name);
        }
    }

    if (ip.param_value != std::numeric_limits<uint8_t>::max() && def->param_table) {
        if (auto name = def->param_table->get<uint32_t>(ip.param_value, "Name")) {
            result += " " + nw::kernel::strings().get(*name);
        }
    }

    return result;
}

int process_item_properties(nw::Creature* obj, const nw::Item* item, nw::EquipIndex index, bool remove)
{
    if (!obj || !item) { return 0; }

    int processed = 0;
    if (!remove) {
        for (const auto& ip : item->properties) {
            if (auto eff = nw::kernel::effects().generate(ip, index, item->baseitem)) {
                eff->creator = item->handle();
                eff->category = nw::EffectCategory::item;
                if (!apply_effect(obj, eff)) {
                    nw::kernel::effects().destroy(eff);
                }
                ++processed;
            }
        }
        return processed;
    } else {
        return remove_effects_by(obj, item->handle());
    }
}

} // namespace nw
