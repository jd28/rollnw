#include "appearance_catalog.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/kernel/Strings.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/util/string.hpp>

#include <absl/strings/ascii.h>

#include <algorithm>
#include <limits>

namespace nw::toolset {
namespace {

bool accessory_catalog(AppearanceCatalogKind kind) noexcept
{
    return kind == AppearanceCatalogKind::wing
        || kind == AppearanceCatalogKind::tail;
}

bool invalid_model(std::string_view model)
{
    return model.empty() || nw::string::icmp(model, "****")
        || nw::string::icmp(model, "null")
        || nw::string::icmp(model, "none");
}

std::string humanize_label(std::string_view label)
{
    std::string result{label};
    std::replace(result.begin(), result.end(), '_', ' ');
    return result;
}

bool invalid_localized_name(std::string_view name)
{
    return name.empty() || name.starts_with("Bad Strref");
}

std::string make_search_text(const AppearanceCatalogRow& row)
{
    std::string result;
    const std::string id = std::to_string(row.id);
    result.reserve(row.name.size() + row.label.size() + row.model.size() + id.size() + 3);
    result += absl::AsciiStrToLower(row.name);
    result.push_back('\n');
    result += absl::AsciiStrToLower(row.label);
    result.push_back('\n');
    result += absl::AsciiStrToLower(row.model);
    result.push_back('\n');
    result += id;
    return result;
}

bool valid_source_row(AppearanceCatalogKind kind,
    int32_t id,
    int32_t model_type,
    std::string_view label,
    std::string_view model)
{
    if (id < 0) {
        return false;
    }
    if (accessory_catalog(kind)) {
        return id == 0 || !label.empty() || !model.empty();
    }
    if (invalid_model(model)) {
        return false;
    }
    return kind == AppearanceCatalogKind::placeable
        || kind == AppearanceCatalogKind::door || model_type >= 0;
}

void finish_catalog(AppearanceCatalog& catalog)
{
    std::sort(catalog.rows.begin(), catalog.rows.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.sort_key != rhs.sort_key) {
            return lhs.sort_key < rhs.sort_key;
        }
        return lhs.id < rhs.id;
    });
    catalog.status = AppearanceCatalogStatus::ready;
}

void append_catalog_row(AppearanceCatalog& catalog,
    int32_t id,
    int32_t model_type,
    std::string_view name,
    std::string_view label,
    std::string_view model)
{
    if (!valid_source_row(catalog.kind, id, model_type, label, model)) {
        return;
    }

    AppearanceCatalogRow row;
    row.id = id;
    row.model_type = model_type;
    row.name = name;
    row.label = label;
    row.model = model;
    if (invalid_localized_name(row.name)) {
        row.name = humanize_label(row.label);
    }
    if (row.name.empty()) {
        row.name = row.model;
    }
    row.sort_key = absl::AsciiStrToLower(row.name);
    row.search_text = make_search_text(row);
    catalog.rows.push_back(std::move(row));
}

template <typename Entries>
bool build_accessory_catalog(
    AppearanceCatalogKind kind, const Entries& entries, AppearanceCatalog& output)
{
    AppearanceCatalog result;
    result.kind = kind;
    result.source_row_count = entries.size();
    if (result.source_row_count
        > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
        result.status = AppearanceCatalogStatus::unavailable;
        result.diagnostic = "Native creature accessory table exceeds the supported row count";
        output = std::move(result);
        return false;
    }

    result.rows.reserve(std::max<size_t>(1, entries.size()));
    const auto* none = entries.empty() ? nullptr : &entries.front();
    append_catalog_row(result, 0, -1, "None",
        none ? none->label : std::string_view{},
        none ? none->model.view() : std::string_view{});
    for (size_t index = 1; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        if (!entry.valid()) { continue; }
        append_catalog_row(result,
            static_cast<int32_t>(index), -1, entry.editor_name(),
            entry.label, entry.model.view());
    }

    finish_catalog(result);
    output = std::move(result);
    return true;
}

