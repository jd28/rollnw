#pragma once

#include <nw/objects/ObjectHandle.hpp>

namespace nw::smalls {
struct Runtime;
}

namespace nw::toolset {

// True singleton: rollnw client has one UI thread, one kernel runtime, and one
// preview selection. Batch semantics do not apply to this process-wide state.
class SmallsRmlUiHost {
public:
    void publish_active_object(nw::ObjectHandle object) noexcept;
    void clear_active_object() noexcept;
    void clear_active_object(nw::ObjectHandle object) noexcept;
    [[nodiscard]] nw::ObjectHandle active_object() const noexcept;

private:
    nw::ObjectHandle active_object_;
};

SmallsRmlUiHost& smalls_rmlui_host();
void register_smalls_rmlui(nw::smalls::Runtime& runtime);

} // namespace nw::toolset
