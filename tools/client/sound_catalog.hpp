#pragma once

#include <nw/resources/assets.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nw::toolset {

enum class SoundCatalogStatus : uint8_t {
    empty,
    ready,
    unavailable,
};

struct SoundCatalogRow {
    Resref resource;
    std::string name;
    std::string sort_key;
    std::string search_text;
};

struct SoundCatalog {
    SoundCatalogStatus status = SoundCatalogStatus::empty;
    size_t table_row_count = 0;
    size_t visible_wav_count = 0;
    std::vector<SoundCatalogRow> rows;
    std::string diagnostic;
};

// Projects the complete active ambient-sound table and visible WAV registry
// into one owned, sorted authoring catalog. Invalid and empty resource names
// are dropped. Duplicate resrefs keep the first table row, so localized table
// metadata wins over a bare registry entry.
[[nodiscard]] bool build_sound_catalog(SoundCatalog& output);

// Replaces output with stable indices into catalog.rows. Empty queries select
// the complete catalog in its fixed sort order.
void filter_sound_catalog(
    const SoundCatalog& catalog, std::string_view query, std::vector<uint32_t>& output);

} // namespace nw::toolset
