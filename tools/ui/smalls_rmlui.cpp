#include "smalls_rmlui.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/smalls/runtime.hpp>

namespace nw::toolset {

void SmallsRmlUiHost::publish_active_object(nw::ObjectHandle object) noexcept
{
    active_object_ = object;
}

void SmallsRmlUiHost::clear_active_object() noexcept
{
    active_object_ = nw::ObjectHandle{};
}

void SmallsRmlUiHost::clear_active_object(nw::ObjectHandle object) noexcept
{
    if (active_object_ == object) {
        clear_active_object();
    }
}

nw::ObjectHandle SmallsRmlUiHost::active_object() const noexcept
{
    if (active_object_.type == nw::ObjectType::invalid) {
        return nw::ObjectHandle{};
    }
    const auto* objects = nw::kernel::services().get<nw::ObjectManager>();
    return objects && objects->valid(active_object_) ? active_object_ : nw::ObjectHandle{};
}

SmallsRmlUiHost& smalls_rmlui_host()
{
    static SmallsRmlUiHost host;
    return host;
}

void register_smalls_rmlui(nw::smalls::Runtime& runtime)
{
    if (runtime.get_native_module("core.rmlui")) {
        return;
    }

    runtime.module("core.rmlui")
        .function("active_object", +[]() -> nw::ObjectHandle { return smalls_rmlui_host().active_object(); })
        .function("active_object_type", +[]() -> int32_t { return static_cast<int32_t>(smalls_rmlui_host().active_object().type); })
        .finalize();
}

} // namespace nw::toolset
