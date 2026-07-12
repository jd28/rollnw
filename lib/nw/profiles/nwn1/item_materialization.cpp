#include "item_materialization.hpp"

#include "../../objects/Item.hpp"
#include "../../smalls/runtime.hpp"

#include <fmt/format.h>

#include <utility>

namespace nwn1 {

namespace {

ItemNativeMaterializationResult failed(std::string error)
{
    return ItemNativeMaterializationResult{
        .ok = false,
        .error = std::move(error),
    };
}

ItemNativeMaterializationResult succeeded()
{
    return ItemNativeMaterializationResult{
        .ok = true,
    };
}

bool item_handle_valid(nw::ObjectHandle handle) noexcept
{
    return handle.id != nw::object_invalid && handle.type == nw::ObjectType::item;
}

} // namespace

ItemNativeMaterializationResult materialize_item_native_components(
    nw::Item& item,
    nw::smalls::Runtime& rt)
{
    if (!item_handle_valid(item.handle())) {
        return failed("item native materialization requires a valid item handle");
    }

    rt.init_object_propsets(item.handle());

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nw::smalls::detail::make_value(&rt, item.handle()));
    auto result = rt.execute_script("nwn1.item", "sync_native_layout", args);
    if (!result.ok()) {
        return failed(fmt::format("nwn1.item.sync_native_layout failed: {}", result.error_message));
    }
    if (result.value.type_id != rt.bool_type() || !result.value.data.bval) {
        return failed("nwn1.item.sync_native_layout returned false");
    }

    args.clear();
    args.push_back(nw::smalls::detail::make_value(&rt, item.handle()));
    args.push_back(nw::smalls::Value::make_bool(true));
    result = rt.execute_script("nwn1.item", "update_standalone_visual", args);
    if (!result.ok()) {
        return failed(fmt::format("nwn1.item.update_standalone_visual failed: {}", result.error_message));
    }
    if (result.value.type_id != rt.bool_type() || !result.value.data.bval) {
        return failed("nwn1.item.update_standalone_visual returned false");
    }

    return succeeded();
}

} // namespace nwn1
