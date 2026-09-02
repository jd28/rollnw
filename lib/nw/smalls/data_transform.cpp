#include "data_transform.hpp"

#include "../formats/StaticTwoDA.hpp"
#include "../util/string.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

#include <fmt/format.h>

namespace nw::smalls {
namespace {

enum class ReadResult : uint8_t {
    value,
    omit_row,
    reject_row,
};

void add_diagnostic(
    Vector<DataDiagnostic>& diagnostics,
    const DataSpec& spec,
    int32_t row,
    StringView target,
    String message,
    DiagnosticSeverity severity = DiagnosticSeverity::error)
{
    diagnostics.push_back({
        .source = spec.source_path,
        .row = row,
        .target = String{target},
        .message = std::move(message),
        .severity = severity,
    });
}

bool raw_missing(StringView value)
{
    return value.empty() || value == "****";
}

ReadResult missing_value(
    const DataSpec& spec,
    const DataValueExpression& expression,
    int32_t row,
    StringView target,
    DataScalar& output,
    Vector<DataDiagnostic>& diagnostics)
{
    if (expression.on_missing == DataErrorPolicy::default_value) {
        output = expression.default_value;
        return ReadResult::value;
    }
    if (expression.on_missing == DataErrorPolicy::omit_row) {
        return ReadResult::omit_row;
    }
    add_diagnostic(diagnostics, spec, row, target,
        fmt::format("Required source value '{}' is missing", expression.column));
    return ReadResult::reject_row;
}

ReadResult read_expression(
    const DataSpec& spec,
    const StaticTwoDA& source,
    size_t row,
    StringView target,
    const DataValueExpression& expression,
    DataScalar& output,
    Vector<DataDiagnostic>& diagnostics)
{
    if (expression.kind == DataValueKind::row_index) {
        if (row > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
            add_diagnostic(diagnostics, spec, -1, target,
                "2DA row index exceeds int32 range");
            return ReadResult::reject_row;
        }
        output = static_cast<int32_t>(row);
        return ReadResult::value;
    }
    if (expression.kind == DataValueKind::enum_value) {
        StringView raw;
        if (!source.get_to(row, expression.column, raw, false)
            || raw_missing(raw)) {
            return missing_value(spec, expression, static_cast<int32_t>(row),
                target, output, diagnostics);
        }
        const auto found = std::ranges::find_if(expression.enum_values,
            [&](const auto& value) { return string::icmp(value.first, raw); });
        if (found == expression.enum_values.end()) {
            add_diagnostic(diagnostics, spec, static_cast<int32_t>(row), target,
                fmt::format("Source value '{}' is not in enum for column '{}'",
                    raw, expression.column));
            return ReadResult::reject_row;
        }
        output = found->second;
        return ReadResult::value;
    }
    switch (expression.type) {
    case DataValueType::integer: {
        int32_t value = 0;
        if (!source.get_to(row, expression.column, value, false)) {
            return missing_value(spec, expression, static_cast<int32_t>(row),
                target, output, diagnostics);
        }
        output = value;
        return ReadResult::value;
    }
    case DataValueType::floating: {
        float value = 0.0f;
        if (!source.get_to(row, expression.column, value, false)) {
            return missing_value(spec, expression, static_cast<int32_t>(row),
                target, output, diagnostics);
        }
        if (!std::isfinite(value)) {
            add_diagnostic(diagnostics, spec, static_cast<int32_t>(row), target,
                fmt::format("Column '{}' contains a non-finite float",
                    expression.column));
            return ReadResult::reject_row;
        }
        output = value;
        return ReadResult::value;
    }
    case DataValueType::boolean: {
        int32_t value = 0;
        if (!source.get_to(row, expression.column, value, false)) {
            return missing_value(spec, expression, static_cast<int32_t>(row),
                target, output, diagnostics);
        }
        if (value != 0 && value != 1) {
            add_diagnostic(diagnostics, spec, static_cast<int32_t>(row), target,
                fmt::format(
                    "Column '{}' contains bool value {} outside [0, 1]; coerced to true",
                    expression.column, value),
                DiagnosticSeverity::warning);
        }
        output = value != 0;
        return ReadResult::value;
    }
    case DataValueType::string:
    case DataValueType::resref: {
        StringView raw;
        if (!source.get_to(row, expression.column, raw, false)
            || raw_missing(raw)) {
            return missing_value(spec, expression, static_cast<int32_t>(row),
                target, output, diagnostics);
        }
        if (expression.type == DataValueType::string) {
            output = String{raw};
        } else {
            output = Resref{raw};
        }
        return ReadResult::value;
    }
    }
    return ReadResult::reject_row;
}

ReadResult materialize_field(
    const DataSpec& spec,
    const StaticTwoDA& source,
    size_t row,
    StringView prefix,
    DataOwner owner,
    const DataFieldSpec& field,
    MaterializedDataBatch& output,
    Vector<DataDiagnostic>& diagnostics)
{
    MaterializedDataValue value;
    value.target = prefix.empty()
        ? field.target
        : fmt::format("{}.{}", prefix, field.target);
    value.owner = owner;
    value.type = field.value.type;
    value.transform = field.value.kind;
    value.source_column = field.value.column;
    const auto result = read_expression(spec, source, row, value.target,
        field.value, value.value, diagnostics);
    if (result == ReadResult::value) {
        output.values.push_back(std::move(value));
    }
    return result;
}

} // namespace

std::span<const MaterializedDataValue> MaterializedDataBatch::row_values(
    const MaterializedDataRow& row) const noexcept
{
    if (row.value_offset > values.size()
        || row.value_count > values.size() - row.value_offset) {
        return {};
    }
    return {values.data() + row.value_offset, row.value_count};
}

size_t MaterializedDataBatch::data_bytes() const noexcept
{
    size_t result = rows.capacity() * sizeof(MaterializedDataRow)
        + values.capacity() * sizeof(MaterializedDataValue)
        + config_path.capacity() + entry_type.capacity();
    for (const auto& value : values) {
        result += value.target.capacity() + value.source_column.capacity();
        if (const auto* text = std::get_if<String>(&value.value)) {
            result += text->capacity();
        }
    }
    return result;
}

bool materialize_data_rows(
    const DataSpec& spec,
    const StaticTwoDA& source,
    MaterializedDataBatch& output,
    Vector<DataDiagnostic>& diagnostics)
{
    if (!source.is_valid()) {
        add_diagnostic(diagnostics, spec, -1, {},
            fmt::format("Required source '{}.2da' is missing",
                spec.source_resource));
        return false;
    }
    if (source.rows()
        > static_cast<size_t>(std::numeric_limits<int32_t>::max()) + 1) {
        add_diagnostic(diagnostics, spec, -1, {},
            "2DA row count exceeds the int32 row-ID domain");
        return false;
    }
    if (!spec.valid_column.empty()
        && source.column_index(spec.valid_column) == StaticTwoDA::npos) {
        add_diagnostic(diagnostics, spec, -1, {},
            fmt::format("Required validity column '{}' is missing",
                spec.valid_column));
        return false;
    }
    const auto validate_columns = [&](const auto& fields) {
        for (const auto& field : fields) {
            const auto& expression = field.value;
            if ((expression.kind == DataValueKind::column
                    || expression.kind == DataValueKind::enum_value)
                && source.column_index(expression.column) == StaticTwoDA::npos
                && expression.on_missing != DataErrorPolicy::default_value) {
                add_diagnostic(diagnostics, spec, -1, field.target,
                    fmt::format("Required source column '{}' is missing",
                        expression.column));
                return false;
            }
        }
        return true;
    };
    if (!validate_columns(spec.fields)) { return false; }
    for (const auto& group : spec.field_groups) {
        if (!validate_columns(group.fields)) { return false; }
    }

    MaterializedDataBatch candidate;
    candidate.config_path = spec.config_path;
    candidate.entry_type = spec.entry_type;
    candidate.indexed_size = source.rows();
    candidate.rows.reserve(source.rows());
    size_t values_per_row = spec.fields.size();
    for (const auto& group : spec.field_groups) {
        values_per_row += group.fields.size();
    }
    if (source.rows() != 0
        && values_per_row
            > std::numeric_limits<uint32_t>::max() / source.rows()) {
        add_diagnostic(diagnostics, spec, -1, {},
            "Materialized value batch exceeds 32-bit span bounds");
        return false;
    }
    candidate.values.reserve(source.rows() * values_per_row);
    std::set<int32_t> row_ids;

    for (size_t row = 0; row < source.rows(); ++row) {
        if (!spec.valid_column.empty()) {
            StringView valid;
            const bool present = source.get_to(
                                     row, spec.valid_column, valid, false)
                && !raw_missing(valid);
            if (!present) {
                if (spec.invalid_row_policy == DataErrorPolicy::omit_row) {
                    continue;
                }
                add_diagnostic(diagnostics, spec, static_cast<int32_t>(row), {},
                    fmt::format("Validity column '{}' is missing",
                        spec.valid_column));
                continue;
            }
        }

        MaterializedDataRow materialized;
        materialized.value_offset = static_cast<uint32_t>(candidate.values.size());
        bool omit = false;
        for (const auto& field : spec.fields) {
            const auto result = materialize_field(spec, source, row, {},
                DataOwner::smalls, field, candidate, diagnostics);
            if (result == ReadResult::reject_row
                || result == ReadResult::omit_row) {
                omit = true;
                break;
            }
        }
        if (!omit) {
            for (const auto& group : spec.field_groups) {
                for (const auto& field : group.fields) {
                    const auto result = materialize_field(spec, source, row,
                        group.target, group.owner, field, candidate, diagnostics);
                    if (result == ReadResult::reject_row
                        || result == ReadResult::omit_row) {
                        omit = true;
                        break;
                    }
                }
                if (omit) { break; }
            }
        }
        if (omit) {
            candidate.values.resize(materialized.value_offset);
            continue;
        }

        const auto values = std::span{
            candidate.values.data() + materialized.value_offset,
            candidate.values.size() - materialized.value_offset};
        const auto id = std::ranges::find(values, StringView{"id"},
            &MaterializedDataValue::target);
        if (id == values.end()
            || !std::holds_alternative<int32_t>(id->value)) {
            add_diagnostic(diagnostics, spec, static_cast<int32_t>(row), "id",
                "Materialized row has no integer ID");
            return false;
        }
        materialized.id = std::get<int32_t>(id->value);
        if (materialized.id < 0 || !row_ids.insert(materialized.id).second) {
            add_diagnostic(diagnostics, spec, static_cast<int32_t>(row), "id",
                "Materialized row has a negative or duplicate ID");
            return false;
        }
        materialized.value_count = static_cast<uint32_t>(candidate.values.size())
            - materialized.value_offset;
        candidate.rows.push_back(materialized);
    }

    output = std::move(candidate);
    return true;
}

} // namespace nw::smalls
