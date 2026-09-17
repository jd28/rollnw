#include "appearance_catalog.hpp"
#include "appearance_view.hpp"
#include "area_door_hooks.hpp"
#include "area_map.hpp"
#include "area_navigation.hpp"
#include "area_object_editor.hpp"
#include "area_regions.hpp"
#include "area_tile_editor.hpp"
#include "area_tile_edits.hpp"
#include "area_tile_interaction.hpp"
#include "area_tile_palette.hpp"
#include "browser_view.hpp"
#include "client_cli.hpp"
#include "client_input.hpp"
#include "client_metrics.hpp"
#include "client_preferences.hpp"
#include "client_rml_runtime.hpp"
#include "client_runtime.hpp"
#include "client_ui_action.hpp"
#include "command_view.hpp"
#include "creature_workbench_view.hpp"
#include "dialog_view.hpp"
#include "forward_plus_debug.hpp"
#include "inventory_workbench_view.hpp"
#include "loading_view.hpp"
#include "object_document.hpp"
#include "object_edits.hpp"
#include "object_workbench.hpp"
#include "object_workbench_view.hpp"
#include "play_preview_view.hpp"
#include "preview_session.hpp"
#include "project.hpp"
#include "project_resource_drag.hpp"
#include "renderer.hpp"
#include "resource_document.hpp"
#include "rml_managed_list.hpp"
#include "rml_smalls_bridge.hpp"
#include "rml_smalls_data_model.hpp"
#include "rml_smalls_language_binding.hpp"
#include "rollnw_tool_version.hpp"
#include "runtime_input.hpp"
#include "shell_controller.hpp"
#include "shell_view.hpp"
#include "smalls_creature_feats.hpp"
#include "smalls_creature_inventory.hpp"
#include "smalls_creature_properties.hpp"
#include "smalls_creature_spells.hpp"
#include "sound_catalog.hpp"
#include "toolset_backend.hpp"
#include "viewport_pointer_drag.hpp"
#include "virtual_combobox.hpp"
#include "virtual_list.hpp"
#include "workspace.hpp"
#include "workspace_view.hpp"

#include "nw/log.hpp"
#include <nw/formats/Tileset.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Store.hpp>
#include <nw/profiles/nwn1/toolset_visual.hpp>
#include <nw/render/viewer/session.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/smalls/runtime.hpp>
#include <nw/util/game_install.hpp>
#include <nw/util/profile.hpp>
#include <nw/util/scope_exit.hpp>

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <RmlUi_Platform_SDL.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <absl/container/flat_hash_set.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#ifndef ROLLNW_CLIENT_APP_ID
#define ROLLNW_CLIENT_APP_ID "org.rollnw.client"
#endif

// ---------------------------------------------------------------------------

namespace {

using nw::toolset::active_tab_has_object_workbench;
using nw::toolset::appearance_catalog_kind;
using nw::toolset::appearance_editor_field_from_name;
using nw::toolset::AppearanceEditorField;
using nw::toolset::append_workspace_subtabs_markup;
using nw::toolset::apply_tab_scroll;
using nw::toolset::AreaObjectPlacementPhase;
using nw::toolset::blur_focused_object_variable_input;
using nw::toolset::client_base_path;
using nw::toolset::creature_spell_filter_field_from_name;
using nw::toolset::CreatureSpellFilterField;
using nw::toolset::data_workbench_only;
using nw::toolset::default_object_workbench_surface;
using nw::toolset::editable_area_object;
using nw::toolset::element_at_mouse;
using nw::toolset::find_recent_item_at;
using nw::toolset::focused_element_has_id;
using nw::toolset::focused_text_input;
using nw::toolset::kObjectWorkbenchTabScrollStrip;
using nw::toolset::kPltPaletteCellPx;
using nw::toolset::kPltPaletteColumns;
using nw::toolset::kPltPaletteRows;
using nw::toolset::kWorkspaceTabScrollStrip;
using nw::toolset::object_has_grid_inventory;
using nw::toolset::ObjectWorkbenchSurface;
using nw::toolset::output_scroll_input;
using nw::toolset::OutputScrollAfterLayout;
using nw::toolset::point_within_element;
using nw::toolset::ProjectBlueprintDragPhase;
using nw::toolset::recent_item_at_point;
using nw::toolset::region_blueprint_resource;
using nw::toolset::remember_tab_scroll;
using nw::toolset::run_project_cli_if_requested;
using nw::toolset::save_ui_preferences;
using nw::toolset::ScopedClientGpuTimer;
using nw::toolset::sync_viewer_fps_overlay;
using nw::toolset::tab_scroll_target;
using nw::toolset::to_context_point;
using nw::toolset::update_appearance_preview_rows;
using nw::toolset::update_client_gpu_metrics;
using nw::toolset::update_viewer_frame_metrics;
using nw::toolset::update_viewer_internal_metrics;
using nw::toolset::update_viewer_render_metrics;
using nw::toolset::workspace_tab_current_index;
using nw::toolset::workspace_tab_detail;
using nw::toolset::workspace_tab_element_at_point;
using nw::toolset::workspace_tab_kind_class;
using nw::toolset::workspace_tab_target_index_at_point;
using nw::toolset::WorkspaceViewerViewportKind;
using nw::toolset::WorkspaceViewerViewportRequest;

constexpr float kManagedListAutoScrollEdgePx = 28.0f;
constexpr float kManagedListAutoScrollStepPx = 14.0f;
constexpr float kWorkspaceTabDragThresholdPx = 5.0f;
constexpr float kTabScrollStepPx = 48.0f;
constexpr float kWorkspaceTabAutoScrollEdgePx = 28.0f;
constexpr float kWorkspaceTabAutoScrollStepPx = 14.0f;

bool environment_flag_enabled(const char* name)
{
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0') {
        return false;
    }

    std::string normalized{value};
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return normalized != "0" && normalized != "false" && normalized != "off" && normalized != "no";
}

float seconds_between_performance_counters(Uint64 start, Uint64 end)
{
    if (end <= start) {
        return 0.0f;
    }

    const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    return static_cast<float>(static_cast<double>(end - start) / frequency);
}

struct CapturedLogLine {
    std::string channel;
    std::string message;
};

class LoguruOutputCapture {
public:
    LoguruOutputCapture()
    {
        loguru::add_callback(kCallbackId, &LoguruOutputCapture::handle_log, this, loguru::Verbosity_INFO);
    }

    ~LoguruOutputCapture()
    {
        loguru::remove_callback(kCallbackId);
    }

    LoguruOutputCapture(const LoguruOutputCapture&) = delete;
    LoguruOutputCapture& operator=(const LoguruOutputCapture&) = delete;

    std::vector<CapturedLogLine> drain()
    {
        std::lock_guard lock{mutex_};
        std::vector<CapturedLogLine> out;
        out.reserve(lines_.size());
        while (!lines_.empty()) {
            out.push_back(std::move(lines_.front()));
            lines_.pop_front();
        }
        return out;
    }

private:
    static constexpr const char* kCallbackId = "rollnw.client.output_log";
    static constexpr size_t kMaxPendingLines = 512;

    static std::string channel_for(loguru::Verbosity verbosity)
    {
        if (verbosity <= loguru::Verbosity_ERROR) {
            return "error";
        }
        if (verbosity == loguru::Verbosity_WARNING) {
            return "warn";
        }
        return "info";
    }

    static std::string format_message(const loguru::Message& message)
    {
        std::string out;
        if (message.indentation && message.indentation[0] != '\0') {
            out += message.indentation;
        }
        if (message.prefix && message.prefix[0] != '\0') {
            out += message.prefix;
        }
        if (message.message && message.message[0] != '\0') {
            out += message.message;
        }
        if (out.empty() && message.preamble) {
            out = message.preamble;
        }
        return out;
    }

    void push(const loguru::Message& message)
    {
        CapturedLogLine line;
        line.channel = channel_for(message.verbosity);
        line.message = format_message(message);
        if (line.message.empty()) {
            return;
        }

        std::lock_guard lock{mutex_};
        while (lines_.size() >= kMaxPendingLines) {
            lines_.pop_front();
        }
        lines_.push_back(std::move(line));
    }

    static void handle_log(void* user_data, const loguru::Message& message) noexcept
    {
        auto* capture = static_cast<LoguruOutputCapture*>(user_data);
        if (!capture) {
            return;
        }

        try {
            capture->push(message);
        } catch (...) {
        }
    }

    std::mutex mutex_;
    std::deque<CapturedLogLine> lines_;
};

std::pair<int, int> query_window_pixels(SDL_Window* window)
{
    int pixel_w = 0;
    int pixel_h = 0;
    SDL_GetWindowSizeInPixels(window, &pixel_w, &pixel_h);
    if (pixel_w <= 0 || pixel_h <= 0) {
        SDL_GetWindowSize(window, &pixel_w, &pixel_h);
    }
    return {pixel_w, pixel_h};
}

std::pair<int, int> query_window_size(SDL_Window* window)
{
    int window_w = 0;
    int window_h = 0;
    SDL_GetWindowSize(window, &window_w, &window_h);
    return {window_w, window_h};
}

void log_window_metrics(SDL_Window* window, const char* label)
{
    if (!window) {
        return;
    }

    const auto window_size = query_window_size(window);
    const auto pixel_size = query_window_pixels(window);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
        "rollnw client window metrics [%s]: window=%dx%d pixels=%dx%d display_scale=%.3f pixel_density=%.3f flags=0x%llx",
        label,
        window_size.first,
        window_size.second,
        pixel_size.first,
        pixel_size.second,
        static_cast<double>(SDL_GetWindowDisplayScale(window)),
        static_cast<double>(SDL_GetWindowPixelDensity(window)),
        static_cast<unsigned long long>(SDL_GetWindowFlags(window)));
}

bool client_ui_dir_exists(const std::filesystem::path& path)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    return fs::is_directory(path, ec)
        && fs::exists(path / "package.json", ec)
        && fs::exists(path / "panel.rml", ec)
        && fs::exists(path / "panel.rcss", ec);
}

std::filesystem::path resolve_client_ui_dir()
{
    namespace fs = std::filesystem;
    std::error_code ec;

    const fs::path base_path = client_base_path();
    const fs::path cwd = fs::current_path(ec);
    const fs::path source_dir = fs::path{__FILE__}.parent_path();
    const std::array<fs::path, 4> candidates{
        base_path / "ui",
        cwd / "ui",
        cwd / "tools/client/ui",
        source_dir / "ui",
    };

    for (const fs::path& candidate : candidates) {
        if (client_ui_dir_exists(candidate)) {
            return fs::weakly_canonical(candidate, ec);
        }
    }

    return {};
}

constexpr size_t kInvalidVirtualIndex = std::numeric_limits<size_t>::max();

enum class AreaWorkspaceSurface : uint8_t {
    properties,
    objects,
    tiles,
};

struct AppState {
    nw::toolset::RmlSmallsBridge smalls;
    nw::toolset::ToolsetBackend backend;
    nw::toolset::ShellController shell;
    nw::toolset::ShellViewState shell_view;
    nw::toolset::PlayPreviewState play_preview;
    nw::toolset::RuntimeInputState runtime_input;
    nw::toolset::WorkspaceState workspace;
    nw::toolset::WorkspaceViewState workspace_view;
    nw::toolset::BrowserViewState browser;
    nw::toolset::CommandViewState command_view;
    nw::toolset::LoadingViewState loading;
    nw::toolset::DialogViewState dialog_view;
    nw::toolset::AreaTileEditorState area_tile_editor;
    AreaWorkspaceSurface area_workspace_surface
        = AreaWorkspaceSurface::properties;
    std::unique_ptr<nw::toolset::RmlSmallsLanguageBinding> rml_smalls_binding;
    std::unique_ptr<nw::toolset::RmlSmallsDataModel> rml_smalls_data_model;
    uint64_t observed_object_mutation_epoch = 0;
    uint64_t observed_area_structure_epoch = 0;
    nw::ObjectHandle stale_area_viewport{};
    bool backend_ready = false;
    std::filesystem::path client_executable;
    bool viewer_viewport_dragging = false;
    bool viewer_viewport_focused = false;
    nw::toolset::AreaObjectDragState area_object_drag;
    nw::toolset::AreaObjectPlacementState area_object_placement;
    nw::toolset::ProjectBlueprintDragState project_blueprint_drag;
    nw::toolset::ManagedListReorderState managed_list_reorder;
    ClientViewportDragMode viewer_viewport_drag_mode = ClientViewportDragMode::look;
    Rml::Vector2f viewer_viewport_last_point;
    nw::toolset::ClientMetricsState metrics;
    bool workspace_hover_refresh_pending = false;
    Rml::Vector2f workspace_hover_refresh_point;
    nw::toolset::ObjectWorkbenchViewState workbench;
};

nw::toolset::ClientInputOwnership client_input_ownership(const AppState& state, bool lifecycle_key = false)
{
    using namespace nw::toolset;
    const bool preview = state.play_preview.session.active() || state.play_preview.placement_pending();
    const auto map = client_input_map(ClientControlRole::editor, preview);
    const bool ui_pointer_gesture = state.shell_view.bottom_dock_resizing || state.shell_view.left_dock_resizing
        || state.shell_view.output_selection.dragging || state.workspace_view.workspace_tab_dragging
        || state.managed_list_reorder.active() || state.project_blueprint_drag.active();
    return {
        .map = map,
        .pointer_owner = state.viewer_viewport_dragging ? (preview ? ClientPointerOwner::pc : ClientPointerOwner::editor)
            : ui_pointer_gesture                        ? ClientPointerOwner::toolset
                                                        : ClientPointerOwner::none,
        .world_available = preview && state.workspace.active_tab_id() == state.play_preview.tab_id
            && state.backend.module_generation() == state.play_preview.module_generation
            && nw::kernel::objects().valid(state.play_preview.area)
            && (state.play_preview.placement_pending() || nw::kernel::objects().valid(state.play_preview.session.actor())),
        .command_modal = state.backend.blueprint_operation_active()
            || state.backend.blueprint_publication_pending(),
        .world_input_blocked = state.loading.module_dialog_open || state.loading.project_load.active(),
        .lifecycle_key = lifecycle_key,
    };
}

nw::toolset::ClientUiActionOwner client_ui_action_owner(const AppState& state)
{
    using namespace nw::toolset;
    const auto displayed = state.workbench.object_details.object;
    const auto script = state.smalls.active_object();
    const auto area = state.smalls.active_area();
    const bool running = state.play_preview.session.active();
    const bool placing = state.play_preview.placement_pending();
    return capture_client_ui_action_owner(state.workspace, {
                                                               .displayed_object = displayed,
                                                               .script_object = script,
                                                               .script_area = area,
                                                               .module_generation = state.backend.module_generation(),
                                                               .resource_generation = nw::kernel::resman().generation(),
                                                               .map = client_input_map(ClientControlRole::editor, running || placing),
                                                               .surface = state.workbench.object_workbench_surface,
                                                               .preview = running ? ClientPreviewPhase::running : placing ? ClientPreviewPhase::placing_actor
                                                                   : state.play_preview.selecting_actor                   ? ClientPreviewPhase::selecting_actor
                                                                                                                          : ClientPreviewPhase::inactive,
                                                               .area_surface = static_cast<uint8_t>(state.area_workspace_surface),
                                                               .live_objects = static_cast<uint8_t>((nw::kernel::objects().valid(displayed) ? 1 : 0) | (nw::kernel::objects().valid(script) ? 2 : 0) | (nw::kernel::objects().valid(area) ? 4 : 0)),
                                                               .command_modal = state.command_view.command_form.has_value() || state.command_view.command_palette_ui_visible || state.backend.blueprint_operation_active() || state.backend.blueprint_publication_pending(),
                                                               .world_blocked = state.loading.module_dialog_open || state.loading.project_load.active(),
                                                           });
}

bool synchronize_area_viewport_structure(
    ClientRenderer& renderer, AppState& state, bool report_failure);

bool ensure_backend_ready(AppState& state);
Rml::Element* find_ancestor_with_id(Rml::Element* element, std::string_view id);
void cancel_area_object_drag(ClientRenderer& renderer, AppState& state);
nw::toolset::CommandContext command_context(
    AppState& state, nw::toolset::CommandSource source);
void append_command_result(
    AppState& state, const nw::toolset::CommandResult& result);

nw::toolset::ShellPreviewLayout shell_preview_layout(const AppState& state)
{
    const bool pending = state.play_preview.placement_pending();
    return {.active = state.play_preview.session.active() || pending, .placement_pending = pending};
}

void apply_shell_layout(Rml::ElementDocument* doc, const AppState& state)
{
    nw::toolset::apply_shell_layout(doc, state.shell, shell_preview_layout(state));
}

void apply_left_dock_width(Rml::ElementDocument* doc, AppState& state, SDL_Window* window, int requested_width_px)
{
    nw::toolset::apply_left_dock_width(doc, state.shell, window, requested_width_px, shell_preview_layout(state));
}

void apply_bottom_dock_height(Rml::ElementDocument* doc, AppState& state, SDL_Window* window, int requested_height_px)
{
    nw::toolset::apply_bottom_dock_height(doc, state.shell, window, requested_height_px, shell_preview_layout(state));
}

bool begin_bottom_dock_resize(Rml::Context* context, SDL_Window* window, Rml::ElementDocument* doc,
    AppState& state, const SDL_MouseButtonEvent& mouse)
{
    return nw::toolset::begin_bottom_dock_resize(context, window, doc, state.shell_view, state.shell, mouse);
}

bool update_bottom_dock_resize(Rml::ElementDocument* doc, AppState& state, SDL_Window* window, const SDL_MouseMotionEvent& motion)
{
    return nw::toolset::update_bottom_dock_resize(doc, state.shell_view, state.shell, window, motion, shell_preview_layout(state));
}

bool end_bottom_dock_resize(AppState& state)
{
    if (!nw::toolset::end_bottom_dock_resize(state.shell_view)) { return false; }
    save_ui_preferences(state.shell_view.preferences_path, state.shell.docks, state.browser.recent_projects);
    return true;
}

bool begin_left_dock_resize(Rml::Context* context, SDL_Window* window, Rml::ElementDocument* doc,
    AppState& state, const SDL_MouseButtonEvent& mouse)
{
    return nw::toolset::begin_left_dock_resize(context, window, doc, state.shell_view, state.shell, mouse);
}

bool update_left_dock_resize(Rml::ElementDocument* doc, AppState& state, SDL_Window* window, const SDL_MouseMotionEvent& motion)
{
    return nw::toolset::update_left_dock_resize(doc, state.shell_view, state.shell, window, motion, shell_preview_layout(state));
}

bool end_left_dock_resize(AppState& state)
{
    if (!nw::toolset::end_left_dock_resize(state.shell_view)) { return false; }
    save_ui_preferences(state.shell_view.preferences_path, state.shell.docks, state.browser.recent_projects);
    return true;
}

bool consume_terminal_toggle_text_input(AppState& state, const SDL_Event& event)
{
    return nw::toolset::consume_terminal_toggle_text_input(state.shell_view, event);
}

std::string escape_html(std::string_view text)
{
    std::string out;
    out.reserve(text.size() + 16);
    for (const char ch : text) {
        switch (ch) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        default:
            out.push_back(ch);
            break;
        }
    }
    return out;
}

void remember_recent_project(AppState& state, const std::filesystem::path& project_dir)
{
    nw::toolset::remember_recent_project(state.shell_view.preferences_path, state.shell.docks, state.browser.recent_projects, project_dir);
}

Rml::Element* find_el(Rml::ElementDocument* doc, const char* id)
{
    return doc ? doc->GetElementById(id) : nullptr;
}

Rml::Element* find_ancestor_with_class(Rml::Element* element, std::string_view class_name)
{
    for (auto* cursor = element; cursor; cursor = cursor->GetParentNode()) {
        if (cursor->IsClassSet(class_name.data())) {
            return cursor;
        }
    }
    return nullptr;
}

Rml::Element* find_ancestor_with_id(Rml::Element* element, std::string_view id)
{
    for (auto* cursor = element; cursor; cursor = cursor->GetParentNode()) {
        if (cursor->GetId() == id) {
            return cursor;
        }
    }
    return nullptr;
}

void hide_object_variable_warning_tooltip(
    Rml::ElementDocument* doc, AppState& state)
{
    nw::toolset::hide_object_variable_warning_tooltip(doc, state.workbench);
}

// One window has one pointer and one transient warning tooltip. This is a true
// UI singleton; there is no batch of simultaneous hover targets to process.
void sync_object_variable_warning_tooltip(Rml::ElementDocument* doc,
    AppState& state,
    Rml::Element* hit,
    Rml::Vector2f point,
    int viewport_width,
    int viewport_height)
{
    nw::toolset::sync_object_variable_warning_tooltip(doc, state.workbench, state.shell.command_palette_visible, hit, point, viewport_width, viewport_height);
}

using nw::toolset::close_active_smalls_selector;

void apply_workspace_tab_scroll(Rml::ElementDocument* doc, AppState& state);
void apply_object_workbench_tab_scroll(Rml::ElementDocument* doc, AppState& state);

void clear_workspace_tab_drag(AppState& state);
std::optional<WorkspaceViewerViewportRequest> active_workspace_viewer_viewport_request(
    Rml::ElementDocument* doc, AppState& state, int frame_width, int frame_height);


bool point_within_viewport(ClientViewportRect rect, Rml::Vector2f point)
{
    return rect.contains_point(point.x, point.y);
}

bool shift_only(SDL_Keymod modifiers) noexcept
{
    return (modifiers & SDL_KMOD_SHIFT) != 0
        && (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) == 0;
}

nw::toolset::AreaTilePointerModifier area_tile_pointer_modifier(
    SDL_Keymod modifiers) noexcept
{
    if ((modifiers & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) != 0) {
        return nw::toolset::AreaTilePointerModifier::blocked;
    }
    return (modifiers & SDL_KMOD_SHIFT) != 0
        ? nw::toolset::AreaTilePointerModifier::select
        : nw::toolset::AreaTilePointerModifier::none;
}

nw::toolset::AreaTilePointerButton area_tile_pointer_button(
    uint8_t button) noexcept
{
    if (button == SDL_BUTTON_LEFT) {
        return nw::toolset::AreaTilePointerButton::primary;
    }
    if (button == SDL_BUTTON_RIGHT) {
        return nw::toolset::AreaTilePointerButton::secondary;
    }
    return nw::toolset::AreaTilePointerButton::other;
}

bool command_palette_contains_point(
    Rml::ElementDocument* palette_doc, const AppState& state, Rml::Vector2f point)
{
    return state.shell.command_palette_visible
        && point_within_element(palette_doc, "command_palette", point);
}

void observe_output_scroll(Rml::ElementDocument* doc, AppState& state)
{
    nw::toolset::observe_output_scroll(doc, state.shell);
}

bool apply_output_scroll_after_layout(Rml::ElementDocument* doc, AppState& state)
{
    return nw::toolset::apply_output_scroll_after_layout(doc, state.shell_view, state.shell);
}

std::optional<size_t> output_text_offset_at_point(Rml::Context* context,
    Rml::ElementDocument* doc, const AppState& state, Rml::Vector2f point)
{
    return nw::toolset::output_text_offset_at_point(context, doc, state.shell_view, point);
}

void clear_rml_focus(Rml::Context* context)
{
    if (auto* focus = context ? context->GetFocusElement() : nullptr) {
        focus->Blur();
    }
}

void focus_workspace_viewport(Rml::ElementDocument* doc, AppState& state)
{
    clear_rml_focus(doc ? doc->GetContext() : nullptr);
    state.viewer_viewport_focused = true;
    if (state.command_view.command_palette_restore_captured) {
        state.command_view.command_palette_restore_focus_id.clear();
        state.command_view.command_palette_restore_viewport_focus = true;
    }
}

std::optional<int32_t> parse_decimal_int32(std::string_view value)
{
    if (value.empty()) {
        return std::nullopt;
    }

    int32_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    return result;
}

std::optional<uint64_t> parse_decimal_uint64(std::string_view value)
{
    if (value.empty()) {
        return std::nullopt;
    }

    uint64_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    return result;
}

bool viewport_mouse_hit_blocked(Rml::ElementDocument* doc, Rml::Element* top_hit, Rml::Vector2f point, const AppState& state)
{
    if (state.shell.bottom_dock_visible() && point_within_element(doc, "bottom_dock", point)) {
        return true;
    }
    if (state.loading.module_dialog_open) {
        return true;
    }

    if (!top_hit) {
        return false;
    }

    return !find_ancestor_with_id(top_hit, "workspace_viewer_viewport");
}

