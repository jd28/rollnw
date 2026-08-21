#include "../runtime.hpp"

#include "../../kernel/Rules.hpp"

#include <algorithm>
#include <cctype>
#include <limits>

namespace nw::smalls {

namespace {

struct ScriptPlaceableAppearanceInfo {
    int32_t id = -1;
    int32_t string_ref = -1;
    nw::Resref model;
};

struct ScriptPlaceableAppearanceOption {
    int32_t id = -1;
    ScriptString label;
    ScriptString model;
    ScriptString sort_key;
};

struct PlaceableAppearanceCatalogRow {
    int32_t id = -1;
    nw::String label;
    nw::Resref model;
    nw::String sort_key;
};

nw::String lower_ascii(nw::StringView value)
{
    nw::String result;
    result.reserve(value.size());
    for (const unsigned char ch : value) {
        result.push_back(static_cast<char>(std::tolower(ch)));
    }
    return result;
}

nw::String placeable_appearance_label(const nw::PlaceableAppearanceInfo& info)
{
    nw::String result = info.editor_name();
    if (result.empty()) { result = info.model.view(); }
    return result;
}

ScriptPlaceableAppearanceInfo placeable_info(int32_t id)
{
    const auto* info = nw::kernel::rules().placeables.get(
        nw::PlaceableAppearance::make(id));
    if (!info) { return {}; }
    return {
        .id = id,
        .string_ref = info->string_ref
                <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max())
            ? static_cast<int32_t>(info->string_ref)
            : -1,
        .model = info->model,
    };
}

bool placeable_model_available(const nw::PlaceableAppearanceInfo& info)
{
    return info.valid()
        && nw::kernel::resman().contains({info.model, nw::ResourceType::mdl});
}

Value make_placeable_appearance_option(
    Runtime& runtime, const PlaceableAppearanceCatalogRow& row)
{
    Runtime::ScopedRoots roots{runtime, 3};
    const auto label_value = Value::make_string(runtime.alloc_string(row.label));
    roots.add(label_value);
    const auto model_value = Value::make_string(runtime.alloc_string(row.model.view()));
    roots.add(model_value);
    const auto sort_key_value = Value::make_string(runtime.alloc_string(row.sort_key));
    roots.add(sort_key_value);

    return detail::make_value(&runtime, ScriptPlaceableAppearanceOption{
                                            .id = row.id,
                                            .label = ScriptString{label_value.data.hptr},
                                            .model = ScriptString{model_value.data.hptr},
                                            .sort_key = ScriptString{sort_key_value.data.hptr},
                                        });
}

Value placeable_appearance_option(int32_t id)
{
    auto& runtime = nw::kernel::runtime();
    const auto* info = nw::kernel::rules().placeables.get(
        nw::PlaceableAppearance::make(id));
    if (!info || !placeable_model_available(*info)) {
        const PlaceableAppearanceCatalogRow unavailable{
            .id = -1,
            .label = "Unavailable",
        };
        return make_placeable_appearance_option(runtime, unavailable);
    }

    auto label = placeable_appearance_label(*info);
    const PlaceableAppearanceCatalogRow row{
        .id = id,
        .label = label,
        .model = info->model,
        .sort_key = lower_ascii(label),
    };
    return make_placeable_appearance_option(runtime, row);
}

Value available_placeable_appearance_options()
{
    auto& runtime = nw::kernel::runtime();
    const auto& entries = nw::kernel::rules().placeables.entries;
    const size_t count = std::min(entries.size(),
        static_cast<size_t>(std::numeric_limits<int32_t>::max()));

    Vector<PlaceableAppearanceCatalogRow> rows;
    rows.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        if (!placeable_model_available(entries[index])) { continue; }
        auto label = placeable_appearance_label(entries[index]);
        rows.push_back({
            .id = static_cast<int32_t>(index),
            .label = label,
            .model = entries[index].model,
            .sort_key = lower_ascii(label),
        });
    }
    std::sort(rows.begin(), rows.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.sort_key != rhs.sort_key) { return lhs.sort_key < rhs.sort_key; }
        return lhs.id < rhs.id;
    });

    const TypeID option_type = runtime.type_id(
        "core.placeable.PlaceableAppearanceOption");
    if (option_type == invalid_type_id) { return {}; }

    const HeapPtr array_ptr = runtime.create_array_typed(option_type, rows.size());
    auto* array = runtime.get_array_typed(array_ptr);
    if (!array) { return {}; }
    const Value array_value = Value::make_heap(
        array_ptr, runtime.heap_.get_header(array_ptr)->type_id);
    Runtime::ScopedRoots array_roots{runtime, 1};
    array_roots.add(array_value);

    for (const auto& row : rows) {
        Runtime::ScopedRoots row_roots{runtime, 1};
        const auto value = make_placeable_appearance_option(runtime, row);
        row_roots.add(value);
        array->append_value(value, runtime);
    }
    return array_value;
}

} // namespace

void register_core_placeable(Runtime& rt)
{
    if (rt.get_native_module("core.placeable")) { return; }

    rt.module("core.placeable")
        .native_struct<ScriptPlaceableAppearanceInfo>("PlaceableAppearanceInfo")
        .field("id", &ScriptPlaceableAppearanceInfo::id)
        .field("string_ref", &ScriptPlaceableAppearanceInfo::string_ref)
        .field("model", &ScriptPlaceableAppearanceInfo::model)
        .end_struct()
        .native_struct<ScriptPlaceableAppearanceOption>("PlaceableAppearanceOption")
        .field("id", &ScriptPlaceableAppearanceOption::id)
        .field("label", &ScriptPlaceableAppearanceOption::label)
        .field("model", &ScriptPlaceableAppearanceOption::model)
        .field("sort_key", &ScriptPlaceableAppearanceOption::sort_key)
        .end_struct()
        .function("placeable_info", &placeable_info)
        .function("placeable_appearance_option", &placeable_appearance_option)
        .function("available_placeable_appearance_options", &available_placeable_appearance_options)
        .finalize();
}

} // namespace nw::smalls
