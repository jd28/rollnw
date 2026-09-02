#pragma once

#include "../config.hpp"
#include "../kernel/Memory.hpp"
#include "../resources/assets.hpp"

#include <cstdint>
#include <span>

namespace nw {

enum CreatureBodyPartInfoFlag : uint32_t {
    creature_body_part_info_flag_none = 0,
    creature_body_part_info_flag_editor_visible = 1 << 0,
    creature_body_part_info_flag_robe = 1 << 1,
};

enum CreatureBodyPartOptionFlag : uint32_t {
    creature_body_part_option_flag_none = 0,
    creature_body_part_option_flag_empty = 1 << 0,
    creature_body_part_option_flag_mirror = 1 << 1,
    creature_body_part_option_flag_armor = 1 << 2,
};

struct CreatureBodyPartSet {
    int32_t assembly_id = -1;
    uint32_t part_offset = 0;
    uint32_t part_count = 0;
};

struct CreatureBodyPartInfo {
    int32_t part_id = -1;
    int32_t mirror_part_id = -1;
    uint32_t option_offset = 0;
    uint32_t option_count = 0;
    uint32_t flags = creature_body_part_info_flag_none;
    Resref anchor;
    String label;
};

struct CreatureBodyPartOption {
    int32_t part_id = -1;
    int32_t option_id = -1;
    uint32_t flags = creature_body_part_option_flag_none;
    Resref model;
};

// One selected profile owns one immutable catalog. Publication validates the
// complete batch before replacing live storage; failed publication leaves the
// previous catalog unchanged. Unknown IDs return empty spans or null rows.
struct CreatureBodyPartCatalog {
    explicit CreatureBodyPartCatalog(
        MemoryResource* allocator = kernel::global_allocator());

    [[nodiscard]] bool publish(
        std::span<const CreatureBodyPartSet> sets,
        std::span<const CreatureBodyPartInfo> parts,
        std::span<const CreatureBodyPartOption> options,
        String& diagnostic);

    [[nodiscard]] std::span<const CreatureBodyPartInfo> parts(
        int32_t assembly_id) const noexcept;
    [[nodiscard]] std::span<const CreatureBodyPartOption> options(
        int32_t assembly_id, int32_t part_id) const noexcept;
    [[nodiscard]] const CreatureBodyPartInfo* part(
        int32_t assembly_id, int32_t part_id) const noexcept;
    [[nodiscard]] const CreatureBodyPartOption* option(
        int32_t assembly_id, int32_t part_id, int32_t option_id) const noexcept;

    [[nodiscard]] size_t data_bytes() const noexcept;

private:
    PVector<CreatureBodyPartSet> sets;
    PVector<CreatureBodyPartInfo> part_rows;
    PVector<CreatureBodyPartOption> option_rows;
};

} // namespace nw
