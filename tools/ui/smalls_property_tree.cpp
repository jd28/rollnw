#include "smalls_property_tree.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/smalls/Array.hpp>
#include <nw/smalls/runtime.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>

namespace nw::toolset {
namespace {

constexpr PropertyNodeFlags read_only_flags = PropertyNodeFlags::read_only;

struct ResolvedType {
    smalls::TypeID id{};
    const smalls::Type* type = nullptr;
};

ResolvedType resolve_storage_type(const smalls::Runtime& runtime, smalls::TypeID declared_type)
{
    auto id = declared_type;
    const auto* type = runtime.get_type(id);
    for (uint32_t guard = 0; type && guard < 32; ++guard) {
        if ((type->type_kind != smalls::TK_alias && type->type_kind != smalls::TK_newtype)
            || !type->type_params[0].is<smalls::TypeID>()) {
            return {id, type};
        }
        const auto next = type->type_params[0].as<smalls::TypeID>();
        if (next == id || next == smalls::invalid_type_id) {
            return {};
        }
        id = next;
        type = runtime.get_type(id);
    }
    return {};
}

std::string_view short_type_name(std::string_view name)
{
    const size_t separator = name.find_last_of('.');
    return separator == std::string_view::npos ? name : name.substr(separator + 1);
}

std::string format_integer(int32_t value)
{
    char buffer[32];
    const auto result = std::to_chars(std::begin(buffer), std::end(buffer), value);
    return result.ec == std::errc{} ? std::string{buffer, result.ptr} : std::string{"<format error>"};
}

std::string format_float(float value)
{
    char buffer[48];
    const auto result = std::to_chars(std::begin(buffer), std::end(buffer), value, std::chars_format::general, 7);
    return result.ec == std::errc{} ? std::string{buffer, result.ptr} : std::string{"<format error>"};
}

std::string format_count(size_t count, std::string_view noun)
{
    std::string result = std::to_string(count);
    result.push_back(' ');
    result.append(noun);
    if (count != 1) {
        result.push_back('s');
    }
    return result;
}

struct FormattedValue {
    std::string text;
    PropertyValueKind kind = PropertyValueKind::unsupported;
    bool unsupported = false;
};

FormattedValue format_scalar(smalls::Runtime& runtime,
    const smalls::Value& value,
    smalls::TypeID declared_type)
{
    if (value.type_id == smalls::invalid_type_id) {
        return {"<unavailable>", PropertyValueKind::error, true};
    }

    if (runtime.find_str_op(declared_type)) {
        const auto string_value = runtime.execute_str_op(value);
        if (string_value.type_id == runtime.string_type()
            && string_value.storage == smalls::ValueStorage::heap) {
            return {std::string{runtime.get_string_view(string_value.data.hptr)}, PropertyValueKind::string, false};
        }
    }

    if (runtime.is_object_like_type(declared_type)) {
        if (value.data.oval.type == ObjectType::invalid) {
            return {"invalid", PropertyValueKind::object, false};
        }
        return {std::to_string(value.data.oval.to_ull()), PropertyValueKind::object, false};
    }

    const auto storage = resolve_storage_type(runtime, declared_type);
    if (!storage.type) {
        return {"<unknown type>", PropertyValueKind::error, true};
    }

    if (storage.type->type_kind == smalls::TK_primitive) {
        switch (storage.type->primitive_kind) {
        case smalls::PK_int:
            return {format_integer(value.data.ival), PropertyValueKind::integer, false};
        case smalls::PK_float:
            return {format_float(value.data.fval), PropertyValueKind::floating, false};
        case smalls::PK_bool:
            return {value.data.bval ? "true" : "false", PropertyValueKind::boolean, false};
        case smalls::PK_string:
            return {std::string{runtime.get_string_view(value.data.hptr)}, PropertyValueKind::string, false};
        default:
            break;
        }
    }

    return {"<unsupported>", PropertyValueKind::unsupported, true};
}

std::string_view group_name(const PropertyTreeBuildOptions& options,
    const PropertyFieldGroup& group) noexcept
{
    if (group.name.offset > options.field_group_text.size()
        || group.name.length > options.field_group_text.size() - group.name.offset) {
        return {};
    }
    return options.field_group_text.substr(group.name.offset, group.name.length);
}

bool validate_field_groups(smalls::Runtime& runtime,
    const PropertyTreeBuildOptions& options,
    std::string& diagnostic)
{
    for (const auto& group : options.field_groups) {
        const auto* definition = runtime.get_struct_def(group.root_propset_type);
        if (!definition || group.field_count == 0
            || group.first_field >= definition->field_count
            || group.field_count > definition->field_count - group.first_field
            || group_name(options, group).empty()) {
            diagnostic = "Property field group has an invalid propset, range, or name";
            return false;
        }
    }

    for (size_t index = 0; index < options.field_groups.size(); ++index) {
        const auto& group = options.field_groups[index];
        const uint32_t group_end = group.first_field + group.field_count;
        for (size_t other_index = index + 1; other_index < options.field_groups.size(); ++other_index) {
            const auto& other = options.field_groups[other_index];
            if (other.root_propset_type != group.root_propset_type) {
                continue;
            }
            const uint32_t other_end = other.first_field + other.field_count;
            if (group.first_field < other_end && other.first_field < group_end) {
                diagnostic = "Property field groups must not overlap";
                return false;
            }
        }
    }
    return true;
}

class PropertyTreeBuilder {
public:
    PropertyTreeBuilder(smalls::Runtime& runtime,
        ObjectHandle object,
        const PropertyTreeExpansionState& expansion,
        PropertyTreeBuildOptions options,
        PropertyTreeSnapshot& output)
        : runtime_{runtime}
        , object_{object}
        , expansion_{expansion}
        , options_{options}
        , output_{output}
    {
    }

