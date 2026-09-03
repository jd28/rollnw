#include "attributes.hpp"

#include "../kernel/Strings.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace nw {

DEFINE_RULE_TYPE(Ability);
DEFINE_RULE_TYPE(Appearance);
DEFINE_RULE_TYPE(WingModel);
DEFINE_RULE_TYPE(TailModel);
DEFINE_RULE_TYPE(PlaceableAppearance);
DEFINE_RULE_TYPE(DoorType);
DEFINE_RULE_TYPE(GenericDoor);
DEFINE_RULE_TYPE(ArmorClass);
DEFINE_RULE_TYPE(Phenotype);
DEFINE_RULE_TYPE(Race);
DEFINE_RULE_TYPE(Save);
DEFINE_RULE_TYPE(SaveVersus);
DEFINE_RULE_TYPE(Skill);

// -- AppearanceInfo ----------------------------------------------------------
// ----------------------------------------------------------------------------

String AppearanceInfo::editor_name() const
{
    auto string = nw::kernel::strings().get(string_ref);
    if (!string.empty()) { return string; }
    return label;
}

// -- CreatureAccessoryModelInfo ---------------------------------------------
// ----------------------------------------------------------------------------

String CreatureAccessoryModelInfo::editor_name() const
{
    String result = label;
    std::replace(result.begin(), result.end(), '_', ' ');
    if (result.empty()) { result = model.view(); }
    return result;
}

// -- PlaceableAppearanceInfo -------------------------------------------------
// ----------------------------------------------------------------------------

String PlaceableAppearanceInfo::editor_name() const
{
    auto string = nw::kernel::strings().get(string_ref);
    if (!string.empty() && !string.starts_with("Bad Strref")) { return string; }
    return label;
}

namespace {

String door_editor_name(uint32_t string_ref, Resref model)
{
    auto result = nw::kernel::strings().get(string_ref);
    if (result.empty() || result.starts_with("Bad Strref")) {
        result = model.view();
    }
    return result;
}

} // namespace

String DoorTypeInfo::editor_name() const
{
    return door_editor_name(string_ref, model);
}

String GenericDoorInfo::editor_name() const
{
    return door_editor_name(string_ref, model);
}

} // namespace nw