bool build_native_catalog(AppearanceCatalogKind kind, AppearanceCatalog& output)
{
    AppearanceCatalog result;
    result.kind = kind;

    const auto& rules = kernel::rules();
    if (kind == AppearanceCatalogKind::creature) {
        result.source_row_count = rules.appearances.entries.size();
        if (result.source_row_count > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
            result.status = AppearanceCatalogStatus::unavailable;
            result.diagnostic = "Native creature appearance table exceeds the supported row count";
            output = std::move(result);
            return false;
        }
        result.rows.reserve(result.source_row_count);
        for (size_t index = 0; index < rules.appearances.entries.size(); ++index) {
            const auto& entry = rules.appearances.entries[index];
            if (!entry.valid()) { continue; }
            append_catalog_row(result,
                static_cast<int32_t>(index),
                static_cast<int32_t>(entry.model_type),
                kernel::strings().get(entry.string_ref),
                entry.label,
                entry.model.view());
        }
    } else if (kind == AppearanceCatalogKind::placeable) {
        result.source_row_count = rules.placeables.entries.size();
        if (result.source_row_count > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
            result.status = AppearanceCatalogStatus::unavailable;
            result.diagnostic = "Native placeable appearance table exceeds the supported row count";
            output = std::move(result);
            return false;
        }
        result.rows.reserve(result.source_row_count);
        for (size_t index = 0; index < rules.placeables.entries.size(); ++index) {
            const auto& entry = rules.placeables.entries[index];
            if (!entry.valid()) { continue; }
            append_catalog_row(result,
                static_cast<int32_t>(index),
                -1,
                kernel::strings().get(entry.string_ref),
                entry.label,
                entry.model.view());
        }
    } else if (kind == AppearanceCatalogKind::door) {
        result.source_row_count = rules.genericdoors.entries.size();
        if (result.source_row_count
            > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
            result.status = AppearanceCatalogStatus::unavailable;
            result.diagnostic = "Native generic Door table exceeds the supported row count";
            output = std::move(result);
            return false;
        }
        result.rows.reserve(result.source_row_count);
        for (size_t index = 0; index < rules.genericdoors.entries.size(); ++index) {
            const auto& entry = rules.genericdoors.entries[index];
            if (!entry.valid()
                || !kernel::resman().contains(
                    {entry.model, ResourceType::mdl})) {
                continue;
            }
            append_catalog_row(result,
                static_cast<int32_t>(index), -1, entry.editor_name(), {},
                entry.model.view());
        }
    } else if (kind == AppearanceCatalogKind::wing) {
        return build_accessory_catalog(kind, rules.wingmodels.entries, output);
    } else if (kind == AppearanceCatalogKind::tail) {
        return build_accessory_catalog(kind, rules.tailmodels.entries, output);
    } else {
        result.status = AppearanceCatalogStatus::unavailable;
        result.diagnostic = "Unsupported native appearance catalog kind";
        output = std::move(result);
        return false;
    }

    finish_catalog(result);
    output = std::move(result);
    return true;
}

} // namespace

size_t AppearanceCatalog::data_bytes() const noexcept
{
    size_t result = rows.capacity() * sizeof(AppearanceCatalogRow) + diagnostic.capacity();
    for (const auto& row : rows) {
        result += row.name.capacity();
        result += row.label.capacity();
        result += row.model.capacity();
        result += row.sort_key.capacity();
        result += row.search_text.capacity();
    }
    return result;
}

bool build_appearance_catalog(
    AppearanceCatalogKind kind, AppearanceCatalog& output)
{
    return build_native_catalog(kind, output);
}

void filter_appearance_catalog(
    const AppearanceCatalog& catalog, std::string_view query, std::vector<uint32_t>& output)
{
    output.clear();
    if (catalog.status != AppearanceCatalogStatus::ready) {
        return;
    }

    const std::string needle = absl::AsciiStrToLower(
        absl::StripAsciiWhitespace(query));
    output.reserve(catalog.rows.size());
    for (size_t index = 0; index < catalog.rows.size(); ++index) {
        if (needle.empty() || catalog.rows[index].search_text.find(needle) != std::string::npos) {
            output.push_back(static_cast<uint32_t>(index));
        }
    }
}

const AppearanceCatalogRow* find_appearance_catalog_row(
    const AppearanceCatalog& catalog, int32_t id) noexcept
{
    const auto found = std::find_if(catalog.rows.begin(), catalog.rows.end(), [id](const auto& row) {
        return row.id == id;
    });
    return found == catalog.rows.end() ? nullptr : &*found;
}

} // namespace nw::toolset
