#include "data_transform.hpp"

#include "../resources/ResourceManager.hpp"
#include "../util/string.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <fmt/format.h>

namespace nw::smalls {
namespace {

enum class ReadResult : uint8_t { value,
    omit_row,
    reject_row };

struct ColumnSource {
    const StaticTwoDA* table = nullptr;
    size_t column = StaticTwoDA::npos;
    explicit operator bool() const noexcept
    {
        return table && column != StaticTwoDA::npos;
    }
};

void add_diagnostic(Vector<DataDiagnostic>& diagnostics, const DataSpec& spec,
    int32_t row, StringView target, String message,
    DiagnosticSeverity severity = DiagnosticSeverity::error)
{
    diagnostics.push_back({spec.source_path, row, String{target},
        std::move(message), severity});
}

ColumnSource resolve_column(const StaticTwoDA& active,
    const StaticTwoDA& base, StringView column)
{
    const size_t active_index = active.column_index(column);
    if (active_index != StaticTwoDA::npos) {
        return {&active, active_index};
    }
    const size_t base_index = base.column_index(column);
    return base_index == StaticTwoDA::npos
        ? ColumnSource{}
        : ColumnSource{&base, base_index};
}

template <typename T>
bool read_column(ColumnSource source, size_t row, T& output)
{
    return source && row < source.table->rows()
        && source.table->get_to(row, source.column, output);
}

MaterializedDataValue::Value scalar_value(const DataScalar& value)
{
    return std::visit([](const auto& item) -> MaterializedDataValue::Value {
        return item;
    },
        value);
}

ReadResult missing_value(const DataSpec& spec,
    const DataValueExpression& expression, int32_t row, StringView target,
    MaterializedDataValue::Value& output,
    Vector<DataDiagnostic>& diagnostics)
{
    if (expression.on_missing == DataErrorPolicy::default_value) {
        output = scalar_value(expression.default_value);
        return ReadResult::value;
    }
    if (expression.on_missing == DataErrorPolicy::omit_row) {
        return ReadResult::omit_row;
    }
    add_diagnostic(diagnostics, spec, row, target,
        fmt::format("Required source value '{}' is missing",
            expression.column));
    return ReadResult::reject_row;
}

const DataTwoDAReferenceSet* find_reference_set(
    const DataSourceBatch& sources, StringView column)
{
    const auto found = std::ranges::find(
        sources.references, column, &DataTwoDAReferenceSet::column);
    return found == sources.references.end() ? nullptr : &*found;
}

const DataTwoDAResource* find_reference_resource(
    const DataSourceBatch& sources, StringView column, StringView name)
{
    const auto* set = find_reference_set(sources, column);
    if (!set) { return nullptr; }
    const auto found = std::lower_bound(set->resources.begin(),
        set->resources.end(), name,
        [](const DataTwoDAResource& resource, StringView key) {
            return resource.name < key;
        });
    return found == set->resources.end() || found->name != name
        ? nullptr
        : &*found;
}

int32_t integer_default(const DataValueExpression& expression)
{
    if (const auto* value = std::get_if<int32_t>(&expression.default_value)) {
        return *value;
    }
    return 0;
}

ReadResult read_expression(const DataSpec& spec,
    const DataSourceBatch& sources, const StaticTwoDA& active,
    const StaticTwoDA& base, size_t row, StringView target,
    const DataValueExpression& expression,
    MaterializedDataValue::Value& output,
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
    if (expression.kind == DataValueKind::constant) {
        output = scalar_value(expression.default_value);
        return ReadResult::value;
    }

    const ColumnSource column = expression.column.empty()
        ? ColumnSource{}
        : resolve_column(active, base, expression.column);
    if (expression.kind == DataValueKind::enum_value) {
        StringView raw;
        if (!read_column(column, row, raw)) {
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

    if (expression.kind == DataValueKind::reference_index) {
        StringView name;
        if (!read_column(column, row, name)) {
            return missing_value(spec, expression, static_cast<int32_t>(row),
                target, output, diagnostics);
        }
        const auto* set = find_reference_set(sources,
            expression.reference_column);
        if (set) {
            const auto found = std::lower_bound(set->resources.begin(),
                set->resources.end(), name,
                [](const DataTwoDAResource& resource, StringView key) {
                    return resource.name < key;
                });
            if (found != set->resources.end() && found->name == name) {
                output = static_cast<int32_t>(
                    std::distance(set->resources.begin(), found));
                return ReadResult::value;
            }
        }
        return missing_value(spec, expression, static_cast<int32_t>(row),
            target, output, diagnostics);
    }

    if (expression.kind == DataValueKind::fixed_array) {
        Vector<int32_t> values;
        values.reserve(expression.columns.size());
        for (const auto& name : expression.columns) {
            int32_t value = integer_default(expression);
            if (!read_column(resolve_column(active, base, name), row, value)
                && expression.on_missing != DataErrorPolicy::default_value) {
                return missing_value(spec, expression,
                    static_cast<int32_t>(row), target, output, diagnostics);
            }
            values.push_back(value);
        }
        output = std::move(values);
        return ReadResult::value;
    }

    if (expression.kind == DataValueKind::indirect_grid) {
        const int32_t default_value = integer_default(expression);
        Vector<int32_t> values(
            static_cast<size_t>(expression.array_size), default_value);
        StringView resource_name;
        if (!read_column(column, row, resource_name)) {
            if (expression.on_missing == DataErrorPolicy::default_value) {
                output = std::move(values);
                return ReadResult::value;
            }
            return missing_value(spec, expression, static_cast<int32_t>(row),
                target, output, diagnostics);
        }
        const auto* resource = find_reference_resource(
            sources, expression.column, resource_name);
        if (!resource || !resource->active.is_valid()) {
            add_diagnostic(diagnostics, spec, static_cast<int32_t>(row), target,
                fmt::format("Referenced 2DA '{}' is missing; using defaults",
                    resource_name),
                DiagnosticSeverity::warning);
            output = std::move(values);
            return ReadResult::value;
        }

        Vector<ColumnSource> grid_columns;
        grid_columns.reserve(static_cast<size_t>(expression.column_count));
        for (int32_t index = 0; index < expression.column_count; ++index) {
            grid_columns.push_back(resolve_column(resource->active,
                resource->base,
                fmt::format("{}{}", expression.column_prefix, index)));
        }
        const ColumnSource limit = expression.limit_column.empty()
            ? ColumnSource{}
            : resolve_column(resource->active, resource->base,
                  expression.limit_column);
        const size_t output_rows = values.size()
            / static_cast<size_t>(expression.column_count);
        const size_t rows = std::min(resource->active.rows(), output_rows);
        for (size_t grid_row = 0; grid_row < rows; ++grid_row) {
            int32_t column_limit = expression.column_count;
            if (!expression.limit_column.empty()
                && !read_column(limit, grid_row, column_limit)) {
                column_limit = 0;
            }
            column_limit = std::clamp(
                column_limit, int32_t{0}, expression.column_count);
            for (int32_t grid_column = 0; grid_column < column_limit;
                ++grid_column) {
                int32_t value = default_value;
                read_column(grid_columns[static_cast<size_t>(grid_column)],
                    grid_row, value);
                values[grid_row * static_cast<size_t>(expression.column_count)
                    + static_cast<size_t>(grid_column)] = value;
            }
        }
        output = std::move(values);
        return ReadResult::value;
    }

    if (expression.kind == DataValueKind::column_array) {
        Vector<int32_t> values(active.rows(), integer_default(expression));
        for (size_t index = 0; index < active.rows(); ++index) {
            read_column(column, index, values[index]);
        }
        output = std::move(values);
        return ReadResult::value;
    }

    if (expression.kind == DataValueKind::struct_array) {
        Vector<MaterializedDataValue::IntegerStruct> values;
        values.reserve(expression.columns.size());
        for (const auto& name : expression.columns) {
            int32_t source_value = -1;
            if (!read_column(resolve_column(active, base, name), row,
                    source_value)
                || source_value < 0) {
                continue;
            }
            MaterializedDataValue::IntegerStruct element;
            element.fields.reserve(
                expression.element_constant_fields.size() + 1);
            element.fields.push_back(
                {expression.element_value_field, source_value});
            element.fields.insert(element.fields.end(),
                expression.element_constant_fields.begin(),
                expression.element_constant_fields.end());
            values.push_back(std::move(element));
        }
        output = std::move(values);
        return ReadResult::value;
    }

    switch (expression.type) {
    case DataValueType::integer: {
        int32_t value = 0;
        if (!read_column(column, row, value)) {
            return missing_value(spec, expression, static_cast<int32_t>(row),
                target, output, diagnostics);
        }
        output = value;
        return ReadResult::value;
    }
    case DataValueType::floating: {
        float value = 0.0f;
        if (!read_column(column, row, value)) {
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
        if (!read_column(column, row, value)) {
            return missing_value(spec, expression, static_cast<int32_t>(row),
                target, output, diagnostics);
        }
        if (value != 0 && value != 1 && expression.warn_on_bool_coerce) {
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
        if (!read_column(column, row, raw)) {
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

ReadResult materialize_field(const DataSpec& spec,
    const DataSourceBatch& sources, const StaticTwoDA& active,
    const StaticTwoDA& base, size_t row, StringView prefix, DataOwner owner,
    const DataFieldSpec& field, MaterializedDataBatch& output,
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
    const auto result = read_expression(spec, sources, active, base, row,
        value.target, field.value, value.value, diagnostics);
    if (result == ReadResult::value) {
        output.values.push_back(std::move(value));
    }
    return result;
}

void collect_reference_columns(const DataSpec& spec, Vector<String>& output)
{
    const auto add = [&](StringView column) {
        if (!column.empty()
            && std::ranges::find(output, column) == output.end()) {
            output.emplace_back(column);
        }
    };
    if (spec.source_kind == DataSourceKind::twoda_references) {
        add(spec.source_reference_column);
    }
    const auto inspect = [&](const auto& fields) {
        for (const auto& field : fields) {
            if (field.value.kind == DataValueKind::reference_index) {
                add(field.value.reference_column);
            } else if (field.value.kind == DataValueKind::indirect_grid) {
                add(field.value.column);
            }
        }
    };
    inspect(spec.fields);
    for (const auto& group : spec.field_groups) {
        inspect(group.fields);
    }
}

bool materialize_impl(const DataSpec& spec, const DataSourceBatch& sources,
    const StaticTwoDA& primary, const StaticTwoDA& primary_base,
    MaterializedDataBatch& output, Vector<DataDiagnostic>& diagnostics)
{
    if (!primary.is_valid()) {
        add_diagnostic(diagnostics, spec, -1, {},
            fmt::format("Required source '{}.2da' is missing",
                spec.source_resource));
        return false;
    }

    const DataTwoDAReferenceSet* source_references = nullptr;
    size_t row_count = primary.rows();
    if (spec.source_kind == DataSourceKind::twoda_references) {
        source_references = find_reference_set(
            sources, spec.source_reference_column);
        if (!source_references) {
            add_diagnostic(diagnostics, spec, -1, {},
                "Required reference source batch is missing");
            return false;
        }
        row_count = source_references->resources.size();
    }
    if (row_count
        > static_cast<size_t>(std::numeric_limits<int32_t>::max()) + 1) {
        add_diagnostic(diagnostics, spec, -1, {},
            "2DA row count exceeds the int32 row-ID domain");
        return false;
    }

    const ColumnSource valid_column = spec.valid_column.empty()
        ? ColumnSource{}
        : resolve_column(primary, primary_base, spec.valid_column);
    const ColumnSource valid_positive = spec.valid_positive_int_column.empty()
        ? ColumnSource{}
        : resolve_column(primary, primary_base,
              spec.valid_positive_int_column);
    if (!spec.valid_column.empty() && !valid_column) {
        add_diagnostic(diagnostics, spec, -1, {},
            fmt::format("Required validity column '{}' is missing",
                spec.valid_column));
        return false;
    }
    if (!spec.valid_positive_int_column.empty() && !valid_positive) {
        add_diagnostic(diagnostics, spec, -1, {},
            fmt::format("Required positive-int validity column '{}' is missing",
                spec.valid_positive_int_column));
        return false;
    }

    if (spec.source_kind == DataSourceKind::twoda) {
        const auto validate = [&](const auto& fields) {
            for (const auto& field : fields) {
                const auto& expression = field.value;
                if (expression.on_missing == DataErrorPolicy::default_value
                    || expression.kind == DataValueKind::row_index
                    || expression.kind == DataValueKind::constant
                    || expression.kind == DataValueKind::struct_array) {
                    continue;
                }
                Vector<StringView> columns;
                if (expression.kind == DataValueKind::fixed_array) {
                    columns.reserve(expression.columns.size());
                    for (const auto& name : expression.columns) {
                        columns.push_back(name);
                    }
                } else if (!expression.column.empty()) {
                    columns.push_back(expression.column);
                }
                for (StringView name : columns) {
                    if (!resolve_column(primary, primary_base, name)) {
                        add_diagnostic(diagnostics, spec, -1, field.target,
                            fmt::format("Required source column '{}' is missing",
                                name));
                        return false;
                    }
                }
            }
            return true;
        };
        if (!validate(spec.fields)) { return false; }
        for (const auto& group : spec.field_groups) {
            if (!validate(group.fields)) { return false; }
        }
    }

    MaterializedDataBatch candidate;
    candidate.config_path = spec.config_path;
    candidate.entry_type = spec.entry_type;
    candidate.indexed_size = row_count;
    candidate.rows.reserve(row_count);
    size_t values_per_row = spec.fields.size();
    for (const auto& group : spec.field_groups) {
        values_per_row += group.fields.size();
    }
    if (row_count != 0
        && values_per_row
            > std::numeric_limits<uint32_t>::max() / row_count) {
        add_diagnostic(diagnostics, spec, -1, {},
            "Materialized value batch exceeds 32-bit span bounds");
        return false;
    }
    candidate.values.reserve(row_count * values_per_row);

    for (size_t row = 0; row < row_count; ++row) {
        const StaticTwoDA* row_source = &primary;
        const StaticTwoDA* row_base = &primary_base;
        if (source_references) {
            const auto& resource = source_references->resources[row];
            if (!resource.active.is_valid()) {
                add_diagnostic(diagnostics, spec, static_cast<int32_t>(row), {},
                    fmt::format("Referenced 2DA '{}' is missing",
                        resource.name));
                continue;
            }
            row_source = &resource.active;
            row_base = &resource.base;
        } else {
            StringView valid;
            if (valid_column && !read_column(valid_column, row, valid)) {
                if (spec.invalid_row_policy == DataErrorPolicy::omit_row) {
                    continue;
                }
                add_diagnostic(diagnostics, spec, static_cast<int32_t>(row), {},
                    fmt::format("Validity column '{}' is missing",
                        spec.valid_column));
                continue;
            }
            int32_t positive = 0;
            if (valid_positive
                && (!read_column(valid_positive, row, positive)
                    || positive <= 0)) {
                continue;
            }
        }

        MaterializedDataRow materialized;
        materialized.value_offset = static_cast<uint32_t>(candidate.values.size());
        bool omit = false;
        for (const auto& field : spec.fields) {
            const auto result = materialize_field(spec, sources, *row_source,
                *row_base, row, {}, DataOwner::smalls, field, candidate,
                diagnostics);
            if (result != ReadResult::value) {
                omit = true;
                break;
            }
        }
        if (!omit) {
            for (const auto& group : spec.field_groups) {
                for (const auto& field : group.fields) {
                    const auto result = materialize_field(spec, sources,
                        *row_source, *row_base, row, group.target, group.owner,
                        field, candidate, diagnostics);
                    if (result != ReadResult::value) {
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
        materialized.value_count = static_cast<uint32_t>(candidate.values.size())
            - materialized.value_offset;
        candidate.rows.push_back(materialized);
    }

    output = std::move(candidate);
    return true;
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
        if (const auto* item = std::get_if<String>(&value.value)) {
            result += item->capacity();
        } else if (const auto* items = std::get_if<Vector<int32_t>>(&value.value)) {
            result += items->capacity() * sizeof(int32_t);
        } else if (const auto* structs = std::get_if<
                       Vector<MaterializedDataValue::IntegerStruct>>(
                       &value.value)) {
            result += structs->capacity()
                * sizeof(MaterializedDataValue::IntegerStruct);
            for (const auto& element : *structs) {
                result += element.fields.capacity()
                    * sizeof(std::pair<String, int32_t>);
                for (const auto& field : element.fields) {
                    result += field.first.capacity();
                }
            }
        }
    }
    return result;
}

bool load_data_sources(const DataSpec& spec,
    const ResourceManager& resources, DataSourceBatch& output,
    Vector<DataDiagnostic>& diagnostics)
{
    DataSourceBatch candidate;
    candidate.primary = StaticTwoDA{resources.demand(
        {spec.source_resource, ResourceType::twoda})};
    if (!candidate.primary.is_valid()) {
        add_diagnostic(diagnostics, spec, -1, {},
            fmt::format("Required source '{}.2da' is missing",
                spec.source_resource));
        return false;
    }
    candidate.primary_base = StaticTwoDA{
        resources.demand_base_twoda(Resref{spec.source_resource})};

    Vector<String> reference_columns;
    collect_reference_columns(spec, reference_columns);
    candidate.references.reserve(reference_columns.size());
    for (const auto& reference_column : reference_columns) {
        DataTwoDAReferenceSet set;
        set.column = reference_column;
        const ColumnSource column = resolve_column(candidate.primary,
            candidate.primary_base, reference_column);
        if (!column) {
            if (spec.source_kind == DataSourceKind::twoda_references
                && reference_column == spec.source_reference_column) {
                add_diagnostic(diagnostics, spec, -1, {},
                    fmt::format("Required reference column '{}' is missing",
                        reference_column));
                return false;
            }
            candidate.references.push_back(std::move(set));
            continue;
        }

        Vector<String> names;
        names.reserve(candidate.primary.rows());
        for (size_t row = 0; row < candidate.primary.rows(); ++row) {
            String name;
            if (read_column(column, row, name) && !name.empty()) {
                names.push_back(std::move(name));
            }
        }
        std::ranges::sort(names);
        const auto unique_end = std::unique(names.begin(), names.end());
        names.erase(unique_end, names.end());

        set.resources.reserve(names.size());
        for (auto& name : names) {
            DataTwoDAResource resource;
            resource.name = std::move(name);
            resource.active = StaticTwoDA{resources.demand(
                {resource.name, ResourceType::twoda})};
            resource.base = StaticTwoDA{
                resources.demand_base_twoda(Resref{resource.name})};
            set.resources.push_back(std::move(resource));
        }
        candidate.references.push_back(std::move(set));
    }

    output = std::move(candidate);
    return true;
}

bool materialize_data_rows(const DataSpec& spec, const StaticTwoDA& source,
    MaterializedDataBatch& output, Vector<DataDiagnostic>& diagnostics)
{
    DataSourceBatch sources;
    return materialize_impl(
        spec, sources, source, StaticTwoDA{}, output, diagnostics);
}

bool materialize_data_rows(const DataSpec& spec,
    const DataSourceBatch& sources, MaterializedDataBatch& output,
    Vector<DataDiagnostic>& diagnostics)
{
    return materialize_impl(spec, sources, sources.primary,
        sources.primary_base, output, diagnostics);
}

} // namespace nw::smalls
