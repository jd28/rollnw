#include "smalls_creature_spells.hpp"

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

CreatureSpellTextSlice append_text(std::string_view value, std::string& output)
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

bool read_int_field(smalls::Runtime& runtime,
    const smalls::Value& row,
    std::string_view field,
    int32_t& output)
{
    if (row.storage != smalls::ValueStorage::heap || row.data.hptr.value == 0) {
        return false;
    }
    const auto value = runtime.read_struct_field(row.data.hptr, row.type_id, field);
    if (value.type_id != runtime.int_type()) {
        return false;
    }
    output = value.data.ival;
    return true;
}

bool read_bool_field(smalls::Runtime& runtime,
    const smalls::Value& row,
    std::string_view field,
    bool& output)
{
    if (row.storage != smalls::ValueStorage::heap || row.data.hptr.value == 0) {
        return false;
    }
    const auto value = runtime.read_struct_field(row.data.hptr, row.type_id, field);
    if (value.type_id != runtime.bool_type()) {
        return false;
    }
    output = value.data.bval;
    return true;
}

std::string localized_name(int32_t strref, std::string_view fallback, int32_t value)
{
    std::string result;
    if (strref >= 0) {
        result = kernel::strings().get(static_cast<uint32_t>(strref));
    }
    if (result.empty() || result.starts_with("Bad Strref")) {
        result = std::string{fallback} + " " + std::to_string(value);
    }
    return result;
}

smalls::Value object_value(smalls::Runtime& runtime, ObjectHandle object)
{
    smalls::Value result = smalls::Value::make_object(object);
    result.type_id = runtime.object_subtype_for_tag(object.type);
    return result;
}

bool copy_class_rows(smalls::Runtime& runtime,
    const smalls::ExecutionResult& result,
    CreatureSpellViewSnapshot& output)
{
    auto* array = result.ok() ? runtime.get_array_typed(result.value.data.hptr) : nullptr;
    if (!array || array->size() > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
        return false;
    }

    output.classes.reserve(array->size());
    for (size_t index = 0; index < array->size(); ++index) {
        smalls::Value value;
        int32_t class_id = -1;
        int32_t strref = -1;
        bool memorizes = false;
        if (!array->get_value(index, value, runtime)
            || !read_int_field(runtime, value, "class_id", class_id)
            || !read_int_field(runtime, value, "name_strref", strref)
            || !read_bool_field(runtime, value, "memorizes", memorizes)
            || class_id < 0) {
            return false;
        }
        const std::string name = localized_name(strref, "Class", class_id);
        output.classes.push_back({class_id, append_text(name, output.text), memorizes});
    }
    return true;
}

bool copy_metamagic_rows(smalls::Runtime& runtime,
    const smalls::ExecutionResult& result,
    CreatureSpellViewSnapshot& output)
{
    auto* array = result.ok() ? runtime.get_array_typed(result.value.data.hptr) : nullptr;
    if (!array || array->size() > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
        return false;
    }

    output.metamagic.reserve(array->size());
    for (size_t index = 0; index < array->size(); ++index) {
        smalls::Value value;
        int32_t code = -1;
        int32_t strref = -1;
        if (!array->get_value(index, value, runtime)
            || !read_int_field(runtime, value, "code", code)
            || !read_int_field(runtime, value, "name_strref", strref)
            || code < 0) {
            return false;
        }
        const std::string name = localized_name(strref, "Metamagic", code);
        output.metamagic.push_back({code, append_text(name, output.text), false});
    }
    return true;
}

bool copy_spell_rows(smalls::Runtime& runtime,
    const smalls::ExecutionResult& result,
    CreatureSpellViewSnapshot& output)
{
    auto* array = result.ok() ? runtime.get_array_typed(result.value.data.hptr) : nullptr;
    if (!array || array->size() > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
        return false;
    }

    output.rows.reserve(array->size());
    for (size_t index = 0; index < array->size(); ++index) {
        smalls::Value value;
        CreatureSpellRow row;
        int32_t name_strref = -1;
        if (!array->get_value(index, value, runtime)
            || !read_int_field(runtime, value, "spell", row.spell_id)
            || !read_int_field(runtime, value, "name_strref", name_strref)
            || !read_int_field(runtime, value, "level", row.level)
            || !read_int_field(runtime, value, "uses", row.uses)
            || !read_bool_field(runtime, value, "known", row.known)
            || row.spell_id < 0 || row.level < 0 || row.level > 9 || row.uses < 0) {
            return false;
        }

        const std::string name = localized_name(
            name_strref, "Spell", row.spell_id);
        row.name = append_text(name, output.text);
        output.rows.push_back(row);
    }

    std::sort(output.rows.begin(), output.rows.end(), [&output](const auto& lhs, const auto& rhs) {
        const int name_order = compare_casefold_ascii(
            output.text_view(lhs.name), output.text_view(rhs.name));
        return name_order == 0 ? lhs.spell_id < rhs.spell_id : name_order < 0;
    });
    return true;
}

} // namespace

