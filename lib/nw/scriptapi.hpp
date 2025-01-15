#pragma once

#include "config.hpp"

#include <stdint.h>

/**
 * @defgroup Python Functions with Python bindings
 * @brief Functions that are accessible via Python bindings.
 */

namespace nw {

struct Creature;
struct Effect;
struct EffectType;
struct Feat;
struct Item;
struct ItemProperty;
struct ItemPropertyType;
struct ObjectBase;
struct ObjectHandle;
enum struct ObjectType : uint8_t;
struct Resref;

// ============================================================================
// == Object: Life Time =======================================================
// ============================================================================

/// Creates an object. Returns invalid object on failure.
///
/// @ingroup Python
/// @since 0.46
ObjectBase* create_object(Resref resref, ObjectType type);

/// Determines if object is valid
///
/// @since 0.46
bool is_valid_object(ObjectHandle obj);

/// Destroys an object, if valid.
///
/// @ingroup Python
/// @since 0.46
void destroy_object(ObjectBase* obj);

// ============================================================================
// == Object: Effects =========================================================
// ============================================================================

/// Applies an effect to an object
bool apply_effect(nw::ObjectBase* obj, nw::Effect* effect);

/// Determines if an effect type is applied to an object
bool has_effect_applied(nw::ObjectBase* obj, nw::EffectType type, int subtype = -1);

/// Removes an effect from an object
bool remove_effect(nw::ObjectBase* obj, nw::Effect* effect, bool destroy = true);

/// Removes effects by creator
int remove_effects_by(nw::ObjectBase* obj, nw::ObjectHandle creator);

// ============================================================================
// == Creature: Feats =========================================================
// ============================================================================

/// Counts the number of known feats in the range [start, end]
int count_feats_in_range(const nw::Creature* obj, nw::Feat start, nw::Feat end);

/// Gets all feats for which requirements are met
/// @note This is not yet very useful until a level up parameter is added.
Vector<nw::Feat> get_all_available_feats(const nw::Creature* obj);

/// Gets the highest known successor feat
std::pair<nw::Feat, int> has_feat_successor(const nw::Creature* obj, nw::Feat feat);

/// Gets the highest known feat in range [start, end]
nw::Feat highest_feat_in_range(const nw::Creature* obj, nw::Feat start, nw::Feat end);

/// Checks if an entity knows a given feat
bool knows_feat(const nw::Creature* obj, nw::Feat feat);

// ============================================================================
// == Item: Properties ========================================================
// ============================================================================

/// Determines if item has a particular item property
bool item_has_property(const Item* item, ItemPropertyType type, int32_t subtype = -1);

/// Converts item property to in-game style string
String itemprop_to_string(const ItemProperty& ip);

} // namespace nw
