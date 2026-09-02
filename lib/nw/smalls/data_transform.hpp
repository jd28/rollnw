#pragma once

#include "data_spec.hpp"

namespace nw {
struct StaticTwoDA;
}

namespace nw::smalls {

struct MaterializedDataValue {
    String target;
    DataOwner owner = DataOwner::smalls;
    DataValueType type = DataValueType::integer;
    DataValueKind transform = DataValueKind::column;
    String source_column;
    DataScalar value;
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

} // namespace nw::smalls
