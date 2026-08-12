#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nw::smalls {
struct Runtime;
}

namespace nw::toolset {

enum class AppearanceCatalogKind : uint8_t {
    creature,
    placeable,
    wing,
    tail,
};

enum class AppearanceCatalogStatus : uint8_t {
    empty,
    ready,
    unavailable,
};

struct AppearanceCatalogRow {
    int32_t id = -1;
    int32_t model_type = -1;
    std::string name;
    std::string label;
    std::string model;
    std::string sort_key;
    std::string search_text;
};

struct AppearanceCatalog {
    AppearanceCatalogKind kind = AppearanceCatalogKind::creature;
    AppearanceCatalogStatus status = AppearanceCatalogStatus::empty;
    size_t source_row_count = 0;
    std::vector<AppearanceCatalogRow> rows;
    std::string diagnostic;

    [[nodiscard]] size_t data_bytes() const noexcept;
};

// Projects one loaded rules table into ascending authoring rows. Creature and
// placeable rows come from native rules arrays; accessory rows come from their
// cached Smalls config arrays. The output owns every string and remains valid
// until it is replaced or cleared. Invalid source rows are dropped; any source
// failure replaces output with an unavailable catalog and a diagnostic.
[[nodiscard]] bool build_appearance_catalog(
    smalls::Runtime& runtime, AppearanceCatalogKind kind, AppearanceCatalog& output);

// Filters a complete catalog batch into stable row indices. The output is
// replaced on every call and indices remain valid only while catalog.rows is
// unchanged. Empty queries select every row in the catalog's fixed sort order.
void filter_appearance_catalog(
    const AppearanceCatalog& catalog, std::string_view query, std::vector<uint32_t>& output);

[[nodiscard]] const AppearanceCatalogRow* find_appearance_catalog_row(
    const AppearanceCatalog& catalog, int32_t id) noexcept;

} // namespace nw::toolset
