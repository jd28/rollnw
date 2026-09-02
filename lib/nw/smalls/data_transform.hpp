#pragma once

#include "../formats/StaticTwoDA.hpp"
#include "data_spec.hpp"

namespace nw {
struct ResourceManager;
}

namespace nw::smalls {

struct MaterializedDataValue {
    String target;
    DataOwner owner = DataOwner::smalls;
    DataValueType type = DataValueType::integer;
    DataValueKind transform = DataValueKind::column;
    String source_column;
    struct IntegerStruct {
        Vector<std::pair<String, int32_t>> fields;
    };
    using Value = std::variant<std::monostate, int32_t, float, bool, String,
        Resref, Vector<int32_t>, Vector<IntegerStruct>>;
    Value value;
};

struct DataTwoDAResource {
    String name;
    StaticTwoDA active;
    StaticTwoDA base;
};

struct DataTwoDAReferenceSet {
    String column;
    Vector<DataTwoDAResource> resources;
};

// Concrete input batch for one transform. The active primary table owns the
// output row extent. Base tables are consulted only when an entire requested
// column is absent from the corresponding active table.
struct DataSourceBatch {
    StaticTwoDA primary;
    StaticTwoDA primary_base;
    Vector<DataTwoDAReferenceSet> references;
};

struct MaterializedDataRow {
    int32_t id = -1;
    uint32_t value_offset = 0;
    uint32_t value_count = 0;
};

// Rows and values are flat contiguous batches. Row spans remain valid until
// the batch is replaced or destroyed.
struct MaterializedDataBatch {
    String config_path;
    String entry_type;
    // Indexed output extent. Structured 2DA specs require row_index identity,
    // so omitted or rejected source rows remain addressable holes.
    size_t indexed_size = 0;
    Vector<MaterializedDataRow> rows;
    Vector<MaterializedDataValue> values;

    [[nodiscard]] std::span<const MaterializedDataValue> row_values(
        const MaterializedDataRow& row) const noexcept;
    [[nodiscard]] size_t data_bytes() const noexcept;
};

bool materialize_data_rows(
    const DataSpec& spec,
    const StaticTwoDA& source,
    MaterializedDataBatch& output,
    Vector<DataDiagnostic>& diagnostics);

bool load_data_sources(
    const DataSpec& spec,
    const ResourceManager& resources,
    DataSourceBatch& output,
    Vector<DataDiagnostic>& diagnostics);

bool materialize_data_rows(
    const DataSpec& spec,
    const DataSourceBatch& sources,
    MaterializedDataBatch& output,
    Vector<DataDiagnostic>& diagnostics);

} // namespace nw::smalls
