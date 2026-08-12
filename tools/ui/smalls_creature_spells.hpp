#pragma once

#include <nw/objects/ObjectHandle.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nw::smalls {
struct Runtime;
}

namespace nw::toolset {

struct CreatureSpellTextSlice {
    uint32_t offset = 0;
    uint32_t length = 0;
};

struct CreatureSpellChoice {
    int32_t value = -1;
    CreatureSpellTextSlice name;
    bool memorizes = false;
};

struct CreatureSpellRow {
    int32_t spell_id = -1;
    int32_t level = -1;
    int32_t uses = 0;
    CreatureSpellTextSlice name;
    bool known = false;
};

enum class CreatureSpellViewStatus : uint8_t {
    empty,
    ready,
    invalid_object,
    invalid_data,
};

struct CreatureSpellViewSnapshot {
    ObjectHandle object{};
    CreatureSpellViewStatus status = CreatureSpellViewStatus::empty;
    std::vector<CreatureSpellChoice> classes;
    std::vector<CreatureSpellChoice> metamagic;
    std::vector<CreatureSpellRow> rows;
    std::string text;
    std::string diagnostic;
    int32_t selected_class = -1;
    int32_t selected_metamagic = 0;
    bool memorizes = false;

    [[nodiscard]] std::string_view text_view(CreatureSpellTextSlice slice) const noexcept;
};

// Builds one presentation snapshot from Smalls-owned spell editor row batches.
// Negative selections choose the first available class and metamagic option.
void build_creature_spell_rows(smalls::Runtime& runtime,
    ObjectHandle active_object,
    int32_t selected_class,
    int32_t selected_metamagic,
    CreatureSpellViewSnapshot& output);

// Replaces output with stable indices into snapshot.rows. Spell levels outside
// [-1, 9] reject the filter and produce no rows; -1 includes every tier.
void filter_creature_spell_rows(const CreatureSpellViewSnapshot& snapshot,
    std::string_view query,
    int32_t spell_level,
    std::vector<uint32_t>& output);

} // namespace nw::toolset
