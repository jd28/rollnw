#include "appearance_catalog.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/kernel/Strings.hpp>
#include <nw/resources/assets.hpp>
#include <nw/smalls/Array.hpp>
#include <nw/smalls/runtime.hpp>

#include <algorithm>
#include <cctype>
#include <limits>

namespace nw::toolset {
namespace {

struct ConfigLayout {
    const smalls::StructDef* definition = nullptr;
    uint32_t id = UINT32_MAX;
    uint32_t label = UINT32_MAX;
    uint32_t name = UINT32_MAX;
    uint32_t model = UINT32_MAX;
    uint32_t model_type = UINT32_MAX;
};

bool accessory_catalog(AppearanceCatalogKind kind) noexcept
{
    return kind == AppearanceCatalogKind::wing
        || kind == AppearanceCatalogKind::tail;
}

bool native_catalog(AppearanceCatalogKind kind) noexcept
{
    return kind == AppearanceCatalogKind::creature
        || kind == AppearanceCatalogKind::placeable;
}

std::string lower_ascii(std::string_view value)
{
    std::string result;
    result.reserve(value.size());
    for (const unsigned char ch : value) {
        result.push_back(static_cast<char>(std::tolower(ch)));
    }
    return result;
}

bool invalid_model(std::string_view model)
{
    const std::string lowered = lower_ascii(model);
    return lowered.empty() || lowered == "****" || lowered == "null" || lowered == "none";
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
    result += lower_ascii(row.name);
    result.push_back('\n');
    result += lower_ascii(row.label);
    result.push_back('\n');
    result += lower_ascii(row.model);
    result.push_back('\n');
    result += id;
    return result;
}

bool resolve_layout(smalls::Runtime& runtime,
    AppearanceCatalogKind kind,
    smalls::TypeID& type_id,
    std::string_view& config_path,
    ConfigLayout& layout,
    std::string& diagnostic)
{
    if (!runtime.load_module("nwn1.rules")) {
        diagnostic = "nwn1.rules could not be loaded";
        return false;
    }

    switch (kind) {
    case AppearanceCatalogKind::creature:
    case AppearanceCatalogKind::placeable:
        diagnostic = "Native appearance catalogs do not use a Smalls config layout";
        return false;
    case AppearanceCatalogKind::wing:
        type_id = runtime.type_id("nwn1.rules.WingModelEntry", false);
        config_path = "nwn1.data.wingmodel";
        break;
    case AppearanceCatalogKind::tail:
        type_id = runtime.type_id("nwn1.rules.TailModelEntry", false);
        config_path = "nwn1.data.tailmodel";
        break;
    }
    layout.definition = runtime.get_struct_def(type_id);
    if (type_id == smalls::invalid_type_id || !layout.definition) {
        diagnostic = "Appearance catalog schema is unavailable";
        return false;
    }

    layout.id = layout.definition->field_index("id");
    layout.label = layout.definition->field_index("label");
    layout.name = accessory_catalog(kind)
        ? UINT32_MAX
        : layout.definition->field_index("name");
    layout.model = layout.definition->field_index("model");
    layout.model_type = kind == AppearanceCatalogKind::creature
        ? layout.definition->field_index("model_type")
        : UINT32_MAX;
    if (layout.id == UINT32_MAX || layout.label == UINT32_MAX
        || layout.model == UINT32_MAX
        || (!accessory_catalog(kind) && layout.name == UINT32_MAX)
        || (kind == AppearanceCatalogKind::creature && layout.model_type == UINT32_MAX)) {
        diagnostic = "Appearance catalog schema is incomplete";
        return false;
    }
    return true;
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
    return kind == AppearanceCatalogKind::placeable || model_type >= 0;
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
    row.sort_key = lower_ascii(row.name);
    row.search_text = make_search_text(row);
    catalog.rows.push_back(std::move(row));
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
    smalls::Runtime& runtime, AppearanceCatalogKind kind, AppearanceCatalog& output)
{
    if (native_catalog(kind)) {
        return build_native_catalog(kind, output);
    }

    AppearanceCatalog result;
    result.kind = kind;

    smalls::TypeID type_id;
    std::string_view config_path;
    ConfigLayout layout;
    if (!resolve_layout(runtime, kind, type_id, config_path, layout, result.diagnostic)) {
        result.status = AppearanceCatalogStatus::unavailable;
        output = std::move(result);
        return false;
    }

    const auto array_value = runtime.load_config_array_value(config_path, type_id);
    auto* array = runtime.get_array_typed(array_value.data.hptr);
    if (array_value.type_id == smalls::invalid_type_id || !array) {
        result.status = AppearanceCatalogStatus::unavailable;
        result.diagnostic = "Smalls appearance config array is unavailable";
        output = std::move(result);
        return false;
    }
    if (array->size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        result.status = AppearanceCatalogStatus::unavailable;
        result.diagnostic = "Smalls appearance config array exceeds the supported row count";
        output = std::move(result);
        return false;
    }

    result.source_row_count = array->size();
    result.rows.reserve(array->size());
    for (size_t index = 0; index < array->size(); ++index) {
        const void* entry = array->element_data(index);
        int32_t id = -1;
        int32_t strref = -1;
        int32_t model_type = -1;
        Resref model_ref;
        smalls::ScriptString label;
        if (!runtime.read_struct_data_field(entry, layout.definition, layout.id, id)
            || (layout.name != UINT32_MAX
                && !runtime.read_struct_data_field(entry, layout.definition, layout.name, strref))
            || !runtime.read_struct_data_field(entry, layout.definition, layout.model, model_ref)
            || !runtime.read_struct_data_field(entry, layout.definition, layout.label, label)
            || (kind == AppearanceCatalogKind::creature
                && !runtime.read_struct_data_field(
                    entry, layout.definition, layout.model_type, model_type))) {
            continue;
        }

        const std::string model{model_ref.view()};
        const std::string label_text = label.ptr.value == 0
            ? std::string{}
            : std::string{label.view(runtime)};
        if (!valid_source_row(kind, id, model_type, label_text, model)) {
            continue;
        }

        std::string name;
        if (accessory_catalog(kind) && id == 0) {
            name = "None";
        } else if (strref >= 0) {
            name = kernel::strings().get(static_cast<uint32_t>(strref));
        }
        append_catalog_row(result, id, model_type, name, label_text, model);
    }

    finish_catalog(result);
    output = std::move(result);
    return true;
}

void filter_appearance_catalog(
    const AppearanceCatalog& catalog, std::string_view query, std::vector<uint32_t>& output)
{
    output.clear();
    if (catalog.status != AppearanceCatalogStatus::ready) {
        return;
    }

    const std::string needle = lower_ascii(query);
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
