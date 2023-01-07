#pragma once

#include "objects/EffectArray.hpp"
#include "rules/Effect.hpp"

#include <cstdint>

namespace nw {

// == Forward Decls ===========================================================
// ============================================================================

struct Creature;
struct ObjectBase;
struct Feat;
struct Item;
struct ItemProperty;
struct ItemPropertyType;
enum struct EquipIndex : uint32_t;

// == Effects =================================================================
// ============================================================================

/// Applies an effect to an object
bool apply_effect(nw::ObjectBase* obj, nw::Effect* effect);

/// Extracts the 0th integer value from an effect
int effect_extract_int0(const nw::EffectHandle& effect);

/**
 * @brief Finds first effect of a given type
 *
 * @tparam It A forward iterator
 * @param begin Beginning of an range of effects
 * @param end Beginning of an range of effects
 * @param type An effect_type_* constant
 * @param subtype An effect subtype
 * @return It iterator to the first effect, or ``end``
 */
template <typename It>
It find_first_effect_of(It begin, It end, nw::EffectType type, int subtype = -1)
{
    nw::EffectHandle needle;
    needle.type = type;
    needle.subtype = subtype;

    auto comp = [](const nw::EffectHandle& h, const nw::EffectHandle& n) {
        return std::tie(h.type, h.subtype) < std::tie(n.type, n.subtype);
    };

    return std::lower_bound(begin, end, needle, comp);
}

/// Determines if an effect type is applied to an object
bool has_effect_applied(nw::ObjectBase* obj, nw::EffectType type, int subtype = -1);

/// Removes an effect from an object
bool remove_effect(nw::ObjectBase* obj, nw::Effect* effect, bool destroy = true);

/// Removes effects by creator
int remove_effects_by(nw::ObjectBase* obj, nw::ObjectHandle creator);

/**
 * @brief Finds all applicable effects of a given type / subtype.
 *
 * Applicable effects are passed to a user supplied callback.
 *
 * @tparam T An arbitrary type that can be held in an effect, e.g. a simple integer, a damage roll, etc.
 * @tparam It An iterator type
 * @tparam CallBack A function with the signature void(T) supplied by the user
 * @tparam Extractor A function that extracts a value of type ``T`` from an EffectHandle
 * @tparam Comp A comparator taking two ``T`` values and returns ``true`` if the first is greater
 *         (Default std::greater<T>)
 * @param begin Start of a range of effect handles of a type/subtype
 * @param end End range of effect handles
 * @param type An effect_type_* constant
 * @param subtype A effect subtype, if no subtype -1 should be passed
 * @param vs Versus struct
 * @param cb A user defined callback that will be passed an applicable effect's value.
 * @param extractor A function that extracts the value from a particular effect.
 * @param comparator A function taking two ``T`` values and returns ``true`` if the first is greater
 *        (Default std::greater<T>)
 * @return iterator to passed last processed effect
 */
template <typename T, typename It, typename CallBack, typename Extractor, typename Comp = std::greater<T>>
It resolve_effects_of(It begin, It end, nw::EffectType type, int subtype, Versus vs,
    CallBack cb, Extractor extractor, Comp comparator = std::greater<T>{}) noexcept
{
    if (type == nw::EffectType::invalid()) { return end; }
    auto it = find_first_effect_of(begin, end, type, subtype);

    while (it != end && it->type == type && it->subtype == subtype) {
        if (!it->effect->versus().match(vs)) {
            ++it;
            continue;
        }
        if (it->creator.type == nw::ObjectType::item) {
            auto item = it->creator;
            T value = extractor(*it);
            ++it;
            while (it != end && it->type == type
                && it->subtype == subtype
                && it->creator == item) {
                if (it->effect->versus().match(vs)) {
                    T next = extractor(*it);
                    value = comparator(value, next) ? value : next;
                }
                ++it;
            }
            cb(value);
        } else if (it->spell_id != nw::Spell::invalid()) {
            auto spell = it->spell_id;
            T value = extractor(*it);
            ++it;
            while (it != end && it->type == type
                && it->subtype == subtype
                && it->spell_id == spell) {
                if (it->effect->versus().match(vs)) {
                    T next = extractor(*it);
                    value = comparator(value, next) ? value : next;
                }
                ++it;
            }
            cb(value);
        } else {
            cb(extractor(*it));
            ++it;
        }
    }
    return it;
}

/**
 * @brief Finds all applicable effects of a given type / subtype.
 *
 * Applicable effects are passed to a user supplied callback.
 *
 * @tparam T An arbitrary type that can be held in an effect, e.g. a simple integer, a damage roll, etc.
 * @tparam It An iterator type
 * @tparam Extractor A function that extracts a value of type ``T`` from an EffectHandle
 * @param begin Start of a range of effect handles of a type/subtype
 * @param end End range of effect handles
 * @param type An effect_type_* constant
 * @param subtype A effect subtype, if no subtype -1 should be passed
 * @param vs Versus struct
 * @param extractor A function that extracts the value from a particular effect.
 * @return (result, iterator)
 */
template <typename T, typename It, typename Extractor = decltype(&effect_extract_int0)>
std::pair<T, It> max_effects_of(It begin, It end, nw::EffectType type, int subtype, Versus vs = {},
    Extractor extractor = &effect_extract_int0) noexcept
{
    T result{};
    auto maxer = [&result](T value) {
        result = std::max(result, value);
    };
    auto it = resolve_effects_of<T>(begin, end, type, subtype, vs, maxer, extractor);
    return std::make_pair(result, it);
}

/**
 * @brief Finds all applicable effects of a given type / subtype.
 *
 * Applicable effects are passed to a user supplied callback.
 *
 * @tparam T An arbitrary type that can be held in an effect, e.g. a simple integer, a damage roll, etc.
 * @tparam It An iterator type
 * @tparam Extractor A function that extracts a value of type ``T`` from an EffectHandle
 * @tparam Comp A comparator taking two ``T`` values and returns ``true`` if the first is greater
 *         (Default std::greater<T>)
 * @param begin Start of a range of effect handles of a type/subtype
 * @param end End range of effect handles
 * @param type An effect_type_* constant
 * @param subtype A effect subtype, if no subtype -1 should be passed
 * @param vs Versus struct
 * @param extractor A function that extracts the value from a particular effect.
 * @param comparator A function taking two ``T`` values and returns ``true`` if the first is greater
 *        (Default std::greater<T>)
 * @return (result, iterator)
 */
template <typename T, typename It, typename Extractor = decltype(&effect_extract_int0), typename Comp = std::greater<T>>
std::pair<T, It> sum_effects_of(It begin, It end, nw::EffectType type, int subtype, Versus vs = {},
    Extractor extractor = &effect_extract_int0, Comp comparator = std::greater<T>{}) noexcept
{
    T result{};
    auto summer = [&result](T value) { result += value; };
    auto it = resolve_effects_of<T>(begin, end, type, subtype, vs, summer, extractor, comparator);
    return std::make_pair(result, it);
}

// == Feats ===================================================================
// ============================================================================

/// Counts the number of known feats in the range [start, end]
int count_feats_in_range(const nw::Creature* obj, nw::Feat start, nw::Feat end);

/// Gets all feats for which requirements are met
/// @note This is not yet very useful until a level up parameter is added.
std::vector<nw::Feat> get_all_available_feats(const nw::Creature* obj);

/// Gets the highest known successor feat
std::pair<nw::Feat, int> has_feat_successor(const nw::Creature* obj, nw::Feat feat);

/// Gets the highest known feat in range [start, end]
nw::Feat highest_feat_in_range(const nw::Creature* obj, nw::Feat start, nw::Feat end);

/// Checks if an entity knows a given feat
bool knows_feat(const nw::Creature* obj, nw::Feat feat);

// == Item Properties =========================================================
// ============================================================================

/// Determines if item has a particular item property
bool item_has_property(const Item* item, ItemPropertyType type, int32_t subtype = -1);

/// Converts item property to in-game style string
std::string itemprop_to_string(const nw::ItemProperty& ip);

/// Processes item properties and applies resulting effects to creature
int process_item_properties(nw::Creature* obj, const nw::Item* item, nw::EquipIndex index, bool remove);

} // namespace nw