    void build()
    {
        std::vector<smalls::TypeID> propset_types;
        runtime_.object_propset_types(object_.type, propset_types);
        output_.registered_propset_count = static_cast<uint32_t>(propset_types.size());

        for (const auto propset_type : propset_types) {
            if (row_limit_reached_) {
                break;
            }

            const auto* definition = runtime_.get_struct_def(propset_type);
            if (definition && definition->is_transient) {
                continue;
            }
            if (std::find(options_.excluded_root_propsets.begin(),
                    options_.excluded_root_propsets.end(), propset_type)
                != options_.excluded_root_propsets.end()) {
                continue;
            }
            ++output_.persistent_propset_count;
            append_propset(propset_type, definition);
        }
    }

private:
    PropertyTextSlice append_text(std::string_view text)
    {
        if (output_.text.size() >= std::numeric_limits<uint32_t>::max()) {
            return {};
        }
        const size_t available = std::numeric_limits<uint32_t>::max() - output_.text.size();
        const size_t length = std::min(text.size(), available);
        const auto offset = static_cast<uint32_t>(output_.text.size());
        output_.text.append(text.data(), length);
        return {offset, static_cast<uint32_t>(length)};
    }

    uint32_t append_row(uint32_t parent,
        uint16_t depth,
        smalls::TypeID root_type,
        smalls::TypeID declared_type,
        PropertyNodeKind node_kind,
        PropertyValueKind value_kind,
        PropertyNodeFlags flags,
        std::string_view name,
        std::string_view type_name,
        std::string_view value)
    {
        if (output_.rows.size() >= options_.max_rows) {
            return property_no_parent;
        }

        PropertyNodeRow row;
        row.parent = parent;
        row.subtree_end = static_cast<uint32_t>(output_.rows.size() + 1);
        row.path_offset = static_cast<uint32_t>(output_.path_segments.size());
        row.path_count = static_cast<uint16_t>(std::min<size_t>(path_.size(), UINT16_MAX));
        row.depth = depth;
        row.root_propset_type = root_type;
        row.declared_type = declared_type;
        row.name = append_text(name);
        row.type_name = append_text(type_name);
        row.value = append_text(value);
        row.node_kind = node_kind;
        row.value_kind = value_kind;
        row.flags = flags;
        output_.path_segments.insert(output_.path_segments.end(), path_.begin(), path_.end());
        output_.rows.push_back(row);
        return static_cast<uint32_t>(output_.rows.size() - 1);
    }

