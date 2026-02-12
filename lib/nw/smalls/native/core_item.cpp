#include "../stdlib.hpp"

#include "../../objects/ObjectManager.hpp"

namespace nw::smalls {

namespace {

nw::Item* as_item(nw::ObjectHandle obj)
{
    auto* base = nw::kernel::objects().get_object_base(obj);
    if (!base) {
        return nullptr;
    }
    return base->as_item();
}

} // namespace

void register_core_item(Runtime& rt)
{
    if (rt.get_native_module("core.item")) {
        return;
    }

    rt.module("core.item")
        .function("property_count", +[](nw::ObjectHandle item) -> int32_t {
            auto* it = as_item(item);
            if (!it) {
                return 0;
            }
            return static_cast<int32_t>(it->properties.size()); })
        .function("clear_properties", +[](nw::ObjectHandle item) {
            auto* it = as_item(item);
            if (!it) {
                return;
            }
            it->properties.clear(); })
        .finalize();
}

} // namespace nw::smalls
