#include "../runtime.hpp"

#include "../../objects/ObjectManager.hpp"

namespace nw::smalls {

void register_core_store(Runtime& rt)
{
    if (rt.get_native_module("core.store")) {
        return;
    }

    rt.module("core.store")
        .function("get_gold", +[](nw::ObjectHandle obj) -> int32_t {
            auto* base = nw::kernel::objects().get_object_base(obj);
            auto* store = base ? base->as_store() : nullptr;
            return store ? store->gold : 0; })
        .function("get_max_price", +[](nw::ObjectHandle obj) -> int32_t {
            auto* base = nw::kernel::objects().get_object_base(obj);
            auto* store = base ? base->as_store() : nullptr;
            return store ? store->max_price : 0; })
        .function("is_blackmarket", +[](nw::ObjectHandle obj) -> bool {
            auto* base = nw::kernel::objects().get_object_base(obj);
            auto* store = base ? base->as_store() : nullptr;
            return store && store->blackmarket; })
        .finalize();
}

} // namespace nw::smalls
