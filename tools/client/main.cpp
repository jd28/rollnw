
#include "appearance_catalog.hpp"
#include "area_map.hpp"
#include "dialog_view.hpp"
#include "forward_plus_debug.hpp"
#include "object_document.hpp"
#include "object_edits.hpp"
#include "project.hpp"
#include "renderer.hpp"
#include "resource_document.hpp"
#include "rml_managed_list.hpp"
#include "rml_smalls_bridge.hpp"
#include "rml_smalls_data_model.hpp"
#include "rml_smalls_language_binding.hpp"
#include "shell_controller.hpp"
#include "smalls_creature_feats.hpp"
#include "smalls_creature_inventory.hpp"
#include "smalls_creature_properties.hpp"
#include "smalls_creature_spells.hpp"
#include "toolset_backend.hpp"
#include "virtual_combobox.hpp"
#include "virtual_list.hpp"
#include "workspace.hpp"

#include "nw/log.hpp"
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/render/viewer/session.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/resources/StaticDirectory.hpp>
#include <nw/smalls/runtime.hpp>
#include <nw/util/game_install.hpp>
#include <nw/util/profile.hpp>

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi_Platform_SDL.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <nlohmann/json.hpp>

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
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#ifndef ROLLNW_CLIENT_APP_VERSION
#define ROLLNW_CLIENT_APP_VERSION "0.0.0"
#endif

#ifndef ROLLNW_CLIENT_APP_ID
#define ROLLNW_CLIENT_APP_ID "org.rollnw.client"
#endif

// ---------------------------------------------------------------------------

namespace {

constexpr int kBottomDockViewportReservePx = 96;
constexpr float kVirtualTreeRowHeightPx = 26.0f;
constexpr size_t kVirtualTreeOverscanRows = 8;
constexpr int kObjectDetailsRowHeightPx = 30;
constexpr int kObjectDetailsOverscanRows = 8;
constexpr int kObjectVariableRowHeightPx = 34;
constexpr int kObjectVariableOverscanRows = 8;
constexpr int kCreatureFeatRowHeightPx = 30;
constexpr int kCreatureFeatOverscanRows = 8;
constexpr int kCreatureSpellRowHeightPx = 30;
constexpr int kCreatureSpellOverscanRows = 8;
constexpr int kCreatureInventoryCellPx = 32;
constexpr int kAppearanceRowHeightPx = 30;
constexpr int kAppearanceOverscanRows = 4;
constexpr int kHomeAreaRowHeightPx = 190;
constexpr int kHomeAreaOverscanRows = 2;
constexpr int kHomeAreaMinimumCardWidthPx = 240;
constexpr int kHomeAreaCardGapPx = 8;
constexpr int kHomeAreaMaximumColumns = 4;
constexpr float kWorkspaceTabDragThresholdPx = 5.0f;
constexpr float kTabScrollStepPx = 48.0f;
constexpr float kWorkspaceTabAutoScrollEdgePx = 28.0f;
constexpr float kWorkspaceTabAutoScrollStepPx = 14.0f;
constexpr float kAreaObjectPlacementOpacity = 0.45f;

struct TabScrollStrip {
    // Each strip names one unique DOM singleton. An input event targets one
    // strip, so batching these records would add work without batch input.
    const char* viewport_id;
    const char* track_id;
    const char* previous_id;
    const char* next_id;
    const char* tab_class;
};

constexpr TabScrollStrip kWorkspaceTabScrollStrip{
    "workspace_tabs",
    "workspace_tab_track",
    "workspace_tabs_previous",
    "workspace_tabs_next",
    "workspace_tab",
};
constexpr TabScrollStrip kObjectWorkbenchTabScrollStrip{
    "object_workbench_tabs",
    "object_workbench_tab_track",
    "object_workbench_tabs_previous",
    "object_workbench_tabs_next",
    "object_workbench_tab",
};

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

enum class WorkspaceViewerViewportKind : uint8_t {
    area,
    preview,
};

struct WorkspaceViewerViewportRequest {
    std::filesystem::path project_dir;
    std::string resource_path;
    uint64_t module_generation = 0;
    WorkspaceViewerViewportKind kind = WorkspaceViewerViewportKind::area;
    ClientViewportRect rect;
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

std::filesystem::path client_base_path()
{
    if (const char* base_path = SDL_GetBasePath(); base_path && base_path[0] != '\0') {
        return std::filesystem::path{base_path};
    }
    std::error_code ec;
    return std::filesystem::current_path(ec);
}

void register_smalls_packages()
{
    const auto stdlib_path = client_base_path() / "stdlib";
    auto& runtime = nw::kernel::runtime();
    runtime.add_module_path(stdlib_path / "core");
    runtime.add_module_path(stdlib_path / nw::kernel::config().profile());
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

struct RmlResourceFile {
    nw::ByteArray bytes;
    size_t position = 0;
    std::FILE* fallback = nullptr;
};

class ClientRmlFileInterface final : public Rml::FileInterface {
public:
    ClientRmlFileInterface(
        nw::ResourceManager& ui_resources, nw::ResourceManager& game_resources)
        : ui_resources_(&ui_resources)
        , game_resources_(&game_resources)
    {
    }

    Rml::FileHandle Open(const Rml::String& path) override
    {
        if (auto data = demand(path); data.bytes.size()) {
            auto* file = new RmlResourceFile{};
            file->bytes = std::move(data.bytes);
            return reinterpret_cast<Rml::FileHandle>(file);
        }

        Rml::String fallback_path = path;
        constexpr std::string_view file_protocol = "file://";
        if (fallback_path.rfind(file_protocol, 0) == 0) {
            fallback_path.erase(0, file_protocol.size());
#if defined(_WIN32)
            if (fallback_path.size() >= 3 && fallback_path[0] == '/'
                && fallback_path[2] == ':') {
                fallback_path.erase(0, 1);
            }
#endif
        }
        std::replace(fallback_path.begin(), fallback_path.end(), '|', ':');
        if (auto* fallback = std::fopen(fallback_path.c_str(), "rb")) {
            auto* file = new RmlResourceFile{};
            file->fallback = fallback;
            return reinterpret_cast<Rml::FileHandle>(file);
        }

        return {};
    }

    void Close(Rml::FileHandle handle) override
    {
        auto* file = reinterpret_cast<RmlResourceFile*>(handle);
        if (!file) {
            return;
        }
        if (file->fallback) {
            std::fclose(file->fallback);
        }
        delete file;
    }

    size_t Read(void* buffer, size_t size, Rml::FileHandle handle) override
    {
        auto* file = reinterpret_cast<RmlResourceFile*>(handle);
        if (!file || !buffer || size == 0) {
            return 0;
        }
        if (file->fallback) {
            return std::fread(buffer, 1, size, file->fallback);
        }

        const size_t available = file->position < file->bytes.size() ? file->bytes.size() - file->position : 0;
        const size_t to_read = std::min(size, available);
        if (to_read > 0) {
            std::memcpy(buffer, file->bytes.data() + file->position, to_read);
            file->position += to_read;
        }
        return to_read;
    }

    bool Seek(Rml::FileHandle handle, long offset, int origin) override
    {
        auto* file = reinterpret_cast<RmlResourceFile*>(handle);
        if (!file) {
            return false;
        }
        if (file->fallback) {
            return std::fseek(file->fallback, offset, origin) == 0;
        }

        long base = 0;
        if (origin == SEEK_SET) {
            base = 0;
        } else if (origin == SEEK_CUR) {
            base = static_cast<long>(file->position);
        } else if (origin == SEEK_END) {
            base = static_cast<long>(file->bytes.size());
        } else {
            return false;
        }

        const long target = base + offset;
        if (target < 0 || static_cast<size_t>(target) > file->bytes.size()) {
            return false;
        }
        file->position = static_cast<size_t>(target);
        return true;
    }

    size_t Tell(Rml::FileHandle handle) override
    {
        auto* file = reinterpret_cast<RmlResourceFile*>(handle);
        if (!file) {
            return 0;
        }
        if (file->fallback) {
            const long position = std::ftell(file->fallback);
            return position >= 0 ? static_cast<size_t>(position) : 0;
        }
        return file->position;
    }

    size_t Length(Rml::FileHandle handle) override
    {
        auto* file = reinterpret_cast<RmlResourceFile*>(handle);
        if (!file) {
            return 0;
        }
        if (file->fallback) {
            return Rml::FileInterface::Length(handle);
        }
        return file->bytes.size();
    }

private:
    nw::Resource resource_from_path(Rml::String path) const
    {
        std::replace(path.begin(), path.end(), '\\', '/');
        std::replace(path.begin(), path.end(), '|', ':');

        if (const auto protocol = path.find("://"); protocol != Rml::String::npos) {
            path.erase(0, protocol + 3);
        }
        while (!path.empty() && path.front() == '/') {
            path.erase(path.begin());
        }
        if (const auto query = path.find('?'); query != Rml::String::npos) {
            path.resize(query);
        }

        auto resource = nw::Resource::from_path(std::filesystem::path{path}, true);
        if (resource.valid() && ui_resources_->contains(resource)) {
            return resource;
        }

        if (!path.empty() && path.rfind("ui/", 0) != 0) {
            return nw::Resource::from_path(std::filesystem::path{"ui"} / path, true);
        }

        return resource;
    }

    nw::ResourceData demand(const Rml::String& path) const
    {
        const nw::Resource resource = resource_from_path(path);
        if (!resource.valid()) {
            return {};
        }
        auto result = ui_resources_->demand(resource);
        if (result.bytes.size() || resource.type != nw::ResourceType::tga) {
            return result;
        }

        const auto filename = std::filesystem::path{resource.filename()}.filename();
        const auto game_resource = nw::Resource::from_path(filename);
        return game_resource.valid() ? game_resources_->demand(game_resource) : nw::ResourceData{};
    }

    nw::ResourceManager* ui_resources_ = nullptr;
    nw::ResourceManager* game_resources_ = nullptr;
};

Rml::ElementDocument* load_rml_document_from_resource(Rml::Context& context,
    const nw::ResourceManager& resources,
    nw::Resource resource)
{
    auto data = resources.demand(resource);
    if (!data.bytes.size()) {
        return nullptr;
    }

    Rml::String source{
        reinterpret_cast<const char*>(data.bytes.data()),
        data.bytes.size(),
    };
    return context.LoadDocumentFromMemory(source, resource.filename());
}

Rml::ElementDocument* load_viewer_fps_document(Rml::Context& context)
{
    static constexpr const char* kFpsOverlayRml = R"RML(
<rml>
<head>
  <style>
    body {
      width: 100%;
      height: 100%;
      margin: 0px;
      padding: 0px;
      background: transparent;
      font-family: RollnwMono;
    }
    #viewer_fps_overlay {
      position: absolute;
      display: none;
      width: 430px;
      height: 54px;
      padding: 3px 7px;
      border: 1px #41505d;
      background: #101820;
      color: #e6eef3;
      font-family: RollnwMono;
      font-size: 11px;
      font-weight: normal;
      line-height: 15px;
      text-align: right;
    }
  </style>
</head>
<body>
  <div id="viewer_fps_overlay">-- FPS</div>
</body>
</rml>
)RML";

    return context.LoadDocumentFromMemory(kFpsOverlayRml, "viewer_fps_overlay.rml");
}

Rml::ElementDocument* load_command_palette_document(Rml::Context& context)
{
    static constexpr const char* kCommandPaletteRml = R"RML(
<rml>
<head>
  <link type="text/rcss" href="panel.rcss" />
  <style>
    body {
      background: transparent;
    }
  </style>
</head>
<body>
  <div id="command_palette">
    <input id="command_input" type="text" value="" />
    <div id="command_list">
      <div id="command_list_items"></div>
    </div>
    <div id="command_details"></div>
  </div>
</body>
</rml>
)RML";

    return context.LoadDocumentFromMemory(kCommandPaletteRml, "command_palette.rml");
}

struct ProjectTreeRow {
    nw::toolset::ProjectTreeNode node;
    int depth = 0;
    bool collapsed = false;
};

struct VirtualRowWindow {
    size_t start = 0;
    size_t end = 0;
};

constexpr size_t kInvalidVirtualIndex = std::numeric_limits<size_t>::max();
constexpr size_t kMaxRecentProjects = 12;
constexpr int kPltPaletteColumns = 16;
constexpr int kPltPaletteRows = 11;
constexpr int kPltPaletteCellPx = 24;

struct RecentProjectEntry {
    std::string name;
    std::string path;
};

enum class ObjectWorkbenchSurface : uint8_t {
    details,
    sheet,
    variables,
    haks,
    classes,
    appearance,
    item_properties,
    feats,
    spells,
    inventory,
};

ObjectWorkbenchSurface default_object_workbench_surface()
{
    return ObjectWorkbenchSurface::details;
}

enum class CreatureSpellFilterField : uint8_t {
    none,
    class_,
    level,
    metamagic,
};

enum class AppearanceEditorField : uint8_t {
    appearance,
    wings,
    tail,
};

struct AreaObjectDragState {
    nw::ObjectSpatialState before;
    nw::ObjectSpatialState preview;
    glm::vec3 grab_offset{0.0f};
    bool active = false;
    bool moved = false;
};

enum class AreaObjectPlacementPhase : uint8_t {
    idle,
    armed,
    ghost_valid,
    ghost_invalid,
};

struct AreaObjectPlacementState {
    nw::Resource resource;
    nw::ObjectHandle area{};
    nw::ObjectHandle object{};
    nw::ObjectHandle previous_selection{};
    nw::ObjectSpatialState preview;
    Rml::Vector2f drag_start;
    std::string tab_id;
    AreaObjectPlacementPhase phase = AreaObjectPlacementPhase::idle;
    bool threshold_crossed = false;
    bool materialization_failed = false;

    [[nodiscard]] bool active() const noexcept
    {
        return phase != AreaObjectPlacementPhase::idle;
    }
};

enum class ProjectItemDragPhase : uint8_t {
    idle,
    armed,
    target_valid,
    target_invalid,
};

enum class ProjectItemDropTargetKind : uint8_t {
    none,
    inventory,
    equipment,
};

struct ProjectItemDropTarget {
    ProjectItemDropTargetKind kind = ProjectItemDropTargetKind::none;
    int32_t page = -1;
    int32_t row = -1;
    int32_t column = -1;
    nw::EquipIndex slot = nw::EquipIndex::invalid;

    bool operator==(const ProjectItemDropTarget&) const = default;
};

struct ProjectItemDragState {
    std::filesystem::path source_path;
    nw::ObjectHandle owner{};
    nw::ObjectHandle item{};
    Rml::Vector2f drag_start;
    std::string tab_id;
    ProjectItemDropTarget target;
    ProjectItemDragPhase phase = ProjectItemDragPhase::idle;
    int32_t width = 0;
    int32_t height = 0;
    bool threshold_crossed = false;
    bool materialization_failed = false;

    [[nodiscard]] bool active() const noexcept
    {
        return phase != ProjectItemDragPhase::idle;
    }
};

struct OutputSelectionState {
    std::string text;
    size_t anchor = 0;
    size_t focus = 0;
    bool dragging = false;

    [[nodiscard]] bool active() const noexcept { return anchor != focus; }

    [[nodiscard]] std::pair<size_t, size_t> range() const noexcept
    {
        return std::minmax(anchor, focus);
    }

    void clear() noexcept
    {
        anchor = 0;
        focus = 0;
        dragging = false;
    }
};

enum class OutputScrollAfterLayout : uint8_t {
    none,
    observe,
    follow_tail,
};

struct AppState {
    nw::toolset::RmlSmallsBridge smalls;
    nw::toolset::ToolsetBackend backend;
    nw::toolset::ShellController shell;
    nw::toolset::WorkspaceState workspace;
    nw::toolset::DialogViewState dialog_view;
    nw::toolset::ObjectDetailsSnapshot object_details;
    nw::toolset::VirtualListController details_list;
    nw::toolset::ObjectVariableSnapshot object_variables;
    nw::toolset::VirtualListController object_variable_list;
    nw::toolset::CreatureClassPresentationSnapshot creature_class_presentation;
    nw::toolset::CreatureFeatViewSnapshot creature_feats;
    nw::toolset::VirtualListController creature_feat_list;
    nw::toolset::CreatureSpellViewSnapshot creature_spells;
    std::vector<uint32_t> creature_spell_matches;
    nw::toolset::VirtualListController creature_spell_list;
    nw::toolset::VirtualComboBox creature_spell_combobox;
    nw::toolset::ItemIconTextureCache item_icon_cache;
    nw::toolset::InventoryViewSnapshot creature_inventory;
    nw::toolset::ManagedListRenderState managed_lists;
    bool creature_inventory_rendered = false;
    nw::toolset::AppearanceCatalog creature_appearance_catalog;
    nw::toolset::AppearanceCatalog placeable_appearance_catalog{
        .kind = nw::toolset::AppearanceCatalogKind::placeable};
    nw::toolset::AppearanceCatalog wing_appearance_catalog{
        .kind = nw::toolset::AppearanceCatalogKind::wing};
    nw::toolset::AppearanceCatalog tail_appearance_catalog{
        .kind = nw::toolset::AppearanceCatalogKind::tail};
    std::vector<uint32_t> appearance_matches;
    nw::toolset::VirtualListController appearance_list;
    nw::toolset::VirtualComboBox body_part_combobox;
    std::unique_ptr<nw::toolset::RmlSmallsLanguageBinding> rml_smalls_binding;
    std::unique_ptr<nw::toolset::RmlSmallsDataModel> rml_smalls_data_model;
    std::string active_object_tab_id;
    std::string creature_feat_query;
    std::string creature_spell_query;
    std::string appearance_query;
    nw::ObjectHandle appearance_object{};
    nw::ObjectHandle appearance_body_preview_object{};
    nw::ObjectHandle body_part_option_object{};
    nw::ObjectHandle color_editor_object{};
    int32_t body_part_option_part = -1;
    int32_t color_editor_channel = -1;
    int32_t creature_spell_level = -1;
    int32_t creature_inventory_page = 0;
    int32_t creature_inventory_selection = -1;
    std::optional<nw::toolset::VirtualComboBoxPopupPlacement> body_part_popup_placement;
    std::optional<nw::toolset::VirtualComboBoxPopupPlacement> creature_spell_popup_placement;
    ObjectWorkbenchSurface object_workbench_surface = ObjectWorkbenchSurface::details;
    CreatureSpellFilterField creature_spell_filter_field = CreatureSpellFilterField::none;
    AppearanceEditorField appearance_editor_field = AppearanceEditorField::appearance;
    uint64_t appearance_catalog_generation = std::numeric_limits<uint64_t>::max();
    uint64_t observed_object_mutation_epoch = 0;
    uint64_t observed_area_structure_epoch = 0;
    bool backend_ready = false;
    bool module_dialog_open = false;
    bool suppress_terminal_toggle_text_input = false;
    bool bottom_dock_resizing = false;
    bool left_dock_resizing = false;
    Uint32 open_module_dialog_event = 0;

    std::string last_recent_query;
    std::string home_area_query;
    std::string last_command_query;
    std::string last_output_filter;
    std::string active_object_variable_warning;
    OutputSelectionState output_selection;
    OutputScrollAfterLayout output_scroll_after_layout = OutputScrollAfterLayout::none;
    std::string module_dialog_command;
    std::string module_dialog_default_location;
    std::string command_palette_restore_focus_id;
    std::filesystem::path preferences_path;

    float bottom_dock_resize_start_y = 0.0f;
    int bottom_dock_resize_start_height_px = 0;
    float left_dock_resize_start_x = 0.0f;
    int left_dock_resize_start_width_px = 0;

    int hovered_recent_index = -1;
    int selected_recent_index = -1;
    int pressed_recent_index = -1;
    float workspace_tab_scroll_x = 0.0f;
    float object_workbench_tab_scroll_x = 0.0f;
    float workspace_tab_drag_start_x = 0.0f;
    float workspace_tab_drag_start_y = 0.0f;
    float appearance_editor_scroll_top = 0.0f;
    std::string workspace_tab_drag_id;
    bool workspace_tab_dragging = false;
    bool viewer_viewport_dragging = false;
    bool viewer_viewport_focused = false;
    AreaObjectDragState area_object_drag;
    AreaObjectPlacementState area_object_placement;
    ProjectItemDragState project_item_drag;
    ClientViewportDragMode viewer_viewport_drag_mode = ClientViewportDragMode::look;
    Rml::Vector2f viewer_viewport_last_point;
    float viewer_fps_frame_seconds = 0.0f;
    float viewer_fps_smoothed_seconds = 0.0f;
    float viewer_fps_work_seconds = 0.0f;
    float viewer_fps_work_smoothed_seconds = 0.0f;
    float viewer_fps_sync_seconds = 0.0f;
    float viewer_fps_sync_smoothed_seconds = 0.0f;
    float viewer_fps_draw_seconds = 0.0f;
    float viewer_fps_draw_smoothed_seconds = 0.0f;
    float viewer_fps_ui_seconds = 0.0f;
    float viewer_fps_ui_smoothed_seconds = 0.0f;
    float viewer_fps_view_seconds = 0.0f;
    float viewer_fps_view_smoothed_seconds = 0.0f;
    float viewer_fps_hud_seconds = 0.0f;
    float viewer_fps_hud_smoothed_seconds = 0.0f;
    float viewer_fps_overlay_seconds = 0.0f;
    float viewer_fps_overlay_smoothed_seconds = 0.0f;
    float viewer_fps_palette_seconds = 0.0f;
    float viewer_fps_palette_smoothed_seconds = 0.0f;
    float viewer_fps_present_seconds = 0.0f;
    float viewer_fps_present_smoothed_seconds = 0.0f;
    float viewer_fps_tick_seconds = 0.0f;
    float viewer_fps_tick_smoothed_seconds = 0.0f;
    float viewer_fps_setup_seconds = 0.0f;
    float viewer_fps_setup_smoothed_seconds = 0.0f;
    float viewer_fps_shadow_seconds = 0.0f;
    float viewer_fps_shadow_smoothed_seconds = 0.0f;
    float viewer_fps_opaque_seconds = 0.0f;
    float viewer_fps_opaque_smoothed_seconds = 0.0f;
    float viewer_fps_water_seconds = 0.0f;
    float viewer_fps_water_smoothed_seconds = 0.0f;
    float viewer_fps_transparent_seconds = 0.0f;
    float viewer_fps_transparent_smoothed_seconds = 0.0f;
    float viewer_fps_particles_seconds = 0.0f;
    float viewer_fps_particles_smoothed_seconds = 0.0f;
    float viewer_fps_debug_seconds = 0.0f;
    float viewer_fps_debug_smoothed_seconds = 0.0f;
    float viewer_fps_area_prepare_seconds = 0.0f;
    float viewer_fps_area_prepare_smoothed_seconds = 0.0f;
    float viewer_fps_view_internal_seconds = 0.0f;
    float viewer_fps_view_internal_smoothed_seconds = 0.0f;
    float viewer_fps_gpu_shadow_seconds = 0.0f;
    float viewer_fps_gpu_shadow_smoothed_seconds = 0.0f;
    float viewer_fps_gpu_opaque_seconds = 0.0f;
    float viewer_fps_gpu_opaque_smoothed_seconds = 0.0f;
    float viewer_fps_gpu_water_seconds = 0.0f;
    float viewer_fps_gpu_water_smoothed_seconds = 0.0f;
    float viewer_fps_gpu_transparent_seconds = 0.0f;
    float viewer_fps_gpu_transparent_smoothed_seconds = 0.0f;
    float viewer_fps_gpu_particles_seconds = 0.0f;
    float viewer_fps_gpu_particles_smoothed_seconds = 0.0f;
    float viewer_fps_gpu_debug_seconds = 0.0f;
    float viewer_fps_gpu_debug_smoothed_seconds = 0.0f;
    float viewer_fps_gpu_total_seconds = 0.0f;
    float viewer_fps_gpu_total_smoothed_seconds = 0.0f;
    uint32_t viewer_fps_gpu_timer_count = 0;
    float viewer_fps_editor_gpu_ui_seconds = 0.0f;
    float viewer_fps_editor_gpu_ui_smoothed_seconds = 0.0f;
    float viewer_fps_editor_gpu_viewport_seconds = 0.0f;
    float viewer_fps_editor_gpu_viewport_smoothed_seconds = 0.0f;
    float viewer_fps_editor_gpu_overlay_seconds = 0.0f;
    float viewer_fps_editor_gpu_overlay_smoothed_seconds = 0.0f;
    float viewer_fps_editor_gpu_palette_seconds = 0.0f;
    float viewer_fps_editor_gpu_palette_smoothed_seconds = 0.0f;
    float viewer_fps_editor_gpu_total_seconds = 0.0f;
    float viewer_fps_editor_gpu_total_smoothed_seconds = 0.0f;
    uint32_t viewer_fps_editor_gpu_timer_count = 0;
    uint32_t viewer_fps_model_count = 0;
    uint32_t viewer_fps_particle_system_count = 0;
    size_t viewer_fps_render_model_animation_sample_input_count = 0;
    size_t viewer_fps_render_model_animation_sampled_count = 0;
    size_t viewer_fps_render_model_animation_disabled_count = 0;
    size_t viewer_fps_render_model_animation_missing_asset_data_count = 0;
    size_t viewer_fps_render_model_animation_invalid_skeleton_count = 0;
    size_t viewer_fps_render_model_animation_failed_sample_count = 0;
    uint32_t viewer_fps_prepared_model_surface_draw_count = 0;
    uint32_t viewer_fps_prepared_model_surface_render_model_draw_count = 0;
    uint32_t viewer_fps_prepared_render_model_skin_table_skinned_surface_count = 0;
    uint32_t viewer_fps_prepared_render_model_skin_table_assigned_surface_count = 0;
    uint32_t viewer_fps_prepared_render_model_skin_table_entry_count = 0;
    uint32_t viewer_fps_prepared_render_model_skin_table_matrix_count = 0;
    uint32_t viewer_fps_prepared_render_model_skin_table_bind_pose_fallback_count = 0;
    uint32_t viewer_fps_prepared_render_model_skin_table_invalid_skin_index_count = 0;
    uint32_t viewer_fps_area_cache_record_count = 0;
    uint32_t viewer_fps_area_cache_static_record_count = 0;
    uint32_t viewer_fps_area_cache_dynamic_record_count = 0;
    uint32_t viewer_fps_area_cache_opaque_record_count = 0;
    uint32_t viewer_fps_area_cache_water_record_count = 0;
    uint32_t viewer_fps_area_cache_transparent_record_count = 0;
    uint32_t viewer_fps_area_cache_shadow_caster_record_count = 0;
    uint32_t viewer_fps_area_cache_prepared_draw_count = 0;
    uint32_t viewer_fps_area_cache_light_index_count = 0;
    uint32_t viewer_fps_area_cache_max_light_indices_per_record = 0;
    uint32_t viewer_fps_area_cache_chunk_count = 0;
    uint32_t viewer_fps_area_cache_nonempty_chunk_count = 0;
    uint32_t viewer_fps_area_cache_max_records_per_chunk = 0;
    uint32_t viewer_fps_area_frame_visible_record_count = 0;
    uint32_t viewer_fps_area_frame_visible_static_record_count = 0;
    uint32_t viewer_fps_area_frame_visible_dynamic_record_count = 0;
    uint32_t viewer_fps_area_frame_visible_chunk_count = 0;
    uint32_t viewer_fps_area_frame_opaque_record_count = 0;
    uint32_t viewer_fps_area_frame_water_record_count = 0;
    uint32_t viewer_fps_area_frame_transparent_record_count = 0;
    uint32_t viewer_fps_area_frame_shadow_caster_record_count = 0;
    uint32_t viewer_fps_area_frame_visible_prepared_surface_count = 0;
    bool viewer_fps_area_frame_uses_cached_draw_lists = false;
    uint32_t viewer_fps_local_light_count = 0;
    uint32_t viewer_fps_local_light_colored_count = 0;
    float viewer_fps_local_light_color_max = 0.0f;
    float viewer_fps_local_light_intensity_max = 0.0f;
    uint32_t viewer_fps_local_light_selected_draw_count = 0;
    uint32_t viewer_fps_local_light_selected_total = 0;
    uint32_t viewer_fps_local_light_selected_max = 0;
    uint32_t viewer_fps_local_light_selected_colored_total = 0;
    float viewer_fps_local_light_selected_color_max = 0.0f;
    float viewer_fps_local_light_selected_intensity_max = 0.0f;
    uint32_t viewer_fps_forward_plus_light_count = 0;
    uint32_t viewer_fps_forward_plus_cluster_count = 0;
    uint32_t viewer_fps_forward_plus_active_cluster_count = 0;
    uint32_t viewer_fps_forward_plus_cluster_light_index_count = 0;
    uint32_t viewer_fps_forward_plus_max_lights_per_cluster = 0;
    uint32_t viewer_fps_forward_plus_overflow_cluster_count = 0;
    uint32_t viewer_fps_forward_plus_overflow_light_count = 0;
    uint32_t viewer_fps_forward_plus_upload_bytes = 0;
    uint32_t viewer_fps_forward_plus_tile_size = 0;
    uint32_t viewer_fps_forward_plus_depth_slices = 0;
    uint32_t viewer_fps_shadow_cascade_count = 0;
    uint32_t viewer_fps_shadow_resolution = 0;
    uint32_t viewer_fps_shadow_caster_model_count = 0;
    uint32_t viewer_fps_shadow_no_caster_model_count = 0;
    uint32_t viewer_fps_shadow_submitted_model_count = 0;
    uint32_t viewer_fps_shadow_culled_model_count = 0;
    uint32_t viewer_fps_main_pass_count = 0;
    uint64_t viewer_fps_draw_count = 0;
    uint64_t viewer_fps_shadow_draw_count = 0;
    uint64_t viewer_fps_transparent_draw_count = 0;
    uint64_t viewer_fps_particle_draw_count = 0;
    uint64_t viewer_fps_indirect_draw_call_count = 0;
    uint64_t viewer_fps_draw_instance_count = 0;
    uint64_t viewer_fps_draw_index_count = 0;
    uint64_t viewer_fps_pipeline_bind_count = 0;
    uint64_t viewer_fps_pipeline_bind_skipped_count = 0;
    uint64_t viewer_fps_resource_bind_count = 0;
    uint64_t viewer_fps_resource_bind_skipped_count = 0;
    uint64_t viewer_fps_uniform_allocation_count = 0;
    uint64_t viewer_fps_uniform_allocation_bytes = 0;
    uint64_t viewer_fps_descriptor_allocation_failure_count = 0;
    uint64_t viewer_fps_descriptor_ring_capacity_bytes = 0;
    uint64_t viewer_fps_descriptor_ring_required_bytes = 0;
    uint64_t viewer_fps_resource_bind_failure_count = 0;
    uint64_t viewer_fps_dropped_draw_count = 0;
    bool viewer_fps_shadows_rendered = false;
    bool viewer_fps_water_rendered = false;
    bool workspace_hover_refresh_pending = false;
    bool workspace_tab_scroll_pending = false;
    bool object_workbench_tab_scroll_pending = false;
    bool details_list_configured = false;
    bool details_rendered = false;
    bool object_variable_list_configured = false;
    bool object_variables_rendered = false;
    bool creature_feat_list_configured = false;
    bool creature_feat_rendered = false;
    bool creature_spell_list_configured = false;
    bool creature_spell_rendered = false;
    bool appearance_list_configured = false;
    bool appearance_rendered = false;
    bool appearance_scroll_to_selection = false;
    bool appearance_selector_open = false;
    bool command_palette_ui_visible = false;
    bool command_palette_restore_captured = false;
    bool command_palette_restore_viewport_focus = false;
    Rml::Vector2f workspace_hover_refresh_point;

    std::vector<RecentProjectEntry> recent_projects;
    std::vector<nw::toolset::LoadedAreaEntry> areas;
    nw::toolset::VirtualListController home_area_list;
    uint64_t home_area_generation = std::numeric_limits<uint64_t>::max();
    nw::toolset::VirtualListRange rendered_home_area_range{};
    size_t rendered_home_area_count = kInvalidVirtualIndex;
    int rendered_home_area_columns = 0;
    std::vector<ProjectTreeRow> project_rows;
    std::unordered_set<std::string> collapsed_project_nodes;
    size_t rendered_project_row_start = kInvalidVirtualIndex;
    size_t rendered_project_row_end = kInvalidVirtualIndex;
    size_t rendered_project_row_count = 0;
    std::vector<nw::toolset::CommandSpec> commands;
    nw::toolset::VirtualListRange rendered_details_range{};
    int rendered_details_row_count = 0;
    nw::toolset::VirtualListRange rendered_object_variable_range{};
    int rendered_object_variable_row_count = 0;
    nw::toolset::VirtualListRange rendered_creature_feat_range{};
    int rendered_creature_feat_row_count = 0;
    nw::toolset::VirtualListRange rendered_creature_spell_range{};
    int rendered_creature_spell_row_count = 0;
    nw::toolset::VirtualListRange rendered_appearance_range{};
    int rendered_appearance_row_count = 0;
};

struct OpenModuleDialogRequest {
    Uint32 event_type = 0;
};

struct OpenModuleDialogResult {
    std::string path;
    std::string error;
    bool canceled = false;
};

bool ensure_backend_ready(AppState& state);
Rml::Element* find_ancestor_with_id(Rml::Element* element, std::string_view id);
Rml::Vector2f to_context_point(SDL_Window* window, float x, float y);

std::filesystem::path preferences_path(const char* app_name)
{
    char* pref_path = SDL_GetPrefPath("rollnw", app_name);
    if (!pref_path) {
        return {};
    }

    std::filesystem::path path{pref_path};
    SDL_free(pref_path);
    return path / "preferences.json";
}

std::filesystem::path client_preferences_path()
{
    return preferences_path("client");
}

void load_dock_preferences(const nlohmann::json& prefs, nw::toolset::DockLayout& docks)
{
    const auto ui = prefs.find("ui");
    if (ui == prefs.end() || !ui->is_object()) {
        return;
    }
    const auto dock_values = ui->find("docks");
    if (dock_values == ui->end() || !dock_values->is_object()) {
        return;
    }

    for (const nw::toolset::DockRegion region : {nw::toolset::DockRegion::left, nw::toolset::DockRegion::right, nw::toolset::DockRegion::bottom}) {
        const std::string region_name{nw::toolset::dock_region_name(region)};
        const auto dock = dock_values->find(region_name);
        if (dock == dock_values->end() || !dock->is_object()) {
            continue;
        }

        auto& pane = docks.pane(region);
        if (auto it = dock->find("size_px"); it != dock->end() && it->is_number_integer()) {
            pane.size_px = std::max(0, it->get<int>());
        }
        if (auto it = dock->find("visible"); it != dock->end() && it->is_boolean()) {
            pane.visible = it->get<bool>();
        }
        if (auto it = dock->find("active_widget"); it != dock->end() && it->is_string()) {
            const std::string active_widget = it->get<std::string>();
            if (docks.contains_widget(region, active_widget)) {
                pane.active_widget = active_widget;
            }
        }
    }
}

void write_dock_preferences(nlohmann::json& prefs, const nw::toolset::DockLayout& docks)
{
    auto& ui = prefs["ui"];
    if (!ui.is_object()) {
        ui = nlohmann::json::object();
    }
    auto& dock_values = ui["docks"];
    if (!dock_values.is_object()) {
        dock_values = nlohmann::json::object();
    }

    for (const nw::toolset::DockRegion region : {nw::toolset::DockRegion::left, nw::toolset::DockRegion::right, nw::toolset::DockRegion::bottom}) {
        const auto& pane = docks.pane(region);
        auto& dock = dock_values[std::string{nw::toolset::dock_region_name(region)}];
        dock["visible"] = pane.visible;
        dock["size_px"] = pane.size_px;
        dock["active_widget"] = pane.active_widget;
    }
}

void load_recent_project_preferences(const nlohmann::json& prefs, std::vector<RecentProjectEntry>& recent_projects)
{
    recent_projects.clear();

    const auto projects = prefs.find("projects");
    if (projects == prefs.end() || !projects->is_object()) {
        return;
    }
    const auto recent = projects->find("recent");
    if (recent == projects->end() || !recent->is_array()) {
        return;
    }

    std::unordered_set<std::string> seen_paths;
    for (const auto& item : *recent) {
        if (!item.is_object()) {
            continue;
        }
        const auto path_it = item.find("path");
        if (path_it == item.end() || !path_it->is_string()) {
            continue;
        }

        std::string path = path_it->get<std::string>();
        if (path.empty() || !seen_paths.insert(path).second) {
            continue;
        }

        std::string name;
        if (const auto name_it = item.find("name"); name_it != item.end() && name_it->is_string()) {
            name = name_it->get<std::string>();
        }
        if (name.empty()) {
            name = nw::toolset::project_display_name(path);
        }

        recent_projects.push_back({std::move(name), std::move(path)});
        if (recent_projects.size() >= kMaxRecentProjects) {
            break;
        }
    }
}

void write_recent_project_preferences(nlohmann::json& prefs, const std::vector<RecentProjectEntry>& recent_projects)
{
    auto& projects = prefs["projects"];
    if (!projects.is_object()) {
        projects = nlohmann::json::object();
    }

    auto recent = nlohmann::json::array();
    for (const auto& project : recent_projects) {
        if (project.path.empty()) {
            continue;
        }
        recent.push_back({
            {"name", project.name},
            {"path", project.path},
        });
    }
    projects["recent"] = std::move(recent);
}

void load_ui_preferences(AppState& state)
{
    state.preferences_path = client_preferences_path();
    if (state.preferences_path.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Unable to resolve rollnw client preferences path: %s", SDL_GetError());
        return;
    }

    std::ifstream input{state.preferences_path};
    if (!input) {
        return;
    }

    try {
        nlohmann::json prefs;
        input >> prefs;
        if (!prefs.is_object()) {
            return;
        }
        load_dock_preferences(prefs, state.shell.docks);
        load_recent_project_preferences(prefs, state.recent_projects);
    } catch (const std::exception& e) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to read rollnw client preferences: %s", e.what());
    }
}

void save_ui_preferences(const AppState& state)
{
    if (state.preferences_path.empty()) {
        return;
    }

    nlohmann::json prefs = nlohmann::json::object();
    if (std::ifstream input{state.preferences_path}; input) {
        try {
            input >> prefs;
            if (!prefs.is_object()) {
                prefs = nlohmann::json::object();
            }
        } catch (const std::exception&) {
            prefs = nlohmann::json::object();
        }
    }

    write_dock_preferences(prefs, state.shell.docks);
    write_recent_project_preferences(prefs, state.recent_projects);
    prefs.erase("left_dock_width_px");
    prefs.erase("bottom_dock_height_px");
    prefs.erase("terminal_height_px");

    std::error_code ec;
    if (const auto parent = state.preferences_path.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to create rollnw client preferences directory: %s", ec.message().c_str());
            return;
        }
    }

    std::ofstream output{state.preferences_path};
    if (!output) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to open rollnw client preferences for writing");
        return;
    }

    output << prefs.dump(2) << '\n';
}

int bottom_dock_available_height_px(SDL_Window* window)
{
    const auto window_size = query_window_size(window);
    const int window_height = window_size.second > 0 ? window_size.second : 720;
    return std::max(1, window_height - kBottomDockViewportReservePx);
}

int left_dock_available_width_px(SDL_Window* window)
{
    const auto window_size = query_window_size(window);
    const int window_width = window_size.first > 0 ? window_size.first : 1280;
    return std::max(1, window_width - kBottomDockViewportReservePx);
}

int clamp_left_dock_width_px(const AppState& state, int width_px, SDL_Window* window)
{
    const auto& left = state.shell.docks.pane(nw::toolset::DockRegion::left);
    const int available_width = left_dock_available_width_px(window);
    const int min_width = std::min(left.min_size_px, available_width);
    const int max_width = std::max(min_width, std::min(left.max_size_px, available_width));
    return std::clamp(width_px, min_width, max_width);
}

int clamp_bottom_dock_height_px(const AppState& state, int height_px, SDL_Window* window)
{
    const auto& bottom = state.shell.docks.pane(nw::toolset::DockRegion::bottom);
    const int available_height = bottom_dock_available_height_px(window);
    const int min_height = std::min(bottom.min_size_px, available_height);
    const int max_height = std::max(min_height, std::min(bottom.max_size_px, available_height));
    return std::clamp(height_px, min_height, max_height);
}

bool left_dock_visible_for_active_tab(const AppState& state)
{
    return state.shell.showing_project_tree || state.shell.showing_areas;
}

void apply_shell_layout(Rml::ElementDocument* doc, const AppState& state)
{
    if (!doc) {
        return;
    }

    const auto& left = state.shell.docks.pane(nw::toolset::DockRegion::left);
    const auto& bottom = state.shell.docks.pane(nw::toolset::DockRegion::bottom);
    const int panel_bottom_px = bottom.visible ? bottom.size_px : 0;
    const bool show_left_dock = left_dock_visible_for_active_tab(state);
    if (auto* panel = doc->GetElementById("panel")) {
        panel->SetProperty("display", show_left_dock ? "flex" : "none");
        panel->SetProperty("width", std::to_string(left.size_px) + "px");
        panel->SetProperty("bottom", std::to_string(panel_bottom_px) + "px");
    }
    if (auto* workspace = doc->GetElementById("workspace_shell")) {
        workspace->SetProperty("left", std::to_string(show_left_dock ? left.size_px : 0) + "px");
        workspace->SetProperty("bottom", std::to_string(panel_bottom_px) + "px");
    }
}

void apply_left_dock_width(Rml::ElementDocument* doc, AppState& state, SDL_Window* window, int requested_width_px)
{
    if (!doc || requested_width_px <= 0) {
        return;
    }

    const int width_px = clamp_left_dock_width_px(state, requested_width_px, window);
    state.shell.docks.set_size_px(nw::toolset::DockRegion::left, width_px);
    apply_shell_layout(doc, state);
}

void apply_bottom_dock_height(Rml::ElementDocument* doc, AppState& state, SDL_Window* window, int requested_height_px)
{
    if (!doc || requested_height_px <= 0) {
        return;
    }

    const int height_px = clamp_bottom_dock_height_px(state, requested_height_px, window);
    state.shell.set_bottom_dock_size_px(height_px);

    if (auto* dock = doc->GetElementById("bottom_dock")) {
        dock->SetProperty("height", std::to_string(height_px) + "px");
    }
    apply_shell_layout(doc, state);
}

bool begin_bottom_dock_resize(Rml::Context* context,
    SDL_Window* window,
    Rml::ElementDocument* doc,
    AppState& state,
    const SDL_MouseButtonEvent& mouse)
{
    if (!state.shell.bottom_dock_visible() || mouse.button != SDL_BUTTON_LEFT || !context || !doc) {
        return false;
    }

    const auto point = to_context_point(window, mouse.x, mouse.y);
    auto* hit = context->GetElementAtPoint(point);
    if (!find_ancestor_with_id(hit, "bottom_dock_resize_grabber")) {
        return false;
    }

    auto* dock = doc->GetElementById("bottom_dock");
    if (!dock) {
        return false;
    }

    const auto& bottom = state.shell.docks.pane(nw::toolset::DockRegion::bottom);
    const int measured_height = static_cast<int>(std::lround(dock->GetOffsetHeight()));
    state.bottom_dock_resizing = true;
    state.bottom_dock_resize_start_y = point.y;
    state.bottom_dock_resize_start_height_px = measured_height > 0
        ? measured_height
        : clamp_bottom_dock_height_px(state, bottom.size_px, window);
    state.shell.set_bottom_dock_size_px(state.bottom_dock_resize_start_height_px);
    SDL_CaptureMouse(true);
    return true;
}

bool update_bottom_dock_resize(Rml::ElementDocument* doc, AppState& state, SDL_Window* window, const SDL_MouseMotionEvent& motion)
{
    if (!state.bottom_dock_resizing) {
        return false;
    }

    const auto point = to_context_point(window, motion.x, motion.y);
    const int requested_height = state.bottom_dock_resize_start_height_px
        - static_cast<int>(std::lround(point.y - state.bottom_dock_resize_start_y));
    apply_bottom_dock_height(doc, state, window, requested_height);
    return true;
}

bool end_bottom_dock_resize(AppState& state)
{
    if (!state.bottom_dock_resizing) {
        return false;
    }

    state.bottom_dock_resizing = false;
    SDL_CaptureMouse(false);
    save_ui_preferences(state);
    return true;
}

bool begin_left_dock_resize(Rml::Context* context,
    SDL_Window* window,
    Rml::ElementDocument* doc,
    AppState& state,
    const SDL_MouseButtonEvent& mouse)
{
    if (mouse.button != SDL_BUTTON_LEFT || !context || !doc) {
        return false;
    }

    const auto point = to_context_point(window, mouse.x, mouse.y);
    auto* hit = context->GetElementAtPoint(point);
    if (!find_ancestor_with_id(hit, "left_dock_resize_grabber")) {
        return false;
    }

    auto* panel = doc->GetElementById("panel");
    if (!panel) {
        return false;
    }

    const auto& left = state.shell.docks.pane(nw::toolset::DockRegion::left);
    const int measured_width = static_cast<int>(std::lround(panel->GetOffsetWidth()));
    state.left_dock_resizing = true;
    state.left_dock_resize_start_x = point.x;
    state.left_dock_resize_start_width_px = measured_width > 0
        ? measured_width
        : clamp_left_dock_width_px(state, left.size_px, window);
    state.shell.docks.set_size_px(nw::toolset::DockRegion::left, state.left_dock_resize_start_width_px);
    SDL_CaptureMouse(true);
    return true;
}

bool update_left_dock_resize(Rml::ElementDocument* doc, AppState& state, SDL_Window* window, const SDL_MouseMotionEvent& motion)
{
    if (!state.left_dock_resizing) {
        return false;
    }

    const auto point = to_context_point(window, motion.x, motion.y);
    const int requested_width = state.left_dock_resize_start_width_px
        + static_cast<int>(std::lround(point.x - state.left_dock_resize_start_x));
    apply_left_dock_width(doc, state, window, requested_width);
    return true;
}

bool end_left_dock_resize(AppState& state)
{
    if (!state.left_dock_resizing) {
        return false;
    }

    state.left_dock_resizing = false;
    SDL_CaptureMouse(false);
    save_ui_preferences(state);
    return true;
}

bool consume_terminal_toggle_text_input(AppState& state, const SDL_Event& event)
{
    if (!state.suppress_terminal_toggle_text_input) {
        return false;
    }

    if (event.type == SDL_EVENT_TEXT_INPUT) {
        state.suppress_terminal_toggle_text_input = false;
        const std::string_view text = event.text.text;
        return text == "`" || text == "~";
    }

    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key != SDLK_GRAVE) {
        state.suppress_terminal_toggle_text_input = false;
    }

    return false;
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

std::filesystem::path module_dialog_start_location()
{
    namespace fs = std::filesystem;
    std::error_code ec;

    const fs::path user = nw::kernel::config().user_path();
    if (!user.empty()) {
        const fs::path modules = user / "modules";
        if (fs::is_directory(modules, ec)) {
            return modules;
        }
        if (fs::is_directory(user, ec)) {
            return user;
        }
    }

    return {};
}

std::filesystem::path project_dialog_start_location()
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    if (!ec && fs::is_directory(cwd, ec)) {
        return cwd;
    }
    return module_dialog_start_location();
}

bool relative_path_escapes_root(const std::filesystem::path& relative)
{
    return std::any_of(relative.begin(), relative.end(), [](const auto& part) {
        return part == "..";
    });
}

std::optional<std::filesystem::path> validated_project_file(
    const std::filesystem::path& project_dir, std::string_view relative_text, std::string& error)
{
    namespace fs = std::filesystem;
    error.clear();
    const fs::path relative{relative_text};
    if (project_dir.empty() || relative.empty() || relative.is_absolute()) {
        error = "Preview document path is not project-relative";
        return std::nullopt;
    }

    std::error_code ec;
    const auto root = fs::weakly_canonical(project_dir, ec);
    if (ec) {
        error = "Failed to resolve project directory: " + ec.message();
        return std::nullopt;
    }
    const auto target = fs::weakly_canonical(project_dir / relative, ec);
    if (ec) {
        error = "Failed to resolve preview document: " + ec.message();
        return std::nullopt;
    }
    const auto from_root = fs::relative(target, root, ec);
    if (ec || from_root.empty() || relative_path_escapes_root(from_root)) {
        error = "Preview document is outside the active project";
        return std::nullopt;
    }
    return target;
}

nw::toolset::CommandResult document_save_result(
    nw::toolset::CommandStatus status, std::string message, nw::toolset::CommandOutputChannel channel)
{
    nw::toolset::CommandResult result;
    result.status = status;
    result.message = std::move(message);
    result.output_channel = channel;
    return result;
}

void remember_recent_project(AppState& state, const std::filesystem::path& project_dir)
{
    if (project_dir.empty()) {
        return;
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path canonical = fs::weakly_canonical(project_dir, ec);
    const fs::path normalized = ec ? project_dir.lexically_normal() : canonical;
    const std::string path = normalized.string();
    if (path.empty()) {
        return;
    }

    state.recent_projects.erase(std::remove_if(state.recent_projects.begin(), state.recent_projects.end(), [&path](const RecentProjectEntry& entry) {
        return entry.path == path;
    }),
        state.recent_projects.end());

    state.recent_projects.insert(state.recent_projects.begin(), RecentProjectEntry{
                                                                    nw::toolset::project_display_name(normalized),
                                                                    path,
                                                                });
    if (state.recent_projects.size() > kMaxRecentProjects) {
        state.recent_projects.resize(kMaxRecentProjects);
    }
    save_ui_preferences(state);
}

void SDLCALL open_module_dialog_callback(void* userdata, const char* const* filelist, int /*filter*/)
{
    auto* request = static_cast<OpenModuleDialogRequest*>(userdata);
    const Uint32 event_type = request ? request->event_type : 0;
    delete request;

    auto* result = new OpenModuleDialogResult{};
    if (!filelist) {
        result->error = SDL_GetError();
    } else if (!filelist[0]) {
        result->canceled = true;
    } else {
        result->path = filelist[0];
    }

    SDL_Event event{};
    event.type = event_type;
    event.user.data1 = result;
    if (event_type == 0 || !SDL_PushEvent(&event)) {
        delete result;
    }
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
    if (state.active_object_variable_warning.empty()) {
        return;
    }
    state.active_object_variable_warning.clear();
    if (auto* tooltip = find_el(doc, "object_variable_warning_tooltip")) {
        tooltip->SetProperty("display", "none");
    }
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
    auto* warning = state.object_workbench_surface == ObjectWorkbenchSurface::variables
            && !state.shell.command_palette_visible
        ? find_ancestor_with_class(hit, "object_variable_field_warning")
        : nullptr;
    const std::string description = warning
        ? warning->GetAttribute<Rml::String>("data-tooltip", "")
        : std::string{};
    if (description.empty() || viewport_width <= 16 || viewport_height <= 16) {
        hide_object_variable_warning_tooltip(doc, state);
        return;
    }

    auto* tooltip = find_el(doc, "object_variable_warning_tooltip");
    if (!tooltip) {
        state.active_object_variable_warning.clear();
        return;
    }
    if (description != state.active_object_variable_warning) {
        state.active_object_variable_warning = description;
        tooltip->SetInnerRML(escape_html(description));
    }

    constexpr int margin = 8;
    constexpr int pointer_offset = 14;
    constexpr int preferred_width = 280;
    constexpr int estimated_height = 54;
    const int width = std::min(preferred_width, viewport_width - 2 * margin);
    const int max_left = std::max(margin, viewport_width - width - margin);
    const int left = std::clamp(
        static_cast<int>(std::lround(point.x)) + pointer_offset,
        margin,
        max_left);
    int top = static_cast<int>(std::lround(point.y)) + pointer_offset;
    if (top + estimated_height > viewport_height - margin) {
        top = static_cast<int>(std::lround(point.y))
            - estimated_height - pointer_offset;
    }
    top = std::clamp(top, margin,
        std::max(margin, viewport_height - estimated_height - margin));

    tooltip->SetProperty("display", "block");
    tooltip->SetProperty("width", std::to_string(width) + "px");
    tooltip->SetProperty("left", std::to_string(left) + "px");
    tooltip->SetProperty("top", std::to_string(top) + "px");
}

bool close_active_smalls_selector(Rml::ElementDocument* document)
{
    if (!document) {
        return false;
    }
    Rml::ElementList selectors;
    document->GetElementsByClassName(selectors, "smalls_selector");
    for (auto* selector : selectors) {
        if (!selector->IsClassSet("active")) {
            continue;
        }
        Rml::ElementList close_buttons;
        selector->GetElementsByClassName(
            close_buttons, "smalls_selector_close");
        if (!close_buttons.empty()) {
            return close_buttons.front()->DispatchEvent("click", {});
        }
    }
    return false;
}

Rml::Vector2f to_context_point(SDL_Window* window, float x, float y);
Rml::Element* find_recent_item_at(Rml::Element* list, Rml::Vector2f point);
Rml::Element* workspace_tab_element_at_point(Rml::ElementDocument* doc, std::string_view class_name, Rml::Vector2f point);
void apply_workspace_tab_scroll(Rml::ElementDocument* doc, AppState& state);
void apply_object_workbench_tab_scroll(Rml::ElementDocument* doc, AppState& state);
size_t workspace_tab_target_index_at_point(Rml::ElementDocument* doc,
    Rml::Vector2f point,
    const std::vector<nw::toolset::WorkspaceTab>& tabs,
    std::string_view dragged_tab_id,
    size_t fallback);
void clear_workspace_tab_drag(AppState& state);
std::optional<WorkspaceViewerViewportRequest> active_workspace_viewer_viewport_request(
    Rml::ElementDocument* doc, AppState& state, int frame_width, int frame_height);

Rml::Element* element_at_mouse(Rml::Context* context, SDL_Window* window, const SDL_MouseButtonEvent& mouse)
{
    if (!context || !window) {
        return nullptr;
    }

    return context->GetElementAtPoint(Rml::Vector2f{static_cast<float>(mouse.x), static_cast<float>(mouse.y)});
}

bool point_within_element(Rml::ElementDocument* doc, std::string_view id, Rml::Vector2f point)
{
    auto* element = find_el(doc, std::string(id).c_str());
    return element && element->IsPointWithinElement(point);
}

bool point_within_viewport(ClientViewportRect rect, Rml::Vector2f point)
{
    const float left = static_cast<float>(rect.x);
    const float top = static_cast<float>(rect.y);
    const float right = left + static_cast<float>(rect.width);
    const float bottom = top + static_cast<float>(rect.height);
    return point.x >= left && point.x < right && point.y >= top && point.y < bottom;
}

bool command_palette_contains_point(Rml::ElementDocument* palette_doc, Rml::Vector2f point)
{
    return point_within_element(palette_doc, "command_palette", point);
}

bool event_targets_command_palette(
    Rml::ElementDocument* palette_doc, const AppState& state, SDL_Window* window, const SDL_Event& event)
{
    if (!state.shell.command_palette_visible || !palette_doc) {
        return false;
    }

    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_TEXT_EDITING:
    case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_TEXT_EDITING_CANDIDATES:
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        return command_palette_contains_point(palette_doc, to_context_point(window, event.button.x, event.button.y));
    case SDL_EVENT_MOUSE_MOTION:
        return command_palette_contains_point(palette_doc, to_context_point(window, event.motion.x, event.motion.y));
    case SDL_EVENT_MOUSE_WHEEL:
        return command_palette_contains_point(palette_doc, to_context_point(window, event.wheel.mouse_x, event.wheel.mouse_y));
    default:
        return false;
    }
}

bool focused_text_input(Rml::Context* context)
{
    auto* focus = context ? context->GetFocusElement() : nullptr;
    if (!focus) {
        return false;
    }

    if (!focus->IsVisible(true)) {
        return false;
    }

    for (auto* cursor = focus; cursor; cursor = cursor->GetParentNode()) {
        const Rml::String id = cursor->GetId();
        if (id == "command_input"
            || id == "terminal_input"
            || id == "recent_search"
            || id == "output_filter") {
            return true;
        }
        if (cursor->GetTagName() == "input"
            || cursor->GetTagName() == "textarea") {
            return true;
        }
    }
    return false;
}

bool focused_element_has_id(Rml::Context* context, const char* id)
{
    auto* focus = context ? context->GetFocusElement() : nullptr;
    if (!focus || !id || !focus->IsVisible(true)) {
        return false;
    }

    for (auto* cursor = focus; cursor; cursor = cursor->GetParentNode()) {
        if (cursor->GetId() == id) {
            return true;
        }
    }
    return false;
}

bool output_scroll_input(Rml::ElementDocument* doc, Rml::Context* context,
    SDL_Window* window, const SDL_Event& event)
{
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
        return focused_element_has_id(context, "output_list");
    case SDL_EVENT_MOUSE_WHEEL:
        return point_within_element(doc, "output_list",
            to_context_point(window, event.wheel.mouse_x, event.wheel.mouse_y));
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        return point_within_element(doc, "output_list",
            to_context_point(window, event.button.x, event.button.y));
    case SDL_EVENT_MOUSE_BUTTON_UP:
        return focused_element_has_id(context, "output_list")
            || point_within_element(doc, "output_list",
                to_context_point(window, event.button.x, event.button.y));
    case SDL_EVENT_MOUSE_MOTION:
        return (event.motion.state & SDL_BUTTON_LMASK) != 0
            && (focused_element_has_id(context, "output_list")
                || point_within_element(doc, "output_list",
                    to_context_point(window, event.motion.x, event.motion.y)));
    default:
        return false;
    }
}

void observe_output_scroll(Rml::ElementDocument* doc, AppState& state)
{
    if (auto* output = doc ? doc->GetElementById("output_list") : nullptr) {
        state.shell.observe_output_scroll(
            output->GetScrollTop(), output->GetScrollHeight(), output->GetClientHeight());
    }
}

bool apply_output_scroll_after_layout(Rml::ElementDocument* doc, AppState& state)
{
    if (state.output_scroll_after_layout == OutputScrollAfterLayout::none
        || !state.shell.output_panel_visible()) {
        return false;
    }

    auto* output = doc ? doc->GetElementById("output_list") : nullptr;
    if (!output || !output->IsVisible(true)) {
        return false;
    }

    const float previous_scroll_top = output->GetScrollTop();
    if (state.output_scroll_after_layout == OutputScrollAfterLayout::follow_tail) {
        output->SetScrollTop(output->GetScrollHeight());
    }
    observe_output_scroll(doc, state);
    state.output_scroll_after_layout = OutputScrollAfterLayout::none;
    return output->GetScrollTop() != previous_scroll_top;
}

std::optional<size_t> parse_size(std::string_view value)
{
    if (value.empty()) {
        return std::nullopt;
    }

    size_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    return result;
}

size_t output_byte_offset_at_x(Rml::Element* row, std::string_view text, float x)
{
    if (!row || text.empty() || x <= 0.0f) {
        return 0;
    }

    size_t previous_byte = 0;
    float width = 0.0f;
    while (previous_byte < text.size()) {
        const char* next = Rml::StringUtilities::SeekForwardUTF8(
            text.data() + previous_byte + 1, text.data() + text.size());
        const size_t current_byte = static_cast<size_t>(next - text.data());
        const float glyph_width = static_cast<float>(Rml::ElementUtilities::GetStringWidth(
            row, Rml::StringView{text.data() + previous_byte, next}));
        if (x < width + glyph_width * 0.5f) {
            return previous_byte;
        }
        previous_byte = current_byte;
        width += glyph_width;
    }
    return text.size();
}

std::optional<size_t> output_text_offset_at_point(
    Rml::Context* context, Rml::ElementDocument* doc,
    const AppState& state, Rml::Vector2f point)
{
    if (!context || !point_within_element(doc, "output_list", point)) {
        return std::nullopt;
    }

    auto* row = find_ancestor_with_class(
        context->GetElementAtPoint(point), "output_line");
    if (!row) {
        return std::nullopt;
    }

    const auto start = parse_size(
        row->GetAttribute<Rml::String>("data-output-start", ""));
    const auto length = parse_size(
        row->GetAttribute<Rml::String>("data-output-length", ""));
    if (!start || !length || *start > state.output_selection.text.size()) {
        return std::nullopt;
    }

    const size_t clamped_length = std::min(
        *length, state.output_selection.text.size() - *start);
    const std::string_view row_text{state.output_selection.text.data() + *start,
        clamped_length};
    const float local_x = point.x - row->GetAbsoluteOffset(Rml::BoxArea::Content).x;
    return *start + output_byte_offset_at_x(row, row_text, local_x);
}

void clear_rml_focus(Rml::Context* context)
{
    if (auto* focus = context ? context->GetFocusElement() : nullptr) {
        focus->Blur();
    }
}

// An Rml context owns exactly one focus element. Some client mouse paths are
// consumed before RmlUi sees them, so explicitly end a variable edit when the
// pointer leaves its focused input.
void blur_focused_object_variable_input(
    Rml::Context* context, Rml::Vector2f point)
{
    auto* focus = context ? context->GetFocusElement() : nullptr;
    if (!focus
        || (!focus->IsClassSet("object_variable_name")
            && !focus->IsClassSet("object_variable_value"))) {
        return;
    }

    auto* hit = context->GetElementAtPoint(point);
    auto* hit_input = find_ancestor_with_class(hit, "object_variable_name");
    if (!hit_input) {
        hit_input = find_ancestor_with_class(hit, "object_variable_value");
    }
    if (hit_input != focus) {
        focus->Blur();
    }
}

std::string focus_restore_id(Rml::Element* focus)
{
    for (auto* cursor = focus; cursor; cursor = cursor->GetParentNode()) {
        const Rml::String id = cursor->GetId();
        if (!id.empty()) {
            return id;
        }
    }
    return {};
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

void capture_command_palette_focus(Rml::Context* context, AppState& state)
{
    if (state.command_palette_restore_captured) {
        return;
    }

    state.command_palette_restore_focus_id.clear();
    state.command_palette_restore_viewport_focus = false;
    state.command_palette_restore_captured = true;

    if (auto* focus = context ? context->GetFocusElement() : nullptr) {
        state.command_palette_restore_focus_id = focus_restore_id(focus);
        return;
    }

    state.command_palette_restore_viewport_focus = state.viewer_viewport_focused;
}

void restore_command_palette_focus(Rml::Context* context, Rml::Context* palette_context, Rml::ElementDocument* doc, AppState& state)
{
    clear_rml_focus(palette_context);

    if (!state.command_palette_restore_captured) {
        return;
    }

    const std::string restore_focus_id = std::move(state.command_palette_restore_focus_id);
    const bool restore_viewport_focus = state.command_palette_restore_viewport_focus;
    state.command_palette_restore_focus_id.clear();
    state.command_palette_restore_viewport_focus = false;
    state.command_palette_restore_captured = false;

    if (!restore_focus_id.empty()) {
        if (auto* element = find_el(doc, restore_focus_id.c_str())) {
            if (element->IsVisible(true)) {
                element->Focus();
                state.viewer_viewport_focused = false;
                return;
            }
        }
    }

    if (restore_viewport_focus) {
        clear_rml_focus(context);
        state.viewer_viewport_focused = true;
    }
}

bool viewport_mouse_hit_blocked(Rml::ElementDocument* doc, Rml::Element* top_hit, Rml::Vector2f point, const AppState& state)
{
    if (state.shell.bottom_dock_visible() && point_within_element(doc, "bottom_dock", point)) {
        return true;
    }
    if (state.module_dialog_open) {
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
        || state.module_dialog_open
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

    auto command = viewer_viewport_camera_command_from_key(key.key, viewer_viewport->kind);
    if (!command) {
        return false;
    }

    state.viewer_viewport_focused = true;
    const float scale = (key.mod & SDL_KMOD_SHIFT) ? 3.0f : 1.0f;
    renderer.viewer_viewport_camera_command(*command, scale, viewer_viewport->rect);
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

Rml::Element* recent_item_at_point(Rml::ElementDocument* doc, Rml::Vector2f point)
{
    if (!doc) {
        return nullptr;
    }

    auto* list = doc->GetElementById("recent_list");
    if (!list) {
        return nullptr;
    }

    if (auto* search = doc->GetElementById("recent_search"); search && search->IsPointWithinElement(point)) {
        return nullptr;
    }

    if (!list->IsPointWithinElement(point)) {
        return nullptr;
    }

    return find_recent_item_at(list, point);
}

Rml::Vector2f to_context_point(SDL_Window* window, float x, float y)
{
    (void)window;
    return Rml::Vector2f{x, y};
}

Rml::Element* find_recent_item_at(Rml::Element* list, Rml::Vector2f point)
{
    if (!list) {
        return nullptr;
    }

    const auto visit = [&](auto&& self, Rml::Element* element) -> Rml::Element* {
        if (!element) {
            return nullptr;
        }
        if (element->IsClassSet("recent_item") && element->IsPointWithinElement(point)) {
            return element;
        }
        const int child_count = element->GetNumChildren();
        for (int i = 0; i < child_count; ++i) {
            if (auto* found = self(self, element->GetChild(i))) {
                return found;
            }
        }
        return nullptr;
    };

    return visit(visit, list);
}

Rml::Element* workspace_tab_element_at_point(Rml::ElementDocument* doc, std::string_view class_name, Rml::Vector2f point)
{
    auto* tabs = find_el(doc, "workspace_tabs");
    if (!tabs || !tabs->IsPointWithinElement(point)) {
        return nullptr;
    }

    const std::string class_text{class_name};
    const auto visit = [&](auto&& self, Rml::Element* element) -> Rml::Element* {
        if (!element) {
            return nullptr;
        }
        if (element->IsClassSet(class_text.c_str()) && element->IsPointWithinElement(point)) {
            return element;
        }
        const int child_count = element->GetNumChildren();
        for (int i = 0; i < child_count; ++i) {
            if (auto* found = self(self, element->GetChild(i))) {
                return found;
            }
        }
        return nullptr;
    };

    return visit(visit, tabs);
}

void apply_tab_scroll(Rml::ElementDocument* doc,
    const TabScrollStrip& strip, float& scroll_x)
{
    auto* tabs = find_el(doc, strip.viewport_id);
    auto* previous = find_el(doc, strip.previous_id);
    auto* next = find_el(doc, strip.next_id);
    if (!tabs) {
        scroll_x = 0.0f;
        if (previous) {
            previous->SetClass("disabled", true);
        }
        if (next) {
            next->SetClass("disabled", true);
        }
        return;
    }

    const float content_width = tabs->GetScrollWidth();
    const float viewport_width = tabs->GetClientWidth();
    const float max_scroll = std::max(0.0f, content_width - viewport_width);
    scroll_x = std::clamp(scroll_x, 0.0f, max_scroll);
    tabs->SetScrollLeft(scroll_x);
    scroll_x = tabs->GetScrollLeft();
    constexpr float boundary_epsilon = 0.5f;
    if (previous) {
        previous->SetClass("disabled", scroll_x <= boundary_epsilon);
    }
    if (next) {
        next->SetClass(
            "disabled",
            scroll_x >= max_scroll - boundary_epsilon);
    }
}

void remember_tab_scroll(Rml::ElementDocument* doc,
    const TabScrollStrip& strip, float& scroll_x)
{
    if (auto* tabs = find_el(doc, strip.viewport_id)) {
        scroll_x = tabs->GetScrollLeft();
    }
}

void apply_workspace_tab_scroll(Rml::ElementDocument* doc, AppState& state)
{
    apply_tab_scroll(doc, kWorkspaceTabScrollStrip,
        state.workspace_tab_scroll_x);
}

void apply_object_workbench_tab_scroll(
    Rml::ElementDocument* doc, AppState& state)
{
    apply_tab_scroll(doc, kObjectWorkbenchTabScrollStrip,
        state.object_workbench_tab_scroll_x);
}

float tab_scroll_target(Rml::ElementDocument* doc,
    const TabScrollStrip& strip, bool forward)
{
    auto* tabs = find_el(doc, strip.viewport_id);
    auto* track = find_el(doc, strip.track_id);
    if (!tabs || !track) {
        return 0.0f;
    }

    const float viewport_width = tabs->GetClientWidth();
    const float max_scroll = std::max(
        0.0f, tabs->GetScrollWidth() - viewport_width);
    const float current = std::clamp(
        tabs->GetScrollLeft(), 0.0f, max_scroll);
    constexpr float boundary_epsilon = 0.5f;

    if (forward) {
        const float visible_right = current + viewport_width;
        const int child_count = track->GetNumChildren();
        for (int i = 0; i < child_count; ++i) {
            auto* child = track->GetChild(i);
            if (!child || !child->IsClassSet(strip.tab_class)) {
                continue;
            }
            const float child_right = child->GetOffsetLeft()
                + child->GetOffsetWidth();
            if (child_right > visible_right + boundary_epsilon) {
                return std::clamp(
                    child_right - viewport_width, 0.0f, max_scroll);
            }
        }
        return max_scroll;
    }

    for (int i = track->GetNumChildren() - 1; i >= 0; --i) {
        auto* child = track->GetChild(i);
        if (!child || !child->IsClassSet(strip.tab_class)) {
            continue;
        }
        const float child_left = child->GetOffsetLeft();
        if (child_left < current - boundary_epsilon) {
            return std::clamp(child_left, 0.0f, max_scroll);
        }
    }
    return 0.0f;
}

size_t workspace_tab_current_index(const std::vector<nw::toolset::WorkspaceTab>& tabs, std::string_view id, size_t fallback)
{
    for (size_t i = 0; i < tabs.size(); ++i) {
        if (tabs[i].id == id) {
            return i;
        }
    }
    return fallback;
}

size_t workspace_tab_locked_prefix_count(const std::vector<nw::toolset::WorkspaceTab>& tabs)
{
    size_t count = 0;
    while (count < tabs.size() && !tabs[count].movable) {
        ++count;
    }
    return count;
}

size_t workspace_tab_target_index_at_point(Rml::ElementDocument* doc,
    Rml::Vector2f point,
    const std::vector<nw::toolset::WorkspaceTab>& tabs,
    std::string_view dragged_tab_id,
    size_t fallback)
{
    auto* track = find_el(doc, "workspace_tab_track");
    if (!track || tabs.empty()) {
        return fallback;
    }

    const size_t locked_prefix = workspace_tab_locked_prefix_count(tabs);
    if (locked_prefix >= tabs.size()) {
        return std::min(fallback, tabs.size() - 1);
    }

    const size_t dragged_index = workspace_tab_current_index(tabs, dragged_tab_id, fallback);
    size_t target = std::clamp(fallback, locked_prefix, tabs.size() - 1);
    const int child_count = track->GetNumChildren();
    for (int i = 0; i < child_count; ++i) {
        auto* child = track->GetChild(i);
        if (!child || !child->IsClassSet("workspace_tab")) {
            continue;
        }
        const std::string index_text = child->GetAttribute<Rml::String>("data-index", "");
        if (index_text.empty()) {
            continue;
        }

        const std::string child_tab_id = child->GetAttribute<Rml::String>("data-tab", "");
        if (child_tab_id == dragged_tab_id) {
            continue;
        }

        const size_t original_index = static_cast<size_t>(std::strtoull(index_text.c_str(), nullptr, 10));
        if (original_index >= tabs.size() || original_index < locked_prefix) {
            continue;
        }

        const size_t index_without_dragged = (dragged_index < original_index) ? original_index - 1 : original_index;
        target = std::max(index_without_dragged, locked_prefix);
        const float midpoint = child->GetAbsoluteLeft() + child->GetOffsetWidth() * 0.5f;
        if (point.x < midpoint) {
            return target;
        }
    }
    return tabs.size() - 1;
}

void clear_workspace_tab_drag(AppState& state)
{
    state.workspace_tab_drag_id.clear();
    state.workspace_tab_dragging = false;
    state.workspace_tab_drag_start_x = 0.0f;
    state.workspace_tab_drag_start_y = 0.0f;
}

void set_recent_hover(Rml::ElementDocument* doc, AppState& state, int hovered_index)
{
    if (!doc) {
        return;
    }
    if (state.hovered_recent_index == hovered_index) {
        return;
    }

    auto* list = doc->GetElementById("recent_list");
    if (!list) {
        state.hovered_recent_index = hovered_index;
        return;
    }

    list->SetProperty("cursor", hovered_index >= 0 ? "pointer" : "auto");

    const std::string target_key = hovered_index >= 0 ? std::to_string(hovered_index) : std::string();

    const auto visit = [&](auto&& self, Rml::Element* element) -> void {
        if (!element) {
            return;
        }
        if (element->IsClassSet("recent_item")) {
            const bool active = (hovered_index >= 0 && element->GetAttribute<Rml::String>("data-key", "") == target_key);
            element->SetClass("hovered", active);
        }
        const int child_count = element->GetNumChildren();
        for (int i = 0; i < child_count; ++i) {
            self(self, element->GetChild(i));
        }
    };

    visit(visit, list);
    state.hovered_recent_index = hovered_index;
}

void set_recent_selected(Rml::ElementDocument* doc, AppState& state, int selected_index)
{
    if (!doc) {
        return;
    }
    if (state.selected_recent_index == selected_index) {
        return;
    }

    auto* list = doc->GetElementById("recent_list");
    if (!list) {
        state.selected_recent_index = selected_index;
        return;
    }

    const std::string target_key = selected_index >= 0 ? std::to_string(selected_index) : std::string();

    const auto visit = [&](auto&& self, Rml::Element* element) -> void {
        if (!element) {
            return;
        }
        if (element->IsClassSet("recent_item")) {
            const bool active = (selected_index >= 0 && element->GetAttribute<Rml::String>("data-key", "") == target_key);
            element->SetClass("selected", active);
        }
        const int child_count = element->GetNumChildren();
        for (int i = 0; i < child_count; ++i) {
            self(self, element->GetChild(i));
        }
    };

    visit(visit, list);
    state.selected_recent_index = selected_index;
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

void set_input_value_and_cursor(Rml::ElementDocument* doc, const char* id, std::string_view value, size_t cursor_byte_position)
{
    if (!doc) {
        return;
    }
    if (auto* input = doc->GetElementById(id)) {
        if (auto* control = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(input)) {
            const Rml::String rml_value(value);
            control->SetValue(rml_value);
            const int cursor = Rml::StringUtilities::ConvertByteOffsetToCharacterOffset(
                rml_value,
                static_cast<int>(std::min(cursor_byte_position, rml_value.size())));
            control->SetSelectionRange(cursor, cursor);
            return;
        }
        input->SetAttribute("value", Rml::String(value));
    }
}

bool get_input_cursor_byte_position(Rml::ElementDocument* doc, const char* id, std::string_view value, size_t& cursor_byte_position)
{
    if (!doc) {
        return false;
    }
    if (auto* input = doc->GetElementById(id)) {
        if (auto* control = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(input)) {
            int selection_start = 0;
            int selection_end = 0;
            Rml::String selected_text;
            control->GetSelection(&selection_start, &selection_end, &selected_text);
            if (selection_start != selection_end) {
                return false;
            }

            const Rml::String rml_value(value);
            const int byte_offset = Rml::StringUtilities::ConvertCharacterOffsetToByteOffset(rml_value, selection_start);
            cursor_byte_position = byte_offset < 0
                ? 0
                : std::min(static_cast<size_t>(byte_offset), rml_value.size());
            return true;
        }
    }
    return false;
}

void append_output(AppState& state, std::string_view channel, std::string_view line)
{
    state.shell.append_output(channel, line);
}

void dispatch_managed_list_events(AppState& state)
{
    auto& host = nw::toolset::ui_v1_host();
    host.drain_events([&](const nw::toolset::UiListEvent& event) {
        const auto* callback = host.callback_ptr(
            event.type == nw::toolset::UiListEventType::scroll
                ? std::string_view{event.scroll.list_id}
                : std::string_view{event.selection.list_id},
            event.type);
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

bool activate_managed_list(Rml::ElementDocument* document,
    AppState& state,
    Rml::Element* hit)
{
    auto& host = nw::toolset::ui_v1_host();
    if (!nw::toolset::activate_managed_list_element(hit, host)) {
        return false;
    }
    dispatch_managed_list_events(state);
    refresh_smalls_elements(document, state);
    nw::toolset::sync_managed_lists(
        document, host, state.managed_lists, true);
    return true;
}

bool cycle_managed_list(Rml::ElementDocument* document,
    AppState& state,
    Rml::Element* element,
    int delta)
{
    auto& host = nw::toolset::ui_v1_host();
    if (!nw::toolset::cycle_managed_list_element(element, host, delta)) {
        return false;
    }
    dispatch_managed_list_events(state);
    refresh_smalls_elements(document, state);
    nw::toolset::sync_managed_lists(
        document, host, state.managed_lists, true);
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
    if (!doc) {
        return;
    }

    std::string markup;
    for (const auto& [style, line] : state.shell.terminal_lines) {
        markup += "<div class=\"terminal_line ";
        markup += escape_html(style);
        markup += "\">";
        markup += escape_html(line);
        markup += "</div>";
    }

    if (auto* lines = doc->GetElementById("terminal_output_lines")) {
        lines->SetInnerRML(markup);
    } else if (auto* output = doc->GetElementById("terminal_output")) {
        output->SetInnerRML(markup);
    }

    if (auto* output = doc->GetElementById("terminal_output")) {
        output->SetScrollTop(output->GetScrollHeight());
    }
}

void refresh_bottom_dock_view(Rml::ElementDocument* doc, AppState& state)
{
    if (!doc) {
        return;
    }

    const auto& bottom = state.shell.docks.pane(nw::toolset::DockRegion::bottom);
    const bool terminal_active = state.shell.terminal_visible();
    const bool output_active = state.shell.output_panel_visible();

    if (auto* dock = doc->GetElementById("bottom_dock")) {
        dock->SetClass("visible", bottom.visible);
        dock->SetClass("output_active", output_active);
        dock->SetClass("terminal_active", terminal_active);
    }
    if (auto* output = doc->GetElementById("output_panel")) {
        output->SetClass("visible", output_active);
    }
    if (auto* terminal = doc->GetElementById("terminal_panel")) {
        terminal->SetClass("visible", terminal_active);
    }
    if (auto* tab = doc->GetElementById("bottom_tab_output")) {
        tab->SetClass("active", output_active);
    }
    if (auto* tab = doc->GetElementById("bottom_tab_terminal")) {
        tab->SetClass("active", terminal_active);
    }

    apply_shell_layout(doc, state);

    if (terminal_active) {
        refresh_terminal_view(doc, state);
        if (auto* input = doc->GetElementById("terminal_input")) {
            input->Focus();
        }
    }
}

void refresh_output_view(Rml::ElementDocument* doc, AppState& state)
{
    if (!doc) {
        return;
    }

    const std::string filter = get_input_value(doc, "output_filter");
    const std::string lowered_filter = [&]() {
        std::string v = filter;
        std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return v;
    }();

    struct OutputViewRow {
        std::string_view channel;
        std::string_view text;
        size_t start = 0;
    };

    std::vector<OutputViewRow> rows;
    std::string text;
    bool has_rows = false;
    for (const auto& [channel, line] : state.shell.output_lines) {
        if (!state.shell.output_channel_visible(channel)) {
            continue;
        }

        std::string lowered_line = line;
        std::transform(lowered_line.begin(), lowered_line.end(), lowered_line.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!lowered_filter.empty() && lowered_line.find(lowered_filter) == std::string::npos) {
            continue;
        }

        size_t segment_start = 0;
        while (true) {
            const size_t segment_end = line.find('\n', segment_start);
            const size_t segment_size = segment_end == std::string::npos
                ? line.size() - segment_start
                : segment_end - segment_start;
            const std::string_view segment{line.data() + segment_start, segment_size};
            if (has_rows) {
                text.push_back('\n');
            }
            const size_t row_start = text.size();
            text.append(segment);
            rows.push_back(OutputViewRow{channel, segment, row_start});
            has_rows = true;

            if (segment_end == std::string::npos) {
                break;
            }
            segment_start = segment_end + 1;
        }
    }

    const bool preserves_offsets = text.size() >= state.output_selection.text.size()
        && text.compare(0, state.output_selection.text.size(), state.output_selection.text) == 0;
    if (!preserves_offsets) {
        state.output_selection.clear();
    }
    state.output_selection.text = std::move(text);
    state.output_selection.anchor = std::min(
        state.output_selection.anchor, state.output_selection.text.size());
    state.output_selection.focus = std::min(
        state.output_selection.focus, state.output_selection.text.size());

    const auto [selection_start, selection_end] = state.output_selection.range();
    std::string markup;
    for (const auto& row : rows) {
        markup += "<div class=\"output_line ";
        markup += escape_html(row.channel);
        markup += "\" data-output-start=\"";
        markup += std::to_string(row.start);
        markup += "\" data-output-length=\"";
        markup += std::to_string(row.text.size());
        markup += "\">";

        const size_t row_end = row.start + row.text.size();
        const size_t selected_start = std::clamp(selection_start, row.start, row_end);
        const size_t selected_end = std::clamp(selection_end, row.start, row_end);
        const size_t local_start = selected_start - row.start;
        const size_t local_end = selected_end - row.start;
        markup += escape_html(row.text.substr(0, local_start));
        if (local_start < local_end) {
            markup += "<span class=\"output_text_selection\">";
            markup += escape_html(row.text.substr(local_start, local_end - local_start));
            markup += "</span>";
        }
        markup += escape_html(row.text.substr(local_end));
        markup += "</div>";
    }

    if (auto* output = doc->GetElementById("output_list")) {
        const float scroll_top = output->GetScrollTop();
        const bool follow_tail = state.shell.output_follows_tail();

        if (auto* lines = doc->GetElementById("output_list_lines")) {
            lines->SetInnerRML(markup);
        }
        if (!follow_tail) {
            output->SetScrollTop(scroll_top);
        }
        state.output_scroll_after_layout = follow_tail
            ? OutputScrollAfterLayout::follow_tail
            : OutputScrollAfterLayout::observe;
    }

    const char* ids[] = {"output_info", "output_warn", "output_error", "output_script"};
    const char* values[] = {"info", "warn", "error", "script"};
    for (size_t i = 0; i < 4; ++i) {
        if (auto* btn = doc->GetElementById(ids[i])) {
            btn->SetClass("active", state.shell.output_channel_visible(values[i]));
        }
    }
}

void refresh_command_palette(Rml::ElementDocument* doc, AppState& state)
{
    if (!doc) {
        return;
    }

    const std::string query = get_input_value(doc, "command_input");
    state.commands = state.backend.list_commands(query);

    std::string markup;
    for (const auto& cmd : state.commands) {
        markup += "<div class=\"nw_list_row command_item\" data-key=\"";
        markup += escape_html(cmd.id);
        markup += "\">";
        markup += "<div class=\"nw_list_col nw_list_col_id\">" + escape_html(cmd.id) + "</div>";
        markup += "<div class=\"nw_list_col\">" + escape_html(cmd.title) + "</div>";
        markup += "<div class=\"nw_list_col nw_list_col_desc\">" + escape_html(cmd.description) + "</div>";
        markup += "</div>";
    }
    if (markup.empty()) {
        markup = "<div class=\"nw_list_empty\">No matching commands.</div>";
    }

    if (auto* list = doc->GetElementById("command_list")) {
        if (auto* items = doc->GetElementById("command_list_items")) {
            items->SetInnerRML(markup);
        } else {
            list->SetInnerRML(markup);
        }
    }
    if (auto* details = doc->GetElementById("command_details")) {
        details->SetInnerRML(state.commands.empty() ? "" : escape_html(state.commands.front().usage));
    }
}

void reset_project_tree_render_state(AppState& state)
{
    state.rendered_project_row_start = kInvalidVirtualIndex;
    state.rendered_project_row_end = kInvalidVirtualIndex;
    state.rendered_project_row_count = 0;
}

void append_project_tree_rows(AppState& state,
    const nw::toolset::ProjectTreeNode& node,
    int depth,
    bool force_expanded)
{
    const bool is_container = node.is_container();
    const bool collapsed = is_container
        && !force_expanded
        && state.collapsed_project_nodes.find(node.id) != state.collapsed_project_nodes.end();

    auto row = node;
    row.children.clear();
    state.project_rows.push_back(ProjectTreeRow{std::move(row), depth, collapsed});

    if (is_container && !collapsed) {
        for (const auto& child : node.children) {
            append_project_tree_rows(state, child, depth + 1, force_expanded);
        }
    }
}

VirtualRowWindow virtual_row_window_for(Rml::Element* list, size_t row_count)
{
    VirtualRowWindow window;
    if (!list || row_count == 0) {
        return window;
    }

    const float client_height = std::max(list->GetClientHeight(), list->GetOffsetHeight());
    const float scroll_top = std::max(0.0f, list->GetScrollTop());
    const size_t first_visible = std::min(row_count, static_cast<size_t>(scroll_top / kVirtualTreeRowHeightPx));
    const size_t visible_count = static_cast<size_t>(std::ceil(client_height / kVirtualTreeRowHeightPx)) + 1;
    window.start = first_visible > kVirtualTreeOverscanRows ? first_visible - kVirtualTreeOverscanRows : 0;
    window.end = std::min(row_count, first_visible + visible_count + kVirtualTreeOverscanRows);
    return window;
}

void append_project_tree_row_markup(const ProjectTreeRow& row,
    size_t row_index,
    std::string& markup)
{
    const auto& node = row.node;
    const bool is_container = node.is_container();
    const bool collapsed = is_container && row.collapsed;
    markup += "<div class=\"recent_row tree_row\"><div class=\"recent_item tree_item";
    switch (node.kind) {
    case nw::toolset::ProjectTreeNodeKind::directory:
        markup += " tree_directory";
        break;
    case nw::toolset::ProjectTreeNodeKind::area:
        markup += " tree_area";
        break;
    case nw::toolset::ProjectTreeNodeKind::resource:
        markup += " tree_resource";
        break;
    case nw::toolset::ProjectTreeNodeKind::file:
        markup += " tree_file";
        break;
    }
    if (!is_container) {
        markup += " tree_leaf";
    }
    if (collapsed) {
        markup += " collapsed";
    }
    markup += "\" data-key=\"";
    markup += std::to_string(row_index);
    markup += "\" style=\"padding-left: ";
    markup += std::to_string(6 + std::max(0, row.depth) * 12);
    markup += "px;\">";
    markup += "<span class=\"tree_twisty";
    markup += is_container ? (collapsed ? " collapsed" : " expanded") : " leaf";
    markup += "\"></span>";
    markup += "<span class=\"tree_label\">";
    markup += escape_html(node.label);
    markup += "</span>";
    if (!node.resource_type.empty() && node.kind != nw::toolset::ProjectTreeNodeKind::directory) {
        markup += "<span class=\"tree_meta\">";
        markup += escape_html(node.resource_type);
        markup += "</span>";
    }
    markup += "</div></div>";
}

bool render_project_tree_window(Rml::ElementDocument* doc, AppState& state, bool force)
{
    auto* list = find_el(doc, "recent_list");
    if (!list) {
        return false;
    }

    const size_t row_count = state.project_rows.size();
    const VirtualRowWindow window = virtual_row_window_for(list, row_count);
    if (!force
        && row_count == state.rendered_project_row_count
        && window.start == state.rendered_project_row_start
        && window.end == state.rendered_project_row_end) {
        return false;
    }

    const float scroll_top = list->GetScrollTop();
    std::string markup;
    if (row_count == 0) {
        markup = "<div class=\"nw_list_empty\">No results.</div>";
    } else {
        if (window.start > 0) {
            markup += "<div class=\"tree_spacer\" style=\"height: ";
            markup += std::to_string(static_cast<int>(std::lround(static_cast<float>(window.start) * kVirtualTreeRowHeightPx)));
            markup += "px;\"></div>";
        }

        for (size_t i = window.start; i < window.end; ++i) {
            append_project_tree_row_markup(state.project_rows[i], i, markup);
        }

        if (window.end < row_count) {
            markup += "<div class=\"tree_spacer\" style=\"height: ";
            markup += std::to_string(static_cast<int>(std::lround(static_cast<float>(row_count - window.end) * kVirtualTreeRowHeightPx)));
            markup += "px;\"></div>";
        }
    }

    list->SetInnerRML(markup);
    list->SetScrollTop(scroll_top);
    state.rendered_project_row_start = window.start;
    state.rendered_project_row_end = window.end;
    state.rendered_project_row_count = row_count;
    state.hovered_recent_index = -1;
    state.pressed_recent_index = -1;
    set_recent_selected(doc, state, state.selected_recent_index);
    return true;
}

void refresh_recent_list(Rml::ElementDocument* doc, AppState& state)
{
    if (!doc) {
        return;
    }

    const std::string query = get_input_value(doc, "recent_search");
    std::string markup;

    state.project_rows.clear();
    bool project_tree_rendered = false;

    if (state.shell.showing_project_tree) {
        reset_project_tree_render_state(state);
        const auto tree = state.backend.list_project_tree(query);
        if (tree.ok) {
            const bool force_expanded = !query.empty();
            for (const auto& child : tree.root.children) {
                append_project_tree_rows(state, child, 0, force_expanded);
            }
            if (state.selected_recent_index >= static_cast<int>(state.project_rows.size())) {
                state.selected_recent_index = -1;
            }
            render_project_tree_window(doc, state, true);
            project_tree_rendered = true;
        } else {
            markup = "<div class=\"nw_list_empty\">" + escape_html(tree.message) + "</div>";
        }
    } else if (state.shell.showing_areas) {
        reset_project_tree_render_state(state);
        state.areas = state.backend.list_areas(query);
        for (size_t i = 0; i < state.areas.size(); ++i) {
            const auto& area = state.areas[i];
            const std::string title = area.name.empty() ? area.resref : area.name;
            markup += "<div class=\"recent_row\"><div class=\"recent_item\" data-key=\"";
            markup += std::to_string(i);
            markup += "\"><div class=\"recent_title_plain\">" + escape_html(title) + "</div>";
            const std::string area_resref = area.resref.empty() ? std::string("(no resref)") : area.resref;
            markup += "<div class=\"recent_path\">" + escape_html(area_resref) + "</div></div></div>";
        }
    } else {
        reset_project_tree_render_state(state);
    }

    if (!project_tree_rendered && markup.empty()) {
        markup = state.shell.showing_areas
            ? "<div class=\"nw_list_empty\">No areas.</div>"
            : "";
    }

    if (!project_tree_rendered) {
        if (auto* list = doc->GetElementById("recent_list")) {
            list->SetInnerRML(markup);
            state.hovered_recent_index = -1;
            state.pressed_recent_index = -1;
            set_recent_selected(doc, state, state.selected_recent_index);
        }
    }

    if (auto* panel = doc->GetElementById("panel")) {
        panel->SetClass("area_mode", state.shell.showing_areas);
        panel->SetClass("project_mode", state.shell.showing_project_tree);
    }
    if (auto* areas_title = doc->GetElementById("title_areas")) {
        areas_title->SetClass("visible", state.shell.showing_areas);
    }
    if (auto* project_title = doc->GetElementById("title_project")) {
        project_title->SetClass("visible", state.shell.showing_project_tree);
        if (state.shell.showing_project_tree) {
            const auto project_dir = state.backend.current_project_dir();
            const std::string title = project_dir.empty() ? std::string{"Project"} : nw::toolset::project_display_name(project_dir);
            project_title->SetInnerRML(escape_html(title));
        }
    }

    apply_shell_layout(doc, state);
}

std::string workspace_tab_kind_class(nw::toolset::WorkspaceTabKind kind)
{
    switch (kind) {
    case nw::toolset::WorkspaceTabKind::home:
        return "home";
    case nw::toolset::WorkspaceTabKind::module:
        return "module";
    case nw::toolset::WorkspaceTabKind::project:
        return "project";
    case nw::toolset::WorkspaceTabKind::area:
        return "area";
    case nw::toolset::WorkspaceTabKind::preview:
        return "preview";
    case nw::toolset::WorkspaceTabKind::dialog:
        return "dialog";
    case nw::toolset::WorkspaceTabKind::resource:
        return "resource";
    case nw::toolset::WorkspaceTabKind::generic:
        break;
    }
    return "generic";
}

std::string workspace_tab_detail(const nw::toolset::WorkspaceTab& tab)
{
    switch (tab.kind) {
    case nw::toolset::WorkspaceTabKind::home:
        return "Recent Projects";
    case nw::toolset::WorkspaceTabKind::area:
        return !tab.detail.empty() ? tab.detail : std::string{"No area selected"};
    case nw::toolset::WorkspaceTabKind::preview:
        return !tab.detail.empty() ? tab.detail : (tab.id.rfind("preview:", 0) == 0 ? tab.id.substr(8) : tab.id);
    case nw::toolset::WorkspaceTabKind::dialog:
        return !tab.detail.empty() ? tab.detail : (tab.id.rfind("dialog:", 0) == 0 ? tab.id.substr(7) : tab.id);
    case nw::toolset::WorkspaceTabKind::module:
        return tab.id.rfind("module:", 0) == 0 ? tab.id.substr(7) : tab.id;
    case nw::toolset::WorkspaceTabKind::project:
        return tab.id.rfind("project:", 0) == 0 ? tab.id.substr(8) : tab.id;
    case nw::toolset::WorkspaceTabKind::resource:
        return !tab.detail.empty() ? tab.detail : (tab.id.rfind("resource:", 0) == 0 ? tab.id.substr(9) : tab.id);
    case nw::toolset::WorkspaceTabKind::generic:
        break;
    }
    return tab.id;
}

std::string workspace_tab_icon(const nw::toolset::WorkspaceTab& tab)
{
    switch (tab.kind) {
    case nw::toolset::WorkspaceTabKind::module:
        return "mod";
    case nw::toolset::WorkspaceTabKind::project:
        return "prj";
    case nw::toolset::WorkspaceTabKind::area:
        return {};
    case nw::toolset::WorkspaceTabKind::preview:
        return "3d";
    case nw::toolset::WorkspaceTabKind::dialog:
        return "dlg";
    case nw::toolset::WorkspaceTabKind::resource: {
        std::string name = workspace_tab_detail(tab);
        const auto json_suffix = name.rfind(".json");
        if (json_suffix != std::string::npos && json_suffix + 5 == name.size()) {
            name.erase(json_suffix);
        }
        const auto dot = name.find_last_of('.');
        if (dot != std::string::npos && dot + 1 < name.size()) {
            std::string ext = name.substr(dot + 1);
            for (char& ch : ext) {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            if (ext.size() > 4) {
                ext.resize(4);
            }
            return ext;
        }
        return "res";
    }
    case nw::toolset::WorkspaceTabKind::generic:
        return "tab";
    case nw::toolset::WorkspaceTabKind::home:
        break;
    }
    return {};
}

std::string workspace_home_tab_icon_markup()
{
    return "<span class=\"workspace_tab_graphic workspace_tab_home_graphic\">"
           "<span class=\"home_icon_cabinet\"></span>"
           "<span class=\"home_icon_drawer home_icon_drawer_1\"></span>"
           "<span class=\"home_icon_drawer home_icon_drawer_2\"></span>"
           "<span class=\"home_icon_label home_icon_label_1\"></span>"
           "<span class=\"home_icon_label home_icon_label_2\"></span>"
           "<span class=\"home_icon_handle home_icon_handle_1\"></span>"
           "<span class=\"home_icon_handle home_icon_handle_2\"></span>"
           "<span class=\"home_icon_base\"></span>"
           "</span>";
}

std::string workspace_area_tab_icon_markup()
{
    return "<span class=\"workspace_tab_graphic workspace_tab_area_graphic\">"
           "<span class=\"area_icon_panel area_icon_panel_1\"></span>"
           "<span class=\"area_icon_panel area_icon_panel_2\"></span>"
           "<span class=\"area_icon_panel area_icon_panel_3\"></span>"
           "<span class=\"area_icon_path area_icon_path_1\"></span>"
           "<span class=\"area_icon_path area_icon_path_2\"></span>"
           "<span class=\"area_icon_path area_icon_path_3\"></span>"
           "<span class=\"area_icon_dot area_icon_dot_1\"></span>"
           "<span class=\"area_icon_dot area_icon_dot_2\"></span>"
           "</span>";
}

void append_workspace_subtabs_markup(std::string& content_markup, const nw::toolset::WorkspaceTab& active_tab)
{
    if (active_tab.subtabs.empty()) {
        return;
    }

    std::string active_subtab_id;
    if (active_tab.active_subtab_index && *active_tab.active_subtab_index < active_tab.subtabs.size()) {
        active_subtab_id = active_tab.subtabs[*active_tab.active_subtab_index].id;
    }

    content_markup += "<div class=\"workspace_subtabs\">";
    for (const auto& subtab : active_tab.subtabs) {
        content_markup += "<div class=\"workspace_subtab";
        if (subtab.id == active_subtab_id) {
            content_markup += " active";
        }
        if (subtab.closable) {
            content_markup += " closable";
        }
        content_markup += "\" data-tab=\"";
        content_markup += escape_html(active_tab.id);
        content_markup += "\" data-subtab=\"";
        content_markup += escape_html(subtab.id);
        content_markup += "\"><span class=\"workspace_subtab_title\">";
        content_markup += escape_html(subtab.title);
        content_markup += "</span>";
        if (subtab.closable) {
            content_markup += "<div class=\"workspace_subtab_close\" data-tab=\"";
            content_markup += escape_html(active_tab.id);
            content_markup += "\" data-subtab=\"";
            content_markup += escape_html(subtab.id);
            content_markup += "\"><span class=\"workspace_subtab_close_glyph\">x</span></div>";
        }
        content_markup += "</div>";
    }
    content_markup += "</div>";
}

std::optional<nw::toolset::ResourceDocument> resource_document_for_tab(const AppState& state,
    const nw::toolset::WorkspaceTab& active_tab)
{
    if (active_tab.kind != nw::toolset::WorkspaceTabKind::resource
        && active_tab.kind != nw::toolset::WorkspaceTabKind::preview
        && active_tab.kind != nw::toolset::WorkspaceTabKind::area) {
        return std::nullopt;
    }

    const auto project_dir = state.backend.current_project_dir();
    if (project_dir.empty()) {
        return std::nullopt;
    }

    const std::string resource_path = active_tab.detail.empty() ? workspace_tab_detail(active_tab) : active_tab.detail;
    if (resource_path.empty()) {
        return std::nullopt;
    }
    return nw::toolset::load_project_resource_document(project_dir, resource_path);
}

void append_resource_document_diagnostics(std::string& content_markup, const nw::toolset::ResourceDocument& document)
{
    if (document.diagnostics.empty()) {
        return;
    }

    content_markup += "<div class=\"resource_inspector_diagnostics\">";
    for (const auto& diagnostic : document.diagnostics) {
        content_markup += "<div class=\"resource_inspector_diagnostic resource_inspector_diagnostic_";
        content_markup += escape_html(nw::toolset::resource_document_diagnostic_severity_label(diagnostic.severity));
        content_markup += "\"><span class=\"resource_inspector_diagnostic_level\">";
        content_markup += escape_html(nw::toolset::resource_document_diagnostic_severity_label(diagnostic.severity));
        content_markup += "</span><span class=\"resource_inspector_diagnostic_message\">";
        content_markup += escape_html(diagnostic.message);
        content_markup += "</span></div>";
    }
    content_markup += "</div>";
}

void append_resource_document_properties(std::string& content_markup, const nw::toolset::ResourceDocument& document)
{
    std::string current_group;
    bool group_open = false;

    for (const auto& property : document.properties) {
        if (property.group != current_group) {
            if (group_open) {
                content_markup += "</div>";
            }
            current_group = property.group;
            group_open = true;
            content_markup += "<div class=\"resource_property_group\"><div class=\"resource_property_group_title\">";
            content_markup += escape_html(current_group);
            content_markup += "</div>";
        }

        content_markup += "<div class=\"resource_property_row\"><div class=\"resource_property_name\">";
        content_markup += escape_html(property.name);
        content_markup += "</div><div class=\"resource_property_value\">";
        content_markup += escape_html(property.value);
        content_markup += "</div></div>";
    }

    if (group_open) {
        content_markup += "</div>";
    }
}

void append_resource_document_inspector(std::string& content_markup,
    const nw::toolset::ResourceDocument& document,
    bool compact)
{
    content_markup += "<div class=\"resource_inspector";
    if (compact) {
        content_markup += " compact";
    }
    if (!document.ok) {
        content_markup += " error";
    }
    content_markup += "\">";

    content_markup += "<div class=\"resource_inspector_header\"><div class=\"resource_inspector_title\">";
    content_markup += escape_html(document.title.empty() ? std::string{"Resource"} : document.title);
    content_markup += "</div><div class=\"resource_inspector_detail\">";
    content_markup += escape_html(document.detail.empty() ? document.relative_path.generic_string() : document.detail);
    content_markup += "</div><div class=\"resource_inspector_badges\"><span class=\"resource_inspector_badge\">";
    content_markup += escape_html(nw::toolset::resource_document_kind_label(document.kind));
    content_markup += "</span>";
    if (!document.resource_type.empty()) {
        content_markup += "<span class=\"resource_inspector_badge\">";
        content_markup += escape_html(document.resource_type);
        content_markup += "</span>";
    }
    if (!document.format.empty()) {
        content_markup += "<span class=\"resource_inspector_badge\">";
        content_markup += escape_html(document.format);
        content_markup += "</span>";
    }
    content_markup += "</div></div>";

    if (!document.ok) {
        content_markup += "<div class=\"resource_inspector_empty\">";
        content_markup += escape_html(document.message.empty() ? std::string{"Unable to load resource document."} : document.message);
        content_markup += "</div>";
    }

    append_resource_document_diagnostics(content_markup, document);
    append_resource_document_properties(content_markup, document);
    content_markup += "</div>";
}

void append_missing_resource_document(std::string& content_markup)
{
    content_markup += "<div class=\"resource_inspector error\"><div class=\"resource_inspector_header\">";
    content_markup += "<div class=\"resource_inspector_title\">Resource</div>";
    content_markup += "<div class=\"resource_inspector_detail\">No project resource selected</div></div>";
    content_markup += "<div class=\"resource_inspector_empty\">Open a project resource to inspect it.</div></div>";
}

void ensure_active_dialog_document(AppState& state)
{
    const auto* active_tab = state.workspace.active_tab();
    if (!active_tab || active_tab->kind != nw::toolset::WorkspaceTabKind::dialog) {
        if (!state.dialog_view.tab_id.empty()) {
            nw::toolset::clear_dialog_view(state.dialog_view);
        }
        return;
    }

    const auto project_dir = state.backend.current_project_dir();
    const auto source_path = project_dir / active_tab->detail;
    if (state.dialog_view.tab_id == active_tab->id
        && state.dialog_view.document.source_path == source_path
        && state.dialog_view.document.status != nw::toolset::DialogDocumentStatus::empty) {
        return;
    }

    nw::toolset::load_dialog_view(state.dialog_view, source_path, active_tab->id);
}

class ObjectDetailsListAdapter final : public nw::toolset::VirtualListAdapter {
public:
    explicit ObjectDetailsListAdapter(
        const nw::toolset::ObjectDetailsSnapshot& snapshot)
        : snapshot_{snapshot}
    {
    }

    [[nodiscard]] int size() const override
    {
        return static_cast<int>(snapshot_.rows.size());
    }

    [[nodiscard]] std::string_view row_extra_classes() const override
    {
        return "object_details_row";
    }

    [[nodiscard]] std::string render_row_inner(int index, bool /*selected*/) const override
    {
        if (index < 0 || static_cast<size_t>(index) >= snapshot_.rows.size()) {
            return {};
        }

        const auto& row = snapshot_.rows[static_cast<size_t>(index)];
        const bool section = row.kind
            == nw::toolset::ObjectDetailsRowKind::section;
        std::string markup;
        markup.reserve(256);
        markup += "<div class=\"property_tree_cells";
        if (section) {
            markup += " section";
        } else if ((index & 1) != 0) {
            markup += " alternate";
        }
        markup += "\"><div class=\"property_tree_name\" style=\"padding-left:";
        markup += section ? "8px;\">" : "22px;\">";
        markup += "<span class=\"tree_twisty leaf\"></span><span class=\"property_tree_label\">";
        markup += escape_html(snapshot_.text_view(row.label));
        markup += "</span></div><div class=\"property_tree_value";
        const auto value = snapshot_.text_view(row.value);
        if (!section && value.empty()) {
            markup += " empty";
        }
        markup += "\">";
        if (!section) {
            if (row.editor == nw::toolset::ObjectDetailsEditorKind::boolean) {
                markup += "<span class=\"object_details_boolean\" data-row=\"";
                markup += std::to_string(index);
                markup += "\" data-current=\"";
                markup += std::to_string(row.edit_value);
                markup += "\"><span class=\"object_details_boolean_box";
                if (row.edit_value != 0) {
                    markup += " checked";
                }
                markup += "\">";
                if (row.edit_value != 0) {
                    markup += "&#10003;";
                }
                markup += "</span><span class=\"object_details_boolean_text\">";
                markup += escape_html(value);
                markup += "</span></span>";
            } else if (row.editor == nw::toolset::ObjectDetailsEditorKind::integer) {
                markup += "<span class=\"object_details_integer_spinner\"><button type=\"button\" "
                          "class=\"object_details_integer_step\" data-row=\"";
                markup += std::to_string(index);
                markup += "\" data-current=\"";
                markup += std::to_string(row.edit_value);
                markup += "\" data-delta=\"-1\" title=\"Decrease; minimum ";
                markup += std::to_string(row.edit_min);
                markup += "\"";
                if (row.edit_value <= row.edit_min) {
                    markup += " disabled";
                }
                markup += ">-</button><input class=\"object_details_integer\" type=\"text\" maxlength=\"11\" data-row=\"";
                markup += std::to_string(index);
                markup += "\" data-current=\"";
                markup += std::to_string(row.edit_value);
                markup += "\" data-min=\"";
                markup += std::to_string(row.edit_min);
                markup += "\" data-max=\"";
                markup += std::to_string(row.edit_max);
                markup += "\" value=\"";
                markup += std::to_string(row.edit_value);
                markup += "\" title=\"Valid range: ";
                markup += std::to_string(row.edit_min);
                markup += " to ";
                markup += std::to_string(row.edit_max);
                markup += ". Use Up/Down to adjust; press Enter to apply.\"/><button type=\"button\" "
                          "class=\"object_details_integer_step\" data-row=\"";
                markup += std::to_string(index);
                markup += "\" data-current=\"";
                markup += std::to_string(row.edit_value);
                markup += "\" data-delta=\"1\" title=\"Increase; maximum ";
                markup += std::to_string(row.edit_max);
                markup += "\"";
                if (row.edit_value >= row.edit_max) {
                    markup += " disabled";
                }
                markup += ">+</button></span>";
            } else {
                markup += escape_html(value.empty() ? std::string_view{"Not set"} : value);
            }
        }
        markup += "</div></div>";
        return markup;
    }

private:
    const nw::toolset::ObjectDetailsSnapshot& snapshot_;
};

class ObjectVariableListAdapter final : public nw::toolset::VirtualListAdapter {
public:
    explicit ObjectVariableListAdapter(
        const nw::toolset::ObjectVariableSnapshot& snapshot)
        : snapshot_{snapshot}
    {
    }

    [[nodiscard]] int size() const override
    {
        return static_cast<int>(snapshot_.rows.size());
    }

    [[nodiscard]] std::string_view row_extra_classes() const override
    {
        return "object_variable_row";
    }

    [[nodiscard]] std::string render_row_inner(int index, bool /*selected*/) const override
    {
        if (index < 0 || static_cast<size_t>(index) >= snapshot_.rows.size()) {
            return {};
        }

        const auto& snapshot_row = snapshot_.rows[static_cast<size_t>(index)];
        const auto& row = snapshot_row.variable;
        const std::string type = std::to_string(static_cast<uint32_t>(row.type));
        const bool duplicate = nw::toolset::has_object_variable_warning(
            snapshot_row.warnings, nw::toolset::ObjectVariableWarning::duplicate_name);
        const bool looks_integer = nw::toolset::has_object_variable_warning(
            snapshot_row.warnings, nw::toolset::ObjectVariableWarning::string_looks_integer);
        const bool looks_floating = nw::toolset::has_object_variable_warning(
            snapshot_row.warnings, nw::toolset::ObjectVariableWarning::string_looks_floating);
        const std::string value = nw::toolset::format_object_variable_value(row);
        std::string markup;
        markup.reserve(512 + row.name.size() + row.string.size()
            + (row.type == nw::toolset::ObjectVariableType::string ? 0 : value.size()));
        markup += "<div class=\"object_variable_cells\"><div class=\"object_variable_field object_variable_name_field";
        if (duplicate) {
            markup += " warning_duplicate";
        }
        markup += "\">";
        if (duplicate) {
            const auto warning = nw::toolset::object_variable_warning_description(
                nw::toolset::ObjectVariableWarning::duplicate_name);
            markup += "<span class=\"object_variable_field_warning\" title=\"";
            markup += escape_html(warning);
            markup += "\" data-tooltip=\"";
            markup += escape_html(warning);
            markup += "\">!</span>";
        }
        markup += "<input class=\"object_variable_name\" type=\"text\" data-name=\"";
        markup += escape_html(row.name);
        markup += "\" data-type=\"";
        markup += type;
        markup += "\" value=\"";
        markup += escape_html(row.name);
        markup += "\" title=\"Press Enter to rename\"/></div><button type=\"button\" "
                  "class=\"object_variable_type\" data-name=\"";
        markup += escape_html(row.name);
        markup += "\" data-type=\"";
        markup += type;
        markup += "\" title=\"Click to change type\">";
        markup += nw::toolset::object_variable_type_name(row.type);
        markup += "</button><div class=\"object_variable_field object_variable_value_field";
        if (looks_integer || looks_floating) {
            markup += " warning_type";
        }
        markup += "\">";
        if (looks_integer || looks_floating) {
            const auto warning = looks_integer
                ? nw::toolset::ObjectVariableWarning::string_looks_integer
                : nw::toolset::ObjectVariableWarning::string_looks_floating;
            markup += "<span class=\"object_variable_field_warning\" title=\"";
            const auto description = nw::toolset::object_variable_warning_description(warning);
            markup += escape_html(description);
            markup += "\" data-tooltip=\"";
            markup += escape_html(description);
            markup += "\">!</span>";
        }
        markup += "<input class=\"object_variable_value\" type=\"text\" data-name=\"";
        markup += escape_html(row.name);
        markup += "\" data-type=\"";
        markup += type;
        if (row.type != nw::toolset::ObjectVariableType::string) {
            markup += "\" data-last-valid=\"";
            markup += escape_html(value);
        }
        markup += "\" value=\"";
        markup += escape_html(value);
        markup += "\" title=\"Press Enter to apply\"/></div><button type=\"button\" "
                  "class=\"object_variable_remove\" data-name=\"";
        markup += escape_html(row.name);
        markup += "\" data-type=\"";
        markup += type;
        markup += "\" title=\"Remove variable\">&#215;</button></div>";
        return markup;
    }

private:
    const nw::toolset::ObjectVariableSnapshot& snapshot_;
};

bool active_tab_has_object_workbench(const nw::toolset::WorkspaceTab* active_tab)
{
    return active_tab
        && (active_tab->kind == nw::toolset::WorkspaceTabKind::preview
            || active_tab->kind == nw::toolset::WorkspaceTabKind::area
            || active_tab->kind == nw::toolset::WorkspaceTabKind::home);
}

bool active_object_details_matches_tab(const AppState& state)
{
    const auto* active_tab = state.workspace.active_tab();
    return active_tab_has_object_workbench(active_tab)
        && state.active_object_tab_id == active_tab->id
        && state.object_details.status == nw::toolset::ObjectDetailsStatus::ready;
}

bool active_object_matches_tab(const AppState& state)
{
    const auto* active_tab = state.workspace.active_tab();
    return active_tab_has_object_workbench(active_tab)
        && state.active_object_tab_id == active_tab->id
        && state.object_details.object.type != nw::ObjectType::invalid;
}

bool active_creature_class_presentation_matches_tab(const AppState& state)
{
    const auto* active_tab = state.workspace.active_tab();
    return active_tab_has_object_workbench(active_tab)
        && state.active_object_tab_id == active_tab->id
        && state.creature_class_presentation.object == state.object_details.object
        && state.creature_class_presentation.status == nw::toolset::ObjectDetailsStatus::ready;
}

size_t active_details_row_count(const AppState& state)
{
    return active_object_details_matches_tab(state) ? state.object_details.rows.size() : 0;
}

bool active_object_variables_match_tab(const AppState& state)
{
    const auto* active_tab = state.workspace.active_tab();
    return active_tab_has_object_workbench(active_tab)
        && state.active_object_tab_id == active_tab->id
        && state.object_variables.object == state.object_details.object
        && state.object_variables.status
        == nw::toolset::ObjectVariableSnapshotStatus::ready;
}

void configure_object_variable_list(AppState& state)
{
    if (state.object_variable_list_configured) {
        return;
    }
    state.object_variable_list.set_row_height(kObjectVariableRowHeightPx);
    state.object_variable_list.set_overscan(kObjectVariableOverscanRows);
    state.object_variable_list_configured = true;
}

void invalidate_object_variable_render(AppState& state)
{
    configure_object_variable_list(state);
    const size_t row_count = state.object_variables.status
            == nw::toolset::ObjectVariableSnapshotStatus::ready
        ? state.object_variables.rows.size()
        : 0;
    state.object_variable_list.set_total_rows(static_cast<int>(row_count));
    state.object_variables_rendered = false;
}

void configure_details_list(AppState& state)
{
    if (state.details_list_configured) {
        return;
    }
    state.details_list.set_row_height(kObjectDetailsRowHeightPx);
    state.details_list.set_overscan(kObjectDetailsOverscanRows);
    state.details_list_configured = true;
}

void invalidate_details_render(AppState& state)
{
    configure_details_list(state);
    const auto* snapshot = &state.object_details;
    const size_t row_count = snapshot->status == nw::toolset::ObjectDetailsStatus::ready
        ? snapshot->rows.size()
        : 0;
    state.details_list.set_total_rows(static_cast<int>(row_count));
    state.details_rendered = false;
}

void rebuild_active_object_details(AppState& state, nw::ObjectHandle object)
{
    auto& runtime = nw::kernel::runtime();
    nw::toolset::build_object_details(runtime, object, state.object_details);
    if (object.type == nw::ObjectType::creature) {
        nw::toolset::build_creature_class_presentation(
            runtime, object, state.creature_class_presentation);
        if (state.creature_class_presentation.status
            == nw::toolset::ObjectDetailsStatus::invalid_data) {
            LOG_F(WARNING, "rollnw-client: %s",
                state.creature_class_presentation.diagnostic.c_str());
        }
    } else {
        state.creature_class_presentation = {};
    }
    if (state.object_details.status == nw::toolset::ObjectDetailsStatus::invalid_data) {
        LOG_F(WARNING, "rollnw-client: %s", state.object_details.diagnostic.c_str());
    }
    nw::toolset::snapshot_object_variables(object, state.object_variables);
    if (state.object_variables.status
        == nw::toolset::ObjectVariableSnapshotStatus::invalid_data) {
        LOG_F(WARNING, "rollnw-client: %s",
            state.object_variables.diagnostic.c_str());
    }
    invalidate_details_render(state);
    invalidate_object_variable_render(state);
}

void clear_active_creature_feats(AppState& state);
void clear_active_creature_spells(AppState& state);
void clear_active_creature_inventory(AppState& state);
void clear_active_appearances(AppState& state);
void clear_body_part_options(AppState& state);
void clear_color_editor(AppState& state);

void clear_active_object_details(AppState& state)
{
    state.object_details = {};
    state.object_variables = {};
    state.creature_class_presentation = {};
    configure_details_list(state);
    state.details_list.set_total_rows(0);
    state.details_list.set_scroll_top(0);
    state.details_rendered = false;
    configure_object_variable_list(state);
    state.object_variable_list.set_total_rows(0);
    state.object_variable_list.set_scroll_top(0);
    state.object_variables_rendered = false;
    clear_active_creature_feats(state);
    clear_active_creature_spells(state);
    clear_active_creature_inventory(state);
    clear_active_appearances(state);
    state.object_workbench_surface = ObjectWorkbenchSurface::details;
}

bool sync_object_variable_window(Rml::ElementDocument* doc, AppState& state, bool force)
{
    if (state.object_workbench_surface != ObjectWorkbenchSurface::variables) {
        return false;
    }
    auto* list = find_el(doc, "object_variable_rows");
    if (!list) {
        return false;
    }

    configure_object_variable_list(state);
    const int viewport_height = std::max(1,
        static_cast<int>(std::lround(
            std::max(list->GetClientHeight(), list->GetOffsetHeight()))));
    const int scroll_top = std::max(
        0, static_cast<int>(std::lround(list->GetScrollTop())));
    state.object_variable_list.set_viewport_height(viewport_height);
    state.object_variable_list.set_scroll_top(scroll_top);
    const auto range = state.object_variable_list.compute_range();
    const int row_count = active_object_variables_match_tab(state)
        ? static_cast<int>(state.object_variables.rows.size())
        : 0;
    if (!force && state.object_variables_rendered
        && row_count == state.rendered_object_variable_row_count
        && range.start == state.rendered_object_variable_range.start
        && range.end == state.rendered_object_variable_range.end) {
        return false;
    }

    std::string markup;
    if (active_object_matches_tab(state)
        && state.object_variables.status
            != nw::toolset::ObjectVariableSnapshotStatus::ready) {
        markup = "<div class=\"property_tree_empty error\">";
        markup += escape_html(state.object_variables.diagnostic.empty()
                ? std::string_view{"Object variable data is unavailable."}
                : std::string_view{state.object_variables.diagnostic});
        markup += "</div>";
    } else if (!active_object_variables_match_tab(state)) {
        markup = "<div class=\"property_tree_empty\">Waiting for a live object.</div>";
    } else if (state.object_variables.rows.empty()) {
        markup = "<div class=\"property_tree_empty\">No variables.</div>";
    } else {
        markup = nw::toolset::render_virtual_list(
            state.object_variable_list,
            ObjectVariableListAdapter{state.object_variables});
    }

    list->SetInnerRML(markup);
    list->SetScrollTop(static_cast<float>(scroll_top));
    if (auto* count = find_el(doc, "object_variable_count")) {
        count->SetInnerRML(std::to_string(row_count));
    }
    state.rendered_object_variable_range = range;
    state.rendered_object_variable_row_count = row_count;
    state.object_variables_rendered = true;
    return true;
}

bool sync_active_module_object(AppState& state)
{
    const auto* active_tab = state.workspace.active_tab();
    if (!active_tab || active_tab->kind != nw::toolset::WorkspaceTabKind::home) {
        return false;
    }

    const nw::ObjectHandle object = state.backend.module_object();
    if (object.type != nw::ObjectType::module) {
        if (state.active_object_tab_id == active_tab->id) {
            state.smalls.clear_active_object();
            state.active_object_tab_id.clear();
            clear_active_object_details(state);
        }
        return false;
    }

    const bool changed = state.object_details.object != object
        || state.active_object_tab_id != active_tab->id
        || state.object_details.status != nw::toolset::ObjectDetailsStatus::ready;
    state.smalls.publish_active_object(object);
    state.smalls.clear_active_area();
    state.active_object_tab_id = active_tab->id;
    if (!changed) {
        return true;
    }

    clear_active_object_details(state);
    configure_details_list(state);
    state.details_list.set_scroll_top(0);
    rebuild_active_object_details(state, object);
    state.observed_object_mutation_epoch = nw::toolset::object_mutation_state().epoch;
    return true;
}

bool sync_object_details_window(Rml::ElementDocument* doc, AppState& state, bool force)
{
    const bool variable_changed = sync_object_variable_window(doc, state, force);
    if (state.object_workbench_surface != ObjectWorkbenchSurface::details) {
        return variable_changed;
    }
    auto* list = find_el(doc, "property_tree_rows");
    if (!list) {
        return false;
    }

    configure_details_list(state);
    const int viewport_height = std::max(1,
        static_cast<int>(std::lround(std::max(list->GetClientHeight(), list->GetOffsetHeight()))));
    const int scroll_top = std::max(0, static_cast<int>(std::lround(list->GetScrollTop())));
    state.details_list.set_viewport_height(viewport_height);
    state.details_list.set_scroll_top(scroll_top);
    const auto range = state.details_list.compute_range();
    const int row_count = static_cast<int>(active_details_row_count(state));

    if (!force && state.details_rendered
        && row_count == state.rendered_details_row_count
        && range.start == state.rendered_details_range.start
        && range.end == state.rendered_details_range.end) {
        return false;
    }

    const auto& snapshot = state.object_details;
    std::string markup;
    if (active_object_matches_tab(state)
        && snapshot.status != nw::toolset::ObjectDetailsStatus::ready) {
        markup = "<div class=\"property_tree_empty error\">";
        markup += escape_html(snapshot.diagnostic.empty()
                ? std::string_view{"Live object Details are unavailable."}
                : std::string_view{snapshot.diagnostic});
        markup += "</div>";
    } else if (!active_object_details_matches_tab(state)) {
        markup = "<div class=\"property_tree_empty\">Waiting for the live preview object.</div>";
    } else if (snapshot.rows.empty()) {
        markup = "<div class=\"property_tree_empty\">No Details available.</div>";
    } else {
        markup = nw::toolset::render_virtual_list(
            state.details_list,
            ObjectDetailsListAdapter{snapshot});
    }

    list->SetInnerRML(markup);
    list->SetScrollTop(static_cast<float>(scroll_top));
    if (auto* count = find_el(doc, "property_tree_count")) {
        count->SetInnerRML(std::to_string(active_details_row_count(state)));
    }
    state.rendered_details_range = range;
    state.rendered_details_row_count = row_count;
    state.details_rendered = true;
    return true;
}

class CreatureFeatListAdapter final : public nw::toolset::VirtualListAdapter {
public:
    explicit CreatureFeatListAdapter(const nw::toolset::CreatureFeatViewSnapshot& snapshot)
        : snapshot_{snapshot}
    {
    }

    [[nodiscard]] int size() const override
    {
        return static_cast<int>(snapshot_.rows.size());
    }

    [[nodiscard]] int row_key(int index) const override
    {
        return static_cast<int>(snapshot_.rows[static_cast<size_t>(index)].feat_id);
    }

    [[nodiscard]] std::string_view row_extra_classes() const override
    {
        return "creature_feat_row";
    }

    [[nodiscard]] std::string render_row_inner(int index, bool /*selected*/) const override
    {
        if (index < 0 || static_cast<size_t>(index) >= snapshot_.rows.size()) {
            return {};
        }

        const auto& row = snapshot_.rows[static_cast<size_t>(index)];
        std::string markup;
        markup.reserve(160);
        markup += "<div class=\"creature_feat_cells";
        if (row.assigned) {
            markup += " assigned";
        }
        markup += "\"><span class=\"creature_feat_name\">";
        markup += escape_html(snapshot_.text_view(row.name));
        markup += "</span></div>";
        return markup;
    }

private:
    const nw::toolset::CreatureFeatViewSnapshot& snapshot_;
};

bool active_creature_feats_match_tab(const AppState& state)
{
    const auto* active_tab = state.workspace.active_tab();
    return active_tab_has_object_workbench(active_tab)
        && state.active_object_tab_id == active_tab->id
        && state.creature_feats.status == nw::toolset::CreatureFeatViewStatus::ready;
}

bool active_creature_feat_object_matches_tab(const AppState& state)
{
    const auto* active_tab = state.workspace.active_tab();
    return active_tab_has_object_workbench(active_tab)
        && state.active_object_tab_id == active_tab->id
        && state.creature_feats.object.type == nw::ObjectType::creature;
}

void configure_creature_feat_list(AppState& state)
{
    if (state.creature_feat_list_configured) {
        return;
    }
    state.creature_feat_list.set_row_height(kCreatureFeatRowHeightPx);
    state.creature_feat_list.set_overscan(kCreatureFeatOverscanRows);
    state.creature_feat_list_configured = true;
}

void invalidate_creature_feat_render(AppState& state)
{
    configure_creature_feat_list(state);
    state.creature_feat_list.set_total_rows(static_cast<int>(state.creature_feats.rows.size()));
    state.creature_feat_rendered = false;
}

void rebuild_active_creature_feats(AppState& state, nw::ObjectHandle object)
{
    nw::toolset::build_creature_feat_rows(
        nw::kernel::runtime(), object, state.creature_feat_query, state.creature_feats);
    invalidate_creature_feat_render(state);
}

void clear_active_creature_feats(AppState& state)
{
    state.creature_feats = {};
    configure_creature_feat_list(state);
    state.creature_feat_list.set_total_rows(0);
    state.creature_feat_list.set_scroll_top(0);
    state.creature_feat_rendered = false;
}

bool sync_creature_feat_window(Rml::ElementDocument* doc, AppState& state, bool force)
{
    auto* list = find_el(doc, "creature_feat_rows");
    if (!list) {
        return false;
    }

    configure_creature_feat_list(state);
    const int viewport_height = std::max(1,
        static_cast<int>(std::lround(std::max(list->GetClientHeight(), list->GetOffsetHeight()))));
    const int scroll_top = std::max(0, static_cast<int>(std::lround(list->GetScrollTop())));
    state.creature_feat_list.set_viewport_height(viewport_height);
    state.creature_feat_list.set_scroll_top(scroll_top);
    const auto range = state.creature_feat_list.compute_range();
    const int row_count = static_cast<int>(state.creature_feats.rows.size());
    if (!force && state.creature_feat_rendered
        && row_count == state.rendered_creature_feat_row_count
        && range.start == state.rendered_creature_feat_range.start
        && range.end == state.rendered_creature_feat_range.end) {
        return false;
    }

    std::string markup;
    if (active_creature_feat_object_matches_tab(state)
        && state.creature_feats.status != nw::toolset::CreatureFeatViewStatus::ready) {
        markup = "<div class=\"property_tree_empty error\">";
        markup += escape_html(state.creature_feats.diagnostic.empty()
                ? std::string_view{"Creature feat data is unavailable."}
                : std::string_view{state.creature_feats.diagnostic});
        markup += "</div>";
    } else if (!active_creature_feats_match_tab(state)) {
        markup = "<div class=\"property_tree_empty\">Waiting for a live Creature.</div>";
    } else if (state.creature_feats.rows.empty()) {
        markup = "<div class=\"property_tree_empty\">No feats match this filter.</div>";
    } else {
        markup = nw::toolset::render_virtual_list(
            state.creature_feat_list, CreatureFeatListAdapter{state.creature_feats});
    }

    list->SetInnerRML(markup);
    list->SetScrollTop(static_cast<float>(scroll_top));
    if (auto* count = find_el(doc, "creature_feat_count")) {
        count->SetInnerRML(active_creature_feats_match_tab(state)
                ? std::to_string(state.creature_feats.rows.size())
                : std::string{"0"});
    }
    state.rendered_creature_feat_range = range;
    state.rendered_creature_feat_row_count = row_count;
    state.creature_feat_rendered = true;
    return true;
}

class CreatureSpellListAdapter final : public nw::toolset::VirtualListAdapter {
public:
    CreatureSpellListAdapter(const nw::toolset::CreatureSpellViewSnapshot& snapshot,
        const std::vector<uint32_t>& matches)
        : snapshot_{snapshot}
        , matches_{matches}
    {
    }

    [[nodiscard]] int size() const override
    {
        return static_cast<int>(matches_.size());
    }

    [[nodiscard]] int row_key(int index) const override
    {
        const auto row_index = matches_[static_cast<size_t>(index)];
        return snapshot_.rows[row_index].spell_id;
    }

    [[nodiscard]] std::string_view row_extra_classes() const override
    {
        return "creature_spell_row";
    }

    [[nodiscard]] std::string render_row_inner(int index, bool /*selected*/) const override
    {
        if (index < 0 || static_cast<size_t>(index) >= matches_.size()) {
            return {};
        }
        const auto row_index = matches_[static_cast<size_t>(index)];
        if (row_index >= snapshot_.rows.size()) {
            return {};
        }

        const auto& row = snapshot_.rows[row_index];
        std::string markup;
        markup.reserve(320);
        markup += "<div class=\"creature_spell_cells";
        if (snapshot_.memorizes ? row.uses > 0 : row.known) {
            markup += " assigned";
        }
        markup += "\"><span class=\"creature_spell_name\">";
        markup += escape_html(snapshot_.text_view(row.name));
        markup += "</span><span class=\"creature_spell_level\">";
        markup += std::to_string(row.level);
        markup += "</span>";
        if (snapshot_.memorizes) {
            markup += "<span class=\"quantity_stepper\"><button type=\"button\" "
                      "class=\"creature_spell_decrement\" data-spell=\"";
            markup += std::to_string(row.spell_id);
            markup += "\"";
            if (row.uses == 0) {
                markup += " disabled";
            }
            markup += ">-</button><span>";
            markup += std::to_string(row.uses);
            markup += "</span><button type=\"button\" class=\"creature_spell_increment\" "
                      "data-spell=\"";
            markup += std::to_string(row.spell_id);
            markup += "\">+</button></span>";
        } else {
            markup += "<span class=\"creature_spell_known\">";
            if (row.known) {
                markup += "Known";
            }
            markup += "</span>";
        }
        markup += "</div>";
        return markup;
    }

private:
    const nw::toolset::CreatureSpellViewSnapshot& snapshot_;
    const std::vector<uint32_t>& matches_;
};

bool active_creature_spells_match_tab(const AppState& state)
{
    const auto* active_tab = state.workspace.active_tab();
    return active_tab_has_object_workbench(active_tab)
        && state.active_object_tab_id == active_tab->id
        && state.creature_spells.status == nw::toolset::CreatureSpellViewStatus::ready;
}

bool active_creature_spell_object_matches_tab(const AppState& state)
{
    const auto* active_tab = state.workspace.active_tab();
    return active_tab_has_object_workbench(active_tab)
        && state.active_object_tab_id == active_tab->id
        && state.creature_spells.object.type == nw::ObjectType::creature;
}

bool active_creature_spell_filter_matches_tab(const AppState& state)
{
    return active_creature_spells_match_tab(state)
        && state.object_workbench_surface == ObjectWorkbenchSurface::spells
        && state.creature_spell_filter_field != CreatureSpellFilterField::none
        && state.creature_spell_combobox.is_active();
}

void clear_creature_spell_filter(AppState& state)
{
    state.creature_spell_combobox.close();
    state.creature_spell_filter_field = CreatureSpellFilterField::none;
    state.creature_spell_popup_placement.reset();
}

void configure_creature_spell_list(AppState& state)
{
    if (state.creature_spell_list_configured) {
        return;
    }
    state.creature_spell_list.set_row_height(kCreatureSpellRowHeightPx);
    state.creature_spell_list.set_overscan(kCreatureSpellOverscanRows);
    state.creature_spell_list_configured = true;
}

void filter_active_creature_spells(AppState& state)
{
    nw::toolset::filter_creature_spell_rows(state.creature_spells,
        state.creature_spell_query,
        state.creature_spell_level,
        state.creature_spell_matches);
    configure_creature_spell_list(state);
    state.creature_spell_list.set_total_rows(
        static_cast<int>(state.creature_spell_matches.size()));
    state.creature_spell_rendered = false;
}

void rebuild_active_creature_spells(AppState& state,
    nw::ObjectHandle object,
    int32_t selected_class = -1,
    int32_t selected_metamagic = -1)
{
    clear_creature_spell_filter(state);
    nw::toolset::build_creature_spell_rows(nw::kernel::runtime(),
        object,
        selected_class,
        selected_metamagic,
        state.creature_spells);
    filter_active_creature_spells(state);
}

void clear_active_creature_spells(AppState& state)
{
    clear_creature_spell_filter(state);
    state.creature_spells = {};
    state.creature_spell_matches.clear();
    configure_creature_spell_list(state);
    state.creature_spell_list.set_total_rows(0);
    state.creature_spell_list.set_scroll_top(0);
    state.creature_spell_rendered = false;
}

std::optional<CreatureSpellFilterField> creature_spell_filter_field_from_name(
    std::string_view value) noexcept
{
    if (value == "class") {
        return CreatureSpellFilterField::class_;
    }
    if (value == "level") {
        return CreatureSpellFilterField::level;
    }
    if (value == "metamagic") {
        return CreatureSpellFilterField::metamagic;
    }
    return std::nullopt;
}

bool open_creature_spell_filter(AppState& state, CreatureSpellFilterField field)
{
    if (!active_creature_spells_match_tab(state)
        || field == CreatureSpellFilterField::none) {
        clear_creature_spell_filter(state);
        return false;
    }

    std::vector<nw::toolset::VirtualComboBoxItem> options;
    int32_t selected = -1;
    if (field == CreatureSpellFilterField::class_) {
        options.reserve(state.creature_spells.classes.size());
        for (const auto& choice : state.creature_spells.classes) {
            options.push_back({
                .key = choice.value,
                .label = std::string{state.creature_spells.text_view(choice.name)},
            });
        }
        selected = state.creature_spells.selected_class;
    } else if (field == CreatureSpellFilterField::level) {
        options.reserve(11);
        options.push_back({.key = -1, .label = "All"});
        for (int32_t level = 0; level <= 9; ++level) {
            options.push_back({.key = level, .label = std::to_string(level)});
        }
        selected = state.creature_spell_level;
    } else {
        options.reserve(state.creature_spells.metamagic.size());
        for (const auto& choice : state.creature_spells.metamagic) {
            options.push_back({
                .key = choice.value,
                .label = std::string{state.creature_spells.text_view(choice.name)},
            });
        }
        selected = state.creature_spells.selected_metamagic;
    }

    if (!state.creature_spell_combobox.open(std::move(options), selected)) {
        clear_creature_spell_filter(state);
        return false;
    }
    state.creature_spell_filter_field = field;
    state.creature_spell_popup_placement.reset();
    return true;
}

bool commit_creature_spell_filter(AppState& state, int32_t value)
{
    if (!active_creature_spell_filter_matches_tab(state)) {
        return false;
    }

    int32_t selected_class = state.creature_spells.selected_class;
    int32_t selected_metamagic = state.creature_spells.selected_metamagic;
    if (state.creature_spell_filter_field == CreatureSpellFilterField::class_) {
        const auto choice = std::ranges::find(
            state.creature_spells.classes, value, &nw::toolset::CreatureSpellChoice::value);
        if (choice == state.creature_spells.classes.end()) {
            return false;
        }
        selected_class = value;
    } else if (state.creature_spell_filter_field == CreatureSpellFilterField::level) {
        if (value < -1 || value > 9) {
            return false;
        }
        state.creature_spell_level = value;
    } else {
        const auto choice = std::ranges::find(
            state.creature_spells.metamagic, value, &nw::toolset::CreatureSpellChoice::value);
        if (choice == state.creature_spells.metamagic.end()) {
            return false;
        }
        selected_metamagic = value;
    }

    if (state.creature_spell_filter_field == CreatureSpellFilterField::level) {
        filter_active_creature_spells(state);
    } else {
        rebuild_active_creature_spells(state,
            state.creature_spells.object,
            selected_class,
            selected_metamagic);
    }
    state.creature_spell_list.set_scroll_top(0);
    clear_creature_spell_filter(state);
    return true;
}

bool sync_creature_spell_window(Rml::ElementDocument* doc, AppState& state, bool force)
{
    auto* list = find_el(doc, "creature_spell_rows");
    if (!list) {
        return false;
    }

    configure_creature_spell_list(state);
    const int viewport_height = std::max(1,
        static_cast<int>(std::lround(std::max(list->GetClientHeight(), list->GetOffsetHeight()))));
    const int scroll_top = std::max(0, static_cast<int>(std::lround(list->GetScrollTop())));
    state.creature_spell_list.set_viewport_height(viewport_height);
    state.creature_spell_list.set_scroll_top(scroll_top);
    const auto range = state.creature_spell_list.compute_range();
    const int row_count = static_cast<int>(state.creature_spell_matches.size());
    if (!force && state.creature_spell_rendered
        && row_count == state.rendered_creature_spell_row_count
        && range.start == state.rendered_creature_spell_range.start
        && range.end == state.rendered_creature_spell_range.end) {
        return false;
    }

    std::string markup;
    if (active_creature_spell_object_matches_tab(state)
        && state.creature_spells.status != nw::toolset::CreatureSpellViewStatus::ready) {
        markup = "<div class=\"property_tree_empty error\">";
        markup += escape_html(state.creature_spells.diagnostic.empty()
                ? std::string_view{"Creature spell data is unavailable."}
                : std::string_view{state.creature_spells.diagnostic});
        markup += "</div>";
    } else if (!active_creature_spells_match_tab(state)) {
        markup = "<div class=\"property_tree_empty\">Waiting for a live Creature.</div>";
    } else if (state.creature_spells.classes.empty()) {
        markup = "<div class=\"property_tree_empty\">";
        markup += escape_html(state.creature_spells.diagnostic.empty()
                ? std::string_view{"Creature has no spellcasting classes."}
                : std::string_view{state.creature_spells.diagnostic});
        markup += "</div>";
    } else if (state.creature_spells.rows.empty()) {
        markup = "<div class=\"property_tree_empty\">Selected class has no configured spells.</div>";
    } else if (state.creature_spell_matches.empty()) {
        markup = "<div class=\"property_tree_empty\">No spells match this filter.</div>";
    } else {
        markup = nw::toolset::render_virtual_list(state.creature_spell_list,
            CreatureSpellListAdapter{state.creature_spells, state.creature_spell_matches});
    }

    list->SetInnerRML(markup);
    list->SetScrollTop(static_cast<float>(scroll_top));
    if (auto* count = find_el(doc, "creature_spell_count")) {
        count->SetInnerRML(active_creature_spells_match_tab(state)
                ? std::to_string(state.creature_spell_matches.size())
                : std::string{"0"});
    }
    if (auto* value_header = find_el(doc, "creature_spell_value_header")) {
        value_header->SetInnerRML(state.creature_spells.memorizes ? "Uses" : "Known");
    }
    state.rendered_creature_spell_range = range;
    state.rendered_creature_spell_row_count = row_count;
    state.creature_spell_rendered = true;
    return true;
}

bool sync_creature_spell_filter_window(
    Rml::ElementDocument* doc, AppState& state, bool force)
{
    auto* list = find_el(doc, "creature_spell_filter_options");
    if (!list || !active_creature_spell_filter_matches_tab(state)
        || !state.creature_spell_combobox.popup_visible()) {
        return false;
    }

    const int viewport_height = std::max(1,
        static_cast<int>(std::lround(std::max(list->GetClientHeight(), list->GetOffsetHeight()))));
    const int observed_scroll_top = std::max(0,
        static_cast<int>(std::lround(list->GetScrollTop())));
    auto update = state.creature_spell_combobox.update(
        viewport_height, observed_scroll_top, force);
    if (update.replace_markup) {
        list->SetInnerRML(update.markup);
    }
    if (update.set_scroll) {
        list->SetScrollTop(static_cast<float>(update.scroll_top));
    }

    bool positioned = false;
    auto* field = find_el(doc, "active_creature_spell_filter_field");
    auto* workbench = find_el(doc, "object_workbench");
    if (field && workbench) {
        const nw::toolset::VirtualComboBoxRect anchor{
            .x = static_cast<int>(std::lround(field->GetAbsoluteLeft() - workbench->GetAbsoluteLeft())),
            .y = static_cast<int>(std::lround(field->GetAbsoluteTop() - workbench->GetAbsoluteTop())),
            .width = static_cast<int>(std::lround(field->GetOffsetWidth())),
            .height = static_cast<int>(std::lround(field->GetOffsetHeight())),
        };
        const nw::toolset::VirtualComboBoxRect bounds{
            .width = static_cast<int>(std::lround(workbench->GetClientWidth())),
            .height = static_cast<int>(std::lround(workbench->GetClientHeight())),
        };
        const auto placement = state.creature_spell_combobox.place_popup(anchor, bounds);
        if (placement.width > 0 && placement.height > 0
            && (!state.creature_spell_popup_placement
                || *state.creature_spell_popup_placement != placement)) {
            list->SetProperty("left", std::to_string(placement.left) + "px");
            list->SetProperty("top", std::to_string(placement.top) + "px");
            list->SetProperty("width", std::to_string(placement.width) + "px");
            list->SetProperty("height", std::to_string(placement.height) + "px");
            state.creature_spell_popup_placement = placement;
            positioned = true;
        }
    }
    return update.replace_markup || update.set_scroll || positioned;
}

bool active_creature_inventory_matches_tab(const AppState& state)
{
    const auto* active_tab = state.workspace.active_tab();
    return active_tab_has_object_workbench(active_tab)
        && state.active_object_tab_id == active_tab->id
        && state.creature_inventory.status == nw::toolset::InventoryViewStatus::ready;
}

bool active_creature_inventory_object_matches_tab(const AppState& state)
{
    const auto* active_tab = state.workspace.active_tab();
    return active_tab_has_object_workbench(active_tab)
        && state.active_object_tab_id == active_tab->id
        && (state.creature_inventory.object.type == nw::ObjectType::creature
            || state.creature_inventory.object.type == nw::ObjectType::item);
}

void rebuild_active_creature_inventory(AppState& state, nw::ObjectHandle object)
{
    nw::toolset::build_object_inventory_rows(
        nw::kernel::runtime(), object, state.item_icon_cache, state.creature_inventory);
    if (state.creature_inventory_page < 0
        || state.creature_inventory_page >= state.creature_inventory.page_count) {
        state.creature_inventory_page = 0;
    }
    if (state.creature_inventory_selection < 0
        || static_cast<size_t>(state.creature_inventory_selection)
            >= state.creature_inventory.inventory.size()) {
        state.creature_inventory_selection = -1;
    }
    state.creature_inventory_rendered = false;
}

void clear_active_creature_inventory(AppState& state)
{
    state.creature_inventory = {};
    state.creature_inventory_page = 0;
    state.creature_inventory_selection = -1;
    state.creature_inventory_rendered = false;
}

std::string render_creature_inventory_page(const AppState& state)
{
    std::string markup;
    if (active_creature_inventory_object_matches_tab(state)
        && state.creature_inventory.status != nw::toolset::InventoryViewStatus::ready) {
        markup = "<div class=\"property_tree_empty error\">";
        markup += escape_html(state.creature_inventory.diagnostic.empty()
                ? std::string_view{"Inventory data is unavailable."}
                : std::string_view{state.creature_inventory.diagnostic});
        markup += "</div>";
    } else if (!active_creature_inventory_matches_tab(state)) {
        markup = "<div class=\"property_tree_empty\">Waiting for a live object inventory.</div>";
    } else {
        markup.reserve(9000);
        const int board_width = state.creature_inventory.column_count * kCreatureInventoryCellPx;
        const int board_height = state.creature_inventory.row_count * kCreatureInventoryCellPx;
        markup += "<div id=\"creature_inventory_board\" class=\"creature_inventory_board\" style=\"flex-basis:";
        markup += std::to_string(board_width);
        markup += "px;width:";
        markup += std::to_string(board_width);
        markup += "px;height:";
        markup += std::to_string(board_height);
        markup += "px\">";
        const int cell_count = state.creature_inventory.row_count
            * state.creature_inventory.column_count;
        for (int index = 0; index < cell_count; ++index) {
            markup += "<span class=\"creature_inventory_cell\"></span>";
        }
        markup += "<span id=\"creature_inventory_drop_target\" "
                  "class=\"creature_inventory_drop_target\"></span>";
        for (const auto& row : state.creature_inventory.inventory) {
            if (row.page != state.creature_inventory_page) {
                continue;
            }
            const int top = (row.row - row.height + 1) * kCreatureInventoryCellPx;
            const int left = row.column * kCreatureInventoryCellPx;
            const int width = row.width * kCreatureInventoryCellPx;
            const int height = row.height * kCreatureInventoryCellPx;
            auto label = state.creature_inventory.text_view(row.name);
            if (label.empty()) {
                label = state.creature_inventory.text_view(row.resref);
            }
            markup += "<div class=\"creature_inventory_item";
            if (state.creature_inventory_selection >= 0
                && row.source_index
                    == static_cast<uint32_t>(state.creature_inventory_selection)) {
                markup += " selected";
            }
            markup += "\" data-key=\"";
            markup += std::to_string(row.source_index);
            markup += "\" title=\"";
            markup += escape_html(label);
            markup += "\" style=\"left:";
            markup += std::to_string(left);
            markup += "px;top:";
            markup += std::to_string(top);
            markup += "px;width:";
            markup += std::to_string(width);
            markup += "px;height:";
            markup += std::to_string(height);
            markup += "px\">";
            const auto icon_source = state.creature_inventory.text_view(row.icon_source);
            if (!icon_source.empty()) {
                markup += "<img class=\"creature_inventory_item_icon\" src=\"";
                markup += escape_html(icon_source);
                markup += "\"/>";
            } else {
                markup += "<span class=\"creature_inventory_item_label\">";
                markup += escape_html(label);
                markup += "</span>";
            }
            if (row.infinite || row.stack_size > 1) {
                markup += "<span class=\"creature_inventory_stack\">";
                markup += row.infinite ? "*" : std::to_string(row.stack_size);
                markup += "</span>";
            }
            markup += "</div>";
        }
        markup += "</div><div class=\"creature_inventory_pages\">";
        for (int page = 0; page < state.creature_inventory.page_count; ++page) {
            markup += "<button class=\"creature_inventory_page";
            if (page == state.creature_inventory_page) {
                markup += " active";
            }
            markup += "\" data-page=\"";
            markup += std::to_string(page);
            markup += "\" title=\"Inventory page ";
            markup += std::to_string(page + 1);
            markup += "\">";
            markup += std::to_string(page + 1);
            markup += "</button>";
        }
        markup += "</div>";
    }
    return markup;
}

bool sync_creature_inventory_window(Rml::ElementDocument* doc, AppState& state, bool force)
{
    auto* surface = find_el(doc, "creature_inventory_page_surface");
    if (!surface || (!force && state.creature_inventory_rendered)) {
        return false;
    }

    surface->SetInnerRML(render_creature_inventory_page(state));
    if (auto* count = find_el(doc, "creature_inventory_count")) {
        count->SetInnerRML(active_creature_inventory_matches_tab(state)
                ? std::to_string(state.creature_inventory.inventory.size())
                : std::string{"0"});
    }
    state.creature_inventory_rendered = true;
    return true;
}

std::optional<nw::toolset::AppearanceCatalogKind> appearance_catalog_kind(nw::ObjectType type)
{
    if (type == nw::ObjectType::creature) {
        return nw::toolset::AppearanceCatalogKind::creature;
    }
    if (type == nw::ObjectType::placeable) {
        return nw::toolset::AppearanceCatalogKind::placeable;
    }
    return std::nullopt;
}

nw::toolset::AppearanceCatalog& appearance_catalog(
    AppState& state, nw::toolset::AppearanceCatalogKind kind)
{
    switch (kind) {
    case nw::toolset::AppearanceCatalogKind::creature:
        return state.creature_appearance_catalog;
    case nw::toolset::AppearanceCatalogKind::placeable:
        return state.placeable_appearance_catalog;
    case nw::toolset::AppearanceCatalogKind::wing:
        return state.wing_appearance_catalog;
    case nw::toolset::AppearanceCatalogKind::tail:
        return state.tail_appearance_catalog;
    }
    std::abort();
}

const nw::toolset::AppearanceCatalog& appearance_catalog(
    const AppState& state, nw::toolset::AppearanceCatalogKind kind)
{
    switch (kind) {
    case nw::toolset::AppearanceCatalogKind::creature:
        return state.creature_appearance_catalog;
    case nw::toolset::AppearanceCatalogKind::placeable:
        return state.placeable_appearance_catalog;
    case nw::toolset::AppearanceCatalogKind::wing:
        return state.wing_appearance_catalog;
    case nw::toolset::AppearanceCatalogKind::tail:
        return state.tail_appearance_catalog;
    }
    std::abort();
}

nw::toolset::AppearanceCatalogKind appearance_catalog_kind(
    const AppState& state, AppearanceEditorField field)
{
    if (field == AppearanceEditorField::wings) {
        return nw::toolset::AppearanceCatalogKind::wing;
    }
    if (field == AppearanceEditorField::tail) {
        return nw::toolset::AppearanceCatalogKind::tail;
    }
    return state.appearance_object.type == nw::ObjectType::placeable
        ? nw::toolset::AppearanceCatalogKind::placeable
        : nw::toolset::AppearanceCatalogKind::creature;
}

const nw::toolset::AppearanceCatalog& active_appearance_catalog(const AppState& state)
{
    return appearance_catalog(
        state, appearance_catalog_kind(state, state.appearance_editor_field));
}

std::optional<int32_t> appearance_editor_value(
    const AppState& state, AppearanceEditorField field)
{
    if (field == AppearanceEditorField::appearance) {
        return nw::toolset::object_appearance(
            nw::kernel::runtime(), state.appearance_object);
    }
    if (state.appearance_object.type != nw::ObjectType::creature) {
        return std::nullopt;
    }
    const auto values = nw::toolset::editable_creature_accessories(
        nw::kernel::runtime(), state.appearance_object);
    const size_t index = field == AppearanceEditorField::wings ? 0 : 1;
    if (index >= values.size()) {
        return std::nullopt;
    }
    return values[index];
}

void reset_appearance_catalogs_for_module(AppState& state)
{
    const uint64_t generation = state.backend.module_generation();
    if (generation == state.appearance_catalog_generation) {
        return;
    }

    state.creature_appearance_catalog = {
        .kind = nw::toolset::AppearanceCatalogKind::creature,
    };
    state.placeable_appearance_catalog = {
        .kind = nw::toolset::AppearanceCatalogKind::placeable,
    };
    state.wing_appearance_catalog = {
        .kind = nw::toolset::AppearanceCatalogKind::wing,
    };
    state.tail_appearance_catalog = {
        .kind = nw::toolset::AppearanceCatalogKind::tail,
    };
    state.appearance_catalog_generation = generation;
    state.appearance_query.clear();
    state.appearance_matches.clear();
    state.appearance_object = nw::ObjectHandle{};
    state.appearance_editor_scroll_top = 0.0f;
    state.appearance_selector_open = false;
    state.appearance_editor_field = AppearanceEditorField::appearance;
    clear_body_part_options(state);
    clear_color_editor(state);
}

void configure_appearance_list(AppState& state)
{
    if (state.appearance_list_configured) {
        return;
    }
    state.appearance_list.set_row_height(kAppearanceRowHeightPx);
    state.appearance_list.set_overscan(kAppearanceOverscanRows);
    state.appearance_list_configured = true;
}

void clear_body_part_options(AppState& state)
{
    state.body_part_combobox.close();
    state.body_part_option_object = nw::ObjectHandle{};
    state.body_part_option_part = -1;
    state.body_part_popup_placement.reset();
}

void clear_color_editor(AppState& state)
{
    state.color_editor_object = nw::ObjectHandle{};
    state.color_editor_channel = -1;
}

void close_appearance_selector(AppState& state)
{
    state.appearance_selector_open = false;
    state.appearance_editor_field = AppearanceEditorField::appearance;
    state.appearance_query.clear();
}

void select_live_appearance(AppState& state)
{
    int selected = -1;
    const auto current = appearance_editor_value(
        state, state.appearance_editor_field);
    if (current) {
        const auto& catalog = active_appearance_catalog(state);
        for (size_t index = 0; index < state.appearance_matches.size(); ++index) {
            const uint32_t row_index = state.appearance_matches[index];
            if (row_index < catalog.rows.size() && catalog.rows[row_index].id == *current) {
                selected = static_cast<int>(index);
                break;
            }
        }
    }
    state.appearance_list.set_selected(selected);
}

void rebuild_active_appearances(AppState& state, nw::ObjectHandle object)
{
    configure_appearance_list(state);
    reset_appearance_catalogs_for_module(state);
    const auto kind = appearance_catalog_kind(object.type);
    if (!kind) {
        clear_active_appearances(state);
        return;
    }
    state.appearance_object = object;

    auto& catalog = appearance_catalog(state, *kind);
    if (catalog.status == nw::toolset::AppearanceCatalogStatus::empty) {
        (void)nw::toolset::build_appearance_catalog(nw::kernel::runtime(), *kind, catalog);
    }
    if (object.type == nw::ObjectType::creature) {
        for (const auto accessory_kind : {
                 nw::toolset::AppearanceCatalogKind::wing,
                 nw::toolset::AppearanceCatalogKind::tail,
             }) {
            auto& accessory_catalog = appearance_catalog(state, accessory_kind);
            if (accessory_catalog.status == nw::toolset::AppearanceCatalogStatus::empty) {
                (void)nw::toolset::build_appearance_catalog(
                    nw::kernel::runtime(), accessory_kind, accessory_catalog);
            }
        }
    } else {
        state.appearance_editor_field = AppearanceEditorField::appearance;
    }

    const auto& active_catalog = active_appearance_catalog(state);
    nw::toolset::filter_appearance_catalog(
        active_catalog, state.appearance_query, state.appearance_matches);
    state.appearance_list.set_total_rows(static_cast<int>(state.appearance_matches.size()));
    select_live_appearance(state);
    state.appearance_scroll_to_selection = true;
    state.appearance_rendered = false;
}

void clear_active_appearances(AppState& state)
{
    clear_body_part_options(state);
    clear_color_editor(state);
    state.appearance_query.clear();
    state.appearance_matches.clear();
    state.appearance_object = nw::ObjectHandle{};
    state.appearance_editor_scroll_top = 0.0f;
    configure_appearance_list(state);
    state.appearance_list.set_total_rows(0);
    state.appearance_list.set_scroll_top(0);
    state.appearance_rendered = false;
    state.appearance_scroll_to_selection = false;
    state.appearance_selector_open = false;
    state.appearance_editor_field = AppearanceEditorField::appearance;
}

bool active_appearances_match_tab(const AppState& state)
{
    const auto* active_tab = state.workspace.active_tab();
    return active_tab_has_object_workbench(active_tab)
        && state.active_object_tab_id == active_tab->id
        && state.appearance_object == state.object_details.object
        && appearance_catalog_kind(state.appearance_object.type).has_value();
}

bool active_body_part_options_match_tab(const AppState& state)
{
    return active_appearances_match_tab(state)
        && state.object_workbench_surface == ObjectWorkbenchSurface::appearance
        && state.body_part_option_object == state.object_details.object
        && state.body_part_option_part >= 0
        && state.body_part_combobox.is_active();
}

bool active_color_editor_matches_tab(const AppState& state)
{
    return active_appearances_match_tab(state)
        && state.object_workbench_surface == ObjectWorkbenchSurface::appearance
        && state.color_editor_object == state.object_details.object
        && state.color_editor_channel >= 0;
}

std::string_view creature_color_palette_asset(int32_t palette) noexcept
{
    if (palette == 0) {
        return "mvpal_skin.png";
    }
    if (palette == 1) {
        return "mvpal_hair.png";
    }
    return {};
}

bool open_color_editor(AppState& state, nw::ObjectHandle object, uint32_t color)
{
    const auto rows = nw::toolset::creature_color_editor_rows(
        nw::kernel::runtime(), object);
    const auto row = std::ranges::find(rows, color,
        &nw::toolset::CreatureColorEditorRow::color);
    if (row == rows.end() || row->value < 0
        || row->value >= kPltPaletteColumns * kPltPaletteRows
        || creature_color_palette_asset(row->palette).empty()) {
        clear_color_editor(state);
        return false;
    }

    state.color_editor_object = object;
    state.color_editor_channel = static_cast<int32_t>(color);
    return true;
}

bool sync_live_body_part_option(AppState& state)
{
    if (!active_body_part_options_match_tab(state)) {
        return false;
    }

    const auto values = nw::toolset::editable_creature_body_parts(
        nw::kernel::runtime(), state.body_part_option_object);
    const auto part = static_cast<size_t>(state.body_part_option_part);
    if (part >= values.size()
        || !state.body_part_combobox.select_key(values[part])) {
        clear_body_part_options(state);
        return false;
    }
    return true;
}

bool open_body_part_options(
    AppState& state, nw::ObjectHandle object, uint32_t part, int32_t current)
{
    auto rows = nw::toolset::creature_body_part_option_rows(
        nw::kernel::runtime(), object, part);
    std::vector<nw::toolset::VirtualComboBoxItem> options;
    options.reserve(rows.size());
    for (auto& row : rows) {
        options.push_back({
            .key = row.key,
            .label = std::move(row.label),
            .detail = std::move(row.detail),
        });
    }
    if (!state.body_part_combobox.open(std::move(options), current)) {
        clear_body_part_options(state);
        return false;
    }

    state.body_part_option_object = object;
    state.body_part_option_part = static_cast<int32_t>(part);
    state.body_part_popup_placement.reset();
    return true;
}

class AppearanceListAdapter final : public nw::toolset::VirtualListAdapter {
public:
    AppearanceListAdapter(const nw::toolset::AppearanceCatalog& catalog,
        const std::vector<uint32_t>& matches)
        : catalog_{catalog}
        , matches_{matches}
    {
    }

    [[nodiscard]] int size() const override
    {
        return static_cast<int>(matches_.size());
    }

    [[nodiscard]] int row_key(int index) const override
    {
        return row(index).id;
    }

    [[nodiscard]] std::string_view row_extra_classes() const override
    {
        return "appearance_row";
    }

    [[nodiscard]] std::string render_row_inner(int index, bool /*selected*/) const override
    {
        const auto& value = row(index);
        std::string markup;
        markup.reserve(value.name.size() + 96);
        markup += "<div class=\"appearance_name\">";
        markup += escape_html(value.name);
        markup += "</div><div class=\"appearance_id\">";
        markup += std::to_string(value.id);
        markup += "</div>";
        return markup;
    }

private:
    [[nodiscard]] const nw::toolset::AppearanceCatalogRow& row(int index) const
    {
        return catalog_.rows[matches_[static_cast<size_t>(index)]];
    }

    const nw::toolset::AppearanceCatalog& catalog_;
    const std::vector<uint32_t>& matches_;
};

bool sync_appearance_window(Rml::ElementDocument* doc, AppState& state, bool force)
{
    auto* list = find_el(doc, "appearance_rows");
    if (!list) {
        return false;
    }

    configure_appearance_list(state);
    const int viewport_height = std::max(1,
        static_cast<int>(std::lround(std::max(list->GetClientHeight(), list->GetOffsetHeight()))));
    const int observed_scroll_top = std::max(0,
        static_cast<int>(std::lround(list->GetScrollTop())));
    int scroll_top = observed_scroll_top;
    state.appearance_list.set_viewport_height(viewport_height);
    state.appearance_list.set_scroll_top(scroll_top);
    bool request_scroll = false;
    if (state.appearance_scroll_to_selection) {
        scroll_top = state.appearance_list.scroll_top_for_index(state.appearance_list.selected());
        state.appearance_list.set_scroll_top(scroll_top);
        request_scroll = scroll_top != observed_scroll_top;
        state.appearance_scroll_to_selection = request_scroll;
    }
    const auto range = state.appearance_list.compute_range();
    const int row_count = static_cast<int>(state.appearance_matches.size());
    const bool stable_markup = !force && state.appearance_rendered
        && row_count == state.rendered_appearance_row_count
        && range.start == state.rendered_appearance_range.start
        && range.end == state.rendered_appearance_range.end;
    if (stable_markup) {
        if (request_scroll) {
            list->SetScrollTop(static_cast<float>(scroll_top));
        }
    } else {
        std::string markup;
        const auto& catalog = active_appearance_catalog(state);
        if (!active_appearances_match_tab(state)) {
            markup = "<div class=\"property_tree_empty\">Waiting for a live Creature or Placeable.</div>";
        } else if (catalog.status != nw::toolset::AppearanceCatalogStatus::ready) {
            markup = "<div class=\"property_tree_empty error\">";
            markup += escape_html(catalog.diagnostic.empty()
                    ? std::string_view{"Appearance data is unavailable."}
                    : std::string_view{catalog.diagnostic});
            markup += "</div>";
        } else if (state.appearance_matches.empty()) {
            markup = "<div class=\"property_tree_empty\">No appearances match this filter.</div>";
        } else {
            markup = nw::toolset::render_virtual_list(
                state.appearance_list, AppearanceListAdapter{catalog, state.appearance_matches});
        }

        list->SetInnerRML(markup);
        list->SetScrollTop(static_cast<float>(scroll_top));
        state.rendered_appearance_range = range;
        state.rendered_appearance_row_count = row_count;
        state.appearance_rendered = true;
    }

    return !stable_markup || request_scroll;
}

bool sync_body_part_option_window(Rml::ElementDocument* doc, AppState& state, bool force)
{
    auto* list = find_el(doc, "body_part_option_rows");
    if (!list || !active_body_part_options_match_tab(state)
        || !state.body_part_combobox.popup_visible()) {
        return false;
    }

    const int viewport_height = std::max(1,
        static_cast<int>(std::lround(std::max(list->GetClientHeight(), list->GetOffsetHeight()))));
    const int observed_scroll_top = std::max(0,
        static_cast<int>(std::lround(list->GetScrollTop())));
    auto update = state.body_part_combobox.update(
        viewport_height, observed_scroll_top, force);
    if (update.replace_markup) {
        list->SetInnerRML(update.markup);
    }
    if (update.set_scroll) {
        list->SetScrollTop(static_cast<float>(update.scroll_top));
    }

    bool positioned = false;
    auto* field = find_el(doc, "active_body_part_field");
    auto* workbench = find_el(doc, "object_workbench");
    if (field && workbench) {
        const nw::toolset::VirtualComboBoxRect anchor{
            .x = static_cast<int>(std::lround(field->GetAbsoluteLeft() - workbench->GetAbsoluteLeft())),
            .y = static_cast<int>(std::lround(field->GetAbsoluteTop() - workbench->GetAbsoluteTop())),
            .width = static_cast<int>(std::lround(field->GetOffsetWidth())),
            .height = static_cast<int>(std::lround(field->GetOffsetHeight())),
        };
        const nw::toolset::VirtualComboBoxRect bounds{
            .width = static_cast<int>(std::lround(workbench->GetClientWidth())),
            .height = static_cast<int>(std::lround(workbench->GetClientHeight())),
        };
        const auto placement = state.body_part_combobox.place_popup(anchor, bounds);
        if (placement.width > 0 && placement.height > 0
            && (!state.body_part_popup_placement
                || *state.body_part_popup_placement != placement)) {
            list->SetProperty("left", std::to_string(placement.left) + "px");
            list->SetProperty("top", std::to_string(placement.top) + "px");
            list->SetProperty("width", std::to_string(placement.width) + "px");
            list->SetProperty("height", std::to_string(placement.height) + "px");
            state.body_part_popup_placement = placement;
            positioned = true;
        }
    }
    return update.replace_markup || update.set_scroll || positioned;
}

std::string appearance_editor_label(
    const AppState& state, AppearanceEditorField field, int32_t current)
{
    const auto* row = nw::toolset::find_appearance_catalog_row(
        appearance_catalog(state, appearance_catalog_kind(state, field)), current);
    if (row) {
        return row->name;
    }
    if (field == AppearanceEditorField::wings) {
        return "Wings " + std::to_string(current);
    }
    if (field == AppearanceEditorField::tail) {
        return "Tail " + std::to_string(current);
    }
    return "Appearance " + std::to_string(current);
}

std::string appearance_editor_label(
    const AppState& state, AppearanceEditorField field)
{
    const auto current = appearance_editor_value(state, field);
    if (!current) {
        return "Unavailable";
    }
    return appearance_editor_label(state, field, *current);
}

std::string_view appearance_editor_field_name(AppearanceEditorField field) noexcept
{
    if (field == AppearanceEditorField::wings) {
        return "wings";
    }
    if (field == AppearanceEditorField::tail) {
        return "tail";
    }
    return "appearance";
}

std::string_view appearance_editor_field_label(AppearanceEditorField field) noexcept
{
    if (field == AppearanceEditorField::wings) {
        return "Wings";
    }
    if (field == AppearanceEditorField::tail) {
        return "Tail";
    }
    return "Appearance";
}

std::optional<AppearanceEditorField> appearance_editor_field_from_name(
    std::string_view field) noexcept
{
    if (field == "appearance") {
        return AppearanceEditorField::appearance;
    }
    if (field == "wings") {
        return AppearanceEditorField::wings;
    }
    if (field == "tail") {
        return AppearanceEditorField::tail;
    }
    return std::nullopt;
}

void append_appearance_catalog_field_markup(std::string& content_markup,
    const AppState& state,
    AppearanceEditorField field,
    std::optional<int32_t> current = std::nullopt)
{
    content_markup += "<div class=\"appearance_catalog_editor\"><div class=\"appearance_field_label\">";
    content_markup += appearance_editor_field_label(field);
    content_markup += "</div><div class=\"appearance_field appearance_catalog_field\" data-field=\"";
    content_markup += appearance_editor_field_name(field);
    content_markup += "\"><span>";
    content_markup += escape_html(current
            ? appearance_editor_label(state, field, *current)
            : appearance_editor_label(state, field));
    content_markup += "</span><span class=\"appearance_field_arrow\">&#9662;</span></div>";
    content_markup += "</div>";
}

void append_appearance_selector_markup(std::string& content_markup, const AppState& state)
{
    const auto label = appearance_editor_field_label(state.appearance_editor_field);
    content_markup += "<div class=\"appearance_selector\">"
                      "<div class=\"appearance_selector_header\">"
                      "<button id=\"appearance_selector_back\" "
                      "class=\"appearance_selector_back panel_back_button\" title=\"Back\">"
                      "<span class=\"panel_back_icon\"><span class=\"panel_back_head\"></span>"
                      "<span class=\"panel_back_shaft\"></span></span></button>"
                      "<div class=\"appearance_selector_title\">";
    content_markup += label;
    content_markup += "</div></div><div class=\"appearance_selector_filter\">"
                      "<input id=\"appearance_search\" type=\"text\" placeholder=\"Filter ";
    content_markup += label;
    content_markup += "...\" value=\"";
    content_markup += escape_html(state.appearance_query);
    content_markup += "\" /></div><div id=\"appearance_rows\" class=\"appearance_selector_rows\">"
                      "<div class=\"property_tree_empty\">Loading options...</div></div></div>";
}

void append_creature_accessories_markup(
    std::string& content_markup, const AppState& state)
{
    const auto values = nw::toolset::editable_creature_accessories(
        nw::kernel::runtime(), state.object_details.object);
    if (values.size() != 2) {
        return;
    }

    content_markup += "<div class=\"creature_accessory_editor\"><div class=\"creature_accessory_title\">Accessories</div>";
    append_appearance_catalog_field_markup(
        content_markup, state, AppearanceEditorField::wings, values[0]);
    append_appearance_catalog_field_markup(
        content_markup, state, AppearanceEditorField::tail, values[1]);
    content_markup += "</div>";
}

void append_creature_body_parts_markup(std::string& content_markup, const AppState& state)
{
    const auto rows = nw::toolset::creature_body_part_editor_rows(
        nw::kernel::runtime(), state.object_details.object);
    if (rows.empty()) {
        return;
    }

    content_markup += "<div class=\"body_part_editor\"><div class=\"body_part_title\">Body Parts</div>";
    content_markup += "<div class=\"body_part_header\"><span>Part</span><span>Model</span></div>";
    content_markup += "<div class=\"body_part_rows\">";
    for (const auto& row : rows) {
        const bool active = state.body_part_combobox.is_active()
            && state.body_part_option_object == state.object_details.object
            && state.body_part_option_part == static_cast<int32_t>(row.part);
        content_markup += "<div class=\"body_part_row";
        if (active && state.body_part_combobox.popup_visible()) {
            content_markup += " open";
        }
        content_markup += "\"><span class=\"body_part_label\">";
        content_markup += escape_html(row.label);
        content_markup += "</span><div class=\"body_part_value_cell\"><div";
        if (active) {
            content_markup += " id=\"active_body_part_field\" tabindex=\"0\"";
        }
        content_markup += " class=\"body_part_field\" data-part=\"";
        content_markup += std::to_string(row.part);
        content_markup += "\" data-current=\"";
        content_markup += std::to_string(row.value);
        content_markup += "\"><span class=\"body_part_field_value\">";
        content_markup += escape_html(row.display);
        content_markup += "</span><span class=\"body_part_field_arrow\">&#9662;</span></div>";
        content_markup += "</div></div>";
    }
    content_markup += "</div></div>";
}

void append_creature_colors_markup(std::string& content_markup, const AppState& state)
{
    const auto rows = nw::toolset::creature_color_editor_rows(
        nw::kernel::runtime(), state.object_details.object);
    if (rows.empty()) {
        return;
    }
    for (const auto& row : rows) {
        if (creature_color_palette_asset(row.palette).empty()
            || row.value < 0
            || row.value >= kPltPaletteColumns * kPltPaletteRows) {
            return;
        }
    }

    content_markup += "<div class=\"creature_color_editor\"><div class=\"creature_color_title\">Colors</div>";
    content_markup += "<div class=\"creature_color_fields\">";
    for (const auto& row : rows) {
        const auto asset = creature_color_palette_asset(row.palette);
        const int source_x = (row.value % kPltPaletteColumns) * 32;
        const int source_y = (row.value / kPltPaletteColumns) * 32;
        content_markup += "<div class=\"creature_color_field\" data-color=\"";
        content_markup += std::to_string(row.color);
        content_markup += "\"><img class=\"creature_color_swatch\" src=\"";
        content_markup += asset;
        content_markup += "\" rect=\"";
        content_markup += std::to_string(source_x);
        content_markup += " ";
        content_markup += std::to_string(source_y);
        content_markup += " 32 32\"/><span>";
        content_markup += escape_html(row.label);
        content_markup += "</span></div>";
    }
    content_markup += "</div></div>";
}

void append_creature_color_selector_markup(std::string& content_markup, const AppState& state)
{
    if (!active_color_editor_matches_tab(state)) {
        return;
    }

    const auto rows = nw::toolset::creature_color_editor_rows(
        nw::kernel::runtime(), state.color_editor_object);
    const auto selected = std::ranges::find(rows, state.color_editor_channel,
        &nw::toolset::CreatureColorEditorRow::color);
    if (selected == rows.end() || selected->value < 0
        || selected->value >= kPltPaletteColumns * kPltPaletteRows) {
        return;
    }
    const auto asset = creature_color_palette_asset(selected->palette);
    if (asset.empty()) {
        return;
    }

    content_markup += "<div id=\"creature_color_selector\" "
                      "class=\"appearance_selector creature_color_selector\">"
                      "<div class=\"appearance_selector_header\">"
                      "<button id=\"creature_color_selector_close\" "
                      "class=\"appearance_selector_back panel_back_button\" title=\"Back\">"
                      "<span class=\"panel_back_icon\"><span class=\"panel_back_head\"></span>"
                      "<span class=\"panel_back_shaft\"></span></span></button>"
                      "<div class=\"appearance_selector_title\">Colors</div></div>"
                      "<div class=\"creature_color_channels\">";
    for (const auto& row : rows) {
        content_markup += "<div class=\"creature_color_channel";
        if (row.color == selected->color) {
            content_markup += " active";
        }
        content_markup += "\" data-color=\"";
        content_markup += std::to_string(row.color);
        content_markup += "\">";
        content_markup += escape_html(row.label);
        content_markup += "</div>";
    }
    content_markup += "</div><div class=\"creature_color_palette_body\">"
                      "<div id=\"creature_color_palette\" class=\"creature_color_palette\" data-color=\"";
    content_markup += std::to_string(selected->color);
    content_markup += "\"><img src=\"";
    content_markup += asset;
    content_markup += "\"/><div class=\"creature_color_selection\" style=\"left:";
    content_markup += std::to_string(
        (selected->value % kPltPaletteColumns) * kPltPaletteCellPx);
    content_markup += "px;top:";
    content_markup += std::to_string(
        (selected->value / kPltPaletteColumns) * kPltPaletteCellPx);
    content_markup += "px\"></div></div></div></div>";
}

void hydrate_item_workbench(Rml::ElementDocument* doc, const AppState& state)
{
    if (!find_el(doc, "item_surface_details")) { return; }
    const auto surface = state.object_workbench_surface;
    struct ItemSurfaceElements {
        const char* tab_id;
        const char* surface_id;
        ObjectWorkbenchSurface surface;
    };
    static constexpr std::array surfaces{
        ItemSurfaceElements{"item_tab_details", "item_surface_details",
            ObjectWorkbenchSurface::details},
        ItemSurfaceElements{"item_tab_variables", "item_surface_variables",
            ObjectWorkbenchSurface::variables},
        ItemSurfaceElements{"item_tab_appearance", "item_surface_appearance",
            ObjectWorkbenchSurface::appearance},
        ItemSurfaceElements{"item_tab_item_properties", "item_surface_item_properties",
            ObjectWorkbenchSurface::item_properties},
        ItemSurfaceElements{"item_tab_inventory", "item_surface_inventory",
            ObjectWorkbenchSurface::inventory},
    };
    for (const auto& elements : surfaces) {
        if (auto* tab = find_el(doc, elements.tab_id)) {
            tab->SetClass("active", surface == elements.surface);
        }
        if (auto* element = find_el(doc, elements.surface_id)) {
            element->SetClass("active", surface == elements.surface);
        }
    }

    const auto* active_tab = state.workspace.active_tab();
    const bool show_header = active_tab
        && active_tab->kind == nw::toolset::WorkspaceTabKind::area
        && state.active_object_tab_id == active_tab->id
        && state.object_details.object.type == nw::ObjectType::item;
    if (auto* header = find_el(doc, "item_object_header")) {
        header->SetClass("visible", show_header);
    }
    if (show_header) {
        if (auto* title = find_el(doc, "item_object_title")) {
            title->SetInnerRML(escape_html(
                nw::toolset::live_object_display_name(state.object_details.object)));
        }
    }
}

constexpr std::array<std::string_view, 18> kCreatureEquipmentSlotLabels{
    "Head",
    "Chest",
    "Boots",
    "Arms",
    "Right Hand",
    "Left Hand",
    "Cloak",
    "Left Ring",
    "Right Ring",
    "Neck",
    "Belt",
    "Arrows",
    "Bullets",
    "Bolts",
    "Attack 2",
    "Attack 1",
    "Special Attack",
    "Skin",
};

constexpr std::array<std::string_view, 18> kCreatureEquipmentSlotAssets{
    "inv_slot_helm.png",
    "inv_slot_armor.png",
    "inv_slot_boots.png",
    "inv_slot_gloves.png",
    "inv_slot_right.png",
    "inv_slot_left.png",
    "inv_slot_cloak.png",
    "inv_slot_ring.png",
    "inv_slot_ring.png",
    "inv_slot_amulet.png",
    "inv_slot_belt.png",
    "inv_slot_arrow.png",
    "inv_slot_sling.png",
    "inv_slot_bolts.png",
    "inv_slot_cre_2.png",
    "inv_slot_cre_1.png",
    "inv_slot_cre_3.png",
    "inv_slot_cre_skin.png",
};

constexpr std::array<std::string_view, 18> kCreatureEquipmentSlotClasses{
    "head",
    "chest",
    "boots",
    "arms",
    "right_hand",
    "left_hand",
    "cloak",
    "left_ring",
    "right_ring",
    "neck",
    "belt",
    "arrows",
    "bullets",
    "bolts",
    "creature_left",
    "creature_right",
    "creature_bite",
    "creature_skin",
};

constexpr std::array<size_t, 4> kCreatureNaturalEquipmentOrder{15, 14, 16, 17};

void append_creature_inventory_markup(std::string& content_markup, const AppState& state)
{
    const bool creature = state.creature_inventory.object.type == nw::ObjectType::creature;
    content_markup += "<div class=\"creature_inventory_editor";
    if (!creature) {
        content_markup += " item_inventory_editor";
    }
    content_markup += "\">";
    if (creature) {
        content_markup += "<div class=\"creature_inventory_section_header\">Equipment</div>";
    }
    if (creature && active_creature_inventory_matches_tab(state)) {
        const auto append_slot = [&](size_t index) {
            const auto& row = state.creature_inventory.equipment[index];
            content_markup += "<div class=\"creature_equipment_slot";
            if (row.assigned()) {
                content_markup += " assigned";
            } else {
                content_markup += " empty";
            }
            content_markup += " creature_equipment_slot_";
            content_markup += kCreatureEquipmentSlotClasses[index];
            content_markup += "\" data-slot=\"";
            content_markup += std::to_string(index);
            content_markup += "\" title=\"";
            content_markup += kCreatureEquipmentSlotLabels[index];
            if (row.assigned()) {
                const auto name = state.creature_inventory.text_view(row.name);
                const auto label = name.empty()
                    ? state.creature_inventory.text_view(row.resref)
                    : name;
                if (!label.empty()) {
                    content_markup += ": ";
                    content_markup += escape_html(label);
                }
            }
            content_markup += "\">";
            const auto icon_source = state.creature_inventory.text_view(row.icon_source);
            if (row.assigned() && !icon_source.empty()) {
                content_markup += "<span class=\"creature_equipment_item_frame\" style=\"width:";
                content_markup += std::to_string(row.icon_visible_width);
                content_markup += "px;height:";
                content_markup += std::to_string(row.icon_visible_height);
                content_markup += "px\"><img class=\"creature_equipment_item_icon\" style=\"left:-";
                content_markup += std::to_string(row.icon_visible_x);
                content_markup += "px;top:-";
                content_markup += std::to_string(row.icon_visible_y);
                content_markup += "px\" src=\"";
                content_markup += escape_html(icon_source);
                content_markup += "\"/></span>";
            } else if (!row.assigned()) {
                content_markup += "<img class=\"creature_equipment_slot_icon\" src=\"";
                content_markup += kCreatureEquipmentSlotAssets[index];
                content_markup += "\"/>";
            }
            content_markup += "</div>";
        };

        content_markup += "<div class=\"creature_equipment_standard\">"
                          "<div class=\"creature_equipment_column creature_equipment_right_column\">";
        append_slot(4);
        content_markup += "</div><div class=\"creature_equipment_column creature_equipment_armor_column\">";
        append_slot(1);
        content_markup += "<div class=\"creature_equipment_ammo_row\">";
        append_slot(11);
        append_slot(12);
        append_slot(13);
        content_markup += "</div></div><div class=\"creature_equipment_column creature_equipment_left_column\">";
        append_slot(5);
        append_slot(10);
        content_markup += "</div><div class=\"creature_equipment_column creature_equipment_wearables_column\">";
        append_slot(0);
        append_slot(3);
        content_markup += "<div class=\"creature_equipment_jewelry_row\"><div>";
        append_slot(8);
        append_slot(7);
        content_markup += "</div>";
        append_slot(9);
        content_markup += "</div></div><div class=\"creature_equipment_column creature_equipment_cloak_column\">";
        append_slot(6);
        append_slot(2);
        content_markup += "</div></div><div class=\"creature_equipment_creature_header\">Creature Slots</div>"
                          "<div class=\"creature_equipment_creature\">";
        for (const size_t index : kCreatureNaturalEquipmentOrder) {
            append_slot(index);
        }
        content_markup += "</div>";
    } else if (creature) {
        content_markup += "<div class=\"creature_equipment_grid_empty\"></div>";
    }
    content_markup += "<div class=\"creature_inventory_header\"><span>Inventory</span>"
                      "<span id=\"creature_inventory_count\" class=\"property_tree_count\">";
    content_markup += active_creature_inventory_matches_tab(state)
        ? std::to_string(state.creature_inventory.inventory.size())
        : std::string{"0"};
    content_markup += "</span></div><div id=\"creature_inventory_page_surface\" "
                      "class=\"creature_inventory_page_surface\">";
    content_markup += render_creature_inventory_page(state);
    content_markup += "</div></div>";
}

std::string creature_spell_choice_label(
    const nw::toolset::CreatureSpellViewSnapshot& snapshot,
    const std::vector<nw::toolset::CreatureSpellChoice>& choices,
    int32_t selected,
    std::string_view fallback)
{
    const auto choice = std::ranges::find(
        choices, selected, &nw::toolset::CreatureSpellChoice::value);
    if (choice == choices.end()) {
        return std::string{fallback};
    }
    return std::string{snapshot.text_view(choice->name)};
}

void append_creature_spell_filter_field(std::string& markup,
    const AppState& state,
    CreatureSpellFilterField field,
    std::string_view name,
    std::string_view value)
{
    const bool active = state.creature_spell_filter_field == field
        && state.creature_spell_combobox.is_active();
    markup += "<label><span>";
    markup += name;
    markup += "</span><div";
    if (active) {
        markup += " id=\"active_creature_spell_filter_field\" tabindex=\"0\"";
    }
    markup += " class=\"creature_spell_filter_field";
    if (active && state.creature_spell_combobox.popup_visible()) {
        markup += " open";
    }
    markup += "\" data-filter=\"";
    if (field == CreatureSpellFilterField::class_) {
        markup += "class";
    } else if (field == CreatureSpellFilterField::level) {
        markup += "level";
    } else {
        markup += "metamagic";
    }
    markup += "\"><span class=\"creature_spell_filter_value\">";
    markup += escape_html(value);
    markup += "</span><span class=\"creature_spell_filter_arrow\">"
              "<span class=\"creature_spell_filter_arrow_indicator\"></span></span>"
              "</div></label>";
}

void append_creature_spell_markup(std::string& markup, const AppState& state)
{
    markup += "<div class=\"creature_spell_editor\"><div class=\"creature_spell_toolbar\">";
    markup += "<input id=\"creature_spell_search\" type=\"text\" placeholder=\"Filter spells...\" value=\"";
    markup += escape_html(state.creature_spell_query);
    markup += "\" /></div><div class=\"creature_spell_header\">";
    markup += "<span class=\"creature_spell_header_name\">Spell</span>";
    markup += "<span class=\"creature_spell_header_level\">Level</span>";
    markup += "<span id=\"creature_spell_value_header\" class=\"creature_spell_header_value\">";
    markup += state.creature_spells.memorizes ? "Uses" : "Known";
    markup += "</span><span id=\"creature_spell_count\" class=\"property_tree_count\">";
    markup += active_creature_spells_match_tab(state)
        ? std::to_string(state.creature_spell_matches.size())
        : std::string{"0"};
    markup += "</span></div><div id=\"creature_spell_rows\" class=\"creature_spell_rows\">";
    markup += "<div class=\"property_tree_empty\">Waiting for a live Creature.</div></div>";
    markup += "<div class=\"creature_spell_filters\">";
    append_creature_spell_filter_field(markup,
        state,
        CreatureSpellFilterField::class_,
        "Class",
        creature_spell_choice_label(state.creature_spells,
            state.creature_spells.classes,
            state.creature_spells.selected_class,
            "None"));
    append_creature_spell_filter_field(markup,
        state,
        CreatureSpellFilterField::level,
        "Spell Level",
        state.creature_spell_level < 0 ? std::string{"All"}
                                       : std::to_string(state.creature_spell_level));
    append_creature_spell_filter_field(markup,
        state,
        CreatureSpellFilterField::metamagic,
        "Metamagic",
        creature_spell_choice_label(state.creature_spells,
            state.creature_spells.metamagic,
            state.creature_spells.selected_metamagic,
            "None"));
    markup += "</div></div>";
}

void append_creature_classes_markup(std::string& markup, const AppState& state)
{
    markup += "<div class=\"creature_classes_editor\">";
    if (!active_creature_class_presentation_matches_tab(state)) {
        markup += "<div class=\"property_tree_empty";
        if (state.creature_class_presentation.status
            == nw::toolset::ObjectDetailsStatus::invalid_data) {
            markup += " error";
        }
        markup += "\">";
        markup += escape_html(state.creature_class_presentation.diagnostic.empty()
                ? std::string_view{"Waiting for a live Creature."}
                : std::string_view{state.creature_class_presentation.diagnostic});
        markup += "</div></div>";
        return;
    }

    markup += "<div class=\"creature_classes_header\"><span>Class</span><span>Level</span></div>";
    if (state.creature_class_presentation.rows.empty()) {
        markup += "<div class=\"property_tree_empty\">No classes assigned.</div></div>";
        return;
    }
    size_t row_index = 0;
    for (const auto& row : state.creature_class_presentation.rows) {
        markup += "<div class=\"creature_class_row";
        if ((row_index & 1u) != 0) {
            markup += " alternate";
        }
        markup += "\"><span class=\"creature_class_name\">";
        markup += escape_html(state.creature_class_presentation.text_view(row.label));
        markup += "</span><span class=\"quantity_stepper creature_class_level_controls\">"
                  "<button type=\"button\" class=\"creature_class_level_adjust\" data-slot=\"";
        markup += std::to_string(row.slot);
        markup += "\" data-delta=\"-1\"";
        if (row.level <= row.minimum_level) {
            markup += " disabled";
        }
        markup += ">-</button><span>";
        markup += std::to_string(row.level);
        markup += "</span><button type=\"button\" class=\"creature_class_level_adjust\" data-slot=\"";
        markup += std::to_string(row.slot);
        markup += "\" data-delta=\"1\"";
        if (row.level >= row.maximum_level) {
            markup += " disabled";
        }
        markup += ">+</button></span></div>";
        ++row_index;
    }
    markup += "</div>";
}

void append_creature_workbench_overlay_markup(
    std::string& markup, const AppState& state)
{
    if (active_body_part_options_match_tab(state)
        && state.body_part_combobox.popup_visible()) {
        markup += "<div id=\"body_part_option_rows\" "
                  "class=\"virtual_combobox_options body_part_option_rows\"";
        if (state.body_part_popup_placement) {
            const auto& placement = *state.body_part_popup_placement;
            markup += " style=\"left:";
            markup += std::to_string(placement.left);
            markup += "px;top:";
            markup += std::to_string(placement.top);
            markup += "px;width:";
            markup += std::to_string(placement.width);
            markup += "px;height:";
            markup += std::to_string(placement.height);
            markup += "px\"";
        }
        markup += "><div class=\"property_tree_empty\">"
                  "Loading model parts...</div></div>";
    }
    if (active_creature_spell_filter_matches_tab(state)
        && state.creature_spell_combobox.popup_visible()) {
        markup += "<div id=\"creature_spell_filter_options\" "
                  "class=\"virtual_combobox_options creature_spell_filter_options\"";
        if (state.creature_spell_popup_placement) {
            const auto& placement = *state.creature_spell_popup_placement;
            markup += " style=\"left:";
            markup += std::to_string(placement.left);
            markup += "px;top:";
            markup += std::to_string(placement.top);
            markup += "px;width:";
            markup += std::to_string(placement.width);
            markup += "px;height:";
            markup += std::to_string(placement.height);
            markup += "px\"";
        }
        markup += "><div class=\"property_tree_empty\">"
                  "Loading choices...</div></div>";
    }
}

void hydrate_creature_workbench(Rml::ElementDocument* doc, const AppState& state)
{
    if (!find_el(doc, "creature_surface_details")) {
        return;
    }

    struct CreatureSurfaceElements {
        const char* tab_id;
        const char* surface_id;
        ObjectWorkbenchSurface surface;
    };
    static constexpr std::array surfaces{
        CreatureSurfaceElements{"creature_tab_details", "creature_surface_details",
            ObjectWorkbenchSurface::details},
        CreatureSurfaceElements{"creature_tab_sheet", "creature_surface_sheet",
            ObjectWorkbenchSurface::sheet},
        CreatureSurfaceElements{"creature_tab_variables", "creature_surface_variables",
            ObjectWorkbenchSurface::variables},
        CreatureSurfaceElements{"creature_tab_classes", "creature_surface_classes",
            ObjectWorkbenchSurface::classes},
        CreatureSurfaceElements{"creature_tab_appearance", "creature_surface_appearance",
            ObjectWorkbenchSurface::appearance},
        CreatureSurfaceElements{"creature_tab_feats", "creature_surface_feats",
            ObjectWorkbenchSurface::feats},
        CreatureSurfaceElements{"creature_tab_spells", "creature_surface_spells",
            ObjectWorkbenchSurface::spells},
        CreatureSurfaceElements{"creature_tab_inventory", "creature_surface_inventory",
            ObjectWorkbenchSurface::inventory},
    };
    for (const auto& elements : surfaces) {
        if (auto* tab = find_el(doc, elements.tab_id)) {
            tab->SetClass("active", state.object_workbench_surface == elements.surface);
        }
        if (auto* surface = find_el(doc, elements.surface_id)) {
            surface->SetClass("active", state.object_workbench_surface == elements.surface);
        }
    }

    const auto* active_tab = state.workspace.active_tab();
    const bool show_header = active_tab
        && active_tab->kind == nw::toolset::WorkspaceTabKind::area
        && state.active_object_tab_id == active_tab->id
        && state.object_details.object.type == nw::ObjectType::creature;
    if (auto* header = find_el(doc, "creature_object_header")) {
        header->SetClass("visible", show_header);
    }
    if (show_header) {
        if (auto* title = find_el(doc, "creature_object_title")) {
            title->SetInnerRML(escape_html(
                nw::toolset::live_object_display_name(state.object_details.object)));
        }
    }

    std::string markup;
    switch (state.object_workbench_surface) {
    case ObjectWorkbenchSurface::classes:
        append_creature_classes_markup(markup, state);
        if (auto* target = find_el(doc, "creature_classes_dynamic")) {
            target->SetInnerRML(markup);
        }
        break;
    case ObjectWorkbenchSurface::appearance:
        if (active_color_editor_matches_tab(state)) {
            append_creature_color_selector_markup(markup, state);
        } else if (state.appearance_selector_open) {
            append_appearance_selector_markup(markup, state);
        } else {
            markup += "<div id=\"appearance_editor\" class=\"appearance_editor\">";
            append_appearance_catalog_field_markup(
                markup, state, AppearanceEditorField::appearance);
            append_creature_body_parts_markup(markup, state);
            append_creature_accessories_markup(markup, state);
            append_creature_colors_markup(markup, state);
            markup += "</div>";
        }
        if (auto* target = find_el(doc, "creature_appearance_dynamic")) {
            target->SetInnerRML(markup);
        }
        break;
    case ObjectWorkbenchSurface::feats:
        set_input_value(doc, "creature_feat_search", state.creature_feat_query);
        break;
    case ObjectWorkbenchSurface::spells:
        append_creature_spell_markup(markup, state);
        if (auto* target = find_el(doc, "creature_spells_dynamic")) {
            target->SetInnerRML(markup);
        }
        break;
    case ObjectWorkbenchSurface::inventory:
        append_creature_inventory_markup(markup, state);
        if (auto* target = find_el(doc, "creature_inventory_dynamic")) {
            target->SetInnerRML(markup);
        }
        break;
    default:
        break;
    }

    markup.clear();
    append_creature_workbench_overlay_markup(markup, state);
    if (auto* overlays = find_el(doc, "creature_workbench_overlays")) {
        overlays->SetInnerRML(markup);
    }
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
    const auto* active_tab = state.workspace.active_tab();
    const bool area_tab = active_tab
        && active_tab->kind == nw::toolset::WorkspaceTabKind::area;
    if (area_tab && !active_object_matches_tab(state)) {
        append_placed_area_object_list_markup(content_markup, state);
        return;
    }
    if (state.object_details.object.type == nw::ObjectType::creature) {
        content_markup += "<template src=\"creature-workbench\"></template>";
        return;
    }
    if (state.object_details.object.type == nw::ObjectType::item) {
        content_markup += "<template src=\"item-workbench\"></template>";
        return;
    }
    const bool project_module = state.object_details.object.type == nw::ObjectType::module
        && !state.backend.current_project_dir().empty();
    content_markup += "<div id=\"object_workbench\" class=\"object_workbench\">";
    if (area_tab && active_object_matches_tab(state)) {
        const std::string object_name = nw::toolset::live_object_display_name(
            state.object_details.object);
        content_markup += "<div class=\"object_workbench_header area_object_header\">";
        content_markup += "<button type=\"button\" "
                          "class=\"area_object_list_back panel_back_button\" "
                          "title=\"Back to placed objects\">"
                          "<span class=\"panel_back_icon\"><span class=\"panel_back_head\"></span>"
                          "<span class=\"panel_back_shaft\"></span></span></button>";
        content_markup += "<div class=\"object_workbench_title\">";
        content_markup += escape_html(object_name);
        content_markup += "</div></div>";
    }
    content_markup += "<div id=\"object_workbench_tab_bar\" class=\"object_workbench_tab_bar\">"
                      "<div id=\"object_workbench_tabs\" class=\"object_workbench_tabs\">"
                      "<div id=\"object_workbench_tab_track\" class=\"object_workbench_tab_track\">";
    content_markup += "<div class=\"object_workbench_tab";
    if (state.object_workbench_surface == ObjectWorkbenchSurface::details) {
        content_markup += " active";
    }
    content_markup += "\" data-surface=\"details\">Details</div>";
    content_markup += "<div class=\"object_workbench_tab";
    if (state.object_workbench_surface == ObjectWorkbenchSurface::variables) {
        content_markup += " active";
    }
    content_markup += "\" data-surface=\"variables\">Variables</div>";
    if (project_module) {
        content_markup += "<div class=\"object_workbench_tab";
        if (state.object_workbench_surface == ObjectWorkbenchSurface::haks) {
            content_markup += " active";
        }
        content_markup += "\" data-surface=\"haks\">Haks</div>";
    }
    const bool appearance = state.object_details.object.type == nw::ObjectType::placeable;
    if (appearance) {
        content_markup += "<div class=\"object_workbench_tab";
        if (state.object_workbench_surface == ObjectWorkbenchSurface::appearance) {
            content_markup += " active";
        }
        content_markup += "\" data-surface=\"appearance\">Appearance</div>";
    }
    content_markup += "</div></div>"
                      "<button id=\"object_workbench_tabs_previous\" "
                      "class=\"object_workbench_tab_scroll_button disabled\" "
                      "type=\"button\" title=\"Scroll editor tabs left\">&#x2039;</button>"
                      "<button id=\"object_workbench_tabs_next\" "
                      "class=\"object_workbench_tab_scroll_button disabled\" "
                      "type=\"button\" title=\"Scroll editor tabs right\">&#x203a;</button>"
                      "</div>";

    if (state.object_workbench_surface == ObjectWorkbenchSurface::variables) {
        content_markup += "<div class=\"object_variable_toolbar\"><button id=\"object_variable_add\" "
                          "type=\"button\" title=\"Add integer variable\">Add Variable</button></div>"
                          "<div class=\"object_variable_header\"><span class=\"object_variable_header_name\">Name</span>"
                          "<span class=\"object_variable_header_type\">Type</span>"
                          "<span class=\"object_variable_header_value\">Value</span>"
                          "<span id=\"object_variable_count\" class=\"property_tree_count\">";
        content_markup += active_object_variables_match_tab(state)
            ? std::to_string(state.object_variables.rows.size())
            : std::string{"0"};
        content_markup += "</span></div><div id=\"object_variable_rows\" "
                          "class=\"object_variable_rows\"><div class=\"property_tree_empty\">"
                          "Waiting for a live object.</div></div>";
    } else if (project_module && state.object_workbench_surface == ObjectWorkbenchSurface::haks) {
        const auto module_summary = state.backend.project_module_summary();
        content_markup += "<div class=\"module_hak_list\">";
        if (!module_summary.ok) {
            content_markup += "<div class=\"module_hak_empty\">";
            content_markup += escape_html(module_summary.message.empty()
                    ? std::string{"Module metadata unavailable."}
                    : module_summary.message);
            content_markup += "</div>";
        } else if (module_summary.haks.empty()) {
            content_markup += "<div class=\"module_hak_empty\">No module haks.</div>";
        } else {
            for (const auto& hak : module_summary.haks) {
                content_markup += "<div class=\"module_hak_item\"><span class=\"module_hak_name\">";
                content_markup += escape_html(hak);
                content_markup += "</span></div>";
            }
        }
        content_markup += "</div>";
    } else if (appearance && state.object_workbench_surface == ObjectWorkbenchSurface::appearance) {
        if (active_color_editor_matches_tab(state)) {
            append_creature_color_selector_markup(content_markup, state);
        } else if (state.appearance_selector_open) {
            append_appearance_selector_markup(content_markup, state);
        } else {
            content_markup += "<div id=\"appearance_editor\" class=\"appearance_editor\">";
            append_appearance_catalog_field_markup(
                content_markup, state, AppearanceEditorField::appearance);
            content_markup += "</div>";
        }
    } else {
        content_markup += "<div class=\"property_tree_header\"><span class=\"property_tree_header_name\">Field</span>";
        content_markup += "<span class=\"property_tree_header_value\">Value</span>";
        content_markup += "<span id=\"property_tree_count\" class=\"property_tree_count\">";
        content_markup += std::to_string(active_details_row_count(state));
        content_markup += "</span></div><div id=\"property_tree_rows\" class=\"property_tree_rows\">";
        content_markup += "<div class=\"property_tree_empty\">Select an object to inspect.</div></div>";
    }
    content_markup += "</div>";
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
        content_markup += "</div></div>";
        content_markup += "<div class=\"workspace_preview_body\"><div id=\"workspace_viewer_viewport\" class=\"workspace_viewer_viewport";
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
        append_object_workbench_markup(content_markup, state);
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
        content_markup += "<div class=\"workspace_preview_body\"><div id=\"workspace_viewer_viewport\" class=\"workspace_viewer_viewport";
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
            append_resource_document_inspector(content_markup, *document, false);
        } else {
            append_missing_resource_document(content_markup);
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

std::string workspace_tab_class(const nw::toolset::WorkspaceTab& tab, std::string_view active_tab_id, const AppState& state)
{
    std::string out = "workspace_tab workspace_tab_";
    out += workspace_tab_kind_class(tab.kind);
    if (tab.id == active_tab_id) {
        out += " active";
    }
    if (tab.closable) {
        out += " closable";
    }
    if (!tab.movable) {
        out += " locked";
    }
    if (state.workspace_tab_dragging && tab.id == state.workspace_tab_drag_id) {
        out += " dragging";
    }
    return out;
}

bool sync_workspace_tab_elements(Rml::ElementDocument* doc, AppState& state)
{
    auto* track = find_el(doc, "workspace_tab_track");
    if (!track) {
        return false;
    }

    const auto& tabs = state.workspace.tabs();
    if (track->GetNumChildren() != static_cast<int>(tabs.size())) {
        return false;
    }

    const std::string active_tab_id = state.workspace.active_tab_id();
    for (size_t tab_index = 0; tab_index < tabs.size(); ++tab_index) {
        auto* child = track->GetChild(static_cast<int>(tab_index));
        if (!child || !child->IsClassSet("workspace_tab")) {
            return false;
        }

        const auto& tab = tabs[tab_index];
        if (child->GetAttribute<Rml::String>("data-tab", "") != tab.id) {
            return false;
        }

        child->SetAttribute("class", workspace_tab_class(tab, active_tab_id, state));
        child->SetAttribute("data-index", std::to_string(tab_index));
        child->SetAttribute("data-movable", tab.movable ? "1" : "0");
    }

    state.workspace_tab_scroll_pending = true;
    return true;
}

bool remove_workspace_tab_element(Rml::ElementDocument* doc, AppState& state, std::string_view tab_id)
{
    auto* track = find_el(doc, "workspace_tab_track");
    if (!track) {
        return false;
    }

    const int child_count = track->GetNumChildren();
    for (int i = 0; i < child_count; ++i) {
        auto* child = track->GetChild(i);
        if (child && child->IsClassSet("workspace_tab") && child->GetAttribute<Rml::String>("data-tab", "") == tab_id) {
            track->RemoveChild(child).reset();
            state.workspace_tab_scroll_pending = true;
            return sync_workspace_tab_elements(doc, state);
        }
    }
    return false;
}

void append_workspace_home_markup(std::string& content_markup, const AppState& state)
{
    const auto project_dir = state.backend.current_project_dir();
    const nw::ObjectHandle module_object = state.backend.module_object();
    const bool project_open = !project_dir.empty();
    const bool module_open = module_object.type == nw::ObjectType::module;

    content_markup += "<div class=\"workspace_home_surface\"><div id=\"workspace_home\">";
    content_markup += "<div class=\"workspace_home_content\"><div id=\"workspace_home_header\">";
    content_markup += "<div><div id=\"workspace_home_title\">";
    if (project_open) {
        content_markup += escape_html(nw::toolset::project_display_name(project_dir));
    } else if (module_open) {
        content_markup += escape_html(nw::toolset::live_object_display_name(module_object));
    } else {
        content_markup += "Recent Projects";
    }
    content_markup += "</div>";
    if (project_open) {
        content_markup += "<div id=\"workspace_home_subtitle\">";
        content_markup += escape_html(project_dir.string());
        content_markup += "</div>";
    }
    content_markup += "</div></div>";

    if (!module_open) {
        content_markup += "<div id=\"home_project_list\">";
        if (state.recent_projects.empty()) {
            content_markup += "<div class=\"home_empty\">No recent projects.</div>";
        } else {
            for (size_t i = 0; i < state.recent_projects.size(); ++i) {
                const auto& project = state.recent_projects[i];
                content_markup += "<div class=\"home_project_item\" data-key=\"";
                content_markup += std::to_string(i);
                content_markup += "\"><div class=\"home_project_name\">";
                content_markup += escape_html(project.name);
                content_markup += "</div><div class=\"home_project_path\">";
                content_markup += escape_html(project.path);
                content_markup += "</div></div>";
            }
        }
        content_markup += "</div>";
    }
    if (module_open) {
        content_markup += "<div id=\"home_area_browser\"><div class=\"home_area_browser_header\">";
        content_markup += "<div class=\"home_section_title\">Areas</div>";
        content_markup += "<div id=\"home_area_count\" class=\"home_area_count\">";
        content_markup += std::to_string(state.areas.size());
        content_markup += "</div></div>";
        content_markup += "<input id=\"home_area_search\" class=\"home_area_search\" type=\"text\" placeholder=\"Filter areas...\" value=\"";
        content_markup += escape_html(state.home_area_query);
        content_markup += "\"/>";
        content_markup += "<div id=\"home_area_list\" class=\"home_area_list\"></div></div>";
    }
    content_markup += "</div></div>";
    if (module_open) {
        append_object_workbench_markup(content_markup, state);
    }
    content_markup += "</div>";
}

bool workspace_home_active(const AppState& state)
{
    const auto* tab = state.workspace.active_tab();
    return !tab || tab->kind == nw::toolset::WorkspaceTabKind::home;
}

void refresh_home_area_catalog(AppState& state, bool force)
{
    const uint64_t generation = state.backend.module_generation();
    if (!force && generation == state.home_area_generation) {
        return;
    }

    if (generation != state.home_area_generation) {
        state.home_area_query.clear();
    }

    state.areas = state.backend.list_areas(state.home_area_query);
    state.home_area_generation = generation;
    state.home_area_list.set_row_height(kHomeAreaRowHeightPx);
    state.home_area_list.set_overscan(kHomeAreaOverscanRows);
    state.home_area_list.set_scroll_top(0);
    state.rendered_home_area_count = kInvalidVirtualIndex;
    state.rendered_home_area_columns = 0;
}

std::string rml_file_source(const std::filesystem::path& path)
{
    std::string result = path.generic_string();
    std::replace(result.begin(), result.end(), ':', '|');
    if (path.is_absolute()) {
        result.insert(0, result.starts_with('/') ? "file://" : "file:///");
    }
    return result;
}

void append_home_area_card_markup(const nw::toolset::LoadedAreaEntry& area,
    size_t index,
    std::string& markup)
{
    markup += "<div class=\"home_area_card\" data-key=\"";
    markup += std::to_string(index);
    markup += "\"><div class=\"home_area_map\">";
    if (!area.map_path.empty()) {
        markup += "<img src=\"";
        markup += escape_html(rml_file_source(area.map_path));
        markup += "\"/>";
    } else {
        markup += "<div class=\"home_area_map_missing\">Map unavailable</div>";
    }
    markup += "</div><div class=\"home_area_name\">";
    markup += escape_html(area.name.empty() ? area.resref : area.name);
    markup += "</div><div class=\"home_area_resref\">";
    markup += escape_html(area.resref);
    markup += "</div></div>";
}

bool sync_home_area_window(Rml::ElementDocument* doc, AppState& state, bool force)
{
    if (!doc || !workspace_home_active(state)) {
        return false;
    }
    auto* list = find_el(doc, "home_area_list");
    if (!list) {
        return false;
    }

    const int list_width = std::max(1, static_cast<int>(std::lround(std::max(list->GetClientWidth(), list->GetOffsetWidth()))));
    const int columns = std::clamp(
        (list_width + kHomeAreaCardGapPx)
            / (kHomeAreaMinimumCardWidthPx + kHomeAreaCardGapPx),
        1,
        kHomeAreaMaximumColumns);
    const int logical_rows = static_cast<int>((state.areas.size()
                                                  + static_cast<size_t>(columns) - 1)
        / static_cast<size_t>(columns));
    state.home_area_list.set_total_rows(logical_rows);
    state.home_area_list.set_viewport_height(std::max(0,
        static_cast<int>(std::lround(std::max(list->GetClientHeight(), list->GetOffsetHeight())))));
    state.home_area_list.set_scroll_top(std::max(0,
        static_cast<int>(std::lround(list->GetScrollTop()))));
    const auto range = state.home_area_list.compute_range();
    if (!force
        && state.rendered_home_area_count == state.areas.size()
        && state.rendered_home_area_columns == columns
        && state.rendered_home_area_range.start == range.start
        && state.rendered_home_area_range.end == range.end) {
        return false;
    }

    const float scroll_top = list->GetScrollTop();
    std::string markup;
    if (state.areas.empty()) {
        markup = "<div class=\"home_empty\">No matching areas.</div>";
    } else {
        if (range.top_spacer_px > 0) {
            markup += "<div class=\"home_area_spacer\" style=\"height:";
            markup += std::to_string(range.top_spacer_px);
            markup += "px;\"></div>";
        }
        for (int row = range.start; row < range.end; ++row) {
            markup += "<div class=\"home_area_grid_row\">";
            for (int column = 0; column < columns; ++column) {
                const size_t index = static_cast<size_t>(row * columns + column);
                if (index < state.areas.size()) {
                    append_home_area_card_markup(state.areas[index], index, markup);
                } else {
                    markup += "<div class=\"home_area_card home_area_card_filler\"></div>";
                }
            }
            markup += "</div>";
        }
        if (range.bottom_spacer_px > 0) {
            markup += "<div class=\"home_area_spacer\" style=\"height:";
            markup += std::to_string(range.bottom_spacer_px);
            markup += "px;\"></div>";
        }
    }

    list->SetInnerRML(markup);
    list->SetScrollTop(scroll_top);
    if (auto* count = find_el(doc, "home_area_count")) {
        count->SetInnerRML(std::to_string(state.areas.size()));
    }
    state.rendered_home_area_range = range;
    state.rendered_home_area_count = state.areas.size();
    state.rendered_home_area_columns = columns;
    return true;
}

void refresh_workspace_content(Rml::ElementDocument* doc, AppState& state)
{
    if (!doc) {
        return;
    }
    if (state.body_part_combobox.popup_visible()) {
        state.body_part_combobox.invalidate_popup_render();
    }
    if (state.creature_spell_combobox.popup_visible()) {
        state.creature_spell_combobox.invalidate_popup_render();
    }
    if (active_appearances_match_tab(state)) {
        if (auto* editor = find_el(doc, "appearance_editor")) {
            state.appearance_editor_scroll_top = std::max(0.0f, editor->GetScrollTop());
        }
    }
    remember_tab_scroll(doc, kObjectWorkbenchTabScrollStrip,
        state.object_workbench_tab_scroll_x);

    ensure_active_dialog_document(state);
    sync_active_module_object(state);
    refresh_home_area_catalog(state, false);
    const auto* active_tab = state.workspace.active_tab();
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
    state.object_workbench_tab_scroll_pending = true;
    apply_shell_layout(doc, state);
    sync_home_area_window(doc, state, true);
    hydrate_creature_workbench(doc, state);
    hydrate_item_workbench(doc, state);
    refresh_smalls_elements(doc, state);
    if (active_appearances_match_tab(state)) {
        if (auto* editor = find_el(doc, "appearance_editor")) {
            editor->SetScrollTop(state.appearance_editor_scroll_top);
        }
    }
    sync_object_details_window(doc, state, true);
    nw::toolset::sync_managed_lists(
        doc, nw::toolset::ui_v1_host(), state.managed_lists, true);
    sync_creature_inventory_window(doc, state, true);
    nw::toolset::sync_dialog_view(doc, state.dialog_view, true);
}

void refresh_workspace_tabs(Rml::ElementDocument* doc, AppState& state)
{
    if (!doc) {
        return;
    }

    remember_tab_scroll(doc, kWorkspaceTabScrollStrip,
        state.workspace_tab_scroll_x);
    const auto& tabs = state.workspace.tabs();
    const std::string active_tab_id = state.workspace.active_tab_id();

    std::string tab_markup;
    tab_markup += "<div id=\"workspace_tab_track\">";
    for (size_t tab_index = 0; tab_index < tabs.size(); ++tab_index) {
        const auto& tab = tabs[tab_index];
        tab_markup += "<div class=\"";
        tab_markup += workspace_tab_class(tab, active_tab_id, state);
        tab_markup += "\" data-tab=\"";
        tab_markup += escape_html(tab.id);
        tab_markup += "\" data-index=\"";
        tab_markup += std::to_string(tab_index);
        tab_markup += "\" data-movable=\"";
        tab_markup += tab.movable ? "1" : "0";
        tab_markup += "\">";
        if (const std::string icon = workspace_tab_icon(tab); !icon.empty()) {
            tab_markup += "<span class=\"workspace_tab_icon workspace_tab_icon_";
            tab_markup += workspace_tab_kind_class(tab.kind);
            tab_markup += "\">";
            tab_markup += escape_html(icon);
            tab_markup += "</span>";
        }
        tab_markup += "<span class=\"workspace_tab_title\">";
        if (tab.kind == nw::toolset::WorkspaceTabKind::home) {
            tab_markup += workspace_home_tab_icon_markup();
        } else if (tab.kind == nw::toolset::WorkspaceTabKind::area) {
            tab_markup += workspace_area_tab_icon_markup();
        } else {
            tab_markup += escape_html(tab.title);
        }
        if (tab.dirty) {
            tab_markup += " *";
        }
        tab_markup += "</span>";
        if (tab.closable) {
            tab_markup += "<div class=\"workspace_tab_close\" data-tab=\"";
            tab_markup += escape_html(tab.id);
            tab_markup += "\"><span class=\"workspace_tab_close_glyph\">x</span></div>";
        }
        tab_markup += "</div>";
    }
    tab_markup += "</div>";
    if (auto* tab_strip = doc->GetElementById("workspace_tabs")) {
        tab_strip->SetInnerRML(tab_markup);
        state.workspace_tab_scroll_pending = true;
    }
}

void refresh_workspace_view(Rml::ElementDocument* doc, AppState& state)
{
    if (!doc) {
        return;
    }

    refresh_workspace_tabs(doc, state);
    remember_tab_scroll(doc, kObjectWorkbenchTabScrollStrip,
        state.object_workbench_tab_scroll_x);

    ensure_active_dialog_document(state);
    sync_active_module_object(state);
    refresh_home_area_catalog(state, false);
    const auto* active_tab = state.workspace.active_tab();
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
    state.object_workbench_tab_scroll_pending = true;
    apply_shell_layout(doc, state);
    sync_home_area_window(doc, state, true);
    hydrate_creature_workbench(doc, state);
    hydrate_item_workbench(doc, state);
    refresh_smalls_elements(doc, state);
    sync_object_details_window(doc, state, true);
    nw::toolset::sync_managed_lists(
        doc, nw::toolset::ui_v1_host(), state.managed_lists, true);
    sync_creature_inventory_window(doc, state, true);
    nw::toolset::sync_dialog_view(doc, state.dialog_view, true);
}

std::optional<WorkspaceViewerViewportRequest> active_workspace_viewer_viewport_request(
    Rml::ElementDocument* doc, AppState& state, int frame_width, int frame_height)
{
    if (!doc || frame_width <= 0 || frame_height <= 0) {
        return std::nullopt;
    }

    const auto* active_tab = state.workspace.active_tab();
    if (!active_tab
        || (active_tab->kind != nw::toolset::WorkspaceTabKind::area
            && active_tab->kind != nw::toolset::WorkspaceTabKind::preview)
        || active_tab->detail.empty()) {
        return std::nullopt;
    }

    const auto project_dir = state.backend.current_project_dir();
    if (project_dir.empty()) {
        return std::nullopt;
    }

    auto* viewport_element = doc->GetElementById("workspace_viewer_viewport");
    if (!viewport_element) {
        return std::nullopt;
    }

    const float left_f = viewport_element->GetAbsoluteLeft() + viewport_element->GetClientLeft();
    const float top_f = viewport_element->GetAbsoluteTop() + viewport_element->GetClientTop();
    const float right_f = left_f + viewport_element->GetClientWidth();
    const float bottom_f = top_f + viewport_element->GetClientHeight();

    const int left = std::clamp(static_cast<int>(std::floor(left_f)), 0, frame_width);
    const int top = std::clamp(static_cast<int>(std::floor(top_f)), 0, frame_height);
    const int right = std::clamp(static_cast<int>(std::ceil(right_f)), left, frame_width);
    const int bottom = std::clamp(static_cast<int>(std::ceil(bottom_f)), top, frame_height);
    if (right - left < 8 || bottom - top < 8) {
        return std::nullopt;
    }

    return WorkspaceViewerViewportRequest{
        project_dir,
        active_tab->detail,
        state.backend.module_generation(),
        active_tab->kind == nw::toolset::WorkspaceTabKind::area
            ? WorkspaceViewerViewportKind::area
            : WorkspaceViewerViewportKind::preview,
        ClientViewportRect{
            left,
            top,
            static_cast<uint32_t>(right - left),
            static_cast<uint32_t>(bottom - top),
        },
    };
}

void smooth_viewer_metric(float& latest_seconds, float& smoothed_seconds, float sample_seconds)
{
    if (sample_seconds < 0.0f) {
        return;
    }

    latest_seconds = sample_seconds;
    if (smoothed_seconds <= 0.0f) {
        smoothed_seconds = sample_seconds;
    } else {
        constexpr float kSmoothing = 0.10f;
        smoothed_seconds += (sample_seconds - smoothed_seconds) * kSmoothing;
    }
}

float display_metric_seconds(float latest_seconds, float smoothed_seconds)
{
    return smoothed_seconds > 0.0f ? smoothed_seconds : latest_seconds;
}

bool viewer_fps_overlay_verbose()
{
    static const bool enabled = environment_flag_enabled("ROLLNW_CLIENT_FPS_OVERLAY_VERBOSE");
    return enabled;
}

class ScopedClientGpuTimer {
public:
    ScopedClientGpuTimer(ClientRenderer& renderer, const char* label)
        : renderer_{&renderer}
        , scope_{renderer.begin_gpu_timer(label)}
    {
    }

    ~ScopedClientGpuTimer()
    {
        if (renderer_ && scope_.valid()) {
            renderer_->end_gpu_timer(scope_);
        }
    }

    ScopedClientGpuTimer(const ScopedClientGpuTimer&) = delete;
    ScopedClientGpuTimer& operator=(const ScopedClientGpuTimer&) = delete;

private:
    ClientRenderer* renderer_ = nullptr;
    ClientGpuTimerScope scope_{};
};

void update_viewer_frame_metrics(AppState& state, float frame_seconds)
{
    if (frame_seconds <= 0.0f) {
        return;
    }

    smooth_viewer_metric(state.viewer_fps_frame_seconds, state.viewer_fps_smoothed_seconds, frame_seconds);
}

void update_viewer_render_metrics(AppState& state,
    float work_seconds,
    float sync_seconds,
    float draw_seconds,
    float ui_seconds,
    float view_seconds,
    float hud_seconds,
    float overlay_seconds,
    float palette_seconds,
    float present_seconds)
{
    smooth_viewer_metric(state.viewer_fps_work_seconds, state.viewer_fps_work_smoothed_seconds, work_seconds);
    smooth_viewer_metric(state.viewer_fps_sync_seconds, state.viewer_fps_sync_smoothed_seconds, sync_seconds);
    smooth_viewer_metric(state.viewer_fps_draw_seconds, state.viewer_fps_draw_smoothed_seconds, draw_seconds);
    smooth_viewer_metric(state.viewer_fps_ui_seconds, state.viewer_fps_ui_smoothed_seconds, ui_seconds);
    smooth_viewer_metric(state.viewer_fps_view_seconds, state.viewer_fps_view_smoothed_seconds, view_seconds);
    smooth_viewer_metric(state.viewer_fps_hud_seconds, state.viewer_fps_hud_smoothed_seconds, hud_seconds);
    smooth_viewer_metric(state.viewer_fps_overlay_seconds, state.viewer_fps_overlay_smoothed_seconds, overlay_seconds);
    smooth_viewer_metric(state.viewer_fps_palette_seconds, state.viewer_fps_palette_smoothed_seconds, palette_seconds);
    smooth_viewer_metric(state.viewer_fps_present_seconds, state.viewer_fps_present_smoothed_seconds, present_seconds);
}

void update_viewer_internal_metrics(AppState& state, const nw::render::viewer::ViewerFrameStats* stats)
{
    if (!stats) {
        return;
    }

    smooth_viewer_metric(state.viewer_fps_tick_seconds, state.viewer_fps_tick_smoothed_seconds, stats->tick_seconds);
    smooth_viewer_metric(state.viewer_fps_setup_seconds, state.viewer_fps_setup_smoothed_seconds, stats->setup_seconds);
    smooth_viewer_metric(state.viewer_fps_shadow_seconds, state.viewer_fps_shadow_smoothed_seconds, stats->shadow_seconds);
    smooth_viewer_metric(state.viewer_fps_opaque_seconds, state.viewer_fps_opaque_smoothed_seconds, stats->opaque_seconds);
    smooth_viewer_metric(state.viewer_fps_water_seconds, state.viewer_fps_water_smoothed_seconds, stats->water_seconds);
    smooth_viewer_metric(state.viewer_fps_transparent_seconds,
        state.viewer_fps_transparent_smoothed_seconds,
        stats->transparent_seconds);
    smooth_viewer_metric(state.viewer_fps_particles_seconds,
        state.viewer_fps_particles_smoothed_seconds,
        stats->particles_seconds);
    smooth_viewer_metric(state.viewer_fps_debug_seconds,
        state.viewer_fps_debug_smoothed_seconds,
        stats->debug_seconds);
    smooth_viewer_metric(state.viewer_fps_area_prepare_seconds,
        state.viewer_fps_area_prepare_smoothed_seconds,
        stats->area_prepare_seconds);
    smooth_viewer_metric(state.viewer_fps_view_internal_seconds,
        state.viewer_fps_view_internal_smoothed_seconds,
        stats->total_render_seconds);
    smooth_viewer_metric(state.viewer_fps_gpu_shadow_seconds,
        state.viewer_fps_gpu_shadow_smoothed_seconds,
        stats->gpu_shadow_seconds);
    smooth_viewer_metric(state.viewer_fps_gpu_opaque_seconds,
        state.viewer_fps_gpu_opaque_smoothed_seconds,
        stats->gpu_opaque_seconds);
    smooth_viewer_metric(state.viewer_fps_gpu_water_seconds,
        state.viewer_fps_gpu_water_smoothed_seconds,
        stats->gpu_water_seconds);
    smooth_viewer_metric(state.viewer_fps_gpu_transparent_seconds,
        state.viewer_fps_gpu_transparent_smoothed_seconds,
        stats->gpu_transparent_seconds);
    smooth_viewer_metric(state.viewer_fps_gpu_particles_seconds,
        state.viewer_fps_gpu_particles_smoothed_seconds,
        stats->gpu_particles_seconds);
    smooth_viewer_metric(state.viewer_fps_gpu_debug_seconds,
        state.viewer_fps_gpu_debug_smoothed_seconds,
        stats->gpu_debug_seconds);
    smooth_viewer_metric(state.viewer_fps_gpu_total_seconds,
        state.viewer_fps_gpu_total_smoothed_seconds,
        stats->gpu_shadow_seconds + stats->gpu_opaque_seconds + stats->gpu_water_seconds
            + stats->gpu_transparent_seconds + stats->gpu_particles_seconds + stats->gpu_debug_seconds);
    state.viewer_fps_gpu_timer_count = stats->gpu_timer_count;
    state.viewer_fps_model_count = stats->model_count;
    state.viewer_fps_particle_system_count = stats->particle_system_count;
    state.viewer_fps_render_model_animation_sample_input_count = stats->render_model_animation_sample_stats.input_count;
    state.viewer_fps_render_model_animation_sampled_count = stats->render_model_animation_sample_stats.sampled_count;
    state.viewer_fps_render_model_animation_disabled_count = stats->render_model_animation_sample_stats.disabled_count;
    state.viewer_fps_render_model_animation_missing_asset_data_count = stats->render_model_animation_sample_stats.missing_asset_data_count;
    state.viewer_fps_render_model_animation_invalid_skeleton_count = stats->render_model_animation_sample_stats.invalid_skeleton_count;
    state.viewer_fps_render_model_animation_failed_sample_count = stats->render_model_animation_sample_stats.failed_sample_count;
    state.viewer_fps_prepared_model_surface_draw_count = stats->prepared_model_surface_stats.draw_count;
    state.viewer_fps_prepared_model_surface_render_model_draw_count = stats->prepared_model_surface_stats.render_model_draw_count;
    state.viewer_fps_prepared_render_model_skin_table_skinned_surface_count = stats->prepared_render_model_skin_table_stats.render_model_skinned_surface_count;
    state.viewer_fps_prepared_render_model_skin_table_assigned_surface_count = stats->prepared_render_model_skin_table_stats.assigned_surface_count;
    state.viewer_fps_prepared_render_model_skin_table_entry_count = stats->prepared_render_model_skin_table_stats.table_entry_count;
    state.viewer_fps_prepared_render_model_skin_table_matrix_count = stats->prepared_render_model_skin_table_stats.matrix_count;
    state.viewer_fps_prepared_render_model_skin_table_bind_pose_fallback_count = stats->prepared_render_model_skin_table_stats.bind_pose_fallback_surface_count;
    state.viewer_fps_prepared_render_model_skin_table_invalid_skin_index_count = stats->prepared_render_model_skin_table_stats.invalid_skin_index_count;
    state.viewer_fps_area_cache_record_count = stats->area_cache_record_count;
    state.viewer_fps_area_cache_static_record_count = stats->area_cache_static_record_count;
    state.viewer_fps_area_cache_dynamic_record_count = stats->area_cache_dynamic_record_count;
    state.viewer_fps_area_cache_opaque_record_count = stats->area_cache_opaque_record_count;
    state.viewer_fps_area_cache_water_record_count = stats->area_cache_water_record_count;
    state.viewer_fps_area_cache_transparent_record_count = stats->area_cache_transparent_record_count;
    state.viewer_fps_area_cache_shadow_caster_record_count = stats->area_cache_shadow_caster_record_count;
    state.viewer_fps_area_cache_prepared_draw_count = stats->area_cache_prepared_draw_count;
    state.viewer_fps_area_cache_light_index_count = stats->area_cache_light_index_count;
    state.viewer_fps_area_cache_max_light_indices_per_record = stats->area_cache_max_light_indices_per_record;
    state.viewer_fps_area_cache_chunk_count = stats->area_cache_chunk_count;
    state.viewer_fps_area_cache_nonempty_chunk_count = stats->area_cache_nonempty_chunk_count;
    state.viewer_fps_area_cache_max_records_per_chunk = stats->area_cache_max_records_per_chunk;
    state.viewer_fps_area_frame_visible_record_count = stats->area_frame_visible_record_count;
    state.viewer_fps_area_frame_visible_static_record_count = stats->area_frame_visible_static_record_count;
    state.viewer_fps_area_frame_visible_dynamic_record_count = stats->area_frame_visible_dynamic_record_count;
    state.viewer_fps_area_frame_visible_chunk_count = stats->area_frame_visible_chunk_count;
    state.viewer_fps_area_frame_opaque_record_count = stats->area_frame_opaque_record_count;
    state.viewer_fps_area_frame_water_record_count = stats->area_frame_water_record_count;
    state.viewer_fps_area_frame_transparent_record_count = stats->area_frame_transparent_record_count;
    state.viewer_fps_area_frame_shadow_caster_record_count = stats->area_frame_shadow_caster_record_count;
    state.viewer_fps_area_frame_visible_prepared_surface_count = stats->area_frame_visible_prepared_surface_count;
    state.viewer_fps_area_frame_uses_cached_draw_lists = stats->area_frame_uses_cached_draw_lists;
    state.viewer_fps_local_light_count = stats->local_light_count;
    state.viewer_fps_local_light_colored_count = stats->local_light_colored_count;
    state.viewer_fps_local_light_color_max = stats->local_light_color_max;
    state.viewer_fps_local_light_intensity_max = stats->local_light_intensity_max;
    state.viewer_fps_local_light_selected_draw_count = stats->local_light_selected_draw_count;
    state.viewer_fps_local_light_selected_total = stats->local_light_selected_total;
    state.viewer_fps_local_light_selected_max = stats->local_light_selected_max;
    state.viewer_fps_local_light_selected_colored_total = stats->local_light_selected_colored_total;
    state.viewer_fps_local_light_selected_color_max = stats->local_light_selected_color_max;
    state.viewer_fps_local_light_selected_intensity_max = stats->local_light_selected_intensity_max;
    state.viewer_fps_forward_plus_light_count = stats->forward_plus_light_count;
    state.viewer_fps_forward_plus_cluster_count = stats->forward_plus_cluster_count;
    state.viewer_fps_forward_plus_active_cluster_count = stats->forward_plus_active_cluster_count;
    state.viewer_fps_forward_plus_cluster_light_index_count = stats->forward_plus_cluster_light_index_count;
    state.viewer_fps_forward_plus_max_lights_per_cluster = stats->forward_plus_max_lights_per_cluster;
    state.viewer_fps_forward_plus_overflow_cluster_count = stats->forward_plus_overflow_cluster_count;
    state.viewer_fps_forward_plus_overflow_light_count = stats->forward_plus_overflow_light_count;
    state.viewer_fps_forward_plus_upload_bytes = stats->forward_plus_upload_bytes;
    state.viewer_fps_forward_plus_tile_size = stats->forward_plus_tile_size;
    state.viewer_fps_forward_plus_depth_slices = stats->forward_plus_depth_slices;
    state.viewer_fps_shadow_cascade_count = stats->shadow_cascade_count;
    state.viewer_fps_shadow_resolution = stats->shadow_resolution;
    state.viewer_fps_shadow_caster_model_count = stats->shadow_caster_model_count;
    state.viewer_fps_shadow_no_caster_model_count = stats->shadow_no_caster_model_count;
    state.viewer_fps_shadow_submitted_model_count = stats->shadow_submitted_model_count;
    state.viewer_fps_shadow_culled_model_count = stats->shadow_culled_model_count;
    state.viewer_fps_main_pass_count = stats->main_pass_count;
    state.viewer_fps_draw_count = stats->total_command_stats.draw_count;
    state.viewer_fps_shadow_draw_count = stats->shadow_command_stats.draw_count;
    state.viewer_fps_transparent_draw_count = stats->transparent_command_stats.draw_count;
    state.viewer_fps_particle_draw_count = stats->particle_command_stats.draw_count;
    state.viewer_fps_indirect_draw_call_count = stats->total_command_stats.indirect_draw_call_count;
    state.viewer_fps_draw_instance_count = stats->total_command_stats.draw_instance_count;
    state.viewer_fps_draw_index_count = stats->total_command_stats.draw_index_count;
    state.viewer_fps_pipeline_bind_count = stats->total_command_stats.pipeline_bind_count;
    state.viewer_fps_pipeline_bind_skipped_count = stats->total_command_stats.pipeline_bind_skipped_count;
    state.viewer_fps_resource_bind_count = stats->total_command_stats.resource_bind_count;
    state.viewer_fps_resource_bind_skipped_count = stats->total_command_stats.resource_bind_skipped_count;
    state.viewer_fps_uniform_allocation_count = stats->total_command_stats.uniform_allocation_count;
    state.viewer_fps_uniform_allocation_bytes = stats->total_command_stats.uniform_allocation_bytes;
    state.viewer_fps_shadows_rendered = stats->shadows_rendered;
    state.viewer_fps_water_rendered = stats->water_rendered;
}

void update_client_gpu_metrics(AppState& state, const ClientGpuFrameStats* stats)
{
    if (!stats) {
        return;
    }

    smooth_viewer_metric(state.viewer_fps_editor_gpu_ui_seconds,
        state.viewer_fps_editor_gpu_ui_smoothed_seconds,
        stats->ui_seconds);
    smooth_viewer_metric(state.viewer_fps_editor_gpu_viewport_seconds,
        state.viewer_fps_editor_gpu_viewport_smoothed_seconds,
        stats->viewport_seconds);
    smooth_viewer_metric(state.viewer_fps_editor_gpu_overlay_seconds,
        state.viewer_fps_editor_gpu_overlay_smoothed_seconds,
        stats->overlay_seconds);
    smooth_viewer_metric(state.viewer_fps_editor_gpu_palette_seconds,
        state.viewer_fps_editor_gpu_palette_smoothed_seconds,
        stats->palette_seconds);
    smooth_viewer_metric(state.viewer_fps_editor_gpu_total_seconds,
        state.viewer_fps_editor_gpu_total_smoothed_seconds,
        stats->total_seconds);
    state.viewer_fps_editor_gpu_timer_count = stats->timer_count;
    state.viewer_fps_descriptor_allocation_failure_count = stats->command_stats.descriptor_allocation_failure_count;
    state.viewer_fps_descriptor_ring_capacity_bytes = stats->command_stats.descriptor_ring_capacity_bytes;
    state.viewer_fps_descriptor_ring_required_bytes = stats->command_stats.descriptor_ring_required_bytes;
    state.viewer_fps_resource_bind_failure_count = stats->command_stats.resource_bind_failure_count;
    state.viewer_fps_dropped_draw_count = stats->command_stats.dropped_draw_count;
}

std::string format_viewer_fps_rml(const AppState& state)
{
    const float frame_seconds = display_metric_seconds(
        state.viewer_fps_frame_seconds, state.viewer_fps_smoothed_seconds);
    if (frame_seconds <= 0.0f) {
        return "-- FPS";
    }

    const float work_seconds = display_metric_seconds(
        state.viewer_fps_work_seconds, state.viewer_fps_work_smoothed_seconds);
    const float sync_seconds = display_metric_seconds(
        state.viewer_fps_sync_seconds, state.viewer_fps_sync_smoothed_seconds);
    const float draw_seconds = display_metric_seconds(
        state.viewer_fps_draw_seconds, state.viewer_fps_draw_smoothed_seconds);
    const float ui_seconds = display_metric_seconds(
        state.viewer_fps_ui_seconds, state.viewer_fps_ui_smoothed_seconds);
    const float view_seconds = display_metric_seconds(
        state.viewer_fps_view_seconds, state.viewer_fps_view_smoothed_seconds);
    const float hud_seconds = display_metric_seconds(
        state.viewer_fps_hud_seconds, state.viewer_fps_hud_smoothed_seconds);
    const float overlay_seconds = display_metric_seconds(
        state.viewer_fps_overlay_seconds, state.viewer_fps_overlay_smoothed_seconds);
    const float palette_seconds = display_metric_seconds(
        state.viewer_fps_palette_seconds, state.viewer_fps_palette_smoothed_seconds);
    const float present_seconds = display_metric_seconds(
        state.viewer_fps_present_seconds, state.viewer_fps_present_smoothed_seconds);
    const float tick_seconds = display_metric_seconds(
        state.viewer_fps_tick_seconds, state.viewer_fps_tick_smoothed_seconds);
    const float setup_seconds = display_metric_seconds(
        state.viewer_fps_setup_seconds, state.viewer_fps_setup_smoothed_seconds);
    const float shadow_seconds = display_metric_seconds(
        state.viewer_fps_shadow_seconds, state.viewer_fps_shadow_smoothed_seconds);
    const float opaque_seconds = display_metric_seconds(
        state.viewer_fps_opaque_seconds, state.viewer_fps_opaque_smoothed_seconds);
    const float water_seconds = display_metric_seconds(
        state.viewer_fps_water_seconds, state.viewer_fps_water_smoothed_seconds);
    const float transparent_seconds = display_metric_seconds(
        state.viewer_fps_transparent_seconds, state.viewer_fps_transparent_smoothed_seconds);
    const float particles_seconds = display_metric_seconds(
        state.viewer_fps_particles_seconds, state.viewer_fps_particles_smoothed_seconds);
    const float debug_seconds = display_metric_seconds(
        state.viewer_fps_debug_seconds, state.viewer_fps_debug_smoothed_seconds);
    const float area_prepare_seconds = display_metric_seconds(
        state.viewer_fps_area_prepare_seconds, state.viewer_fps_area_prepare_smoothed_seconds);
    const float view_internal_seconds = display_metric_seconds(
        state.viewer_fps_view_internal_seconds, state.viewer_fps_view_internal_smoothed_seconds);
    const float gpu_shadow_seconds = display_metric_seconds(
        state.viewer_fps_gpu_shadow_seconds, state.viewer_fps_gpu_shadow_smoothed_seconds);
    const float gpu_opaque_seconds = display_metric_seconds(
        state.viewer_fps_gpu_opaque_seconds, state.viewer_fps_gpu_opaque_smoothed_seconds);
    const float gpu_water_seconds = display_metric_seconds(
        state.viewer_fps_gpu_water_seconds, state.viewer_fps_gpu_water_smoothed_seconds);
    const float gpu_transparent_seconds = display_metric_seconds(
        state.viewer_fps_gpu_transparent_seconds, state.viewer_fps_gpu_transparent_smoothed_seconds);
    const float gpu_particles_seconds = display_metric_seconds(
        state.viewer_fps_gpu_particles_seconds, state.viewer_fps_gpu_particles_smoothed_seconds);
    const float gpu_debug_seconds = display_metric_seconds(
        state.viewer_fps_gpu_debug_seconds, state.viewer_fps_gpu_debug_smoothed_seconds);
    const float gpu_total_seconds = display_metric_seconds(
        state.viewer_fps_gpu_total_seconds, state.viewer_fps_gpu_total_smoothed_seconds);
    const float editor_gpu_ui_seconds = display_metric_seconds(
        state.viewer_fps_editor_gpu_ui_seconds, state.viewer_fps_editor_gpu_ui_smoothed_seconds);
    const float editor_gpu_viewport_seconds = display_metric_seconds(
        state.viewer_fps_editor_gpu_viewport_seconds, state.viewer_fps_editor_gpu_viewport_smoothed_seconds);
    const float editor_gpu_overlay_seconds = display_metric_seconds(
        state.viewer_fps_editor_gpu_overlay_seconds, state.viewer_fps_editor_gpu_overlay_smoothed_seconds);
    const float editor_gpu_palette_seconds = display_metric_seconds(
        state.viewer_fps_editor_gpu_palette_seconds, state.viewer_fps_editor_gpu_palette_smoothed_seconds);
    const float editor_gpu_total_seconds = display_metric_seconds(
        state.viewer_fps_editor_gpu_total_seconds, state.viewer_fps_editor_gpu_total_smoothed_seconds);

    char compact_frame_text[128]{};
    std::snprintf(compact_frame_text,
        sizeof(compact_frame_text),
        "%.1f FPS frame %.1f | view %.1f ui %.1f present %.1f ms",
        static_cast<double>(1.0f / frame_seconds),
        static_cast<double>(frame_seconds * 1000.0f),
        static_cast<double>(view_seconds * 1000.0f),
        static_cast<double>(ui_seconds * 1000.0f),
        static_cast<double>(present_seconds * 1000.0f));

    char compact_gpu_text[128]{};
    std::snprintf(compact_gpu_text,
        sizeof(compact_gpu_text),
        "gpu vp %.2f pass %.2f ui %.2f ov %.2f pal %.2f ms",
        static_cast<double>(editor_gpu_viewport_seconds * 1000.0f),
        static_cast<double>(gpu_total_seconds * 1000.0f),
        static_cast<double>(editor_gpu_ui_seconds * 1000.0f),
        static_cast<double>(editor_gpu_overlay_seconds * 1000.0f),
        static_cast<double>(editor_gpu_palette_seconds * 1000.0f));

    char compact_scene_text[128]{};
    std::snprintf(compact_scene_text,
        sizeof(compact_scene_text),
        "vis %u chunks %u lights %u | draws %llu ind %llu",
        state.viewer_fps_area_frame_visible_record_count,
        state.viewer_fps_area_frame_visible_chunk_count,
        state.viewer_fps_forward_plus_light_count,
        static_cast<unsigned long long>(state.viewer_fps_draw_count),
        static_cast<unsigned long long>(state.viewer_fps_indirect_draw_call_count));

    std::string compact_result = escape_html(compact_frame_text);
    if (state.viewer_fps_gpu_timer_count > 0 || state.viewer_fps_editor_gpu_timer_count > 0) {
        compact_result += "<br/>";
        compact_result += escape_html(compact_gpu_text);
    }
    compact_result += "<br/>";
    compact_result += escape_html(compact_scene_text);
    if (!viewer_fps_overlay_verbose()) {
        return compact_result;
    }

    char frame_text[64]{};
    std::snprintf(frame_text,
        sizeof(frame_text),
        "%.1f FPS | frame %.1f ms",
        static_cast<double>(1.0f / frame_seconds),
        static_cast<double>(frame_seconds * 1000.0f));

    char cost_text[128]{};
    std::snprintf(cost_text,
        sizeof(cost_text),
        "work %.1f sync %.1f cpu-draw %.1f present %.1f ms",
        static_cast<double>(work_seconds * 1000.0f),
        static_cast<double>(sync_seconds * 1000.0f),
        static_cast<double>(draw_seconds * 1000.0f),
        static_cast<double>(present_seconds * 1000.0f));

    char draw_text[144]{};
    std::snprintf(draw_text,
        sizeof(draw_text),
        "ui %.1f view %.1f hud %.1f overlay %.1f palette %.1f ms",
        static_cast<double>(ui_seconds * 1000.0f),
        static_cast<double>(view_seconds * 1000.0f),
        static_cast<double>(hud_seconds * 1000.0f),
        static_cast<double>(overlay_seconds * 1000.0f),
        static_cast<double>(palette_seconds * 1000.0f));

    char view_text[160]{};
    std::snprintf(view_text,
        sizeof(view_text),
        "view total %.1f tick %.1f setup %.1f prep %.3f shadow %.1f particles %.1f debug %.1f ms",
        static_cast<double>(view_internal_seconds * 1000.0f),
        static_cast<double>(tick_seconds * 1000.0f),
        static_cast<double>(setup_seconds * 1000.0f),
        static_cast<double>(area_prepare_seconds * 1000.0f),
        static_cast<double>(shadow_seconds * 1000.0f),
        static_cast<double>(particles_seconds * 1000.0f),
        static_cast<double>(debug_seconds * 1000.0f));

    char gpu_text[192]{};
    std::snprintf(gpu_text,
        sizeof(gpu_text),
        "gpu total %.2f opaque %.2f shadow %.2f water %.2f trans %.2f ps %.2f debug %.2f ms timers %u",
        static_cast<double>(gpu_total_seconds * 1000.0f),
        static_cast<double>(gpu_opaque_seconds * 1000.0f),
        static_cast<double>(gpu_shadow_seconds * 1000.0f),
        static_cast<double>(gpu_water_seconds * 1000.0f),
        static_cast<double>(gpu_transparent_seconds * 1000.0f),
        static_cast<double>(gpu_particles_seconds * 1000.0f),
        static_cast<double>(gpu_debug_seconds * 1000.0f),
        state.viewer_fps_gpu_timer_count);

    char editor_gpu_text[160]{};
    std::snprintf(editor_gpu_text,
        sizeof(editor_gpu_text),
        "gpu editor total %.2f ui %.2f viewport %.2f overlay %.2f palette %.2f ms timers %u",
        static_cast<double>(editor_gpu_total_seconds * 1000.0f),
        static_cast<double>(editor_gpu_ui_seconds * 1000.0f),
        static_cast<double>(editor_gpu_viewport_seconds * 1000.0f),
        static_cast<double>(editor_gpu_overlay_seconds * 1000.0f),
        static_cast<double>(editor_gpu_palette_seconds * 1000.0f),
        state.viewer_fps_editor_gpu_timer_count);

    char pass_text[192]{};
    std::snprintf(pass_text,
        sizeof(pass_text),
        "passes opaque %.1f water %.1f trans %.1f ms | models %u ps %u lights %u/%u c%.2f i%.2f lit %u/%u/%u lc%u c%.2f i%.2f sh %u pass %u",
        static_cast<double>(opaque_seconds * 1000.0f),
        static_cast<double>(water_seconds * 1000.0f),
        static_cast<double>(transparent_seconds * 1000.0f),
        state.viewer_fps_model_count,
        state.viewer_fps_particle_system_count,
        state.viewer_fps_local_light_count,
        state.viewer_fps_local_light_colored_count,
        static_cast<double>(state.viewer_fps_local_light_color_max),
        static_cast<double>(state.viewer_fps_local_light_intensity_max),
        state.viewer_fps_local_light_selected_draw_count,
        state.viewer_fps_local_light_selected_total,
        state.viewer_fps_local_light_selected_max,
        state.viewer_fps_local_light_selected_colored_total,
        static_cast<double>(state.viewer_fps_local_light_selected_color_max),
        static_cast<double>(state.viewer_fps_local_light_selected_intensity_max),
        state.viewer_fps_shadow_cascade_count,
        state.viewer_fps_main_pass_count);

    char shadow_text[144]{};
    std::snprintf(shadow_text,
        sizeof(shadow_text),
        "shadow res %u casters %u no-caster %u submitted %u culled %u",
        state.viewer_fps_shadow_resolution,
        state.viewer_fps_shadow_caster_model_count,
        state.viewer_fps_shadow_no_caster_model_count,
        state.viewer_fps_shadow_submitted_model_count,
        state.viewer_fps_shadow_culled_model_count);

    char render_model_text[256]{};
    std::snprintf(render_model_text,
        sizeof(render_model_text),
        "rmodel samples in %zu ok %zu dis %zu miss %zu badskel %zu fail %zu | surf %u rm %u skin %u assign %u entries %u mats %u bind %u invalid %u",
        state.viewer_fps_render_model_animation_sample_input_count,
        state.viewer_fps_render_model_animation_sampled_count,
        state.viewer_fps_render_model_animation_disabled_count,
        state.viewer_fps_render_model_animation_missing_asset_data_count,
        state.viewer_fps_render_model_animation_invalid_skeleton_count,
        state.viewer_fps_render_model_animation_failed_sample_count,
        state.viewer_fps_prepared_model_surface_draw_count,
        state.viewer_fps_prepared_model_surface_render_model_draw_count,
        state.viewer_fps_prepared_render_model_skin_table_skinned_surface_count,
        state.viewer_fps_prepared_render_model_skin_table_assigned_surface_count,
        state.viewer_fps_prepared_render_model_skin_table_entry_count,
        state.viewer_fps_prepared_render_model_skin_table_matrix_count,
        state.viewer_fps_prepared_render_model_skin_table_bind_pose_fallback_count,
        state.viewer_fps_prepared_render_model_skin_table_invalid_skin_index_count);

    char area_cache_text[224]{};
    std::snprintf(area_cache_text,
        sizeof(area_cache_text),
        "area cache rec %u static %u dyn %u prep draws %u lights %u max %u chunks %u/%u max %u pass %u/%u/%u sh %u",
        state.viewer_fps_area_cache_record_count,
        state.viewer_fps_area_cache_static_record_count,
        state.viewer_fps_area_cache_dynamic_record_count,
        state.viewer_fps_area_cache_prepared_draw_count,
        state.viewer_fps_area_cache_light_index_count,
        state.viewer_fps_area_cache_max_light_indices_per_record,
        state.viewer_fps_area_cache_nonempty_chunk_count,
        state.viewer_fps_area_cache_chunk_count,
        state.viewer_fps_area_cache_max_records_per_chunk,
        state.viewer_fps_area_cache_opaque_record_count,
        state.viewer_fps_area_cache_water_record_count,
        state.viewer_fps_area_cache_transparent_record_count,
        state.viewer_fps_area_cache_shadow_caster_record_count);

    char area_frame_text[224]{};
    std::snprintf(area_frame_text,
        sizeof(area_frame_text),
        "area frame vis %u static %u dyn %u prep surf %u chunks %u lists %u/%u/%u sh %u cached %u",
        state.viewer_fps_area_frame_visible_record_count,
        state.viewer_fps_area_frame_visible_static_record_count,
        state.viewer_fps_area_frame_visible_dynamic_record_count,
        state.viewer_fps_area_frame_visible_prepared_surface_count,
        state.viewer_fps_area_frame_visible_chunk_count,
        state.viewer_fps_area_frame_opaque_record_count,
        state.viewer_fps_area_frame_water_record_count,
        state.viewer_fps_area_frame_transparent_record_count,
        state.viewer_fps_area_frame_shadow_caster_record_count,
        state.viewer_fps_area_frame_uses_cached_draw_lists ? 1u : 0u);

    char forward_plus_text[192]{};
    std::snprintf(forward_plus_text,
        sizeof(forward_plus_text),
        "f+ %s lights %u clusters %u/%u refs %u max %u ov %u/%u upload %.1f KB tile %u z %u dbg %s",
        state.shell.viewer_forward_plus_enabled ? "on" : "off",
        state.viewer_fps_forward_plus_light_count,
        state.viewer_fps_forward_plus_active_cluster_count,
        state.viewer_fps_forward_plus_cluster_count,
        state.viewer_fps_forward_plus_cluster_light_index_count,
        state.viewer_fps_forward_plus_max_lights_per_cluster,
        state.viewer_fps_forward_plus_overflow_cluster_count,
        state.viewer_fps_forward_plus_overflow_light_count,
        static_cast<double>(state.viewer_fps_forward_plus_upload_bytes) / 1024.0,
        state.viewer_fps_forward_plus_tile_size,
        state.viewer_fps_forward_plus_depth_slices,
        nw::toolset::forward_plus_debug_mode_label(state.shell.viewer_forward_plus_debug_mode));

    char submit_text[224]{};
    std::snprintf(submit_text,
        sizeof(submit_text),
        "submit draws %llu ind %llu inst %llu idx %.1fM sh %llu trans %llu ps %llu | pipe %llu/%llu res %llu/%llu ubos %llu %.1f KB desc %.1f/%.1f KB fail %llu/%llu drop %llu",
        static_cast<unsigned long long>(state.viewer_fps_draw_count),
        static_cast<unsigned long long>(state.viewer_fps_indirect_draw_call_count),
        static_cast<unsigned long long>(state.viewer_fps_draw_instance_count),
        static_cast<double>(state.viewer_fps_draw_index_count) / 1000000.0,
        static_cast<unsigned long long>(state.viewer_fps_shadow_draw_count),
        static_cast<unsigned long long>(state.viewer_fps_transparent_draw_count),
        static_cast<unsigned long long>(state.viewer_fps_particle_draw_count),
        static_cast<unsigned long long>(state.viewer_fps_pipeline_bind_count),
        static_cast<unsigned long long>(state.viewer_fps_pipeline_bind_skipped_count),
        static_cast<unsigned long long>(state.viewer_fps_resource_bind_count),
        static_cast<unsigned long long>(state.viewer_fps_resource_bind_skipped_count),
        static_cast<unsigned long long>(state.viewer_fps_uniform_allocation_count),
        static_cast<double>(state.viewer_fps_uniform_allocation_bytes) / 1024.0,
        static_cast<double>(state.viewer_fps_descriptor_ring_required_bytes) / 1024.0,
        static_cast<double>(state.viewer_fps_descriptor_ring_capacity_bytes) / 1024.0,
        static_cast<unsigned long long>(state.viewer_fps_descriptor_allocation_failure_count),
        static_cast<unsigned long long>(state.viewer_fps_resource_bind_failure_count),
        static_cast<unsigned long long>(state.viewer_fps_dropped_draw_count));

    std::string result = escape_html(frame_text) + "<br/>" + escape_html(cost_text) + "<br/>"
        + escape_html(draw_text) + "<br/>" + escape_html(view_text) + "<br/>" + escape_html(pass_text)
        + "<br/>" + escape_html(shadow_text);
    if (state.viewer_fps_model_count > 0 || state.viewer_fps_render_model_animation_sample_input_count > 0
        || state.viewer_fps_prepared_model_surface_draw_count > 0) {
        result += "<br/>";
        result += escape_html(render_model_text);
    }
    if (state.viewer_fps_gpu_timer_count > 0) {
        result += "<br/>";
        result += escape_html(gpu_text);
    }
    if (state.viewer_fps_editor_gpu_timer_count > 0) {
        result += "<br/>";
        result += escape_html(editor_gpu_text);
    }
    if (state.viewer_fps_area_cache_record_count > 0) {
        result += "<br/>";
        result += escape_html(area_cache_text);
        result += "<br/>";
        result += escape_html(area_frame_text);
    }
    if (state.viewer_fps_forward_plus_cluster_count > 0) {
        result += "<br/>";
        result += escape_html(forward_plus_text);
    }
    result += "<br/>";
    result += escape_html(submit_text);
    return result;
}

void sync_viewer_fps_overlay(Rml::ElementDocument* fps_doc,
    const std::optional<WorkspaceViewerViewportRequest>& viewer_viewport,
    const AppState& state)
{
    auto* overlay = find_el(fps_doc, "viewer_fps_overlay");
    if (!overlay) {
        return;
    }

    if (!viewer_viewport || !viewer_viewport->rect.valid()) {
        overlay->SetProperty("display", "none");
        return;
    }

    const bool verbose = viewer_fps_overlay_verbose();
    const int overlay_width = verbose ? 776 : 446;
    constexpr int kOverlayMargin = 8;
    const auto& rect = viewer_viewport->rect;
    const int rect_width = static_cast<int>(rect.width);
    const int left = std::max(rect.x + kOverlayMargin, rect.x + rect_width - overlay_width - kOverlayMargin);
    const int top = rect.y + kOverlayMargin;

    overlay->SetInnerRML(format_viewer_fps_rml(state));
    overlay->SetProperty("display", "block");
    overlay->SetProperty("width", std::to_string(verbose ? 760 : 430) + "px");
    overlay->SetProperty("height", std::to_string(verbose ? 144 : 54) + "px");
    overlay->SetProperty("left", std::to_string(left) + "px");
    overlay->SetProperty("top", std::to_string(top) + "px");
}

void toggle_command_palette(Rml::Context* context,
    Rml::Context* palette_context,
    Rml::ElementDocument* doc,
    Rml::ElementDocument* palette_doc,
    AppState& state,
    bool visible)
{
    const bool was_visible = state.command_palette_ui_visible;
    state.shell.set_command_palette_visible(visible);
    state.command_palette_ui_visible = visible;
    if (auto* palette = find_el(palette_doc, "command_palette")) {
        palette->SetClass("visible", visible);
    }
    if (visible) {
        if (!was_visible) {
            capture_command_palette_focus(context, state);
        }
        state.viewer_viewport_focused = false;
        if (!ensure_backend_ready(state)) {
            append_output(state, "warn", "Command palette unavailable: backend init failed");
        }
        refresh_command_palette(palette_doc, state);
        if (auto* input = find_el(palette_doc, "command_input")) {
            input->Focus();
        }
    } else if (was_visible || state.command_palette_restore_captured) {
        restore_command_palette_focus(context, palette_context, doc, state);
    }
}

void toggle_terminal(Rml::ElementDocument* doc, AppState& state, bool visible)
{
    state.shell.set_terminal_visible(visible);
    refresh_bottom_dock_view(doc, state);
    if (visible) {
        refresh_terminal_view(doc, state);
        if (auto* input = doc->GetElementById("terminal_input")) {
            input->Focus();
        }
    }
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
    return context;
}

std::string command_channel_class(nw::toolset::CommandOutputChannel channel)
{
    return std::string(nw::toolset::command_output_channel_name(channel));
}

void append_command_result(AppState& state, const nw::toolset::CommandResult& result)
{
    if (!result.should_log()) {
        return;
    }
    append_output(state, command_channel_class(result.output_channel), result.message);
}

void append_terminal_result(AppState& state, const nw::toolset::CommandResult& result)
{
    if (!result.should_log()) {
        return;
    }
    const std::string channel = command_channel_class(result.output_channel);
    append_terminal(state, channel, result.message);
    append_output(state, channel, result.message);
}

std::string format_command_candidates(const std::vector<nw::toolset::CommandSpec>& candidates)
{
    constexpr size_t max_listed = 8;
    std::string line = "Matches:";
    const size_t count = std::min(candidates.size(), max_listed);
    for (size_t i = 0; i < count; ++i) {
        line += (i == 0) ? " " : ", ";
        line += candidates[i].id;
    }
    if (candidates.size() > max_listed) {
        line += ", ...";
    }
    return line;
}

bool complete_terminal_command(Rml::ElementDocument* doc, AppState& state)
{
    if (!doc) {
        return false;
    }

    const std::string line = get_input_value(doc, "terminal_input");
    size_t cursor_byte_position = 0;
    if (!get_input_cursor_byte_position(doc, "terminal_input", line, cursor_byte_position)) {
        return false;
    }

    const auto completion = state.backend.complete_console_command(line, cursor_byte_position);
    if (completion.completed) {
        set_input_value_and_cursor(doc, "terminal_input", completion.replacement, completion.cursor_byte_position);
    }
    if (completion.ambiguous && !completion.candidates.empty()) {
        append_terminal(state, "info", format_command_candidates(completion.candidates));
    }
    return completion.completed || !completion.candidates.empty();
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

// Object variable text inputs are genuine single-field UI transactions. RmlUi
// emits change events for every keystroke, marks the Enter event with
// linebreak=true, and emits a separate non-bubbling blur event on focus loss.
// Reject invalid numeric mutations as they arrive, then commit only on Enter
// or blur so one user edit produces one command.
class ObjectVariableChangeListener final : public Rml::EventListener {
public:
    explicit ObjectVariableChangeListener(AppState& state)
        : state_{state}
    {
    }

    void ProcessEvent(Rml::Event& event) override
    {
        if (event == Rml::EventId::Change) {
            if (!event.GetParameter<bool>("linebreak", false)) {
                reject_invalid_numeric_input(event.GetTargetElement());
                return;
            }

            if (commit_input(event.GetTargetElement())) {
                suppress_blur_commit_ = true;
                event.GetTargetElement()->Blur();
                suppress_blur_commit_ = false;
            }
            return;
        }

        if (event != Rml::EventId::Blur || suppress_blur_commit_) {
            return;
        }

        (void)commit_input(event.GetTargetElement());
    }

private:
    static void reject_invalid_numeric_input(Rml::Element* target)
    {
        if (!target || !target->IsClassSet("object_variable_value")) {
            return;
        }

        auto* input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(target);
        if (!input) {
            return;
        }

        const auto type_value = parse_decimal_int32(
            input->GetAttribute<Rml::String>("data-type", ""));
        if (!type_value) {
            return;
        }
        const auto type = static_cast<nw::toolset::ObjectVariableType>(*type_value);
        if (type == nw::toolset::ObjectVariableType::string) {
            return;
        }

        const Rml::String value = input->GetValue();
        if (nw::toolset::valid_object_variable_input_prefix(type, value)) {
            input->SetAttribute("data-last-valid", value);
            return;
        }

        int selection_start = 0;
        input->GetSelection(&selection_start, nullptr, nullptr);
        const Rml::String previous = input->GetAttribute<Rml::String>(
            "data-last-valid", "");
        const auto rml_character_count = [](const Rml::String& text) {
            return static_cast<int>(std::min(
                Rml::StringUtilities::LengthUTF8(text),
                static_cast<size_t>(std::numeric_limits<int>::max())));
        };
        const int value_characters = rml_character_count(value);
        const int previous_characters = rml_character_count(previous);
        const int inserted_characters = std::max(
            value_characters - previous_characters, 0);
        const int cursor = std::clamp(
            selection_start - inserted_characters, 0, previous_characters);
        input->SetValue(previous);
        input->SetSelectionRange(cursor, cursor);
    }

    bool commit_input(Rml::Element* target)
    {
        const bool rename = target && target->IsClassSet("object_variable_name");
        const bool set_value = target && target->IsClassSet("object_variable_value");
        if (!rename && !set_value) {
            return false;
        }

        auto* input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(target);
        if (!input) {
            return false;
        }

        const std::string name = input->GetAttribute<Rml::String>("data-name", "");
        const std::string type = input->GetAttribute<Rml::String>("data-type", "");
        const std::string desired = input->GetValue();
        const auto result = dispatch_command(state_,
            rename ? "object.variables.rename" : "object.variables.set_value",
            {name, type, desired},
            nw::toolset::CommandSource::widget);
        append_command_result(state_, result);
        return result.ok();
    }

    AppState& state_;
    bool suppress_blur_commit_ = false;
};

bool commit_active_appearance_selection(AppState& state, int32_t value)
{
    const std::string selected = std::to_string(value);
    nw::toolset::CommandResult result;
    if (state.appearance_editor_field == AppearanceEditorField::appearance) {
        result = dispatch_command(state,
            "object.set_appearance",
            {std::string_view{selected}},
            nw::toolset::CommandSource::widget);
    } else {
        const std::string_view accessory = appearance_editor_field_name(
            state.appearance_editor_field);
        result = dispatch_command(state,
            "object.creature.set_accessory",
            {accessory, std::string_view{selected}},
            nw::toolset::CommandSource::widget);
    }
    append_command_result(state, result);
    return result.ok();
}

bool commit_active_body_part_selection(AppState& state, int32_t value)
{
    if (!active_body_part_options_match_tab(state)) {
        return false;
    }

    const std::string part = std::to_string(state.body_part_option_part);
    const std::string selected = std::to_string(value);
    const auto result = dispatch_command(state,
        "object.creature.set_body_part",
        {std::string_view{part}, std::string_view{selected}},
        nw::toolset::CommandSource::widget);
    append_command_result(state, result);
    (void)sync_live_body_part_option(state);
    return result.ok();
}

bool cycle_active_body_part_selection(AppState& state, int delta)
{
    const auto previous = state.body_part_combobox.selected_key();
    (void)state.body_part_combobox.move_selection(delta);
    const auto selected = state.body_part_combobox.selected_key();
    if (!selected || selected == previous
        || !commit_active_body_part_selection(state, *selected)) {
        return false;
    }

    state.body_part_combobox.hide_popup();
    return true;
}

bool commit_active_color_selection(AppState& state, int32_t value)
{
    if (!active_color_editor_matches_tab(state)) {
        return false;
    }

    const std::string color = std::to_string(state.color_editor_channel);
    const std::string selected = std::to_string(value);
    const auto result = dispatch_command(state,
        "object.creature.set_color",
        {std::string_view{color}, std::string_view{selected}},
        nw::toolset::CommandSource::widget);
    append_command_result(state, result);
    return result.ok();
}

nw::ObjectHandle desired_appearance_body_preview(const AppState& state) noexcept
{
    if (state.object_workbench_surface == ObjectWorkbenchSurface::appearance
        && active_object_details_matches_tab(state)
        && state.object_details.object.type == nw::ObjectType::creature) {
        return state.object_details.object;
    }
    return nw::ObjectHandle{};
}

bool update_appearance_preview_rows(nw::ObjectHandle object, bool equipment_visible)
{
    if (object.type != nw::ObjectType::creature || !nw::kernel::objects().valid(object)) {
        return false;
    }

    auto& runtime = nw::kernel::runtime();
    auto object_value = nw::smalls::Value::make_object(object);
    object_value.type_id = runtime.object_subtype_for_tag(object.type);
    const auto result = runtime.execute_script("nwn1.creature", "update_appearance_preview",
        {object_value, nw::smalls::Value::make_bool(equipment_visible)});
    return result.ok() && result.value.type_id == runtime.bool_type() && result.value.data.bval;
}

bool refresh_active_viewer_object_visual(
    ClientRenderer& renderer, nw::ObjectHandle object)
{
    if (renderer.active_viewer_object() != object) {
        return true;
    }
    return renderer.refresh_live_viewer_object_visual(object);
}

bool sync_appearance_body_preview(ClientRenderer& renderer, AppState& state)
{
    const nw::ObjectHandle desired = desired_appearance_body_preview(state);
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

bool editable_area_object(nw::ObjectHandle object) noexcept
{
    return object.type == nw::ObjectType::creature || object.type == nw::ObjectType::placeable;
}

std::string precise_float_text(float value)
{
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(
        buffer.data(), buffer.data() + buffer.size(), value, std::chars_format::general,
        std::numeric_limits<float>::max_digits10);
    return result.ec == std::errc{} ? std::string{buffer.data(), result.ptr} : std::string{};
}

void sync_area_object_after_command(
    ClientRenderer& renderer, AppState& state, const nw::toolset::CommandResult& result)
{
    append_command_result(state, result);
    const auto object = state.smalls.active_object();
    if (editable_area_object(object)) {
        renderer.sync_viewer_area_object_spatial(object);
    }
}

bool begin_area_object_drag(ClientRenderer& renderer,
    AppState& state,
    Rml::Vector2f point,
    const WorkspaceViewerViewportRequest& viewport)
{
    const nw::ObjectHandle object = renderer.active_viewer_object();
    if (viewport.kind != WorkspaceViewerViewportKind::area || !editable_area_object(object)) {
        return false;
    }

    const auto* spatial = nw::kernel::objects().components().find_spatial(object);
    if (!spatial) {
        return false;
    }
    const auto surface_point = renderer.viewer_area_surface_point(
        point.x, point.y, viewport.rect);
    if (!surface_point) {
        return false;
    }

    state.area_object_drag = {
        .before = *spatial,
        .preview = *spatial,
        .grab_offset = spatial->position - *surface_point,
        .active = true,
    };
    state.smalls.publish_active_object(object);
    state.active_object_tab_id = state.workspace.active_tab_id();
    return true;
}

bool update_area_object_drag(ClientRenderer& renderer,
    AppState& state,
    Rml::Vector2f point,
    const WorkspaceViewerViewportRequest& viewport)
{
    if (!state.area_object_drag.active || viewport.kind != WorkspaceViewerViewportKind::area) {
        return false;
    }
    const auto surface_point = renderer.viewer_area_surface_point(
        point.x, point.y, viewport.rect);
    if (!surface_point) {
        return false;
    }

    const glm::vec3 position = *surface_point + state.area_object_drag.grab_offset;
    if (position == state.area_object_drag.preview.position) {
        return true;
    }
    state.area_object_drag.preview.position = position;
    state.area_object_drag.moved = position != state.area_object_drag.before.position;
    renderer.preview_viewer_area_object_spatial(state.area_object_drag.preview);
    return true;
}

void cancel_area_object_drag(ClientRenderer& renderer, AppState& state)
{
    if (!state.area_object_drag.active) {
        return;
    }
    renderer.sync_viewer_area_object_spatial(state.area_object_drag.before.owner);
    state.area_object_drag = {};
}

void commit_area_object_drag(ClientRenderer& renderer, AppState& state)
{
    if (!state.area_object_drag.active) {
        return;
    }

    const auto drag = state.area_object_drag;
    state.area_object_drag = {};
    if (!drag.moved || state.smalls.active_object() != drag.before.owner) {
        renderer.sync_viewer_area_object_spatial(drag.before.owner);
        return;
    }

    const std::array<std::string, 3> args{
        precise_float_text(drag.preview.position.x),
        precise_float_text(drag.preview.position.y),
        precise_float_text(drag.preview.position.z),
    };
    if (std::any_of(args.begin(), args.end(), [](const auto& arg) { return arg.empty(); })) {
        renderer.sync_viewer_area_object_spatial(drag.before.owner);
        return;
    }
    const auto result = dispatch_command(state,
        "object.transform.set_position",
        {args[0], args[1], args[2]},
        nw::toolset::CommandSource::renderer);
    sync_area_object_after_command(renderer, state, result);
}

bool placement_blueprint_resource(const nw::Resource& resource) noexcept
{
    return resource.valid()
        && (resource.type == nw::ResourceType::utc
            || resource.type == nw::ResourceType::utp);
}

bool area_object_placement_position_valid(nw::ObjectHandle area, glm::vec3 position)
{
    constexpr float k_tile_size = 10.0f;
    if (area.type != nw::ObjectType::area) {
        return false;
    }
    const auto* live_area = nw::kernel::objects().get<nw::Area>(area);
    return live_area && live_area->width > 0 && live_area->height > 0
        && std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z)
        && position.x >= 0.0f
        && position.x <= static_cast<float>(live_area->width) * k_tile_size
        && position.y >= 0.0f
        && position.y <= static_cast<float>(live_area->height) * k_tile_size;
}

void arm_area_object_placement(ClientRenderer& renderer,
    AppState& state,
    nw::Resource resource,
    Rml::Vector2f point)
{
    if (!placement_blueprint_resource(resource)) {
        return;
    }

    const auto previous_selection = renderer.active_viewer_object();
    state.area_object_placement = {
        .resource = std::move(resource),
        .previous_selection = previous_selection,
        .drag_start = point,
        .tab_id = state.workspace.active_tab_id(),
        .phase = AreaObjectPlacementPhase::armed,
    };
}

void cancel_area_object_placement(ClientRenderer& renderer, AppState& state)
{
    if (!state.area_object_placement.active()) {
        return;
    }

    const auto placement = std::move(state.area_object_placement);
    state.area_object_placement = {};
    if (nw::kernel::objects().valid(placement.object)) {
        nw::kernel::objects().destroy(placement.object);
    }
    if (placement.object.type != nw::ObjectType::invalid
        && renderer.area_viewer_object() == placement.area) {
        const auto selected = nw::kernel::objects().valid(placement.previous_selection)
            ? placement.previous_selection
            : nw::ObjectHandle{};
        if (!renderer.rebuild_live_viewer_area(placement.area, selected)) {
            append_output(state, "error", "Area object placement cancellation rebuild failed");
        }
    }
}

bool update_area_object_placement(ClientRenderer& renderer,
    AppState& state,
    Rml::Vector2f point,
    const std::optional<WorkspaceViewerViewportRequest>& viewport)
{
    auto& placement = state.area_object_placement;
    if (!placement.active()) {
        return false;
    }

    const float dx = point.x - placement.drag_start.x;
    const float dy = point.y - placement.drag_start.y;
    if (!placement.threshold_crossed
        && std::abs(dx) < kWorkspaceTabDragThresholdPx
        && std::abs(dy) < kWorkspaceTabDragThresholdPx) {
        return false;
    }
    placement.threshold_crossed = true;
    state.pressed_recent_index = -1;

    if (!viewport || viewport->kind != WorkspaceViewerViewportKind::area
        || !point_within_viewport(viewport->rect, point)) {
        if (placement.object.type != nw::ObjectType::invalid) {
            placement.phase = AreaObjectPlacementPhase::ghost_invalid;
        }
        return true;
    }

    const auto area = renderer.area_viewer_object();
    if (area.type != nw::ObjectType::area || !nw::kernel::objects().valid(area)
        || state.workspace.active_tab_id() != placement.tab_id) {
        placement.phase = AreaObjectPlacementPhase::ghost_invalid;
        return true;
    }
    if (placement.area.type != nw::ObjectType::invalid && placement.area != area) {
        placement.phase = AreaObjectPlacementPhase::ghost_invalid;
        return true;
    }

    const auto surface_point = renderer.viewer_area_surface_point(
        point.x, point.y, viewport->rect);
    if (!surface_point) {
        placement.phase = AreaObjectPlacementPhase::ghost_invalid;
        return true;
    }
    const bool valid_position = area_object_placement_position_valid(area, *surface_point);
    if (placement.object.type == nw::ObjectType::invalid) {
        if (!valid_position || placement.materialization_failed) {
            placement.phase = AreaObjectPlacementPhase::ghost_invalid;
            return true;
        }

        const std::array placement_rows{nw::toolset::AreaObjectBlueprintPlacement{
            .resource = placement.resource,
            .transform = {
                .position = *surface_point,
                .orientation = {1.0f, 0.0f, 0.0f},
                .scale = {1.0f, 1.0f, 1.0f},
            },
        }};
        auto loaded = nw::toolset::load_area_object_blueprints(area, placement_rows);
        if (!loaded.ok() || loaded.objects.size() != 1) {
            placement.materialization_failed = true;
            placement.phase = AreaObjectPlacementPhase::ghost_invalid;
            append_output(state,
                loaded.status == nw::toolset::AreaObjectBlueprintLoadStatus::failed ? "error" : "warn",
                loaded.diagnostic.empty() ? "Area object placement load failed" : loaded.diagnostic);
            return true;
        }

        placement.area = area;
        placement.object = loaded.objects.front();
        const std::array objects{placement.object};
        if (!renderer.append_viewer_area_object_previews(objects, kAreaObjectPlacementOpacity)) {
            nw::kernel::objects().destroy(placement.object);
            placement.object = nw::ObjectHandle{};
            placement.materialization_failed = true;
            placement.phase = AreaObjectPlacementPhase::ghost_invalid;
            append_output(state, "error", "Area object placement preview construction failed");
            return true;
        }
        const auto* spatial = nw::kernel::objects().components().find_spatial(placement.object);
        if (!spatial) {
            cancel_area_object_placement(renderer, state);
            return true;
        }
        placement.preview = *spatial;
    }

    placement.preview.position = *surface_point;
    if (!renderer.preview_viewer_area_object_spatial(placement.preview)) {
        append_output(state, "error", "Area object placement preview update failed");
        cancel_area_object_placement(renderer, state);
        return true;
    }
    placement.phase = valid_position
        ? AreaObjectPlacementPhase::ghost_valid
        : AreaObjectPlacementPhase::ghost_invalid;
    return true;
}

void commit_area_object_placement(ClientRenderer& renderer, AppState& state)
{
    if (!state.area_object_placement.active()) {
        return;
    }
    if (state.area_object_placement.phase != AreaObjectPlacementPhase::ghost_valid
        || !nw::kernel::objects().valid(state.area_object_placement.object)) {
        cancel_area_object_placement(renderer, state);
        return;
    }

    const auto placement = std::move(state.area_object_placement);
    state.area_object_placement = {};
    if (!nw::kernel::objects().components().set_position(
            placement.object, placement.preview.position)) {
        if (nw::kernel::objects().valid(placement.object)) {
            nw::kernel::objects().destroy(placement.object);
        }
        const auto selected = nw::kernel::objects().valid(placement.previous_selection)
            ? placement.previous_selection
            : nw::ObjectHandle{};
        if (!renderer.rebuild_live_viewer_area(placement.area, selected)) {
            append_output(state, "error", "Area object placement rollback rebuild failed");
        }
        append_output(state, "error", "Area object placement spatial commit failed");
        return;
    }

    const std::array objects{placement.object};
    auto result = state.backend.place_area_objects(
        placement.area,
        objects,
        command_context(state, nw::toolset::CommandSource::renderer));
    append_command_result(state, result);
    if (!result.ok()) {
        if (nw::kernel::objects().valid(placement.object)) {
            nw::kernel::objects().destroy(placement.object);
        }
        const auto selected = nw::kernel::objects().valid(placement.previous_selection)
            ? placement.previous_selection
            : nw::ObjectHandle{};
        if (!renderer.rebuild_live_viewer_area(placement.area, selected)) {
            append_output(state, "error", "Area object placement rollback rebuild failed");
        }
    }
}

bool project_item_drag_resource(const nw::Resource& resource,
    const std::filesystem::path& source_path) noexcept
{
    return resource.valid()
        && resource.type == nw::ResourceType::uti
        && source_path.extension() == ".json";
}

void clear_project_item_drop_visuals(Rml::ElementDocument* doc,
    const ProjectItemDragState& drag)
{
    if (auto* target = find_el(doc, "creature_inventory_drop_target")) {
        target->SetProperty("display", "none");
        target->SetClass("valid", false);
        target->SetClass("invalid", false);
    }
    if (drag.target.kind == ProjectItemDropTargetKind::equipment
        && static_cast<uint32_t>(drag.target.slot) < 18) {
        const std::string id = "creature_equipment_slot_"
            + std::to_string(static_cast<uint32_t>(drag.target.slot));
        if (auto* slot = find_el(doc, id.c_str())) {
            slot->SetClass("drop_valid", false);
            slot->SetClass("drop_invalid", false);
        }
    }
}

void set_project_item_drop_target(Rml::ElementDocument* doc,
    ProjectItemDragState& drag,
    ProjectItemDropTarget target,
    bool valid)
{
    clear_project_item_drop_visuals(doc, drag);
    drag.target = target;
    drag.phase = valid
        ? ProjectItemDragPhase::target_valid
        : ProjectItemDragPhase::target_invalid;

    if (target.kind == ProjectItemDropTargetKind::inventory) {
        if (auto* overlay = find_el(doc, "creature_inventory_drop_target")) {
            const int top = (target.row - drag.height + 1) * kCreatureInventoryCellPx;
            overlay->SetProperty("display", "block");
            overlay->SetProperty("left",
                std::to_string(target.column * kCreatureInventoryCellPx) + "px");
            overlay->SetProperty("top", std::to_string(top) + "px");
            overlay->SetProperty("width",
                std::to_string(drag.width * kCreatureInventoryCellPx) + "px");
            overlay->SetProperty("height",
                std::to_string(drag.height * kCreatureInventoryCellPx) + "px");
            overlay->SetClass("valid", valid);
            overlay->SetClass("invalid", !valid);
        }
    } else if (target.kind == ProjectItemDropTargetKind::equipment
        && static_cast<uint32_t>(target.slot) < 18) {
        const std::string id = "creature_equipment_slot_"
            + std::to_string(static_cast<uint32_t>(target.slot));
        if (auto* slot = find_el(doc, id.c_str())) {
            slot->SetClass("drop_valid", valid);
            slot->SetClass("drop_invalid", !valid);
        }
    }
}

void arm_project_item_drag(AppState& state,
    const nw::Resource& resource,
    const std::filesystem::path& source_path,
    Rml::Vector2f point)
{
    if (!project_item_drag_resource(resource, source_path)
        || state.object_workbench_surface != ObjectWorkbenchSurface::inventory
        || !active_creature_inventory_matches_tab(state)) {
        return;
    }

    state.project_item_drag = {
        .source_path = source_path,
        .owner = state.creature_inventory.object,
        .drag_start = point,
        .tab_id = state.workspace.active_tab_id(),
        .phase = ProjectItemDragPhase::armed,
    };
}

void cancel_project_item_drag(Rml::ElementDocument* doc, AppState& state)
{
    if (!state.project_item_drag.active()) {
        return;
    }
    clear_project_item_drop_visuals(doc, state.project_item_drag);
    const auto item = state.project_item_drag.item;
    state.project_item_drag = {};
    if (nw::kernel::objects().valid(item)) {
        nw::kernel::objects().destroy(item);
    }
}

bool materialize_project_item_drag(AppState& state)
{
    auto& drag = state.project_item_drag;
    if (drag.item.type == nw::ObjectType::item) {
        return true;
    }
    if (drag.materialization_failed) {
        return false;
    }

    std::ifstream input{drag.source_path};
    input >> std::ws;
    if (!input || input.peek() != '{') {
        drag.materialization_failed = true;
        append_output(state, "warn", "Item drag requires an authored component/propset JSON blueprint");
        return false;
    }

    auto* item = nw::kernel::objects().load_file<nw::Item>(drag.source_path);
    const auto* layout = item
        ? nw::kernel::objects().components().find_item_layout(item->handle())
        : nullptr;
    if (!item || !layout || layout->inventory_width <= 0
        || layout->inventory_height <= 0
        || layout->inventory_width > nw::Inventory::max_columns
        || layout->inventory_height > nw::Inventory::max_rows) {
        if (item) {
            nw::kernel::objects().destroy(item->handle());
        }
        drag.materialization_failed = true;
        append_output(state, "error", "Item drag blueprint has no valid inventory footprint");
        return false;
    }

    drag.item = item->handle();
    drag.width = layout->inventory_width;
    drag.height = layout->inventory_height;
    return true;
}

bool update_project_item_drag(Rml::Context* context,
    Rml::ElementDocument* doc,
    AppState& state,
    Rml::Vector2f point)
{
    auto& drag = state.project_item_drag;
    if (!drag.active()) {
        return false;
    }

    const float dx = point.x - drag.drag_start.x;
    const float dy = point.y - drag.drag_start.y;
    if (!drag.threshold_crossed
        && std::abs(dx) < kWorkspaceTabDragThresholdPx
        && std::abs(dy) < kWorkspaceTabDragThresholdPx) {
        return false;
    }
    drag.threshold_crossed = true;
    state.pressed_recent_index = -1;

    if (state.workspace.active_tab_id() != drag.tab_id
        || state.object_workbench_surface != ObjectWorkbenchSurface::inventory
        || state.creature_inventory.object != drag.owner
        || !active_creature_inventory_matches_tab(state)
        || !materialize_project_item_drag(state)) {
        set_project_item_drop_target(doc, drag, {}, false);
        return true;
    }

    auto* creature = drag.owner.type == nw::ObjectType::creature
        ? nw::kernel::objects().get<nw::Creature>(drag.owner)
        : nullptr;
    auto* owner_item = drag.owner.type == nw::ObjectType::item
        ? nw::kernel::objects().get<nw::Item>(drag.owner)
        : nullptr;
    nw::Inventory* inventory = creature ? &creature->inventory()
        : owner_item                    ? &owner_item->inventory()
                                        : nullptr;
    if (!inventory) {
        set_project_item_drop_target(doc, drag, {}, false);
        return true;
    }

    auto* hit = context ? context->GetElementAtPoint(point) : nullptr;
    if (creature) {
        if (auto* equipment = find_ancestor_with_class(hit, "creature_equipment_slot")) {
            const auto slot_value = parse_decimal_int32(
                equipment->GetAttribute<Rml::String>("data-slot", ""));
            ProjectItemDropTarget target;
            target.kind = ProjectItemDropTargetKind::equipment;
            if (slot_value && *slot_value >= 0 && *slot_value < 18) {
                target.slot = static_cast<nw::EquipIndex>(*slot_value);
            }
            if (target == drag.target
                && (drag.phase == ProjectItemDragPhase::target_valid
                    || drag.phase == ProjectItemDragPhase::target_invalid)) {
                return true;
            }
            const auto* layout = nw::kernel::objects().components().find_item_layout(drag.item);
            auto* item = nw::kernel::objects().get<nw::Item>(drag.item);
            const bool valid = static_cast<uint32_t>(target.slot) < 18
                && item && layout
                && !nw::get_equipped_item(creature, target.slot)
                && nw::toolset::can_place_creature_item_in_slot(drag.item, target.slot)
                && creature->inventory().find_slot(
                                            layout->inventory_width, layout->inventory_height)
                        .page
                    >= 0;
            set_project_item_drop_target(doc, drag, target, valid);
            return true;
        }
    }

    auto* board = find_el(doc, "creature_inventory_board");
    if (!board || !board->IsPointWithinElement(point)) {
        set_project_item_drop_target(doc, drag, {}, false);
        return true;
    }

    const float left = board->GetAbsoluteLeft() + board->GetClientLeft();
    const float top = board->GetAbsoluteTop() + board->GetClientTop();
    const int column = static_cast<int>((point.x - left) / kCreatureInventoryCellPx);
    const int visual_row = static_cast<int>((point.y - top) / kCreatureInventoryCellPx);
    ProjectItemDropTarget target{
        .kind = ProjectItemDropTargetKind::inventory,
        .page = state.creature_inventory_page,
        .row = visual_row + drag.height - 1,
        .column = column,
    };
    if (target == drag.target
        && (drag.phase == ProjectItemDragPhase::target_valid
            || drag.phase == ProjectItemDragPhase::target_invalid)) {
        return true;
    }
    const bool in_bounds = target.page >= 0
        && target.page < inventory->pages()
        && visual_row >= 0
        && visual_row + drag.height <= inventory->rows()
        && column >= 0
        && column + drag.width <= inventory->columns();
    const bool valid = in_bounds
        && inventory->check_available(
            target.page, target.row, target.column, drag.width, drag.height);
    set_project_item_drop_target(doc, drag, target, valid);
    return true;
}

void commit_project_item_drag(Rml::ElementDocument* doc, AppState& state)
{
    if (!state.project_item_drag.active()) {
        return;
    }
    if (state.project_item_drag.phase != ProjectItemDragPhase::target_valid
        || !nw::kernel::objects().valid(state.project_item_drag.item)) {
        cancel_project_item_drag(doc, state);
        return;
    }

    clear_project_item_drop_visuals(doc, state.project_item_drag);
    const auto drag = std::move(state.project_item_drag);
    state.project_item_drag = {};
    nw::toolset::ItemPlacement placement{
        .item = drag.item,
        .target = drag.target.kind == ProjectItemDropTargetKind::equipment
            ? nw::toolset::ItemPlacementTarget::equipment
            : nw::toolset::ItemPlacementTarget::inventory,
        .page = drag.target.page,
        .row = drag.target.row,
        .column = drag.target.column,
        .slot = drag.target.slot,
    };
    const std::array placements{placement};
    auto result = state.backend.place_items(
        drag.owner,
        placements,
        command_context(state, nw::toolset::CommandSource::renderer));
    append_command_result(state, result);
}

bool handle_area_object_key(ClientRenderer& renderer,
    Rml::Context* context,
    Rml::ElementDocument* doc,
    AppState& state,
    const SDL_KeyboardEvent& key,
    int frame_width,
    int frame_height)
{
    if (key.repeat || key.key != SDLK_R
        || (key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI | SDL_KMOD_SHIFT))
        || state.shell.command_palette_visible || state.module_dialog_open
        || focused_text_input(context)) {
        return false;
    }
    const auto viewport = active_workspace_viewer_viewport_request(
        doc, state, frame_width, frame_height);
    const auto object = renderer.active_viewer_object();
    if (!viewport || viewport->kind != WorkspaceViewerViewportKind::area
        || !editable_area_object(object)) {
        return false;
    }

    const auto result = dispatch_command(state,
        "object.transform.randomize_orientation", {}, nw::toolset::CommandSource::shortcut);
    sync_area_object_after_command(renderer, state, result);
    return true;
}

std::optional<nw::toolset::CommandPromptAction> show_command_prompt(
    SDL_Window* window, const nw::toolset::CommandPrompt& prompt)
{
    std::vector<SDL_MessageBoxButtonData> buttons;
    buttons.reserve(prompt.actions.size());
    for (size_t i = 0; i < prompt.actions.size(); ++i) {
        SDL_MessageBoxButtonFlags flags = 0;
        if (prompt.actions[i].id == "save") {
            flags |= SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
        }
        if (prompt.actions[i].id == "cancel") {
            flags |= SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
        }
        buttons.push_back(SDL_MessageBoxButtonData{
            .flags = flags,
            .buttonID = static_cast<int>(i),
            .text = prompt.actions[i].label.c_str(),
        });
    }

    std::string message = prompt.message;
    if (!prompt.detail.empty()) {
        message += "\n\n";
        message += prompt.detail;
    }
    const SDL_MessageBoxData message_box{
        .flags = SDL_MESSAGEBOX_WARNING | SDL_MESSAGEBOX_BUTTONS_RIGHT_TO_LEFT,
        .window = window,
        .title = prompt.title.c_str(),
        .message = message.c_str(),
        .numbuttons = static_cast<int>(buttons.size()),
        .buttons = buttons.data(),
        .colorScheme = nullptr,
    };

    int button_id = -1;
    if (!SDL_ShowMessageBox(&message_box, &button_id)
        || button_id < 0
        || static_cast<size_t>(button_id) >= prompt.actions.size()) {
        return std::nullopt;
    }
    return prompt.actions[static_cast<size_t>(button_id)];
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
        const auto action = show_command_prompt(window, *result.prompt);
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

void show_open_module_dialog(SDL_Window* window, AppState& state)
{
    if (state.open_module_dialog_event == 0) {
        append_output(state, "error", "Open module dialog unavailable");
        return;
    }
    if (state.module_dialog_open) {
        append_output(state, "info", "Open module dialog already active");
        return;
    }

    static constexpr SDL_DialogFileFilter filters[] = {
        {"Neverwinter modules", "mod;zip"},
        {"All files", "*"},
    };

    state.module_dialog_default_location = module_dialog_start_location().string();
    const char* default_location = state.module_dialog_default_location.empty()
        ? nullptr
        : state.module_dialog_default_location.c_str();

    state.module_dialog_command = "toolset.open";
    state.module_dialog_open = true;
    SDL_ShowOpenFileDialog(open_module_dialog_callback,
        new OpenModuleDialogRequest{state.open_module_dialog_event},
        window,
        filters,
        static_cast<int>(sizeof(filters) / sizeof(filters[0])),
        default_location,
        false);
}

void show_open_project_dialog(SDL_Window* window, AppState& state)
{
    if (state.open_module_dialog_event == 0) {
        append_output(state, "error", "Open project dialog unavailable");
        return;
    }
    if (state.module_dialog_open) {
        append_output(state, "info", "Open dialog already active");
        return;
    }

    state.module_dialog_default_location = project_dialog_start_location().string();
    const char* default_location = state.module_dialog_default_location.empty()
        ? nullptr
        : state.module_dialog_default_location.c_str();

    state.module_dialog_command = "toolset.open_project";
    state.module_dialog_open = true;
    SDL_ShowOpenFolderDialog(open_module_dialog_callback,
        new OpenModuleDialogRequest{state.open_module_dialog_event},
        window,
        default_location,
        false);
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

void handle_open_module_dialog_result(Rml::ElementDocument* doc, AppState& state, SDL_Event& event)
{
    auto* result = static_cast<OpenModuleDialogResult*>(event.user.data1);
    state.module_dialog_open = false;
    const std::string command = state.module_dialog_command.empty()
        ? "toolset.open"
        : state.module_dialog_command;
    state.module_dialog_command.clear();
    if (!result) {
        return;
    }

    const std::string path = std::move(result->path);
    const std::string error = std::move(result->error);
    const bool canceled = result->canceled;
    delete result;

    if (!error.empty()) {
        append_output(state, "error", std::string{"Open module dialog failed: "} + error);
        return;
    }
    if (canceled || path.empty()) {
        return;
    }
    if (!ensure_backend_ready(state)) {
        append_output(state, "error", "Backend initialization failed");
        return;
    }

    const bool was_showing_areas = state.shell.showing_areas;
    const bool was_showing_project = state.shell.showing_project_tree;
    const auto command_result = dispatch_command(state,
        command,
        {std::string_view{path}},
        nw::toolset::CommandSource::palette);
    append_command_result(state, command_result);
    if (command_result.ok() && command == "toolset.open_project") {
        remember_recent_project(state, state.backend.current_project_dir());
    }
    refresh_bottom_dock_view(doc, state);
    if (command_result.ok()
        && (state.shell.showing_areas
            || state.shell.showing_project_tree
            || state.shell.showing_areas != was_showing_areas
            || state.shell.showing_project_tree != was_showing_project)) {
        state.selected_recent_index = -1;
        set_input_value(doc, "recent_search", "");
        refresh_recent_list(doc, state);
    }
    if (command_result.ok()) {
        refresh_workspace_view(doc, state);
    }
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
    if (state.active_object_tab_id.empty()) {
        return;
    }

    const auto* active_tab = state.workspace.active_tab();
    if (active_tab_has_object_workbench(active_tab)
        && active_tab->id == state.active_object_tab_id) {
        return;
    }

    state.smalls.clear_active_object();
    state.smalls.clear_active_area();
    state.active_object_tab_id.clear();
}

void print_cli_usage(std::ostream& out)
{
    out << "Usage:\n"
        << "  rollnw-client init <project-dir>\n"
        << "  rollnw-client import (--json|--legacy) <module.mod> [project-dir]\n";
}

std::filesystem::path default_import_project_dir(const std::filesystem::path& module_path)
{
    std::error_code ec;
    const auto cwd = std::filesystem::current_path(ec);
    const auto base = cwd.empty() ? std::filesystem::path{"."} : cwd;
    return base / module_path.stem();
}

int run_project_init_cli(int argc, char* argv[])
{
    if (argc != 3) {
        print_cli_usage(std::cerr);
        return 2;
    }

    const auto result = nw::toolset::initialize_project(std::filesystem::path{argv[2]});
    (result.ok ? std::cout : std::cerr) << result.message << '\n';
    return result.ok ? 0 : 1;
}

bool ensure_project_import_kernel(nw::toolset::ProjectImportFormat format, std::ostream& err)
{
    if (format != nw::toolset::ProjectImportFormat::json) {
        return true;
    }

    if (nw::kernel::services().get<nw::kernel::Rules>()) {
        return true;
    }

    const auto install = nw::probe_nwn_install(nw::GameVersion::vEE);
    if (install.install.empty()) {
        err << "rollnw-client: failed to find NWN install; set NWN_ROOT and NWN_HOME\n";
        return false;
    }

    try {
        nw::kernel::config().set_paths(install.install, install.user);
        nw::kernel::config().set_init_module("");
        nw::kernel::services().create();
        register_smalls_packages();
        nw::kernel::services().start();
    } catch (const std::exception& e) {
        err << "rollnw-client: failed to initialize import services: " << e.what() << '\n';
        return false;
    }

    return true;
}

int run_project_import_cli(int argc, char* argv[])
{
    nw::toolset::ProjectImportOptions options;
    bool json = false;
    bool legacy = false;
    std::vector<std::string_view> positional;
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--json") {
            json = true;
            options.format = nw::toolset::ProjectImportFormat::json;
        } else if (arg == "--legacy") {
            legacy = true;
            options.format = nw::toolset::ProjectImportFormat::legacy;
        } else if (!arg.empty() && arg.front() == '-') {
            print_cli_usage(std::cerr);
            return 2;
        } else {
            positional.push_back(arg);
        }
    }

    if (json == legacy || positional.empty() || positional.size() > 2) {
        print_cli_usage(std::cerr);
        return 2;
    }

    const std::filesystem::path module_path{positional[0]};
    const std::filesystem::path project_dir = positional.size() == 2
        ? std::filesystem::path{positional[1]}
        : default_import_project_dir(module_path);

    if (!ensure_project_import_kernel(options.format, std::cerr)) {
        return 1;
    }

    const auto result = nw::toolset::import_module_project(module_path, project_dir, options);
    (result.ok ? std::cout : std::cerr) << result.message << '\n';
    return result.ok ? 0 : 1;
}

int run_project_cli_if_requested(int argc, char* argv[])
{
    if (argc <= 1) {
        return -1;
    }

    const std::string_view command{argv[1]};
    if (command == "init") {
        return run_project_init_cli(argc, argv);
    }
    if (command == "import") {
        return run_project_import_cli(argc, argv);
    }
    if (command == "--help" || command == "-h" || command == "help") {
        print_cli_usage(std::cout);
        return 0;
    }
    return -1;
}

} // namespace

// ---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    loguru::g_stderr_verbosity = loguru::Verbosity_WARNING;
    nw::init_logger(argc, argv);
    if (const int cli_result = run_project_cli_if_requested(argc, argv); cli_result >= 0) {
        return cli_result;
    }
    LoguruOutputCapture log_capture;

    const auto install = nw::probe_nwn_install(nw::GameVersion::vEE);
    if (install.install.empty()) {
        LOG_F(ERROR, "rollnw-client: failed to find NWN install; set NWN_ROOT and NWN_HOME");
        return 1;
    }
    nw::kernel::config().set_paths(install.install, install.user);
    nw::kernel::config().set_init_module("");
    nw::kernel::services().create();
    register_smalls_packages();
    nw::kernel::services().start();

    SDL_SetLogPriorities(SDL_LOG_PRIORITY_INFO);
    if (!SDL_SetAppMetadata("rollnw | client", ROLLNW_CLIENT_APP_VERSION, ROLLNW_CLIENT_APP_ID)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL_SetAppMetadata failed: %s", SDL_GetError());
    }
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_TYPE_STRING, "application");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    int width = 1280;
    int height = 720;
    int frame_width = 1280;
    int frame_height = 720;

    SDL_Window* window = SDL_CreateWindow(
        "rollnw | client",
        width, height,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateWindow failed: %s", SDL_GetError());
        return 1;
    }
    SDL_ShowWindow(window);

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

    nw::StaticDirectory ui_assets{ui_dir};
    if (!ui_assets.valid()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to index rollnw client UI assets: %s", ui_dir.string().c_str());
        return 1;
    }

    nw::ResourceManager ui_resources{nw::kernel::global_allocator()};
    if (!ui_resources.add_custom_container(&ui_assets, false)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to add rollnw client UI assets to resource manager: %s", ui_dir.string().c_str());
        return 1;
    }
    ui_resources.build_registry();
    const nw::Resource panel_rml{nw::Resref{"ui/panel"}, nw::ResourceType::rml};
    const nw::Resource panel_rcss{nw::Resref{"ui/panel"}, nw::ResourceType::rcss};
    if (!ui_resources.contains(panel_rml) || !ui_resources.contains(panel_rcss)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "rollnw client UI resource package is incomplete: %s", ui_dir.string().c_str());
        return 1;
    }

    ClientRmlFileInterface rml_file_interface{
        ui_resources, nw::kernel::resman()};
    Rml::SetRenderInterface(rml_renderer);
    Rml::SetSystemInterface(&system_interface);
    Rml::SetFileInterface(&rml_file_interface);
    if (!Rml::Initialise()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Rml::Initialise failed");
        return 1;
    }

    // Fonts: load into memory so the buffers stay alive for RmlUi's font engine.
    // Keep backing buffers alive for the lifetime of the app.
    std::vector<Rml::byte> font_regular_data;
    std::vector<Rml::byte> font_medium_data;
    std::vector<Rml::byte> font_semibold_data;
    std::vector<Rml::byte> font_bold_data;
    std::vector<Rml::byte> font_mono_data;
    auto load_font = [](const char* path, const char* family, Rml::Style::FontWeight weight, std::vector<Rml::byte>& storage) {
        FILE* f = std::fopen(path, "rb");
        if (!f) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Font not found: %s", path);
            return false;
        }
        std::fseek(f, 0, SEEK_END);
        const auto size = std::ftell(f);
        if (size <= 0) {
            std::fclose(f);
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Font file has invalid size: %s", path);
            return false;
        }
        std::rewind(f);
        storage.resize(static_cast<size_t>(size));
        const size_t read = std::fread(storage.data(), 1, static_cast<size_t>(size), f);
        std::fclose(f);
        if (read != static_cast<size_t>(size)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to read full font file: %s", path);
            storage.clear();
            return false;
        }

        return Rml::LoadFontFace(Rml::Span<const Rml::byte>(storage.data(), storage.size()),
            family, Rml::Style::FontStyle::Normal, weight);
    };
    const char* base_path = SDL_GetBasePath();
    const char* font_dir = (base_path && base_path[0] != '\0') ? base_path : "./";
    Rml::String font_regular = Rml::String(font_dir) + "Inter-Regular.ttf";
    Rml::String font_medium = Rml::String(font_dir) + "Inter-Medium.ttf";
    Rml::String font_semibold = Rml::String(font_dir) + "Inter-SemiBold.ttf";
    Rml::String font_bold = Rml::String(font_dir) + "Inter-Bold.ttf";
    Rml::String font_mono = Rml::String(font_dir) + "Cousine-Regular.ttf";
    constexpr auto ui_font_weight_medium = static_cast<Rml::Style::FontWeight>(500);
    constexpr auto ui_font_weight_semibold = static_cast<Rml::Style::FontWeight>(600);
    const bool regular_ok = load_font(font_regular.c_str(), "RollnwSans", Rml::Style::FontWeight::Normal, font_regular_data);
    const bool medium_ok = load_font(font_medium.c_str(), "RollnwSans", ui_font_weight_medium, font_medium_data);
    const bool semibold_ok = load_font(font_semibold.c_str(), "RollnwSans", ui_font_weight_semibold, font_semibold_data);
    const bool bold_ok = load_font(font_bold.c_str(), "RollnwSans", Rml::Style::FontWeight::Bold, font_bold_data);
    const bool mono_ok = load_font(font_mono.c_str(), "RollnwMono", Rml::Style::FontWeight::Normal, font_mono_data);
    if (!regular_ok || !medium_ok || !semibold_ok || !bold_ok || !mono_ok) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load required fonts for RmlUI");
        return 1;
    }
    Rml::Context* context = Rml::CreateContext("toolset", Rml::Vector2i(width, height));
    if (!context) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Rml::CreateContext failed");
        return 1;
    }
    renderer.on_resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height), context);
    Rml::Context* fps_context = Rml::CreateContext("viewer_fps", Rml::Vector2i(width, height));
    if (!fps_context) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Rml::CreateContext failed: viewer_fps");
        return 1;
    }
    fps_context->SetDensityIndependentPixelRatio(context->GetDensityIndependentPixelRatio());
    Rml::Context* palette_context = Rml::CreateContext("command_palette", Rml::Vector2i(width, height));
    if (!palette_context) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Rml::CreateContext failed: command_palette");
        return 1;
    }
    palette_context->SetDensityIndependentPixelRatio(context->GetDensityIndependentPixelRatio());

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
    ObjectVariableChangeListener object_variable_change_listener{state};
    context->AddEventListener(
        "change", &object_variable_change_listener, false);
    context->AddEventListener(
        "blur", &object_variable_change_listener, true);
    renderer.set_rml_generated_textures(&state.item_icon_cache.textures);
    state.backend.bind(&state.smalls, &state.shell, &state.workspace);
    state.backend.set_document_save_handler([&state, &renderer](std::string_view tab_id) {
        const auto& tabs = state.workspace.tabs();
        const auto tab = std::find_if(tabs.begin(), tabs.end(), [tab_id](const auto& candidate) {
            return candidate.id == tab_id;
        });
        if (tab == tabs.end()
            || (tab->kind != nw::toolset::WorkspaceTabKind::preview
                && tab->kind != nw::toolset::WorkspaceTabKind::area)) {
            return document_save_result(nw::toolset::CommandStatus::rejected,
                "Only blueprint preview and area tabs can save live objects",
                nw::toolset::CommandOutputChannel::warn);
        }
        const auto* active_tab = state.workspace.active_tab();
        if (!active_tab || active_tab->id != tab_id) {
            return document_save_result(nw::toolset::CommandStatus::rejected,
                "Document tab must be active before it can be saved",
                nw::toolset::CommandOutputChannel::warn);
        }

        const auto object = tab->kind == nw::toolset::WorkspaceTabKind::area
            ? renderer.area_viewer_object()
            : state.smalls.active_object();
        if (object.type == nw::ObjectType::invalid) {
            return document_save_result(nw::toolset::CommandStatus::failed,
                "Live document object is unavailable",
                nw::toolset::CommandOutputChannel::error);
        }

        std::string error;
        const auto target = validated_project_file(state.backend.current_project_dir(), tab->detail, error);
        if (!target) {
            return document_save_result(nw::toolset::CommandStatus::rejected,
                std::move(error),
                nw::toolset::CommandOutputChannel::warn);
        }
        const bool saved = tab->kind == nw::toolset::WorkspaceTabKind::area
            ? nw::toolset::save_live_area_json_atomic(object, *target, error)
            : nw::toolset::save_live_blueprint_json_atomic(object, *target, error);
        if (!saved) {
            return document_save_result(nw::toolset::CommandStatus::failed,
                std::move(error),
                nw::toolset::CommandOutputChannel::error);
        }
        if (tab->kind == nw::toolset::WorkspaceTabKind::area) {
            const auto* area = nw::kernel::objects().get<nw::Area>(object);
            if (!area) {
                return document_save_result(nw::toolset::CommandStatus::success,
                    "Saved " + tab->detail + "; live area map input is unavailable",
                    nw::toolset::CommandOutputChannel::warn);
            }
            const std::array<const nw::Area*, 1> area_batch{area};
            const auto map_sources = nw::toolset::collect_area_map_sources(area_batch);
            const auto map_result = nw::toolset::write_project_area_maps(
                state.backend.current_project_dir(), map_sources);
            if (map_result.failed > 0) {
                return document_save_result(nw::toolset::CommandStatus::success,
                    "Saved " + tab->detail + "; area map unavailable: "
                        + map_result.first_error,
                    nw::toolset::CommandOutputChannel::warn);
            }
            if (map_result.degraded > 0) {
                return document_save_result(nw::toolset::CommandStatus::success,
                    "Saved " + tab->detail + "; area map contains missing-tile markers: "
                        + map_result.first_warning,
                    nw::toolset::CommandOutputChannel::warn);
            }
        }
        return document_save_result(nw::toolset::CommandStatus::success,
            "Saved " + tab->detail,
            nw::toolset::CommandOutputChannel::info);
    });
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

    auto* doc = load_rml_document_from_resource(*context, ui_resources, panel_rml);
    if (!doc) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadDocument failed: ui/panel.rml");
        return 1;
    }
    doc->Show();
    auto* palette_doc = load_command_palette_document(*palette_context);
    if (!palette_doc) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadDocument failed: command_palette.rml");
        return 1;
    }
    palette_doc->Show();
    auto* fps_doc = load_viewer_fps_document(*fps_context);
    if (!fps_doc) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadDocument failed: viewer_fps_overlay.rml");
        return 1;
    }
    fps_doc->Show();

    load_ui_preferences(state);
    state.workspace.ensure_default_tabs("Home", true);
    apply_bottom_dock_height(doc, state, window, state.shell.docks.pane(nw::toolset::DockRegion::bottom).size_px);
    apply_left_dock_width(doc, state, window, state.shell.docks.pane(nw::toolset::DockRegion::left).size_px);
    state.open_module_dialog_event = SDL_RegisterEvents(1);
    flush_log_capture(log_capture, state);
    append_output(state, "info", "rollnw client shell started");
    if (state.open_module_dialog_event == 0) {
        append_output(state, "warn", "Native file dialog events unavailable");
    }
    append_output(state, "info",
        "Ctrl+Shift+P: command palette, Ctrl+S: save tab, Ctrl+W: close tab, Ctrl+Z/Y: undo/redo, `: terminal tab, Ctrl+J: output tab");

    refresh_recent_list(doc, state);
    refresh_workspace_view(doc, state);
    refresh_command_palette(palette_doc, state);
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
        update_viewer_frame_metrics(state, raw_frame_delta_seconds);

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            clear_inactive_object(state);
            if (state.project_item_drag.active()
                && (state.workspace.active_tab_id() != state.project_item_drag.tab_id
                    || state.creature_inventory.object != state.project_item_drag.owner
                    || state.object_workbench_surface != ObjectWorkbenchSurface::inventory)) {
                cancel_project_item_drag(doc, state);
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
            if (state.open_module_dialog_event != 0 && event.type == state.open_module_dialog_event) {
                handle_open_module_dialog_result(doc, state, event);
                continue;
            }
            if (consume_terminal_toggle_text_input(state, event)) {
                continue;
            }

            bool dispatched_to_rml = false;
            switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN: {
                if (!event.key.repeat && event.key.key == SDLK_ESCAPE
                    && state.project_item_drag.active()) {
                    cancel_project_item_drag(doc, state);
                    state.pressed_recent_index = -1;
                    system_interface.SetMouseCursor("arrow");
                    dispatched_to_rml = true;
                    break;
                }
                if (state.project_item_drag.active()
                    && state.project_item_drag.threshold_crossed) {
                    dispatched_to_rml = true;
                    break;
                }
                if (!event.key.repeat && event.key.key == SDLK_ESCAPE
                    && state.area_object_placement.active()) {
                    cancel_area_object_placement(renderer, state);
                    state.pressed_recent_index = -1;
                    system_interface.SetMouseCursor("arrow");
                    dispatched_to_rml = true;
                    break;
                }
                if (state.area_object_placement.active()
                    && state.area_object_placement.threshold_crossed) {
                    dispatched_to_rml = true;
                    break;
                }

                if ((event.key.mod & SDL_KMOD_CTRL) && (event.key.mod & SDL_KMOD_SHIFT) && event.key.key == SDLK_P) {
                    append_command_result(state, dispatch_command(state, "rollnw.client.palette.toggle", {}, nw::toolset::CommandSource::shortcut));
                    toggle_command_palette(context, palette_context, doc, palette_doc, state, state.shell.command_palette_visible);
                    dispatched_to_rml = true;
                    break;
                }

                if (state.shell.command_palette_visible && event.key.key == SDLK_ESCAPE) {
                    toggle_command_palette(context, palette_context, doc, palette_doc, state, false);
                    dispatched_to_rml = true;
                    break;
                }

                if (!event.key.repeat && event.key.key == SDLK_ESCAPE
                    && close_active_smalls_selector(doc)) {
                    dispatched_to_rml = true;
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
                    dispatched_to_rml = true;
                    break;
                }

                auto* focused_details_integer = find_ancestor_with_class(
                    context->GetFocusElement(), "object_details_integer");
                if (!event.key.repeat && event.key.key == SDLK_ESCAPE
                    && focused_details_integer) {
                    clear_rml_focus(context);
                    sync_object_details_window(doc, state, true);
                    dispatched_to_rml = true;
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
                    dispatched_to_rml = true;
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
                    dispatched_to_rml = true;
                    break;
                }

                if (!event.key.repeat && event.key.key == SDLK_ESCAPE
                    && state.color_editor_channel >= 0) {
                    clear_color_editor(state);
                    refresh_workspace_content(doc, state);
                    dispatched_to_rml = true;
                    break;
                }

                if (!event.key.repeat && event.key.key == SDLK_ESCAPE
                    && state.appearance_selector_open) {
                    close_appearance_selector(state);
                    rebuild_active_appearances(state, state.object_details.object);
                    refresh_workspace_content(doc, state);
                    dispatched_to_rml = true;
                    break;
                }

                if (!event.key.repeat && event.key.key == SDLK_ESCAPE
                    && state.body_part_option_part >= 0) {
                    clear_body_part_options(state);
                    refresh_workspace_content(doc, state);
                    dispatched_to_rml = true;
                    break;
                }

                if (!event.key.repeat && event.key.key == SDLK_ESCAPE
                    && state.creature_spell_filter_field != CreatureSpellFilterField::none) {
                    clear_creature_spell_filter(state);
                    refresh_workspace_content(doc, state);
                    dispatched_to_rml = true;
                    break;
                }

                if (!event.key.repeat && event.key.key == SDLK_ESCAPE
                    && state.area_object_drag.active) {
                    cancel_area_object_drag(renderer, state);
                    system_interface.SetMouseCursor("arrow");
                    dispatched_to_rml = true;
                    break;
                }

                if (!event.key.repeat && event.key.key == SDLK_ESCAPE
                    && state.object_workbench_surface == ObjectWorkbenchSurface::inventory
                    && state.creature_inventory_selection >= 0) {
                    state.creature_inventory_selection = -1;
                    sync_creature_inventory_window(doc, state, true);
                    dispatched_to_rml = true;
                    break;
                }

                if (!event.key.repeat && event.key.key == SDLK_ESCAPE
                    && renderer.clear_viewer_area_object_selection()) {
                    state.smalls.clear_active_object();
                    state.active_object_tab_id.clear();
                    clear_active_object_details(state);
                    clear_active_creature_feats(state);
                    clear_active_creature_spells(state);
                    clear_active_creature_inventory(state);
                    state.smalls.refresh_ui_lists();
                    refresh_workspace_content(doc, state);
                    sync_object_details_window(doc, state, true);
                    sync_creature_feat_window(doc, state, true);
                    sync_creature_spell_window(doc, state, true);
                    sync_creature_inventory_window(doc, state, true);
                    dispatched_to_rml = true;
                    break;
                }

                if (state.shell.command_palette_visible
                    && (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER)
                    && focused_element_has_id(palette_context, "command_input")) {
                    refresh_command_palette(palette_doc, state);
                    if (!state.commands.empty()) {
                        execute_palette_command(window, context, palette_context, doc, palette_doc, state, state.commands.front().id);
                    }
                    dispatched_to_rml = true;
                    break;
                }

                const bool appearance_search_focused = !state.shell.command_palette_visible
                    && state.object_workbench_surface == ObjectWorkbenchSurface::appearance
                    && state.appearance_selector_open
                    && focused_element_has_id(context, "appearance_search")
                    && !(event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI));
                if (appearance_search_focused
                    && (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN)) {
                    const int selected = state.appearance_list.move_selection(
                        event.key.key == SDLK_UP ? -1 : 1);
                    const int scroll_top = state.appearance_list.scroll_top_for_index(selected);
                    state.appearance_list.set_scroll_top(scroll_top);
                    if (auto* list = find_el(doc, "appearance_rows")) {
                        list->SetScrollTop(static_cast<float>(scroll_top));
                    }
                    state.appearance_rendered = false;
                    sync_appearance_window(doc, state, true);
                    dispatched_to_rml = true;
                    break;
                }
                if (!event.key.repeat && appearance_search_focused
                    && (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER)) {
                    const int selected = state.appearance_list.selected();
                    const auto& catalog = active_appearance_catalog(state);
                    if (selected >= 0
                        && static_cast<size_t>(selected) < state.appearance_matches.size()) {
                        const uint32_t row_index = state.appearance_matches[static_cast<size_t>(selected)];
                        if (row_index < catalog.rows.size()) {
                            if (commit_active_appearance_selection(
                                    state, catalog.rows[row_index].id)) {
                                close_appearance_selector(state);
                                rebuild_active_appearances(state, state.object_details.object);
                                refresh_workspace_content(doc, state);
                            }
                        }
                    }
                    dispatched_to_rml = true;
                    break;
                }

                const bool body_part_combobox_focused = !state.shell.command_palette_visible
                    && state.object_workbench_surface == ObjectWorkbenchSurface::appearance
                    && !state.appearance_selector_open
                    && active_body_part_options_match_tab(state)
                    && focused_element_has_id(context, "active_body_part_field")
                    && !(event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI));
                if (body_part_combobox_focused
                    && (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN)) {
                    if (cycle_active_body_part_selection(state,
                            event.key.key == SDLK_UP ? -1 : 1)) {
                        refresh_workspace_content(doc, state);
                    }
                    sync_body_part_option_window(doc, state, true);
                    if (auto* field = find_el(doc, "active_body_part_field")) {
                        field->Focus();
                    }
                    dispatched_to_rml = true;
                    break;
                }
                if (!event.key.repeat && body_part_combobox_focused
                    && (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER)) {
                    if (!state.body_part_combobox.popup_visible()) {
                        (void)state.body_part_combobox.show_popup();
                        refresh_workspace_content(doc, state);
                        sync_body_part_option_window(doc, state, true);
                    } else if (const auto selected = state.body_part_combobox.selected_key()) {
                        if (commit_active_body_part_selection(state, *selected)) {
                            state.body_part_combobox.hide_popup();
                            refresh_workspace_content(doc, state);
                        }
                    }
                    if (auto* field = find_el(doc, "active_body_part_field")) {
                        field->Focus();
                    }
                    dispatched_to_rml = true;
                    break;
                }

                auto* focused_managed_list = find_ancestor_with_class(
                    context->GetFocusElement(), "managed_list_cycle");
                const bool managed_list_cycle_focused = !state.shell.command_palette_visible
                    && focused_managed_list
                    && !(event.key.mod
                        & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI
                            | SDL_KMOD_SHIFT));
                if (managed_list_cycle_focused
                    && (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN)) {
                    (void)cycle_managed_list(doc, state, focused_managed_list,
                        event.key.key == SDLK_UP ? -1 : 1);
                    dispatched_to_rml = true;
                    break;
                }

                const bool creature_spell_filter_focused = !state.shell.command_palette_visible
                    && active_creature_spell_filter_matches_tab(state)
                    && focused_element_has_id(context, "active_creature_spell_filter_field")
                    && !(event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI));
                if (creature_spell_filter_focused
                    && (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN)) {
                    if (!state.creature_spell_combobox.popup_visible()) {
                        (void)state.creature_spell_combobox.show_popup();
                        refresh_workspace_content(doc, state);
                    }
                    (void)state.creature_spell_combobox.move_selection(
                        event.key.key == SDLK_UP ? -1 : 1);
                    sync_creature_spell_filter_window(doc, state, true);
                    dispatched_to_rml = true;
                    break;
                }
                if (!event.key.repeat && creature_spell_filter_focused
                    && (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER)) {
                    if (!state.creature_spell_combobox.popup_visible()) {
                        (void)state.creature_spell_combobox.show_popup();
                        refresh_workspace_content(doc, state);
                        sync_creature_spell_filter_window(doc, state, true);
                    } else if (const auto selected = state.creature_spell_combobox.selected_key()) {
                        if (commit_creature_spell_filter(state, *selected)) {
                            refresh_workspace_content(doc, state);
                            sync_creature_spell_window(doc, state, true);
                        }
                    }
                    dispatched_to_rml = true;
                    break;
                }

                const bool output_shortcut = !event.key.repeat
                    && focused_element_has_id(context, "output_list")
                    && (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI))
                    && !(event.key.mod & SDL_KMOD_ALT);
                if (output_shortcut && event.key.key == SDLK_A) {
                    state.output_selection.anchor = 0;
                    state.output_selection.focus = state.output_selection.text.size();
                    state.shell.output_dirty = true;
                    dispatched_to_rml = true;
                    break;
                }
                if (output_shortcut && event.key.key == SDLK_C) {
                    const auto [selection_start, selection_end]
                        = state.output_selection.range();
                    if (selection_start < selection_end) {
                        system_interface.SetClipboardText(state.output_selection.text.substr(
                            selection_start, selection_end - selection_start));
                    }
                    dispatched_to_rml = true;
                    break;
                }

                const bool command_ctrl = !event.key.repeat
                    && (event.key.mod & SDL_KMOD_CTRL)
                    && !(event.key.mod & (SDL_KMOD_SHIFT | SDL_KMOD_ALT | SDL_KMOD_GUI));
                if (command_ctrl && event.key.key == SDLK_W) {
                    if (ensure_backend_ready(state)) {
                        dispatch_command_flow(
                            window, state, "workspace.close_tab", {}, nw::toolset::CommandSource::shortcut);
                        refresh_workspace_view(doc, state);
                    }
                    dispatched_to_rml = true;
                    break;
                }
                if (command_ctrl && event.key.key == SDLK_S) {
                    if (ensure_backend_ready(state)) {
                        dispatch_command_flow(
                            window, state, "workspace.save_tab", {}, nw::toolset::CommandSource::shortcut);
                        refresh_workspace_view(doc, state);
                    }
                    dispatched_to_rml = true;
                    break;
                }
                if (command_ctrl && (event.key.key == SDLK_Z || event.key.key == SDLK_Y)) {
                    if (ensure_backend_ready(state)) {
                        dispatch_command_flow(window,
                            state,
                            event.key.key == SDLK_Z ? "command.undo" : "command.redo",
                            {},
                            nw::toolset::CommandSource::shortcut);
                    }
                    dispatched_to_rml = true;
                    break;
                }

                if ((event.key.mod & SDL_KMOD_CTRL) && event.key.key == SDLK_J) {
                    append_command_result(state, dispatch_command(state, "rollnw.client.output.toggle", {}, nw::toolset::CommandSource::shortcut));
                    toggle_output_panel(doc, state, state.shell.output_panel_visible());
                    dispatched_to_rml = true;
                    break;
                }

                if (!(event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) && event.key.key == SDLK_GRAVE) {
                    append_command_result(state, dispatch_command(state, "rollnw.client.terminal.toggle", {}, nw::toolset::CommandSource::shortcut));
                    toggle_terminal(doc, state, state.shell.terminal_visible());
                    state.suppress_terminal_toggle_text_input = true;
                    dispatched_to_rml = true;
                    break;
                }

                const bool terminal_input_focused = focused_element_has_id(context, "terminal_input");
                if (state.shell.terminal_visible() && terminal_input_focused && event.key.key == SDLK_TAB) {
                    complete_terminal_command(doc, state);
                    dispatched_to_rml = true;
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
                    dispatched_to_rml = true;
                    break;
                }

                if (handle_area_object_key(
                        renderer, context, doc, state, event.key, frame_width, frame_height)
                    || handle_viewer_viewport_key(
                        renderer, context, doc, state, event.key, frame_width, frame_height)) {
                    dispatched_to_rml = true;
                    break;
                }
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                blur_focused_object_variable_input(context,
                    to_context_point(window, event.button.x, event.button.y));
                if (event.button.button == SDL_BUTTON_RIGHT
                    && state.project_item_drag.active()) {
                    cancel_project_item_drag(doc, state);
                    state.pressed_recent_index = -1;
                    system_interface.SetMouseCursor("arrow");
                    dispatched_to_rml = true;
                    break;
                }
                if (state.project_item_drag.active()
                    && state.project_item_drag.threshold_crossed) {
                    dispatched_to_rml = true;
                    break;
                }
                if (event.button.button == SDL_BUTTON_RIGHT
                    && state.area_object_placement.active()) {
                    cancel_area_object_placement(renderer, state);
                    state.pressed_recent_index = -1;
                    system_interface.SetMouseCursor("arrow");
                    dispatched_to_rml = true;
                    break;
                }
                if (state.area_object_placement.active()
                    && state.area_object_placement.threshold_crossed) {
                    dispatched_to_rml = true;
                    break;
                }
                if (event.button.button == SDL_BUTTON_LEFT
                    || event.button.button == SDL_BUTTON_MIDDLE
                    || event.button.button == SDL_BUTTON_RIGHT) {
                    const auto point = to_context_point(window, event.button.x, event.button.y);
                    if (command_palette_contains_point(palette_doc, point)) {
                        RmlSDL::InputEventHandler(palette_context, window, event);
                        dispatched_to_rml = true;
                        break;
                    }
                    auto* top_hit = context ? context->GetElementAtPoint(point) : nullptr;
                    if (auto viewer_viewport = active_workspace_viewer_viewport_request(doc, state, frame_width, frame_height);
                        viewer_viewport
                        && point_within_viewport(viewer_viewport->rect, point)
                        && !viewport_mouse_hit_blocked(doc, top_hit, point, state)) {
                        state.viewer_viewport_focused = true;
                        state.viewer_viewport_last_point = point;
                        clear_rml_focus(context);
                        const bool preview_orbit_drag = viewer_viewport->kind == WorkspaceViewerViewportKind::preview
                            && event.button.button == SDL_BUTTON_LEFT;
                        if (viewer_viewport->kind == WorkspaceViewerViewportKind::area
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
                                system_interface.SetMouseCursor("grabbing");
                            }
                        }
                        if (preview_orbit_drag || event.button.button == SDL_BUTTON_RIGHT || event.button.button == SDL_BUTTON_MIDDLE) {
                            state.viewer_viewport_dragging = true;
                            state.viewer_viewport_drag_mode = event.button.button == SDL_BUTTON_MIDDLE
                                ? ClientViewportDragMode::pan
                                : ClientViewportDragMode::look;
                            system_interface.SetMouseCursor("grabbing");
                        }
                        dispatched_to_rml = true;
                        break;
                    }
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        state.viewer_viewport_focused = false;
                    }
                }
                if (event.button.button == SDL_BUTTON_LEFT) {
                    if (begin_bottom_dock_resize(context, window, doc, state, event.button)) {
                        dispatched_to_rml = true;
                        break;
                    }
                    if (begin_left_dock_resize(context, window, doc, state, event.button)) {
                        dispatched_to_rml = true;
                        break;
                    }

                    state.pressed_recent_index = -1;
                    const auto point = to_context_point(window, event.button.x, event.button.y);
                    if (const auto offset = output_text_offset_at_point(
                            context, doc, state, point)) {
                        if (auto* output = find_el(doc, "output_list")) {
                            output->Focus();
                        }
                        state.output_selection.anchor = *offset;
                        state.output_selection.focus = *offset;
                        state.output_selection.dragging = true;
                        state.shell.output_dirty = true;
                        state.viewer_viewport_focused = false;
                        dispatched_to_rml = true;
                        break;
                    }
                    if (state.output_selection.active()) {
                        state.output_selection.clear();
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
                            state.workspace_tab_drag_id = workspace_tab_hit->GetAttribute<Rml::String>("data-tab", "");
                            state.workspace_tab_drag_start_x = point.x;
                            state.workspace_tab_drag_start_y = point.y;
                            dispatched_to_rml = true;
                            break;
                        }
                    }
                    if (!recent_list_hit_blocked(doc, top_hit, point, state)) {
                        if (auto* recent_item = recent_item_at_point(doc, point)) {
                            const std::string key = recent_item->GetAttribute<Rml::String>("data-key", "");
                            if (!key.empty()) {
                                state.pressed_recent_index = static_cast<int>(std::strtol(key.c_str(), nullptr, 10));
                                const size_t index = static_cast<size_t>(
                                    std::max(state.pressed_recent_index, 0));
                                if (state.shell.showing_project_tree
                                    && state.pressed_recent_index >= 0
                                    && index < state.project_rows.size()) {
                                    const auto& row = state.project_rows[index].node;
                                    if (!row.is_container()) {
                                        const auto resource = nw::Resource::from_path(
                                            row.relative_path, false);
                                        if (resource.type == nw::ResourceType::uti) {
                                            arm_project_item_drag(
                                                state, resource, row.path, point);
                                        } else {
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
                    dispatched_to_rml = true;
                    break;
                }
                if (update_left_dock_resize(doc, state, window, event.motion)) {
                    dispatched_to_rml = true;
                    break;
                }

                const auto point = to_context_point(window, event.motion.x, event.motion.y);
                auto* top_hit = context ? context->GetElementAtPoint(point) : nullptr;
                sync_object_variable_warning_tooltip(doc, state, top_hit, point,
                    frame_width, frame_height);
                if (state.output_selection.dragging) {
                    if (const auto offset = output_text_offset_at_point(
                            context, doc, state, point);
                        offset && state.output_selection.focus != *offset) {
                        state.output_selection.focus = *offset;
                        state.shell.output_dirty = true;
                    }
                    dispatched_to_rml = true;
                    break;
                }
                if (command_palette_contains_point(palette_doc, point)) {
                    RmlSDL::InputEventHandler(palette_context, window, event);
                    dispatched_to_rml = true;
                    break;
                }
                if (state.project_item_drag.active()) {
                    if (update_project_item_drag(context, doc, state, point)) {
                        const char* cursor = state.project_item_drag.phase
                                == ProjectItemDragPhase::target_valid
                            ? "cross"
                            : "unavailable";
                        system_interface.SetMouseCursor(cursor);
                        dispatched_to_rml = true;
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
                        dispatched_to_rml = true;
                        break;
                    }
                }
                if (state.viewer_viewport_dragging) {
                    const float dx = point.x - state.viewer_viewport_last_point.x;
                    const float dy = point.y - state.viewer_viewport_last_point.y;
                    state.viewer_viewport_last_point = point;
                    if (auto viewer_viewport = active_workspace_viewer_viewport_request(doc, state, frame_width, frame_height)) {
                        renderer.drag_viewer_viewport(state.viewer_viewport_drag_mode, dx, dy, viewer_viewport->rect);
                        system_interface.SetMouseCursor("grabbing");
                    } else {
                        state.viewer_viewport_dragging = false;
                        system_interface.SetMouseCursor("arrow");
                    }
                    dispatched_to_rml = true;
                    break;
                }
                if (state.area_object_drag.active) {
                    if (auto viewer_viewport = active_workspace_viewer_viewport_request(
                            doc, state, frame_width, frame_height)) {
                        update_area_object_drag(renderer, state, point, *viewer_viewport);
                        system_interface.SetMouseCursor("grabbing");
                    } else {
                        cancel_area_object_drag(renderer, state);
                        system_interface.SetMouseCursor("arrow");
                    }
                    dispatched_to_rml = true;
                    break;
                }

                if (!state.workspace_tab_drag_id.empty()) {
                    const float dx = point.x - state.workspace_tab_drag_start_x;
                    const float dy = point.y - state.workspace_tab_drag_start_y;
                    if (!state.workspace_tab_dragging
                        && (std::abs(dx) >= kWorkspaceTabDragThresholdPx || std::abs(dy) >= kWorkspaceTabDragThresholdPx)) {
                        state.workspace_tab_dragging = true;
                        system_interface.SetMouseCursor("grabbing");
                    }

                    if (state.workspace_tab_dragging) {
                        system_interface.SetMouseCursor("grabbing");
                        if (auto* tabs = find_el(doc, "workspace_tabs")) {
                            state.workspace_tab_scroll_x = tabs->GetScrollLeft();
                            const float left = tabs->GetAbsoluteLeft();
                            const float right = left + tabs->GetClientWidth();
                            if (point.x < left + kWorkspaceTabAutoScrollEdgePx) {
                                state.workspace_tab_scroll_x -= kWorkspaceTabAutoScrollStepPx;
                                apply_workspace_tab_scroll(doc, state);
                            } else if (point.x > right - kWorkspaceTabAutoScrollEdgePx) {
                                state.workspace_tab_scroll_x += kWorkspaceTabAutoScrollStepPx;
                                apply_workspace_tab_scroll(doc, state);
                            }
                        }

                        const auto& tabs = state.workspace.tabs();
                        const size_t current_index = workspace_tab_current_index(tabs, state.workspace_tab_drag_id, kInvalidVirtualIndex);
                        const size_t fallback = current_index == kInvalidVirtualIndex
                            ? (tabs.empty() ? 0 : tabs.size() - 1)
                            : current_index;
                        const size_t target_index = workspace_tab_target_index_at_point(doc, point, tabs, state.workspace_tab_drag_id, fallback);
                        if (current_index != kInvalidVirtualIndex && target_index != current_index) {
                            const std::string target_index_text = std::to_string(target_index);
                            const auto result = dispatch_command(state,
                                "workspace.move_tab",
                                {std::string_view{state.workspace_tab_drag_id}, std::string_view{target_index_text}},
                                nw::toolset::CommandSource::widget);
                            if (result.ok()) {
                                refresh_workspace_view(doc, state);
                            }
                        }
                        dispatched_to_rml = true;
                        break;
                    }
                }

                auto* list = find_el(doc, "recent_list");
                auto* search = find_el(doc, "recent_search");
                int hovered = -1;
                if (!recent_list_hit_blocked(doc, top_hit, point, state)
                    && list && (!search || !search->IsPointWithinElement(point)) && list->IsPointWithinElement(point)) {
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
                if (state.project_item_drag.active()
                    && state.project_item_drag.threshold_crossed) {
                    dispatched_to_rml = true;
                    break;
                }
                if (state.area_object_placement.active()
                    && state.area_object_placement.threshold_crossed) {
                    dispatched_to_rml = true;
                    break;
                }
                const auto point = to_context_point(window, event.wheel.mouse_x, event.wheel.mouse_y);
                if (command_palette_contains_point(palette_doc, point)) {
                    RmlSDL::InputEventHandler(palette_context, window, event);
                    dispatched_to_rml = true;
                    break;
                }
                if (event.wheel.y != 0.0f) {
                    const SDL_Keymod modifiers = SDL_GetModState();
                    auto* managed_list_at_point = find_ancestor_with_class(
                        context ? context->GetElementAtPoint(point) : nullptr,
                        "managed_list_cycle");
                    const bool managed_list_wheel = !state.shell.command_palette_visible
                        && !state.module_dialog_open
                        && managed_list_at_point
                        && !(modifiers
                            & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI
                                | SDL_KMOD_SHIFT));
                    if (managed_list_wheel) {
                        (void)cycle_managed_list(doc, state,
                            managed_list_at_point,
                            event.wheel.y > 0.0f ? -1 : 1);
                        dispatched_to_rml = true;
                        break;
                    }
                    const bool body_part_combobox_focused = !state.shell.command_palette_visible
                        && !state.module_dialog_open
                        && state.object_workbench_surface == ObjectWorkbenchSurface::appearance
                        && !state.appearance_selector_open
                        && active_body_part_options_match_tab(state)
                        && focused_element_has_id(context, "active_body_part_field")
                        && !(modifiers & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI | SDL_KMOD_SHIFT));
                    if (body_part_combobox_focused) {
                        if (cycle_active_body_part_selection(state,
                                event.wheel.y > 0.0f ? -1 : 1)) {
                            refresh_workspace_content(doc, state);
                        }
                        sync_body_part_option_window(doc, state, true);
                        if (auto* field = find_el(doc, "active_body_part_field")) {
                            field->Focus();
                        }
                        dispatched_to_rml = true;
                        break;
                    }
                    if (auto viewer_viewport = active_workspace_viewer_viewport_request(doc, state, frame_width, frame_height);
                        viewer_viewport && point_within_viewport(viewer_viewport->rect, point)) {
                        const auto object = renderer.active_viewer_object();
                        const bool object_wheel = viewer_viewport->kind == WorkspaceViewerViewportKind::area
                            && editable_area_object(object)
                            && !focused_text_input(context)
                            && !state.module_dialog_open;
                        if (object_wheel
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
                            dispatched_to_rml = true;
                            break;
                        }
                        renderer.zoom_viewer_viewport(event.wheel.y, viewer_viewport->rect);
                        dispatched_to_rml = true;
                        break;
                    }
                }
                if (point_within_element(doc, "workspace_tabs", point)) {
                    const float delta = event.wheel.x != 0.0f ? event.wheel.x : -event.wheel.y;
                    if (auto* tabs = find_el(doc, "workspace_tabs")) {
                        state.workspace_tab_scroll_x = tabs->GetScrollLeft();
                    }
                    state.workspace_tab_scroll_x += delta * kTabScrollStepPx;
                    apply_workspace_tab_scroll(doc, state);
                    dispatched_to_rml = true;
                    break;
                }
                if (point_within_element(doc, "object_workbench_tabs", point)) {
                    const float delta = event.wheel.x != 0.0f ? event.wheel.x : -event.wheel.y;
                    if (auto* tabs = find_el(doc, "object_workbench_tabs")) {
                        state.object_workbench_tab_scroll_x = tabs->GetScrollLeft();
                    }
                    state.object_workbench_tab_scroll_x += delta * kTabScrollStepPx;
                    apply_object_workbench_tab_scroll(doc, state);
                    dispatched_to_rml = true;
                    break;
                }
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (state.output_selection.dragging
                    && event.button.button == SDL_BUTTON_LEFT) {
                    const auto point = to_context_point(
                        window, event.button.x, event.button.y);
                    if (const auto offset = output_text_offset_at_point(
                            context, doc, state, point);
                        offset && state.output_selection.focus != *offset) {
                        state.output_selection.focus = *offset;
                        state.shell.output_dirty = true;
                    }
                    state.output_selection.dragging = false;
                    dispatched_to_rml = true;
                    break;
                }
                if (state.project_item_drag.active()
                    && event.button.button == SDL_BUTTON_LEFT) {
                    const bool item_dragged = state.project_item_drag.threshold_crossed;
                    if (item_dragged) {
                        const auto point = to_context_point(
                            window, event.button.x, event.button.y);
                        (void)update_project_item_drag(context, doc, state, point);
                        commit_project_item_drag(doc, state);
                        state.pressed_recent_index = -1;
                        system_interface.SetMouseCursor("arrow");
                        dispatched_to_rml = true;
                        break;
                    }
                    cancel_project_item_drag(doc, state);
                }
                if (state.project_item_drag.active()
                    && state.project_item_drag.threshold_crossed) {
                    dispatched_to_rml = true;
                    break;
                }
                if (state.area_object_placement.active()
                    && event.button.button == SDL_BUTTON_LEFT) {
                    const bool placement_dragged = state.area_object_placement.threshold_crossed;
                    if (placement_dragged) {
                        const auto point = to_context_point(window, event.button.x, event.button.y);
                        const auto viewport = active_workspace_viewer_viewport_request(
                            doc, state, frame_width, frame_height);
                        update_area_object_placement(renderer, state, point, viewport);
                        commit_area_object_placement(renderer, state);
                        state.pressed_recent_index = -1;
                        system_interface.SetMouseCursor("arrow");
                        dispatched_to_rml = true;
                        break;
                    }
                    state.area_object_placement = {};
                }
                if (state.area_object_placement.active()
                    && state.area_object_placement.threshold_crossed) {
                    dispatched_to_rml = true;
                    break;
                }
                if (state.area_object_drag.active && event.button.button == SDL_BUTTON_LEFT) {
                    commit_area_object_drag(renderer, state);
                    const auto point = to_context_point(window, event.button.x, event.button.y);
                    system_interface.SetMouseCursor(
                        point_within_element(doc, "workspace_tabs", point) ? "pointer" : "arrow");
                    dispatched_to_rml = true;
                    break;
                }
                if (state.viewer_viewport_dragging
                    && (event.button.button == SDL_BUTTON_LEFT
                        || event.button.button == SDL_BUTTON_MIDDLE
                        || event.button.button == SDL_BUTTON_RIGHT)) {
                    state.viewer_viewport_dragging = false;
                    const auto point = to_context_point(window, event.button.x, event.button.y);
                    system_interface.SetMouseCursor(point_within_element(doc, "workspace_tabs", point) ? "pointer" : "arrow");
                    dispatched_to_rml = true;
                    break;
                }
                if (event.button.button == SDL_BUTTON_LEFT) {
                    if (end_bottom_dock_resize(state)) {
                        dispatched_to_rml = true;
                        break;
                    }
                    if (end_left_dock_resize(state)) {
                        dispatched_to_rml = true;
                        break;
                    }

                    const auto point = to_context_point(window, event.button.x, event.button.y);
                    const bool workspace_tab_was_dragging = state.workspace_tab_dragging;
                    if (!state.workspace_tab_drag_id.empty()) {
                        clear_workspace_tab_drag(state);
                        if (workspace_tab_was_dragging) {
                            refresh_workspace_view(doc, state);
                            system_interface.SetMouseCursor(point_within_element(doc, "workspace_tabs", point) ? "pointer" : "arrow");
                            dispatched_to_rml = true;
                            break;
                        }
                    }

                    if (command_palette_contains_point(palette_doc, point)) {
                        if (auto* hit = element_at_mouse(palette_context, window, event.button)) {
                            if (auto* command_item = find_ancestor_with_class(hit, "command_item")) {
                                const std::string command_id = command_item->GetAttribute<Rml::String>("data-key", "");
                                execute_palette_command(window, context, palette_context, doc, palette_doc, state, command_id);
                            } else {
                                RmlSDL::InputEventHandler(palette_context, window, event);
                            }
                        }
                        dispatched_to_rml = true;
                        break;
                    }

                    Rml::Element* recent_hit = nullptr;
                    bool handled = false;
                    bool released_workspace_mouse_up = false;
                    const auto release_workspace_mouse_up = [&] {
                        if (!released_workspace_mouse_up && context) {
                            RmlSDL::InputEventHandler(context, window, event);
                            released_workspace_mouse_up = true;
                            dispatched_to_rml = true;
                        }
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

                        if (workspace_tab_scroll_button) {
                            release_workspace_mouse_up();
                            if (!workspace_tab_scroll_button->IsClassSet("disabled")) {
                                const bool forward = workspace_tab_scroll_button->GetId()
                                    == "workspace_tabs_next";
                                state.workspace_tab_scroll_x = tab_scroll_target(
                                    doc, kWorkspaceTabScrollStrip, forward);
                                apply_workspace_tab_scroll(doc, state);
                            }
                            handled = true;
                        } else if (object_workbench_tab_scroll_button) {
                            release_workspace_mouse_up();
                            if (!object_workbench_tab_scroll_button->IsClassSet("disabled")) {
                                const bool forward = object_workbench_tab_scroll_button->GetId()
                                    == "object_workbench_tabs_next";
                                state.object_workbench_tab_scroll_x = tab_scroll_target(
                                    doc, kObjectWorkbenchTabScrollStrip, forward);
                                apply_object_workbench_tab_scroll(doc, state);
                            }
                            handled = true;
                        } else if (workspace_tab_close_hit) {
                            const std::string tab_id = workspace_tab_close_hit->GetAttribute<Rml::String>("data-tab", "");
                            if (!tab_id.empty() && ensure_backend_ready(state)) {
                                release_workspace_mouse_up();
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
                                release_workspace_mouse_up();
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
                                release_workspace_mouse_up();
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
                                release_workspace_mouse_up();
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
                                release_workspace_mouse_up();
                                nw::toolset::sync_dialog_view(doc, state.dialog_view, true);
                            }
                            handled = true;
                        } else if (find_ancestor_with_id(hit, "creature_color_selector_close")) {
                            release_workspace_mouse_up();
                            clear_color_editor(state);
                            refresh_workspace_content(doc, state);
                            handled = true;
                        } else if (auto* color_channel = find_ancestor_with_class(hit, "creature_color_channel")) {
                            const auto color = parse_decimal_int32(
                                color_channel->GetAttribute<Rml::String>("data-color", ""));
                            if (color && *color >= 0
                                && open_color_editor(state, state.object_details.object,
                                    static_cast<uint32_t>(*color))) {
                                release_workspace_mouse_up();
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
                                release_workspace_mouse_up();
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
                                release_workspace_mouse_up();
                                clear_body_part_options(state);
                                close_appearance_selector(state);
                                (void)open_color_editor(state,
                                    state.object_details.object,
                                    static_cast<uint32_t>(*color));
                                refresh_workspace_content(doc, state);
                            }
                            handled = true;
                        } else if (find_ancestor_with_id(hit, "creature_color_selector")) {
                            release_workspace_mouse_up();
                            handled = true;
                        } else if (auto* option = find_ancestor_with_class(hit, "virtual_combobox_option")) {
                            const std::string key = option->GetAttribute<Rml::String>("data-key", "");
                            const auto value = parse_decimal_int32(key);
                            if (value && active_creature_spell_filter_matches_tab(state)) {
                                release_workspace_mouse_up();
                                if (commit_creature_spell_filter(state, *value)) {
                                    refresh_workspace_content(doc, state);
                                    sync_creature_spell_window(doc, state, true);
                                }
                            } else if (value && active_body_part_options_match_tab(state)) {
                                release_workspace_mouse_up();
                                if (commit_active_body_part_selection(state, *value)) {
                                    state.body_part_combobox.hide_popup();
                                    refresh_workspace_content(doc, state);
                                    if (auto* active_field = find_el(doc, "active_body_part_field")) {
                                        active_field->Focus();
                                    }
                                }
                            }
                            handled = true;
                        } else if (auto* spell_filter_field = find_ancestor_with_class(hit, "creature_spell_filter_field")) {
                            const auto filter = creature_spell_filter_field_from_name(
                                spell_filter_field->GetAttribute<Rml::String>("data-filter", ""));
                            if (filter && active_creature_spells_match_tab(state)) {
                                release_workspace_mouse_up();
                                if (state.creature_spell_filter_field == *filter
                                    && state.creature_spell_combobox.is_active()) {
                                    if (state.creature_spell_combobox.popup_visible()) {
                                        state.creature_spell_combobox.hide_popup();
                                    } else {
                                        (void)state.creature_spell_combobox.show_popup();
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
                        } else if (auto* field = find_ancestor_with_class(hit, "body_part_field")) {
                            const std::string part_text = field->GetAttribute<Rml::String>("data-part", "");
                            const std::string current_text = field->GetAttribute<Rml::String>("data-current", "");
                            const auto part = parse_decimal_int32(part_text);
                            const auto current = parse_decimal_int32(current_text);
                            if (part && current && *part >= 0
                                && active_appearances_match_tab(state)) {
                                release_workspace_mouse_up();
                                clear_color_editor(state);
                                if (state.body_part_combobox.is_active()
                                    && state.body_part_option_part == *part) {
                                    if (state.body_part_combobox.popup_visible()) {
                                        state.body_part_combobox.hide_popup();
                                    } else {
                                        (void)state.body_part_combobox.show_popup();
                                    }
                                } else {
                                    close_appearance_selector(state);
                                    (void)open_body_part_options(state,
                                        state.object_details.object,
                                        static_cast<uint32_t>(*part),
                                        *current);
                                }
                                refresh_workspace_content(doc, state);
                                sync_body_part_option_window(doc, state, true);
                                if (auto* active_field = find_el(doc, "active_body_part_field")) {
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
                                    release_workspace_mouse_up();
                                    if (renderer.set_viewer_area_object_selection(object)) {
                                        (void)renderer.focus_viewer_area_object_selection();
                                    }
                                }
                            }
                            handled = true;
                        } else if (find_ancestor_with_class(hit, "area_object_list_back")) {
                            release_workspace_mouse_up();
                            (void)renderer.clear_viewer_area_object_selection();
                            handled = true;
                        } else if (find_ancestor_with_id(hit, "object_variable_add")) {
                            release_workspace_mouse_up();
                            append_command_result(state,
                                dispatch_command(state,
                                    "object.variables.add",
                                    {},
                                    nw::toolset::CommandSource::widget));
                            handled = true;
                        } else if (auto* remove_variable = find_ancestor_with_class(
                                       hit, "object_variable_remove")) {
                            const std::string name = remove_variable->GetAttribute<Rml::String>(
                                "data-name", "");
                            const std::string type = remove_variable->GetAttribute<Rml::String>(
                                "data-type", "");
                            release_workspace_mouse_up();
                            append_command_result(state,
                                dispatch_command(state,
                                    "object.variables.remove",
                                    {name, type},
                                    nw::toolset::CommandSource::widget));
                            handled = true;
                        } else if (auto* variable_type = find_ancestor_with_class(
                                       hit, "object_variable_type")) {
                            const std::string name = variable_type->GetAttribute<Rml::String>(
                                "data-name", "");
                            const std::string type = variable_type->GetAttribute<Rml::String>(
                                "data-type", "");
                            const auto parsed_type = parse_decimal_int32(type);
                            release_workspace_mouse_up();
                            if (parsed_type && *parsed_type >= 1 && *parsed_type <= 3) {
                                const std::string desired_type = std::to_string(
                                    *parsed_type == 3 ? 1 : *parsed_type + 1);
                                append_command_result(state,
                                    dispatch_command(state,
                                        "object.variables.set_type",
                                        {name, type, desired_type},
                                        nw::toolset::CommandSource::widget));
                            }
                            handled = true;
                        } else if (auto* integer_step = find_ancestor_with_class(
                                       hit, "object_details_integer_step")) {
                            const auto row_index = parse_decimal_int32(
                                integer_step->GetAttribute<Rml::String>("data-row", ""));
                            const auto current = parse_decimal_int32(
                                integer_step->GetAttribute<Rml::String>("data-current", ""));
                            const auto delta = parse_decimal_int32(
                                integer_step->GetAttribute<Rml::String>("data-delta", ""));
                            if (row_index && current && delta
                                && *row_index >= 0
                                && (*delta == -1 || *delta == 1)
                                && active_object_details_matches_tab(state)
                                && static_cast<size_t>(*row_index) < state.object_details.rows.size()) {
                                const auto& row = state.object_details.rows[static_cast<size_t>(*row_index)];
                                const bool within_range = row.kind == nw::toolset::ObjectDetailsRowKind::value
                                    && row.editor == nw::toolset::ObjectDetailsEditorKind::integer
                                    && row.edit_value == *current
                                    && (*delta < 0 ? row.edit_value > row.edit_min
                                                   : row.edit_value < row.edit_max);
                                if (within_range) {
                                    release_workspace_mouse_up();
                                    const std::string row_text = std::to_string(*row_index);
                                    const std::string current_text = std::to_string(*current);
                                    const std::string desired_text = std::to_string(*current + *delta);
                                    append_command_result(state,
                                        dispatch_command(state,
                                            "object.details.set_integer",
                                            {row_text, current_text, desired_text},
                                            nw::toolset::CommandSource::widget));
                                }
                            }
                            handled = true;
                        } else if (auto* boolean = find_ancestor_with_class(
                                       hit, "object_details_boolean")) {
                            const auto row_index = parse_decimal_int32(
                                boolean->GetAttribute<Rml::String>("data-row", ""));
                            const auto current = parse_decimal_int32(
                                boolean->GetAttribute<Rml::String>("data-current", ""));
                            if (row_index && current
                                && *row_index >= 0
                                && (*current == 0 || *current == 1)
                                && active_object_details_matches_tab(state)
                                && static_cast<size_t>(*row_index) < state.object_details.rows.size()) {
                                const auto& row = state.object_details.rows[static_cast<size_t>(*row_index)];
                                if (row.kind == nw::toolset::ObjectDetailsRowKind::value
                                    && row.editor == nw::toolset::ObjectDetailsEditorKind::boolean
                                    && row.edit_value == *current) {
                                    const std::string row_text = std::to_string(*row_index);
                                    const std::string current_text = std::to_string(*current);
                                    const std::string desired_text = std::to_string(1 - *current);
                                    append_command_result(state,
                                        dispatch_command(state,
                                            "object.details.set_boolean",
                                            {row_text, current_text, desired_text},
                                            nw::toolset::CommandSource::widget));
                                }
                            }
                            release_workspace_mouse_up();
                            handled = true;
                        } else if (find_ancestor_with_id(hit, "appearance_selector_back")) {
                            release_workspace_mouse_up();
                            close_appearance_selector(state);
                            rebuild_active_appearances(state, state.object_details.object);
                            refresh_workspace_content(doc, state);
                            handled = true;
                        } else if (auto* surface_tab = find_ancestor_with_class(hit, "object_workbench_tab")) {
                            const std::string surface = surface_tab->GetAttribute<Rml::String>("data-surface", "");
                            release_workspace_mouse_up();
                            clear_body_part_options(state);
                            clear_creature_spell_filter(state);
                            clear_color_editor(state);
                            (void)close_active_smalls_selector(doc);
                            close_appearance_selector(state);
                            if (surface == "details") {
                                state.object_workbench_surface = ObjectWorkbenchSurface::details;
                            } else if (surface == "sheet"
                                && state.object_details.object.type == nw::ObjectType::creature) {
                                state.object_workbench_surface = ObjectWorkbenchSurface::sheet;
                            } else if (surface == "variables"
                                && state.object_details.object.type
                                    != nw::ObjectType::invalid) {
                                state.object_workbench_surface = ObjectWorkbenchSurface::variables;
                            } else if (surface == "haks"
                                && state.object_details.object.type == nw::ObjectType::module
                                && !state.backend.current_project_dir().empty()) {
                                state.object_workbench_surface = ObjectWorkbenchSurface::haks;
                            } else if (surface == "classes"
                                && state.object_details.object.type == nw::ObjectType::creature) {
                                state.object_workbench_surface = ObjectWorkbenchSurface::classes;
                            } else if (surface == "appearance"
                                && (appearance_catalog_kind(state.object_details.object.type)
                                    || state.object_details.object.type == nw::ObjectType::item)) {
                                state.object_workbench_surface = ObjectWorkbenchSurface::appearance;
                                if (state.object_details.object.type != nw::ObjectType::item) {
                                    rebuild_active_appearances(state, state.object_details.object);
                                }
                            } else if (surface == "item-properties"
                                && state.object_details.object.type == nw::ObjectType::item) {
                                state.object_workbench_surface = ObjectWorkbenchSurface::item_properties;
                            } else if (surface == "feats"
                                && state.object_details.object.type == nw::ObjectType::creature) {
                                state.object_workbench_surface = ObjectWorkbenchSurface::feats;
                            } else if (surface == "spells"
                                && state.object_details.object.type == nw::ObjectType::creature) {
                                state.object_workbench_surface = ObjectWorkbenchSurface::spells;
                            } else if (surface == "inventory"
                                && (state.object_details.object.type == nw::ObjectType::creature
                                    || state.object_details.object.type == nw::ObjectType::item)) {
                                state.object_workbench_surface = ObjectWorkbenchSurface::inventory;
                            }
                            invalidate_details_render(state);
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
                                nw::toolset::ui_v1_host(), state.managed_lists, true);
                            handled = true;
                        } else if (auto* catalog_field = find_ancestor_with_class(hit, "appearance_catalog_field")) {
                            const auto selected_field = appearance_editor_field_from_name(
                                catalog_field->GetAttribute<Rml::String>("data-field", ""));
                            if (selected_field && active_appearances_match_tab(state)
                                && (*selected_field == AppearanceEditorField::appearance
                                    || state.object_details.object.type == nw::ObjectType::creature)) {
                                release_workspace_mouse_up();
                                clear_body_part_options(state);
                                clear_color_editor(state);
                                state.appearance_editor_field = *selected_field;
                                state.appearance_selector_open = true;
                                state.appearance_query.clear();
                                rebuild_active_appearances(state, state.object_details.object);
                                state.appearance_scroll_to_selection = true;
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
                                release_workspace_mouse_up();
                                if (commit_active_appearance_selection(state, *id)) {
                                    close_appearance_selector(state);
                                    rebuild_active_appearances(state, state.object_details.object);
                                    refresh_workspace_content(doc, state);
                                }
                            }
                            handled = true;
                        } else if (auto* equipment_slot = find_ancestor_with_class(hit, "creature_equipment_slot")) {
                            const auto slot = parse_decimal_int32(
                                equipment_slot->GetAttribute<Rml::String>("data-slot", ""));
                            if (slot && *slot >= 0 && *slot < 18
                                && active_creature_inventory_matches_tab(state)) {
                                release_workspace_mouse_up();
                                nw::toolset::CommandResult result;
                                const auto& equipment = state.creature_inventory.equipment[static_cast<size_t>(*slot)];
                                const std::string slot_text = std::to_string(*slot);
                                if (equipment.assigned()) {
                                    result = dispatch_command(state,
                                        "object.creature.unequip_slot",
                                        {std::string_view{slot_text}},
                                        nw::toolset::CommandSource::widget);
                                } else if (state.creature_inventory_selection >= 0) {
                                    const std::string inventory_text = std::to_string(
                                        state.creature_inventory_selection);
                                    result = dispatch_command(state,
                                        "object.creature.equip_inventory_item",
                                        {std::string_view{inventory_text}, std::string_view{slot_text}},
                                        nw::toolset::CommandSource::widget);
                                } else {
                                    result.status = nw::toolset::CommandStatus::noop;
                                    result.output_channel = nw::toolset::CommandOutputChannel::none;
                                }
                                if (result.ok()) {
                                    state.creature_inventory_selection = -1;
                                }
                                append_command_result(state, result);
                                sync_creature_inventory_window(doc, state, true);
                            }
                            handled = true;
                        } else if (auto* inventory_row = find_ancestor_with_class(hit, "creature_inventory_item")) {
                            const auto source_index = parse_decimal_int32(
                                inventory_row->GetAttribute<Rml::String>("data-key", ""));
                            if (source_index && *source_index >= 0
                                && static_cast<size_t>(*source_index)
                                    < state.creature_inventory.inventory.size()
                                && active_creature_inventory_matches_tab(state)) {
                                release_workspace_mouse_up();
                                state.creature_inventory_selection = *source_index;
                                sync_creature_inventory_window(doc, state, true);
                            }
                            handled = true;
                        } else if (auto* page_button = find_ancestor_with_class(hit, "creature_inventory_page")) {
                            const auto page = parse_decimal_int32(
                                page_button->GetAttribute<Rml::String>("data-page", ""));
                            if (page && *page >= 0
                                && *page < state.creature_inventory.page_count
                                && active_creature_inventory_matches_tab(state)) {
                                release_workspace_mouse_up();
                                state.creature_inventory_page = *page;
                                state.creature_inventory_selection = -1;
                                sync_creature_inventory_window(doc, state, true);
                            }
                            handled = true;
                        } else if (auto* adjustment = find_ancestor_with_class(hit, "creature_class_level_adjust")) {
                            const auto slot = parse_decimal_int32(
                                adjustment->GetAttribute<Rml::String>("data-slot", ""));
                            const auto delta = parse_decimal_int32(
                                adjustment->GetAttribute<Rml::String>("data-delta", ""));
                            if (slot && *slot >= 0 && delta && (*delta == -1 || *delta == 1)
                                && active_creature_class_presentation_matches_tab(state)) {
                                const auto row = std::ranges::find(
                                    state.creature_class_presentation.rows,
                                    *slot,
                                    &nw::toolset::CreatureClassPresentationRow::slot);
                                const bool within_range = row != state.creature_class_presentation.rows.end()
                                    && (*delta < 0 ? row->level > row->minimum_level
                                                   : row->level < row->maximum_level);
                                if (within_range) {
                                    release_workspace_mouse_up();
                                    const std::string slot_text = std::to_string(*slot);
                                    const std::string delta_text = std::to_string(*delta);
                                    append_command_result(state,
                                        dispatch_command(state,
                                            "object.creature.adjust_class_level",
                                            {std::string_view{slot_text}, std::string_view{delta_text}},
                                            nw::toolset::CommandSource::widget));
                                }
                            }
                            handled = true;
                        } else if (auto* decrement = find_ancestor_with_class(hit, "creature_spell_decrement")) {
                            const auto spell = parse_decimal_int32(
                                decrement->GetAttribute<Rml::String>("data-spell", ""));
                            if (spell && *spell >= 0 && active_creature_spells_match_tab(state)
                                && state.creature_spells.memorizes) {
                                const auto row = std::ranges::find(state.creature_spells.rows,
                                    *spell,
                                    &nw::toolset::CreatureSpellRow::spell_id);
                                if (row != state.creature_spells.rows.end() && row->uses > 0) {
                                    release_workspace_mouse_up();
                                    const std::string class_id = std::to_string(
                                        state.creature_spells.selected_class);
                                    const std::string spell_id = std::to_string(*spell);
                                    const std::string metamagic = std::to_string(
                                        state.creature_spells.selected_metamagic);
                                    append_command_result(state,
                                        dispatch_command(state,
                                            "object.creature.adjust_memorized_spell",
                                            {std::string_view{class_id}, std::string_view{spell_id},
                                                std::string_view{metamagic}, std::string_view{"-1"}},
                                            nw::toolset::CommandSource::widget));
                                }
                            }
                            handled = true;
                        } else if (auto* increment = find_ancestor_with_class(hit, "creature_spell_increment")) {
                            const auto spell = parse_decimal_int32(
                                increment->GetAttribute<Rml::String>("data-spell", ""));
                            if (spell && *spell >= 0 && active_creature_spells_match_tab(state)
                                && state.creature_spells.memorizes) {
                                release_workspace_mouse_up();
                                const std::string class_id = std::to_string(
                                    state.creature_spells.selected_class);
                                const std::string spell_id = std::to_string(*spell);
                                const std::string metamagic = std::to_string(
                                    state.creature_spells.selected_metamagic);
                                append_command_result(state,
                                    dispatch_command(state,
                                        "object.creature.adjust_memorized_spell",
                                        {std::string_view{class_id}, std::string_view{spell_id},
                                            std::string_view{metamagic}, std::string_view{"1"}},
                                        nw::toolset::CommandSource::widget));
                            }
                            handled = true;
                        } else if (auto* spell_row = find_ancestor_with_class(hit, "creature_spell_row")) {
                            const auto spell = parse_decimal_int32(
                                spell_row->GetAttribute<Rml::String>("data-key", ""));
                            if (spell && *spell >= 0 && active_creature_spells_match_tab(state)
                                && !state.creature_spells.memorizes) {
                                const auto row = std::ranges::find(state.creature_spells.rows,
                                    *spell,
                                    &nw::toolset::CreatureSpellRow::spell_id);
                                if (row != state.creature_spells.rows.end()) {
                                    release_workspace_mouse_up();
                                    const std::string class_id = std::to_string(
                                        state.creature_spells.selected_class);
                                    const std::string spell_id = std::to_string(*spell);
                                    const std::string known = row->known ? "0" : "1";
                                    append_command_result(state,
                                        dispatch_command(state,
                                            "object.creature.set_known_spell",
                                            {std::string_view{class_id}, std::string_view{spell_id},
                                                std::string_view{known}},
                                            nw::toolset::CommandSource::widget));
                                }
                            }
                            handled = true;
                        } else if (auto* feat_row = find_ancestor_with_class(hit, "creature_feat_row")) {
                            const auto feat_id = parse_decimal_int32(
                                feat_row->GetAttribute<Rml::String>("data-key", ""));
                            if (feat_id && *feat_id >= 0 && active_creature_feats_match_tab(state)) {
                                const auto row = std::ranges::find(state.creature_feats.rows,
                                    static_cast<uint32_t>(*feat_id),
                                    &nw::toolset::CreatureFeatRow::feat_id);
                                if (row != state.creature_feats.rows.end()) {
                                    release_workspace_mouse_up();
                                    const std::string id = std::to_string(*feat_id);
                                    const std::string assigned = row->assigned ? "0" : "1";
                                    append_command_result(state, dispatch_command(state, "object.creature.set_feat", {std::string_view{id}, std::string_view{assigned}}, nw::toolset::CommandSource::widget));
                                }
                            }
                            handled = true;
                        } else if (activate_managed_list(doc, state, hit)) {
                            release_workspace_mouse_up();
                            handled = true;
                        } else if (auto* area_card = find_ancestor_with_class(hit, "home_area_card")) {
                            const auto index = parse_decimal_int32(
                                area_card->GetAttribute<Rml::String>("data-key", ""));
                            if (index && *index >= 0
                                && static_cast<size_t>(*index) < state.areas.size()
                                && ensure_backend_ready(state)) {
                                const auto result = dispatch_command(state,
                                    "toolset.select_area",
                                    {std::string_view{state.areas[static_cast<size_t>(*index)].resref}},
                                    nw::toolset::CommandSource::widget);
                                append_command_result(state, result);
                                if (result.ok()) {
                                    refresh_workspace_view(doc, state);
                                }
                            }
                            handled = true;
                        } else if (auto* project_item = find_ancestor_with_class(hit, "home_project_item")) {
                            const std::string index_text = project_item->GetAttribute<Rml::String>("data-key", "");
                            const int clicked_index = index_text.empty() ? -1 : static_cast<int>(std::strtol(index_text.c_str(), nullptr, 10));
                            if (clicked_index >= 0 && static_cast<size_t>(clicked_index) < state.recent_projects.size()
                                && ensure_backend_ready(state)) {
                                const auto& project = state.recent_projects[static_cast<size_t>(clicked_index)];
                                const auto result = dispatch_command(state,
                                    "toolset.open_project",
                                    {std::string_view{project.path}},
                                    nw::toolset::CommandSource::widget);
                                append_command_result(state, result);
                                if (result.ok()) {
                                    remember_recent_project(state, state.backend.current_project_dir());
                                    sync_shell_visibility(context, palette_context, doc, palette_doc, state);
                                    state.selected_recent_index = -1;
                                    set_input_value(doc, "recent_search", "");
                                    refresh_recent_list(doc, state);
                                    refresh_workspace_view(doc, state);
                                }
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
                                if (clicked_index >= 0 && clicked_index == state.pressed_recent_index) {
                                    set_recent_selected(doc, state, clicked_index);

                                    const size_t idx = static_cast<size_t>(clicked_index);
                                    if (state.shell.showing_project_tree) {
                                        if (idx < state.project_rows.size()) {
                                            const auto& row = state.project_rows[idx].node;
                                            if (row.is_container()) {
                                                if (state.collapsed_project_nodes.erase(row.id) == 0) {
                                                    state.collapsed_project_nodes.insert(row.id);
                                                }
                                                state.selected_recent_index = -1;
                                                refresh_recent_list(doc, state);
                                            } else if (ensure_backend_ready(state)) {
                                                const std::string relative_path = row.relative_path.generic_string();
                                                const auto result = dispatch_command(state,
                                                    "toolset.open_resource",
                                                    {std::string_view{relative_path}},
                                                    nw::toolset::CommandSource::widget);
                                                append_command_result(state, result);
                                                if (result.ok()) {
                                                    refresh_workspace_view(doc, state);
                                                }
                                            }
                                        }
                                    } else if (state.shell.showing_areas) {
                                        if (idx < state.areas.size()) {
                                            append_command_result(state,
                                                dispatch_command(state,
                                                    "toolset.select_area",
                                                    {std::string_view{state.areas[idx].resref}},
                                                    nw::toolset::CommandSource::widget));
                                        }
                                    }
                                }
                            }
                        }
                    }
                    state.pressed_recent_index = -1;
                    if (handled) {
                        dispatched_to_rml = true;
                    }
                }
                break;
            case SDL_EVENT_WINDOW_MOUSE_LEAVE:
                if (state.project_item_drag.active()) {
                    cancel_project_item_drag(doc, state);
                    state.pressed_recent_index = -1;
                    system_interface.SetMouseCursor("arrow");
                }
                if (state.area_object_placement.active()) {
                    cancel_area_object_placement(renderer, state);
                    state.pressed_recent_index = -1;
                    system_interface.SetMouseCursor("arrow");
                }
                state.viewer_viewport_dragging = false;
                state.output_selection.dragging = false;
                hide_object_variable_warning_tooltip(doc, state);
                set_recent_hover(doc, state, -1);
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
                const auto pixels = query_window_pixels(window);
                frame_width = pixels.first;
                frame_height = pixels.second;
                state.workspace_tab_scroll_pending = true;
                state.object_workbench_tab_scroll_pending = true;
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
                state.workspace_tab_scroll_pending = true;
                state.object_workbench_tab_scroll_pending = true;
                break;
            }
            default:
                break;
            }
            if (!dispatched_to_rml) {
                const bool targets_palette = event_targets_command_palette(
                    palette_doc, state, window, event);
                RmlSDL::InputEventHandler(targets_palette ? palette_context : context,
                    window, event);
                if (!targets_palette && state.shell.output_panel_visible()
                    && output_scroll_input(doc, context, window, event)) {
                    observe_output_scroll(doc, state);
                }
            }
        }
        clear_inactive_object(state);
        if (state.appearance_body_preview_object.type != nw::ObjectType::invalid
            && state.active_object_tab_id.empty()
            && !sync_appearance_body_preview(renderer, state)) {
            append_output(state, "error", "Failed to restore the creature Appearance preview");
        }

        const auto mutation = nw::toolset::object_mutation_state();
        if (mutation.epoch != state.observed_object_mutation_epoch) {
            state.observed_object_mutation_epoch = mutation.epoch;
            refresh_workspace_tabs(doc, state);
            const bool area_structure_changed = mutation.area_structure_epoch != state.observed_area_structure_epoch;
            state.observed_area_structure_epoch = mutation.area_structure_epoch;
            if (area_structure_changed) {
                state.area_object_drag = {};
                if (!renderer.rebuild_live_viewer_area(mutation.area, mutation.object)) {
                    append_output(state, "error", "Failed to rebuild the live area viewport after structural edit");
                }
                state.smalls.publish_active_area(mutation.area);
                if (mutation.object.type != nw::ObjectType::invalid) {
                    state.smalls.publish_active_object(mutation.object);
                    state.active_object_tab_id = state.workspace.active_tab_id();
                    state.object_workbench_surface = default_object_workbench_surface();
                    clear_active_appearances(state);
                    configure_details_list(state);
                    state.details_list.set_scroll_top(0);
                    rebuild_active_object_details(state, mutation.object);
                    if (mutation.object.type == nw::ObjectType::creature) {
                        configure_creature_feat_list(state);
                        state.creature_feat_list.set_scroll_top(0);
                        rebuild_active_creature_feats(state, mutation.object);
                        configure_creature_spell_list(state);
                        state.creature_spell_list.set_scroll_top(0);
                        rebuild_active_creature_spells(state, mutation.object);
                        state.creature_inventory_page = 0;
                        state.creature_inventory_selection = -1;
                        rebuild_active_creature_inventory(state, mutation.object);
                    } else {
                        clear_active_creature_feats(state);
                        clear_active_creature_spells(state);
                        if (mutation.object.type == nw::ObjectType::item) {
                            state.creature_inventory_page = 0;
                            state.creature_inventory_selection = -1;
                            rebuild_active_creature_inventory(state, mutation.object);
                        } else {
                            clear_active_creature_inventory(state);
                        }
                    }
                } else {
                    state.smalls.clear_active_object();
                    state.active_object_tab_id.clear();
                    clear_active_object_details(state);
                    clear_active_creature_feats(state);
                    clear_active_creature_spells(state);
                    clear_active_creature_inventory(state);
                }
                refresh_workspace_content(doc, state);
                sync_object_details_window(doc, state, true);
                sync_creature_feat_window(doc, state, true);
                sync_creature_spell_window(doc, state, true);
                sync_creature_inventory_window(doc, state, true);
                sync_appearance_window(doc, state, true);
            } else {
                const auto* active_tab = state.workspace.active_tab();
                const bool area_tab = active_tab && active_tab->kind == nw::toolset::WorkspaceTabKind::area;
                if (mutation.kind == nw::toolset::ObjectMutationKind::spatial) {
                    renderer.sync_viewer_area_object_spatial(mutation.object);
                } else if (mutation.kind == nw::toolset::ObjectMutationKind::visual) {
                    if (state.appearance_body_preview_object == mutation.object
                        && !update_appearance_preview_rows(mutation.object, false)) {
                        append_output(state, "error", "Failed to refresh the creature Appearance preview");
                    }
                    bool refreshed = false;
                    if (mutation.visual_kind == nw::toolset::ObjectVisualMutationKind::detail) {
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
                if (area_tab) {
                    renderer.set_viewer_area_object_selection(mutation.object);
                }
                if (mutation.object == state.object_details.object && active_object_details_matches_tab(state)) {
                    bool workbench_rebuilt = false;
                    rebuild_active_object_details(state, mutation.object);
                    if (mutation.object.type == nw::ObjectType::creature) {
                        const int32_t selected_class = state.creature_spells.selected_class;
                        const int32_t selected_metamagic = state.creature_spells.selected_metamagic;
                        rebuild_active_creature_feats(state, mutation.object);
                        rebuild_active_creature_spells(
                            state, mutation.object, selected_class, selected_metamagic);
                        rebuild_active_creature_inventory(state, mutation.object);
                    } else if (mutation.object.type == nw::ObjectType::item) {
                        rebuild_active_creature_inventory(state, mutation.object);
                    }
                    const bool item_mutation = mutation.object.type == nw::ObjectType::item;
                    if (state.object_workbench_surface == ObjectWorkbenchSurface::appearance
                        && !item_mutation) {
                        if (mutation.kind == nw::toolset::ObjectMutationKind::visual) {
                            (void)sync_live_body_part_option(state);
                            state.body_part_combobox.hide_popup();
                        }
                        rebuild_active_appearances(state, mutation.object);
                        if (mutation.kind == nw::toolset::ObjectMutationKind::visual) {
                            refresh_workspace_content(doc, state);
                            workbench_rebuilt = true;
                            if (auto* field = find_el(doc, "active_body_part_field")) {
                                field->Focus();
                            }
                        }
                    }
                    if (item_mutation
                        || state.object_workbench_surface == ObjectWorkbenchSurface::inventory) {
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
                        nw::toolset::ui_v1_host(), state.managed_lists, true);
                }
            }
        }
        if (state.object_workbench_surface == ObjectWorkbenchSurface::feats) {
            const std::string feat_query = get_input_value(doc, "creature_feat_search");
            if (feat_query != state.creature_feat_query) {
                state.creature_feat_query = feat_query;
                if (state.object_details.object.type == nw::ObjectType::creature
                    && active_object_details_matches_tab(state)) {
                    rebuild_active_creature_feats(state, state.object_details.object);
                    state.creature_feat_list.set_scroll_top(0);
                    sync_creature_feat_window(doc, state, true);
                }
            }
        }
        if (state.object_workbench_surface == ObjectWorkbenchSurface::spells) {
            const std::string query = get_input_value(doc, "creature_spell_search");
            if (query != state.creature_spell_query) {
                state.creature_spell_query = query;
                if (state.object_details.object.type == nw::ObjectType::creature
                    && active_object_details_matches_tab(state)) {
                    filter_active_creature_spells(state);
                    state.creature_spell_list.set_scroll_top(0);
                    sync_creature_spell_window(doc, state, true);
                }
            }
        }
        if (state.object_workbench_surface == ObjectWorkbenchSurface::appearance
            && state.appearance_selector_open) {
            const std::string appearance_query = get_input_value(doc, "appearance_search");
            if (appearance_query != state.appearance_query) {
                state.appearance_query = appearance_query;
                if (appearance_catalog_kind(state.object_details.object.type)
                    && active_object_details_matches_tab(state)) {
                    rebuild_active_appearances(state, state.object_details.object);
                    state.appearance_list.set_scroll_top(0);
                    sync_appearance_window(doc, state, true);
                }
            }
        }

        if (workspace_home_active(state)
            && state.backend.module_object().type == nw::ObjectType::module) {
            const std::string area_query = get_input_value(doc, "home_area_search");
            if (area_query != state.home_area_query) {
                state.home_area_query = area_query;
                refresh_home_area_catalog(state, true);
                sync_home_area_window(doc, state, true);
            } else {
                sync_home_area_window(doc, state, false);
            }
        }

        const std::string recent_query = get_input_value(doc, "recent_search");
        if (recent_query != state.last_recent_query) {
            state.last_recent_query = recent_query;
            refresh_recent_list(doc, state);
        } else if (state.shell.showing_project_tree) {
            render_project_tree_window(doc, state, false);
        }

        const std::string command_query = get_input_value(palette_doc, "command_input");
        if (state.shell.command_palette_visible && command_query != state.last_command_query) {
            state.last_command_query = command_query;
            refresh_command_palette(palette_doc, state);
        }

        const std::string output_filter = get_input_value(doc, "output_filter");
        if (output_filter != state.last_output_filter) {
            state.last_output_filter = output_filter;
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

        const Uint64 begin_frame_start_counter = SDL_GetPerformanceCounter();
        renderer.begin_frame();
        const Uint64 draw_start_counter = SDL_GetPerformanceCounter();
        context->Update();
        bool tab_scroll_layout_changed = false;
        if (state.workspace_tab_scroll_pending) {
            state.workspace_tab_scroll_pending = false;
            apply_workspace_tab_scroll(doc, state);
            tab_scroll_layout_changed = true;
        }
        if (state.object_workbench_tab_scroll_pending) {
            state.object_workbench_tab_scroll_pending = false;
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
        if (sync_body_part_option_window(doc, state, false)) {
            context->Update();
        }
        if (nw::toolset::sync_managed_lists(doc,
                nw::toolset::ui_v1_host(), state.managed_lists, false)) {
            context->Update();
        }
        if (apply_output_scroll_after_layout(doc, state)) {
            context->Update();
        }
        {
            const ScopedClientGpuTimer gpu_timer{renderer, kClientGpuTimerUi};
            context->Render();
        }
        const Uint64 ui_end_counter = SDL_GetPerformanceCounter();
        const auto viewer_viewport = active_workspace_viewer_viewport_request(doc, state, frame_width, frame_height);
        if (viewer_viewport) {
            sync_viewer_render_options(renderer, state);
            bool rendered = false;
            {
                const ScopedClientGpuTimer gpu_timer{renderer, kClientGpuTimerViewport};
                rendered = viewer_viewport->kind == WorkspaceViewerViewportKind::area
                    ? renderer.render_area_viewport(
                          viewer_viewport->project_dir,
                          viewer_viewport->module_generation,
                          viewer_viewport->resource_path,
                          viewer_viewport->rect,
                          frame_delta_ms)
                    : renderer.render_preview_viewport(
                          viewer_viewport->project_dir,
                          viewer_viewport->module_generation,
                          viewer_viewport->resource_path,
                          viewer_viewport->rect,
                          frame_delta_ms);
            }
            if (!rendered) {
                // The viewport renderer logs specific load/render failures; keep the UI frame intact.
            }

            if (rendered
                && (viewer_viewport->kind == WorkspaceViewerViewportKind::preview
                    || viewer_viewport->kind == WorkspaceViewerViewportKind::area)) {
                if (viewer_viewport->kind == WorkspaceViewerViewportKind::area) {
                    const auto area = renderer.area_viewer_object();
                    const bool area_changed = state.smalls.active_area() != area;
                    state.smalls.publish_active_area(area);
                    if (area_changed) {
                        refresh_workspace_content(doc, state);
                    }
                } else {
                    state.smalls.clear_active_area();
                }
                const auto object = renderer.active_viewer_object();
                if (object.type != nw::ObjectType::invalid) {
                    state.smalls.publish_active_object(object);
                    const std::string active_tab_id = state.workspace.active_tab_id();
                    const bool object_changed = state.object_details.object != object
                        || state.active_object_tab_id != active_tab_id
                        || state.object_details.status != nw::toolset::ObjectDetailsStatus::ready;
                    state.active_object_tab_id = active_tab_id;
                    if (object_changed) {
                        state.object_workbench_surface = default_object_workbench_surface();
                        clear_active_appearances(state);
                        configure_details_list(state);
                        state.details_list.set_scroll_top(0);
                        rebuild_active_object_details(state, object);
                        if (object.type == nw::ObjectType::creature) {
                            configure_creature_feat_list(state);
                            state.creature_feat_list.set_scroll_top(0);
                            rebuild_active_creature_feats(state, object);
                            configure_creature_spell_list(state);
                            state.creature_spell_list.set_scroll_top(0);
                            rebuild_active_creature_spells(state, object);
                            state.creature_inventory_page = 0;
                            state.creature_inventory_selection = -1;
                            rebuild_active_creature_inventory(state, object);
                        } else {
                            clear_active_creature_feats(state);
                            clear_active_creature_spells(state);
                            if (object.type == nw::ObjectType::item) {
                                state.creature_inventory_page = 0;
                                state.creature_inventory_selection = -1;
                                rebuild_active_creature_inventory(state, object);
                            } else {
                                clear_active_creature_inventory(state);
                            }
                        }
                        state.observed_object_mutation_epoch = nw::toolset::object_mutation_state().epoch;
                        refresh_workspace_content(doc, state);
                        sync_object_details_window(doc, state, true);
                        sync_creature_feat_window(doc, state, true);
                        sync_creature_spell_window(doc, state, true);
                        sync_creature_inventory_window(doc, state, true);
                        sync_appearance_window(doc, state, true);
                    }
                } else {
                    const bool had_active_object = state.object_details.object.type != nw::ObjectType::invalid;
                    state.smalls.clear_active_object();
                    state.active_object_tab_id.clear();
                    if (state.object_details.status != nw::toolset::ObjectDetailsStatus::empty) {
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
                state.active_object_tab_id.clear();
                if (state.object_details.status != nw::toolset::ObjectDetailsStatus::empty) {
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
                state.active_object_tab_id.clear();
                if (state.object_details.status != nw::toolset::ObjectDetailsStatus::empty) {
                    clear_active_object_details(state);
                    sync_object_details_window(doc, state, true);
                }
            }
        }
        if (!sync_appearance_body_preview(renderer, state)) {
            append_output(state, "error", "Failed to synchronize the active creature Appearance preview");
        }
        const Uint64 view_end_counter = SDL_GetPerformanceCounter();
        update_viewer_internal_metrics(state, renderer.last_viewer_frame_stats());
        const Uint64 overlay_start_counter = view_end_counter;
        sync_viewer_fps_overlay(fps_doc, viewer_viewport, state);
        {
            const ScopedClientGpuTimer gpu_timer{renderer, kClientGpuTimerOverlay};
            fps_context->Update();
            fps_context->Render();
        }
        const Uint64 overlay_end_counter = SDL_GetPerformanceCounter();
        Uint64 palette_end_counter = overlay_end_counter;
        if (state.shell.command_palette_visible) {
            const ScopedClientGpuTimer gpu_timer{renderer, kClientGpuTimerPalette};
            palette_context->Update();
            palette_context->Render();
            palette_end_counter = SDL_GetPerformanceCounter();
        }
        const Uint64 present_start_counter = palette_end_counter;
        renderer.end_frame();
        update_client_gpu_metrics(state, renderer.last_gpu_frame_stats());
        const Uint64 present_end_counter = SDL_GetPerformanceCounter();
        update_viewer_render_metrics(state,
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

    cancel_project_item_drag(doc, state);
    cancel_area_object_placement(renderer, state);
    if (state.appearance_body_preview_object.type != nw::ObjectType::invalid
        && nw::kernel::objects().valid(state.appearance_body_preview_object)) {
        (void)update_appearance_preview_rows(state.appearance_body_preview_object, true);
    }
    state.appearance_body_preview_object = nw::ObjectHandle{};
    renderer.wait_idle();
    state.smalls.clear_active_object();
    state.active_object_tab_id.clear();
    Rml::ReleaseCompiledGeometry(rml_renderer);
    Rml::ReleaseTextures(rml_renderer);
    context->RemoveEventListener(
        "change", &object_variable_change_listener, false);
    context->RemoveEventListener(
        "blur", &object_variable_change_listener, true);
    state.rml_smalls_data_model->shutdown();
    Rml::RemoveContext("command_palette");
    Rml::RemoveContext("viewer_fps");
    Rml::RemoveContext("toolset");
    Rml::Shutdown();
    renderer.shutdown();
    nw::kernel::services().shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
