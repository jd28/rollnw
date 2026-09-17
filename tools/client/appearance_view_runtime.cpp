#include "appearance_view.hpp"
#include "renderer.hpp"
#include <nw/kernel/Kernel.hpp>
#include <nw/objects/ObjectManager.hpp>
namespace nw::toolset {
namespace {
nw::ObjectHandle desired_appearance_body_preview(ObjectWorkbenchTarget target) noexcept
{
    if (target.surface == ObjectWorkbenchSurface::appearance
        && (target.matches_active_tab && target.details_ready)
        && target.object.type == nw::ObjectType::creature) {
        return target.object;
    }
    return nw::ObjectHandle{};
}

bool refresh_active_viewer_object_visual(
    ClientRenderer& renderer, nw::ObjectHandle object)
{
    if (renderer.active_viewer_object() != object) {
        return true;
    }
    return renderer.refresh_live_viewer_object_visual(object);
}

} // namespace
bool sync_appearance_body_preview(ClientRenderer& renderer, AppearanceViewState& state, ObjectWorkbenchTarget target)
{
    const nw::ObjectHandle desired = desired_appearance_body_preview(target);
    const nw::ObjectHandle current = state.appearance_body_preview_object;
    if (current == desired) {
        return true;
    }

    if (current.type != nw::ObjectType::invalid) {
        if (nw::kernel::objects().valid(current)) {
            if (!update_appearance_preview_rows(current, true)) {
                return false;
            }
            if (!refresh_active_viewer_object_visual(renderer, current)) {
                return false;
            }
        }
        state.appearance_body_preview_object = nw::ObjectHandle{};
    }

    if (desired.type == nw::ObjectType::invalid) {
        return true;
    }
    if (!update_appearance_preview_rows(desired, false)) {
        return false;
    }
    state.appearance_body_preview_object = desired;
    return refresh_active_viewer_object_visual(renderer, desired);
}

} // namespace nw::toolset