std::optional<ClientViewportCameraCommand> viewer_viewport_camera_command_from_key(
    SDL_Keycode key, WorkspaceViewerViewportKind kind)
{
    const bool preview_controls = kind == WorkspaceViewerViewportKind::preview;
    switch (key) {
    case SDLK_W:
        return preview_controls ? ClientViewportCameraCommand::pitch_up : ClientViewportCameraCommand::move_forward;
    case SDLK_S:
        return preview_controls ? ClientViewportCameraCommand::pitch_down : ClientViewportCameraCommand::move_backward;
    case SDLK_A:
        return preview_controls ? ClientViewportCameraCommand::yaw_left : ClientViewportCameraCommand::move_left;
    case SDLK_D:
        return preview_controls ? ClientViewportCameraCommand::yaw_right : ClientViewportCameraCommand::move_right;
    case SDLK_Q:
        return preview_controls ? ClientViewportCameraCommand::zoom_out : ClientViewportCameraCommand::move_down;
    case SDLK_E:
        return preview_controls ? ClientViewportCameraCommand::zoom_in : ClientViewportCameraCommand::move_up;
    case SDLK_LEFT:
        return ClientViewportCameraCommand::yaw_left;
    case SDLK_RIGHT:
        return ClientViewportCameraCommand::yaw_right;
    case SDLK_UP:
        return ClientViewportCameraCommand::pitch_up;
    case SDLK_DOWN:
        return ClientViewportCameraCommand::pitch_down;
    case SDLK_F:
        return ClientViewportCameraCommand::fit;
    case SDLK_G:
        if (!preview_controls) {
            return ClientViewportCameraCommand::gameplay;
        }
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

bool handle_viewer_viewport_key(ClientRenderer& renderer,
    Rml::Context* context,
    Rml::ElementDocument* doc,
    AppState& state,
    const SDL_KeyboardEvent& key,
    int frame_width,
    int frame_height)
{
    if (state.shell.command_palette_visible
        || focused_text_input(context)
        || state.loading.module_dialog_open
        || (key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI))) {
        return false;
    }

    if (!viewer_viewport_camera_command_from_key(key.key, WorkspaceViewerViewportKind::area)) {
        return false;
    }

    auto viewer_viewport = active_workspace_viewer_viewport_request(doc, state, frame_width, frame_height);
    if (!viewer_viewport) {
        state.viewer_viewport_focused = false;
        return false;
    }
    const bool tile_camera_context
        = state.area_workspace_surface == AreaWorkspaceSurface::tiles
        && viewer_viewport->kind == WorkspaceViewerViewportKind::area;
    if (!state.viewer_viewport_focused && !tile_camera_context) {
        return false;
    }

    auto command = viewer_viewport_camera_command_from_key(key.key, viewer_viewport->kind);
    if (!command) {
        return false;
    }

    state.viewer_viewport_focused = true;
    const float scale = (key.mod & SDL_KMOD_SHIFT) ? 3.0f : 1.0f;
    cancel_area_object_drag(renderer, state);
    renderer.viewer_viewport_camera_command(*command, scale, viewer_viewport->rect);
    if (tile_camera_context) {
        state.area_tile_editor.cursor_update_pending = true;
    }
    return true;
}

bool recent_list_hit_blocked(Rml::ElementDocument* doc, Rml::Element* top_hit, Rml::Vector2f point, const AppState& state)
{
    if (!doc) {
        return true;
    }

    if (find_ancestor_with_id(top_hit, "recent_list") || find_ancestor_with_id(top_hit, "panel")) {
        return false;
    }

    if (state.shell.bottom_dock_visible() && point_within_element(doc, "bottom_dock", point)) {
        return true;
    }

    return false;
}

void apply_workspace_tab_scroll(Rml::ElementDocument* doc, AppState& state)
{
    apply_tab_scroll(doc, kWorkspaceTabScrollStrip,
        state.workspace_view.workspace_tab_scroll_x);
}

void apply_object_workbench_tab_scroll(
    Rml::ElementDocument* doc, AppState& state)
{
    apply_tab_scroll(doc, kObjectWorkbenchTabScrollStrip,
        state.workbench.object_workbench_tab_scroll_x);
}

void clear_workspace_tab_drag(AppState& state)
{
    nw::toolset::clear_workspace_tab_drag(state.workspace_view);
}

void set_recent_hover(Rml::ElementDocument* doc, AppState& state, int index)
{
    nw::toolset::set_recent_hover(doc, state.browser, index);
}

void set_recent_selected(Rml::ElementDocument* doc, AppState& state, int index)
{
    nw::toolset::set_recent_selected(doc, state.browser, index);
}

std::string get_input_value(Rml::ElementDocument* doc, const char* id)
{
    if (!doc) {
        return {};
    }
    if (auto* input = doc->GetElementById(id)) {
        if (auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(input)) {
            return control->GetValue();
        }
        return input->GetAttribute<Rml::String>("value", "");
    }
    return {};
}

void set_input_value(Rml::ElementDocument* doc, const char* id, std::string_view value)
{
    if (!doc) {
        return;
    }
    if (auto* input = doc->GetElementById(id)) {
        if (auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(input)) {
            control->SetValue(Rml::String(value));
            return;
        }
        input->SetAttribute("value", Rml::String(value));
    }
}

void append_output(AppState& state, std::string_view channel, std::string_view line)
{
    state.shell.append_output(channel, line);
}

void dispatch_managed_list_events(AppState& state)
{
    auto& host = nw::toolset::ui_v1_host();
    host.drain_events([&](const nw::toolset::UiListEvent& event) {
        const auto* callback = host.callback_ptr(event.list_id(), event.type);
        if (!callback) {
            return;
        }
        const std::string qualified_function = *callback;
        const auto result = state.smalls.call_ui_list_callback(
            qualified_function, event);
        if (!result.ok) {
            append_output(state, "error", result.message);
        }
    });
}

bool synchronize_smalls_runtime(AppState& state)
{
    if (!state.rml_smalls_binding || !state.rml_smalls_data_model
        || !state.smalls.initialize()) {
        return false;
    }
    auto& runtime = nw::kernel::runtime();
    return state.rml_smalls_binding->initialize(runtime)
        && state.rml_smalls_data_model->synchronize(runtime);
}

void refresh_smalls_elements(Rml::ElementDocument* document, AppState& state)
{
    if (document && synchronize_smalls_runtime(state)) {
        state.rml_smalls_binding->refresh_elements(document);
        state.rml_smalls_data_model->dirty_all();
    }
}

struct ManagedListActivationResult {
    std::optional<nw::toolset::ManagedListFocusTarget> focus_target;
    bool activated = false;
};

ManagedListActivationResult activate_managed_list(Rml::ElementDocument* document,
    AppState& state,
    Rml::Element* hit)
{
    ManagedListActivationResult result;
    result.focus_target = nw::toolset::managed_list_focus_target(hit);
    auto& host = nw::toolset::ui_v1_host();
    if (!nw::toolset::activate_managed_list_element(hit, host)) {
        return result;
    }
    dispatch_managed_list_events(state);
    refresh_smalls_elements(document, state);
    nw::toolset::sync_managed_lists(
        document, host, state.workbench.managed_lists, true);
    result.activated = true;
    return result;
}

bool cycle_managed_list(Rml::ElementDocument* document,
    AppState& state,
    Rml::Element* element,
    int delta)
{
    const auto focus_target = nw::toolset::managed_list_focus_target(element);
    auto& host = nw::toolset::ui_v1_host();
    if (!nw::toolset::cycle_managed_list_element(element, host, delta)) {
        return false;
    }
    dispatch_managed_list_events(state);
    refresh_smalls_elements(document, state);
    nw::toolset::sync_managed_lists(
        document, host, state.workbench.managed_lists, true);
    if (focus_target) {
        (void)nw::toolset::focus_managed_list_target(
            document, *focus_target);
    }
    return true;
}

void flush_log_capture(LoguruOutputCapture& capture, AppState& state)
{
    for (auto& line : capture.drain()) {
        append_output(state, line.channel, line.message);
    }
}

void append_terminal(AppState& state, std::string_view style, std::string_view line)
{
    state.shell.append_terminal(style, line);
}

void refresh_terminal_view(Rml::ElementDocument* doc, AppState& state)
{
    nw::toolset::refresh_terminal_view(doc, state.shell);
}

void refresh_bottom_dock_view(Rml::ElementDocument* doc, AppState& state)
{
    nw::toolset::refresh_bottom_dock_view(doc, state.shell, shell_preview_layout(state));
}

void refresh_output_view(Rml::ElementDocument* doc, AppState& state)
{
    nw::toolset::refresh_output_view(doc, state.shell_view, state.shell);
}

bool render_project_tree_window(Rml::ElementDocument* doc, AppState& state, bool force)
{
    return nw::toolset::render_project_tree_window(doc, state.browser, force);
}

void refresh_recent_list(Rml::ElementDocument* doc, AppState& state)
{
    if (!doc) { return; }
    nw::toolset::refresh_browser_view(doc, state.browser, state.backend, state.shell,
        state.backend_ready, state.play_preview.selecting_actor);
    apply_shell_layout(doc, state);
}

std::optional<nw::toolset::ResourceDocument> resource_document_for_tab(const AppState& state,
    const nw::toolset::WorkspaceTab& active_tab)
{
    return nw::toolset::resource_document_for_tab(state.backend.current_project_dir(), active_tab);
}

void ensure_active_dialog_document(AppState& state)
{
    nw::toolset::ensure_active_dialog_document(state.dialog_view, state.backend.current_project_dir(), state.workspace.active_tab());
}

bool active_object_details_matches_tab(const AppState& state)
{
    return nw::toolset::active_object_details_matches_tab(state.workbench, state.workspace);
}

bool active_object_matches_tab(const AppState& state)
{
    return nw::toolset::active_object_matches_tab(state.workbench, state.workspace);
}

void configure_details_list(AppState& state)
{
    nw::toolset::configure_details_list(state.workbench);
}

void close_object_details_combobox(
    Rml::ElementDocument* doc, AppState& state)
{
    nw::toolset::close_object_details_combobox(doc, state.workbench);
}

bool open_object_details_sound_position_combobox(
    Rml::ElementDocument* doc, AppState& state, uint32_t row_index)
{
    return nw::toolset::open_object_details_sound_position_combobox(doc, state.workbench, state.workspace, row_index);
}

bool sync_object_details_combobox(
    Rml::ElementDocument* doc, AppState& state, bool force = false)
{
    return nw::toolset::sync_object_details_combobox(doc, state.workbench, state.workspace, force);
}

void rebuild_active_object_details(AppState& state, nw::ObjectHandle object)
{
    nw::toolset::rebuild_object_workbench_snapshots(state.workbench, object);
}

void clear_color_editor(AppState& state);

void clear_active_object_details(AppState& state)
{
    nw::toolset::clear_object_workbench_snapshots(state.workbench);
    state.managed_list_reorder = {};
    nw::toolset::clear_object_workbench_children(state.workbench);
}

bool sync_object_variable_window(Rml::ElementDocument* doc, AppState& state, bool force)
{
    return nw::toolset::sync_object_variable_window(doc, state.workbench, state.workspace, force);
}

bool sync_active_module_object(AppState& state)
{
    const auto* active_tab = state.workspace.active_tab();
    if (!active_tab || active_tab->kind != nw::toolset::WorkspaceTabKind::home) {
        return false;
    }

    const nw::ObjectHandle object = state.backend.module_object();
    if (object.type != nw::ObjectType::module) {
        if (state.workbench.active_object_tab_id == active_tab->id) {
            state.smalls.clear_active_object();
            state.workbench.active_object_tab_id.clear();
            clear_active_object_details(state);
        }
        return false;
    }

    const bool changed = state.workbench.object_details.object != object
        || state.workbench.active_object_tab_id != active_tab->id
        || state.workbench.object_details.status != nw::toolset::ObjectDetailsStatus::ready;
    state.smalls.publish_active_object(object);
    state.smalls.clear_active_area();
    state.workbench.active_object_tab_id = active_tab->id;
    if (!changed) {
        return true;
    }

    clear_active_object_details(state);
    configure_details_list(state);
    state.workbench.details_list.set_scroll_top(0);
    rebuild_active_object_details(state, object);
    state.observed_object_mutation_epoch = nw::toolset::object_mutation_state().epoch;
    return true;
}

bool sync_object_details_window(Rml::ElementDocument* doc, AppState& state, bool force)
{
    return nw::toolset::sync_object_details_window(doc, state.workbench, state.workspace, force);
}

void rebuild_active_creature_feats(AppState& state, nw::ObjectHandle object)
{
    nw::toolset::rebuild_active_creature_feats(state.workbench.creature_view, object);
}

bool sync_creature_feat_window(Rml::ElementDocument* doc, AppState& state, bool force)
{
    return nw::toolset::sync_creature_feat_window(doc, state.workbench.creature_view, nw::toolset::object_workbench_target(state.workbench, state.workspace), force);
}

bool active_creature_spells_match_tab(const AppState& state)
{
    return nw::toolset::active_creature_spells_match_tab(state.workbench.creature_view, nw::toolset::object_workbench_target(state.workbench, state.workspace));
}

bool active_creature_spell_filter_matches_tab(const AppState& state)
{
    return nw::toolset::active_creature_spell_filter_matches_tab(state.workbench.creature_view, nw::toolset::object_workbench_target(state.workbench, state.workspace));
}

void clear_creature_spell_filter(AppState& state)
{
    nw::toolset::clear_creature_spell_filter(state.workbench.creature_view);
}

void filter_active_creature_spells(AppState& state)
{
    nw::toolset::filter_active_creature_spells(state.workbench.creature_view);
}

bool open_creature_spell_filter(AppState& state, CreatureSpellFilterField field)
{
    return nw::toolset::open_creature_spell_filter(state.workbench.creature_view, nw::toolset::object_workbench_target(state.workbench, state.workspace), field);
}

bool commit_creature_spell_filter(AppState& state, int32_t value)
{
    return nw::toolset::commit_creature_spell_filter(state.workbench.creature_view, nw::toolset::object_workbench_target(state.workbench, state.workspace), value);
}

bool sync_creature_spell_window(Rml::ElementDocument* doc, AppState& state, bool force)
{
    return nw::toolset::sync_creature_spell_window(doc, state.workbench.creature_view, nw::toolset::object_workbench_target(state.workbench, state.workspace), force);
}

bool sync_creature_spell_filter_window(
    Rml::ElementDocument* doc, AppState& state, bool force)
{
    return nw::toolset::sync_creature_spell_filter_window(doc, state.workbench.creature_view, nw::toolset::object_workbench_target(state.workbench, state.workspace), force);
}

bool active_creature_inventory_matches_tab(const AppState& state)
{
    return nw::toolset::active_creature_inventory_matches_tab(state.workbench.inventory_view, nw::toolset::object_workbench_target(state.workbench, state.workspace));
}

bool sync_creature_inventory_window(Rml::ElementDocument* doc, AppState& state, bool force)
{
    return nw::toolset::sync_creature_inventory_window(doc, state.workbench.inventory_view, nw::toolset::object_workbench_target(state.workbench, state.workspace), force);
}

bool sync_area_tile_palette_window(Rml::ElementDocument* doc, AppState& state, bool force)
{
    const auto* tab = state.workspace.active_tab();
    const nw::ObjectHandle area = tab && tab->kind == nw::toolset::WorkspaceTabKind::area
        ? tab->document.object()
        : nw::ObjectHandle{};
    return nw::toolset::sync_area_tile_palette_window(doc, state.area_tile_editor, area,
        state.area_workspace_surface == AreaWorkspaceSurface::tiles,
        area_tile_pointer_modifier(SDL_GetModState()), force);
}

const nw::toolset::AppearanceCatalog& active_appearance_catalog(const AppState& state)
{
    return nw::toolset::active_appearance_catalog(state.workbench.appearance_view);
}

void clear_color_editor(AppState& state)
{
    nw::toolset::clear_color_editor(state.workbench.appearance_view);
}

void close_appearance_selector(AppState& state)
{
    nw::toolset::close_appearance_selector(state.workbench.appearance_view);
}

void rebuild_active_appearances(AppState& state, nw::ObjectHandle object)
{
    nw::toolset::rebuild_active_appearances(state.workbench.appearance_view, state.backend.module_generation(), object);
}

bool active_appearances_match_tab(const AppState& state)
{
    return nw::toolset::active_appearances_match_tab(state.workbench.appearance_view, nw::toolset::object_workbench_target(state.workbench, state.workspace));
}

bool open_color_editor(AppState& state, nw::ObjectHandle object, uint32_t color)
{
    return nw::toolset::open_color_editor(state.workbench.appearance_view, object, color);
}

bool sync_appearance_window(Rml::ElementDocument* doc, AppState& state, bool force)
{
    return nw::toolset::sync_appearance_window(doc, state.workbench.appearance_view, nw::toolset::object_workbench_target(state.workbench, state.workspace), force);
}

void close_sound_resource_selector(AppState& state)
{
    nw::toolset::close_sound_resource_selector(state.workbench.appearance_view);
}

bool active_sound_resource_selector_matches_tab(const AppState& state)
{
    return nw::toolset::active_sound_resource_selector_matches_tab(state.workbench.appearance_view, nw::toolset::object_workbench_target(state.workbench, state.workspace));
}

void rebuild_sound_catalog(AppState& state, bool reset_selection)
{
    nw::toolset::rebuild_sound_catalog(state.workbench.appearance_view, state.backend_ready ? nw::kernel::resman().generation() : 0, reset_selection);
}

bool sync_sound_catalog_window(
    Rml::ElementDocument* doc, AppState& state, bool force)
{
    return nw::toolset::sync_sound_catalog_window(doc, state.workbench.appearance_view, nw::toolset::object_workbench_target(state.workbench, state.workspace), state.backend_ready ? nw::kernel::resman().generation() : 0, force);
}

bool commit_sound_catalog_selection(AppState& state, uint32_t row_index)
{
    return nw::toolset::commit_sound_catalog_selection(state.workbench.appearance_view, nw::toolset::object_workbench_target(state.workbench, state.workspace), state.backend, state.shell, command_context(state, nw::toolset::CommandSource::widget), row_index);
}

bool commit_active_appearance_selection(AppState& state, int32_t value)
{
    return nw::toolset::commit_active_appearance_selection(state.workbench.appearance_view, state.backend, state.shell, command_context(state, nw::toolset::CommandSource::widget), value);
}

bool cycle_active_appearance(AppState& state, int direction)
{
    return nw::toolset::cycle_active_appearance(state.workbench.appearance_view, nw::toolset::object_workbench_target(state.workbench, state.workspace), state.backend, state.shell, command_context(state, nw::toolset::CommandSource::widget), direction);
}

bool commit_active_color_selection(AppState& state, int32_t value)
{
    return nw::toolset::commit_active_color_selection(state.workbench.appearance_view, nw::toolset::object_workbench_target(state.workbench, state.workspace), state.backend, state.shell, command_context(state, nw::toolset::CommandSource::widget), value);
}

bool sync_appearance_body_preview(ClientRenderer& renderer, AppState& state)
{
    return nw::toolset::sync_appearance_body_preview(renderer, state.workbench.appearance_view, nw::toolset::object_workbench_target(state.workbench, state.workspace));
}


void append_placed_area_object_list_markup(
    std::string& content_markup, const AppState& state)
{
    std::vector<nw::toolset::PlacedAreaObjectRow> rows;
    const auto* area = nw::kernel::objects().get<nw::Area>(state.smalls.active_area());
    if (area) {
        nw::toolset::build_placed_area_object_rows(*area, rows);
    }

    content_markup += "<div id=\"object_workbench\" class=\"object_workbench area_object_list_workbench\">";
    content_markup += "<div class=\"object_workbench_header area_object_list_header\">";
    content_markup += "<div class=\"object_workbench_title\">Placed Objects</div>";
    content_markup += "<span class=\"area_object_list_count\">";
    content_markup += std::to_string(rows.size());
    content_markup += "</span></div><div class=\"area_object_list\">";
    if (!area) {
        content_markup += "<div class=\"property_tree_empty\">Loading placed objects...</div>";
    } else if (rows.empty()) {
        content_markup += "<div class=\"property_tree_empty\">This area has no placed objects.</div>";
    } else {
        for (const auto& row : rows) {
            content_markup += "<button type=\"button\" class=\"area_object_row\" data-object=\"";
            content_markup += std::to_string(row.object.to_ull());
            content_markup += "\"><span class=\"area_object_row_name\">";
            content_markup += escape_html(row.name);
            content_markup += "</span><span class=\"area_object_row_type\">";
            content_markup += escape_html(
                nw::toolset::placed_area_object_type_label(row.object.type));
            content_markup += "</span></button>";
        }
    }
    content_markup += "</div></div>";
}

void append_object_workbench_markup(std::string& content_markup, const AppState& state)
{
    const auto* tab = state.workspace.active_tab();
    if (tab && tab->kind == nw::toolset::WorkspaceTabKind::area
        && state.area_workspace_surface == AreaWorkspaceSurface::objects
        && !active_object_matches_tab(state)) {
        append_placed_area_object_list_markup(content_markup, state);
        return;
    }
    nw::toolset::append_object_workbench_markup(content_markup, state.workbench, state.workspace, state.backend);
}

void append_workspace_document_markup(std::string& content_markup,
    const nw::toolset::WorkspaceTab& active_tab,
    const AppState& state)
{
    if (active_tab.kind == nw::toolset::WorkspaceTabKind::area) {
        const bool has_resource = !active_tab.detail.empty();
        content_markup += "<div class=\"workspace_area_surface workspace_viewer_surface\">";
        content_markup += "<div class=\"workspace_area_toolbar\"><div class=\"workspace_area_title\">";
        content_markup += escape_html(active_tab.title);
        content_markup += "</div><div class=\"workspace_area_detail\">";
        content_markup += escape_html(workspace_tab_detail(active_tab));
        content_markup += "</div><div class=\"workspace_area_tabs object_workbench_tab_track\">";
        struct AreaSurfaceTab {
            AreaWorkspaceSurface surface;
            std::string_view id;
            std::string_view label;
        };
        constexpr std::array tabs{
            AreaSurfaceTab{AreaWorkspaceSurface::properties,
                "properties", "Properties"},
            AreaSurfaceTab{AreaWorkspaceSurface::objects,
                "objects", "Objects"},
            AreaSurfaceTab{AreaWorkspaceSurface::tiles,
                "tiles", "Tiles"},
        };
        for (const auto& tab : tabs) {
            content_markup += "<div class=\"area_workspace_tab object_workbench_tab";
            if (state.area_workspace_surface == tab.surface) {
                content_markup += " active";
            }
            content_markup += "\" data-area-surface=\"";
            content_markup += tab.id;
            content_markup += "\">";
            content_markup += tab.label;
            content_markup += "</div>";
        }
        content_markup += "</div></div>";
        content_markup += "<div class=\"workspace_preview_body workspace_area_body\"><div id=\"workspace_viewer_viewport\" class=\"workspace_viewer_viewport";
        if (!has_resource) {
            content_markup += " empty";
        }
        content_markup += "\" data-resource=\"";
        content_markup += escape_html(active_tab.detail);
        content_markup += "\">";
        if (!has_resource) {
            content_markup += "<div class=\"workspace_area_placeholder\">Open an area from the project tree.</div>";
        }
        content_markup += "</div>";
        if (state.area_workspace_surface == AreaWorkspaceSurface::tiles) {
            nw::toolset::append_area_tile_palette_markup(content_markup, state.area_tile_editor);
        } else {
            append_object_workbench_markup(content_markup, state);
        }
        content_markup += "</div></div>";
        return;
    }

    if (active_tab.kind == nw::toolset::WorkspaceTabKind::preview) {
        const bool has_resource = !active_tab.detail.empty();
        content_markup += "<div class=\"workspace_area_surface workspace_viewer_surface\">";
        content_markup += "<div class=\"workspace_area_toolbar\"><div class=\"workspace_area_title\">";
        content_markup += escape_html(active_tab.title);
        content_markup += "</div><div class=\"workspace_area_detail\">";
        content_markup += escape_html(workspace_tab_detail(active_tab));
        content_markup += "</div></div>";
        content_markup += "<div class=\"workspace_preview_body";
        if (data_workbench_only(state.workbench.object_details.object.type,
                state.workbench.object_workbench_surface)) {
            content_markup += " data_workbench_only";
        }
        content_markup += "\"><div id=\"workspace_viewer_viewport\" class=\"workspace_viewer_viewport";
        if (!has_resource) {
            content_markup += " empty";
        }
        content_markup += "\" data-resource=\"";
        content_markup += escape_html(active_tab.detail);
        content_markup += "\">";
        if (!has_resource) {
            content_markup += "<div class=\"workspace_area_placeholder\">Open a previewable blueprint from the project tree.</div>";
        }
        content_markup += "</div>";
        append_object_workbench_markup(content_markup, state);
        content_markup += "</div></div>";
        return;
    }

    if (active_tab.kind == nw::toolset::WorkspaceTabKind::dialog) {
        content_markup += nw::toolset::dialog_view_markup(state.dialog_view);
        return;
    }

    if (active_tab.kind == nw::toolset::WorkspaceTabKind::resource) {
        const auto document = resource_document_for_tab(state, active_tab);
        content_markup += "<div class=\"workspace_resource_surface\">";
        if (document) {
            nw::toolset::append_resource_document_inspector(content_markup, *document);
        } else {
            nw::toolset::append_missing_resource_document(content_markup);
        }
        content_markup += "</div>";
        return;
    }

    content_markup += "<div class=\"workspace_document workspace_document_";
    content_markup += workspace_tab_kind_class(active_tab.kind);
    content_markup += "\"><div class=\"workspace_document_title\">";
    content_markup += escape_html(active_tab.title);
    content_markup += "</div><div class=\"workspace_document_detail\">";
    content_markup += escape_html(workspace_tab_detail(active_tab));
    content_markup += "</div></div>";
}

bool sync_workspace_tab_elements(Rml::ElementDocument* doc, AppState& state)
{
    return nw::toolset::sync_workspace_tab_elements(doc, state.workspace_view, state.workspace);
}

bool remove_workspace_tab_element(Rml::ElementDocument* doc, AppState& state, std::string_view tab_id)
{
    return nw::toolset::remove_workspace_tab_element(doc, state.workspace_view, state.workspace, tab_id);
}

void append_workspace_home_markup(std::string& content_markup, AppState& state)
{
    const bool module_open = nw::toolset::append_workspace_home_start_markup(content_markup, state.browser,
        state.backend, state.loading, ROLLNW_TOOL_NAME " " ROLLNW_TOOL_VERSION);
    if (module_open) { append_object_workbench_markup(content_markup, state); }
    content_markup += "</div>";
}

bool workspace_home_active(const AppState& state)
{
    const auto* tab = state.workspace.active_tab();
    return !tab || tab->kind == nw::toolset::WorkspaceTabKind::home;
}

void refresh_home_area_catalog(AppState& state, bool force)
{
    nw::toolset::refresh_home_area_catalog(state.browser, state.backend, force);
}

bool sync_home_area_window(Rml::ElementDocument* doc, AppState& state, bool force)
{
    return nw::toolset::sync_home_area_window(doc, state.browser, workspace_home_active(state), force);
}

void refresh_workspace_content_impl(Rml::ElementDocument* doc, AppState& state, bool preserve_controls)
{
    if (!doc) {
        return;
    }
    if (preserve_controls && state.workbench.creature_view.creature_spell_combobox.popup_visible()) {
        state.workbench.creature_view.creature_spell_combobox.invalidate_popup_render();
    }
    if (preserve_controls && active_appearances_match_tab(state)) {
        if (auto* editor = find_el(doc, "appearance_editor")) {
            state.workbench.appearance_view.appearance_editor_scroll_top = std::max(0.0f, editor->GetScrollTop());
        }
    }
    remember_tab_scroll(doc, kObjectWorkbenchTabScrollStrip,
        state.workbench.object_workbench_tab_scroll_x);

    ensure_active_dialog_document(state);
    sync_active_module_object(state);
    refresh_home_area_catalog(state, false);
    const auto* active_tab = state.workspace.active_tab();
    const bool focus_area = nw::toolset::area_viewport_changed(doc, active_tab);
    std::string content_markup;
    if (!active_tab || active_tab->kind == nw::toolset::WorkspaceTabKind::home) {
        append_workspace_home_markup(content_markup, state);
    } else {
        append_workspace_subtabs_markup(content_markup, *active_tab);
        append_workspace_document_markup(content_markup, *active_tab, state);
    }

    if (auto* content = doc->GetElementById("workspace_content")) {
        content->SetInnerRML(content_markup);
    }
    state.workbench.object_workbench_tab_scroll_pending = true;
    apply_shell_layout(doc, state);
    sync_home_area_window(doc, state, true);
    nw::toolset::hydrate_object_workbench(doc, state.workbench, state.workspace);
    refresh_smalls_elements(doc, state);
    if (preserve_controls && active_appearances_match_tab(state)) {
        if (auto* editor = find_el(doc, "appearance_editor")) {
            editor->SetScrollTop(state.workbench.appearance_view.appearance_editor_scroll_top);
        }
    }
    sync_object_details_window(doc, state, true);
    nw::toolset::sync_managed_lists(
        doc, nw::toolset::ui_v1_host(), state.workbench.managed_lists, true);
    sync_creature_inventory_window(doc, state, true);
    nw::toolset::sync_dialog_view(doc, state.dialog_view, true);
    if (focus_area) focus_workspace_viewport(doc, state);
}

void refresh_workspace_content(Rml::ElementDocument* doc, AppState& state)
{
    refresh_workspace_content_impl(doc, state, true);
}

void refresh_workspace_tabs(Rml::ElementDocument* doc, AppState& state)
{
    nw::toolset::refresh_workspace_tabs(doc, state.workspace_view, state.workspace);
}

void refresh_workspace_view(Rml::ElementDocument* doc, AppState& state)
{
    if (!doc) { return; }
    refresh_workspace_tabs(doc, state);
    refresh_workspace_content_impl(doc, state, false);
}

std::optional<WorkspaceViewerViewportRequest> active_workspace_viewer_viewport_request(
    Rml::ElementDocument* doc, AppState& state, int frame_width, int frame_height)
{
    return nw::toolset::active_workspace_viewer_viewport_request(doc, state.workspace, state.backend, frame_width, frame_height);
}

void restore_play_preview_picker_shell(Rml::ElementDocument* doc, AppState& state)
{
    if (!nw::toolset::restore_play_preview_picker_shell(doc, state.play_preview, state.shell)) { return; }
    refresh_recent_list(doc, state);
    focus_workspace_viewport(doc, state);
}

void stop_play_preview(ClientRenderer& renderer, SystemInterface_SDL& system_interface,
    Rml::ElementDocument* doc, AppState& state)
{
    nw::toolset::stop_play_preview(renderer, state.play_preview, state.runtime_input, state.shell);
    apply_shell_layout(doc, state);
    system_interface.SetMouseCursor("arrow");
}

void request_play_preview_actor(Rml::ElementDocument* doc, AppState& state, std::string_view reason)
{
    nw::toolset::request_play_preview_actor(doc, state.play_preview, state.shell, reason);
    refresh_recent_list(doc, state);
    state.viewer_viewport_focused = false;
    if (auto* search = find_el(doc, "recent_search")) { search->Focus(); }
}

bool start_play_preview_from_ray(ClientRenderer& renderer, SystemInterface_SDL& system_interface,
    Rml::ElementDocument* doc, AppState& state, const ClientViewportRay& ray)
{
    const auto result = nw::toolset::start_play_preview_from_ray(renderer, state.play_preview,
        state.runtime_input, state.shell, ray);
    using nw::toolset::PlayPreviewStartStatus;
    if (result.status == PlayPreviewStartStatus::unavailable) { return false; }
    if (result.status == PlayPreviewStartStatus::spawn_failed) {
        system_interface.SetMouseCursor("cross");
        append_output(state, "error", result.message);
        return false;
    }
    apply_shell_layout(doc, state);
    system_interface.SetMouseCursor("arrow");
    const bool started = result.status == PlayPreviewStartStatus::started;
    append_output(state, started ? "info" : "error", result.message);
    return started;
}

bool prepare_play_preview(ClientRenderer& renderer,
    SystemInterface_SDL& system_interface,
    Rml::ElementDocument* doc,
    AppState& state,
    const std::filesystem::path& selected_actor = {})
{
    if (state.play_preview.session.active()
        || state.play_preview.placement_pending()) {
        return true;
    }
    const auto warn = [&](std::string_view message) {
        append_output(state, "warn", message);
        state.shell.set_output_panel_visible(true);
        refresh_bottom_dock_view(doc, state);
    };
    // Startup needs the active document, not a laid-out viewport rectangle.
    // The DOM may have just been rebuilt; geometry is only needed on placement.
    const auto* tab = state.workspace.active_tab();
    const auto project_dir = state.backend.current_project_dir();
    if (!tab || tab->kind != nw::toolset::WorkspaceTabKind::area
        || tab->detail.empty() || project_dir.empty()) {
        warn("F9 play preview requires an open project area");
        return false;
    }
    if (!synchronize_area_viewport_structure(renderer, state, true)) {
        warn("F9 play preview is blocked until the area viewport rebuilds");
        return false;
    }

    const auto resolved = nw::toolset::resolve_play_preview_actor(project_dir, selected_actor);
    if (!resolved.actor.valid()) {
        request_play_preview_actor(doc, state, resolved.diagnostic);
        return false;
    }
    if (!renderer.area_viewer_matches_resource(tab->detail)) {
        warn("Area viewport is still loading; press F9 again when it is visible");
        return false;
    }
    const nw::ObjectHandle area = renderer.area_viewer_object();
    if (area.type != nw::ObjectType::area) {
        warn("Area viewport is not ready for play preview");
        return false;
    }

    nw::toolset::arm_play_preview(state.play_preview, state.runtime_input, resolved.actor,
        area, state.backend.module_generation(), state.workspace.active_tab_id());
    restore_play_preview_picker_shell(doc, state);
    focus_workspace_viewport(doc, state);
    apply_shell_layout(doc, state);
    system_interface.SetMouseCursor("cross");
    append_output(state, "info", "Click a walkable area surface to start play preview");
    return true;
}

void toggle_command_palette(Rml::Context* context,
    Rml::Context* palette_context,
    Rml::ElementDocument* doc,
    Rml::ElementDocument* palette_doc,
    AppState& state,
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

void toggle_terminal(Rml::ElementDocument* doc, AppState& state, bool visible)
{
    state.shell.set_terminal_visible(visible);
    refresh_bottom_dock_view(doc, state);
}

void toggle_output_panel(Rml::ElementDocument* doc, AppState& state, bool visible)
{
    state.shell.set_output_panel_visible(visible);
    refresh_bottom_dock_view(doc, state);
}

nw::toolset::CommandContext command_context(AppState& state, nw::toolset::CommandSource source)
{
    nw::toolset::CommandContext context;
    context.source = source;
    context.workspace = &state.workspace;
    context.active_tab_id = state.workspace.active_tab_id();
    context.area_object = state.smalls.active_area();
    context.play_preview_active = state.play_preview.session.active();
    return context;
}

void append_command_result(AppState& state, const nw::toolset::CommandResult& result)
{
    nw::toolset::append_command_results(state.shell, {&result, 1});
}

void append_terminal_result(AppState& state, const nw::toolset::CommandResult& result)
{
    nw::toolset::append_terminal_results(state.shell, {&result, 1});
}

bool complete_terminal_command(Rml::ElementDocument* doc, AppState& state)
{
    return nw::toolset::complete_terminal_command(doc, state.shell, state.backend);
}

void sync_shell_visibility(Rml::Context* context,
    Rml::Context* palette_context,
    Rml::ElementDocument* doc,
    Rml::ElementDocument* palette_doc,
    AppState& state)
{
    toggle_command_palette(context, palette_context, doc, palette_doc, state, state.shell.command_palette_visible);
    refresh_bottom_dock_view(doc, state);
}

void sync_viewer_render_options(ClientRenderer& renderer, const AppState& state)
{
    renderer.set_area_viewer_options(ClientAreaViewerOptions{
        .lights_enabled = state.shell.viewer_area_lights_enabled,
        .debug_enabled = state.shell.viewer_area_debug_enabled,
        .triggers_enabled = state.shell.viewer_area_triggers_enabled,
        .encounters_enabled = state.shell.viewer_area_encounters_enabled,
        .forward_plus_enabled = state.shell.viewer_forward_plus_enabled,
        .forward_plus_auto_configure_area = state.shell.viewer_forward_plus_auto_configure_area,
        .forward_plus_tile_size = state.shell.viewer_forward_plus_tile_size,
        .forward_plus_depth_slices = state.shell.viewer_forward_plus_depth_slices,
        .forward_plus_max_lights_per_cluster = state.shell.viewer_forward_plus_max_lights_per_cluster,
        .forward_plus_debug_mode = state.shell.viewer_forward_plus_debug_mode,
        .fog_enabled = state.shell.viewer_area_fog_enabled,
        .shadows_enabled = state.shell.viewer_area_shadows_enabled,
        .day_night_autoplay = state.shell.viewer_area_day_night_autoplay,
        .day_night_elapsed_seconds = state.shell.viewer_area_day_night_elapsed_seconds,
        .day_night_time_generation = state.shell.viewer_area_day_night_time_generation,
        .reload_generation = state.shell.viewer_area_reload_generation,
    });
}

nw::toolset::CommandResult dispatch_command(AppState& state,
    std::string_view command_id,
    std::vector<std::string_view> args,
    nw::toolset::CommandSource source)
{
    return state.backend.execute_command(command_id, args, command_context(state, source));
}

bool commit_object_details_sound_position(Rml::ElementDocument* doc, AppState& state, int32_t desired)
{
    return nw::toolset::commit_object_details_sound_position(doc, state.workbench, state.workspace,
        state.backend, state.shell, command_context(state, nw::toolset::CommandSource::widget), desired);
}

class ObjectWorkbenchChangeListener final : public Rml::EventListener {
public:
    explicit ObjectWorkbenchChangeListener(AppState& state)
        : state_{state}
    {
    }
    void ProcessEvent(Rml::Event& event) override
    {
        nw::toolset::process_object_workbench_change(event, state_.workbench, state_.workspace,
            state_.backend, state_.shell, command_context(state_, nw::toolset::CommandSource::widget));
    }
    bool commit_sound_volume()
    {
        return nw::toolset::commit_object_workbench_sound_volume(state_.workbench, state_.workspace,
            state_.backend, state_.shell, command_context(state_, nw::toolset::CommandSource::widget));
    }
private:
    AppState& state_;
};

std::string precise_float_text(float value)
{
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(
        buffer.data(), buffer.data() + buffer.size(), value, std::chars_format::general,
        std::numeric_limits<float>::max_digits10);
    return result.ec == std::errc{} ? std::string{buffer.data(), result.ptr} : std::string{};
}

void sync_area_object_after_command(ClientRenderer& renderer, AppState& state, const nw::toolset::CommandResult& result)
{
    nw::toolset::sync_area_object_after_command(renderer, state.shell, result, state.smalls.active_object());
}

bool begin_area_object_drag(ClientRenderer& renderer, AppState& state, Rml::Vector2f point, const WorkspaceViewerViewportRequest& viewport)
{
    if (viewport.kind != WorkspaceViewerViewportKind::area
        || !nw::toolset::begin_area_object_drag(renderer, state.area_object_drag, point, viewport.rect)) { return false; }
    state.smalls.publish_active_object(state.area_object_drag.before.owner);
    state.workbench.active_object_tab_id = state.workspace.active_tab_id();
    return true;
}

bool update_area_object_drag(ClientRenderer& renderer, AppState& state, Rml::Vector2f point, const WorkspaceViewerViewportRequest& viewport)
{
    if (viewport.kind != WorkspaceViewerViewportKind::area) {
        nw::toolset::cancel_area_object_drag(renderer, state.area_object_drag);
        return false;
    }
    return nw::toolset::update_area_object_drag(renderer, state.area_object_drag, point, viewport.rect);
}

void cancel_area_object_drag(ClientRenderer& renderer, AppState& state)
{
    nw::toolset::cancel_area_object_drag(renderer, state.area_object_drag);
}

void commit_area_object_drag(ClientRenderer& renderer, AppState& state)
{
    if (!state.area_object_drag.active) { return; }
    nw::toolset::commit_area_object_drag(renderer, state.area_object_drag,
        state.smalls.active_object(), state.backend,
        command_context(state, nw::toolset::CommandSource::renderer), state.shell);
}

bool add_encounter_spawn_point(ClientRenderer& renderer, AppState& state, Rml::Vector2f point, const WorkspaceViewerViewportRequest& viewport)
{
    return nw::toolset::add_encounter_spawn_point(renderer, point, viewport.rect,
        state.backend, command_context(state, nw::toolset::CommandSource::renderer), state.shell);
}

nw::ObjectHandle active_workspace_area(const AppState& state) noexcept
{
    const auto* tab = state.workspace.active_tab();
    return tab && tab->kind == nw::toolset::WorkspaceTabKind::area
        ? tab->document.object()
        : nw::ObjectHandle{};
}

bool area_tile_editor_action_allowed(const AppState& state) noexcept
{
    return state.area_workspace_surface == AreaWorkspaceSurface::tiles
        && active_workspace_area(state).type == nw::ObjectType::area
        && !state.command_view.command_form && !state.loading.module_dialog_open
        && !state.backend.blueprint_operation_active()
        && !state.backend.blueprint_publication_pending()
        && !state.shell.command_palette_visible
        && !state.play_preview.session.active();
}

std::optional<nw::toolset::AreaTileBrush> selected_area_tile_brush(
    const AppState& state, uint8_t pointer_button) noexcept
{
    return nw::toolset::selected_area_tile_brush(state.area_tile_editor, area_tile_pointer_button(pointer_button));
}

bool area_tile_stroke_context_valid(const AppState& state) noexcept
{
    return area_tile_editor_action_allowed(state)
        && state.area_tile_editor.stroke.active
        && active_workspace_area(state) == state.area_tile_editor.stroke.area;
}

void cancel_area_object_placement(ClientRenderer& renderer, AppState& state);
void cancel_area_tile_stroke(ClientRenderer& renderer, AppState& state);
bool begin_area_tile_stroke(ClientRenderer& renderer,
    AppState& state,
    Rml::Vector2f point,
    const WorkspaceViewerViewportRequest& viewport,
    uint8_t pointer_button);

bool synchronize_area_viewport_structure(
    ClientRenderer& renderer, AppState& state, bool report_failure)
{
    const nw::ObjectHandle area = active_workspace_area(state);
    if (area.type != nw::ObjectType::area
        || state.stale_area_viewport != area) {
        return true;
    }
    cancel_area_object_placement(renderer, state);
    cancel_area_object_drag(renderer, state);
    cancel_area_tile_stroke(renderer, state);
    if (!renderer.rebuild_live_viewer_area(
            area, renderer.active_viewer_object())) {
        if (report_failure) {
            append_output(state, "error",
                "The area viewport is stale because its structural rebuild failed");
        }
        return false;
    }
    state.observed_area_structure_epoch
        = nw::toolset::object_mutation_state().area_structure_epoch;
    state.stale_area_viewport = nw::ObjectHandle{};
    return true;
}

void clear_area_tile_selection(ClientRenderer& renderer, AppState& state)
{
    nw::toolset::clear_area_tile_selection(renderer, state.area_tile_editor, active_workspace_area(state));
}

bool update_area_tile_selection_preview(ClientRenderer& renderer, AppState& state)
{
    return nw::toolset::update_area_tile_selection_preview(renderer, state.area_tile_editor, active_workspace_area(state));
}

bool select_area_tiles(ClientRenderer& renderer, AppState& state,
    Rml::Vector2f point, const WorkspaceViewerViewportRequest& viewport)
{
    if (viewport.kind != WorkspaceViewerViewportKind::area) { return false; }
    return nw::toolset::select_area_tiles(renderer, state.area_tile_editor,
        active_workspace_area(state), point, viewport.rect,
        area_tile_editor_action_allowed(state), state.shell);
}

bool cycle_area_tile_at_point(ClientRenderer& renderer, AppState& state,
    Rml::Vector2f point, const WorkspaceViewerViewportRequest& viewport)
{
    if (viewport.kind != WorkspaceViewerViewportKind::area) { return false; }
    return nw::toolset::cycle_area_tile_at_point(renderer, state.area_tile_editor,
        active_workspace_area(state), point, viewport.rect,
        area_tile_editor_action_allowed(state), state.backend,
        command_context(state, nw::toolset::CommandSource::renderer), state.shell);
}

bool handle_area_tile_pointer_down(
    ClientRenderer& renderer,
    SystemInterface_SDL& system_interface,
    Rml::ElementDocument* doc,
    AppState& state,
    Rml::Vector2f point,
    const WorkspaceViewerViewportRequest& viewport,
    uint8_t pointer_button)
{
    if (state.area_workspace_surface != AreaWorkspaceSurface::tiles
        || viewport.kind != WorkspaceViewerViewportKind::area) {
        return false;
    }

    const auto input = nw::toolset::AreaTilePointerInput{
        .button = area_tile_pointer_button(pointer_button),
        .modifier = area_tile_pointer_modifier(SDL_GetModState()),
        .secondary_paint_available
        = selected_area_tile_brush(state, SDL_BUTTON_RIGHT).has_value(),
    };
    const auto result
        = nw::toolset::resolve_area_tile_pointer_input(input);
    switch (result.action) {
    case nw::toolset::AreaTilePointerAction::select:
        (void)select_area_tiles(renderer, state, point, viewport);
        sync_area_tile_palette_window(doc, state, true);
        system_interface.SetMouseCursor("arrow");
        break;
    case nw::toolset::AreaTilePointerAction::cycle_variation:
        (void)cycle_area_tile_at_point(renderer, state, point, viewport);
        sync_area_tile_palette_window(doc, state, true);
        system_interface.SetMouseCursor("arrow");
        break;
    case nw::toolset::AreaTilePointerAction::paint: {
        clear_area_tile_selection(renderer, state);
        const bool began = begin_area_tile_stroke(
            renderer, state, point, viewport, pointer_button);
        system_interface.SetMouseCursor(
            began ? "cross" : "unavailable");
        break;
    }
    case nw::toolset::AreaTilePointerAction::none:
        break;
    }
    return result.consumed;
}

void cancel_area_tile_stroke(ClientRenderer& renderer, AppState& state)
{
    nw::toolset::cancel_area_tile_stroke(renderer, state.area_tile_editor, active_workspace_area(state));
}

bool cancel_area_tile_action(ClientRenderer& renderer, AppState& state)
{
    return nw::toolset::cancel_area_tile_action(renderer, state.area_tile_editor, active_workspace_area(state));
}

bool open_area_tile_editor(ClientRenderer& renderer, AppState& state)
{
    const nw::ObjectHandle area_handle = active_workspace_area(state);
    const auto* area = nw::kernel::objects().get<nw::Area>(area_handle);
    if (!area || area->tiles.empty()) {
        append_output(state, "warn", "The displayed area has no editable tiles");
        return false;
    }

    cancel_area_object_placement(renderer, state);
    cancel_area_object_drag(renderer, state);
    (void)renderer.clear_viewer_area_object_selection();
    auto& editor = state.area_tile_editor;
    state.area_workspace_surface = AreaWorkspaceSurface::tiles;
    if (!nw::toolset::reset_area_tile_editor(editor, area_handle)) {
        append_output(state, "error", editor.palette.diagnostic);
    }
    return true;
}

void close_area_tile_editor(ClientRenderer& renderer, AppState& state)
{
    cancel_area_tile_stroke(renderer, state);
    state.area_tile_editor = {};
    (void)renderer.clear_viewer_area_object_selection();
}

bool set_area_workspace_surface(ClientRenderer& renderer,
    AppState& state,
    AreaWorkspaceSurface surface)
{
    if (state.area_workspace_surface == surface) {
        return true;
    }
    if (surface == AreaWorkspaceSurface::tiles) {
        return open_area_tile_editor(renderer, state);
    }

    if (state.area_workspace_surface == AreaWorkspaceSurface::tiles) {
        close_area_tile_editor(renderer, state);
    } else {
        cancel_area_object_placement(renderer, state);
        cancel_area_object_drag(renderer, state);
        (void)renderer.clear_viewer_area_object_selection();
    }
    state.area_workspace_surface = surface;
    state.smalls.clear_active_object();
    state.workbench.active_object_tab_id.clear();
    clear_active_object_details(state);
    return true;
}

bool update_area_tile_cursor(ClientRenderer& renderer, AppState& state,
    Rml::Vector2f point, const WorkspaceViewerViewportRequest& viewport)
{
    if (state.area_workspace_surface != AreaWorkspaceSurface::tiles
        || viewport.kind != WorkspaceViewerViewportKind::area) { return false; }
    return nw::toolset::update_area_tile_cursor(renderer, state.area_tile_editor,
        active_workspace_area(state), point, viewport.rect,
        area_tile_pointer_modifier(SDL_GetModState()), state.shell);
}

void flush_area_tile_cursor_update(ClientRenderer& renderer,
    SDL_Window* window,
    Rml::Context* context,
    Rml::ElementDocument* doc,
    AppState& state,
    int frame_width,
    int frame_height)
{
    auto& editor = state.area_tile_editor;
    if (!area_tile_editor_action_allowed(state)) {
        return;
    }
    const auto modifier = area_tile_pointer_modifier(SDL_GetModState());
    if (!nw::toolset::prepare_area_tile_cursor_update(
            renderer, editor, active_workspace_area(state), modifier)) { return; }
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    (void)SDL_GetMouseState(&mouse_x, &mouse_y);
    const Rml::Vector2f point = to_context_point(window, mouse_x, mouse_y);
    editor.pending_cursor_point = point;
    editor.cursor_update_pending = false;
    const auto viewport = active_workspace_viewer_viewport_request(
        doc, state, frame_width, frame_height);
    auto* top_hit = context ? context->GetElementAtPoint(point) : nullptr;
    if (modifier != nw::toolset::AreaTilePointerModifier::blocked
        && SDL_GetMouseFocus() == window
        && viewport
        && viewport->kind == WorkspaceViewerViewportKind::area
        && point_within_viewport(viewport->rect, point)
        && !viewport_mouse_hit_blocked(doc, top_hit, point, state)) {
        (void)update_area_tile_cursor(renderer, state, point, *viewport);
        return;
    }
    nw::toolset::clear_area_tile_cursor_target(renderer, editor, active_workspace_area(state), modifier);
}

bool begin_area_tile_stroke(ClientRenderer& renderer, AppState& state,
    Rml::Vector2f point, const WorkspaceViewerViewportRequest& viewport, uint8_t pointer_button)
{
    auto& editor = state.area_tile_editor;
    editor.cursor_update_pending = false;
    const auto brush = nw::toolset::selected_area_tile_brush(editor, area_tile_pointer_button(pointer_button));
    if (!brush) {
        editor.feedback = "Choose a terrain action";
        return false;
    }
    if (area_tile_editor_action_allowed(state)
        && !synchronize_area_viewport_structure(renderer, state, true)) { return true; }
    return nw::toolset::begin_area_tile_stroke(renderer, editor,
        active_workspace_area(state), point, viewport.rect, pointer_button, *brush,
        area_tile_pointer_modifier(SDL_GetModState()), area_tile_editor_action_allowed(state), state.shell);
}

void commit_area_tile_stroke(ClientRenderer& renderer, AppState& state)
{
    if (!state.area_tile_editor.stroke.active) { return; }
    nw::toolset::commit_area_tile_stroke(renderer, state.area_tile_editor,
        active_workspace_area(state), area_tile_editor_action_allowed(state),
        state.stale_area_viewport == state.area_tile_editor.stroke.area, state.backend,
        command_context(state, nw::toolset::CommandSource::renderer), state.shell);
}

void arm_area_object_placement(ClientRenderer& renderer, AppState& state, nw::Resource resource, Rml::Vector2f point)
{
    nw::toolset::arm_area_object_placement(renderer, state.area_object_placement,
        std::move(resource), point, state.workspace.active_tab_id());
}

void cancel_area_object_placement(ClientRenderer& renderer, AppState& state)
{
    nw::toolset::cancel_area_object_placement(renderer, state.area_object_placement, state.shell);
}

bool update_area_object_placement(ClientRenderer& renderer, AppState& state, Rml::Vector2f point, const std::optional<WorkspaceViewerViewportRequest>& viewport)
{
    const std::optional<ClientViewportRect> area_viewport = viewport && viewport->kind == WorkspaceViewerViewportKind::area
        ? std::optional<ClientViewportRect>{viewport->rect}
        : std::nullopt;
    return nw::toolset::update_area_object_placement(renderer, state.area_object_placement,
        point, area_viewport, state.workspace.active_tab_id(), state.browser.pressed_recent_index, state.shell);
}

bool accept_area_region_point(ClientRenderer& renderer, AppState& state, Rml::Vector2f point, const WorkspaceViewerViewportRequest& viewport)
{
    return nw::toolset::accept_area_region_point(renderer, state.area_object_placement,
        point, viewport.rect, state.shell);
}

void commit_area_object_placement(
    ClientRenderer& renderer, AppState& state);

bool complete_area_region_placement(ClientRenderer& renderer, AppState& state, Rml::Vector2f point, const WorkspaceViewerViewportRequest& viewport)
{
    return nw::toolset::complete_area_region_placement(renderer, state.area_object_placement,
        point, viewport.rect, state.backend, command_context(state, nw::toolset::CommandSource::renderer), state.shell);
}

void commit_area_object_placement(ClientRenderer& renderer, AppState& state)
{
    if (!state.area_object_placement.active()) { return; }
    nw::toolset::commit_area_object_placement(renderer, state.area_object_placement,
        state.backend, command_context(state, nw::toolset::CommandSource::renderer), state.shell);
}

nw::toolset::ProjectResourceDragContext project_resource_drag_context(const AppState& state)
{
    return {
        .active_tab_id = state.workspace.active_tab_id(),
        .details_object = state.workbench.object_details.object,
        .inventory_object = state.workbench.inventory_view.creature_inventory.object,
        .surface = state.workbench.object_workbench_surface,
        .object_matches_tab = active_object_matches_tab(state),
        .inventory_matches_tab = active_creature_inventory_matches_tab(state),
    };
}

bool arm_project_blueprint_drag(AppState& state, const nw::Resource& resource,
    const std::filesystem::path& source_path, Rml::Vector2f point)
{
    return nw::toolset::arm_project_blueprint_drag(state.project_blueprint_drag,
        project_resource_drag_context(state), resource, source_path, point);
}

bool project_blueprint_drag_context_matches(const AppState& state)
{
    return nw::toolset::project_blueprint_drag_context_matches(state.project_blueprint_drag,
        project_resource_drag_context(state));
}

void cancel_project_blueprint_drag(Rml::ElementDocument* doc, AppState& state)
{
    nw::toolset::cancel_project_blueprint_drag(doc, state.project_blueprint_drag);
}

bool update_project_blueprint_drag(Rml::Context* context, Rml::ElementDocument* doc,
    AppState& state, Rml::Vector2f point)
{
    return nw::toolset::update_project_blueprint_drag(context, doc, state.project_blueprint_drag,
        project_resource_drag_context(state), point, state.workbench.inventory_view.creature_inventory_page,
        state.browser.pressed_recent_index, state.shell);
}

void commit_project_blueprint_drag(Rml::ElementDocument* doc, AppState& state)
{
    if (!state.project_blueprint_drag.active()) { return; }
    nw::toolset::commit_project_blueprint_drag(doc, state.project_blueprint_drag,
        state.backend, command_context(state, nw::toolset::CommandSource::renderer), state.shell);
}

bool handle_area_tile_key(ClientRenderer& renderer,
    Rml::Context* context,
    Rml::ElementDocument* doc,
    AppState& state,
    const SDL_KeyboardEvent& key,
    int frame_width,
    int frame_height)
{
    if (key.repeat || key.key != SDLK_R
        || (key.mod
            & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI
                | SDL_KMOD_SHIFT))
        || focused_text_input(context)
        || !area_tile_editor_action_allowed(state)) {
        return false;
    }
    const auto viewport = active_workspace_viewer_viewport_request(doc, state, frame_width, frame_height);
    const std::optional<ClientViewportRect> area_viewport = viewport && viewport->kind == WorkspaceViewerViewportKind::area
        ? std::optional<ClientViewportRect>{viewport->rect}
        : std::nullopt;
    if (nw::toolset::rotate_area_tile_group(renderer, state.area_tile_editor,
            active_workspace_area(state), area_viewport, area_tile_pointer_modifier(SDL_GetModState()), state.shell)) {
        sync_area_tile_palette_window(doc, state, true);
    }
    return true;
}

bool handle_area_object_key(ClientRenderer& renderer,
    Rml::Context* context,
    Rml::ElementDocument* doc,
    AppState& state,
    const SDL_KeyboardEvent& key,
    int frame_width,
    int frame_height)
{
    if (key.repeat
        || (key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI | SDL_KMOD_SHIFT))
        || state.shell.command_palette_visible || state.loading.module_dialog_open
        || focused_text_input(context)) {
        return false;
    }
    if (key.key != SDLK_DELETE && key.key != SDLK_R) {
        return false;
    }
    const auto viewport = active_workspace_viewer_viewport_request(
        doc, state, frame_width, frame_height);
    if (!viewport || viewport->kind != WorkspaceViewerViewportKind::area) {
        return false;
    }
    if (!synchronize_area_viewport_structure(renderer, state, true)) {
        return true;
    }
    const auto action = key.key == SDLK_DELETE
        ? nw::toolset::AreaObjectEditKey::remove
        : nw::toolset::AreaObjectEditKey::randomize_orientation;
    return nw::toolset::handle_area_object_edit_key(renderer, action, state.backend,
        command_context(state, nw::toolset::CommandSource::shortcut), state.shell, state.smalls.active_object());
}

