#pragma once

#include "../objects/ObjectBase.hpp"
#include "../rules/attributes.hpp"
#include "../rules/creature_body_parts.hpp"
#include "../rules/items.hpp"
#include "../rules/system.hpp"
#include "../util/FixedVector.hpp"

#include <cstddef>
#include <cstdint>

namespace nw::kernel {

struct Rules : public Service {
    const static std::type_index type_index;
    using QualifierMatcher = bool (*)(const Qualifier&, const ObjectBase*);

    Rules(MemoryResource* memory);
    virtual ~Rules() = default;

    /// Initializes rules system
    virtual void initialize(ServiceInitTime time) override;

    /// Match
    bool match(const Qualifier& qual, const ObjectBase* obj) const;

    /// Gets maximum spell levels
    size_t maximum_spell_levels() const noexcept { return maximum_spell_levels_; }

    /// Publishes the validated skill domain size used by creature GFF padding.
    bool publish_skill_count(int32_t count) noexcept;

    /// Gets the active skill domain size.
    size_t skill_count() const noexcept { return skill_count_; }

    /// Meets requirements
    bool meets_requirement(const Requirement& req, const ObjectBase* obj) const;

    /// Sets the active profile qualifier matcher.
    void set_qualifier_matcher(QualifierMatcher matcher) noexcept;

    /// Get service stats
    nlohmann::json stats() const override;

    AppearanceArray appearances;
    WingModelArray wingmodels;
    TailModelArray tailmodels;
    CreatureBodyPartCatalog creature_body_parts;
    BaseItemArray baseitems;
    PlaceableAppearanceArray placeables;
    DoorTypeArray doortypes;
    GenericDoorArray genericdoors;

private:
    QualifierMatcher qualifier_matcher_ = nullptr;
    size_t maximum_spell_levels_ = 10;
    size_t skill_count_ = 0;
};

inline Rules& rules()
{
    auto res = services().get_mut<Rules>();
    if (!res) {
        throw std::runtime_error("kernel: unable to load rules service");
    }
    return *res;
}

} // namespace nw::kernel