    bool reserve_regular_row(uint32_t parent, uint16_t depth, smalls::TypeID root_type)
    {
        if (output_.rows.size() + 1 < options_.max_rows) {
            return true;
        }
        append_limit_row(parent, depth, root_type, "Row materialization limit reached");
        row_limit_reached_ = true;
        return false;
    }

    void append_limit_row(uint32_t parent, uint16_t depth, smalls::TypeID root_type, std::string_view message)
    {
        if (output_.rows.size() >= options_.max_rows) {
            return;
        }
        output_.truncated = true;
        if (output_.diagnostic.empty()) {
            output_.diagnostic.assign(message);
        }
        append_row(parent,
            depth,
            root_type,
            smalls::invalid_type_id,
            PropertyNodeKind::limit,
            PropertyValueKind::error,
            read_only_flags | PropertyNodeFlags::truncated,
            "Limit",
            {},
            message);
        if (parent != property_no_parent && parent < output_.rows.size()) {
            ++output_.rows[parent].direct_child_count;
        }
    }

    bool can_descend(uint32_t parent, uint16_t depth, smalls::TypeID root_type)
    {
        if (depth <= options_.max_depth) {
            return true;
        }
        append_limit_row(parent, depth, root_type, "Maximum property depth reached");
        return false;
    }

    void finish_row(uint32_t row_index)
    {
        if (row_index != property_no_parent && row_index < output_.rows.size()) {
            output_.rows[row_index].subtree_end = static_cast<uint32_t>(output_.rows.size());
        }
    }

    void append_propset(smalls::TypeID propset_type, const smalls::StructDef* definition)
    {
        if (!reserve_regular_row(property_no_parent, 0, propset_type)) {
            return;
        }

        const auto qualified_name = runtime_.type_name(propset_type);
        const bool expanded = expansion_.is_expanded(propset_type, {}, true);
        auto flags = read_only_flags;
        if (definition && definition->field_count > 0) {
            flags |= PropertyNodeFlags::has_children;
        }
        if (expanded) {
            flags |= PropertyNodeFlags::expanded;
        }

        const auto propset_ref = runtime_.find_propset_ref(propset_type, object_);
        const bool present = propset_ref.type_id != smalls::invalid_type_id;
        const auto row_index = append_row(property_no_parent,
            0,
            propset_type,
            propset_type,
            PropertyNodeKind::propset,
            definition && present ? PropertyValueKind::aggregate : PropertyValueKind::error,
            definition && present ? flags : flags | PropertyNodeFlags::unsupported,
            short_type_name(qualified_name),
            qualified_name,
            !definition ? "Schema unavailable" : (present ? format_count(definition->field_count, "field") : "Not instantiated"));

        if (row_index == property_no_parent || !expanded || !definition || !present) {
            finish_row(row_index);
            return;
        }

        append_struct_fields(propset_ref, definition, row_index, 1, propset_type, true);
        finish_row(row_index);
    }

    void append_struct_fields(const smalls::Value& parent_value,
        const smalls::StructDef* definition,
        uint32_t parent,
        uint16_t depth,
        smalls::TypeID root_type,
        bool apply_field_groups)
    {
        if (!can_descend(parent, depth, root_type)) {
            return;
        }

        for (uint32_t field_index = 0; field_index < definition->field_count; ++field_index) {
            if (row_limit_reached_) {
                return;
            }

            const auto group = apply_field_groups
                ? std::ranges::find_if(options_.field_groups, [&](const auto& candidate) {
                      return candidate.root_propset_type == root_type
                          && candidate.first_field == field_index;
                  })
                : options_.field_groups.end();
            if (group != options_.field_groups.end()) {
                append_field_group(parent_value, definition, *group, parent, depth, root_type);
                field_index += group->field_count - 1;
                continue;
            }

            path_.push_back({PropertyPathSegmentKind::field, field_index});
            append_field(parent_value, definition->fields[field_index], parent, depth, root_type);
            path_.pop_back();
        }
    }

