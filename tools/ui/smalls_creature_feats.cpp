#include "smalls_creature_feats.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Strings.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/smalls/Array.hpp>
#include <nw/smalls/runtime.hpp>

#include <algorithm>
#include <limits>

namespace nw::toolset {
namespace {

unsigned char fold_ascii(unsigned char value) noexcept
{
    if (value >= 'A' && value <= 'Z') {
        return static_cast<unsigned char>(value + ('a' - 'A'));
    }
    return value;
}

bool contains_casefold_ascii(std::string_view haystack, std::string_view needle)
{
    if (needle.empty()) {
        return true;
    }
    return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
               [](unsigned char lhs, unsigned char rhs) {
                   return fold_ascii(lhs) == fold_ascii(rhs);
               })
        != haystack.end();
}

int compare_casefold_ascii(std::string_view lhs, std::string_view rhs) noexcept
{
    const size_t count = std::min(lhs.size(), rhs.size());
    for (size_t index = 0; index < count; ++index) {
        const auto left = fold_ascii(static_cast<unsigned char>(lhs[index]));
        const auto right = fold_ascii(static_cast<unsigned char>(rhs[index]));
        if (left != right) {
            return left < right ? -1 : 1;
        }
    }
    if (lhs.size() == rhs.size()) {
        return 0;
    }
    return lhs.size() < rhs.size() ? -1 : 1;
}

CreatureFeatTextSlice append_text(std::string_view value, std::string& output)
{
    if (output.size() >= std::numeric_limits<uint32_t>::max()) {
        return {};
    }
    const size_t available = std::numeric_limits<uint32_t>::max() - output.size();
    const size_t length = std::min(value.size(), available);
    const auto offset = static_cast<uint32_t>(output.size());
    output.append(value.data(), length);
    return {offset, static_cast<uint32_t>(length)};
}

bool read_assigned_feats(smalls::Runtime& runtime,
    ObjectHandle object,
    std::vector<uint32_t>& assigned,
    std::string& diagnostic)
{
    const auto stats_type = runtime.type_id("nwn1.propsets.CreatureStats", false);
    const auto* definition = runtime.get_struct_def(stats_type);
    const uint32_t feats_field = definition ? definition->field_index("feats") : UINT32_MAX;
    if (!definition || feats_field == UINT32_MAX || !definition->fields[feats_field].is_unmanaged_array) {
        diagnostic = "CreatureStats.feats schema unavailable";
        return false;
    }

    const auto stats = runtime.find_propset_ref(stats_type, object);
    if (stats.type_id == smalls::invalid_type_id) {
        diagnostic = "CreatureStats propset is not instantiated";
        return false;
    }

    const auto& field = definition->fields[feats_field];
    const auto value = runtime.read_value_field_at_offset(stats, field.offset, field.type_id);
    const auto handle = TypedHandle::from_ull(value.data.handle);
    const auto* array = runtime.object_pool().get_unmanaged_array(handle);
    if (!array) {
        diagnostic = "CreatureStats.feats storage unavailable";
        return false;
    }
    if (array->size() > std::numeric_limits<uint32_t>::max()) {
        diagnostic = "CreatureStats.feats exceeds the snapshot count range";
        return false;
    }

    assigned.reserve(array->size());
    uint32_t previous = 0;
    for (size_t i = 0; i < array->size(); ++i) {
        smalls::Value entry;
        if (!array->get_value(i, entry, runtime) || entry.type_id != runtime.int_type() || entry.data.ival < 0) {
            diagnostic = "CreatureStats.feats contains an invalid entry";
            return false;
        }
        const auto feat_id = static_cast<uint32_t>(entry.data.ival);
        if (i > 0 && feat_id <= previous) {
            diagnostic = "CreatureStats.feats is not sorted and unique";
            return false;
        }
        assigned.push_back(feat_id);
        previous = feat_id;
    }
    return true;
}

bool read_int_field(smalls::Runtime& runtime,
    const smalls::Value& row,
    std::string_view field,
    int32_t& output)
{
    if (row.storage != smalls::ValueStorage::heap || row.data.hptr.value == 0) {
        return false;
    }
    const auto value = runtime.read_struct_field(
        row.data.hptr, row.type_id, field);
    if (value.type_id != runtime.int_type()) { return false; }
    output = value.data.ival;
    return true;
}

} // namespace