void sync_command_overlay_visibility(AppState& state)
{
    nw::toolset::sync_command_overlay_visibility(state.command_view,
        state.loading.project_load.active(), state.backend.blueprint_operation_active() || state.backend.blueprint_publication_pending());
}

void close_command_form_combobox(AppState& state)
{
    nw::toolset::close_command_form_combobox(state.command_view);
}

bool open_command_form_combobox(AppState& state, size_t field_index)
{
    return nw::toolset::open_command_form_combobox(state.command_view, field_index);
}

void sync_command_form_combobox(AppState& state, bool force = false)
{
    nw::toolset::sync_command_form_combobox(state.command_view, force);
}

void sync_command_form(AppState& state, bool force = false)
{
    nw::toolset::sync_command_form(state.command_view, state.backend,
        state.loading.project_load.active(), force);
}

bool commit_command_form_combobox(AppState& state, int32_t choice_index)
{
    return nw::toolset::commit_command_form_combobox(state.command_view,
        state.backend, state.loading.project_load.active(), choice_index);
}

void sync_blueprint_operation(AppState& state)
{
    nw::toolset::sync_blueprint_operation(state.command_view, state.backend,
        state.loading.project_load.active());
}

void sync_project_load_overlay(AppState& state)
{
    sync_command_overlay_visibility(state);
    nw::toolset::sync_loading_overlay(state.command_view.command_overlay_document, state.loading.project_load);
}