    void append_field_group(const smalls::Value& parent_value,
        const smalls::StructDef* definition,
        const PropertyFieldGroup& group,
        uint32_t parent,
        uint16_t depth,
        smalls::TypeID root_type)
    {
        if (!reserve_regular_row(parent, depth, root_type)) {
            return;
        }

        path_.push_back({PropertyPathSegmentKind::presentation_group, group.first_field});
        const bool expanded = expansion_.is_expanded(root_type, path_, false);
        auto flags = read_only_flags | PropertyNodeFlags::has_children;
        if (expanded) {
            flags |= PropertyNodeFlags::expanded;
        }
        const uint32_t row_index = append_row(parent,
            depth,
            root_type,
            smalls::invalid_type_id,
            PropertyNodeKind::group,
            PropertyValueKind::aggregate,
            flags,
            group_name(options_, group),
            {},
            format_count(group.field_count, "field"));
        path_.pop_back();
        ++output_.rows[parent].direct_child_count;

        if (row_index != property_no_parent && expanded
            && can_descend(row_index, static_cast<uint16_t>(depth + 1), root_type)) {
            const uint32_t end = group.first_field + group.field_count;
            for (uint32_t field_index = group.first_field;
                field_index < end && !row_limit_reached_;
                ++field_index) {
                path_.push_back({PropertyPathSegmentKind::field, field_index});
                append_field(parent_value,
                    definition->fields[field_index],
                    row_index,
                    static_cast<uint16_t>(depth + 1),
                    root_type);
                path_.pop_back();
            }
        }
        finish_row(row_index);
    }

    void append_field(const smalls::Value& parent_value,
        const smalls::FieldDef& field,
        uint32_t parent,
        uint16_t depth,
        smalls::TypeID root_type)
    {
        if (!reserve_regular_row(parent, depth, root_type)) {
            return;
        }

        const auto storage = resolve_storage_type(runtime_, field.type_id);
        const auto type_name = runtime_.type_name(field.type_id);
        if (!storage.type) {
            append_unsupported_row(parent, depth, root_type, field.type_id, PropertyNodeKind::field,
                field.name.view(), type_name, "Unknown type");
            return;
        }

        if (storage.type->type_kind == smalls::TK_fixed_array) {
            append_fixed_array(parent_value, field, *storage.type, parent, depth, root_type);
            return;
        }

        const auto value = runtime_.read_value_field_at_offset(parent_value, field.offset, field.type_id);
        if (storage.type->type_kind == smalls::TK_array) {
            append_dynamic_array(value, field, parent, depth, root_type);
            return;
        }
        if (storage.type->type_kind == smalls::TK_struct) {
            append_struct(value, field.name.view(), field.type_id, parent, depth, root_type, PropertyNodeKind::field);
            return;
        }

        const auto formatted = format_scalar(runtime_, value, field.type_id);
        auto flags = read_only_flags;
        if (formatted.unsupported) {
            flags |= PropertyNodeFlags::unsupported;
        }
        append_row(parent,
            depth,
            root_type,
            field.type_id,
            PropertyNodeKind::field,
            formatted.kind,
            flags,
            field.name.view(),
            type_name,
            formatted.text);
        ++output_.rows[parent].direct_child_count;
    }