std::string_view CreatureFeatViewSnapshot::text_view(CreatureFeatTextSlice slice) const noexcept
{
    if (slice.offset > text.size() || slice.length > text.size() - slice.offset) {
        return {};
    }
    return std::string_view{text}.substr(slice.offset, slice.length);
}

void build_creature_feat_rows(smalls::Runtime& runtime,
    ObjectHandle active_object,
    std::string_view query,
    CreatureFeatViewSnapshot& output)
{
    output = {};
    output.object = active_object;
    if (active_object.type != ObjectType::creature
        || !kernel::objects().get_object_base(active_object)) {
        output.status = CreatureFeatViewStatus::invalid_object;
        output.diagnostic = "Active object is not a live Creature";
        return;
    }

    std::vector<uint32_t> assigned;
    if (!read_assigned_feats(runtime, active_object, assigned, output.diagnostic)) {
        output.status = CreatureFeatViewStatus::invalid_data;
        return;
    }

    const auto editor_rows = runtime.execute_script(
        "nwn1.feats", "editor_rows", {});
    auto* entries = editor_rows.ok()
        ? runtime.get_array_typed(editor_rows.value.data.hptr)
        : nullptr;
    constexpr size_t max_feat_entries = static_cast<size_t>(std::numeric_limits<int32_t>::max());
    if (!entries || entries->size() > max_feat_entries) {
        output.status = CreatureFeatViewStatus::invalid_data;
        output.diagnostic = "Smalls feat editor rows are unavailable";
        return;
    }
    output.rows.reserve(entries->size());
    size_t assigned_index = 0;
    int32_t previous_feat_id = -1;
    for (size_t index = 0; index < entries->size(); ++index) {
        smalls::Value row;
        int32_t feat_id = -1;
        int32_t name_strref = -1;
        if (!entries->get_value(index, row, runtime)
            || !read_int_field(runtime, row, "id", feat_id)
            || !read_int_field(runtime, row, "name_strref", name_strref)
            || feat_id <= previous_feat_id || name_strref < 0) {
            output = {};
            output.object = active_object;
            output.status = CreatureFeatViewStatus::invalid_data;
            output.diagnostic = "Smalls feat editor rows contain invalid data";
            return;
        }
        previous_feat_id = feat_id;

        std::string name = kernel::strings().get(
            static_cast<uint32_t>(name_strref));
        if (name.empty() || name.starts_with("Bad Strref")) {
            name = "Feat " + std::to_string(feat_id);
        }
        if (!contains_casefold_ascii(name, query)) {
            continue;
        }

        const auto feat_index = static_cast<uint32_t>(feat_id);
        while (assigned_index < assigned.size()
            && assigned[assigned_index] < feat_index) {
            ++assigned_index;
        }
        const bool is_assigned = assigned_index < assigned.size() && assigned[assigned_index] == feat_index;
        output.rows.push_back({
            feat_index,
            append_text(name, output.text),
            is_assigned,
        });
    }

    std::sort(output.rows.begin(), output.rows.end(), [&output](const auto& lhs, const auto& rhs) {
        const auto left = output.text_view(lhs.name);
        const auto right = output.text_view(rhs.name);
        if (left.empty() != right.empty()) {
            return !left.empty();
        }
        const int name_order = compare_casefold_ascii(left, right);
        return name_order == 0 ? lhs.feat_id < rhs.feat_id : name_order < 0;
    });

    output.assigned_count = static_cast<uint32_t>(assigned.size());
    output.status = CreatureFeatViewStatus::ready;
}

} // namespace nw::toolset
