#include "client_application_commands.hpp"
#include "browser_view.hpp"
#include "client_application_input.hpp"
#include "client_application_shell.hpp"
#include "client_application_workspace.hpp"
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
#include <utility>

namespace nw::toolset::client_application_detail {

void sync_command_overlay_visibility(ClientApplicationState& state)
{
    nw::toolset::sync_command_overlay_visibility(state.command_view,
        state.loading.project_load.active(), state.backend.blueprint_operation_active() || state.backend.blueprint_publication_pending());
}

void close_command_form_combobox(ClientApplicationState& state)
{
    nw::toolset::close_command_form_combobox(state.command_view);
}

void sync_command_form(ClientApplicationState& state, bool force)
{
    nw::toolset::sync_command_form(state.command_view, state.backend,
        state.loading.project_load.active(), force);
}

void sync_blueprint_operation(ClientApplicationState& state)
{
    nw::toolset::sync_blueprint_operation(state.command_view, state.backend,
        state.loading.project_load.active());
}

void sync_project_load_overlay(ClientApplicationState& state)
{
    sync_command_overlay_visibility(state);
    nw::toolset::sync_loading_overlay(state.command_view.command_overlay_document, state.loading.project_load);
}

bool queue_project_open(ClientApplicationState& state, std::string path,
    nw::toolset::CommandSource source, bool close_import_panel_on_success)
{
    if (!nw::toolset::queue_loading_project(state.loading, std::move(path), source, close_import_panel_on_success)) { return false; }
    sync_project_load_overlay(state);
    return true;
}

nw::toolset::CommandResult resolve_command_result(SDL_Window* window,
    ClientApplicationState& state,
    nw::toolset::CommandResult result,
    nw::toolset::CommandSource source,
    bool terminal_output)
{
    bool prompted = false;
    while (result.prompt) {
        prompted = true;
        if (nw::toolset::take_command_form_prompt(state.command_view, result)) {
            break;
        }
        const auto action = nw::toolset::show_command_prompt(window, *result.prompt);
        if (!action || action->command_id.empty()) {
            result = {};
            result.status = nw::toolset::CommandStatus::noop;
            result.output_channel = nw::toolset::CommandOutputChannel::none;
            break;
        }

        std::vector<std::string_view> args;
        args.reserve(action->args.size());
        for (const auto& argument : action->args) {
            args.push_back(argument);
        }
        result = dispatch_command(state, action->command_id, std::move(args), source);
    }

    nw::toolset::release_area_map_textures(
        result.refreshed_area_maps);

    if (terminal_output) {
        append_terminal_result(state, result);
    } else {
        append_command_result(state, result);
    }
    if (prompted && !result.ok() && result.should_log()) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Save failed", result.message.c_str(), window);
    }
    return result;
}

nw::toolset::CommandResult dispatch_command_flow(SDL_Window* window,
    ClientApplicationState& state,
    std::string_view command_id,
    std::vector<std::string_view> args,
    nw::toolset::CommandSource source)
{
    return resolve_command_result(window, state, dispatch_command(state, command_id, std::move(args), source), source);
}

void show_open_module_dialog(SDL_Window* window, ClientApplicationState& state, bool import)
{
    const auto status = nw::toolset::show_loading_module_dialog(window, state.loading, import);
    if (status == nw::toolset::LoadingDialogStatus::unavailable) { append_output(state, "error", "Open module dialog unavailable"); }
    if (status == nw::toolset::LoadingDialogStatus::already_active) { append_output(state, "info", "Open module dialog already active"); }
}

void show_open_project_dialog(SDL_Window* window, ClientApplicationState& state, bool import)
{
    const auto status = nw::toolset::show_loading_project_dialog(window, state.loading, import);
    if (status == nw::toolset::LoadingDialogStatus::unavailable) { append_output(state, "error", "Open project dialog unavailable"); }
    if (status == nw::toolset::LoadingDialogStatus::already_active) { append_output(state, "info", "Open dialog already active"); }
}

void execute_palette_command(SDL_Window* window,
    Rml::Context* context,
    Rml::Context* palette_context,
    Rml::ElementDocument* doc,
    Rml::ElementDocument* palette_doc,
    ClientApplicationState& state,
    std::string_view command_id)
{
    if (command_id.empty() || !ensure_backend_ready(state)) {
        return;
    }

    if (command_id == "toolset.open") {
        show_open_module_dialog(window, state);
        toggle_command_palette(context, palette_context, doc, palette_doc, state, false);
        sync_shell_visibility(context, palette_context, doc, palette_doc, state);
        return;
    }

    if (command_id == "toolset.open_project") {
        show_open_project_dialog(window, state);
        toggle_command_palette(context, palette_context, doc, palette_doc, state, false);
        sync_shell_visibility(context, palette_context, doc, palette_doc, state);
        return;
    }

    const bool was_showing_areas = state.shell.showing_areas;
    const bool was_showing_project = state.shell.showing_project_tree;
    const auto result = dispatch_command_flow(window, state, command_id, {}, nw::toolset::CommandSource::palette);
    sync_shell_visibility(context, palette_context, doc, palette_doc, state);
    refresh_workspace_view(doc, state);
    if (result.ok()
        && (state.shell.showing_areas
            || state.shell.showing_project_tree
            || state.shell.showing_areas != was_showing_areas
            || state.shell.showing_project_tree != was_showing_project)) {
        refresh_recent_list(doc, state);
    }
    toggle_command_palette(context, palette_context, doc, palette_doc, state, false);
}

