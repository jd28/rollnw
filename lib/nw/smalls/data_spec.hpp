#pragma once

#include "Diagnostic.hpp"

#include "../config.hpp"
#include "../resources/assets.hpp"

#include <filesystem>
#include <span>
#include <variant>

namespace nw::smalls {

enum class DataValueKind : uint8_t {
    row_index,
    constant,
    column,
    enum_value,
    reference_index,
    fixed_array,
    indirect_grid,
    column_array,
    struct_array,
};

enum class DataValueType : uint8_t {
    integer,
    floating,
    boolean,
    string,
    resref,
};

enum class DataOwner : uint8_t {
    native,
    smalls,
};

enum class DataErrorPolicy : uint8_t {
    reject,
    default_value,
    omit_row,
};

using DataScalar = std::variant<std::monostate, int32_t, float, bool, String, Resref>;

struct DataDiagnostic {
    std::filesystem::path source;
    int32_t row = -1;
    String target;
    String message;
    DiagnosticSeverity severity = DiagnosticSeverity::error;
};

struct DataValueExpression {
    DataValueKind kind = DataValueKind::column;
    DataValueType type = DataValueType::integer;
    String column;
    Vector<String> columns;
    Vector<std::pair<String, int32_t>> enum_values;
    String reference_column;
    String column_prefix;
    String limit_column;
    int32_t column_count = 0;
    int32_t array_size = 0;
    String element_value_field;
    String element_type;
    Vector<std::pair<String, int32_t>> element_constant_fields;
    bool warn_on_bool_coerce = true;
    DataErrorPolicy on_missing = DataErrorPolicy::reject;
    DataScalar default_value;
};

enum class DataSourceKind : uint8_t {
    twoda,
    twoda_references,
};

struct DataFieldSpec {
    String target;
    DataValueExpression value;
};

struct DataFieldGroupSpec {
    String target;
    DataOwner owner = DataOwner::smalls;
    String type;
    Vector<DataFieldSpec> fields;
};

struct DataSpec {
    std::filesystem::path source_path;
    DataSourceKind source_kind = DataSourceKind::twoda;
    String source_resource;
    String source_reference_column;
    String valid_column;
    String valid_positive_int_column;
    DataErrorPolicy invalid_row_policy = DataErrorPolicy::omit_row;
    String config_path;
    String entry_type;
    String snapshot_filename_column;
    bool snapshot_filename_is_strref = false;
    Vector<DataFieldSpec> fields;
    Vector<DataFieldGroupSpec> field_groups;
};

bool parse_data_specs(
    std::span<const std::filesystem::path> paths,
    Vector<DataSpec>& output,
    Vector<DataDiagnostic>& diagnostics);

bool parse_data_spec(
    StringView contents,
    StringView source_name,
    DataSpec& output,
    Vector<DataDiagnostic>& diagnostics);

} // namespace nw::smalls
