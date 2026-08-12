#include "smalls_ui_v1.hpp"

#include "ui_v1.hpp"

#include "nw/smalls/runtime.hpp"

#include <limits>

namespace nw::toolset {

namespace {

using nw::smalls::FunctionMetadata;
using nw::smalls::ModuleInterface;
using nw::smalls::NativeFunction;
using nw::smalls::ParamMetadata;
using nw::smalls::Runtime;
using nw::smalls::Value;
using nw::smalls::ValueStorage;

bool value_to_string(Runtime& rt, const Value& value, std::string& out)
{
    if (value.type_id != rt.string_type() || value.storage != ValueStorage::heap || value.data.hptr.value == 0) {
        return false;
    }
    out = std::string(rt.get_string_view(value.data.hptr));
    return true;
}

bool value_to_int(Runtime& rt, const Value& value, int& out)
{
    if (value.type_id != rt.int_type()) {
        return false;
    }
    out = value.data.ival;
    return true;
}

bool value_to_bool(Runtime& rt, const Value& value, bool& out)
{
    if (value.type_id != rt.bool_type()) {
        return false;
    }
    out = value.data.bval;
    return true;
}

bool read_struct_string_field(Runtime& rt, const Value& value, std::string_view field, std::string& out)
{
    if (value.storage != ValueStorage::heap || value.data.hptr.value == 0) {
        return false;
    }
    const Value field_value = rt.read_struct_field(value.data.hptr, value.type_id, field);
    return value_to_string(rt, field_value, out);
}

bool read_struct_int_field(Runtime& rt, const Value& value, std::string_view field, int& out)
{
    if (value.storage != ValueStorage::heap || value.data.hptr.value == 0) {
        return false;
    }
    const Value field_value = rt.read_struct_field(value.data.hptr, value.type_id, field);
    return value_to_int(rt, field_value, out);
}

bool decode_list_config(Runtime& rt, const Value& value, UiListConfig& out)
{
    int row_height = out.row_height;
    int overscan = out.overscan;
    int columns = out.columns;
    if (!read_struct_int_field(rt, value, "row_height", row_height)) {
        return false;
    }
    if (!read_struct_int_field(rt, value, "overscan", overscan)) {
        return false;
    }
    if (!read_struct_int_field(rt, value, "columns", columns)) {
        return false;
    }
    out.row_height = row_height;
    out.overscan = overscan;
    out.columns = columns;
    return true;
}

bool decode_list_item(Runtime& rt, const Value& value, UiListItem& out)
{
    int cell_count = out.cell_count;
    int enabled_mask = out.enabled_mask;
    if (!read_struct_string_field(rt, value, "key", out.key)
        || !read_struct_string_field(rt, value, "icon_source", out.icon_source)
        || !read_struct_string_field(rt, value, "cell0", out.cells[0])
        || !read_struct_string_field(rt, value, "cell1", out.cells[1])
        || !read_struct_string_field(rt, value, "cell2", out.cells[2])
        || !read_struct_string_field(rt, value, "cell3", out.cells[3])
        || !read_struct_int_field(rt, value, "cell_count", cell_count)
        || !read_struct_int_field(rt, value, "enabled_mask", enabled_mask)
        || cell_count < 0 || cell_count > std::numeric_limits<uint8_t>::max()
        || enabled_mask < 0 || enabled_mask > std::numeric_limits<uint8_t>::max()) {
        return false;
    }
    out.cell_count = static_cast<uint8_t>(cell_count);
    out.enabled_mask = static_cast<uint8_t>(enabled_mask);
    return true;
}

bool decode_list_selection(Runtime& rt, const Value& value, UiListSelection& out)
{
    int index = out.index;
    int cell = out.cell;
    if (!read_struct_string_field(rt, value, "list_id", out.list_id)) {
        return false;
    }
    if (!read_struct_string_field(rt, value, "key", out.key)) {
        return false;
    }
    if (!read_struct_int_field(rt, value, "index", index)) {
        return false;
    }
    if (!read_struct_int_field(rt, value, "cell", cell)) {
        return false;
    }
    out.index = index;
    out.cell = cell;
    return true;
}

bool decode_list_items(Runtime& rt, const Value& value, std::vector<UiListItem>& out)
{
    if (value.storage != ValueStorage::heap || value.data.hptr.value == 0) {
        return false;
    }

    out.clear();
    const size_t count = rt.array_size(value.data.hptr);
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        Value item_value;
        if (!rt.array_get(value.data.hptr, static_cast<uint32_t>(i), item_value)) {
            return false;
        }

        UiListItem item;
        if (!decode_list_item(rt, item_value, item)) {
            return false;
        }
        out.push_back(std::move(item));
    }
    return true;
}

Value encode_list_selection(Runtime& rt, const UiListSelection& selection)
{
    const auto type_id = rt.type_id("core.ui.ListSelection", false);
    if (type_id == nw::smalls::invalid_type_id) {
        return Value{};
    }

    const auto ptr = rt.alloc_struct(type_id);
    if (ptr.value == 0) {
        return Value{};
    }

    auto make_string = [&rt](std::string_view value) {
        return Value::make_string(rt.alloc_string(value));
    };

    if (!rt.write_struct_field(ptr, type_id, "list_id", make_string(selection.list_id))
        || !rt.write_struct_field(ptr, type_id, "key", make_string(selection.key))
        || !rt.write_struct_field(ptr, type_id, "index", Value::make_int(selection.index))
        || !rt.write_struct_field(ptr, type_id, "cell", Value::make_int(selection.cell))) {
        return Value{};
    }
    return Value::make_heap(ptr, type_id);
}

FunctionMetadata make_meta(std::string_view name, nw::smalls::TypeID ret,
    std::initializer_list<std::pair<const char*, nw::smalls::TypeID>> params)
{
    FunctionMetadata meta;
    meta.name = std::string(name);
    meta.return_type = ret;
    for (const auto& [param_name, param_type] : params) {
        meta.params.push_back(ParamMetadata{std::string(param_name), param_type});
    }
    return meta;
}

} // namespace

