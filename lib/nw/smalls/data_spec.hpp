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
    column,
    enum_value,
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
    Vector<std::pair<String, int32_t>> enum_values;
    DataErrorPolicy on_missing = DataErrorPolicy::reject;
    DataScalar default_value;
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
    String source_resource;
    String valid_column;
    DataErrorPolicy invalid_row_policy = DataErrorPolicy::omit_row;
    String config_path;
    String entry_type;
    String snapshot_filename_column;
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
