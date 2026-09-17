#include "smalls_view.hpp"
#include "rml_smalls_bridge.hpp"
#include "rml_smalls_data_model.hpp"
#include "rml_smalls_language_binding.hpp"
#include "shell_controller.hpp"
#include "smalls_ui_v1.hpp"
#include <nw/kernel/Kernel.hpp>

namespace nw::toolset {
void dispatch_smalls_list_events(RmlSmallsBridge& bridge, ShellController& shell)
{
    auto& host = nw::toolset::ui_v1_host();
    host.drain_events([&](const nw::toolset::UiListEvent& event) {
        const auto* callback = host.callback_ptr(event.list_id(), event.type);
        if (!callback) {
            return;
        }
        const std::string qualified_function = *callback;
        const auto result = bridge.call_ui_list_callback(
            qualified_function, event);
        if (!result.ok) {
            shell.append_output("error", result.message);
        }
    });
}

bool synchronize_smalls_view(RmlSmallsBridge& bridge, RmlSmallsLanguageBinding* language, RmlSmallsDataModel* model)
{
    if (!language || !model
        || !bridge.initialize()) {
        return false;
    }
    auto& runtime = nw::kernel::runtime();
    return language->initialize(runtime)
        && model->synchronize(runtime);
}

void refresh_smalls_view(Rml::ElementDocument* document, RmlSmallsBridge& bridge, RmlSmallsLanguageBinding* language, RmlSmallsDataModel* model)
{
    if (document && synchronize_smalls_view(bridge, language, model)) {
        language->refresh_elements(document);
        model->dirty_all();
    }
}

SmallsListActivation activate_smalls_list(Rml::ElementDocument* document, Rml::Element* hit,
    RmlSmallsBridge& bridge, RmlSmallsLanguageBinding* language, RmlSmallsDataModel* model, ShellController& shell, ManagedListRenderState& lists)
{
    SmallsListActivation result;
    result.focus_target = nw::toolset::managed_list_focus_target(hit);
    auto& host = nw::toolset::ui_v1_host();
    if (!nw::toolset::activate_managed_list_element(hit, host)) {
        return result;
    }
    dispatch_smalls_list_events(bridge, shell);
    refresh_smalls_view(document, bridge, language, model);
    nw::toolset::sync_managed_lists(
        document, host, lists, true);
    result.activated = true;
    return result;
}

bool cycle_smalls_list(Rml::ElementDocument* document, Rml::Element* element, int delta,
    RmlSmallsBridge& bridge, RmlSmallsLanguageBinding* language, RmlSmallsDataModel* model, ShellController& shell, ManagedListRenderState& lists)
{
    const auto focus_target = nw::toolset::managed_list_focus_target(element);
    auto& host = nw::toolset::ui_v1_host();
    if (!nw::toolset::cycle_managed_list_element(element, host, delta)) {
        return false;
    }
    dispatch_smalls_list_events(bridge, shell);
    refresh_smalls_view(document, bridge, language, model);
    nw::toolset::sync_managed_lists(
        document, host, lists, true);
    if (focus_target) {
        (void)nw::toolset::focus_managed_list_target(
            document, *focus_target);
    }
    return true;
}

} // namespace nw::toolset