void handle_open_module_dialog_result(SDL_Window* window, Rml::ElementDocument* doc, ClientApplicationState& state, SDL_Event& event)
{
    const auto result = nw::toolset::take_loading_dialog_result(state.loading, event);
    if (!result) { return; }
    const auto& command = result->command;
    const auto& path = result->selection.path;
    if (command == "blueprint.directory") {
        if (nw::toolset::apply_command_form_directory_result(state.command_view, path,
                result->selection.error, result->selection.canceled)) { sync_command_form(state); }
        return;
    }
    const auto action = nw::toolset::apply_loading_dialog_selection(state.loading, *result, window, state.shell);
    if (action == nw::toolset::LoadingDialogAction::import_changed) {
        refresh_workspace_view(doc, state);
        return;
    }
    if (action == nw::toolset::LoadingDialogAction::none) { return; }
    if (!ensure_backend_ready(state)) {
        append_output(state, "error", "Backend initialization failed");
        return;
    }

    if (action == nw::toolset::LoadingDialogAction::open_project) {
        if (!queue_project_open(state, path,
                nw::toolset::CommandSource::palette)) {
            append_output(state, "warn", "A project is already opening");
        }
        return;
    }

    const bool was_showing_areas = state.shell.showing_areas;
    const bool was_showing_project = state.shell.showing_project_tree;
    const auto command_result = dispatch_command(state,
        command,
        {std::string_view{path}},
        nw::toolset::CommandSource::palette);
    append_command_result(state, command_result);
    refresh_bottom_dock_view(doc, state);
    if (command_result.ok()
        && (state.shell.showing_areas
            || state.shell.showing_project_tree
            || state.shell.showing_areas != was_showing_areas
            || state.shell.showing_project_tree != was_showing_project)) {
        state.browser.selected_recent_index = -1;
        set_input_value(doc, "recent_search", "");
        refresh_recent_list(doc, state);
    }
    if (command_result.ok()) {
        refresh_workspace_view(doc, state);
    }
}

void run_command_form_action(SDL_Window* window, Rml::ElementDocument* doc, ClientApplicationState& state, size_t index)
{
    const auto action = nw::toolset::take_command_form_action(state.command_view,
        state.backend, state.loading.project_load.active(), state.loading.module_dialog_open, index);
    if (!action) { return; }
    std::vector<std::string_view> args;
    for (const auto& argument : action->args) {
        args.push_back(argument);
    }
    (void)dispatch_command_flow(window, state, action->command_id, std::move(args), nw::toolset::CommandSource::widget);
    refresh_recent_list(doc, state);
    refresh_workspace_view(doc, state);
    sync_command_form(state);
}

void poll_project_import(SDL_Window* window, Rml::ElementDocument* doc, ClientApplicationState& state)
{
    const auto result = nw::toolset::poll_loading_import(state.loading, window, state.shell);
    if (!result) { return; }
    if (result->ok) {
        remember_recent_project(state, result->project_dir);
        if (nw::toolset::finish_loading_import(state.loading, *result,
                {.dirty_tabs = state.workspace.has_dirty_tabs(),
                    .module_generation = state.backend.module_generation(),
                    .preview_active = state.play_preview.session.active() || state.play_preview.placement_pending()},
                window)) {
            sync_project_load_overlay(state);
        }
    }
    refresh_recent_list(doc, state);
    refresh_workspace_view(doc, state);
}

void poll_project_open(SDL_Window* window,
    Rml::Context* context,
    Rml::Context* palette_context,
    Rml::ElementDocument* doc,
    Rml::ElementDocument* palette_doc,
    ClientRenderer& renderer,
    ClientApplicationState& state)
{
    if (!state.loading.project_load.active() || !state.loading.project_load.presented) {
        return;
    }

    const auto request = state.loading.project_load;
    auto pending_result = nw::toolset::poll_loading_project(state.loading, window, context,
        palette_context, state.command_view.command_overlay_document, renderer, state.backend);
    if (!pending_result) { return; }
    const auto result = resolve_command_result(window, state, std::move(*pending_result), request.source);
    state.loading.project_load = {};
    if (result.ok()) {
        remember_recent_project(state, state.backend.current_project_dir());
        if (request.close_import_panel_on_success) {
            state.loading.import_panel_open = false;
        }
        state.browser.selected_recent_index = -1;
        set_input_value(doc, "recent_search", "");
    } else {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
            "Unable to open project", result.message.c_str(), window);
    }

    sync_project_load_overlay(state);
    sync_shell_visibility(
        context, palette_context, doc, palette_doc, state);
    refresh_recent_list(doc, state);
    refresh_workspace_view(doc, state);
}