void register_smalls_ui_v1(Runtime& rt)
{
    if (rt.get_native_module("core.ui.v1")) {
        return;
    }

    const auto cfg_type = rt.type_id("core.ui.ListConfig", false);
    const auto selection_type = rt.type_id("core.ui.ListSelection", false);

    const auto cfg_param = (cfg_type == nw::smalls::invalid_type_id) ? rt.any_type() : cfg_type;
    const auto selection_param = (selection_type == nw::smalls::invalid_type_id) ? rt.any_type() : selection_type;
    const auto selection_ret = (selection_type == nw::smalls::invalid_type_id) ? rt.any_type() : selection_type;

    auto list_create_meta = make_meta("list_create", rt.bool_type(), {
                                                                         {"list_id", rt.string_type()},
                                                                         {"config", cfg_param},
                                                                     });
    auto list_set_items_meta = make_meta("list_set_items", rt.bool_type(), {
                                                                               {"list_id", rt.string_type()},
                                                                               {"items", rt.any_array_type()},
                                                                           });
    auto list_set_selected_meta = make_meta("list_set_selected", rt.bool_type(), {
                                                                                     {"list_id", rt.string_type()},
                                                                                     {"selection", selection_param},
                                                                                 });
    auto list_get_selected_meta = make_meta("list_get_selected", selection_ret, {
                                                                                    {"list_id", rt.string_type()},
                                                                                });
    auto list_destroy_meta = make_meta("list_destroy", rt.bool_type(), {
                                                                           {"list_id", rt.string_type()},
                                                                       });
    auto list_set_visible_meta = make_meta("list_set_visible", rt.bool_type(), {
                                                                                   {"list_id", rt.string_type()},
                                                                                   {"visible", rt.bool_type()},
                                                                               });
    auto list_set_title_meta = make_meta("list_set_title", rt.bool_type(), {
                                                                               {"list_id", rt.string_type()},
                                                                               {"title", rt.string_type()},
                                                                           });
    auto list_on_refresh_meta = make_meta("list_on_refresh", rt.bool_type(), {
                                                                                 {"qualified_function", rt.string_type()},
                                                                             });

    auto list_on_hover_meta = make_meta("list_on_hover", rt.bool_type(), {
                                                                             {"list_id", rt.string_type()},
                                                                             {"function_name", rt.string_type()},
                                                                         });
    auto list_on_select_meta = make_meta("list_on_select", rt.bool_type(), {
                                                                               {"list_id", rt.string_type()},
                                                                               {"function_name", rt.string_type()},
                                                                           });
    auto list_on_activate_meta = make_meta("list_on_activate", rt.bool_type(), {
                                                                                   {"list_id", rt.string_type()},
                                                                                   {"function_name", rt.string_type()},
                                                                               });
    auto list_on_scroll_meta = make_meta("list_on_scroll", rt.bool_type(), {
                                                                               {"list_id", rt.string_type()},
                                                                               {"function_name", rt.string_type()},
                                                                           });

    ModuleInterface iface;
    iface.module_path = "core.ui.v1";
    iface.functions = {
        list_create_meta,
        list_set_items_meta,
        list_set_selected_meta,
        list_get_selected_meta,
        list_destroy_meta,
        list_set_visible_meta,
        list_set_title_meta,
        list_on_refresh_meta,
        list_on_hover_meta,
        list_on_select_meta,
        list_on_activate_meta,
        list_on_scroll_meta,
    };
    rt.register_native_interface(std::move(iface));

    rt.register_native_function(NativeFunction{
        .name = "core.ui.v1.list_create",
        .wrapper = +[](Runtime* runtime, const Value* args, uint8_t argc) -> Value {
            if (!runtime || argc != 2) {
                return Value::make_bool(false);
            }

            std::string list_id;
            UiListConfig cfg;
            if (!value_to_string(*runtime, args[0], list_id) || !decode_list_config(*runtime, args[1], cfg)) {
                return Value::make_bool(false);
            }

            return Value::make_bool(ui_v1_host().create(std::move(list_id), cfg));
        },
        .metadata = std::move(list_create_meta),
    });

    rt.register_native_function(NativeFunction{
        .name = "core.ui.v1.list_set_items",
        .wrapper = +[](Runtime* runtime, const Value* args, uint8_t argc) -> Value {
            if (!runtime || argc != 2) {
                return Value::make_bool(false);
            }

            std::string list_id;
            std::vector<UiListItem> items;
            if (!value_to_string(*runtime, args[0], list_id) || !decode_list_items(*runtime, args[1], items)) {
                return Value::make_bool(false);
            }

            return Value::make_bool(ui_v1_host().set_items(list_id, std::move(items)));
        },
        .metadata = std::move(list_set_items_meta),
    });

    rt.register_native_function(NativeFunction{
        .name = "core.ui.v1.list_set_selected",
        .wrapper = +[](Runtime* runtime, const Value* args, uint8_t argc) -> Value {
            if (!runtime || argc != 2) {
                return Value::make_bool(false);
            }

            std::string list_id;
            UiListSelection selection;
            if (!value_to_string(*runtime, args[0], list_id) || !decode_list_selection(*runtime, args[1], selection)) {
                return Value::make_bool(false);
            }
            selection.list_id = list_id;
            return Value::make_bool(ui_v1_host().set_selected(list_id, selection, true));
        },
        .metadata = std::move(list_set_selected_meta),
    });

    rt.register_native_function(NativeFunction{
        .name = "core.ui.v1.list_get_selected",
        .wrapper = +[](Runtime* runtime, const Value* args, uint8_t argc) -> Value {
            if (!runtime || argc != 1) {
                return Value{};
            }

            std::string list_id;
            if (!value_to_string(*runtime, args[0], list_id)) {
                return Value{};
            }

            const auto selected = ui_v1_host().get_selected(list_id);
            if (!selected.has_value()) {
                UiListSelection empty;
                empty.list_id = list_id;
                return encode_list_selection(*runtime, empty);
            }
            return encode_list_selection(*runtime, *selected);
        },
        .metadata = std::move(list_get_selected_meta),
    });

    rt.register_native_function(NativeFunction{
        .name = "core.ui.v1.list_destroy",
        .wrapper = +[](Runtime* runtime, const Value* args, uint8_t argc) -> Value {
            if (!runtime || argc != 1) {
                return Value::make_bool(false);
            }

            std::string list_id;
            if (!value_to_string(*runtime, args[0], list_id)) {
                return Value::make_bool(false);
            }
            return Value::make_bool(ui_v1_host().destroy(list_id));
        },
        .metadata = std::move(list_destroy_meta),
    });

    rt.register_native_function(NativeFunction{
        .name = "core.ui.v1.list_set_visible",
        .wrapper = +[](Runtime* runtime, const Value* args, uint8_t argc) -> Value {
            if (!runtime || argc != 2) {
                return Value::make_bool(false);
            }
            std::string list_id;
            bool visible = false;
            if (!value_to_string(*runtime, args[0], list_id)
                || !value_to_bool(*runtime, args[1], visible)) {
                return Value::make_bool(false);
            }
            return Value::make_bool(ui_v1_host().set_visible(list_id, visible));
        },
        .metadata = std::move(list_set_visible_meta),
    });

    rt.register_native_function(NativeFunction{
        .name = "core.ui.v1.list_set_title",
        .wrapper = +[](Runtime* runtime, const Value* args, uint8_t argc) -> Value {
            if (!runtime || argc != 2) {
                return Value::make_bool(false);
            }
            std::string list_id;
            std::string title;
            if (!value_to_string(*runtime, args[0], list_id)
                || !value_to_string(*runtime, args[1], title)) {
                return Value::make_bool(false);
            }
            return Value::make_bool(ui_v1_host().set_title(list_id, std::move(title)));
        },
        .metadata = std::move(list_set_title_meta),
    });

    rt.register_native_function(NativeFunction{
        .name = "core.ui.v1.list_on_refresh",
        .wrapper = +[](Runtime* runtime, const Value* args, uint8_t argc) -> Value {
            if (!runtime || argc != 1) {
                return Value::make_bool(false);
            }
            std::string function;
            if (!value_to_string(*runtime, args[0], function)) {
                return Value::make_bool(false);
            }
            return Value::make_bool(ui_v1_host().register_refresh_callback(std::move(function)));
        },
        .metadata = std::move(list_on_refresh_meta),
    });

    auto register_callback = [&rt](std::string_view fn_name, UiListEventType type, FunctionMetadata meta) {
        rt.register_native_function(NativeFunction{
            .name = "core.ui.v1." + std::string(fn_name),
            .wrapper = [type](Runtime* runtime, const Value* args, uint8_t argc) -> Value {
                if (!runtime || argc != 2) {
                    return Value::make_bool(false);
                }
                std::string list_id;
                std::string fn;
                if (!value_to_string(*runtime, args[0], list_id) || !value_to_string(*runtime, args[1], fn)) {
                    return Value::make_bool(false);
                }
                return Value::make_bool(ui_v1_host().set_callback(list_id, type, std::move(fn)));
            },
            .metadata = std::move(meta),
        });
    };

    register_callback("list_on_hover", UiListEventType::hover, std::move(list_on_hover_meta));
    register_callback("list_on_select", UiListEventType::select, std::move(list_on_select_meta));
    register_callback("list_on_activate", UiListEventType::activate, std::move(list_on_activate_meta));
    register_callback("list_on_scroll", UiListEventType::scroll, std::move(list_on_scroll_meta));
}

} // namespace nw::toolset
