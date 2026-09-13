#include "sound_catalog.hpp"

#include <nw/formats/StaticTwoDA.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Strings.hpp>
#include <nw/resources/ResourceManager.hpp>

#include <absl/container/flat_hash_set.h>
#include <absl/strings/ascii.h>
#include <absl/strings/strip.h>

#include <algorithm>

namespace nw::toolset {
namespace {

bool invalid_localized_name(std::string_view name)
{
    return name.empty() || name.starts_with("Bad Strref");
}

std::string make_search_text(const SoundCatalogRow& row)
{
    std::string result;
    result.reserve(row.name.size() + row.resource.length() + 1);
    result += absl::AsciiStrToLower(row.name);
    result.push_back('\n');
    result += row.resource.view();
    return result;
}

std::string sound_name(const StaticTwoDA& table, size_t row, const Resref& resource)
{
    String display_name;
    if (table.get_to(row, "DisplayName", display_name, false)
        && !display_name.empty()) {
        return display_name;
    }

    int32_t description = -1;
    if (table.get_to(row, "Description", description, false)
        && description >= 0) {
        auto localized = kernel::strings().get(
            static_cast<uint32_t>(description));
        if (!invalid_localized_name(localized)) {
            return localized;
        }
    }
    return resource.string();
}

void append_sound_row(SoundCatalog& catalog,
    absl::flat_hash_set<Resref>& seen,
    Resref resource,
    std::string name)
{
    if (resource.empty() || !seen.insert(resource).second) {
        return;
    }
    if (name.empty()) {
        name = resource.string();
    }

    SoundCatalogRow row{
        .resource = resource,
        .name = std::move(name),
    };
    row.sort_key = absl::AsciiStrToLower(row.name);
    row.search_text = make_search_text(row);
    catalog.rows.push_back(std::move(row));
}

} // namespace

bool build_sound_catalog(SoundCatalog& output)
{
    SoundCatalog result;
    absl::flat_hash_set<Resref> seen;

    StaticTwoDA table{kernel::resman().demand(
        Resource{StringView{"ambientsound"}, ResourceType::twoda})};
    if (table.is_valid()) {
        result.table_row_count = table.rows();
        result.rows.reserve(table.rows());
        seen.reserve(table.rows());
        for (size_t row = 0; row < table.rows(); ++row) {
            StringView value;
            if (!table.get_to(row, "Resource", value, false)) {
                continue;
            }
            Resref resource{value};
            append_sound_row(result, seen, resource,
                sound_name(table, row, resource));
        }
    }

    kernel::resman().visit([&](Resource resource) {
        if (resource.type != ResourceType::wav) {
            return;
        }
        ++result.visible_wav_count;
        append_sound_row(result, seen, resource.resref, {});
    });

    std::sort(result.rows.begin(), result.rows.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.sort_key != rhs.sort_key) {
            return lhs.sort_key < rhs.sort_key;
        }
        return lhs.resource < rhs.resource;
    });
    if (!table.is_valid() && result.rows.empty()) {
        result.status = SoundCatalogStatus::unavailable;
        result.diagnostic = "No ambient sound table or WAV resources are available";
        output = std::move(result);
        return false;
    }

    result.status = SoundCatalogStatus::ready;
    output = std::move(result);
    return true;
}

void filter_sound_catalog(
    const SoundCatalog& catalog, std::string_view query, std::vector<uint32_t>& output)
{
    output.clear();
    if (catalog.status != SoundCatalogStatus::ready) {
        return;
    }

    const std::string needle = absl::AsciiStrToLower(
        absl::StripAsciiWhitespace(query));
    output.reserve(catalog.rows.size());
    for (size_t index = 0; index < catalog.rows.size(); ++index) {
        if (needle.empty()
            || catalog.rows[index].search_text.find(needle)
                != std::string::npos) {
            output.push_back(static_cast<uint32_t>(index));
        }
    }
}

} // namespace nw::toolset
