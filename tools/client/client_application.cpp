#include "client_application.hpp"
#include "client_application_commands.hpp"
#include "client_application_editor.hpp"
#include "client_application_frame.hpp"
#include "client_application_input.hpp"
#include "client_application_preview.hpp"
#include "client_application_shell.hpp"
#include "client_application_workbench.hpp"
#include "client_application_workspace.hpp"
#include "client_preferences.hpp"
#include "client_rml_runtime.hpp"
#include "client_runtime.hpp"
#include "rollnw_tool_version.hpp"
#include <RmlUi/Core.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/util/game_install.hpp>
#include <nw/util/scope_exit.hpp>
#include <utility>

#ifndef ROLLNW_CLIENT_APP_ID
#define ROLLNW_CLIENT_APP_ID "org.rollnw.client"
#endif

namespace nw::toolset {
using namespace client_application_detail;

int run_client_application(const char* executable)
{
    LoguruOutputCapture log_capture;
    LOG_F(INFO, "{} {}", ROLLNW_TOOL_NAME, ROLLNW_TOOL_VERSION);

    const auto install = nw::probe_nwn_install(nw::GameVersion::vEE);
    if (install.install.empty()) {
        LOG_F(ERROR, "rollnw-client: failed to find NWN install; set NWN_ROOT and NWN_HOME");
        return 1;
    }
    nw::toolset::ClientSdlRuntime desktop;
    nw::toolset::ClientKernelRuntime kernel{install.install, install.user};
    if (!desktop.initialize_video(ROLLNW_TOOL_VERSION, ROLLNW_CLIENT_APP_ID)) {
        return 1;
    }
    if (!desktop.create_window()) {
        return 1;
    }
    auto* window = desktop.window();
    int width = 1280;
    int height = 720;
    int frame_width = 1280;
    int frame_height = 720;

    {
        const auto window_size = query_window_size(window);
        width = window_size.first;
        height = window_size.second;
        const auto pixel_size = query_window_pixels(window);
        frame_width = pixel_size.first;
        frame_height = pixel_size.second;
    }
    log_window_metrics(window, "startup");

    ClientRenderer renderer;
    if (!renderer.initialize(window)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Renderer init failed");
        return 1;
    }
    const auto renderer_cleanup = create_scope_exit([&] { renderer.shutdown(); });

    uint32_t width_u32 = static_cast<uint32_t>(width);
    uint32_t height_u32 = static_cast<uint32_t>(height);
    renderer.bootstrap_swapchain(width_u32, height_u32);
    width = static_cast<int>(width_u32);
    height = static_cast<int>(height_u32);
    if (!renderer.is_swapchain_valid()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create swapchain after bootstrap — cannot continue");
        return 1;
    }

    // -- RmlUI SDL system interface --
    SystemInterface_SDL system_interface;
    system_interface.SetWindow(window);

    auto* rml_renderer = renderer.render_interface();
    if (!rml_renderer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Selected renderer backend does not provide Rml render interface yet");
        return 1;
    }

    const std::filesystem::path ui_dir = resolve_client_ui_dir();
    if (ui_dir.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to find rollnw client UI assets");
        return 1;
    }

    nw::toolset::ClientRmlRuntime rml_runtime{ui_dir, nw::kernel::resman()};
    if (!rml_runtime.initialize(system_interface, *rml_renderer, client_base_path(), {width, height})) {
        return 1;
    }
    auto* context = rml_runtime.contexts().toolset;
    renderer.on_resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height), context);
    if (!rml_runtime.create_overlay_contexts({width, height})) {
        return 1;
    }
    auto* fps_context = rml_runtime.contexts().fps;
    auto* palette_context = rml_runtime.contexts().palette;
    const nw::Resource panel_rml{nw::Resref{"ui/panel"}, nw::ResourceType::rml};
    const nw::Resource command_modals_rml{nw::Resref{"ui/command_modals"}, nw::ResourceType::rml};

    {
        float dp_ratio = 1.0f;
        if (const char* override = std::getenv("ROLLNW_TOOLSET_UI_SCALE")) {
            const float v = std::strtof(override, nullptr);
            if (v > 0.0f) dp_ratio = v;
        }
        context->SetDensityIndependentPixelRatio(dp_ratio);
        fps_context->SetDensityIndependentPixelRatio(dp_ratio);
        palette_context->SetDensityIndependentPixelRatio(dp_ratio);
    }

    ClientApplicationState state;
    // Failed startup unwinds listener guards first, then feature bindings while
    // their storage is still live. Normal shutdown resets the context borrows.
    const auto feature_cleanup = create_scope_exit([&] {
        if (!rml_runtime.contexts().toolset) { return; }
        (void)nw::toolset::close_loading_dialog_delivery(state.loading);
        nw::toolset::close_runtime_gamepad(state.runtime_input);
        renderer.wait_idle();
        state.smalls.clear_active_object();
        state.workbench.active_object_tab_id.clear();
        rml_runtime.release_render_resources();
        state.backend.shutdown_item_editor_data_model();
        if (state.rml_smalls_data_model) { state.rml_smalls_data_model->shutdown(); }
        rml_runtime.shutdown();
        renderer.set_rml_generated_textures(nullptr, nullptr);
        renderer.shutdown();
        state.workspace.clear();
    });
    {
        int gamepad_count = 0;
        if (SDL_JoystickID* gamepads = SDL_GetGamepads(&gamepad_count)) {
            if (gamepad_count > 0) {
                nw::toolset::open_runtime_gamepad(state.runtime_input, gamepads[0]);
            }
            SDL_free(gamepads);
        }
    }
    ObjectWorkbenchChangeListener object_workbench_change_listener{state};
    context->AddEventListener(
        "change", &object_workbench_change_listener, false);
    context->AddEventListener(
        "blur", &object_workbench_change_listener, true);
    context->AddEventListener(
        "click", &object_workbench_change_listener, false);
    const auto workbench_listener_cleanup = create_scope_exit([&] {
        if (!rml_runtime.contexts().toolset) { return; }
        context->RemoveEventListener("change", &object_workbench_change_listener, false);
        context->RemoveEventListener("blur", &object_workbench_change_listener, true);
        context->RemoveEventListener("click", &object_workbench_change_listener, false);
    });
    renderer.set_rml_generated_textures(
        &state.workbench.inventory_view.item_icon_cache.textures,
        &state.area_tile_editor.palette.textures);
    state.backend.bind(&state.smalls, &state.shell, &state.workspace);
    state.backend_ready = state.backend.initialize();
    if (!state.backend_ready) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize rollnw client backend");
        return 1;
    }

    if (!state.smalls.initialize()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize the Smalls UI bridge");
        return 1;
    }

    state.rml_smalls_binding = std::make_unique<nw::toolset::RmlSmallsLanguageBinding>();
    if (!state.rml_smalls_binding->initialize(nw::kernel::runtime())) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize the RmlUi Smalls language binding");
        return 1;
    }

    state.rml_smalls_data_model = std::make_unique<nw::toolset::RmlSmallsDataModel>();
    const std::array presentation_bindings{
        nw::toolset::RmlSmallsGlobalBinding{
            .variable = "toolset",
            .module = "toolset.ui",
            .global = "rml_model",
        },
    };
    if (!state.rml_smalls_data_model->initialize(*context,
            nw::kernel::runtime(), "toolset_presentation", presentation_bindings)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Failed to initialize the RmlUi Smalls presentation model");
        return 1;
    }
    if (!state.backend.initialize_item_editor_data_model(*context)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Failed to initialize the Item editor presentation model");
        return 1;
    }

    auto* doc = rml_runtime.load_document(*context, panel_rml);
    if (!doc) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadDocument failed: ui/panel.rml");
        return 1;
    }
    doc->Show();
    HomeProjectActionListener home_project_action_listener{window, doc, state};
    context->AddEventListener("click", &home_project_action_listener);
    const auto home_listener_cleanup = create_scope_exit([&] {
        if (rml_runtime.contexts().toolset) {
            context->RemoveEventListener("click", &home_project_action_listener);
        }
    });
    BlueprintActionListener blueprint_action_listener{window, doc, state};
    palette_context->AddEventListener("click", &blueprint_action_listener);
    const auto blueprint_listener_cleanup = create_scope_exit([&] {
        if (rml_runtime.contexts().palette) {
            palette_context->RemoveEventListener("click", &blueprint_action_listener);
        }
    });
    const std::filesystem::path executable_arg{executable};
    state.client_executable = executable_arg.has_parent_path()
        ? std::filesystem::absolute(executable_arg)
        : client_base_path() / executable_arg;
    auto* palette_doc = nw::toolset::load_command_palette_document(*palette_context);
    if (!palette_doc) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadDocument failed: command_palette.rml");
        return 1;
    }
    palette_doc->Show();
    // The command context renders after the native viewport. Modal UI belongs
    // here; z-index in the main document cannot cover a later native draw.
    state.command_view.command_overlay_document = rml_runtime.load_document(*palette_context, command_modals_rml);
    if (!state.command_view.command_overlay_document) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadDocument failed: ui/command_modals.rml");
        return 1;
    }
    auto* fps_doc = nw::toolset::load_viewer_fps_document(*fps_context);
    if (!fps_doc) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadDocument failed: viewer_fps_overlay.rml");
        return 1;
    }
    fps_doc->Show();

    ClientApplicationSurfaces surfaces{window, &system_interface, context, fps_context, palette_context, doc, fps_doc, palette_doc, width, height, frame_width, frame_height};
    state.shell_view.preferences_path = nw::toolset::client_preferences_path();
    nw::toolset::load_ui_preferences(state.shell_view.preferences_path,
        state.shell.docks, state.browser.recent_projects,
        &state.workbench.toolset_language);
    state.workspace.ensure_default_tabs("Home", true);
    apply_bottom_dock_height(doc, state, window, state.shell.docks.pane(nw::toolset::DockRegion::bottom).size_px);
    apply_left_dock_width(doc, state, window, state.shell.docks.pane(nw::toolset::DockRegion::left).size_px);
    state.loading.open_module_dialog_event = SDL_RegisterEvents(1);
    flush_log_capture(log_capture, state);
    append_output(state, "info", "rollnw client shell started");
    if (state.loading.open_module_dialog_event == 0) {
        append_output(state, "warn", "Native file dialog events unavailable");
    }
    append_output(state, "info",
        "Ctrl+Shift+P: command palette, Ctrl+S: save tab, Ctrl+W: close tab, Ctrl+Z/Y: undo/redo, `: terminal tab, Ctrl+J: output tab");

    refresh_recent_list(doc, state);
    refresh_workspace_view(doc, state);
    nw::toolset::refresh_command_palette(palette_doc, state.command_view, state.backend);
    refresh_bottom_dock_view(doc, state);
    refresh_output_view(doc, state);
    state.shell.output_dirty = false;
    refresh_terminal_view(doc, state);
    state.shell.terminal_dirty = false;
    toggle_command_palette(context, palette_context, doc, palette_doc, state, false);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "rollnw client running");

    const int exit_code = run_client_frames(surfaces, renderer, state, object_workbench_change_listener, log_capture);

    (void)nw::toolset::close_loading_dialog_delivery(state.loading);
    cancel_project_blueprint_drag(doc, state);
    cancel_area_object_placement(renderer, state);
    stop_play_preview(renderer, system_interface, doc, state);
    nw::toolset::close_runtime_gamepad(state.runtime_input);
    if (state.workbench.appearance_view.appearance_body_preview_object.type != nw::ObjectType::invalid
        && nw::kernel::objects().valid(state.workbench.appearance_view.appearance_body_preview_object)) {
        (void)update_appearance_preview_rows(state.workbench.appearance_view.appearance_body_preview_object, true);
    }
    state.workbench.appearance_view.appearance_body_preview_object = nw::ObjectHandle{};
    renderer.wait_idle();
    state.smalls.clear_active_object();
    state.workbench.active_object_tab_id.clear();
    rml_runtime.release_render_resources();
    context->RemoveEventListener(
        "change", &object_workbench_change_listener, false);
    context->RemoveEventListener(
        "blur", &object_workbench_change_listener, true);
    context->RemoveEventListener("click", &home_project_action_listener);
    palette_context->RemoveEventListener("click", &blueprint_action_listener);
    state.backend.shutdown_item_editor_data_model();
    state.rml_smalls_data_model->shutdown();
    rml_runtime.shutdown();
    renderer.shutdown();
    state.workspace.clear();

    return exit_code;
}

} // namespace nw::toolset
