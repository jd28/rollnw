#include "../stdlib.hpp"

#include "../../objects/ObjectManager.hpp"

namespace nw::smalls {

void register_core_module(Runtime& rt)
{
    if (rt.get_native_module("core.module")) {
        return;
    }

    rt.module("core.module")
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
        .finalize();
}

} // namespace nw::smalls
