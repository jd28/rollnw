#include "client_application_frame.hpp"
#include "client_application_commands.hpp"
#include "client_application_editor.hpp"
#include "client_application_input.hpp"
#include "client_application_preview.hpp"
#include "client_application_shell.hpp"
#include "client_application_workbench.hpp"
#include "client_application_workspace.hpp"
#include "client_frame.hpp"
#include "client_runtime.hpp"
#include "client_ui_action.hpp"
#include "rml_managed_list.hpp"
#include "runtime_input.hpp"
#include "smalls_rmlui.hpp"
#include <RmlUi/Core.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/util/profile.hpp>
#include <utility>

namespace nw::toolset::client_application_detail {
namespace {
float seconds_between_performance_counters(Uint64 start, Uint64 end)
{
    if (end <= start) {
        return 0.0f;
    }

    const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    return static_cast<float>(static_cast<double>(end - start) / frequency);
}

void poll_client_jobs(ClientApplicationSurfaces& surfaces, ClientRenderer& renderer, ClientApplicationState& state)
{
    auto* window = surfaces.window;
    auto* context = surfaces.context;
    auto* palette_context = surfaces.palette_context;
    auto* doc = surfaces.doc;
    auto* palette_doc = surfaces.palette_doc;
    if (const auto result = state.backend.poll_blueprint_updates(state.client_executable)) {
        if (!result->message.empty()) { append_output(state, result->ok() ? "info" : "error", result->message); }
        refresh_workspace_view(doc, state);
    }
    sync_blueprint_operation(state);
    poll_project_import(window, doc, state);
    poll_project_open(window, context, palette_context,
        doc, palette_doc, renderer, state);
    sync_command_form(state);
    synchronize_smalls_runtime(state);
}

void refresh_client_queries(ClientApplicationSurfaces& surfaces, ClientRenderer& renderer, ClientApplicationState& state, LoguruOutputCapture& log_capture)
{
    auto* context = surfaces.context;
    auto* doc = surfaces.doc;
    auto* palette_doc = surfaces.palette_doc;
    clear_inactive_object(state);
    if (state.workbench.appearance_view.appearance_body_preview_object.type != nw::ObjectType::invalid
        && state.workbench.active_object_tab_id.empty()
        && !sync_appearance_body_preview(renderer, state)) {
        append_output(state, "error", "Failed to restore the creature Appearance preview");
    }

    synchronize_client_mutations(renderer, state, context, doc);
    nw::toolset::refresh_object_workbench_queries(doc, state.workbench,
        state.workspace, state.backend, state.backend_ready ? nw::kernel::resman().generation() : 0);
    if (state.area_tile_editor.stroke.active
        && !area_tile_stroke_context_valid(state)) {
        cancel_area_tile_stroke(renderer, state);
    }
    nw::toolset::refresh_area_tile_palette_query(doc, state.area_tile_editor,
        active_workspace_area(state), state.area_workspace_surface == AreaWorkspaceSurface::tiles,
        area_tile_pointer_modifier(SDL_GetModState()));
    nw::toolset::refresh_home_area_query(doc, state.browser, state.backend, workspace_home_active(state));

    const std::string recent_query = get_input_value(doc, "recent_search");
    sync_command_form(state);
    if (recent_query != state.browser.last_recent_query
        || (state.backend_ready && state.browser.project_resource_generation != nw::kernel::resman().generation())) {
        refresh_recent_list(doc, state);
    } else if (state.shell.showing_project_tree) {
        render_project_tree_window(doc, state, false);
    }

    nw::toolset::refresh_command_palette_query(palette_doc, state.command_view,
        state.backend, state.shell.command_palette_visible);

    nw::toolset::refresh_output_filter(doc, state.shell_view, state.shell);

    flush_log_capture(log_capture, state);

    if (state.shell.output_dirty) {
        refresh_output_view(doc, state);
        state.shell.output_dirty = false;
    }

    if (state.shell.terminal_dirty) {
        refresh_terminal_view(doc, state);
        state.shell.terminal_dirty = false;
    }
}

bool prepare_client_surface(ClientApplicationSurfaces& surfaces, ClientRenderer& renderer, ClientApplicationState& state, Uint64 frame_start_ms)
{
    auto* window = surfaces.window;
    auto* context = surfaces.context;
    auto* palette_context = surfaces.palette_context;
    auto* fps_context = surfaces.fps_context;
    auto* doc = surfaces.doc;
    const auto window_size = query_window_size(window);
    surfaces.width = window_size.first;
    surfaces.height = window_size.second;
    const auto pixel_size = query_window_pixels(window);
    surfaces.frame_width = pixel_size.first;
    surfaces.frame_height = pixel_size.second;

    const SDL_WindowFlags window_flags = SDL_GetWindowFlags(window);
    if ((window_flags & SDL_WINDOW_MINIMIZED) || surfaces.frame_width <= 0 || surfaces.frame_height <= 0) {
        SDL_Delay(50);
        return false;
    }

    uint32_t swapchain_width = static_cast<uint32_t>(surfaces.frame_width);
    uint32_t swapchain_height = static_cast<uint32_t>(surfaces.frame_height);
    if (!renderer.ensure_swapchain(window, swapchain_width, swapchain_height, context)) {
        surfaces.frame_width = static_cast<int>(swapchain_width);
        surfaces.frame_height = static_cast<int>(swapchain_height);
        const Uint64 frame_elapsed_ms = SDL_GetTicks() - frame_start_ms;
        if (frame_elapsed_ms < 16) {
            SDL_Delay(static_cast<Uint32>(16 - frame_elapsed_ms));
        }
        return false; // Wayland surface not ready yet — wait for next frame
    }
    surfaces.frame_width = static_cast<int>(swapchain_width);
    surfaces.frame_height = static_cast<int>(swapchain_height);
    fps_context->SetDimensions(Rml::Vector2i(surfaces.frame_width, surfaces.frame_height));
    palette_context->SetDimensions(Rml::Vector2i(surfaces.frame_width, surfaces.frame_height));
    flush_area_tile_cursor_update(
        renderer, window, context, doc, state, surfaces.frame_width, surfaces.frame_height);

    return true;
}

void render_client_layout(ClientApplicationSurfaces& surfaces, ClientRenderer& renderer, ClientApplicationState& state)
{
    auto* context = surfaces.context;
    auto* doc = surfaces.doc;
    context->Update();
    bool tab_scroll_layout_changed = false;
    if (state.workspace_view.workspace_tab_scroll_pending) {
        state.workspace_view.workspace_tab_scroll_pending = false;
        apply_workspace_tab_scroll(doc, state);
        tab_scroll_layout_changed = true;
    }
    if (state.workbench.object_workbench_tab_scroll_pending) {
        state.workbench.object_workbench_tab_scroll_pending = false;
        apply_object_workbench_tab_scroll(doc, state);
        tab_scroll_layout_changed = true;
    }
    if (tab_scroll_layout_changed) {
        context->Update();
    }
    if (state.workspace_hover_refresh_pending) {
        state.workspace_hover_refresh_pending = false;
        context->ProcessMouseMove(static_cast<int>(std::lround(state.workspace_hover_refresh_point.x)),
            static_cast<int>(std::lround(state.workspace_hover_refresh_point.y)),
            RmlSDL::GetKeyModifierState());
        context->Update();
    }
    if (sync_object_details_window(doc, state, false)) {
        context->Update();
    }
    if (nw::toolset::sync_dialog_view(doc, state.dialog_view, false)) {
        context->Update();
    }
    if (sync_creature_feat_window(doc, state, false)) {
        context->Update();
    }
    if (sync_creature_spell_window(doc, state, false)) {
        context->Update();
    }
    if (sync_creature_spell_filter_window(doc, state, false)) {
        context->Update();
    }
    if (sync_creature_inventory_window(doc, state, false)) {
        context->Update();
    }
    if (sync_appearance_window(doc, state, false)) {
        context->Update();
    }
    if (sync_sound_catalog_window(doc, state, false)) {
        context->Update();
    }
    if (nw::toolset::sync_managed_lists(doc,
            nw::toolset::ui_v1_host(), state.workbench.managed_lists, false)) {
        context->Update();
    }
    state.backend.apply_item_editor_pending_focus(doc);
    if (apply_output_scroll_after_layout(doc, state)) {
        context->Update();
    }
    {
        const ScopedClientGpuTimer gpu_timer{renderer, kClientGpuTimerUi};
        context->Render();
    }
}

std::optional<WorkspaceViewerViewportRequest> render_client_workspace(ClientApplicationSurfaces& surfaces, ClientRenderer& renderer, ClientApplicationState& state, float raw_frame_delta_seconds, int32_t frame_delta_ms)
{
    auto* context = surfaces.context;
    auto* palette_context = surfaces.palette_context;
    auto* doc = surfaces.doc;
    auto* palette_doc = surfaces.palette_doc;
    auto& system_interface = *surfaces.system_interface;
    const auto viewer_viewport = active_workspace_viewer_viewport_request(doc, state, surfaces.frame_width, surfaces.frame_height);
    if ((state.play_preview.session.active()
            || state.play_preview.placement_pending())
        && (!viewer_viewport
            || viewer_viewport->kind != WorkspaceViewerViewportKind::area
            || viewer_viewport->module_generation
                != state.play_preview.module_generation
            || state.workspace.active_tab_id() != state.play_preview.tab_id)) {
        stop_play_preview(renderer, system_interface, doc, state);
    }
    if (state.play_preview.session.active()) {
        // Capture ownership after UI callbacks/layout, rather than physical
        // key-down disposition: SDL held keys can outlive a claimed event.
        const std::array facts{nw::toolset::capture_client_held_input_facts(context, palette_context, palette_doc,
            state.command_view.command_overlay_document, client_input_ownership(state))};
        std::array<nw::toolset::ClientInputRoute, 1> routes{};
        (void)nw::toolset::resolve_client_input_routes(facts, routes);
        const auto eligibility = routes.front().sources;
        if (nw::toolset::update_play_preview_frame(renderer, state.play_preview,
                state.runtime_input, state.shell, static_cast<double>(raw_frame_delta_seconds), eligibility)
            == nw::toolset::PreviewStatus::invalid_input) {
            state.viewer_viewport_pointer_owner = nw::toolset::ClientPointerOwner::none;
            apply_shell_layout(doc, state);
            system_interface.SetMouseCursor("arrow");
        }
    }
    auto* viewer_tab = state.workspace.active_tab();
    const auto viewer_project_dir = state.backend.current_project_dir();
    const bool data_workbench_preview = !viewer_viewport
        && viewer_tab
        && viewer_tab->kind == nw::toolset::WorkspaceTabKind::preview
        && !viewer_tab->detail.empty()
        && !viewer_project_dir.empty()
        && data_workbench_only(state.workbench.object_details.object.type,
            state.workbench.object_workbench_surface);
    const bool viewer_requested = viewer_viewport.has_value()
        || data_workbench_preview;
    if (viewer_requested) {
        sync_viewer_render_options(renderer, state);
        bool viewer_ready = false;
        WorkspaceViewerViewportKind viewer_kind = WorkspaceViewerViewportKind::preview;
        {
            const ScopedClientGpuTimer gpu_timer{renderer, kClientGpuTimerViewport};
            if (viewer_viewport) {
                viewer_kind = viewer_viewport->kind;
                viewer_ready = viewer_viewport->kind == WorkspaceViewerViewportKind::area
                    ? renderer.render_area_viewport(
                          viewer_viewport->project_dir,
                          viewer_viewport->module_generation,
                          viewer_viewport->resource_path,
                          viewer_tab->document,
                          viewer_viewport->rect,
                          frame_delta_ms)
                    : renderer.render_preview_viewport(
                          viewer_viewport->project_dir,
                          viewer_viewport->module_generation,
                          viewer_viewport->resource_path,
                          viewer_tab->document,
                          viewer_viewport->rect,
                          frame_delta_ms);
            } else {
                const bool current_preview_is_ready
                    = state.workbench.active_object_tab_id == viewer_tab->id
                    && state.workbench.object_details.status
                        == nw::toolset::ObjectDetailsStatus::ready
                    && renderer.active_viewer_object()
                        == state.workbench.object_details.object;
                viewer_ready = current_preview_is_ready
                    || renderer.prepare_preview_object(
                        viewer_project_dir,
                        state.backend.module_generation(),
                        viewer_tab->detail, viewer_tab->document);
            }
        }
        if (!viewer_ready) {
            // The viewport renderer logs specific load/render failures; keep the UI frame intact.
        }

        if (viewer_ready) {
            if (viewer_kind == WorkspaceViewerViewportKind::area) {
                const auto area = renderer.area_viewer_object();
                const bool area_changed = state.smalls.active_area() != area;
                state.smalls.publish_active_area(area);
                if (area_changed) {
                    refresh_workspace_content(doc, state);
                }
            } else {
                state.smalls.clear_active_area();
            }
            const nw::ObjectHandle object
                = viewer_kind == WorkspaceViewerViewportKind::area
                    && state.area_workspace_surface
                        == AreaWorkspaceSurface::properties
                ? renderer.area_viewer_object()
                : renderer.active_viewer_object();
            if (object.type != nw::ObjectType::invalid) {
                state.smalls.publish_active_object(object);
                const std::string active_tab_id = state.workspace.active_tab_id();
                const bool object_changed = state.workbench.object_details.object != object
                    || state.workbench.active_object_tab_id != active_tab_id
                    || state.workbench.object_details.status != nw::toolset::ObjectDetailsStatus::ready;
                state.workbench.active_object_tab_id = active_tab_id;
                if (object_changed) {
                    state.managed_list_reorder = {};
                    nw::toolset::activate_object_workbench(state.workbench, object, state.workspace.active_tab_id());
                    state.observed_object_mutation_epoch = nw::toolset::object_mutation_state().epoch;
                    refresh_workspace_content(doc, state);
                    sync_object_details_window(doc, state, true);
                    sync_creature_feat_window(doc, state, true);
                    sync_creature_spell_window(doc, state, true);
                    sync_creature_inventory_window(doc, state, true);
                    sync_appearance_window(doc, state, true);
                }
            } else {
                const bool had_active_object = state.workbench.object_details.object.type != nw::ObjectType::invalid;
                state.smalls.clear_active_object();
                state.workbench.active_object_tab_id.clear();
                if (state.workbench.object_details.status != nw::toolset::ObjectDetailsStatus::empty) {
                    clear_active_object_details(state);
                    if (had_active_object) {
                        refresh_workspace_content(doc, state);
                    }
                    sync_object_details_window(doc, state, true);
                }
            }
        } else {
            state.smalls.clear_active_object();
            state.smalls.clear_active_area();
            state.workbench.active_object_tab_id.clear();
            if (state.workbench.object_details.status != nw::toolset::ObjectDetailsStatus::empty) {
                clear_active_object_details(state);
                sync_object_details_window(doc, state, true);
            }
        }
    } else {
        renderer.clear_viewer_viewport();
        if (sync_active_module_object(state)) {
            sync_object_details_window(doc, state, false);
        } else {
            state.smalls.clear_active_object();
            state.smalls.clear_active_area();
            state.workbench.active_object_tab_id.clear();
            if (state.workbench.object_details.status != nw::toolset::ObjectDetailsStatus::empty) {
                clear_active_object_details(state);
                sync_object_details_window(doc, state, true);
            }
        }
    }
    if (!sync_appearance_body_preview(renderer, state)) {
        append_output(state, "error", "Failed to synchronize the active creature Appearance preview");
    }

    return viewer_viewport;
}

} // namespace

int run_client_frames(ClientApplicationSurfaces& surfaces, ClientRenderer& renderer, ClientApplicationState& state, ObjectWorkbenchChangeListener& object_workbench_change_listener, LoguruOutputCapture& log_capture)
{
    auto* palette_context = surfaces.palette_context;
    auto* fps_context = surfaces.fps_context;
    auto* fps_doc = surfaces.fps_doc;
    const bool frame_pacing_enabled = nw::toolset::client_frame_pacing_enabled();
    if (!frame_pacing_enabled) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "frame pacing disabled by ROLLNW_CLIENT_UNCAPPED");
    }

    bool running = true;
    std::array<nw::toolset::ClientFrameClock, 1> frame_clocks{{{SDL_GetTicks(), 0}}};
    int exit_code = 0;
    while (running) {
#if defined(ROLLNW_ENABLE_TRACY)
        FrameMark;
#endif
        poll_client_jobs(surfaces, renderer, state);
        const Uint64 frame_start_counter = SDL_GetPerformanceCounter();
        const Uint64 frame_start_ms = SDL_GetTicks();
        const std::array frame_samples{nw::toolset::ClientFrameSample{
            frame_start_ms, frame_start_counter, SDL_GetPerformanceFrequency()}};
        std::array<nw::toolset::ClientFrameDelta, 1> frame_deltas{};
        if (!nw::toolset::advance_client_frames(frame_clocks, frame_samples, frame_deltas)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid client frame timing sample");
            exit_code = 1;
            break;
        }
        const float raw_frame_delta_seconds = frame_deltas.front().raw_seconds;
        const int32_t frame_delta_ms = frame_deltas.front().camera_milliseconds;
        update_viewer_frame_metrics(state.metrics, raw_frame_delta_seconds);

        poll_client_input(surfaces, renderer, state, object_workbench_change_listener, running);
        refresh_client_queries(surfaces, renderer, state, log_capture);
        if (!prepare_client_surface(surfaces, renderer, state, frame_start_ms)) { continue; }
        const Uint64 begin_frame_start_counter = SDL_GetPerformanceCounter();
        renderer.begin_frame();
        const Uint64 draw_start_counter = SDL_GetPerformanceCounter();
        render_client_layout(surfaces, renderer, state);
        const Uint64 ui_end_counter = SDL_GetPerformanceCounter();
        const auto viewer_viewport = render_client_workspace(surfaces, renderer, state, raw_frame_delta_seconds, frame_delta_ms);
        const Uint64 view_end_counter = SDL_GetPerformanceCounter();
        update_viewer_internal_metrics(state.metrics, renderer.last_viewer_frame_stats());
        const Uint64 overlay_start_counter = view_end_counter;
        sync_viewer_fps_overlay(fps_doc,
            viewer_viewport ? viewer_viewport->rect : ClientViewportRect{}, state.metrics,
            state.shell.viewer_forward_plus_enabled, state.shell.viewer_forward_plus_debug_mode);
        nw::toolset::sync_play_preview_viewport_overlay(fps_doc,
            viewer_viewport ? std::optional{viewer_viewport->rect} : std::nullopt, state.play_preview);
        {
            const ScopedClientGpuTimer gpu_timer{renderer, kClientGpuTimerOverlay};
            fps_context->Update();
            fps_context->Render();
        }
        const Uint64 overlay_end_counter = SDL_GetPerformanceCounter();
        Uint64 palette_end_counter = overlay_end_counter;
        if (state.shell.command_palette_visible || state.command_view.command_form
            || state.loading.project_load.active()
            || state.backend.blueprint_operation_active() || state.backend.blueprint_publication_pending()) {
            const ScopedClientGpuTimer gpu_timer{renderer, kClientGpuTimerPalette};
            palette_context->Update();
            palette_context->Render();
            if (state.loading.project_load.active()) {
                state.loading.project_load.presented = true;
            }
            palette_end_counter = SDL_GetPerformanceCounter();
        }
        const Uint64 present_start_counter = palette_end_counter;
        renderer.end_frame();
        update_client_gpu_metrics(state.metrics, renderer.last_gpu_frame_stats());
        const Uint64 present_end_counter = SDL_GetPerformanceCounter();
        update_viewer_render_metrics(state.metrics,
            seconds_between_performance_counters(frame_start_counter, present_end_counter),
            seconds_between_performance_counters(begin_frame_start_counter, draw_start_counter),
            seconds_between_performance_counters(draw_start_counter, present_start_counter),
            seconds_between_performance_counters(draw_start_counter, ui_end_counter),
            seconds_between_performance_counters(ui_end_counter, view_end_counter),
            seconds_between_performance_counters(view_end_counter, present_start_counter),
            seconds_between_performance_counters(overlay_start_counter, overlay_end_counter),
            seconds_between_performance_counters(overlay_end_counter, palette_end_counter),
            seconds_between_performance_counters(present_start_counter, present_end_counter));

        const Uint64 frame_elapsed_ms = SDL_GetTicks() - frame_start_ms;
        if (frame_pacing_enabled && frame_elapsed_ms < 16) {
            SDL_Delay(static_cast<Uint32>(16 - frame_elapsed_ms));
        }
    }
    return exit_code;
}

} // namespace nw::toolset::client_application_detail