    void append_struct(const smalls::Value& value,
        std::string_view name,
        smalls::TypeID declared_type,
        uint32_t parent,
        uint16_t depth,
        smalls::TypeID root_type,
        PropertyNodeKind node_kind)
    {
        const auto* definition = runtime_.get_struct_def(declared_type);
        const bool readable = value.type_id != smalls::invalid_type_id && definition;
        const bool expanded = expansion_.is_expanded(root_type, path_, false);
        auto flags = read_only_flags;
        if (definition && definition->field_count > 0) {
            flags |= PropertyNodeFlags::has_children;
        }
        if (expanded) {
            flags |= PropertyNodeFlags::expanded;
        }
        if (!readable) {
            flags |= PropertyNodeFlags::unsupported;
        }

        const uint32_t row_index = append_row(parent,
            depth,
            root_type,
            declared_type,
            node_kind,
            readable ? PropertyValueKind::aggregate : PropertyValueKind::error,
            flags,
            name,
            runtime_.type_name(declared_type),
            readable ? format_count(definition->field_count, "field") : std::string{"Value unavailable"});
        ++output_.rows[parent].direct_child_count;
        if (row_index != property_no_parent && readable && expanded) {
            append_struct_fields(value, definition, row_index,
                static_cast<uint16_t>(depth + 1), root_type, false);
        }
        finish_row(row_index);
    }

    void append_dynamic_array(const smalls::Value& value,
        const smalls::FieldDef& field,
        uint32_t parent,
        uint16_t depth,
        smalls::TypeID root_type)
    {
        smalls::IArray* array = value.type_id != smalls::invalid_type_id
            ? runtime_.resolve_array(value)
            : nullptr;

        const bool expanded = expansion_.is_expanded(root_type, path_, false);
        auto flags = read_only_flags | PropertyNodeFlags::has_children;
        if (expanded) {
            flags |= PropertyNodeFlags::expanded;
        }
        if (!array) {
            flags |= PropertyNodeFlags::unsupported;
        }
        const uint32_t row_index = append_row(parent,
            depth,
            root_type,
            field.type_id,
            PropertyNodeKind::field,
            array ? PropertyValueKind::aggregate : PropertyValueKind::error,
            flags,
            field.name.view(),
            runtime_.type_name(field.type_id),
            array ? format_count(array->size(), "entry") : std::string{"Array unavailable"});
        ++output_.rows[parent].direct_child_count;

        if (row_index != property_no_parent && array && expanded
            && can_descend(row_index, static_cast<uint16_t>(depth + 1), root_type)) {
            for (size_t index = 0; index < array->size() && !row_limit_reached_; ++index) {
                if (!reserve_regular_row(row_index, static_cast<uint16_t>(depth + 1), root_type)) {
                    break;
                }
                smalls::Value element;
                path_.push_back({PropertyPathSegmentKind::array_index, static_cast<uint32_t>(index)});
                const std::string index_name = "[" + std::to_string(index) + "]";
                if (!array->get_value(index, element, runtime_)) {
                    append_unsupported_row(row_index,
                        static_cast<uint16_t>(depth + 1),
                        root_type,
                        array->element_type(),
                        PropertyNodeKind::array_element,
                        index_name,
                        runtime_.type_name(array->element_type()),
                        "Element unavailable");
                } else {
                    append_array_element(element, index_name, array->element_type(), row_index,
                        static_cast<uint16_t>(depth + 1), root_type);
                }
                path_.pop_back();
            }
        }
        finish_row(row_index);
    }

