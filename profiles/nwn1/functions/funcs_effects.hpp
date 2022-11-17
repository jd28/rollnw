#pragma once

#include <nw/components/ObjectBase.hpp>
#include <nw/rules/Effect.hpp>

#include <functional>
#include <iterator>

namespace nwn1 {

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
 * @param cb A user defined callback that will be passed an applicable effect's value.
 * @param extractor A function that extracts the value from a particular effect.
 * @param comparator A function taking two ``T`` values and returns ``true`` if the first is greater
 *        (Default std::greater<T>)
 * @return Number of effects processed
 */
template <typename T, typename It, typename CallBack, typename Extractor, typename Comp = std::greater<T>>
It resolve_effects_of(It begin, It end, nw::EffectType type, int subtype,
    CallBack cb, Extractor extractor, Comp comparator = std::greater<T>{}) noexcept
{
    if (type == nw::EffectType::invalid()) { return end; }
    auto it = find_first_effect_of(begin, end, type, subtype);

    while (it != end && it->type == type && it->subtype == subtype) {
        if (it->creator.type == nw::ObjectType::item) {
            auto item = it->creator;
            T value = extractor(*it);
            while (it != end && it->type == type
                && it->subtype == subtype
                && it->creator == item) {
                T next = extractor(*it);
                value = comparator(value, next) ? value : next;
                ++it;
            }
            cb(value);
        } else if (it->spell_id != nw::Spell::invalid()) {
            auto spell = it->spell_id;
            T value = extractor(*it);
            while (it != end && it->type == type
                && it->subtype == subtype
                && it->spell_id == spell) {
                T next = extractor(*it);
                value = comparator(value, next) ? value : next;
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

} // namespace nwn1