bool queue_project_open(AppState& state, std::string path,
    nw::toolset::CommandSource source, bool close_import_panel_on_success = false)
{
    if (!nw::toolset::queue_loading_project(state.loading, std::move(path), source, close_import_panel_on_success)) { return false; }
    sync_project_load_overlay(state);
    return true;
}

nw::toolset::CommandResult resolve_command_result(SDL_Window* window,
    AppState& state,
    nw::toolset::CommandResult result,
    nw::toolset::CommandSource source,
    bool terminal_output = false)
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
    AppState& state,
    std::string_view command_id,
    std::vector<std::string_view> args,
    nw::toolset::CommandSource source)
{
    return resolve_command_result(window, state, dispatch_command(state, command_id, std::move(args), source), source);
}

void show_open_module_dialog(SDL_Window* window, AppState& state, bool import = false)
{
    const auto status = nw::toolset::show_loading_module_dialog(window, state.loading, import);
    if (status == nw::toolset::LoadingDialogStatus::unavailable) { append_output(state, "error", "Open module dialog unavailable"); }
    if (status == nw::toolset::LoadingDialogStatus::already_active) { append_output(state, "info", "Open module dialog already active"); }
}

void show_open_project_dialog(SDL_Window* window, AppState& state, bool import = false)
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
    AppState& state,
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

void handle_open_module_dialog_result(SDL_Window* window, Rml::ElementDocument* doc, AppState& state, SDL_Event& event)
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

void run_command_form_action(SDL_Window* window, Rml::ElementDocument* doc, AppState& state, size_t index)
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

class BlueprintActionListener final : public Rml::EventListener {
public:
    BlueprintActionListener(SDL_Window* window, Rml::ElementDocument* document, AppState& state)
        : window_{window}
        , document_{document}
        , state_{state}
    {
    }

    void ProcessEvent(Rml::Event& event) override
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

private:
    SDL_Window* window_;
    Rml::ElementDocument* document_;
    AppState& state_;
};

class HomeProjectActionListener final : public Rml::EventListener {
public:
    HomeProjectActionListener(SDL_Window* window, Rml::ElementDocument* document, AppState& state)
        : window_{window}
        , document_{document}
        , state_{state}
    {
    }

    void ProcessEvent(Rml::Event& event) override
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

private:
    SDL_Window* window_;
    Rml::ElementDocument* document_;
    AppState& state_;
};

void poll_project_import(SDL_Window* window, Rml::ElementDocument* doc, AppState& state)
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
    AppState& state)
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

bool ensure_backend_ready(AppState& state)
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

void clear_inactive_object(AppState& state)
{
    if (state.workbench.active_object_tab_id.empty()) {
        return;
    }

    const auto* active_tab = state.workspace.active_tab();
    if (active_tab_has_object_workbench(active_tab)
        && active_tab->id == state.workbench.active_object_tab_id) {
        return;
    }

    state.smalls.clear_active_object();
    state.smalls.clear_active_area();
    state.workbench.active_object_tab_id.clear();
}

} // namespace

