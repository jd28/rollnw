#include "../stdlib.hpp"

#include "../../objects/ObjectManager.hpp"

namespace nw::smalls {

void register_core_area(Runtime& rt)
{
    if (rt.get_native_module("core.area")) { return; }

    rt.module("core.area")
        .function("get_width", +[](nw::ObjectHandle obj) -> int32_t {
            auto* base = nw::kernel::objects().get_object_base(obj);
            auto* area = base ? base->as_area() : nullptr;
            return area ? area->width : 0; })
        .function("get_height", +[](nw::ObjectHandle obj) -> int32_t {
            auto* base = nw::kernel::objects().get_object_base(obj);
            auto* area = base ? base->as_area() : nullptr;
            return area ? area->height : 0; })
        .function("get_tile_count", +[](nw::ObjectHandle obj) -> int32_t {
            auto* base = nw::kernel::objects().get_object_base(obj);
            auto* area = base ? base->as_area() : nullptr;
            return area ? static_cast<int32_t>(area->tiles.size()) : 0; })
        .finalize();
}

} // namespace nw::smalls
