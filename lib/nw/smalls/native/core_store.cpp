#include "../stdlib.hpp"

#include "../../objects/ObjectManager.hpp"

namespace nw::smalls {

void register_core_store(Runtime& rt)
{
    if (rt.get_native_module("core.store")) {
        return;
    }

    rt.module("core.store")
        .function("is_valid", +[](nw::ObjectHandle obj) -> bool { return nw::kernel::objects().get_object_base(obj) != nullptr; })
        .function("is_store", +[](nw::ObjectHandle obj) -> bool {
            auto* base = nw::kernel::objects().get_object_base(obj);
            return base && base->as_store(); })
        .function("as_store", +[](nw::ObjectHandle obj) -> nw::ObjectHandle {
            auto* base = nw::kernel::objects().get_object_base(obj);
            return (base && base->as_store()) ? obj : nw::ObjectHandle{}; })
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
        .function("type_id", +[]() -> int32_t { return static_cast<int32_t>(nw::ObjectType::store); })
        .finalize();
}

} // namespace nw::smalls