    void append_fixed_array(const smalls::Value& parent_value,
        const smalls::FieldDef& field,
        const smalls::Type& array_type,
        uint32_t parent,
        uint16_t depth,
        smalls::TypeID root_type)
    {
        if (!array_type.type_params[0].is<smalls::TypeID>() || !array_type.type_params[1].is<int32_t>()) {
            append_unsupported_row(parent, depth, root_type, field.type_id, PropertyNodeKind::field,
                field.name.view(), runtime_.type_name(field.type_id), "Invalid fixed array metadata");
            return;
        }
        const auto element_type = array_type.type_params[0].as<smalls::TypeID>();
        const int32_t signed_count = array_type.type_params[1].as<int32_t>();
        const auto* element_storage = runtime_.get_type(element_type);
        if (signed_count < 0 || !element_storage || element_storage->size == 0) {
            append_unsupported_row(parent, depth, root_type, field.type_id, PropertyNodeKind::field,
                field.name.view(), runtime_.type_name(field.type_id), "Invalid fixed array layout");
            return;
        }

        const uint32_t count = static_cast<uint32_t>(signed_count);
        if (count > 0
            && count - 1 > (std::numeric_limits<uint32_t>::max() - field.offset) / element_storage->size) {
            append_unsupported_row(parent, depth, root_type, field.type_id, PropertyNodeKind::field,
                field.name.view(), runtime_.type_name(field.type_id), "Fixed array offset exceeds the addressable range");
            return;
        }
        const bool expanded = expansion_.is_expanded(root_type, path_, false);
        auto flags = read_only_flags;
        if (count > 0) {
            flags |= PropertyNodeFlags::has_children;
        }
        if (expanded) {
            flags |= PropertyNodeFlags::expanded;
        }
        const uint32_t row_index = append_row(parent,
            depth,
            root_type,
            field.type_id,
            PropertyNodeKind::field,
            PropertyValueKind::aggregate,
            flags,
            field.name.view(),
            runtime_.type_name(field.type_id),
            format_count(count, "entry"));
        ++output_.rows[parent].direct_child_count;

        if (row_index != property_no_parent && expanded
            && can_descend(row_index, static_cast<uint16_t>(depth + 1), root_type)) {
            for (uint32_t index = 0; index < count && !row_limit_reached_; ++index) {
                if (!reserve_regular_row(row_index, static_cast<uint16_t>(depth + 1), root_type)) {
                    break;
                }
                path_.push_back({PropertyPathSegmentKind::array_index, index});
                const auto element = runtime_.read_value_field_at_offset(
                    parent_value, field.offset + index * element_storage->size, element_type);
                append_array_element(element,
                    "[" + std::to_string(index) + "]",
                    element_type,
                    row_index,
                    static_cast<uint16_t>(depth + 1),
                    root_type);
                path_.pop_back();
            }
        }
        finish_row(row_index);
    }

    void append_array_element(const smalls::Value& element,
        std::string_view name,
        smalls::TypeID element_type,
        uint32_t parent,
        uint16_t depth,
        smalls::TypeID root_type)
    {
        const auto storage = resolve_storage_type(runtime_, element_type);
        if (storage.type && storage.type->type_kind == smalls::TK_struct) {
            append_struct(element, name, element_type, parent, depth, root_type, PropertyNodeKind::array_element);
            return;
        }

        const auto formatted = format_scalar(runtime_, element, element_type);
        auto flags = read_only_flags;
        if (formatted.unsupported) {
            flags |= PropertyNodeFlags::unsupported;
        }
        append_row(parent,
            depth,
            root_type,
            element_type,
            PropertyNodeKind::array_element,
            formatted.kind,
            flags,
            name,
            runtime_.type_name(element_type),
            formatted.text);
        ++output_.rows[parent].direct_child_count;
    }

    void append_unsupported_row(uint32_t parent,
        uint16_t depth,
        smalls::TypeID root_type,
        smalls::TypeID declared_type,
        PropertyNodeKind node_kind,
        std::string_view name,
        std::string_view type_name,
        std::string_view value)
    {
        append_row(parent,
            depth,
            root_type,
            declared_type,
            node_kind,
            PropertyValueKind::unsupported,
            read_only_flags | PropertyNodeFlags::unsupported,
            name,
            type_name,
            value);
        ++output_.rows[parent].direct_child_count;
    }

