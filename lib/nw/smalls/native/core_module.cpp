#include "../stdlib.hpp"

#include "../../objects/ObjectManager.hpp"

namespace nw::smalls {

void register_core_module(Runtime& rt)
{
    if (rt.get_native_module("core.module")) {
        return;
    }

    rt.module("core.module")
        .function("is_valid", +[](nw::ObjectHandle obj) -> bool { return nw::kernel::objects().get_object_base(obj) != nullptr; })
        .function("is_module", +[](nw::ObjectHandle obj) -> bool {
            auto* base = nw::kernel::objects().get_object_base(obj);
            return base && base->as_module(); })
        .function("as_module", +[](nw::ObjectHandle obj) -> nw::ObjectHandle {
            auto* base = nw::kernel::objects().get_object_base(obj);
            return (base && base->as_module()) ? obj : nw::ObjectHandle{}; })
        .function("get_area_count", +[](nw::ObjectHandle obj) -> int32_t {
            auto* base = nw::kernel::objects().get_object_base(obj);
            auto* mod = base ? base->as_module() : nullptr;
            return mod ? static_cast<int32_t>(mod->area_count()) : 0; })
        .function("get_hak_count", +[](nw::ObjectHandle obj) -> int32_t {
            auto* base = nw::kernel::objects().get_object_base(obj);
            auto* mod = base ? base->as_module() : nullptr;
            return mod ? static_cast<int32_t>(mod->haks.size()) : 0; })
        .function("is_save_game", +[](nw::ObjectHandle obj) -> bool {
            auto* base = nw::kernel::objects().get_object_base(obj);
            auto* mod = base ? base->as_module() : nullptr;
            return mod && mod->is_save_game; })
        .function("type_id", +[]() -> int32_t { return static_cast<int32_t>(nw::ObjectType::module); })
        .finalize();
}

} // namespace nw::smalls