// ---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    if (nw::toolset::print_client_build_info_if_requested(argc, argv)) {
        return 0;
    }
    loguru::g_stderr_verbosity = loguru::Verbosity_WARNING;
    nw::init_logger(argc, argv);
    if (const int cli_result = run_project_cli_if_requested(argc, argv); cli_result >= 0) {
        return cli_result;
    }
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

    AppState state;
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
    const auto workbench_listener_cleanup = create_scope_exit([&] {
        if (!rml_runtime.contexts().toolset) { return; }
        context->RemoveEventListener("change", &object_workbench_change_listener, false);
        context->RemoveEventListener("blur", &object_workbench_change_listener, true);
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
    const std::filesystem::path executable_arg{argv[0]};
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

    state.shell_view.preferences_path = nw::toolset::client_preferences_path();
    nw::toolset::load_ui_preferences(state.shell_view.preferences_path, state.shell.docks, state.browser.recent_projects);
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

    const bool frame_pacing_enabled = !environment_flag_enabled("ROLLNW_CLIENT_UNCAPPED");
    if (!frame_pacing_enabled) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "frame pacing disabled by ROLLNW_CLIENT_UNCAPPED");
    }

    bool running = true;
    Uint64 last_frame_ms = SDL_GetTicks();
    Uint64 last_frame_counter = 0;
    while (running) {
#if defined(ROLLNW_ENABLE_TRACY)
        FrameMark;
#endif
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
        const Uint64 frame_start_counter = SDL_GetPerformanceCounter();
        const Uint64 frame_start_ms = SDL_GetTicks();
        const float raw_frame_delta_seconds = last_frame_counter != 0
            ? seconds_between_performance_counters(last_frame_counter, frame_start_counter)
            : 0.0f;
        last_frame_counter = frame_start_counter;
        const Uint64 raw_frame_delta_ms = frame_start_ms >= last_frame_ms ? frame_start_ms - last_frame_ms : 0;
        last_frame_ms = frame_start_ms;
        const int32_t frame_delta_ms = static_cast<int32_t>(std::min<Uint64>(raw_frame_delta_ms, 100));
        update_viewer_frame_metrics(state.metrics, raw_frame_delta_seconds);

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            nw::toolset::ClientInputDispatchState dispatch;
            clear_inactive_object(state);
            if (state.project_blueprint_drag.active()
                && !project_blueprint_drag_context_matches(state)) {
                cancel_project_blueprint_drag(doc, state);
            }
            if (state.area_object_placement.active()
                && (state.workspace.active_tab_id() != state.area_object_placement.tab_id
                    || (state.area_object_placement.object.type != nw::ObjectType::invalid
                        && renderer.area_viewer_object() != state.area_object_placement.area))) {
                cancel_area_object_placement(renderer, state);
            }
            if (state.area_object_drag.active
                && state.smalls.active_object() != state.area_object_drag.before.owner) {
                cancel_area_object_drag(renderer, state);
            }
            if (state.loading.open_module_dialog_event != 0 && event.type == state.loading.open_module_dialog_event) {
                handle_open_module_dialog_result(window, doc, state, event);
                continue;
            }
            if (consume_terminal_toggle_text_input(state, event)) {
                continue;
            }

            if (!nw::toolset::valid_client_pointer_event(event)) {
                cancel_area_tile_stroke(renderer, state);
                cancel_area_object_drag(renderer, state);
                cancel_area_object_placement(renderer, state);
                cancel_project_blueprint_drag(doc, state);
                nw::toolset::clear_managed_list_reorder(state.managed_list_reorder, doc);
                clear_workspace_tab_drag(state);
                (void)end_bottom_dock_resize(state);
                (void)end_left_dock_resize(state);
                state.viewer_viewport_dragging = false;
                state.shell_view.output_selection.dragging = false;
                state.browser.pressed_recent_index = -1;
                nw::toolset::discard_runtime_pointer_input(state.runtime_input);
                nw::toolset::cancel_client_pointer_interactions(context, palette_context, event);
                system_interface.SetMouseCursor("arrow");
                continue;
            }

            if ((state.backend.blueprint_operation_active() || state.backend.blueprint_publication_pending())) {
                if (event.type == SDL_EVENT_QUIT) {
                    if (state.backend.blueprint_progress().stage == "recovery" && !state.backend.blueprint_worker_active()
                        && !state.workspace.has_dirty_tabs()) {
                        running = false;
                        continue;
                    }
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Blueprint update in progress",
                        "Finish or cancel the blueprint operation before quitting.", window);
                    continue;
                }
                if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP
                    || event.type == SDL_EVENT_TEXT_INPUT || event.type == SDL_EVENT_TEXT_EDITING
                    || event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
                    || event.type == SDL_EVENT_MOUSE_BUTTON_UP || event.type == SDL_EVENT_MOUSE_WHEEL) {
                    (void)nw::toolset::forward_client_input(dispatch, nw::toolset::ClientRmlRecipient::command,
                        nw::toolset::ClientRmlForwardPhase::after_native, palette_context, window, event);
                    continue;
                }
            }
            if (state.command_view.command_form) {
                if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                    auto* focused_choice = find_ancestor_with_class(
                        palette_context->GetFocusElement(),
                        "command_form_choice_field");
                    const bool choice_key = focused_choice
                        && !(event.key.mod
                            & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI));
                    const auto focused_field = choice_key
                        ? parse_decimal_int32(
                              focused_choice->GetAttribute<Rml::String>(
                                  "data-field", ""))
                        : std::nullopt;
                    if (event.key.key == SDLK_ESCAPE
                        && state.command_view.command_form_combobox.is_active()) {
                        close_command_form_combobox(state);
                        continue;
                    }
                    if (event.key.key == SDLK_TAB
                        && state.command_view.command_form_combobox.is_active()) {
                        close_command_form_combobox(state);
                    }
                    if (focused_field && *focused_field >= 0
                        && (event.key.key == SDLK_UP
                            || event.key.key == SDLK_DOWN)) {
                        const auto field_index = static_cast<size_t>(*focused_field);
                        if (state.command_view.command_form_combobox_field != field_index
                            || !state.command_view.command_form_combobox.is_active()) {
                            (void)open_command_form_combobox(
                                state, field_index);
                        } else if (!state.command_view.command_form_combobox.popup_visible()) {
                            (void)state.command_view.command_form_combobox.show_popup();
                        }
                        (void)state.command_view.command_form_combobox.move_selection(
                            event.key.key == SDLK_UP ? -1 : 1);
                        sync_command_form_combobox(state, true);
                        continue;
                    }
                    if (focused_field && *focused_field >= 0
                        && (event.key.key == SDLK_RETURN
                            || event.key.key == SDLK_KP_ENTER)) {
                        const auto field_index = static_cast<size_t>(*focused_field);
                        if (state.command_view.command_form_combobox_field != field_index
                            || !state.command_view.command_form_combobox.is_active()) {
                            if (open_command_form_combobox(
                                    state, field_index)) {
                                sync_command_form_combobox(state, true);
                            }
                        } else if (!state.command_view.command_form_combobox.popup_visible()) {
                            (void)state.command_view.command_form_combobox.show_popup();
                            sync_command_form_combobox(state, true);
                        } else if (const auto selected
                            = state.command_view.command_form_combobox.selected_key()) {
                            (void)commit_command_form_combobox(
                                state, *selected);
                        }
                        continue;
                    }
                    std::optional<size_t> action_index;
                    if (event.key.key == SDLK_ESCAPE) {
                        const auto cancel = std::find_if(state.command_view.command_form->actions.begin(), state.command_view.command_form->actions.end(), [](const auto& action) { return action.id == "cancel"; });
                        if (cancel != state.command_view.command_form->actions.end()) {
                            action_index = static_cast<size_t>(std::distance(state.command_view.command_form->actions.begin(), cancel));
                        }
                    } else if (event.key.key == SDLK_RETURN
                        && !state.command_view.command_form->actions.empty()) {
                        action_index = 0;
                    }
                    if (action_index) {
                        run_command_form_action(window, doc, state, *action_index);
                        continue;
                    }
                    if (event.key.key == SDLK_ESCAPE) { continue; }
                }
                if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP
                    || event.type == SDL_EVENT_TEXT_INPUT || event.type == SDL_EVENT_TEXT_EDITING
                    || event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
                    || event.type == SDL_EVENT_MOUSE_BUTTON_UP || event.type == SDL_EVENT_MOUSE_WHEEL) {
                    (void)nw::toolset::forward_client_input(dispatch, nw::toolset::ClientRmlRecipient::command,
                        nw::toolset::ClientRmlForwardPhase::after_native, palette_context, window, event);
                    continue;
                }
            }

            switch (event.type) {
            case SDL_EVENT_QUIT: {
                if (state.loading.project_import.active()) {
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Import in progress",
                        "Please wait for the module import to finish before quitting.", window);
                    break;
                }
                if (!state.workspace.has_dirty_tabs()) {
                    running = false;
                    break;
                }
                const nw::toolset::CommandPrompt prompt{
                    .id = "workspace.quit",
                    .title = "Unsaved documents",
                    .message = "Save changes before quitting?",
                    .detail = "Discard closes all documents without saving their edits.",
                    .actions = {
                        {"save", "Save All", "toolset.save_all", {}},
                        {"discard", "Discard", {}, {}},
                        {"cancel", "Cancel", {}, {}},
                    },
                };
                const auto action = show_command_prompt(window, prompt);
                if (action && action->id == "discard") {
                    running = false;
                } else if (action && action->id == "save") {
                    const auto result = dispatch_command_flow(window, state,
                        "toolset.save_all", {}, nw::toolset::CommandSource::shortcut);
                    refresh_workspace_view(doc, state);
                    if (result.ok() && !state.workspace.has_dirty_tabs()) {
                        running = false;
                    } else {
                        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Save failed", result.message.c_str(), window);
                    }
                }
                break;
            }
            case SDL_EVENT_KEY_DOWN: {
                if (state.play_preview.session.active()
                    || state.play_preview.placement_pending()) {
                    if (!event.key.repeat
                        && (event.key.key == SDLK_F9 || event.key.key == SDLK_ESCAPE)) {
                        stop_play_preview(renderer, system_interface, doc, state);
                    } else if (!event.key.repeat
                        && event.key.key == SDLK_F8
                        && state.play_preview.session.active()) {
                        nw::toolset::toggle_play_preview_navigation_debug(renderer, state.play_preview, state.shell);
                    }
                    dispatch.native_handled = true;
                    break;
                }
                if (!event.key.repeat && (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_F9)
                    && state.play_preview.selecting_actor) {
                    restore_play_preview_picker_shell(doc, state);
                    dispatch.native_handled = true;
                    break;
                }
                if (!event.key.repeat && event.key.key == SDLK_ESCAPE
                    && state.area_workspace_surface
                        == AreaWorkspaceSurface::tiles) {
                    if (cancel_area_tile_action(renderer, state)) {
                        sync_area_tile_palette_window(doc, state, true);
                    } else {
                        (void)set_area_workspace_surface(renderer, state,
                            AreaWorkspaceSurface::properties);
                        refresh_workspace_content(doc, state);
                    }
                    system_interface.SetMouseCursor("arrow");
                    dispatch.native_handled = true;
                    break;
                }
                if (!event.key.repeat && event.key.key == SDLK_F9) {
                    if (state.area_workspace_surface
                        == AreaWorkspaceSurface::tiles) {
                        (void)set_area_workspace_surface(renderer, state,
                            AreaWorkspaceSurface::properties);
                        refresh_workspace_content(doc, state);
                    }
                    (void)prepare_play_preview(renderer, system_interface,
                        doc, state);
                    dispatch.native_handled = true;
                    break;
                }
                if (!event.key.repeat && event.key.key == SDLK_ESCAPE
                    && state.project_blueprint_drag.active()) {
                    cancel_project_blueprint_drag(doc, state);
                    state.browser.pressed_recent_index = -1;
                    system_interface.SetMouseCursor("arrow");
                    dispatch.native_handled = true;
                    break;
                }
                if (state.project_blueprint_drag.active()
                    && state.project_blueprint_drag.threshold_crossed) {
                    dispatch.native_handled = true;
                    break;
                }
                if (!event.key.repeat && event.key.key == SDLK_ESCAPE
                    && state.area_object_placement.active()) {
                    cancel_area_object_placement(renderer, state);
                    state.browser.pressed_recent_index = -1;
                    system_interface.SetMouseCursor("arrow");
                    dispatch.native_handled = true;
                    break;
                }
                if (state.area_object_placement.active()
                    && state.area_object_placement.threshold_crossed) {
                    dispatch.native_handled = true;
                    break;
                }

                if ((event.key.mod & SDL_KMOD_CTRL) && (event.key.mod & SDL_KMOD_SHIFT) && event.key.key == SDLK_P) {
                    cancel_area_tile_stroke(renderer, state);
                    append_command_result(state, dispatch_command(state, "rollnw.client.palette.toggle", {}, nw::toolset::CommandSource::shortcut));
                    toggle_command_palette(context, palette_context, doc, palette_doc, state, state.shell.command_palette_visible);
                    dispatch.native_handled = true;
                    break;
                }

                if (state.shell.command_palette_visible && event.key.key == SDLK_ESCAPE) {
                    toggle_command_palette(context, palette_context, doc, palette_doc, state, false);
                    dispatch.native_handled = true;
                    break;
                }

                if (!event.key.repeat && event.key.key == SDLK_ESCAPE
                    && state.workbench.object_details_combobox.is_active()) {
                    close_object_details_combobox(doc, state);
                    dispatch.native_handled = true;
                    break;
                }

                if (!event.key.repeat && event.key.key == SDLK_ESCAPE
                    && close_active_smalls_selector(doc)) {
                    dispatch.native_handled = true;
                    break;
                }

                auto* focused_variable_name = find_ancestor_with_class(
                    context->GetFocusElement(), "object_variable_name");
                auto* focused_variable_value = find_ancestor_with_class(
                    context->GetFocusElement(), "object_variable_value");
                if (!event.key.repeat && event.key.key == SDLK_ESCAPE
                    && (focused_variable_name || focused_variable_value)) {
                    clear_rml_focus(context);
                    sync_object_variable_window(doc, state, true);
                    dispatch.native_handled = true;
                    break;
                }

                auto* focused_details_integer = find_ancestor_with_class(
                    context->GetFocusElement(), "object_details_integer");
                if (!event.key.repeat && event.key.key == SDLK_ESCAPE
                    && focused_details_integer) {
                    clear_rml_focus(context);
                    sync_object_details_window(doc, state, true);
                    dispatch.native_handled = true;
                    break;
                }

                if ((event.key.key == SDLK_UP || event.key.key == SDLK_DOWN)
                    && focused_details_integer
                    && !(event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI))) {
                    const auto minimum = parse_decimal_int32(
                        focused_details_integer->GetAttribute<Rml::String>("data-min", ""));
                    const auto maximum = parse_decimal_int32(
                        focused_details_integer->GetAttribute<Rml::String>("data-max", ""));
                    auto* input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(
                        focused_details_integer);
                    const auto value = input
                        ? parse_decimal_int32(input->GetValue())
                        : std::nullopt;
                    if (input && value && minimum && maximum
                        && *minimum <= *value && *value <= *maximum) {
                        const bool can_decrement = event.key.key == SDLK_DOWN
                            && *value > *minimum;
                        const bool can_increment = event.key.key == SDLK_UP
                            && *value < *maximum;
                        if (can_decrement || can_increment) {
                            const int32_t adjusted = *value
                                + (can_increment ? 1 : -1);
                            const Rml::String adjusted_text = std::to_string(adjusted);
                            input->SetValue(adjusted_text);
                            const int cursor = static_cast<int>(adjusted_text.size());
                            input->SetSelectionRange(cursor, cursor);
                        }
                    }
                    dispatch.native_handled = true;
                    break;
                }

                if (!event.key.repeat
                    && (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER)
                    && focused_details_integer
                    && !(event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI))) {
                    const std::string row = focused_details_integer->GetAttribute<Rml::String>(
                        "data-row", "");
                    const std::string current = focused_details_integer->GetAttribute<Rml::String>(
                        "data-current", "");
                    auto* input = rmlui_dynamic_cast<Rml::ElementFormControl*>(
                        focused_details_integer);
                    const std::string desired = input ? input->GetValue() : std::string{};
                    const auto result = dispatch_command(state,
                        "object.details.set_integer",
                        {row, current, desired},
                        nw::toolset::CommandSource::widget);
                    append_command_result(state, result);
                    if (result.ok()) {
                        clear_rml_focus(context);
                    }
                    dispatch.native_handled = true;
                    break;
                }

                auto* focused_sound_position = find_ancestor_with_class(
                    context->GetFocusElement(),
                    "object_details_sound_position_field");
                const auto focused_sound_position_row = focused_sound_position
                    ? parse_decimal_int32(focused_sound_position->GetAttribute<Rml::String>(
                          "data-row", ""))
                    : std::nullopt;
                const bool sound_position_focused = focused_sound_position_row
                    && *focused_sound_position_row >= 0
                    && !(event.key.mod
                        & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI));
                if (sound_position_focused
                    && (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN)) {
                    const auto row_index = static_cast<uint32_t>(
                        *focused_sound_position_row);
                    if (state.workbench.object_details_combobox_row != row_index
                        || !state.workbench.object_details_combobox.is_active()) {
                        (void)open_object_details_sound_position_combobox(
                            doc, state, row_index);
                    } else if (!state.workbench.object_details_combobox.popup_visible()) {
                        (void)state.workbench.object_details_combobox.show_popup();
                    }
                    (void)state.workbench.object_details_combobox.move_selection(
                        event.key.key == SDLK_UP ? -1 : 1);
                    (void)sync_object_details_combobox(doc, state, true);
                    dispatch.native_handled = true;
                    break;
                }
                if (!event.key.repeat && sound_position_focused
                    && (event.key.key == SDLK_RETURN
                        || event.key.key == SDLK_KP_ENTER)) {
                    const auto row_index = static_cast<uint32_t>(
                        *focused_sound_position_row);
                    if (state.workbench.object_details_combobox_row != row_index
                        || !state.workbench.object_details_combobox.is_active()) {
                        if (open_object_details_sound_position_combobox(
                                doc, state, row_index)) {
                            (void)sync_object_details_combobox(doc, state, true);
                        }
                    } else if (!state.workbench.object_details_combobox.popup_visible()) {
                        (void)state.workbench.object_details_combobox.show_popup();
                        (void)sync_object_details_combobox(doc, state, true);
                    } else if (const auto selected
                        = state.workbench.object_details_combobox.selected_key()) {
                        if (commit_object_details_sound_position(
                                doc, state, *selected)) {
                            refresh_workspace_content(doc, state);
                        }
                    }
                    dispatch.native_handled = true;
                    break;
                }

                if (!event.key.repeat && event.key.key == SDLK_ESCAPE
                    && state.workbench.appearance_view.color_editor_channel >= 0) {
                    clear_color_editor(state);
                    refresh_workspace_content(doc, state);
                    dispatch.native_handled = true;
                    break;
                }

                if (!event.key.repeat && event.key.key == SDLK_ESCAPE
                    && state.workbench.appearance_view.appearance_selector_open) {
                    close_appearance_selector(state);
                    rebuild_active_appearances(state, state.workbench.object_details.object);
                    refresh_workspace_content(doc, state);
                    dispatch.native_handled = true;
                    break;
                }

                if (!event.key.repeat && event.key.key == SDLK_ESCAPE
                    && state.workbench.appearance_view.sound_resource_selector_open) {
                    close_sound_resource_selector(state);
                    refresh_workspace_content(doc, state);
                    dispatch.native_handled = true;
                    break;
                }

                if (!event.key.repeat && event.key.key == SDLK_ESCAPE
                    && state.managed_list_reorder.active()) {
                    nw::toolset::clear_managed_list_reorder(
                        state.managed_list_reorder, doc);
                    system_interface.SetMouseCursor("arrow");
                    dispatch.native_handled = true;
                    break;
                }

                if (!event.key.repeat && event.key.key == SDLK_ESCAPE
                    && state.workbench.creature_view.creature_spell_filter_field != CreatureSpellFilterField::none) {
                    clear_creature_spell_filter(state);
                    refresh_workspace_content(doc, state);
                    dispatch.native_handled = true;
                    break;
                }

                if (!event.key.repeat && event.key.key == SDLK_ESCAPE
                    && state.area_object_drag.active) {
                    cancel_area_object_drag(renderer, state);
                    system_interface.SetMouseCursor("arrow");
                    dispatch.native_handled = true;
                    break;
                }

                if (!event.key.repeat && event.key.key == SDLK_ESCAPE
                    && state.workbench.object_workbench_surface == ObjectWorkbenchSurface::inventory
                    && state.workbench.inventory_view.creature_inventory_selection >= 0) {
                    state.workbench.inventory_view.creature_inventory_selection = -1;
                    sync_creature_inventory_window(doc, state, true);
                    dispatch.native_handled = true;
                    break;
                }

                if (!event.key.repeat && event.key.key == SDLK_ESCAPE
                    && renderer.clear_viewer_area_object_selection()) {
                    state.smalls.clear_active_object();
                    state.workbench.active_object_tab_id.clear();
                    clear_active_object_details(state);
                    state.smalls.refresh_ui_lists();
                    refresh_workspace_content(doc, state);
                    sync_object_details_window(doc, state, true);
                    sync_creature_feat_window(doc, state, true);
                    sync_creature_spell_window(doc, state, true);
                    sync_creature_inventory_window(doc, state, true);
                    dispatch.native_handled = true;
                    break;
                }

                if (state.shell.command_palette_visible
                    && (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER)
                    && focused_element_has_id(palette_context, "command_input")) {
                    nw::toolset::refresh_command_palette(palette_doc, state.command_view, state.backend);
                    if (!state.command_view.commands.empty()) {
                        execute_palette_command(window, context, palette_context, doc, palette_doc, state, state.command_view.commands.front().id);
                    }
                    dispatch.native_handled = true;
                    break;
                }

                const bool appearance_search_focused = !state.shell.command_palette_visible
                    && state.workbench.object_workbench_surface == ObjectWorkbenchSurface::appearance
                    && state.workbench.appearance_view.appearance_selector_open
                    && focused_element_has_id(context, "appearance_search")
                    && !(event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI));
                if (appearance_search_focused
                    && (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN)) {
                    const int selected = state.workbench.appearance_view.appearance_list.move_selection(
                        event.key.key == SDLK_UP ? -1 : 1);
                    const int scroll_top = state.workbench.appearance_view.appearance_list.scroll_top_for_index(selected);
                    state.workbench.appearance_view.appearance_list.set_scroll_top(scroll_top);
                    if (auto* list = find_el(doc, "appearance_rows")) {
                        list->SetScrollTop(static_cast<float>(scroll_top));
                    }
                    state.workbench.appearance_view.appearance_rendered = false;
                    sync_appearance_window(doc, state, true);
                    dispatch.native_handled = true;
                    break;
                }
                if (!event.key.repeat && appearance_search_focused
                    && (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER)) {
                    const int selected = state.workbench.appearance_view.appearance_list.selected();
                    const auto& catalog = active_appearance_catalog(state);
                    if (selected >= 0
                        && static_cast<size_t>(selected) < state.workbench.appearance_view.appearance_matches.size()) {
                        const uint32_t row_index = state.workbench.appearance_view.appearance_matches[static_cast<size_t>(selected)];
                        if (row_index < catalog.rows.size()) {
                            if (commit_active_appearance_selection(
                                    state, catalog.rows[row_index].id)) {
                                close_appearance_selector(state);
                                rebuild_active_appearances(state, state.workbench.object_details.object);
                                refresh_workspace_content(doc, state);
                            }
                        }
                    }
                    dispatch.native_handled = true;
                    break;
                }

                const bool sound_catalog_search_focused
                    = !state.shell.command_palette_visible
                    && active_sound_resource_selector_matches_tab(state)
                    && focused_element_has_id(context, "sound_catalog_search")
                    && !(event.key.mod
                        & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI));
                if (sound_catalog_search_focused
                    && (event.key.key == SDLK_UP
                        || event.key.key == SDLK_DOWN)) {
                    const int selected = state.workbench.appearance_view.sound_catalog_list.move_selection(
                        event.key.key == SDLK_UP ? -1 : 1);
                    const int scroll_top
                        = state.workbench.appearance_view.sound_catalog_list.scroll_top_for_index(selected);
                    state.workbench.appearance_view.sound_catalog_list.set_scroll_top(scroll_top);
                    if (auto* list = find_el(doc, "sound_catalog_rows")) {
                        list->SetScrollTop(static_cast<float>(scroll_top));
                    }
                    state.workbench.appearance_view.sound_catalog_rendered = false;
                    sync_sound_catalog_window(doc, state, true);
                    dispatch.native_handled = true;
                    break;
                }
                if (!event.key.repeat && sound_catalog_search_focused
                    && (event.key.key == SDLK_RETURN
                        || event.key.key == SDLK_KP_ENTER)) {
                    const int selected = state.workbench.appearance_view.sound_catalog_list.selected();
                    if (selected >= 0
                        && static_cast<size_t>(selected)
                            < state.workbench.appearance_view.sound_catalog_matches.size()) {
                        const uint32_t row_index
                            = state.workbench.appearance_view.sound_catalog_matches[static_cast<size_t>(selected)];
                        if (commit_sound_catalog_selection(state, row_index)) {
                            close_sound_resource_selector(state);
                            refresh_workspace_content(doc, state);
                        }
                    }
                    dispatch.native_handled = true;
                    break;
                }

                auto* focused_managed_list = find_ancestor_with_class(
                    context->GetFocusElement(), "managed_list_cycle");
                const bool managed_list_cycle_focused = !state.shell.command_palette_visible
                    && !state.viewer_viewport_focused
                    && focused_managed_list
                    && !(event.key.mod
                        & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI
                            | SDL_KMOD_SHIFT));
                if (managed_list_cycle_focused
                    && (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN)
                    && cycle_managed_list(doc, state, focused_managed_list,
                        event.key.key == SDLK_UP ? -1 : 1)) {
                    state.viewer_viewport_focused = false;
                    dispatch.native_handled = true;
                    break;
                }

                const bool creature_spell_filter_focused = !state.shell.command_palette_visible
                    && active_creature_spell_filter_matches_tab(state)
                    && focused_element_has_id(context, "active_creature_spell_filter_field")
                    && !(event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI));
                if (creature_spell_filter_focused
                    && (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN)) {
                    if (!state.workbench.creature_view.creature_spell_combobox.popup_visible()) {
                        (void)state.workbench.creature_view.creature_spell_combobox.show_popup();
                        refresh_workspace_content(doc, state);
                    }
                    (void)state.workbench.creature_view.creature_spell_combobox.move_selection(
                        event.key.key == SDLK_UP ? -1 : 1);
                    sync_creature_spell_filter_window(doc, state, true);
                    dispatch.native_handled = true;
                    break;
                }
                if (!event.key.repeat && creature_spell_filter_focused
                    && (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER)) {
                    if (!state.workbench.creature_view.creature_spell_combobox.popup_visible()) {
                        (void)state.workbench.creature_view.creature_spell_combobox.show_popup();
                        refresh_workspace_content(doc, state);
                        sync_creature_spell_filter_window(doc, state, true);
                    } else if (const auto selected = state.workbench.creature_view.creature_spell_combobox.selected_key()) {
                        if (commit_creature_spell_filter(state, *selected)) {
                            refresh_workspace_content(doc, state);
                            sync_creature_spell_window(doc, state, true);
                        }
                    }
                    dispatch.native_handled = true;
                    break;
                }

                const bool output_shortcut = !event.key.repeat
                    && focused_element_has_id(context, "output_list")
                    && (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI))
                    && !(event.key.mod & SDL_KMOD_ALT);
                if (output_shortcut && event.key.key == SDLK_A) {
                    state.shell_view.output_selection.anchor = 0;
                    state.shell_view.output_selection.focus = state.shell_view.output_selection.text.size();
                    state.shell.output_dirty = true;
                    dispatch.native_handled = true;
                    break;
                }
                if (output_shortcut && event.key.key == SDLK_C) {
                    const auto [selection_start, selection_end]
                        = state.shell_view.output_selection.range();
                    if (selection_start < selection_end) {
                        system_interface.SetClipboardText(state.shell_view.output_selection.text.substr(
                            selection_start, selection_end - selection_start));
                    }
                    dispatch.native_handled = true;
                    break;
                }

                const bool command_ctrl = !event.key.repeat
                    && (event.key.mod & SDL_KMOD_CTRL)
                    && !(event.key.mod & (SDL_KMOD_SHIFT | SDL_KMOD_ALT | SDL_KMOD_GUI));
                if (command_ctrl && event.key.key == SDLK_W) {
                    cancel_area_tile_stroke(renderer, state);
                    if (ensure_backend_ready(state)) {
                        dispatch_command_flow(
                            window, state, "workspace.close_tab", {}, nw::toolset::CommandSource::shortcut);
                        refresh_workspace_view(doc, state);
                    }
                    dispatch.native_handled = true;
                    break;
                }
                const bool save_all_shortcut = !event.key.repeat
                    && (event.key.mod & SDL_KMOD_CTRL)
                    && (event.key.mod & SDL_KMOD_SHIFT)
                    && !(event.key.mod & (SDL_KMOD_ALT | SDL_KMOD_GUI));
                if ((command_ctrl || save_all_shortcut) && event.key.key == SDLK_S) {
                    cancel_area_tile_stroke(renderer, state);
                    if (ensure_backend_ready(state)) {
                        dispatch_command_flow(
                            window, state, save_all_shortcut ? "toolset.save_all" : "workspace.save_tab",
                            {}, nw::toolset::CommandSource::shortcut);
                        refresh_workspace_view(doc, state);
                    }
                    dispatch.native_handled = true;
                    break;
                }
                if (command_ctrl && (event.key.key == SDLK_Z || event.key.key == SDLK_Y)) {
                    cancel_area_tile_stroke(renderer, state);
                    if (ensure_backend_ready(state)) {
                        dispatch_command_flow(window,
                            state,
                            event.key.key == SDLK_Z ? "command.undo" : "command.redo",
                            {},
                            nw::toolset::CommandSource::shortcut);
                    }
                    dispatch.native_handled = true;
                    break;
                }

                if ((event.key.mod & SDL_KMOD_CTRL) && event.key.key == SDLK_J) {
                    append_command_result(state, dispatch_command(state, "rollnw.client.output.toggle", {}, nw::toolset::CommandSource::shortcut));
                    toggle_output_panel(doc, state, state.shell.output_panel_visible());
                    dispatch.native_handled = true;
                    break;
                }

                if (!(event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) && event.key.key == SDLK_GRAVE) {
                    append_command_result(state, dispatch_command(state, "rollnw.client.terminal.toggle", {}, nw::toolset::CommandSource::shortcut));
                    toggle_terminal(doc, state, state.shell.terminal_visible());
                    state.shell_view.suppress_terminal_toggle_text_input = true;
                    dispatch.native_handled = true;
                    break;
                }

                const bool terminal_input_focused = focused_element_has_id(context, "terminal_input");
                if (state.shell.terminal_visible() && terminal_input_focused && event.key.key == SDLK_TAB) {
                    complete_terminal_command(doc, state);
                    dispatch.native_handled = true;
                    break;
                }

                if (state.shell.terminal_visible() && terminal_input_focused && event.key.key == SDLK_RETURN) {
                    const std::string line = get_input_value(doc, "terminal_input");
                    if (!line.empty()) {
                        append_terminal(state, "cmd", std::string("> ") + line);
                        if (state.backend.is_open_module_dialog_invocation(line)) {
                            show_open_module_dialog(window, state);
                        } else if (state.backend.is_open_project_dialog_invocation(line)) {
                            show_open_project_dialog(window, state);
                        } else if (ensure_backend_ready(state)) {
                            const bool was_showing_areas = state.shell.showing_areas;
                            const bool was_showing_project = state.shell.showing_project_tree;
                            auto result = state.backend.console_execute(line, command_context(state, nw::toolset::CommandSource::terminal));
                            result = resolve_command_result(
                                window, state, std::move(result), nw::toolset::CommandSource::terminal, true);
                            if (result.ok() && state.shell.showing_project_tree) {
                                remember_recent_project(state, state.backend.current_project_dir());
                            }
                            sync_shell_visibility(context, palette_context, doc, palette_doc, state);
                            if (result.ok()
                                && (state.shell.showing_areas
                                    || state.shell.showing_project_tree
                                    || state.shell.showing_areas != was_showing_areas
                                    || state.shell.showing_project_tree != was_showing_project)) {
                                refresh_recent_list(doc, state);
                            }
                            refresh_workspace_view(doc, state);
                        } else {
                            append_terminal(state, "error", "Backend initialization failed");
                        }
                        set_input_value(doc, "terminal_input", "");
                    }
                    dispatch.native_handled = true;
                    break;
                }

                if (handle_area_tile_key(
                        renderer, context, doc, state, event.key,
                        frame_width, frame_height)
                    || handle_area_object_key(
                        renderer, context, doc, state, event.key, frame_width, frame_height)
                    || handle_viewer_viewport_key(
                        renderer, context, doc, state, event.key, frame_width, frame_height)) {
                    dispatch.native_handled = true;
                    break;
                }
                break;
            }
            case SDL_EVENT_KEY_UP:
                if (state.play_preview.session.active()) {
                    dispatch.native_handled = true;
                }
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                nw::toolset::open_runtime_gamepad(
                    state.runtime_input, event.gdevice.which);
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                if (state.runtime_input.gamepad
                    && SDL_GetGamepadID(state.runtime_input.gamepad.get())
                        == event.gdevice.which) {
                    nw::toolset::close_runtime_gamepad(state.runtime_input);
                    if (state.play_preview.session.active()) {
                        append_output(state, "warn",
                            "Play-preview controller disconnected");
                    }
                }
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                if (state.play_preview.session.active()
                    && (event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST
                        || event.gbutton.button == SDL_GAMEPAD_BUTTON_BACK)) {
                    state.runtime_input.pending.flags
                        |= nw::toolset::preview_input_cancel;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                blur_focused_object_variable_input(context,
                    to_context_point(window, event.button.x, event.button.y));
                if (event.button.button == SDL_BUTTON_RIGHT
                    && state.area_tile_editor.stroke.active) {
                    cancel_area_tile_stroke(renderer, state);
                    system_interface.SetMouseCursor("arrow");
                    dispatch.native_handled = true;
                    break;
                }
                if (event.button.button == SDL_BUTTON_RIGHT
                    && state.managed_list_reorder.active()) {
                    nw::toolset::clear_managed_list_reorder(
                        state.managed_list_reorder, doc);
                    system_interface.SetMouseCursor("arrow");
                    dispatch.native_handled = true;
                    break;
                }
                if (event.button.button == SDL_BUTTON_RIGHT
                    && state.project_blueprint_drag.active()) {
                    cancel_project_blueprint_drag(doc, state);
                    state.browser.pressed_recent_index = -1;
                    system_interface.SetMouseCursor("arrow");
                    dispatch.native_handled = true;
                    break;
                }
                if (state.project_blueprint_drag.active()
                    && state.project_blueprint_drag.threshold_crossed) {
                    dispatch.native_handled = true;
                    break;
                }
                if (event.button.button == SDL_BUTTON_RIGHT
                    && state.area_object_placement.active()) {
                    cancel_area_object_placement(renderer, state);
                    state.browser.pressed_recent_index = -1;
                    system_interface.SetMouseCursor("arrow");
                    dispatch.native_handled = true;
                    break;
                }
                if (event.button.button == SDL_BUTTON_LEFT
                    && state.area_object_placement.region_drawing) {
                    const auto point = to_context_point(
                        window, event.button.x, event.button.y);
                    const auto viewport = active_workspace_viewer_viewport_request(
                        doc, state, frame_width, frame_height);
                    if (viewport
                        && viewport->kind == WorkspaceViewerViewportKind::area
                        && point_within_viewport(viewport->rect, point)) {
                        if (event.button.clicks >= 2) {
                            complete_area_region_placement(
                                renderer, state, point, *viewport);
                        } else {
                            accept_area_region_point(
                                renderer, state, point, *viewport);
                        }
                    }
                    system_interface.SetMouseCursor(
                        state.area_object_placement.region_drawing
                            ? "cross"
                            : "arrow");
                    dispatch.native_handled = true;
                    break;
                }
                if (state.area_object_placement.active()
                    && state.area_object_placement.threshold_crossed) {
                    dispatch.native_handled = true;
                    break;
                }
                if (event.button.button == SDL_BUTTON_LEFT
                    || event.button.button == SDL_BUTTON_MIDDLE
                    || event.button.button == SDL_BUTTON_RIGHT) {
                    const auto point = to_context_point(window, event.button.x, event.button.y);
                    if (command_palette_contains_point(
                            palette_doc, state, point)) {
                        (void)nw::toolset::forward_client_input(dispatch, nw::toolset::ClientRmlRecipient::command,
                            nw::toolset::ClientRmlForwardPhase::after_native, palette_context, window, event);
                        dispatch.native_handled = true;
                        break;
                    }
                    auto* top_hit = context ? context->GetElementAtPoint(point) : nullptr;
                    if (event.button.button == SDL_BUTTON_LEFT
                        && !nw::toolset::combobox_contains_element(top_hit)) {
                        const bool closed_smalls
                            = close_active_smalls_selector(doc);
                        const bool closed_details
                            = state.workbench.object_details_combobox.is_active();
                        if (closed_details) {
                            close_object_details_combobox(doc, state);
                        }
                        const bool closed_spell
                            = state.workbench.creature_view.creature_spell_combobox.is_active();
                        if (closed_spell) {
                            clear_creature_spell_filter(state);
                            refresh_workspace_content(doc, state);
                        }
                        if (closed_smalls || closed_details || closed_spell) {
                            context->Update();
                            top_hit = context->GetElementAtPoint(point);
                        }
                    }
                    if (auto viewer_viewport = active_workspace_viewer_viewport_request(doc, state, frame_width, frame_height);
                        viewer_viewport
                        && point_within_viewport(viewer_viewport->rect, point)
                        && !viewport_mouse_hit_blocked(doc, top_hit, point, state)) {
                        (void)close_active_smalls_selector(doc);
                        if (state.workbench.appearance_view.appearance_selector_open) {
                            close_appearance_selector(state);
                            rebuild_active_appearances(
                                state, state.workbench.object_details.object);
                            refresh_workspace_content(doc, state);
                        }
                        if (state.workbench.appearance_view.sound_resource_selector_open) {
                            close_sound_resource_selector(state);
                            refresh_workspace_content(doc, state);
                        }
                        state.viewer_viewport_focused = true;
                        state.viewer_viewport_last_point = point;
                        clear_rml_focus(context);
                        if (viewer_viewport->kind
                                == WorkspaceViewerViewportKind::area
                            && !synchronize_area_viewport_structure(
                                renderer, state, true)) {
                            system_interface.SetMouseCursor("unavailable");
                            dispatch.native_handled = true;
                            break;
                        }
                        if ((state.play_preview.session.active()
                                || state.play_preview.placement_pending())
                            && viewer_viewport->kind == WorkspaceViewerViewportKind::area) {
                            if (event.button.button == SDL_BUTTON_LEFT
                                && state.play_preview.placement_pending()) {
                                if (const auto ray
                                    = nw::toolset::acquire_runtime_viewport_ray(renderer,
                                        {point.x, point.y}, viewer_viewport->rect)) {
                                    (void)start_play_preview_from_ray(renderer,
                                        system_interface, doc, state, *ray);
                                } else {
                                    state.play_preview.placement_diagnostic
                                        = "Navigation ray could not be constructed";
                                    system_interface.SetMouseCursor("cross");
                                    append_output(state, "warn",
                                        state.play_preview.placement_diagnostic);
                                }
                            } else if (event.button.button == SDL_BUTTON_LEFT) {
                                nw::toolset::apply_play_preview_pointer_action(renderer, state.play_preview,
                                    state.runtime_input, {point.x, point.y}, viewer_viewport->rect);
                            } else if (event.button.button == SDL_BUTTON_RIGHT
                                || event.button.button == SDL_BUTTON_MIDDLE) {
                                state.viewer_viewport_dragging = true;
                                state.viewer_viewport_drag_mode
                                    = ClientViewportDragMode::look;
                                system_interface.SetMouseCursor("grabbing");
                            }
                            dispatch.native_handled = true;
                            break;
                        }
                        if (handle_area_tile_pointer_down(
                                renderer, system_interface, doc, state,
                                point, *viewer_viewport,
                                event.button.button)) {
                            dispatch.native_handled = true;
                            break;
                        }
                        const bool preview_orbit_drag = viewer_viewport->kind == WorkspaceViewerViewportKind::preview
                            && event.button.button == SDL_BUTTON_LEFT;
                        if (viewer_viewport->kind
                                == WorkspaceViewerViewportKind::area
                            && event.button.button == SDL_BUTTON_LEFT
                            && shift_only(SDL_GetModState())
                            && add_encounter_spawn_point(
                                renderer, state, point, *viewer_viewport)) {
                            system_interface.SetMouseCursor("arrow");
                            dispatch.native_handled = true;
                            break;
                        }
                        if (viewer_viewport->kind == WorkspaceViewerViewportKind::area
                            && state.area_workspace_surface
                                == AreaWorkspaceSurface::objects
                            && event.button.button == SDL_BUTTON_LEFT) {
                            const bool control_pressed = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
                            renderer.select_viewer_area_object(
                                point.x,
                                point.y,
                                viewer_viewport->rect,
                                control_pressed
                                    ? ClientAreaSelectionTarget::tile
                                    : ClientAreaSelectionTarget::object);
                            if (begin_area_object_drag(renderer, state, point, *viewer_viewport)) {
                                system_interface.SetMouseCursor("arrow");
                            }
                        }
                        if (preview_orbit_drag || event.button.button == SDL_BUTTON_RIGHT || event.button.button == SDL_BUTTON_MIDDLE) {
                            cancel_area_object_drag(renderer, state);
                            state.viewer_viewport_dragging = true;
                            state.viewer_viewport_drag_mode = event.button.button == SDL_BUTTON_MIDDLE
                                ? ClientViewportDragMode::pan
                                : ClientViewportDragMode::look;
                            system_interface.SetMouseCursor("grabbing");
                        }
                        dispatch.native_handled = true;
                        break;
                    }
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        state.viewer_viewport_focused = false;
                    }
                }
                if (event.button.button == SDL_BUTTON_LEFT) {
                    if (begin_bottom_dock_resize(context, window, doc, state, event.button)) {
                        dispatch.native_handled = true;
                        break;
                    }
                    if (begin_left_dock_resize(context, window, doc, state, event.button)) {
                        dispatch.native_handled = true;
                        break;
                    }

                    state.browser.pressed_recent_index = -1;
                    const auto point = to_context_point(window, event.button.x, event.button.y);
                    if (const auto offset = output_text_offset_at_point(
                            context, doc, state, point)) {
                        if (auto* output = find_el(doc, "output_list")) {
                            output->Focus();
                        }
                        state.shell_view.output_selection.anchor = *offset;
                        state.shell_view.output_selection.focus = *offset;
                        state.shell_view.output_selection.dragging = true;
                        state.shell.output_dirty = true;
                        state.viewer_viewport_focused = false;
                        dispatch.native_handled = true;
                        break;
                    }
                    if (state.shell_view.output_selection.active()) {
                        state.shell_view.output_selection.clear();
                        state.shell.output_dirty = true;
                    }
                    auto* top_hit = context ? context->GetElementAtPoint(point) : nullptr;
                    clear_workspace_tab_drag(state);
                    auto* workspace_tab_close_hit = find_ancestor_with_class(top_hit, "workspace_tab_close");
                    if (!workspace_tab_close_hit) {
                        workspace_tab_close_hit = workspace_tab_element_at_point(doc, "workspace_tab_close", point);
                    }
                    if (!workspace_tab_close_hit) {
                        auto* workspace_tab_hit = find_ancestor_with_class(top_hit, "workspace_tab");
                        if (!workspace_tab_hit) {
                            workspace_tab_hit = workspace_tab_element_at_point(doc, "workspace_tab", point);
                        }
                        if (workspace_tab_hit && workspace_tab_hit->GetAttribute<Rml::String>("data-movable", "") == "1") {
                            state.workspace_view.workspace_tab_drag_id = workspace_tab_hit->GetAttribute<Rml::String>("data-tab", "");
                            state.workspace_view.workspace_tab_drag_start_x = point.x;
                            state.workspace_view.workspace_tab_drag_start_y = point.y;
                            dispatch.native_handled = true;
                            break;
                        }
                    }
                    if (!state.project_blueprint_drag.active()
                        && nw::toolset::begin_managed_list_reorder(
                            state.managed_list_reorder, top_hit,
                            nw::toolset::ui_v1_host(), point.x, point.y)) {
                        dispatch.native_handled = true;
                        break;
                    }
                    if (!recent_list_hit_blocked(doc, top_hit, point, state)) {
                        if (auto* recent_item = recent_item_at_point(doc, point)) {
                            const std::string key = recent_item->GetAttribute<Rml::String>("data-key", "");
                            if (!key.empty()) {
                                state.browser.pressed_recent_index = static_cast<int>(std::strtol(key.c_str(), nullptr, 10));
                                const size_t index = static_cast<size_t>(
                                    std::max(state.browser.pressed_recent_index, 0));
                                if (state.shell.showing_project_tree
                                    && !state.play_preview.selecting_actor
                                    && state.browser.pressed_recent_index >= 0
                                    && index < state.browser.project_rows.size()) {
                                    const auto& row = state.browser.project_rows[index].node;
                                    if (!row.is_container()) {
                                        const auto resource = nw::Resource::from_path(
                                            row.relative_path, false);
                                        const bool armed = arm_project_blueprint_drag(
                                            state, resource, row.path, point);
                                        if (!armed) {
                                            arm_area_object_placement(
                                                renderer, state, resource, point);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                break;
            case SDL_EVENT_MOUSE_MOTION: {
                if (update_bottom_dock_resize(doc, state, window, event.motion)) {
                    dispatch.native_handled = true;
                    break;
                }
                if (update_left_dock_resize(doc, state, window, event.motion)) {
                    dispatch.native_handled = true;
                    break;
                }

                const auto point = to_context_point(window, event.motion.x, event.motion.y);
                auto* top_hit = context ? context->GetElementAtPoint(point) : nullptr;
                sync_object_variable_warning_tooltip(doc, state, top_hit, point,
                    frame_width, frame_height);
                if (state.shell_view.output_selection.dragging) {
                    if (const auto offset = output_text_offset_at_point(
                            context, doc, state, point);
                        offset && state.shell_view.output_selection.focus != *offset) {
                        state.shell_view.output_selection.focus = *offset;
                        state.shell.output_dirty = true;
                    }
                    dispatch.native_handled = true;
                    break;
                }
                if (command_palette_contains_point(
                        palette_doc, state, point)) {
                    (void)nw::toolset::forward_client_input(dispatch, nw::toolset::ClientRmlRecipient::command,
                        nw::toolset::ClientRmlForwardPhase::after_native, palette_context, window, event);
                    dispatch.native_handled = true;
                    break;
                }
                if (nw::toolset::update_managed_list_reorder(
                        state.managed_list_reorder, doc,
                        nw::toolset::ui_v1_host(), state.workbench.managed_lists,
                        point.x, point.y, kWorkspaceTabDragThresholdPx,
                        kManagedListAutoScrollEdgePx,
                        kManagedListAutoScrollStepPx)) {
                    const auto& gesture = state.managed_list_reorder;
                    const char* cursor = "arrow";
                    if (gesture.dragging) {
                        cursor = nw::toolset::managed_list_reorder_destination(
                                     gesture)
                            ? "grabbing"
                            : "unavailable";
                    }
                    system_interface.SetMouseCursor(cursor);
                    dispatch.native_handled = true;
                    break;
                }
                if (state.project_blueprint_drag.active()) {
                    if (update_project_blueprint_drag(context, doc, state, point)) {
                        const char* cursor = state.project_blueprint_drag.phase
                                == ProjectBlueprintDragPhase::target_valid
                            ? "cross"
                            : "unavailable";
                        system_interface.SetMouseCursor(cursor);
                        dispatch.native_handled = true;
                        break;
                    }
                }
                if (state.area_object_placement.active()) {
                    const auto viewport = active_workspace_viewer_viewport_request(
                        doc, state, frame_width, frame_height);
                    if (update_area_object_placement(renderer, state, point, viewport)) {
                        const char* cursor = "unavailable";
                        if (!state.area_object_placement.active()) {
                            cursor = "arrow";
                        } else if (state.area_object_placement.phase == AreaObjectPlacementPhase::ghost_valid) {
                            cursor = "cross";
                        }
                        system_interface.SetMouseCursor(cursor);
                        dispatch.native_handled = true;
                        break;
                    }
                }
                if (state.viewer_viewport_dragging) {
                    const float dx = point.x - state.viewer_viewport_last_point.x;
                    const float dy = point.y - state.viewer_viewport_last_point.y;
                    state.viewer_viewport_last_point = point;
                    if (auto viewer_viewport = active_workspace_viewer_viewport_request(doc, state, frame_width, frame_height)) {
                        if (state.play_preview.session.active()) {
                            state.runtime_input.mouse_look_pixels.x += dx;
                            state.runtime_input.mouse_look_pixels.y += dy;
                        } else {
                            renderer.drag_viewer_viewport(
                                state.viewer_viewport_drag_mode, dx, dy,
                                viewer_viewport->rect);
                            if (state.area_workspace_surface
                                == AreaWorkspaceSurface::tiles) {
                                state.area_tile_editor.cursor_update_pending = true;
                            }
                        }
                        system_interface.SetMouseCursor("grabbing");
                    } else {
                        state.viewer_viewport_dragging = false;
                        system_interface.SetMouseCursor("arrow");
                    }
                    dispatch.native_handled = true;
                    break;
                }
                if (state.area_object_drag.active) {
                    if (auto viewer_viewport = active_workspace_viewer_viewport_request(
                            doc, state, frame_width, frame_height)) {
                        update_area_object_drag(renderer, state, point, *viewer_viewport);
                        system_interface.SetMouseCursor(!state.area_object_drag.pointer.dragging ? "arrow"
                                : state.area_object_drag.valid                                   ? "grabbing"
                                                                                                 : "unavailable");
                    } else {
                        cancel_area_object_drag(renderer, state);
                        system_interface.SetMouseCursor("arrow");
                    }
                    dispatch.native_handled = true;
                    break;
                }

                if (state.area_workspace_surface
                    == AreaWorkspaceSurface::tiles) {
                    const auto viewer_viewport
                        = active_workspace_viewer_viewport_request(
                            doc, state, frame_width, frame_height);
                    if (viewer_viewport
                        && viewer_viewport->kind
                            == WorkspaceViewerViewportKind::area
                        && point_within_viewport(
                            viewer_viewport->rect, point)
                        && !viewport_mouse_hit_blocked(
                            doc, top_hit, point, state)) {
                        state.area_tile_editor.pending_cursor_point = point;
                        const bool tile_shift
                            = area_tile_pointer_modifier(SDL_GetModState())
                            == nw::toolset::AreaTilePointerModifier::select;
                        state.area_tile_editor.cursor_update_pending
                            = true;
                        system_interface.SetMouseCursor(
                            tile_shift ? "pointer" : "cross");
                        dispatch.native_handled = true;
                        break;
                    }
                    state.area_tile_editor.cursor_update_pending = false;
                    if (state.area_tile_editor.stroke.active) {
                        state.area_tile_editor.stroke.has_last_target = false;
                        dispatch.native_handled = true;
                        break;
                    }
                    if (state.area_tile_editor.cursor_target_index != UINT32_MAX
                        || !state.area_tile_editor.preview_rows.empty()) {
                        state.area_tile_editor.cursor_target_index = UINT32_MAX;
                        state.area_tile_editor.preview_rows.clear();
                        (void)renderer.update_viewer_area_tile_preview(
                            active_workspace_area(state), {});
                    }
                }

                if (state.play_preview.session.active()
                    || state.play_preview.placement_pending()) {
                    const auto viewer_viewport
                        = active_workspace_viewer_viewport_request(
                            doc, state, frame_width, frame_height);
                    if (viewer_viewport
                        && viewer_viewport->kind
                            == WorkspaceViewerViewportKind::area
                        && point_within_viewport(viewer_viewport->rect, point)
                        && !viewport_mouse_hit_blocked(
                            doc, top_hit, point, state)) {
                        system_interface.SetMouseCursor(nw::toolset::play_preview_pointer_cursor(
                            renderer, state.play_preview, {point.x, point.y}, viewer_viewport->rect));
                        dispatch.native_handled = true;
                        break;
                    }
                }

                if (!state.workspace_view.workspace_tab_drag_id.empty()) {
                    const float dx = point.x - state.workspace_view.workspace_tab_drag_start_x;
                    const float dy = point.y - state.workspace_view.workspace_tab_drag_start_y;
                    if (!state.workspace_view.workspace_tab_dragging
                        && (std::abs(dx) >= kWorkspaceTabDragThresholdPx || std::abs(dy) >= kWorkspaceTabDragThresholdPx)) {
                        state.workspace_view.workspace_tab_dragging = true;
                        system_interface.SetMouseCursor("grabbing");
                    }

                    if (state.workspace_view.workspace_tab_dragging) {
                        system_interface.SetMouseCursor("grabbing");
                        if (auto* tabs = find_el(doc, "workspace_tabs")) {
                            state.workspace_view.workspace_tab_scroll_x = tabs->GetScrollLeft();
                            const float left = tabs->GetAbsoluteLeft();
                            const float right = left + tabs->GetClientWidth();
                            if (point.x < left + kWorkspaceTabAutoScrollEdgePx) {
                                state.workspace_view.workspace_tab_scroll_x -= kWorkspaceTabAutoScrollStepPx;
                                apply_workspace_tab_scroll(doc, state);
                            } else if (point.x > right - kWorkspaceTabAutoScrollEdgePx) {
                                state.workspace_view.workspace_tab_scroll_x += kWorkspaceTabAutoScrollStepPx;
                                apply_workspace_tab_scroll(doc, state);
                            }
                        }

                        const auto& tabs = state.workspace.tabs();
                        const size_t current_index = workspace_tab_current_index(tabs, state.workspace_view.workspace_tab_drag_id, kInvalidVirtualIndex);
                        const size_t fallback = current_index == kInvalidVirtualIndex
                            ? (tabs.empty() ? 0 : tabs.size() - 1)
                            : current_index;
                        const size_t target_index = workspace_tab_target_index_at_point(doc, point, tabs, state.workspace_view.workspace_tab_drag_id, fallback);
                        if (current_index != kInvalidVirtualIndex && target_index != current_index) {
                            const std::string target_index_text = std::to_string(target_index);
                            const auto result = dispatch_command(state,
                                "workspace.move_tab",
                                {std::string_view{state.workspace_view.workspace_tab_drag_id}, std::string_view{target_index_text}},
                                nw::toolset::CommandSource::widget);
                            if (result.ok()) {
                                refresh_workspace_view(doc, state);
                            }
                        }
                        dispatch.native_handled = true;
                        break;
                    }
                }

                auto* list = find_el(doc, "recent_list");
                auto* search = find_el(doc, "recent_search");
                int hovered = -1;
                if (!recent_list_hit_blocked(doc, top_hit, point, state)
                    && list && list->IsVisible(true)
                    && (!search || !search->IsVisible(true)
                        || !search->IsPointWithinElement(point))
                    && list->IsPointWithinElement(point)) {
                    if (auto* recent_item = find_recent_item_at(list, point)) {
                        const std::string key = recent_item->GetAttribute<Rml::String>("data-key", "");
                        if (!key.empty()) {
                            hovered = static_cast<int>(std::strtol(key.c_str(), nullptr, 10));
                        }
                    }
                }
                set_recent_hover(doc, state, hovered);
                break;
            }
            case SDL_EVENT_MOUSE_WHEEL: {
                if (state.project_blueprint_drag.active()
                    && state.project_blueprint_drag.threshold_crossed) {
                    dispatch.native_handled = true;
                    break;
                }
                if (state.area_object_placement.active()
                    && state.area_object_placement.threshold_crossed) {
                    dispatch.native_handled = true;
                    break;
                }
                const auto point = to_context_point(window, event.wheel.mouse_x, event.wheel.mouse_y);
                if (command_palette_contains_point(
                        palette_doc, state, point)) {
                    (void)nw::toolset::forward_client_input(dispatch, nw::toolset::ClientRmlRecipient::command,
                        nw::toolset::ClientRmlForwardPhase::after_native, palette_context, window, event);
                    dispatch.native_handled = true;
                    break;
                }
                auto* wheel_hit = context
                    ? context->GetElementAtPoint(point)
                    : nullptr;
                if (nw::toolset::combobox_popup_contains_element(
                        wheel_hit)) {
                    // RmlUi scrolls the open popup. Focused-field cycling is
                    // only the closed combobox path.
                    break;
                }
                if (event.wheel.y != 0.0f) {
                    const SDL_Keymod modifiers = SDL_GetModState();
                    auto* focused_managed_list = find_ancestor_with_class(
                        context->GetFocusElement(), "managed_list_cycle");
                    const bool managed_list_cycle_focused
                        = !state.shell.command_palette_visible
                        && !state.loading.module_dialog_open
                        && !state.viewer_viewport_focused
                        && focused_managed_list
                        && !(modifiers
                            & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI
                                | SDL_KMOD_SHIFT));
                    if (managed_list_cycle_focused
                        && cycle_managed_list(doc, state,
                            focused_managed_list,
                            event.wheel.y > 0.0f ? -1 : 1)) {
                        state.viewer_viewport_focused = false;
                        dispatch.native_handled = true;
                        break;
                    }
                    if (auto viewer_viewport = active_workspace_viewer_viewport_request(doc, state, frame_width, frame_height);
                        viewer_viewport && point_within_viewport(viewer_viewport->rect, point)) {
                        if (state.play_preview.session.active()) {
                            state.runtime_input.wheel_zoom += event.wheel.y;
                            dispatch.native_handled = true;
                            break;
                        }
                        const auto object = renderer.active_viewer_object();
                        const bool area_object_wheel = viewer_viewport->kind == WorkspaceViewerViewportKind::area
                            && (object.type == nw::ObjectType::creature
                                || object.type == nw::ObjectType::item
                                || object.type == nw::ObjectType::placeable)
                            && !focused_text_input(context)
                            && !state.loading.module_dialog_open;
                        const bool sound_radius_wheel
                            = viewer_viewport->kind == WorkspaceViewerViewportKind::area
                            && object.type == nw::ObjectType::sound
                            && !focused_text_input(context)
                            && !state.loading.module_dialog_open;
                        cancel_area_object_drag(renderer, state);
                        if (sound_radius_wheel
                            && !(modifiers & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI | SDL_KMOD_SHIFT))) {
                            if (const auto sound
                                = nwn1::sound_toolset_visual_state(object)) {
                                const float factor = std::pow(1.1f, event.wheel.y);
                                const float radius = std::max(sound->distance_min,
                                    sound->distance_max * factor);
                                if (std::isfinite(radius)
                                    && radius != sound->distance_max) {
                                    const auto result = state.backend.resize_sound_radius(
                                        {
                                            .sound = object,
                                            .before = sound->distance_max,
                                            .after = radius,
                                        },
                                        command_context(state,
                                            nw::toolset::CommandSource::renderer));
                                    append_command_result(state, result);
                                }
                            }
                            dispatch.native_handled = true;
                            break;
                        }
                        if (area_object_wheel
                            && !(modifiers & (SDL_KMOD_ALT | SDL_KMOD_GUI | SDL_KMOD_SHIFT))) {
                            const bool rotate = (modifiers & SDL_KMOD_CTRL) != 0;
                            const float value = rotate
                                ? event.wheel.y * 15.0f
                                : std::pow(1.1f, event.wheel.y);
                            const std::string argument = precise_float_text(value);
                            if (!argument.empty()) {
                                const auto result = dispatch_command(state,
                                    rotate ? "object.transform.rotate" : "object.transform.scale",
                                    {argument},
                                    nw::toolset::CommandSource::renderer);
                                sync_area_object_after_command(renderer, state, result);
                            }
                            dispatch.native_handled = true;
                            break;
                        }
                        renderer.zoom_viewer_viewport(event.wheel.y, viewer_viewport->rect);
                        if (state.area_workspace_surface == AreaWorkspaceSurface::tiles) {
                            state.area_tile_editor.cursor_update_pending = true;
                        }
                        dispatch.native_handled = true;
                        break;
                    }
                }
                if (point_within_element(doc, "workspace_tabs", point)) {
                    const float delta = event.wheel.x != 0.0f ? event.wheel.x : -event.wheel.y;
                    if (auto* tabs = find_el(doc, "workspace_tabs")) {
                        state.workspace_view.workspace_tab_scroll_x = tabs->GetScrollLeft();
                    }
                    state.workspace_view.workspace_tab_scroll_x += delta * kTabScrollStepPx;
                    apply_workspace_tab_scroll(doc, state);
                    dispatch.native_handled = true;
                    break;
                }
                if (point_within_element(doc, "object_workbench_tabs", point)) {
                    const float delta = event.wheel.x != 0.0f ? event.wheel.x : -event.wheel.y;
                    if (auto* tabs = find_el(doc, "object_workbench_tabs")) {
                        state.workbench.object_workbench_tab_scroll_x = tabs->GetScrollLeft();
                    }
                    state.workbench.object_workbench_tab_scroll_x += delta * kTabScrollStepPx;
                    apply_object_workbench_tab_scroll(doc, state);
                    dispatch.native_handled = true;
                    break;
                }
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (state.area_tile_editor.stroke.active
                    && event.button.button
                        == state.area_tile_editor.stroke.pointer_button) {
                    state.area_tile_editor.cursor_update_pending = false;
                    const auto point = to_context_point(
                        window, event.button.x, event.button.y);
                    if (const auto viewport
                        = active_workspace_viewer_viewport_request(
                            doc, state, frame_width, frame_height);
                        viewport
                        && viewport->kind
                            == WorkspaceViewerViewportKind::area
                        && point_within_viewport(viewport->rect, point)) {
                        (void)update_area_tile_cursor(
                            renderer, state, point, *viewport);
                    }
                    commit_area_tile_stroke(renderer, state);
                    system_interface.SetMouseCursor("arrow");
                    dispatch.native_handled = true;
                    break;
                }
                if (state.shell_view.output_selection.dragging
                    && event.button.button == SDL_BUTTON_LEFT) {
                    const auto point = to_context_point(
                        window, event.button.x, event.button.y);
                    if (const auto offset = output_text_offset_at_point(
                            context, doc, state, point);
                        offset && state.shell_view.output_selection.focus != *offset) {
                        state.shell_view.output_selection.focus = *offset;
                        state.shell.output_dirty = true;
                    }
                    state.shell_view.output_selection.dragging = false;
                    dispatch.native_handled = true;
                    break;
                }
                if (state.managed_list_reorder.active()
                    && event.button.button == SDL_BUTTON_LEFT) {
                    const auto point = to_context_point(
                        window, event.button.x, event.button.y);
                    const bool was_dragging = state.managed_list_reorder.dragging;
                    (void)nw::toolset::update_managed_list_reorder(
                        state.managed_list_reorder, doc,
                        nw::toolset::ui_v1_host(), state.workbench.managed_lists,
                        point.x, point.y, kWorkspaceTabDragThresholdPx,
                        kManagedListAutoScrollEdgePx,
                        kManagedListAutoScrollStepPx);
                    const bool dragged = was_dragging
                        || (state.managed_list_reorder.active()
                            && state.managed_list_reorder.dragging);
                    if (dragged) {
                        if (nw::toolset::commit_managed_list_reorder(
                                state.managed_list_reorder, doc,
                                nw::toolset::ui_v1_host())) {
                            dispatch_managed_list_events(state);
                            refresh_smalls_elements(doc, state);
                            (void)nw::toolset::sync_managed_lists(doc,
                                nw::toolset::ui_v1_host(),
                                state.workbench.managed_lists, true);
                        }
                    } else {
                        nw::toolset::clear_managed_list_reorder(
                            state.managed_list_reorder, doc);
                    }
                    system_interface.SetMouseCursor("arrow");
                    if (dragged) {
                        dispatch.native_handled = true;
                        break;
                    }
                }
                if (state.project_blueprint_drag.active()
                    && event.button.button == SDL_BUTTON_LEFT) {
                    const bool blueprint_dragged = state.project_blueprint_drag.threshold_crossed;
                    if (blueprint_dragged) {
                        const auto point = to_context_point(
                            window, event.button.x, event.button.y);
                        (void)update_project_blueprint_drag(context, doc, state, point);
                        commit_project_blueprint_drag(doc, state);
                        state.browser.pressed_recent_index = -1;
                        system_interface.SetMouseCursor("arrow");
                        dispatch.native_handled = true;
                        break;
                    }
                    cancel_project_blueprint_drag(doc, state);
                }
                if (state.project_blueprint_drag.active()
                    && state.project_blueprint_drag.threshold_crossed) {
                    dispatch.native_handled = true;
                    break;
                }
                if (state.area_object_placement.active()
                    && event.button.button == SDL_BUTTON_LEFT) {
                    if (state.area_object_placement.region_drawing) {
                        dispatch.native_handled = true;
                        break;
                    }
                    const bool placement_dragged = state.area_object_placement.threshold_crossed;
                    if (placement_dragged) {
                        const auto point = to_context_point(window, event.button.x, event.button.y);
                        const auto viewport = active_workspace_viewer_viewport_request(
                            doc, state, frame_width, frame_height);
                        update_area_object_placement(renderer, state, point, viewport);
                        if (region_blueprint_resource(
                                state.area_object_placement.resource)) {
                            auto& placement = state.area_object_placement;
                            if (placement.phase
                                    == AreaObjectPlacementPhase::ghost_valid
                                && placement.region_hover) {
                                placement.region_points.push_back(
                                    *placement.region_hover);
                                placement.region_hover.reset();
                                placement.region_drawing = true;
                                placement.region_closing_valid = false;
                                placement.diagnostic.clear();
                                renderer.update_viewer_area_region_preview(
                                    placement.region_points,
                                    std::nullopt,
                                    false);
                                state.browser.pressed_recent_index = -1;
                                system_interface.SetMouseCursor("cross");
                            } else {
                                cancel_area_object_placement(renderer, state);
                                system_interface.SetMouseCursor("arrow");
                            }
                            dispatch.native_handled = true;
                            break;
                        }
                        commit_area_object_placement(renderer, state);
                        state.browser.pressed_recent_index = -1;
                        system_interface.SetMouseCursor("arrow");
                        dispatch.native_handled = true;
                        break;
                    }
                    state.area_object_placement = {};
                }
                if (state.area_object_placement.active()
                    && state.area_object_placement.threshold_crossed) {
                    dispatch.native_handled = true;
                    break;
                }
                if (state.area_object_drag.active && event.button.button == SDL_BUTTON_LEFT) {
                    const auto point = to_context_point(window, event.button.x, event.button.y);
                    if (const auto viewport = active_workspace_viewer_viewport_request(
                            doc, state, frame_width, frame_height)) {
                        update_area_object_drag(renderer, state, point, *viewport);
                        commit_area_object_drag(renderer, state);
                    } else {
                        cancel_area_object_drag(renderer, state);
                    }
                    system_interface.SetMouseCursor(
                        point_within_element(doc, "workspace_tabs", point) ? "pointer" : "arrow");
                    dispatch.native_handled = true;
                    break;
                }
                if (state.viewer_viewport_dragging
                    && (event.button.button == SDL_BUTTON_LEFT
                        || event.button.button == SDL_BUTTON_MIDDLE
                        || event.button.button == SDL_BUTTON_RIGHT)) {
                    state.viewer_viewport_dragging = false;
                    const auto point = to_context_point(window, event.button.x, event.button.y);
                    system_interface.SetMouseCursor(point_within_element(doc, "workspace_tabs", point) ? "pointer" : "arrow");
                    dispatch.native_handled = true;
                    break;
                }
                if (event.button.button == SDL_BUTTON_LEFT) {
                    if (end_bottom_dock_resize(state)) {
                        dispatch.native_handled = true;
                        break;
                    }
                    if (end_left_dock_resize(state)) {
                        dispatch.native_handled = true;
                        break;
                    }

                    const auto point = to_context_point(window, event.button.x, event.button.y);
                    const bool workspace_tab_was_dragging = state.workspace_view.workspace_tab_dragging;
                    if (!state.workspace_view.workspace_tab_drag_id.empty()) {
                        clear_workspace_tab_drag(state);
                        if (workspace_tab_was_dragging) {
                            refresh_workspace_view(doc, state);
                            system_interface.SetMouseCursor(point_within_element(doc, "workspace_tabs", point) ? "pointer" : "arrow");
                            dispatch.native_handled = true;
                            break;
                        }
                    }

                    if (command_palette_contains_point(
                            palette_doc, state, point)) {
                        if (auto* hit = element_at_mouse(palette_context, window, event.button)) {
                            if (auto* command_item = find_ancestor_with_class(hit, "command_item")) {
                                const std::string command_id = command_item->GetAttribute<Rml::String>("data-key", "");
                                execute_palette_command(window, context, palette_context, doc, palette_doc, state, command_id);
                            } else {
                                (void)nw::toolset::forward_client_input(dispatch, nw::toolset::ClientRmlRecipient::command,
                                    nw::toolset::ClientRmlForwardPhase::after_native, palette_context, window, event);
                            }
                        }
                        dispatch.native_handled = true;
                        break;
                    }

                    Rml::Element* recent_hit = nullptr;
                    bool handled = false;
                    const auto release_workspace_mouse_up = [&] {
                        const auto before = client_ui_action_owner(state);
                        if (dispatch.forwarded_recipient == nw::toolset::ClientRmlRecipient::none && context) {
                            (void)nw::toolset::forward_client_input(dispatch, nw::toolset::ClientRmlRecipient::toolset,
                                nw::toolset::ClientRmlForwardPhase::before_native, context, window, event);
                        }
                        const bool unchanged = nw::toolset::same_client_ui_action_owner(before, client_ui_action_owner(state));
                        if (!unchanged) {
                            state.browser.pressed_recent_index = -1;
                            dispatch.native_handled = true;
                        }
                        return unchanged;
                    };
                    if (auto* hit = element_at_mouse(context, window, event.button)) {
                        auto* workspace_tab_scroll_button = find_ancestor_with_class(
                            hit, "workspace_tab_scroll_button");
                        auto* object_workbench_tab_scroll_button = find_ancestor_with_class(
                            hit, "object_workbench_tab_scroll_button");
                        auto* workspace_tab_close_hit = find_ancestor_with_class(hit, "workspace_tab_close");
                        if (!workspace_tab_close_hit) {
                            workspace_tab_close_hit = workspace_tab_element_at_point(doc, "workspace_tab_close", point);
                        }
                        auto* workspace_tab_hit = find_ancestor_with_class(hit, "workspace_tab");
                        if (!workspace_tab_hit) {
                            workspace_tab_hit = workspace_tab_element_at_point(doc, "workspace_tab", point);
                        }

                        if (auto* area_surface_tab
                            = find_ancestor_with_class(
                                hit, "area_workspace_tab")) {
                            const std::string surface
                                = area_surface_tab->GetAttribute<Rml::String>(
                                    "data-area-surface", "");
                            if (!release_workspace_mouse_up()) { break; }
                            if (surface == "properties") {
                                (void)set_area_workspace_surface(renderer, state,
                                    AreaWorkspaceSurface::properties);
                            } else if (surface == "objects") {
                                (void)set_area_workspace_surface(renderer, state,
                                    AreaWorkspaceSurface::objects);
                            } else if (surface == "tiles") {
                                (void)set_area_workspace_surface(renderer, state,
                                    AreaWorkspaceSurface::tiles);
                            }
                            refresh_workspace_content(doc, state);
                            sync_area_tile_palette_window(doc, state, true);
                            handled = true;
                        } else if (find_ancestor_with_id(
                                       hit, "area_tile_editor_back")) {
                            if (!release_workspace_mouse_up()) { break; }
                            auto& editor = state.area_tile_editor;
                            if (nw::toolset::leave_area_tile_palette_folder(
                                    editor.palette)) {
                                cancel_area_tile_stroke(renderer, state);
                                clear_area_tile_selection(renderer, state);
                                nw::toolset::reset_area_tile_palette_folder_view(editor);
                                refresh_workspace_content(doc, state);
                                sync_area_tile_palette_window(doc, state, true);
                            } else {
                                editor.feedback
                                    = "Tile palette navigation is unavailable";
                                sync_area_tile_palette_window(doc, state, true);
                            }
                            handled = true;
                        } else if (auto* tile_row = find_ancestor_with_class(
                                       hit, "area_tile_palette_row")) {
                            const auto before = client_ui_action_owner(state);
                            const auto row_key = nw::toolset::release_client_row_key(
                                dispatch, tile_row, context, window, event);
                            if (!nw::toolset::same_client_ui_action_owner(before, client_ui_action_owner(state))) {
                                state.browser.pressed_recent_index = -1;
                                dispatch.native_handled = true;
                                break;
                            }
                            if (row_key && *row_key >= 0
                                && static_cast<size_t>(*row_key)
                                    < state.area_tile_editor.palette.rows.size()) {
                                auto& editor = state.area_tile_editor;
                                const auto& row = editor.palette.rows[static_cast<size_t>(*row_key)];
                                if (row.kind
                                    == nw::toolset::AreaTilePaletteRowKind::folder) {
                                    if (nw::toolset::enter_area_tile_palette_folder(
                                            editor.palette,
                                            static_cast<uint32_t>(*row_key))) {
                                        cancel_area_tile_stroke(renderer, state);
                                        clear_area_tile_selection(
                                            renderer, state);
                                        nw::toolset::reset_area_tile_palette_folder_view(
                                            editor);
                                        refresh_workspace_content(doc, state);
                                        sync_area_tile_palette_window(
                                            doc, state, true);
                                    } else {
                                        editor.feedback
                                            = "Tile palette folder is unavailable";
                                        sync_area_tile_palette_window(
                                            doc, state, true);
                                    }
                                    handled = true;
                                } else {
                                    clear_area_tile_selection(renderer, state);
                                    editor.selected_row = *row_key;
                                    editor.group_orientation = 0;
                                    editor.cursor_target_index = UINT32_MAX;
                                    editor.cursor_update_pending = false;
                                    editor.preview_rows.clear();
                                    (void)renderer.update_viewer_area_tile_preview(
                                        active_workspace_area(state), {});
                                    editor.feedback.clear();
                                    const auto selected = std::find(
                                        editor.palette.matches.begin(),
                                        editor.palette.matches.end(),
                                        static_cast<uint32_t>(*row_key));
                                    if (selected
                                        != editor.palette.matches.end()) {
                                        editor.list.set_selected(
                                            static_cast<int>(std::distance(
                                                editor.palette.matches.begin(),
                                                selected)));
                                    }
                                    editor.rendered = false;
                                    sync_area_tile_palette_window(
                                        doc, state, true);
                                }
                            }
                            handled = true;
                        } else if (workspace_tab_scroll_button) {
                            const bool disabled = workspace_tab_scroll_button->IsClassSet("disabled");
                            const bool forward = workspace_tab_scroll_button->GetId() == "workspace_tabs_next";
                            if (!release_workspace_mouse_up()) { break; }
                            if (!disabled) {
                                state.workspace_view.workspace_tab_scroll_x = tab_scroll_target(
                                    doc, kWorkspaceTabScrollStrip, forward);
                                apply_workspace_tab_scroll(doc, state);
                            }
                            handled = true;
                        } else if (object_workbench_tab_scroll_button) {
                            const bool disabled = object_workbench_tab_scroll_button->IsClassSet("disabled");
                            const bool forward = object_workbench_tab_scroll_button->GetId() == "object_workbench_tabs_next";
                            if (!release_workspace_mouse_up()) { break; }
                            if (!disabled) {
                                state.workbench.object_workbench_tab_scroll_x = tab_scroll_target(
                                    doc, kObjectWorkbenchTabScrollStrip, forward);
                                apply_object_workbench_tab_scroll(doc, state);
                            }
                            handled = true;
                        } else if (workspace_tab_close_hit) {
                            const std::string tab_id = workspace_tab_close_hit->GetAttribute<Rml::String>("data-tab", "");
                            if (!tab_id.empty() && ensure_backend_ready(state)) {
                                if (!release_workspace_mouse_up()) { break; }
                                const auto result = dispatch_command_flow(window,
                                    state,
                                    "workspace.close_tab",
                                    {std::string_view{tab_id}},
                                    nw::toolset::CommandSource::widget);
                                if (result.ok()) {
                                    if (!remove_workspace_tab_element(doc, state, tab_id)) {
                                        refresh_workspace_view(doc, state);
                                    } else {
                                        refresh_workspace_content(doc, state);
                                    }
                                    state.workspace_hover_refresh_pending = true;
                                    state.workspace_hover_refresh_point = point;
                                }
                            }
                            handled = true;
                        } else if (workspace_tab_hit) {
                            const std::string tab_id = workspace_tab_hit->GetAttribute<Rml::String>("data-tab", "");
                            if (!tab_id.empty() && ensure_backend_ready(state)) {
                                if (!release_workspace_mouse_up()) { break; }
                                const auto result = dispatch_command(state,
                                    "workspace.activate_tab",
                                    {std::string_view{tab_id}},
                                    nw::toolset::CommandSource::widget);
                                append_command_result(state, result);
                                if (result.ok()) {
                                    if (!sync_workspace_tab_elements(doc, state)) {
                                        refresh_workspace_view(doc, state);
                                    } else {
                                        refresh_workspace_content(doc, state);
                                    }
                                    state.workspace_hover_refresh_pending = true;
                                    state.workspace_hover_refresh_point = point;
                                }
                            }
                            handled = true;
                        } else if (auto* workspace_subtab_close = find_ancestor_with_class(hit, "workspace_subtab_close")) {
                            const std::string tab_id = workspace_subtab_close->GetAttribute<Rml::String>("data-tab", "");
                            const std::string subtab_id = workspace_subtab_close->GetAttribute<Rml::String>("data-subtab", "");
                            if (!tab_id.empty() && !subtab_id.empty() && ensure_backend_ready(state)) {
                                if (!release_workspace_mouse_up()) { break; }
                                const auto result = dispatch_command(state,
                                    "workspace.close_subtab",
                                    {std::string_view{tab_id}, std::string_view{subtab_id}},
                                    nw::toolset::CommandSource::widget);
                                append_command_result(state, result);
                                if (result.ok()) {
                                    refresh_workspace_content(doc, state);
                                    state.workspace_hover_refresh_pending = true;
                                    state.workspace_hover_refresh_point = point;
                                }
                            }
                            handled = true;
                        } else if (auto* workspace_subtab = find_ancestor_with_class(hit, "workspace_subtab")) {
                            const std::string tab_id = workspace_subtab->GetAttribute<Rml::String>("data-tab", "");
                            const std::string subtab_id = workspace_subtab->GetAttribute<Rml::String>("data-subtab", "");
                            if (!tab_id.empty() && !subtab_id.empty() && ensure_backend_ready(state)) {
                                if (!release_workspace_mouse_up()) { break; }
                                const auto result = dispatch_command(state,
                                    "workspace.activate_subtab",
                                    {std::string_view{tab_id}, std::string_view{subtab_id}},
                                    nw::toolset::CommandSource::widget);
                                append_command_result(state, result);
                                if (result.ok()) {
                                    refresh_workspace_content(doc, state);
                                    state.workspace_hover_refresh_pending = true;
                                    state.workspace_hover_refresh_point = point;
                                }
                            }
                            handled = true;
                        } else if (auto* dialog_row = find_ancestor_with_class(hit, "dialog_row")) {
                            const auto row_index = parse_decimal_int32(
                                dialog_row->GetAttribute<Rml::String>("data-key", ""));
                            const auto* active_tab = state.workspace.active_tab();
                            if (row_index && *row_index >= 0
                                && active_tab
                                && active_tab->kind == nw::toolset::WorkspaceTabKind::dialog
                                && nw::toolset::select_dialog_view_row(state.dialog_view, *row_index)) {
                                if (!release_workspace_mouse_up()) { break; }
                                nw::toolset::sync_dialog_view(doc, state.dialog_view, true);
                            }
                            handled = true;
                        } else if (find_ancestor_with_id(hit, "creature_color_selector_close")) {
                            if (!release_workspace_mouse_up()) { break; }
                            clear_color_editor(state);
                            refresh_workspace_content(doc, state);
                            handled = true;
                        } else if (auto* color_channel = find_ancestor_with_class(hit, "creature_color_channel")) {
                            const auto color = parse_decimal_int32(
                                color_channel->GetAttribute<Rml::String>("data-color", ""));
                            if (color && *color >= 0
                                && open_color_editor(state, state.workbench.object_details.object,
                                    static_cast<uint32_t>(*color))) {
                                if (!release_workspace_mouse_up()) { break; }
                                refresh_workspace_content(doc, state);
                            }
                            handled = true;
                        } else if (auto* palette = find_ancestor_with_id(hit, "creature_color_palette")) {
                            const float left = palette->GetAbsoluteLeft() + palette->GetClientLeft();
                            const float top = palette->GetAbsoluteTop() + palette->GetClientTop();
                            const float palette_width = palette->GetClientWidth();
                            const float palette_height = palette->GetClientHeight();
                            const float local_x = point.x - left;
                            const float local_y = point.y - top;
                            if (palette_width > 0.0f && palette_height > 0.0f
                                && local_x >= 0.0f && local_x < palette_width
                                && local_y >= 0.0f && local_y < palette_height) {
                                const int column = std::min(kPltPaletteColumns - 1,
                                    static_cast<int>(local_x * kPltPaletteColumns / palette_width));
                                const int row = std::min(kPltPaletteRows - 1,
                                    static_cast<int>(local_y * kPltPaletteRows / palette_height));
                                if (!release_workspace_mouse_up()) { break; }
                                if (commit_active_color_selection(
                                        state, row * kPltPaletteColumns + column)) {
                                    refresh_workspace_content(doc, state);
                                }
                            }
                            handled = true;
                        } else if (auto* color_field = find_ancestor_with_class(hit, "creature_color_field")) {
                            const auto color = parse_decimal_int32(
                                color_field->GetAttribute<Rml::String>("data-color", ""));
                            if (color && *color >= 0
                                && active_appearances_match_tab(state)) {
                                if (!release_workspace_mouse_up()) { break; }
                                (void)close_active_smalls_selector(doc);
                                close_appearance_selector(state);
                                (void)open_color_editor(state,
                                    state.workbench.object_details.object,
                                    static_cast<uint32_t>(*color));
                                refresh_workspace_content(doc, state);
                            }
                            handled = true;
                        } else if (find_ancestor_with_id(hit, "creature_color_selector")) {
                            if (!release_workspace_mouse_up()) { break; }
                            handled = true;
                        } else if (auto* option = find_ancestor_with_class(hit, "combobox_option")) {
                            const std::string key = option->GetAttribute<Rml::String>("data-key", "");
                            const auto value = parse_decimal_int32(key);
                            if (value
                                && state.workbench.object_details_combobox.is_active()
                                && find_ancestor_with_id(option,
                                    "object_details_combobox_popup")) {
                                if (!release_workspace_mouse_up()) { break; }
                                if (commit_object_details_sound_position(
                                        doc, state, *value)) {
                                    refresh_workspace_content(doc, state);
                                }
                            } else if (value
                                && active_creature_spell_filter_matches_tab(state)) {
                                if (!release_workspace_mouse_up()) { break; }
                                if (commit_creature_spell_filter(state, *value)) {
                                    refresh_workspace_content(doc, state);
                                    sync_creature_spell_window(doc, state, true);
                                }
                            }
                            handled = true;
                        } else if (auto* sound_position_field
                            = find_ancestor_with_class(hit,
                                "object_details_sound_position_field")) {
                            const auto row_index = parse_decimal_int32(
                                sound_position_field->GetAttribute<Rml::String>(
                                    "data-row", ""));
                            if (row_index && *row_index >= 0) {
                                const std::string field_id = sound_position_field->GetId();
                                if (!release_workspace_mouse_up()) { break; }
                                if (open_object_details_sound_position_combobox(
                                        doc, state,
                                        static_cast<uint32_t>(*row_index))) {
                                    (void)sync_object_details_combobox(
                                        doc, state, true);
                                    if (auto* field = find_el(doc, field_id.c_str()); field
                                        && parse_decimal_int32(field->GetAttribute<Rml::String>("data-row", "")) == row_index) {
                                        field->Focus();
                                    }
                                }
                            }
                            handled = true;
                        } else if (auto* spell_filter_field = find_ancestor_with_class(hit, "creature_spell_filter_field")) {
                            const auto filter = creature_spell_filter_field_from_name(
                                spell_filter_field->GetAttribute<Rml::String>("data-filter", ""));
                            if (filter && active_creature_spells_match_tab(state)) {
                                if (!release_workspace_mouse_up()) { break; }
                                if (state.workbench.creature_view.creature_spell_filter_field == *filter
                                    && state.workbench.creature_view.creature_spell_combobox.is_active()) {
                                    if (state.workbench.creature_view.creature_spell_combobox.popup_visible()) {
                                        state.workbench.creature_view.creature_spell_combobox.hide_popup();
                                    } else {
                                        (void)state.workbench.creature_view.creature_spell_combobox.show_popup();
                                    }
                                } else {
                                    (void)open_creature_spell_filter(state, *filter);
                                }
                                refresh_workspace_content(doc, state);
                                sync_creature_spell_filter_window(doc, state, true);
                                if (auto* active_field = find_el(
                                        doc, "active_creature_spell_filter_field")) {
                                    active_field->Focus();
                                }
                            }
                            handled = true;
                        } else if (auto* object_row = find_ancestor_with_class(hit, "area_object_row")) {
                            const auto packed = parse_decimal_uint64(
                                object_row->GetAttribute<Rml::String>("data-object", ""));
                            if (packed) {
                                const auto object = nw::ObjectHandle::from_ull(*packed);
                                if (nw::kernel::objects().valid(object)) {
                                    if (!release_workspace_mouse_up()) { break; }
                                    if (renderer.set_viewer_area_object_selection(object)) {
                                        (void)renderer.focus_viewer_area_object_selection();
                                    }
                                }
                            }
                            handled = true;
                        } else if (find_ancestor_with_class(hit, "area_object_list_back")) {
                            if (!release_workspace_mouse_up()) { break; }
                            (void)renderer.clear_viewer_area_object_selection();
                            handled = true;
                        } else if (auto click = nw::toolset::capture_object_workbench_command_click(
                                       hit, state.workbench, state.workspace, state.backend.module_generation())) {
                            if (click->release_phase == nw::toolset::ClientRmlForwardPhase::before_native
                                && !release_workspace_mouse_up()) { break; }
                            (void)nw::toolset::execute_object_workbench_command_click(*click,
                                state.workbench, state.workspace, state.backend, state.shell,
                                command_context(state, nw::toolset::CommandSource::widget));
                            if (click->release_phase == nw::toolset::ClientRmlForwardPhase::after_native
                                && !release_workspace_mouse_up()) { break; }
                            handled = true;
                        } else if (auto sound_click = nw::toolset::capture_sound_resource_click(
                                       hit, state.workbench.appearance_view,
                                       nw::toolset::object_workbench_target(state.workbench, state.workspace),
                                       state.workspace, state.backend.module_generation(), nw::kernel::resman().generation())) {
                            if (!release_workspace_mouse_up()) { break; }
                            if (sound_click->kind == nw::toolset::SoundResourceClickKind::open) {
                                (void)close_active_smalls_selector(doc);
                            }
                            const auto effect = nw::toolset::apply_sound_resource_click(*sound_click,
                                state.workbench.appearance_view,
                                nw::toolset::object_workbench_target(state.workbench, state.workspace),
                                state.workspace, state.backend, state.shell,
                                command_context(state, nw::toolset::CommandSource::widget));
                            if (effect != nw::toolset::SoundResourceClickEffect::none) {
                                refresh_workspace_content(doc, state);
                            }
                            if (effect == nw::toolset::SoundResourceClickEffect::opened) {
                                sync_sound_catalog_window(doc, state, true);
                                if (auto* input = find_el(doc, "sound_catalog_search")) { input->Focus(); }
                            }
                            handled = true;
                        } else if (find_ancestor_with_id(hit, "appearance_selector_back")) {
                            if (!release_workspace_mouse_up()) { break; }
                            close_appearance_selector(state);
                            rebuild_active_appearances(state, state.workbench.object_details.object);
                            refresh_workspace_content(doc, state);
                            handled = true;
                        } else if (auto surface_click = nw::toolset::capture_object_workbench_surface_click(hit)) {
                            if (!release_workspace_mouse_up()) { break; }
                            (void)nw::toolset::apply_object_workbench_surface_click(
                                *surface_click, state.workbench, doc, state.backend);
                            if (!sync_appearance_body_preview(renderer, state)) {
                                append_output(state, "error", "Failed to update the creature Appearance preview");
                            }
                            refresh_workspace_content(doc, state);
                            sync_object_details_window(doc, state, true);
                            sync_creature_feat_window(doc, state, true);
                            sync_creature_spell_window(doc, state, true);
                            sync_creature_inventory_window(doc, state, true);
                            sync_appearance_window(doc, state, true);
                            nw::toolset::sync_managed_lists(doc,
                                nw::toolset::ui_v1_host(), state.workbench.managed_lists, true);
                            handled = true;
                        } else if (find_ancestor_with_id(
                                       hit, "placeable_appearance_previous")) {
                            if (!release_workspace_mouse_up()) { break; }
                            if (cycle_active_appearance(state, -1)) {
                                rebuild_active_appearances(
                                    state, state.workbench.object_details.object);
                                refresh_workspace_content(doc, state);
                            }
                            handled = true;
                        } else if (find_ancestor_with_id(
                                       hit, "placeable_appearance_next")) {
                            if (!release_workspace_mouse_up()) { break; }
                            if (cycle_active_appearance(state, 1)) {
                                rebuild_active_appearances(
                                    state, state.workbench.object_details.object);
                                refresh_workspace_content(doc, state);
                            }
                            handled = true;
                        } else if (find_ancestor_with_id(
                                       hit, "door_appearance_previous")) {
                            if (!release_workspace_mouse_up()) { break; }
                            if (cycle_active_appearance(state, -1)) {
                                rebuild_active_appearances(
                                    state, state.workbench.object_details.object);
                                refresh_workspace_content(doc, state);
                            }
                            handled = true;
                        } else if (find_ancestor_with_id(
                                       hit, "door_appearance_next")) {
                            if (!release_workspace_mouse_up()) { break; }
                            if (cycle_active_appearance(state, 1)) {
                                rebuild_active_appearances(
                                    state, state.workbench.object_details.object);
                                refresh_workspace_content(doc, state);
                            }
                            handled = true;
                        } else if (auto* catalog_field = find_ancestor_with_class(hit, "appearance_catalog_field")) {
                            const auto selected_field = appearance_editor_field_from_name(
                                catalog_field->GetAttribute<Rml::String>("data-field", ""));
                            if (selected_field && active_appearances_match_tab(state)
                                && (*selected_field == AppearanceEditorField::appearance
                                    || state.workbench.object_details.object.type == nw::ObjectType::creature)) {
                                if (!release_workspace_mouse_up()) { break; }
                                (void)close_active_smalls_selector(doc);
                                clear_color_editor(state);
                                state.workbench.appearance_view.appearance_editor_field = *selected_field;
                                state.workbench.appearance_view.appearance_selector_open = true;
                                state.workbench.appearance_view.appearance_query.clear();
                                rebuild_active_appearances(state, state.workbench.object_details.object);
                                state.workbench.appearance_view.appearance_scroll_to_selection = true;
                                refresh_workspace_content(doc, state);
                                sync_appearance_window(doc, state, true);
                                if (auto* input = find_el(doc, "appearance_search")) {
                                    input->Focus();
                                }
                            }
                            handled = true;
                        } else if (auto* appearance_row = find_ancestor_with_class(hit, "appearance_row")) {
                            const std::string id_text = appearance_row->GetAttribute<Rml::String>("data-key", "");
                            const auto id = parse_decimal_int32(id_text);
                            if (id && *id >= 0 && active_appearances_match_tab(state)) {
                                if (!release_workspace_mouse_up()) { break; }
                                if (commit_active_appearance_selection(state, *id)) {
                                    close_appearance_selector(state);
                                    rebuild_active_appearances(state, state.workbench.object_details.object);
                                    refresh_workspace_content(doc, state);
                                }
                            }
                            handled = true;
                        } else if (auto inventory_click = nw::toolset::capture_inventory_workbench_click(
                                       hit, state.workbench.inventory_view,
                                       nw::toolset::object_workbench_target(state.workbench, state.workspace),
                                       state.workspace, state.backend.module_generation())) {
                            if (inventory_click->release_phase == nw::toolset::ClientRmlForwardPhase::before_native
                                && !release_workspace_mouse_up()) { break; }
                            if (nw::toolset::apply_inventory_workbench_click(
                                    *inventory_click, state.workbench.inventory_view,
                                    nw::toolset::object_workbench_target(state.workbench, state.workspace),
                                    state.workspace, state.backend, state.shell,
                                    command_context(state, nw::toolset::CommandSource::widget))) {
                                sync_creature_inventory_window(doc, state, true);
                            }
                            handled = true;
                        } else if (auto creature_click = nw::toolset::capture_creature_workbench_command_click(
                                       hit, state.workbench.creature_view,
                                       nw::toolset::object_workbench_target(state.workbench, state.workspace),
                                       state.workspace, state.backend.module_generation())) {
                            if (creature_click->release_phase == nw::toolset::ClientRmlForwardPhase::before_native
                                && !release_workspace_mouse_up()) { break; }
                            (void)nw::toolset::execute_creature_workbench_command_click(
                                *creature_click, state.workbench.creature_view,
                                nw::toolset::object_workbench_target(state.workbench, state.workspace),
                                state.workspace, state.backend, state.shell,
                                command_context(state, nw::toolset::CommandSource::widget));
                            handled = true;
                        } else if (const auto activation = activate_managed_list(
                                       doc, state, hit);
                            activation.activated) {
                            if (!release_workspace_mouse_up()) { break; }
                            if (activation.focus_target) {
                                (void)nw::toolset::focus_managed_list_target(
                                    doc, *activation.focus_target);
                            }
                            handled = true;
                        } else if (auto* area_card = find_ancestor_with_class(hit, "home_area_card")) {
                            const auto index = parse_decimal_int32(
                                area_card->GetAttribute<Rml::String>("data-key", ""));
                            if (index && *index >= 0
                                && static_cast<size_t>(*index) < state.browser.home_areas.size()
                                && ensure_backend_ready(state)) {
                                const std::string resref = state.browser.home_areas[static_cast<size_t>(*index)].resref;
                                if (!release_workspace_mouse_up()) { break; }
                                const auto result = dispatch_command_flow(window, state,
                                    "toolset.select_area",
                                    {std::string_view{resref}},
                                    nw::toolset::CommandSource::widget);
                                if (result.ok()) {
                                    refresh_workspace_view(doc, state);
                                    if (result.status == nw::toolset::CommandStatus::success) {
                                        focus_workspace_viewport(doc, state);
                                    }
                                }
                            }
                            handled = true;
                        } else if (auto* remove = find_ancestor_with_class(hit, "home_project_remove")) {
                            const auto index = parse_decimal_int32(remove->GetAttribute<Rml::String>("data-key", ""));
                            if (index && *index >= 0 && static_cast<size_t>(*index) < state.browser.recent_projects.size()) {
                                if (!release_workspace_mouse_up()) { break; }
                                auto previous = state.browser.recent_projects;
                                const std::array indices{static_cast<size_t>(*index)};
                                if (nw::toolset::forget_recent_projects(state.browser.recent_projects, indices)
                                    && !save_ui_preferences(state.shell_view.preferences_path, state.shell.docks, state.browser.recent_projects)) {
                                    state.browser.recent_projects = std::move(previous);
                                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Unable to remove recent project",
                                        "Could not save preferences. The project was kept in the recent list.", window);
                                }
                                refresh_workspace_content(doc, state);
                            }
                            handled = true;
                        } else if (auto* project_item = find_ancestor_with_class(hit, "home_project_item")) {
                            const auto index = parse_decimal_int32(project_item->GetAttribute<Rml::String>("data-key", ""));
                            if (index && *index >= 0 && static_cast<size_t>(*index) < state.browser.recent_projects.size()) {
                                nw::toolset::refresh_recent_projects(state.browser.recent_projects);
                                const auto project = state.browser.recent_projects[static_cast<size_t>(*index)];
                                if (!release_workspace_mouse_up()) { break; }
                                if (!project.error.empty()) {
                                    const auto message = project.error + ":\n" + project.path;
                                    append_output(state, "error", message);
                                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Unable to open project", message.c_str(), window);
                                } else if (ensure_backend_ready(state)) {
                                    if (!queue_project_open(state,
                                            project.path,
                                            nw::toolset::CommandSource::widget)) {
                                        append_output(state, "warn",
                                            "A project is already opening");
                                    }
                                } else {
                                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Unable to open project", "Backend initialization failed.", window);
                                }
                                refresh_workspace_view(doc, state);
                            }
                            handled = true;
                        } else if (auto* dock_tab = find_ancestor_with_class(hit, "dock_tab")) {
                            const std::string widget = dock_tab->GetAttribute<Rml::String>("data-widget", "");
                            if (!widget.empty()) {
                                append_command_result(state,
                                    dispatch_command(state,
                                        "rollnw.client.dock.activate",
                                        {"bottom", std::string_view{widget}},
                                        nw::toolset::CommandSource::widget));
                                sync_shell_visibility(context, palette_context, doc, palette_doc, state);
                            }
                            handled = true;
                        } else if (auto* output_toggle = find_ancestor_with_class(hit, "output_toggle")) {
                            const std::string id = output_toggle->GetId();
                            std::string_view channel;
                            if (id == "output_info") {
                                channel = "info";
                            } else if (id == "output_warn") {
                                channel = "warn";
                            } else if (id == "output_error") {
                                channel = "error";
                            } else if (id == "output_script") {
                                channel = "script";
                            }
                            if (!channel.empty()) {
                                append_command_result(state, dispatch_command(state, "rollnw.client.output.channel", {channel}, nw::toolset::CommandSource::widget));
                                handled = true;
                            }
                        } else {
                            recent_hit = find_ancestor_with_class(hit, "recent_item");
                        }
                    }

                    if (!handled) {
                        auto* top_hit = context ? context->GetElementAtPoint(point) : nullptr;
                        if (!recent_hit && !recent_list_hit_blocked(doc, top_hit, point, state)) {
                            recent_hit = recent_item_at_point(doc, point);
                        }

                        if (recent_hit) {
                            auto* recent_item = recent_hit;
                            const std::string index_text = recent_item->GetAttribute<Rml::String>("data-key", "");
                            if (!index_text.empty()) {
                                const int clicked_index = static_cast<int>(std::strtol(index_text.c_str(), nullptr, 10));
                                if (clicked_index >= 0 && clicked_index == state.browser.pressed_recent_index) {
                                    set_recent_selected(doc, state, clicked_index);

                                    const size_t idx = static_cast<size_t>(clicked_index);
                                    if (state.shell.showing_project_tree) {
                                        if (idx < state.browser.project_rows.size()) {
                                            const auto& row = state.browser.project_rows[idx].node;
                                            if (row.is_container()) {
                                                if (state.browser.collapsed_project_nodes.erase(row.id) == 0) {
                                                    state.browser.collapsed_project_nodes.insert(row.id);
                                                }
                                                state.browser.selected_recent_index = -1;
                                                refresh_recent_list(doc, state);
                                            } else if (state.play_preview.selecting_actor) {
                                                const auto resource = nw::Resource::from_path(
                                                    row.relative_path, false);
                                                if (resource.type != nw::ResourceType::utc) {
                                                    append_output(state, "warn",
                                                        "Choose a Creature blueprint for play preview");
                                                } else {
                                                    const auto saved
                                                        = nw::toolset::save_project_preview_test_actor(
                                                            state.backend.current_project_dir(),
                                                            row.relative_path);
                                                    append_output(state,
                                                        saved.ok ? "info" : "error",
                                                        saved.message);
                                                    if (saved.ok) {
                                                        const auto actor_path = row.relative_path;
                                                        if (!release_workspace_mouse_up()) { break; }
                                                        (void)prepare_play_preview(renderer,
                                                            system_interface, doc, state,
                                                            actor_path);
                                                    } else {
                                                        state.shell.set_output_panel_visible(true);
                                                        refresh_bottom_dock_view(doc, state);
                                                    }
                                                }
                                            } else if (ensure_backend_ready(state)) {
                                                const std::string relative_path = row.relative_path.generic_string();
                                                if (!release_workspace_mouse_up()) { break; }
                                                const auto result = dispatch_command_flow(window, state,
                                                    "toolset.open_resource",
                                                    {std::string_view{relative_path}},
                                                    nw::toolset::CommandSource::widget);
                                                if (result.ok()) {
                                                    refresh_workspace_view(doc, state);
                                                    const auto* tab = state.workspace.active_tab();
                                                    if (result.status == nw::toolset::CommandStatus::success
                                                        && tab && tab->kind == nw::toolset::WorkspaceTabKind::area) {
                                                        focus_workspace_viewport(doc, state);
                                                    }
                                                }
                                            }
                                        }
                                    } else if (state.shell.showing_areas) {
                                        const std::string resref = recent_item->GetAttribute<Rml::String>("data-resref", "");
                                        if (!resref.empty()) {
                                            if (!release_workspace_mouse_up()) { break; }
                                            const auto result = dispatch_command_flow(window, state,
                                                "toolset.select_area",
                                                {std::string_view{resref}},
                                                nw::toolset::CommandSource::widget);
                                            if (result.ok()) {
                                                refresh_workspace_view(doc, state);
                                                if (result.status == nw::toolset::CommandStatus::success) {
                                                    focus_workspace_viewport(doc, state);
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    state.browser.pressed_recent_index = -1;
                    if (handled) {
                        dispatch.native_handled = true;
                    }
                }
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
            case SDL_EVENT_WINDOW_MOUSE_LEAVE:
                if (state.project_blueprint_drag.active()) {
                    cancel_project_blueprint_drag(doc, state);
                    state.browser.pressed_recent_index = -1;
                    system_interface.SetMouseCursor("arrow");
                }
                if (state.area_object_placement.active()) {
                    cancel_area_object_placement(renderer, state);
                    state.browser.pressed_recent_index = -1;
                    system_interface.SetMouseCursor("arrow");
                }
                state.viewer_viewport_dragging = false;
                state.shell_view.output_selection.dragging = false;
                hide_object_variable_warning_tooltip(doc, state);
                set_recent_hover(doc, state, -1);
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
                const auto pixels = query_window_pixels(window);
                frame_width = pixels.first;
                frame_height = pixels.second;
                state.workspace_view.workspace_tab_scroll_pending = true;
                state.workbench.object_workbench_tab_scroll_pending = true;
                log_window_metrics(window, "pixel-size-changed");
            } break;
            case SDL_EVENT_WINDOW_RESIZED: {
                const auto window_size = query_window_size(window);
                width = window_size.first;
                height = window_size.second;
                log_window_metrics(window, "resized");
                renderer.on_resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height), context);
                fps_context->SetDimensions(Rml::Vector2i(frame_width, frame_height));
                palette_context->SetDimensions(Rml::Vector2i(frame_width, frame_height));
                apply_bottom_dock_height(doc, state, window, state.shell.docks.pane(nw::toolset::DockRegion::bottom).size_px);
                apply_left_dock_width(doc, state, window, state.shell.docks.pane(nw::toolset::DockRegion::left).size_px);
                state.workspace_view.workspace_tab_scroll_pending = true;
                state.workbench.object_workbench_tab_scroll_pending = true;
                break;
            }
            default:
                break;
            }
            if (nw::toolset::client_input_forwarding_pending(dispatch)) {
                const auto route = nw::toolset::resolve_client_forward_route(event, window, palette_doc,
                    nw::toolset::client_input_map(nw::toolset::ClientControlRole::editor,
                        state.play_preview.session.active() || state.play_preview.placement_pending()));
                const bool targets_palette = route.rml == nw::toolset::ClientRmlRecipient::command;
                (void)nw::toolset::forward_client_input(dispatch,
                    route.rml, route.phase,
                    targets_palette ? palette_context : context, window, event);
                const bool sound_volume_gesture_ended = !targets_palette
                    && ((event.type == SDL_EVENT_MOUSE_BUTTON_UP
                            && event.button.button == SDL_BUTTON_LEFT)
                        || event.type == SDL_EVENT_KEY_DOWN);
                if (sound_volume_gesture_ended) {
                    (void)object_workbench_change_listener.commit_sound_volume();
                }
                if (!targets_palette && state.shell.output_panel_visible()
                    && output_scroll_input(doc, context, window, event)) {
                    observe_output_scroll(doc, state);
                }
            }
        }
        clear_inactive_object(state);
        if (state.workbench.appearance_view.appearance_body_preview_object.type != nw::ObjectType::invalid
            && state.workbench.active_object_tab_id.empty()
            && !sync_appearance_body_preview(renderer, state)) {
            append_output(state, "error", "Failed to restore the creature Appearance preview");
        }

        const auto mutation = nw::toolset::object_mutation_state();
        if (mutation.epoch != state.observed_object_mutation_epoch) {
            const auto mutation_focus_target = nw::toolset::managed_list_focus_target(
                context ? context->GetFocusElement() : nullptr);
            state.observed_object_mutation_epoch = mutation.epoch;
            refresh_workspace_tabs(doc, state);
            const bool area_structure_changed = mutation.area_structure_epoch != state.observed_area_structure_epoch;
            const auto* displayed_tab = state.workspace.active_tab();
            const bool changed_area_visible = displayed_tab && displayed_tab->kind == nw::toolset::WorkspaceTabKind::area
                && displayed_tab->document.object() == mutation.area;
            if (area_structure_changed && changed_area_visible) {
                cancel_area_object_placement(renderer, state);
                cancel_area_object_drag(renderer, state);
                cancel_area_tile_stroke(renderer, state);
                const nw::ObjectHandle selected
                    = state.area_workspace_surface
                        == AreaWorkspaceSurface::objects
                    ? mutation.object
                    : nw::ObjectHandle{};
                const bool rebuilt = renderer.rebuild_live_viewer_area(
                    mutation.area, selected);
                if (!rebuilt) {
                    state.stale_area_viewport = mutation.area;
                    append_output(state, "error", "Failed to rebuild the live area viewport after structural edit");
                } else {
                    state.observed_area_structure_epoch
                        = mutation.area_structure_epoch;
                    state.stale_area_viewport = nw::ObjectHandle{};
                    if (state.area_workspace_surface
                        == AreaWorkspaceSurface::tiles) {
                        if (state.area_tile_editor.selection.active()) {
                            const uint32_t source_tile_index
                                = state.area_tile_editor.selection
                                      .source_tile_index;
                            nw::toolset::AreaTileSelection selection;
                            const auto selection_result
                                = nw::toolset::build_area_tile_selection(
                                    mutation.area, source_tile_index,
                                    selection);
                            if (selection_result.ok()) {
                                state.area_tile_editor.selection
                                    = std::move(selection);
                                (void)update_area_tile_selection_preview(
                                    renderer, state);
                            } else {
                                clear_area_tile_selection(renderer, state);
                                state.area_tile_editor.feedback
                                    = "Tile selection cleared: "
                                    + selection_result.diagnostic;
                            }
                        } else if (state.area_tile_editor.cursor_target_index
                            != UINT32_MAX) {
                            state.area_tile_editor.cursor_target_index
                                = UINT32_MAX;
                            state.area_tile_editor.preview_rows.clear();
                            (void)renderer.update_viewer_area_tile_preview(
                                mutation.area, {});
                        }
                    }
                }
                state.smalls.publish_active_area(mutation.area);
                if (state.area_workspace_surface
                        == AreaWorkspaceSurface::objects
                    && mutation.object.type != nw::ObjectType::invalid) {
                    state.smalls.publish_active_object(mutation.object);
                    state.workbench.active_object_tab_id = state.workspace.active_tab_id();
                    state.managed_list_reorder = {};
                    nw::toolset::activate_object_workbench(state.workbench, mutation.object, state.workspace.active_tab_id());
                } else {
                    state.smalls.clear_active_object();
                    state.workbench.active_object_tab_id.clear();
                    clear_active_object_details(state);
                }
                refresh_workspace_content(doc, state);
                sync_object_details_window(doc, state, true);
                sync_creature_feat_window(doc, state, true);
                sync_creature_spell_window(doc, state, true);
                sync_creature_inventory_window(doc, state, true);
                sync_appearance_window(doc, state, true);
            } else {
                if (area_structure_changed
                    && state.stale_area_viewport.type
                        == nw::ObjectType::invalid) {
                    state.observed_area_structure_epoch
                        = mutation.area_structure_epoch;
                }
                const auto* active_tab = state.workspace.active_tab();
                const bool area_tab = active_tab && active_tab->kind == nw::toolset::WorkspaceTabKind::area;
                if (mutation.kind == nw::toolset::ObjectMutationKind::spatial) {
                    renderer.sync_viewer_area_object_spatial(mutation.object);
                } else if (mutation.kind == nw::toolset::ObjectMutationKind::structure
                    && mutation.object.type == nw::ObjectType::encounter
                    && !area_tab) {
                    if (!renderer.rebuild_live_viewer_object(mutation.object)) {
                        append_output(state, "error",
                            "Failed to rebuild the Encounter spawn preview after spawn-list edit");
                    }
                } else if (mutation.kind == nw::toolset::ObjectMutationKind::visual) {
                    if (state.workbench.appearance_view.appearance_body_preview_object == mutation.object
                        && !update_appearance_preview_rows(mutation.object, false)) {
                        append_output(state, "error", "Failed to refresh the creature Appearance preview");
                    }
                    bool refreshed = false;
                    if (mutation.visual_kind
                        == nw::toolset::ObjectVisualMutationKind::debug_geometry) {
                        refreshed = area_tab && renderer.rebuild_live_viewer_area(renderer.area_viewer_object(), mutation.object);
                    } else if (mutation.visual_kind == nw::toolset::ObjectVisualMutationKind::detail
                        || (mutation.visual_kind == nw::toolset::ObjectVisualMutationKind::base_appearance
                            && mutation.object.type == nw::ObjectType::creature)) {
                        refreshed = renderer.refresh_live_viewer_object_visual(mutation.object);
                    } else if (mutation.visual_kind == nw::toolset::ObjectVisualMutationKind::base_appearance) {
                        refreshed = area_tab
                            ? renderer.rebuild_live_viewer_area(
                                  renderer.area_viewer_object(), mutation.object)
                            : renderer.rebuild_live_viewer_object(mutation.object);
                    }
                    if (!refreshed) {
                        append_output(state, "error", "Failed to refresh the live object viewport after visual edit");
                    }
                }
                if (area_tab
                    && state.area_workspace_surface
                        == AreaWorkspaceSurface::objects
                    && editable_area_object(mutation.object)) {
                    renderer.set_viewer_area_object_selection(mutation.object);
                }
                if (mutation.object == state.workbench.object_details.object && active_object_details_matches_tab(state)) {
                    bool workbench_rebuilt = false;
                    (void)nw::toolset::refresh_object_workbench_snapshots(state.workbench, mutation.object);
                    const bool smalls_appearance_mutation = mutation.object.type == nw::ObjectType::door
                        || mutation.object.type == nw::ObjectType::item;
                    if (state.workbench.object_workbench_surface == ObjectWorkbenchSurface::appearance
                        && appearance_catalog_kind(mutation.object.type)
                        && mutation.object.type != nw::ObjectType::placeable) {
                        rebuild_active_appearances(state, mutation.object);
                        if (mutation.kind == nw::toolset::ObjectMutationKind::visual) {
                            refresh_workspace_content(doc, state);
                            workbench_rebuilt = true;
                        }
                    }
                    if (smalls_appearance_mutation
                        || state.workbench.object_workbench_surface == ObjectWorkbenchSurface::inventory) {
                        refresh_workspace_content(doc, state);
                        workbench_rebuilt = true;
                    }
                    if (!workbench_rebuilt) {
                        refresh_smalls_elements(doc, state);
                    }
                    sync_object_details_window(doc, state, true);
                    sync_creature_feat_window(doc, state, true);
                    sync_creature_spell_window(doc, state, true);
                    sync_creature_inventory_window(doc, state, true);
                    sync_appearance_window(doc, state, true);
                    nw::toolset::sync_managed_lists(doc,
                        nw::toolset::ui_v1_host(), state.workbench.managed_lists, true);
                }
            }
            if (mutation_focus_target
                && nw::toolset::focus_managed_list_target(
                    doc, *mutation_focus_target)) {
                state.viewer_viewport_focused = false;
            }
        }
        if (state.workbench.object_workbench_surface == ObjectWorkbenchSurface::feats) {
            const std::string feat_query = get_input_value(doc, "creature_feat_search");
            if (feat_query != state.workbench.creature_view.creature_feat_query) {
                state.workbench.creature_view.creature_feat_query = feat_query;
                if (state.workbench.object_details.object.type == nw::ObjectType::creature
                    && active_object_details_matches_tab(state)) {
                    rebuild_active_creature_feats(state, state.workbench.object_details.object);
                    state.workbench.creature_view.creature_feat_list.set_scroll_top(0);
                    sync_creature_feat_window(doc, state, true);
                }
            }
        }
        if (state.workbench.object_workbench_surface == ObjectWorkbenchSurface::spells) {
            const std::string query = get_input_value(doc, "creature_spell_search");
            if (query != state.workbench.creature_view.creature_spell_query) {
                state.workbench.creature_view.creature_spell_query = query;
                if (state.workbench.object_details.object.type == nw::ObjectType::creature
                    && active_object_details_matches_tab(state)) {
                    filter_active_creature_spells(state);
                    state.workbench.creature_view.creature_spell_list.set_scroll_top(0);
                    sync_creature_spell_window(doc, state, true);
                }
            }
        }
        if (state.workbench.object_workbench_surface == ObjectWorkbenchSurface::appearance
            && state.workbench.appearance_view.appearance_selector_open) {
            const std::string appearance_query = get_input_value(doc, "appearance_search");
            if (appearance_query != state.workbench.appearance_view.appearance_query) {
                state.workbench.appearance_view.appearance_query = appearance_query;
                if (appearance_catalog_kind(state.workbench.object_details.object.type)
                    && active_object_details_matches_tab(state)) {
                    rebuild_active_appearances(state, state.workbench.object_details.object);
                    state.workbench.appearance_view.appearance_list.set_scroll_top(0);
                    sync_appearance_window(doc, state, true);
                }
            }
        }
        if (active_sound_resource_selector_matches_tab(state)) {
            const std::string query
                = get_input_value(doc, "sound_catalog_search");
            if (query != state.workbench.appearance_view.sound_catalog_query) {
                state.workbench.appearance_view.sound_catalog_query = query;
                rebuild_sound_catalog(state, true);
                state.workbench.appearance_view.sound_catalog_list.set_scroll_top(0);
                sync_sound_catalog_window(doc, state, true);
            }
        }
        if (state.area_tile_editor.stroke.active
            && !area_tile_stroke_context_valid(state)) {
            cancel_area_tile_stroke(renderer, state);
        }
        if (state.area_workspace_surface == AreaWorkspaceSurface::tiles) {
            const std::string query
                = get_input_value(doc, "area_tile_palette_search");
            if (query != state.area_tile_editor.query) {
                auto& editor = state.area_tile_editor;
                editor.query = query;
                (void)nw::toolset::filter_area_tile_palette(
                    editor.palette, editor.query);
                editor.list.set_total_rows(
                    static_cast<int>(editor.palette.matches.size()));
                const auto selected = std::find_if(
                    editor.palette.matches.begin(),
                    editor.palette.matches.end(),
                    [&editor](uint32_t row_index) {
                        return row_index < editor.palette.rows.size()
                            && static_cast<int32_t>(row_index)
                            == editor.selected_row;
                    });
                editor.list.set_selected(
                    selected == editor.palette.matches.end()
                        ? -1
                        : static_cast<int>(std::distance(
                              editor.palette.matches.begin(), selected)));
                editor.list.set_scroll_top(0);
                editor.rendered = false;
                sync_area_tile_palette_window(doc, state, true);
            } else {
                sync_area_tile_palette_window(doc, state, false);
            }
        }

        if (workspace_home_active(state)
            && state.backend.module_object().type == nw::ObjectType::module) {
            const std::string area_query = get_input_value(doc, "home_area_search");
            if (area_query != state.browser.home_area_query) {
                state.browser.home_area_query = area_query;
                refresh_home_area_catalog(state, true);
                sync_home_area_window(doc, state, true);
            } else {
                sync_home_area_window(doc, state, false);
            }
        }

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

        const std::string output_filter = get_input_value(doc, "output_filter");
        if (output_filter != state.shell_view.last_output_filter) {
            state.shell_view.last_output_filter = output_filter;
            state.shell.output_dirty = true;
        }

        flush_log_capture(log_capture, state);

        if (state.shell.output_dirty) {
            refresh_output_view(doc, state);
            state.shell.output_dirty = false;
        }

        if (state.shell.terminal_dirty) {
            refresh_terminal_view(doc, state);
            state.shell.terminal_dirty = false;
        }

        const auto window_size = query_window_size(window);
        width = window_size.first;
        height = window_size.second;
        const auto pixel_size = query_window_pixels(window);
        frame_width = pixel_size.first;
        frame_height = pixel_size.second;

        const SDL_WindowFlags window_flags = SDL_GetWindowFlags(window);
        if ((window_flags & SDL_WINDOW_MINIMIZED) || frame_width <= 0 || frame_height <= 0) {
            SDL_Delay(50);
            continue;
        }

        uint32_t swapchain_width = static_cast<uint32_t>(frame_width);
        uint32_t swapchain_height = static_cast<uint32_t>(frame_height);
        if (!renderer.ensure_swapchain(window, swapchain_width, swapchain_height, context)) {
            frame_width = static_cast<int>(swapchain_width);
            frame_height = static_cast<int>(swapchain_height);
            const Uint64 frame_elapsed_ms = SDL_GetTicks() - frame_start_ms;
            if (frame_elapsed_ms < 16) {
                SDL_Delay(static_cast<Uint32>(16 - frame_elapsed_ms));
            }
            continue; // Wayland surface not ready yet — wait for next frame
        }
        frame_width = static_cast<int>(swapchain_width);
        frame_height = static_cast<int>(swapchain_height);
        fps_context->SetDimensions(Rml::Vector2i(frame_width, frame_height));
        palette_context->SetDimensions(Rml::Vector2i(frame_width, frame_height));
        flush_area_tile_cursor_update(
            renderer, window, context, doc, state, frame_width, frame_height);

        const Uint64 begin_frame_start_counter = SDL_GetPerformanceCounter();
        renderer.begin_frame();
        const Uint64 draw_start_counter = SDL_GetPerformanceCounter();
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
        const Uint64 ui_end_counter = SDL_GetPerformanceCounter();
        const auto viewer_viewport = active_workspace_viewer_viewport_request(doc, state, frame_width, frame_height);
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

    return 0;
}