std::string_view CreatureSpellViewSnapshot::text_view(CreatureSpellTextSlice slice) const noexcept
{
    if (slice.offset > text.size() || slice.length > text.size() - slice.offset) {
        return {};
    }
    return std::string_view{text}.substr(slice.offset, slice.length);
}

void build_creature_spell_rows(smalls::Runtime& runtime,
    ObjectHandle active_object,
    int32_t selected_class,
    int32_t selected_metamagic,
    CreatureSpellViewSnapshot& output)
{
    output = {};
    output.object = active_object;
    if (active_object.type != ObjectType::creature
        || !kernel::objects().get_object_base(active_object)) {
        output.status = CreatureSpellViewStatus::invalid_object;
        output.diagnostic = "Active object is not a live Creature";
        return;
    }

    const auto object = object_value(runtime, active_object);
    if (!copy_class_rows(runtime,
            runtime.execute_script("nwn1.creature", "get_spell_class_editor_rows", {object}),
            output)
        || !copy_metamagic_rows(runtime,
            runtime.execute_script("nwn1.creature", "get_spell_metamagic_editor_rows", {object}),
            output)) {
        output.status = CreatureSpellViewStatus::invalid_data;
        output.diagnostic = "Smalls spell editor choices are unavailable";
        return;
    }

    const auto selected_class_row = std::find_if(
        output.classes.begin(), output.classes.end(), [selected_class](const auto& row) {
            return row.value == selected_class;
        });
    const auto class_row = selected_class_row != output.classes.end()
        ? selected_class_row
        : output.classes.begin();
    if (class_row == output.classes.end()) {
        output.status = CreatureSpellViewStatus::ready;
        output.diagnostic = "Creature has no spellcasting classes";
        return;
    }
    output.selected_class = class_row->value;
    output.memorizes = class_row->memorizes;

    const auto selected_meta_row = std::find_if(
        output.metamagic.begin(), output.metamagic.end(), [selected_metamagic](const auto& row) {
            return row.value == selected_metamagic;
        });
    const auto meta_row = selected_meta_row != output.metamagic.end()
        ? selected_meta_row
        : output.metamagic.begin();
    if (meta_row == output.metamagic.end()) {
        output.status = CreatureSpellViewStatus::invalid_data;
        output.diagnostic = "Smalls spell editor has no metamagic choices";
        return;
    }
    output.selected_metamagic = meta_row->value;

    const auto spells = runtime.execute_script("nwn1.creature", "get_spell_editor_rows",
        {object, smalls::Value::make_int(output.selected_class),
            smalls::Value::make_int(output.selected_metamagic)});
    if (!copy_spell_rows(runtime, spells, output)) {
        output.status = CreatureSpellViewStatus::invalid_data;
        output.diagnostic = "Smalls spell editor rows are unavailable";
        return;
    }
    output.status = CreatureSpellViewStatus::ready;
}

void filter_creature_spell_rows(const CreatureSpellViewSnapshot& snapshot,
    std::string_view query,
    int32_t spell_level,
    std::vector<uint32_t>& output)
{
    output.clear();
    if (snapshot.status != CreatureSpellViewStatus::ready
        || spell_level < -1 || spell_level > 9) {
        return;
    }

    output.reserve(snapshot.rows.size());
    for (size_t index = 0; index < snapshot.rows.size(); ++index) {
        const auto& row = snapshot.rows[index];
        if ((spell_level < 0 || row.level == spell_level)
            && contains_casefold_ascii(snapshot.text_view(row.name), query)) {
            output.push_back(static_cast<uint32_t>(index));
        }
    }
}

} // namespace nw::toolset
