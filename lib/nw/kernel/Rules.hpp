#pragma once

#include "../formats/TwoDA.hpp"
#include "../resources/Resource.hpp"
#include "../rules/Ability.hpp"
#include "../rules/BaseItem.hpp"
#include "../rules/Class.hpp"
#include "../rules/Feat.hpp"
#include "../rules/Race.hpp"
#include "../rules/Skill.hpp"
#include "../rules/Spell.hpp"
#include "../rules/system.hpp"
#include "Strings.hpp"

#include <absl/container/flat_hash_map.h>

#include <cstdint>
#include <limits>
#include <vector>

namespace nw::kernel {

struct Rules {
    virtual ~Rules();

    // Intializes rules system
    virtual bool initialize();

    /// Loads rules
    /// @note if fail_hard is `true` exceptions will be thrown on failure
    virtual bool load(bool fail_hard = true);

    void load_abilities(bool fail_hard = true);
    void load_baseitems(bool fail_hard = true);
    void load_classes(bool fail_hard = true);
    void load_feats(bool fail_hard = true);
    void load_races(bool fail_hard = true);
    void load_skills(bool fail_hard = true);
    void load_spells(bool fail_hard = true);

    /// Reloads rules
    /// @note if fail_hard is `true` exceptions will be thrown on failure
    virtual bool reload(bool fail_hard = true);

    /// Clears rules system of all rules and cached 2da files
    virtual void clear();

    /// Caches
    virtual bool cache_2da(const Resource& res);

    /// Gets a cached TwoDA
    virtual TwoDA& get_cached_2da(const Resource& res);

    /// Gets constant
    virtual Constant get_constant(std::string_view name) const;

    /// Gets a ability
    const Ability& ability(size_t index) const;
    /// Gets the number of loaded abilitys
    /// @note There is no 2da for this, so it's hardcoded to default NWN.
    size_t ability_count() const noexcept;

    /// Gets a baseitem
    const BaseItem& baseitem(size_t index);
    /// Gets the number of loaded baseitems
    /// @note Depending on baseitems.2da, some may be invalid.
    size_t baseitem_count() const noexcept;

    /// Gets a class
    const Class& classes(size_t index);
    /// Gets the number of loaded classs
    /// @note Depending on classes.2da, some may be invalid.
    size_t class_count() const noexcept;

    /// Gets a feat
    const Feat& feat(size_t index);
    /// Gets the number of loaded feats
    /// @note Depending on feat.2da, some may be invalid.
    size_t feat_count() const noexcept;

    /// Gets a race
    const Race& race(size_t index);
    /// Gets the number of loaded races
    /// @note Depending on racialtypes.2da, some may be invalid.
    size_t race_count() const noexcept;

    /// Registers constant
    /// @note If constant by name is already registered, it will return that.
    virtual Constant register_constant(std::string_view name, int value);

    /// Gets a skill
    const Skill& skill(size_t index);
    /// Gets the number of loaded skills
    /// @note Depending on skills.2da, some may be invalid.
    size_t skill_count() const noexcept;

    /// Gets a spell
    const Spell& spell(size_t index);
    /// Gets the number of loaded spells
    /// @note Depending on spells.2da, some may be invalid.
    size_t spell_count() const noexcept;

private:
    std::vector<Ability> ability_info_;
    std::vector<BaseItem> baseitem_info_;
    std::vector<Class> class_info_;
    std::vector<Feat> feat_info_;
    std::vector<Race> race_info_;
    std::vector<Skill> skill_info_;
    std::vector<Spell> spell_info_;
    absl::flat_hash_map<Resource, TwoDA> cached_2das_;
    absl::flat_hash_map<InternedString, RuleVariant, InternedStringHash, InternedStringEq> constants_;
};

} // namespace nw::kernel
