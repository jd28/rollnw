#include "creature_body_parts.hpp"

#include <algorithm>
#include <limits>
#include <vector>

namespace nw {
namespace {

template <typename Rows>
bool bounded_span(uint32_t offset, uint32_t count, const Rows& rows) noexcept
{
    return offset <= rows.size() && count <= rows.size() - offset;
}

bool validate_catalog(
    std::span<const CreatureBodyPartSet> sets,
    std::span<const CreatureBodyPartInfo> parts,
    std::span<const CreatureBodyPartOption> options,
    String& diagnostic)
{
    if (sets.size() > std::numeric_limits<uint32_t>::max()
        || parts.size() > std::numeric_limits<uint32_t>::max()
        || options.size() > std::numeric_limits<uint32_t>::max()) {
        diagnostic = "Creature body-part catalog exceeds 32-bit span bounds";
        return false;
    }

    std::vector<uint8_t> claimed_parts(parts.size());
    std::vector<uint8_t> claimed_options(options.size());
    for (size_t set_index = 0; set_index < sets.size(); ++set_index) {
        const auto& set = sets[set_index];
        if (set.assembly_id < 0
            || !bounded_span(set.part_offset, set.part_count, parts)) {
            diagnostic = "Creature body-part catalog contains an invalid assembly span";
            return false;
        }
        for (size_t previous = 0; previous < set_index; ++previous) {
            if (sets[previous].assembly_id == set.assembly_id) {
                diagnostic = "Creature body-part catalog contains a duplicate assembly ID";
                return false;
            }
        }

        const auto assembly_parts = parts.subspan(set.part_offset, set.part_count);
        for (size_t part_index = 0; part_index < assembly_parts.size(); ++part_index) {
            const size_t absolute_part = set.part_offset + part_index;
            if (claimed_parts[absolute_part] != 0) {
                diagnostic = "Creature body-part catalog contains overlapping part spans";
                return false;
            }
            claimed_parts[absolute_part] = 1;

            const auto& part = assembly_parts[part_index];
            if (part.part_id < 0
                || part.mirror_part_id < -1
                || !bounded_span(part.option_offset, part.option_count, options)) {
                diagnostic = "Creature body-part catalog contains an invalid part row";
                return false;
            }
            for (size_t previous = 0; previous < part_index; ++previous) {
                if (assembly_parts[previous].part_id == part.part_id) {
                    diagnostic = "Creature body-part catalog contains a duplicate part ID";
                    return false;
                }
            }
            if (part.mirror_part_id >= 0
                && std::ranges::find(assembly_parts, part.mirror_part_id,
                       &CreatureBodyPartInfo::part_id)
                    == assembly_parts.end()) {
                diagnostic = "Creature body-part catalog references an unknown mirror part";
                return false;
            }

            const auto part_options = options.subspan(
                part.option_offset, part.option_count);
            for (size_t option_index = 0; option_index < part_options.size(); ++option_index) {
                const size_t absolute_option = part.option_offset + option_index;
                if (claimed_options[absolute_option] != 0) {
                    diagnostic = "Creature body-part catalog contains overlapping option spans";
                    return false;
                }
                claimed_options[absolute_option] = 1;

                const auto& option = part_options[option_index];
                if (option.part_id != part.part_id || option.option_id < 0) {
                    diagnostic = "Creature body-part catalog contains an option for an unknown part";
                    return false;
                }
                for (size_t previous = 0; previous < option_index; ++previous) {
                    if (part_options[previous].option_id == option.option_id) {
                        diagnostic = "Creature body-part catalog contains a duplicate option ID";
                        return false;
                    }
                }
            }
        }
    }

    if (std::ranges::find(claimed_parts, uint8_t{0}) != claimed_parts.end()
        || std::ranges::find(claimed_options, uint8_t{0}) != claimed_options.end()) {
        diagnostic = "Creature body-part catalog contains unowned payload rows";
        return false;
    }
    return true;
}

} // namespace

CreatureBodyPartCatalog::CreatureBodyPartCatalog(MemoryResource* allocator)
    : sets{allocator}
    , part_rows{allocator}
    , option_rows{allocator}
{
}

bool CreatureBodyPartCatalog::publish(
    std::span<const CreatureBodyPartSet> new_sets,
    std::span<const CreatureBodyPartInfo> new_parts,
    std::span<const CreatureBodyPartOption> new_options,
    String& diagnostic)
{
    diagnostic.clear();
    if (!validate_catalog(new_sets, new_parts, new_options, diagnostic)) {
        return false;
    }

    PVector<CreatureBodyPartSet> published_sets{sets.get_allocator()};
    PVector<CreatureBodyPartInfo> published_parts{part_rows.get_allocator()};
    PVector<CreatureBodyPartOption> published_options{option_rows.get_allocator()};
    published_sets.assign(new_sets.begin(), new_sets.end());
    published_parts.assign(new_parts.begin(), new_parts.end());
    published_options.assign(new_options.begin(), new_options.end());
    sets.swap(published_sets);
    part_rows.swap(published_parts);
    option_rows.swap(published_options);
    return true;
}

std::span<const CreatureBodyPartInfo> CreatureBodyPartCatalog::parts(
    int32_t assembly_id) const noexcept
{
    const auto found = std::ranges::find(
        sets, assembly_id, &CreatureBodyPartSet::assembly_id);
    if (found == sets.end()
        || !bounded_span(found->part_offset, found->part_count, part_rows)) {
        return {};
    }
    return {part_rows.data() + found->part_offset, found->part_count};
}

std::span<const CreatureBodyPartOption> CreatureBodyPartCatalog::options(
    int32_t assembly_id, int32_t part_id) const noexcept
{
    const auto* found = part(assembly_id, part_id);
    if (!found
        || !bounded_span(found->option_offset, found->option_count, option_rows)) {
        return {};
    }
    return {option_rows.data() + found->option_offset, found->option_count};
}

const CreatureBodyPartInfo* CreatureBodyPartCatalog::part(
    int32_t assembly_id, int32_t part_id) const noexcept
{
    const auto assembly_parts = parts(assembly_id);
    const auto found = std::ranges::find(
        assembly_parts, part_id, &CreatureBodyPartInfo::part_id);
    return found == assembly_parts.end() ? nullptr : &*found;
}

const CreatureBodyPartOption* CreatureBodyPartCatalog::option(
    int32_t assembly_id, int32_t part_id, int32_t option_id) const noexcept
{
    const auto part_options = options(assembly_id, part_id);
    const auto found = std::ranges::find(
        part_options, option_id, &CreatureBodyPartOption::option_id);
    return found == part_options.end() ? nullptr : &*found;
}

size_t CreatureBodyPartCatalog::data_bytes() const noexcept
{
    size_t result = sets.capacity() * sizeof(CreatureBodyPartSet)
        + part_rows.capacity() * sizeof(CreatureBodyPartInfo)
        + option_rows.capacity() * sizeof(CreatureBodyPartOption);
    for (const auto& part : part_rows) {
        result += part.label.capacity();
    }
    return result;
}

} // namespace nw
