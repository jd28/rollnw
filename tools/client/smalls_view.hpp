#pragma once

#include "rml_managed_list.hpp"

namespace nw::toolset {
class RmlSmallsBridge;
class RmlSmallsLanguageBinding;
class RmlSmallsDataModel;
class ShellController;

// Existing shared UI singleton bindings, borrowed for one call. No ownership or
// DOM borrow is retained. Missing bindings reject synchronization/refresh; missing
// callbacks drop their event, callback failures log and batch draining continues.
void dispatch_smalls_list_events(RmlSmallsBridge&, ShellController&);
bool synchronize_smalls_view(RmlSmallsBridge&, RmlSmallsLanguageBinding*, RmlSmallsDataModel*);
void refresh_smalls_view(Rml::ElementDocument*, RmlSmallsBridge&, RmlSmallsLanguageBinding*, RmlSmallsDataModel*);
struct SmallsListActivation {
    std::optional<ManagedListFocusTarget> focus_target;
    bool activated = false;
};
// One ordered displayed gesture is a true singleton; its callback events and
// managed-list row updates use existing batches. Focus is owned across refresh.
SmallsListActivation activate_smalls_list(Rml::ElementDocument*, Rml::Element*,
    RmlSmallsBridge&, RmlSmallsLanguageBinding*, RmlSmallsDataModel*,
    ShellController&, ManagedListRenderState&);
bool cycle_smalls_list(Rml::ElementDocument*, Rml::Element*, int delta,
    RmlSmallsBridge&, RmlSmallsLanguageBinding*, RmlSmallsDataModel*,
    ShellController&, ManagedListRenderState&);
} // namespace nw::toolset
