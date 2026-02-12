#include "../stdlib.hpp"

#include "../../objects/ObjectManager.hpp"

namespace nw::smalls {

void register_core_area(Runtime& rt)
{
    if (rt.get_native_module("core.area")) {
        return;
    }

    rt.module("core.area")
        .function("is_valid", +[](nw::ObjectHandle obj) -> bool { return nw::kernel::objects().get_object_base(obj) != nullptr; })
        .function("is_area", +[](nw::ObjectHandle obj) -> bool {
            auto* base = nw::kernel::objects().get_object_base(obj);
            return base && base->as_area(); })
        .function("as_area", +[](nw::ObjectHandle obj) -> nw::ObjectHandle {
            auto* base = nw::kernel::objects().get_object_base(obj);
            return (base && base->as_area()) ? obj : nw::ObjectHandle{}; })
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
        .function("type_id", +[]() -> int32_t { return static_cast<int32_t>(nw::ObjectType::area); })
        .finalize();
}

} // namespace nw::smalls