    smalls::Runtime& runtime_;
    ObjectHandle object_;
    const PropertyTreeExpansionState& expansion_;
    PropertyTreeBuildOptions options_;
    PropertyTreeSnapshot& output_;
    std::vector<PropertyPathSegment> path_;
    bool row_limit_reached_ = false;
};

} // namespace

std::string_view PropertyTreeSnapshot::text_view(PropertyTextSlice slice) const noexcept
{
    if (slice.offset > text.size() || slice.length > text.size() - slice.offset) {
        return {};
    }
    return std::string_view{text}.substr(slice.offset, slice.length);
}

std::span<const PropertyPathSegment> PropertyTreeSnapshot::path(const PropertyNodeRow& row) const noexcept
{
    if (row.path_offset > path_segments.size() || row.path_count > path_segments.size() - row.path_offset) {
        return {};
    }
    return std::span{path_segments}.subspan(row.path_offset, row.path_count);
}

size_t PropertyTreeSnapshot::data_bytes() const noexcept
{
    return rows.size() * sizeof(PropertyNodeRow)
        + path_segments.size() * sizeof(PropertyPathSegment)
        + text.size()
        + diagnostic.size();
}

bool PropertyTreeExpansionState::is_expanded(smalls::TypeID root_propset_type,
    std::span<const PropertyPathSegment> path,
    bool default_value) const noexcept
{
    const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& entry) {
        return entry.root_propset_type == root_propset_type
            && std::ranges::equal(entry.path, path);
    });
    return found == entries_.end() ? default_value : found->expanded;
}

void PropertyTreeExpansionState::set_expanded(smalls::TypeID root_propset_type,
    std::span<const PropertyPathSegment> path,
    bool expanded,
    bool default_value)
{
    const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& entry) {
        return entry.root_propset_type == root_propset_type
            && std::ranges::equal(entry.path, path);
    });
    if (expanded == default_value) {
        if (found != entries_.end()) {
            entries_.erase(found);
        }
        return;
    }
    if (found != entries_.end()) {
        found->expanded = expanded;
        return;
    }
    entries_.push_back({root_propset_type, {path.begin(), path.end()}, expanded});
}

void PropertyTreeExpansionState::toggle(smalls::TypeID root_propset_type,
    std::span<const PropertyPathSegment> path,
    bool default_value)
{
    set_expanded(root_propset_type, path,
        !is_expanded(root_propset_type, path, default_value), default_value);
}

void PropertyTreeExpansionState::clear() noexcept
{
    entries_.clear();
}

size_t PropertyTreeExpansionState::size() const noexcept
{
    return entries_.size();
}

void build_property_rows(smalls::Runtime& runtime,
    ObjectHandle active_object,
    const PropertyTreeExpansionState& expansion,
    PropertyTreeBuildOptions options,
    PropertyTreeSnapshot& output)
{
    output = {};
    output.object = active_object;
    if (options.max_rows == 0 || options.max_depth == 0
        || options.max_depth == std::numeric_limits<uint16_t>::max()) {
        output.status = PropertyTreeStatus::invalid_options;
        output.diagnostic = "Property tree row limit must be nonzero and depth must be in 1..65534";
        return;
    }
    if (!kernel::objects().valid(active_object)) {
        output.status = PropertyTreeStatus::invalid_object;
        output.diagnostic = "The active object is no longer valid";
        return;
    }
    if (!validate_field_groups(runtime, options, output.diagnostic)) {
        output.status = PropertyTreeStatus::invalid_options;
        return;
    }

    output.status = PropertyTreeStatus::ready;
    PropertyTreeBuilder{runtime, active_object, expansion, options, output}.build();
}

std::span<const PropertyNodeRow> slice_visible_property_rows(
    const PropertyTreeSnapshot& snapshot, uint32_t start, uint32_t count) noexcept
{
    const size_t first = std::min<size_t>(start, snapshot.rows.size());
    const size_t available = snapshot.rows.size() - first;
    return std::span{snapshot.rows}.subspan(first, std::min<size_t>(count, available));
}

} // namespace nw::toolset