BlueprintActionListener::BlueprintActionListener(SDL_Window* window, Rml::ElementDocument* document, ClientApplicationState& state)
    : window_{window}
    , document_{document}
    , state_{state}
{
}

void BlueprintActionListener::ProcessEvent(Rml::Event& event)
{
    const auto action = nw::toolset::handle_command_overlay_target(
        state_.command_view, state_.backend, state_.loading.project_load.active(), event.GetTargetElement());
    switch (action.kind) {
    case nw::toolset::CommandOverlayActionKind::dispatch:
        (void)dispatch_command_flow(window_, state_, action.command_id, {}, nw::toolset::CommandSource::widget);
        refresh_recent_list(document_, state_);
        refresh_workspace_view(document_, state_);
        sync_command_form(state_);
        sync_blueprint_operation(state_);
        break;
    case nw::toolset::CommandOverlayActionKind::submit:
        run_command_form_action(window_, document_, state_, action.form_action_index);
        break;
    case nw::toolset::CommandOverlayActionKind::browse_directory: {
        if (!state_.command_view.command_form || state_.command_view.command_form->fields.size() < 2 || state_.loading.module_dialog_open || state_.loading.open_module_dialog_event == 0) { return; }
        close_command_form_combobox(state_);
        sync_command_form(state_);
        const auto chosen = std::filesystem::path{state_.command_view.command_form->fields[1].value};
        state_.command_view.command_form_browse_generation = state_.command_view.command_form_generation;
        nw::toolset::show_loading_blueprint_directory_dialog(window_, state_.loading,
            chosen.is_absolute() ? chosen : state_.backend.current_project_dir() / chosen);
        break;
    }
    case nw::toolset::CommandOverlayActionKind::handled:
        break;
    case nw::toolset::CommandOverlayActionKind::none:
        return;
    }
    event.StopPropagation();
}

HomeProjectActionListener::HomeProjectActionListener(SDL_Window* window, Rml::ElementDocument* document, ClientApplicationState& state)
    : window_{window}
    , document_{document}
    , state_{state}
{
}

void HomeProjectActionListener::ProcessEvent(Rml::Event& event)
{
    const auto action = nw::toolset::handle_loading_home_target(state_.loading, event.GetTargetElement(),
        window_, state_.shell, state_.client_executable, state_.backend.module_generation());
    switch (action) {
    case nw::toolset::LoadingHomeAction::browse_module:
        show_open_module_dialog(window_, state_, true);
        break;
    case nw::toolset::LoadingHomeAction::browse_destination:
        show_open_project_dialog(window_, state_, true);
        break;
    case nw::toolset::LoadingHomeAction::open_project:
        show_open_project_dialog(window_, state_);
        break;
    case nw::toolset::LoadingHomeAction::changed:
        break;
    case nw::toolset::LoadingHomeAction::none:
        return;
    }
    event.StopPropagation();
    refresh_workspace_view(document_, state_);
}

void toggle_command_palette(Rml::Context* context,
    Rml::Context* palette_context,
    Rml::ElementDocument* doc,
    Rml::ElementDocument* palette_doc,
    ClientApplicationState& state,
    bool visible)
{
    state.shell.set_command_palette_visible(visible);
    nw::toolset::set_command_palette_visibility(state.command_view, context,
        palette_context, doc, palette_doc, state.viewer_viewport_focused, visible);
    if (visible) {
        if (!ensure_backend_ready(state)) {
            append_output(state, "warn", "Command palette unavailable: backend init failed");
        }
        nw::toolset::refresh_command_palette(palette_doc, state.command_view, state.backend);
        if (auto* input = find_el(palette_doc, "command_input")) {
            input->Focus();
        }
    }
}

nw::toolset::CommandContext command_context(ClientApplicationState& state, nw::toolset::CommandSource source)
{
    nw::toolset::CommandContext context;
    context.source = source;
    context.workspace = &state.workspace;
    context.active_tab_id = state.workspace.active_tab_id();
    context.area_object = state.smalls.active_area();
    context.play_preview_active = state.play_preview.session.active();
    return context;
}

void append_command_result(ClientApplicationState& state, const nw::toolset::CommandResult& result)
{
    nw::toolset::append_command_results(state.shell, {&result, 1});
}

void append_terminal_result(ClientApplicationState& state, const nw::toolset::CommandResult& result)
{
    nw::toolset::append_terminal_results(state.shell, {&result, 1});
}

nw::toolset::CommandResult dispatch_command(ClientApplicationState& state,
    std::string_view command_id,
    std::vector<std::string_view> args,
    nw::toolset::CommandSource source)
{
    return state.backend.execute_command(command_id, args, command_context(state, source));
}

bool ensure_backend_ready(ClientApplicationState& state)
{
    if (state.backend_ready) {
        return true;
    }

    state.backend_ready = state.backend.initialize();
    if (!state.backend_ready) {
        append_output(state, "error", "Failed to initialize backend");
        return false;
    }

    append_output(state, "info", "Backend initialized");
    return true;
}

} // namespace nw::toolset::client_application_detail
