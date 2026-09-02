#include "data_spec.hpp"

#include <algorithm>
#include <fstream>
#include <set>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

namespace nw::smalls {
namespace {

using Json = nlohmann::json;

void add_diagnostic(
    Vector<DataDiagnostic>& diagnostics,
    const std::filesystem::path& source,
    StringView target,
    String message)
{
    diagnostics.push_back({
        .source = source,
        .target = String{target},
        .message = std::move(message),
    });
}

bool parse_value_type(StringView name, DataValueType& output)
{
    if (name == "int") {
        output = DataValueType::integer;
    } else if (name == "float") {
        output = DataValueType::floating;
    } else if (name == "bool") {
        output = DataValueType::boolean;
    } else if (name == "string") {
        output = DataValueType::string;
    } else if (name == "resref") {
        output = DataValueType::resref;
    } else {
        return false;
    }
    return true;
}

bool parse_policy(StringView name, DataErrorPolicy& output)
{
    if (name == "reject") {
        output = DataErrorPolicy::reject;
    } else if (name == "default") {
        output = DataErrorPolicy::default_value;
    } else if (name == "omit_row") {
        output = DataErrorPolicy::omit_row;
    } else {
        return false;
    }
    return true;
}

bool parse_scalar(const Json& source, DataValueType type, DataScalar& output)
{
    switch (type) {
    case DataValueType::integer:
        if (!source.is_number_integer()) { return false; }
        output = source.get<int32_t>();
        return true;
    case DataValueType::floating:
        if (!source.is_number()) { return false; }
        output = source.get<float>();
        return true;
    case DataValueType::boolean:
        if (!source.is_boolean()) { return false; }
        output = source.get<bool>();
        return true;
    case DataValueType::string:
        if (!source.is_string()) { return false; }
        output = String{source.get<std::string>()};
        return true;
    case DataValueType::resref:
        if (!source.is_string()) { return false; }
        output = Resref{source.get<std::string>()};
        return true;
    }
    return false;
}

bool parse_expression(
    const Json& source,
    const std::filesystem::path& source_path,
    StringView target,
    DataValueExpression& output,
    Vector<DataDiagnostic>& diagnostics)
{
    if (!source.is_object() || !source.contains("kind")
        || !source["kind"].is_string()) {
        add_diagnostic(diagnostics, source_path, target,
            "Data field value must contain a string 'kind'");
        return false;
    }

    const auto kind = source["kind"].get<std::string>();
    if (kind == "row_index") {
        output.kind = DataValueKind::row_index;
        output.type = DataValueType::integer;
    } else if (kind == "constant") {
        output.kind = DataValueKind::constant;
    } else if (kind == "column") {
        output.kind = DataValueKind::column;
    } else if (kind == "enum") {
        output.kind = DataValueKind::enum_value;
        output.type = DataValueType::integer;
    } else if (kind == "reference_index") {
        output.kind = DataValueKind::reference_index;
        output.type = DataValueType::integer;
    } else if (kind == "fixed_array") {
        output.kind = DataValueKind::fixed_array;
        output.type = DataValueType::integer;
    } else if (kind == "indirect_grid") {
        output.kind = DataValueKind::indirect_grid;
        output.type = DataValueType::integer;
    } else if (kind == "column_array") {
        output.kind = DataValueKind::column_array;
        output.type = DataValueType::integer;
    } else if (kind == "struct_array") {
        output.kind = DataValueKind::struct_array;
        output.type = DataValueType::integer;
    } else {
        add_diagnostic(diagnostics, source_path, target,
            fmt::format("Unknown data expression kind '{}'", kind));
        return false;
    }

    if (output.kind == DataValueKind::column
        || output.kind == DataValueKind::constant) {
        if (!source.contains("type") || !source["type"].is_string()
            || !parse_value_type(source["type"].get<std::string>(), output.type)) {
            add_diagnostic(diagnostics, source_path, target,
                "Data expression has a missing or invalid 'type'");
            return false;
        }
    }
    if (source.contains("warn_on_coerce")) {
        if (!source["warn_on_coerce"].is_boolean()) {
            add_diagnostic(diagnostics, source_path, target,
                "warn_on_coerce must be boolean");
            return false;
        }
        output.warn_on_bool_coerce = source["warn_on_coerce"].get<bool>();
    }

    if (output.kind == DataValueKind::column
        || output.kind == DataValueKind::enum_value
        || output.kind == DataValueKind::reference_index
        || output.kind == DataValueKind::indirect_grid
        || output.kind == DataValueKind::column_array) {
        if (!source.contains("column") || !source["column"].is_string()
            || source["column"].get<std::string>().empty()) {
            add_diagnostic(diagnostics, source_path, target,
                "Column expression requires a non-empty 'column'");
            return false;
        }
        output.column = source["column"].get<std::string>();
    }

    if (output.kind == DataValueKind::constant) {
        if (!source.contains("value")
            || !parse_scalar(source["value"], output.type,
                output.default_value)) {
            add_diagnostic(diagnostics, source_path, target,
                "Constant expression requires a typed 'value'");
            return false;
        }
    }

    if (output.kind == DataValueKind::fixed_array
        || output.kind == DataValueKind::struct_array) {
        if (!source.contains("columns") || !source["columns"].is_array()) {
            add_diagnostic(diagnostics, source_path, target,
                "Array expression requires a 'columns' array");
            return false;
        }
        for (const auto& column : source["columns"]) {
            if (!column.is_string() || column.get<std::string>().empty()) {
                add_diagnostic(diagnostics, source_path, target,
                    "Array expression columns must be non-empty strings");
                return false;
            }
            output.columns.push_back(column.get<std::string>());
        }
        if (output.columns.empty()) {
            add_diagnostic(diagnostics, source_path, target,
                "Array expression requires at least one column");
            return false;
        }
    }

    if (output.kind == DataValueKind::reference_index) {
        output.reference_column = source.value("reference_column", output.column);
        if (output.reference_column.empty()) {
            add_diagnostic(diagnostics, source_path, target,
                "Reference-index expression requires 'reference_column'");
            return false;
        }
    }

    if (output.kind == DataValueKind::indirect_grid) {
        output.column_prefix = source.value("column_prefix", "");
        output.limit_column = source.value("limit_column", "");
        output.column_count = source.value("column_count", 0);
        output.array_size = source.value("array_size", 0);
        if (output.column_prefix.empty() || output.column_count <= 0
            || output.array_size <= 0) {
            add_diagnostic(diagnostics, source_path, target,
                "Indirect grid requires column_prefix, positive column_count, and positive array_size");
            return false;
        }
    }

    if (output.kind == DataValueKind::struct_array) {
        output.element_value_field = source.value("value_field", "");
        output.element_type = source.value("element_type", "");
        if (output.element_value_field.empty()
            || output.element_type.empty()
            || !source.contains("constant_fields")
            || !source["constant_fields"].is_object()) {
            add_diagnostic(diagnostics, source_path, target,
                "Struct array requires element_type, value_field, and constant_fields");
            return false;
        }
        for (const auto& [name, value] : source["constant_fields"].items()) {
            if (name.empty() || !value.is_number_integer()) {
                add_diagnostic(diagnostics, source_path, target,
                    "Struct-array constant fields must be named integers");
                return false;
            }
            output.element_constant_fields.push_back(
                {name, value.get<int32_t>()});
        }
    }

    if (output.kind == DataValueKind::enum_value) {
        if (!source.contains("values") || !source["values"].is_object()) {
            add_diagnostic(diagnostics, source_path, target,
                "Enum expression requires an object 'values'");
            return false;
        }
        for (const auto& [name, value] : source["values"].items()) {
            if (!value.is_number_integer()) {
                add_diagnostic(diagnostics, source_path, target,
                    "Enum values must be integers");
                return false;
            }
            output.enum_values.push_back({name, value.get<int32_t>()});
        }
        if (output.enum_values.empty()) {
            add_diagnostic(diagnostics, source_path, target,
                "Enum expression must contain at least one value");
            return false;
        }
    }

    if (source.contains("on_missing")) {
        if (!source["on_missing"].is_string()
            || !parse_policy(source["on_missing"].get<std::string>(), output.on_missing)) {
            add_diagnostic(diagnostics, source_path, target,
                "Data expression has an invalid 'on_missing' policy");
            return false;
        }
    }
    if (output.on_missing == DataErrorPolicy::default_value) {
        if (!source.contains("default")
            || !parse_scalar(source["default"], output.type, output.default_value)) {
            add_diagnostic(diagnostics, source_path, target,
                "Default policy requires a typed 'default' value");
            return false;
        }
    }
    return true;
}

bool parse_fields(
    const Json& source,
    const std::filesystem::path& source_path,
    StringView prefix,
    Vector<DataFieldSpec>& output,
    std::set<String>& targets,
    Vector<DataDiagnostic>& diagnostics)
{
    if (!source.is_array()) {
        add_diagnostic(diagnostics, source_path, prefix,
            "Data spec 'fields' must be an array");
        return false;
    }
    for (const auto& field_json : source) {
        if (!field_json.is_object() || !field_json.contains("target")
            || !field_json["target"].is_string()
            || !field_json.contains("value")) {
            add_diagnostic(diagnostics, source_path, prefix,
                "Each data field requires 'target' and 'value'");
            return false;
        }
        DataFieldSpec field;
        field.target = field_json["target"].get<std::string>();
        if (field.target.empty() || field.target.find('.') != String::npos) {
            add_diagnostic(diagnostics, source_path, field.target,
                "Data field target must be one field name");
            return false;
        }
        const String full_target = prefix.empty()
            ? field.target
            : fmt::format("{}.{}", prefix, field.target);
        if (!targets.insert(full_target).second) {
            add_diagnostic(diagnostics, source_path, full_target,
                "Duplicate data field target");
            return false;
        }
        if (!parse_expression(field_json["value"], source_path, full_target,
                field.value, diagnostics)) {
            return false;
        }
        output.push_back(std::move(field));
    }
    return true;
}

bool parse_spec_json(
    const Json& root,
    const std::filesystem::path& source_path,
    DataSpec& output,
    Vector<DataDiagnostic>& diagnostics)
{
    if (!root.is_object() || !root.contains("source")
        || !root.contains("output")) {
        add_diagnostic(diagnostics, source_path, {},
            "Data spec requires 'source' and 'output' objects");
        return false;
    }
    const auto& source = root["source"];
    const auto& sink = root["output"];
    if (!source.is_object()) {
        add_diagnostic(diagnostics, source_path, {},
            "Data spec source must be an object");
        return false;
    }
    const auto source_kind = source.value("kind", "");
    if ((source_kind != "twoda" && source_kind != "twoda_references")
        || !source.contains("resource") || !source["resource"].is_string()) {
        add_diagnostic(diagnostics, source_path, {},
            "Data spec source must be a named 'twoda' or 'twoda_references' resource");
        return false;
    }
    output.source_kind = source_kind == "twoda_references"
        ? DataSourceKind::twoda_references
        : DataSourceKind::twoda;
    output.source_path = source_path;
    output.source_resource = source["resource"].get<std::string>();
    if (output.source_resource.empty()) {
        add_diagnostic(diagnostics, source_path, {},
            "Data spec source resource must not be empty");
        return false;
    }
    if (output.source_kind == DataSourceKind::twoda_references) {
        if (!source.contains("reference_column")
            || !source["reference_column"].is_string()
            || source["reference_column"].get<std::string>().empty()) {
            add_diagnostic(diagnostics, source_path, {},
                "twoda_references source requires 'reference_column'");
            return false;
        }
        output.source_reference_column = source["reference_column"].get<std::string>();
    }

    if (source.contains("valid_when")) {
        const auto& valid = source["valid_when"];
        if (!valid.is_object() || !valid.contains("column")
            || !valid["column"].is_string()
            || !valid.contains("on_missing")
            || !valid["on_missing"].is_string()
            || !parse_policy(valid["on_missing"].get<std::string>(),
                output.invalid_row_policy)
            || output.invalid_row_policy == DataErrorPolicy::default_value) {
            add_diagnostic(diagnostics, source_path, {},
                "Data spec 'valid_when' is invalid");
            return false;
        }
        output.valid_column = valid["column"].get<std::string>();
        if (valid.contains("positive_int_column")) {
            if (!valid["positive_int_column"].is_string()) {
                add_diagnostic(diagnostics, source_path, {},
                    "Data spec positive_int_column must be a string");
                return false;
            }
            output.valid_positive_int_column = valid["positive_int_column"].get<std::string>();
        }
    }

    if (!sink.is_object() || !sink.contains("config_path")
        || !sink["config_path"].is_string()
        || !sink.contains("entry_type") || !sink["entry_type"].is_string()) {
        add_diagnostic(diagnostics, source_path, {},
            "Data spec output requires 'config_path' and 'entry_type'");
        return false;
    }
    output.config_path = sink["config_path"].get<std::string>();
    output.entry_type = sink["entry_type"].get<std::string>();
    if (output.config_path.empty() || output.config_path.find('/') != String::npos
        || output.entry_type.empty()) {
        add_diagnostic(diagnostics, source_path, {},
            "Data spec paths must use non-empty canonical dotted names");
        return false;
    }

    if (sink.contains("snapshot_filename")) {
        if (!sink["snapshot_filename"].is_object()
            || (sink["snapshot_filename"].contains("column")
                == sink["snapshot_filename"].contains("strref_column"))) {
            add_diagnostic(diagnostics, source_path, {},
                "Data spec snapshot filename requires exactly one column or strref_column");
            return false;
        }
        const char* key = sink["snapshot_filename"].contains("strref_column")
            ? "strref_column"
            : "column";
        if (!sink["snapshot_filename"][key].is_string()) {
            add_diagnostic(diagnostics, source_path, {},
                "Data spec snapshot filename source must be a string");
            return false;
        }
        output.snapshot_filename_column = sink["snapshot_filename"][key].get<std::string>();
        output.snapshot_filename_is_strref = key == std::string_view{"strref_column"};
        if (output.snapshot_filename_column.empty()) {
            add_diagnostic(diagnostics, source_path, {},
                "Data spec snapshot filename column must not be empty");
            return false;
        }
    }

    std::set<String> targets;
    if (!sink.contains("fields")
        || !parse_fields(sink["fields"], source_path, {}, output.fields,
            targets, diagnostics)) {
        return false;
    }
    const auto id = std::ranges::find(output.fields, "id", &DataFieldSpec::target);
    if (id == output.fields.end()
        || id->value.kind != DataValueKind::row_index) {
        add_diagnostic(diagnostics, source_path, "id",
            "Data spec requires an 'id' row_index field");
        return false;
    }

    if (sink.contains("field_groups")) {
        if (!sink["field_groups"].is_array()) {
            add_diagnostic(diagnostics, source_path, {},
                "Data spec 'field_groups' must be an array");
            return false;
        }
        std::set<String> group_targets;
        for (const auto& group_json : sink["field_groups"]) {
            if (!group_json.is_object() || !group_json.contains("target")
                || !group_json["target"].is_string()
                || !group_json.contains("owner")
                || !group_json["owner"].is_string()
                || !group_json.contains("type") || !group_json["type"].is_string()
                || !group_json.contains("fields")) {
                add_diagnostic(diagnostics, source_path, {},
                    "Each field group requires target, owner, type, and fields");
                return false;
            }
            DataFieldGroupSpec group;
            group.target = group_json["target"].get<std::string>();
            group.type = group_json["type"].get<std::string>();
            if (!group_targets.insert(group.target).second) {
                add_diagnostic(diagnostics, source_path, group.target,
                    "Duplicate data field-group target");
                return false;
            }
            const auto owner = group_json["owner"].get<std::string>();
            if (owner == "native") {
                group.owner = DataOwner::native;
            } else if (owner == "smalls") {
                group.owner = DataOwner::smalls;
            } else {
                add_diagnostic(diagnostics, source_path, group.target,
                    "Field group owner must be 'native' or 'smalls'");
                return false;
            }
            if (group.target.empty() || group.target.find('.') != String::npos
                || group.type.empty()
                || !parse_fields(group_json["fields"], source_path,
                    group.target, group.fields, targets, diagnostics)) {
                return false;
            }
            output.field_groups.push_back(std::move(group));
        }
    }
    return true;
}

} // namespace

bool parse_data_spec(
    StringView contents,
    StringView source_name,
    DataSpec& output,
    Vector<DataDiagnostic>& diagnostics)
{
    DataSpec candidate;
    const std::filesystem::path source_path{source_name};
    try {
        const auto root = Json::parse(contents);
        if (!parse_spec_json(root, source_path, candidate, diagnostics)) {
            return false;
        }
    } catch (const std::exception& error) {
        add_diagnostic(diagnostics, source_path, {},
            fmt::format("Failed to parse data spec: {}", error.what()));
        return false;
    }
    output = std::move(candidate);
    return true;
}

bool parse_data_specs(
    std::span<const std::filesystem::path> paths,
    Vector<DataSpec>& output,
    Vector<DataDiagnostic>& diagnostics)
{
    Vector<DataSpec> candidates;
    candidates.reserve(paths.size());
    for (const auto& path : paths) {
        std::ifstream input{path};
        if (!input) {
            add_diagnostic(diagnostics, path, {}, "Failed to open data spec");
            return false;
        }
        const String contents{
            std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
        DataSpec spec;
        if (!parse_data_spec(contents, path.string(), spec, diagnostics)) {
            return false;
        }
        if (std::ranges::find(candidates, spec.config_path,
                &DataSpec::config_path)
            != candidates.end()) {
            add_diagnostic(diagnostics, path, spec.config_path,
                "Duplicate data spec config path");
            return false;
        }
        candidates.push_back(std::move(spec));
    }
    output = std::move(candidates);
    return true;
}

} // namespace nw::smalls
