#include "appearance_catalog.hpp"
#include "area_door_hooks.hpp"
#include "area_map.hpp"
#include "area_navigation.hpp"
#include "area_regions.hpp"
#include "area_tile_edits.hpp"
#include "area_tile_interaction.hpp"
#include "area_tile_palette.hpp"
#include "dialog_view.hpp"
#include "forward_plus_debug.hpp"
#include "object_document.hpp"
#include "object_edits.hpp"
#include "preview_session.hpp"
#include "project.hpp"
#include "project_import.hpp"
#include "renderer.hpp"
#include "resource_document.hpp"
#include "rml_managed_list.hpp"
#include "rml_smalls_bridge.hpp"
#include "rml_smalls_data_model.hpp"
#include "rml_smalls_language_binding.hpp"
#include "rollnw_tool_version.hpp"
#include "shell_controller.hpp"
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
constexpr int kSoundCatalogRowHeightPx = 34;
constexpr int kSoundCatalogOverscanRows = 4;
constexpr int kAreaTilePaletteRowHeightPx = 58;
constexpr int kAreaTilePaletteOverscanRows = 3;
constexpr float kManagedListAutoScrollEdgePx = 28.0f;
constexpr float kManagedListAutoScrollStepPx = 14.0f;
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
    runtime.add_module_path(stdlib_path / *nw::kernel::config().profile());
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
    #play_preview_viewport_overlay {
      position: absolute;
      display: none;
      width: 320px;
      height: 50px;
      padding: 7px 12px 8px 12px;
      border: 1px #9b835d;
      border-radius: 3px;
      background: #0d1217dd;
      pointer-events: none;
    }
    .play_preview_viewport_title {
      display: block;
      width: 100%;
      color: #eadcc3;
      font-size: 17px;
      font-weight: bold;
      line-height: 21px;
    }
    .play_preview_viewport_help {
      display: block;
      width: 100%;
      color: #aeb8c3;
      font-size: 11px;
      font-weight: normal;
      line-height: 14px;
    }
    .play_preview_viewport_error {
      display: block;
      width: 100%;
      color: #ff9a8a;
      font-size: 11px;
      font-weight: normal;
      line-height: 14px;
    }
  </style>
</head>
<body>
  <div id="viewer_fps_overlay">-- FPS</div>
  <div id="play_preview_viewport_overlay"></div>
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
constexpr int kPltPaletteColumns = 16;
constexpr int kPltPaletteRows = 11;
constexpr int kPltPaletteCellPx = 24;

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
    spawns,
    sounds,
    store_inventory,
};

ObjectWorkbenchSurface default_object_workbench_surface()
{
    return ObjectWorkbenchSurface::details;
}

bool data_workbench_only(nw::ObjectType type,
    ObjectWorkbenchSurface surface) noexcept
{
    switch (type) {
    case nw::ObjectType::sound:
    case nw::ObjectType::store:
    case nw::ObjectType::trigger:
        return true;
    default:
        return type == nw::ObjectType::item
            && surface == ObjectWorkbenchSurface::item_properties;
    }
}

bool object_has_grid_inventory(nw::ObjectType type) noexcept
{
    return type == nw::ObjectType::creature
        || type == nw::ObjectType::item
        || type == nw::ObjectType::placeable;
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
    nw::ObjectHandle area{};
    nw::ObjectSpatialState before;
    nw::ObjectSpatialState preview;
    glm::vec3 grab_offset{0.0f};
    ClientViewportPointerDrag pointer;
    bool active = false;
    bool moved = false;
    bool valid = false;
    bool encounter_spawn = false;
    uint32_t encounter_spawn_index = UINT32_MAX;
    nw::ObjectSpawnPoint spawn_before;
    nw::ObjectSpawnPoint spawn_preview;
    std::unique_ptr<nw::toolset::AreaDoorHookSnapshot> door_hooks;
    int32_t door_hook_type = -2;
    std::unique_ptr<nw::toolset::AreaPlacementNavigation> navigation;
    std::string diagnostic;
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
    bool region_drawing = false;
    bool region_closing_valid = false;
    std::vector<glm::vec3> region_points;
    std::optional<glm::vec3> region_hover;
    std::unique_ptr<nw::toolset::AreaDoorHookSnapshot> door_hooks;
    int32_t door_hook_type = -2;
    std::unique_ptr<nw::toolset::AreaPlacementNavigation> navigation;
    std::string diagnostic;

    [[nodiscard]] bool active() const noexcept
    {
        return phase != AreaObjectPlacementPhase::idle;
    }
};

enum class ProjectBlueprintDragPhase : uint8_t {
    idle,
    armed,
    target_valid,
    target_invalid,
};

enum class ProjectBlueprintDragKind : uint8_t {
    none,
    item,
    encounter_spawn,
    sound_resource,
};

enum class ProjectBlueprintDropTargetKind : uint8_t {
    none,
    inventory,
    equipment,
    encounter_spawns,
    sound_resources,
    store_inventory,
};

struct ProjectBlueprintDropTarget {
    ProjectBlueprintDropTargetKind kind = ProjectBlueprintDropTargetKind::none;
    int32_t page = -1;
    int32_t row = -1;
    int32_t column = -1;
    int32_t category = -1;
    nw::EquipIndex slot = nw::EquipIndex::invalid;

    bool operator==(const ProjectBlueprintDropTarget&) const = default;
};

struct ProjectBlueprintDragState {
    std::filesystem::path source_path;
    nw::Resource resource;
    nw::ObjectHandle owner{};
    nw::ObjectHandle item{};
    std::optional<nw::toolset::EncounterSpawnEdit> encounter_spawn_edit;
    std::optional<nw::toolset::SoundResourceEdit> sound_resource_edit;
    Rml::Vector2f drag_start;
    std::string tab_id;
    ProjectBlueprintDropTarget target;
    ProjectBlueprintDragKind kind = ProjectBlueprintDragKind::none;
    ProjectBlueprintDragPhase phase = ProjectBlueprintDragPhase::idle;
    int32_t width = 0;
    int32_t height = 0;
    bool threshold_crossed = false;
    bool materialization_failed = false;

    [[nodiscard]] bool active() const noexcept
    {
        return phase != ProjectBlueprintDragPhase::idle;
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

enum class AreaWorkspaceSurface : uint8_t {
    properties,
    objects,
    tiles,
};

struct PlayPreviewState {
    nw::toolset::ToolsetPreviewSession session;
    nw::toolset::PreviewFixedStepState fixed_step;
    nw::toolset::PreviewInputSample pending_input;
    std::array<nw::toolset::PreviewInputSample, 6> tick_inputs{};
    std::array<nw::ObjectSpatialState, 1> spatial_rows{};
    std::array<nw::toolset::PreviewActorLocomotion, 1> locomotion_rows{};
    nw::Resource pending_actor{};
    nw::ObjectHandle area{};
    uint64_t module_generation = 0;
    std::string tab_id;
    std::string placement_diagnostic;
    std::string picker_previous_query;
    SDL_Gamepad* gamepad = nullptr;
    float mouse_look_x = 0.0f;
    float mouse_look_y = 0.0f;
    double mouse_sample_seconds = 0.0;
    float wheel_zoom = 0.0f;
    bool selecting_actor = false;
    bool picker_was_showing_project_tree = false;
    bool picker_was_showing_areas = false;
    bool visuals_attached = false;

    bool placement_pending() const noexcept
    {
        return pending_actor.valid();
    }
};

struct ProjectLoadRequest {
    std::string path;
    std::string stage = "Indexing project files...";
    nw::toolset::CommandSource source = nw::toolset::CommandSource::widget;
    bool presented = false;
    bool close_import_panel_on_success = false;

    [[nodiscard]] bool active() const noexcept { return !path.empty(); }
};

struct AreaTileStrokeState {
    nw::ObjectHandle area{};
    nw::Resref tileset;
    nw::toolset::AreaTileBrush brush;
    std::string label;
    uint64_t mutation_epoch = 0;
    uint64_t resource_generation = 0;
    int32_t width = 0;
    int32_t height = 0;
    nw::toolset::AreaTileCellCoord last_target{};
    std::vector<uint8_t> visited;
    std::vector<uint8_t> previewed_tiles;
    std::vector<uint32_t> tile_indices;
    std::vector<uint32_t> corner_indices;
    uint8_t pointer_button = 0;
    bool active = false;
    bool has_last_target = false;
};

struct AreaTileEditorState {
    nw::toolset::AreaTilePalette palette;
    nw::toolset::VirtualListController list;
    nw::toolset::VirtualListRange rendered_range{};
    AreaTileStrokeState stroke;
    nw::toolset::AreaTileSelection selection;
    std::string feedback;
    std::string query;
    std::vector<nw::render::viewer::AreaTilePreviewRow> preview_rows;
    Rml::Vector2f pending_cursor_point{};
    uint32_t cursor_target_index = UINT32_MAX;
    // Cached presentation mode, not a second source of keyboard state.
    nw::toolset::AreaTilePointerModifier cursor_modifier
        = nw::toolset::AreaTilePointerModifier::none;
    uint64_t next_random_seed = 1;
    int rendered_row_count = 0;
    int32_t selected_row = -1;
    int32_t group_orientation = 0;
    bool list_configured = false;
    bool rendered = false;
    bool cursor_update_pending = false;
};

struct AppState {
    nw::toolset::RmlSmallsBridge smalls;
    nw::toolset::ToolsetBackend backend;
    nw::toolset::ShellController shell;
    PlayPreviewState play_preview;
    nw::toolset::WorkspaceState workspace;
    std::optional<nw::toolset::CommandPrompt> command_form;
    Rml::ElementDocument* command_overlay_document = nullptr; // Borrowed from the command context.
    uint64_t command_form_generation = 0;
    uint64_t rendered_command_form_generation = 0;
    uint64_t command_form_browse_generation = 0;
    nw::toolset::VirtualComboBox command_form_combobox;
    std::optional<size_t> command_form_combobox_field;
    std::optional<nw::toolset::VirtualComboBoxPopupPlacement> command_form_combobox_placement;
    size_t blueprint_review_page = 0;
    std::string blueprint_operation_markup;
    nw::toolset::DialogViewState dialog_view;
    nw::toolset::ObjectDetailsSnapshot object_details;
    nw::toolset::VirtualListController details_list;
    nw::toolset::VirtualComboBox object_details_combobox{
        nw::toolset::VirtualComboBoxConfig{
            .row_height = 30,
            .visible_rows = 3,
            .overscan = 0,
        }};
    std::optional<uint32_t> object_details_combobox_row;
    std::optional<nw::toolset::VirtualComboBoxPopupPlacement>
        object_details_combobox_placement;
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
    nw::toolset::AppearanceCatalog door_appearance_catalog{
        .kind = nw::toolset::AppearanceCatalogKind::door};
    nw::toolset::AppearanceCatalog wing_appearance_catalog{
        .kind = nw::toolset::AppearanceCatalogKind::wing};
    nw::toolset::AppearanceCatalog tail_appearance_catalog{
        .kind = nw::toolset::AppearanceCatalogKind::tail};
    std::vector<uint32_t> appearance_matches;
    nw::toolset::VirtualListController appearance_list;
    nw::toolset::SoundCatalog sound_catalog;
    std::vector<uint32_t> sound_catalog_matches;
    nw::toolset::VirtualListController sound_catalog_list;
    AreaTileEditorState area_tile_editor;
    AreaWorkspaceSurface area_workspace_surface
        = AreaWorkspaceSurface::properties;
    std::unique_ptr<nw::toolset::RmlSmallsLanguageBinding> rml_smalls_binding;
    std::unique_ptr<nw::toolset::RmlSmallsDataModel> rml_smalls_data_model;
    std::string active_object_tab_id;
    std::string creature_feat_query;
    std::string creature_spell_query;
    std::string appearance_query;
    std::string sound_catalog_query;
    nw::ObjectHandle appearance_object{};
    nw::ObjectHandle appearance_body_preview_object{};
    nw::ObjectHandle color_editor_object{};
    int32_t color_editor_channel = -1;
    int32_t creature_spell_level = -1;
    int32_t creature_inventory_page = 0;
    int32_t creature_inventory_selection = -1;
    std::optional<nw::toolset::VirtualComboBoxPopupPlacement> creature_spell_popup_placement;
    ObjectWorkbenchSurface object_workbench_surface = ObjectWorkbenchSurface::details;
    CreatureSpellFilterField creature_spell_filter_field = CreatureSpellFilterField::none;
    AppearanceEditorField appearance_editor_field = AppearanceEditorField::appearance;
    uint64_t appearance_catalog_generation = std::numeric_limits<uint64_t>::max();
    uint64_t sound_catalog_generation = std::numeric_limits<uint64_t>::max();
    uint64_t observed_object_mutation_epoch = 0;
    uint64_t observed_area_structure_epoch = 0;
    nw::ObjectHandle stale_area_viewport{};
    bool backend_ready = false;
    bool module_dialog_open = false;
    bool suppress_terminal_toggle_text_input = false;
    bool bottom_dock_resizing = false;
    bool left_dock_resizing = false;
    Uint32 open_module_dialog_event = 0;

    std::string last_recent_query;
    uint64_t project_resource_generation = 0;
    std::string home_area_query;
    std::string last_command_query;
    std::string last_output_filter;
    std::string active_object_variable_warning;
    OutputSelectionState output_selection;
    OutputScrollAfterLayout output_scroll_after_layout = OutputScrollAfterLayout::none;
    std::string module_dialog_command;
    std::string module_dialog_default_location;
    std::string import_module_path;
    std::filesystem::path import_parent_dir;
    bool import_panel_open = false;
    std::string import_status;
    std::filesystem::path client_executable;
    nw::toolset::ProjectImportJob project_import;
    ProjectLoadRequest project_load;
    uint64_t import_module_generation = 0;
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
    ProjectBlueprintDragState project_blueprint_drag;
    nw::toolset::ManagedListReorderState managed_list_reorder;
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
    bool sound_catalog_list_configured = false;
    bool sound_catalog_rendered = false;
    bool sound_resource_selector_open = false;
    bool command_palette_ui_visible = false;
    bool command_palette_restore_captured = false;
    bool command_palette_restore_viewport_focus = false;
    Rml::Vector2f workspace_hover_refresh_point;

    std::vector<nw::toolset::RecentProjectEntry> recent_projects;
    std::vector<nw::toolset::LoadedAreaEntry> home_areas;
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
    nw::toolset::VirtualListRange rendered_sound_catalog_range{};
    int rendered_sound_catalog_row_count = 0;
};

bool synchronize_area_viewport_structure(
    ClientRenderer& renderer, AppState& state, bool report_failure);

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
void cancel_area_object_drag(ClientRenderer& renderer, AppState& state);
nw::toolset::CommandContext command_context(
    AppState& state, nw::toolset::CommandSource source);
void append_command_result(
    AppState& state, const nw::toolset::CommandResult& result);

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
        nw::toolset::load_recent_project_preferences(prefs, state.recent_projects);
    } catch (const std::exception& e) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to read rollnw client preferences: %s", e.what());
    }
}

bool save_ui_preferences(const AppState& state)
{
    if (state.preferences_path.empty()) {
        return false;
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
    nw::toolset::write_recent_project_preferences(prefs, state.recent_projects);
    prefs.erase("left_dock_width_px");
    prefs.erase("bottom_dock_height_px");
    prefs.erase("terminal_height_px");

    std::error_code ec;
    if (const auto parent = state.preferences_path.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to create rollnw client preferences directory: %s", ec.message().c_str());
            return false;
        }
    }

    // Existing preferences use the same atomic replacement as document saves.
    if (std::filesystem::exists(state.preferences_path, ec)) {
        std::string error;
        if (!nw::toolset::save_json_resource_document_atomic(state.preferences_path, prefs, error)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to save rollnw client preferences: %s", error.c_str());
            return false;
        }
        return true;
    }
    std::ofstream output{state.preferences_path};
    if (!output) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to open rollnw client preferences for writing");
        return false;
    }

    output << prefs.dump(2) << '\n';
    output.flush();
    if (!output) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to write rollnw client preferences");
        return false;
    }
    return true;
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

    const bool play_preview_active = state.play_preview.session.active()
        || state.play_preview.placement_pending();
    doc->SetClass("play_preview_active", play_preview_active);
    doc->SetClass("play_preview_placement_pending",
        state.play_preview.placement_pending());
    const auto& left = state.shell.docks.pane(nw::toolset::DockRegion::left);
    const auto& bottom = state.shell.docks.pane(nw::toolset::DockRegion::bottom);
    const int panel_bottom_px = bottom.visible ? bottom.size_px : 0;
    const bool show_left_dock = !play_preview_active
        && left_dock_visible_for_active_tab(state);
    if (auto* panel = doc->GetElementById("panel")) {
        panel->SetProperty("display", show_left_dock ? "flex" : "none");
        panel->SetProperty("width", std::to_string(left.size_px) + "px");
        panel->SetProperty("bottom", std::to_string(panel_bottom_px) + "px");
    }
    if (auto* workspace = doc->GetElementById("workspace_shell")) {
        workspace->SetProperty("left", std::to_string(show_left_dock ? left.size_px : 0) + "px");
        workspace->SetProperty("bottom", std::to_string(play_preview_active ? 0 : panel_bottom_px) + "px");
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

    state.recent_projects.erase(std::remove_if(state.recent_projects.begin(), state.recent_projects.end(), [&path](const nw::toolset::RecentProjectEntry& entry) {
        return entry.path == path;
    }),
        state.recent_projects.end());

    state.recent_projects.insert(state.recent_projects.begin(), nw::toolset::RecentProjectEntry{
                                                                    nw::toolset::project_display_name(normalized),
                                                                    path,
                                                                });
    if (state.recent_projects.size() > nw::toolset::kMaxRecentProjects) {
        state.recent_projects.resize(nw::toolset::kMaxRecentProjects);
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
    return element && element->IsVisible(true)
        && element->IsPointWithinElement(point);
}

bool point_within_viewport(ClientViewportRect rect, Rml::Vector2f point)
{
    const float left = static_cast<float>(rect.x);
    const float top = static_cast<float>(rect.y);
    const float right = left + static_cast<float>(rect.width);
    const float bottom = top + static_cast<float>(rect.height);
    return point.x >= left && point.x < right && point.y >= top && point.y < bottom;
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
        return command_palette_contains_point(
            palette_doc, state, to_context_point(window, event.button.x, event.button.y));
    case SDL_EVENT_MOUSE_MOTION:
        return command_palette_contains_point(
            palette_doc, state, to_context_point(window, event.motion.x, event.motion.y));
    case SDL_EVENT_MOUSE_WHEEL:
        return command_palette_contains_point(
            palette_doc, state, to_context_point(window, event.wheel.mouse_x, event.wheel.mouse_y));
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

void focus_workspace_viewport(Rml::ElementDocument* doc, AppState& state)
{
    clear_rml_focus(doc ? doc->GetContext() : nullptr);
    state.viewer_viewport_focused = true;
    if (state.command_palette_restore_captured) {
        state.command_palette_restore_focus_id.clear();
        state.command_palette_restore_viewport_focus = true;
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

Rml::Element* recent_item_at_point(Rml::ElementDocument* doc, Rml::Vector2f point)
{
    if (!doc) {
        return nullptr;
    }

    auto* list = doc->GetElementById("recent_list");
    if (!list) {
        return nullptr;
    }

    if (auto* search = doc->GetElementById("recent_search");
        search && search->IsVisible(true)
        && search->IsPointWithinElement(point)) {
        return nullptr;
    }

    if (!list->IsVisible(true) || !list->IsPointWithinElement(point)) {
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
        if (!element || !element->IsVisible(true)) {
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
    if (!tabs || !tabs->IsVisible(true)
        || !tabs->IsPointWithinElement(point)) {
        return nullptr;
    }

    const std::string class_text{class_name};
    const auto visit = [&](auto&& self, Rml::Element* element) -> Rml::Element* {
        if (!element || !element->IsVisible(true)) {
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
        document, host, state.managed_lists, true);
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
        document, host, state.managed_lists, true);
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
    state.last_recent_query = query;
    state.project_resource_generation = state.backend_ready ? nw::kernel::resman().generation() : 0;
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
        markup = nw::toolset::area_rows_markup(state.backend.list_areas(query));
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
            const std::string title = state.play_preview.selecting_actor
                ? std::string{"Choose Preview Creature"}
                : project_dir.empty()
                ? std::string{"Project"}
                : nw::toolset::project_display_name(project_dir);
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
            } else if (row.editor == nw::toolset::ObjectDetailsEditorKind::door_state) {
                markup += "<button type=\"button\" class=\"object_details_cycle_state\" data-row=\"";
                markup += std::to_string(index);
                markup += "\" data-current=\"";
                markup += std::to_string(row.edit_value);
                markup += "\" title=\"Cycle authored state: Closed, Open 1, Open 2\">";
                markup += escape_html(value);
                markup += "</button>";
            } else if (row.editor
                == nw::toolset::ObjectDetailsEditorKind::sound_position) {
                markup += "<button id=\"object_details_sound_position_field_";
                markup += std::to_string(index);
                markup += "\" type=\"button\" class=\"combobox_field "
                          "object_details_sound_position_field\" data-row=\"";
                markup += std::to_string(index);
                markup += "\" data-current=\"";
                markup += std::to_string(row.edit_value);
                markup += "\" title=\"Choose sound placement\">"
                          "<span class=\"combobox_value\">";
                markup += escape_html(value);
                markup += "</span><span class=\"combobox_arrow\">"
                          "<span class=\"combobox_arrow_indicator\"></span>"
                          "</span></button>";
            } else if (row.editor
                == nw::toolset::ObjectDetailsEditorKind::sound_volume) {
                const auto volume = nw::toolset::sound_volume_editor_value(
                    row.edit_value);
                if (!volume) {
                    markup += "Invalid";
                } else {
                    markup += "<span class=\"object_details_sound_volume_control\">"
                              "<input class=\"object_details_sound_volume\" type=\"range\" "
                              "min=\"0\" max=\"10\" step=\"1\" data-row=\"";
                    markup += std::to_string(index);
                    markup += "\" data-current=\"";
                    markup += std::to_string(row.edit_value);
                    markup += "\" value=\"";
                    markup += std::to_string(*volume);
                    markup += "\" title=\"Volume: 0 to 10\"/>"
                              "<span class=\"object_details_sound_volume_value\">";
                    markup += std::to_string(*volume);
                    markup += "</span></span>";
                }
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

void clear_object_details_combobox_state(AppState& state)
{
    state.object_details_combobox.close();
    state.object_details_combobox_row.reset();
    state.object_details_combobox_placement.reset();
}

void close_object_details_combobox(
    Rml::ElementDocument* doc, AppState& state)
{
    if (state.object_details_combobox_row) {
        const auto field_id = "object_details_sound_position_field_"
            + std::to_string(*state.object_details_combobox_row);
        if (auto* field = find_el(doc, field_id.c_str())) {
            field->SetClass("open", false);
        }
    }
    clear_object_details_combobox_state(state);
    if (auto* popup = find_el(doc, "object_details_combobox_popup")) {
        popup->SetClass("active", false);
        popup->SetInnerRML("");
    }
}

bool open_object_details_sound_position_combobox(
    Rml::ElementDocument* doc, AppState& state, uint32_t row_index)
{
    if (!active_object_details_matches_tab(state)
        || row_index >= state.object_details.rows.size()) {
        return false;
    }
    const auto& row = state.object_details.rows[row_index];
    if (row.kind != nw::toolset::ObjectDetailsRowKind::value
        || row.editor != nw::toolset::ObjectDetailsEditorKind::sound_position
        || row.edit_value < 0 || row.edit_value > 2) {
        return false;
    }
    if (state.object_details_combobox_row == row_index
        && state.object_details_combobox.is_active()) {
        if (state.object_details_combobox.popup_visible()) {
            state.object_details_combobox.hide_popup();
        } else {
            (void)state.object_details_combobox.show_popup();
        }
        state.object_details_combobox_placement.reset();
        return true;
    }

    std::vector<nw::toolset::VirtualComboBoxItem> options{
        {.key = 0, .label = "Everywhere"},
        {.key = 1, .label = "Positional"},
        {.key = 2, .label = "Random Position"},
    };
    close_object_details_combobox(doc, state);
    if (!state.object_details_combobox.open(
            std::move(options), row.edit_value)) {
        return false;
    }
    state.object_details_combobox_row = row_index;
    return true;
}

bool sync_object_details_combobox(
    Rml::ElementDocument* doc, AppState& state, bool force = false)
{
    if (!state.object_details_combobox.is_active()
        || !state.object_details_combobox_row) {
        return false;
    }
    const auto row_index = *state.object_details_combobox_row;
    if (!active_object_details_matches_tab(state)
        || row_index >= state.object_details.rows.size()) {
        close_object_details_combobox(doc, state);
        return true;
    }
    const auto& row = state.object_details.rows[row_index];
    const auto selected = state.object_details_combobox.selected_key();
    if (row.editor != nw::toolset::ObjectDetailsEditorKind::sound_position
        || !selected || *selected < 0 || *selected > 2) {
        close_object_details_combobox(doc, state);
        return true;
    }

    const auto field_id = "object_details_sound_position_field_"
        + std::to_string(row_index);
    auto* field = find_el(doc, field_id.c_str());
    auto* popup = find_el(doc, "object_details_combobox_popup");
    auto* workbench = find_el(doc, "object_workbench");
    if (!field || !popup || !workbench) {
        close_object_details_combobox(doc, state);
        return true;
    }

    const bool visible = state.object_details_combobox.popup_visible();
    field->SetClass("open", visible);
    popup->SetClass("active", visible);
    if (!visible) {
        return false;
    }

    const nw::toolset::VirtualComboBoxRect anchor{
        .x = static_cast<int>(std::lround(
            field->GetAbsoluteLeft() - workbench->GetAbsoluteLeft())),
        .y = static_cast<int>(std::lround(
            field->GetAbsoluteTop() - workbench->GetAbsoluteTop())),
        .width = static_cast<int>(std::lround(field->GetOffsetWidth())),
        .height = static_cast<int>(std::lround(field->GetOffsetHeight())),
    };
    const nw::toolset::VirtualComboBoxRect bounds{
        .width = static_cast<int>(std::lround(workbench->GetClientWidth())),
        .height = static_cast<int>(std::lround(workbench->GetClientHeight())),
    };
    const auto placement = state.object_details_combobox.place_popup(
        anchor, bounds);
    if (placement.width <= 0 || placement.height <= 0) {
        return false;
    }

    bool changed = false;
    if (!state.object_details_combobox_placement
        || *state.object_details_combobox_placement != placement) {
        popup->SetProperty("left", std::to_string(placement.left) + "px");
        popup->SetProperty("top", std::to_string(placement.top) + "px");
        popup->SetProperty("width", std::to_string(placement.width) + "px");
        popup->SetProperty("height", std::to_string(placement.height) + "px");
        state.object_details_combobox_placement = placement;
        changed = true;
    }

    const int observed_scroll_top = std::max(0,
        static_cast<int>(std::lround(popup->GetScrollTop())));
    auto update = state.object_details_combobox.update(
        placement.height, observed_scroll_top, force);
    if (update.replace_markup) {
        popup->SetInnerRML(update.markup);
    }
    if (update.set_scroll) {
        popup->SetScrollTop(static_cast<float>(update.scroll_top));
    }
    return changed || update.replace_markup || update.set_scroll;
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
void clear_active_sound_catalog(AppState& state);
void clear_color_editor(AppState& state);

void clear_active_object_details(AppState& state)
{
    clear_object_details_combobox_state(state);
    state.managed_list_reorder = {};
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
    clear_active_sound_catalog(state);
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
        const bool combobox_changed = state.object_details_combobox.is_active();
        if (combobox_changed) {
            close_object_details_combobox(doc, state);
        }
        return variable_changed || combobox_changed;
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
        return variable_changed
            || sync_object_details_combobox(doc, state, false);
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
    (void)sync_object_details_combobox(doc, state, true);
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
        && object_has_grid_inventory(state.creature_inventory.object.type);
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
    if (type == nw::ObjectType::door) {
        return nw::toolset::AppearanceCatalogKind::door;
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
    case nw::toolset::AppearanceCatalogKind::door:
        return state.door_appearance_catalog;
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
    case nw::toolset::AppearanceCatalogKind::door:
        return state.door_appearance_catalog;
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
    if (state.appearance_object.type == nw::ObjectType::placeable) {
        return nw::toolset::AppearanceCatalogKind::placeable;
    }
    if (state.appearance_object.type == nw::ObjectType::door) {
        return nw::toolset::AppearanceCatalogKind::door;
    }
    return nw::toolset::AppearanceCatalogKind::creature;
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
        if (state.appearance_object.type == nw::ObjectType::door) {
            const auto selectors = nw::toolset::door_appearance(
                nw::kernel::runtime(), state.appearance_object);
            return selectors && selectors->appearance == 0
                ? std::optional<int32_t>{selectors->generic_type}
                : std::nullopt;
        }
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
    state.door_appearance_catalog = {
        .kind = nw::toolset::AppearanceCatalogKind::door,
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
        (void)nw::toolset::build_appearance_catalog(*kind, catalog);
    }
    if (object.type == nw::ObjectType::creature) {
        for (const auto accessory_kind : {
                 nw::toolset::AppearanceCatalogKind::wing,
                 nw::toolset::AppearanceCatalogKind::tail,
             }) {
            auto& accessory_catalog = appearance_catalog(state, accessory_kind);
            if (accessory_catalog.status == nw::toolset::AppearanceCatalogStatus::empty) {
                (void)nw::toolset::build_appearance_catalog(
                    accessory_kind, accessory_catalog);
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

void configure_sound_catalog_list(AppState& state)
{
    if (state.sound_catalog_list_configured) {
        return;
    }
    state.sound_catalog_list.set_row_height(kSoundCatalogRowHeightPx);
    state.sound_catalog_list.set_overscan(kSoundCatalogOverscanRows);
    state.sound_catalog_list_configured = true;
}

void close_sound_resource_selector(AppState& state)
{
    state.sound_resource_selector_open = false;
    state.sound_catalog_query.clear();
}

void clear_active_sound_catalog(AppState& state)
{
    close_sound_resource_selector(state);
    state.sound_catalog_matches.clear();
    configure_sound_catalog_list(state);
    state.sound_catalog_list.set_total_rows(0);
    state.sound_catalog_list.set_scroll_top(0);
    state.sound_catalog_rendered = false;
}

bool active_sound_resource_selector_matches_tab(const AppState& state)
{
    return state.sound_resource_selector_open
        && state.object_workbench_surface == ObjectWorkbenchSurface::sounds
        && state.object_details.object.type == nw::ObjectType::sound
        && active_object_matches_tab(state);
}

void rebuild_sound_catalog(AppState& state, bool reset_selection)
{
    configure_sound_catalog_list(state);
    const uint64_t generation = state.backend_ready
        ? nw::kernel::resman().generation()
        : 0;
    if (generation != state.sound_catalog_generation) {
        state.sound_catalog = {};
        state.sound_catalog_generation = generation;
    }
    if (state.sound_catalog.status == nw::toolset::SoundCatalogStatus::empty) {
        (void)nw::toolset::build_sound_catalog(state.sound_catalog);
    }

    nw::toolset::filter_sound_catalog(state.sound_catalog,
        state.sound_catalog_query, state.sound_catalog_matches);
    state.sound_catalog_list.set_total_rows(
        static_cast<int>(state.sound_catalog_matches.size()));
    if (reset_selection
        || state.sound_catalog_list.selected() < 0
        || static_cast<size_t>(state.sound_catalog_list.selected())
            >= state.sound_catalog_matches.size()) {
        state.sound_catalog_list.set_selected(
            state.sound_catalog_matches.empty() ? -1 : 0);
    }
    state.sound_catalog_rendered = false;
}

class SoundCatalogListAdapter final : public nw::toolset::VirtualListAdapter {
public:
    SoundCatalogListAdapter(const nw::toolset::SoundCatalog& catalog,
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
        return static_cast<int>(matches_[static_cast<size_t>(index)]);
    }

    [[nodiscard]] std::string_view row_extra_classes() const override
    {
        return "sound_catalog_row";
    }

    [[nodiscard]] std::string render_row_inner(
        int index, bool /*selected*/) const override
    {
        const auto& value = row(index);
        std::string markup;
        markup.reserve(value.name.size() + value.resource.length() + 96);
        markup += "<div class=\"sound_catalog_name\">";
        markup += escape_html(value.name);
        markup += "</div><div class=\"sound_catalog_resref\">";
        markup += escape_html(value.resource.view());
        markup += "</div>";
        return markup;
    }

private:
    [[nodiscard]] const nw::toolset::SoundCatalogRow& row(int index) const
    {
        return catalog_.rows[matches_[static_cast<size_t>(index)]];
    }

    const nw::toolset::SoundCatalog& catalog_;
    const std::vector<uint32_t>& matches_;
};

bool sync_sound_catalog_window(
    Rml::ElementDocument* doc, AppState& state, bool force)
{
    auto* list = find_el(doc, "sound_catalog_rows");
    if (!list || !active_sound_resource_selector_matches_tab(state)) {
        return false;
    }

    const uint64_t generation = state.backend_ready
        ? nw::kernel::resman().generation()
        : 0;
    if (generation != state.sound_catalog_generation) {
        rebuild_sound_catalog(state, true);
        force = true;
    }

    configure_sound_catalog_list(state);
    const int viewport_height = std::max(1,
        static_cast<int>(std::lround(std::max(
            list->GetClientHeight(), list->GetOffsetHeight()))));
    const int scroll_top = std::max(0,
        static_cast<int>(std::lround(list->GetScrollTop())));
    state.sound_catalog_list.set_viewport_height(viewport_height);
    state.sound_catalog_list.set_scroll_top(scroll_top);
    const auto range = state.sound_catalog_list.compute_range();
    const int row_count = static_cast<int>(state.sound_catalog_matches.size());
    const bool stable_markup = !force && state.sound_catalog_rendered
        && row_count == state.rendered_sound_catalog_row_count
        && range.start == state.rendered_sound_catalog_range.start
        && range.end == state.rendered_sound_catalog_range.end;
    if (stable_markup) {
        return false;
    }

    std::string markup;
    if (state.sound_catalog.status
        != nw::toolset::SoundCatalogStatus::ready) {
        markup = "<div class=\"property_tree_empty error\">";
        markup += escape_html(state.sound_catalog.diagnostic.empty()
                ? std::string_view{"Sound data is unavailable."}
                : std::string_view{state.sound_catalog.diagnostic});
        markup += "</div>";
    } else if (state.sound_catalog_matches.empty()) {
        markup = "<div class=\"property_tree_empty\">"
                 "No sound resources match this filter.</div>";
    } else {
        markup = nw::toolset::render_virtual_list(state.sound_catalog_list,
            SoundCatalogListAdapter{
                state.sound_catalog, state.sound_catalog_matches});
    }

    list->SetInnerRML(markup);
    list->SetScrollTop(static_cast<float>(scroll_top));
    state.rendered_sound_catalog_range = range;
    state.rendered_sound_catalog_row_count = row_count;
    state.sound_catalog_rendered = true;
    return true;
}

void configure_area_tile_palette_list(AppState& state)
{
    auto& editor = state.area_tile_editor;
    if (editor.list_configured) {
        return;
    }
    editor.list.set_row_height(kAreaTilePaletteRowHeightPx);
    editor.list.set_overscan(kAreaTilePaletteOverscanRows);
    editor.list_configured = true;
}

void reset_area_tile_palette_folder_view(AreaTileEditorState& editor)
{
    editor.feedback.clear();
    editor.query.clear();
    editor.selected_row = -1;
    editor.list.set_selected(-1);
    editor.list.set_scroll_top(0);
    editor.list.set_total_rows(
        static_cast<int>(editor.palette.matches.size()));
    editor.rendered = false;
}

bool rebuild_area_tile_palette(AppState& state, nw::ObjectHandle area)
{
    auto& editor = state.area_tile_editor;
    if (!nw::toolset::build_area_tile_palette(area, editor.palette)) {
        editor.list.set_total_rows(0);
        editor.rendered = false;
        return false;
    }
    if (!nw::toolset::filter_area_tile_palette(
            editor.palette, editor.query)) {
        editor.list.set_total_rows(0);
        editor.rendered = false;
        return false;
    }
    configure_area_tile_palette_list(state);
    editor.list.set_total_rows(
        static_cast<int>(editor.palette.matches.size()));
    const auto selected = std::find(
        editor.palette.matches.begin(), editor.palette.matches.end(),
        static_cast<uint32_t>(editor.selected_row));
    if (selected == editor.palette.matches.end()) {
        editor.selected_row = -1;
        editor.list.set_selected(-1);
    } else {
        editor.list.set_selected(static_cast<int>(
            std::distance(editor.palette.matches.begin(), selected)));
    }
    editor.rendered = false;
    return true;
}

class AreaTilePaletteListAdapter final
    : public nw::toolset::VirtualListAdapter {
public:
    explicit AreaTilePaletteListAdapter(
        const nw::toolset::AreaTilePalette& palette)
        : palette_{palette}
    {
    }

    [[nodiscard]] int size() const override
    {
        return static_cast<int>(palette_.matches.size());
    }

    [[nodiscard]] int row_key(int index) const override
    {
        return static_cast<int>(
            palette_.matches[static_cast<size_t>(index)]);
    }

    [[nodiscard]] std::string_view row_extra_classes() const override
    {
        return "area_tile_palette_row";
    }

    [[nodiscard]] std::string render_row_inner(
        int index, bool /*selected*/) const override
    {
        const auto& value = row(index);
        std::string markup;
        markup.reserve(
            value.label.size() + value.thumbnail_source.size() + 160);
        markup += "<div class=\"area_tile_palette_thumbnail";
        if (value.kind == nw::toolset::AreaTilePaletteRowKind::folder) {
            markup += " area_tile_palette_folder_indicator\">";
            markup += "<span class=\"area_tile_palette_folder_glyph\">›</span>";
        } else {
            markup += "\">";
            if (!value.thumbnail_source.empty()) {
                markup += "<img src=\"";
                markup += escape_html(value.thumbnail_source);
                markup += "\"/>";
            } else {
                std::string_view glyph;
                switch (value.brush.kind) {
                case nw::toolset::AreaTileBrushKind::terrain:
                    glyph = "T";
                    break;
                case nw::toolset::AreaTileBrushKind::crosser:
                    glyph = "~";
                    break;
                case nw::toolset::AreaTileBrushKind::group:
                    glyph = "F";
                    break;
                case nw::toolset::AreaTileBrushKind::eraser:
                    glyph = "E";
                    break;
                case nw::toolset::AreaTileBrushKind::raise:
                    glyph = "+/-";
                    break;
                case nw::toolset::AreaTileBrushKind::lower:
                    glyph = "-";
                    break;
                }
                markup += "<span class=\"area_tile_palette_action_glyph\">";
                markup += glyph;
                markup += "</span>";
            }
        }
        markup += "</div><div class=\"area_tile_palette_text\">"
                  "<div class=\"area_tile_palette_action_name\">";
        markup += escape_html(value.label);
        markup += "</div></div>";
        return markup;
    }

private:
    [[nodiscard]] const nw::toolset::AreaTilePaletteRow& row(
        int index) const
    {
        return palette_.rows[palette_.matches[static_cast<size_t>(index)]];
    }

    const nw::toolset::AreaTilePalette& palette_;
};

void sync_area_tile_selection_info(
    Rml::ElementDocument* doc, const AppState& state, nw::ObjectHandle area_handle)
{
    auto* element = find_el(doc, "area_tile_selection_info");
    if (!element) {
        return;
    }
    const auto& editor = state.area_tile_editor;
    const auto& selection = editor.selection;
    const auto* area = nw::kernel::objects().get<nw::Area>(area_handle);
    if (!selection.active() || selection.area != area_handle || !area
        || !area->tileset
        || selection.source_tile_index >= area->tiles.size()) {
        element->SetInnerRML("");
        element->SetClass("visible", false);
        return;
    }

    const auto& tile = area->tiles[selection.source_tile_index];
    const uint32_t x = selection.source_tile_index
        % static_cast<uint32_t>(area->width);
    const uint32_t y = selection.source_tile_index
        / static_cast<uint32_t>(area->width);
    std::string_view label = "Tileset Group";
    if (selection.is_group()) {
        const auto row = std::ranges::find_if(editor.palette.rows,
            [&selection](const auto& value) {
                return value.kind
                    == nw::toolset::AreaTilePaletteRowKind::action
                    && value.brush.kind
                    == nw::toolset::AreaTileBrushKind::group
                    && value.brush.value
                    == static_cast<int32_t>(selection.group_index);
            });
        if (row != editor.palette.rows.end()) {
            label = row->label;
        }
    } else if (tile.id >= 0
        && static_cast<size_t>(tile.id) < area->tileset->tiles.size()) {
        label = area->tileset->tiles[static_cast<size_t>(tile.id)].model;
    } else {
        label = "Area Tile";
    }

    std::string markup;
    markup.reserve(label.size() + 180);
    markup += "<div class=\"area_tile_selection_title\">Selected ";
    markup += selection.is_group() ? "Group" : "Tile";
    markup += "</div><div class=\"area_tile_selection_name\">";
    markup += escape_html(label);
    markup += "</div><div class=\"area_tile_selection_meta\">Cell ";
    markup += std::to_string(x);
    markup += ", ";
    markup += std::to_string(y);
    markup += " · Tile ";
    markup += std::to_string(tile.id);
    markup += " · Height ";
    markup += std::to_string(tile.height);
    markup += " · Rotation ";
    markup += std::to_string(tile.orientation * 90);
    markup += "°";
    if (selection.is_group()) {
        markup += " · ";
        markup += std::to_string(selection.tile_indices.size());
        markup += " cells";
    }
    markup += "</div>";
    element->SetInnerRML(markup);
    element->SetClass("visible", true);
}

bool sync_area_tile_palette_window(
    Rml::ElementDocument* doc, AppState& state, bool force)
{
    auto& editor = state.area_tile_editor;
    auto* list = find_el(doc, "area_tile_palette_rows");
    const auto* active_tab = state.workspace.active_tab();
    const nw::ObjectHandle area = active_tab
            && active_tab->kind == nw::toolset::WorkspaceTabKind::area
        ? active_tab->document.object()
        : nw::ObjectHandle{};
    if (state.area_workspace_surface != AreaWorkspaceSurface::tiles
        || !list || area.type != nw::ObjectType::area) {
        return false;
    }
    if (auto* feedback = find_el(doc, "area_tile_palette_feedback")) {
        const bool visible = !editor.feedback.empty();
        feedback->SetInnerRML(
            visible ? escape_html(editor.feedback) : std::string{});
        feedback->SetClass("visible", visible);
    }
    if (auto* hint = find_el(doc, "area_tile_modifier_hint")) {
        hint->SetClass("visible",
            area_tile_pointer_modifier(SDL_GetModState())
                == nw::toolset::AreaTilePointerModifier::select);
    }
    sync_area_tile_selection_info(doc, state, area);

    if (editor.palette.area != area
        || editor.palette.resource_generation
            != nw::kernel::resman().generation()) {
        (void)rebuild_area_tile_palette(state, area);
        force = true;
    }
    configure_area_tile_palette_list(state);
    const int viewport_height = std::max(1,
        static_cast<int>(std::lround(std::max(
            list->GetClientHeight(), list->GetOffsetHeight()))));
    const int scroll_top = std::max(0,
        static_cast<int>(std::lround(list->GetScrollTop())));
    editor.list.set_viewport_height(viewport_height);
    editor.list.set_scroll_top(scroll_top);
    const auto range = editor.list.compute_range();
    const int row_count = static_cast<int>(editor.palette.matches.size());
    const bool stable = !force && editor.rendered
        && list->GetNumChildren() > 0
        && editor.rendered_row_count == row_count
        && editor.rendered_range.start == range.start
        && editor.rendered_range.end == range.end;
    if (stable) {
        return false;
    }
    (void)nw::toolset::load_area_tile_palette_thumbnails(
        editor.palette, range.start, range.end);

    std::string markup;
    if (editor.palette.status
        != nw::toolset::AreaTilePaletteStatus::ready) {
        markup = "<div class=\"property_tree_empty error\">";
        markup += escape_html(editor.palette.diagnostic.empty()
                ? std::string_view{"Tile palette is unavailable."}
                : std::string_view{editor.palette.diagnostic});
        markup += "</div>";
    } else if (editor.palette.matches.empty()) {
        markup = "<div class=\"property_tree_empty\">"
                 "No actions match this filter.</div>";
    } else {
        markup = nw::toolset::render_virtual_list(
            editor.list, AreaTilePaletteListAdapter{editor.palette});
    }
    list->SetInnerRML(markup);
    list->SetScrollTop(static_cast<float>(scroll_top));
    editor.rendered_range = range;
    editor.rendered_row_count = row_count;
    editor.rendered = true;
    return true;
}

bool commit_sound_catalog_selection(AppState& state, uint32_t row_index)
{
    if (!active_sound_resource_selector_matches_tab(state)
        || row_index >= state.sound_catalog.rows.size()) {
        return false;
    }

    const std::array additions{
        state.sound_catalog.rows[row_index].resource,
    };
    auto edit = nw::toolset::make_sound_resource_additions(
        nw::kernel::runtime(), state.object_details.object, additions);
    if (!edit) {
        append_output(state, "warn",
            "The Sound resource list is invalid or already contains 1,024 entries");
        return false;
    }

    auto result = state.backend.replace_sound_resources(std::move(*edit),
        "Add sound resource",
        command_context(state, nw::toolset::CommandSource::widget));
    const bool committed = result.ok();
    append_command_result(state, result);
    return committed;
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
    content_markup += "</div><div class=\"combobox_field appearance_field appearance_catalog_field\" data-field=\"";
    content_markup += appearance_editor_field_name(field);
    content_markup += "\"><span class=\"combobox_value\">";
    content_markup += escape_html(current
            ? appearance_editor_label(state, field, *current)
            : appearance_editor_label(state, field));
    content_markup += "</span><span class=\"combobox_arrow\">"
                      "<span class=\"combobox_arrow_indicator\"></span></span></div>";
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

void append_sound_resource_selector_markup(
    std::string& content_markup, const AppState& state)
{
    content_markup += "<div class=\"sound_resource_selector appearance_selector\">"
                      "<div class=\"appearance_selector_header\">"
                      "<button id=\"sound_resource_selector_back\" "
                      "class=\"appearance_selector_back panel_back_button\" title=\"Back\">"
                      "<span class=\"panel_back_icon\"><span class=\"panel_back_head\"></span>"
                      "<span class=\"panel_back_shaft\"></span></span></button>"
                      "<div class=\"appearance_selector_title\">Add Sound</div></div>"
                      "<div class=\"appearance_selector_filter\">"
                      "<input id=\"sound_catalog_search\" type=\"text\" "
                      "placeholder=\"Filter sounds...\" value=\"";
    content_markup += escape_html(state.sound_catalog_query);
    content_markup += "\" /></div><div id=\"sound_catalog_rows\" "
                      "class=\"appearance_selector_rows sound_catalog_rows\">"
                      "<div class=\"property_tree_empty\">Loading sounds...</div>"
                      "</div></div>";
}

void append_placeable_appearance_markup(
    std::string& content_markup, const AppState& state)
{
    const auto current = appearance_editor_value(
        state, AppearanceEditorField::appearance);
    const auto* row = current
        ? nw::toolset::find_appearance_catalog_row(
              state.placeable_appearance_catalog, *current)
        : nullptr;

    content_markup += "<div class=\"placeable_appearance_editor active\">";
    if (!current) {
        content_markup += "<div class=\"property_tree_empty error\">"
                          "Placeable appearance is unavailable.</div>";
    } else {
        content_markup += "<div class=\"placeable_appearance_summary\">"
                          "<div class=\"placeable_appearance_row\">"
                          "<span class=\"placeable_appearance_label\">Name</span>"
                          "<span class=\"placeable_appearance_value\">";
        content_markup += escape_html(row
                ? std::string_view{row->name}
                : std::string_view{"Unavailable"});
        content_markup += "</span></div><div class=\"placeable_appearance_row\">"
                          "<span class=\"placeable_appearance_label\">Model</span>"
                          "<span class=\"placeable_appearance_value\">";
        if (row) {
            content_markup += escape_html(row->model);
        }
        content_markup += "</span></div></div>";
        if (!row) {
            content_markup += "<div class=\"property_tree_empty error\">"
                              "Appearance ";
            content_markup += std::to_string(*current);
            content_markup += " is unavailable.</div>";
        }
    }
    content_markup += "<div class=\"placeable_appearance_actions\">"
                      "<button id=\"placeable_appearance_previous\" "
                      "class=\"placeable_appearance_cycle_button\" type=\"button\" "
                      "title=\"Previous appearance\">&#x2039;</button>"
                      "<button id=\"placeable_appearance_open\" "
                      "class=\"placeable_appearance_open appearance_catalog_field\" "
                      "data-field=\"appearance\" type=\"button\">Choose Appearance</button>"
                      "<button id=\"placeable_appearance_next\" "
                      "class=\"placeable_appearance_cycle_button\" type=\"button\" "
                      "title=\"Next appearance\">&#x203a;</button></div></div>";
}

void append_door_appearance_markup(
    std::string& content_markup, const AppState& state)
{
    const auto selectors = nw::toolset::door_appearance(
        nw::kernel::runtime(), state.appearance_object);
    const nw::DoorTypeInfo* door_type = nullptr;
    const nw::GenericDoorInfo* generic_door = nullptr;
    if (selectors) {
        if (selectors->appearance == 0) {
            generic_door = nw::kernel::rules().genericdoors.get(
                nw::GenericDoor::make(selectors->generic_type));
        } else {
            door_type = nw::kernel::rules().doortypes.get(
                nw::DoorType::make(selectors->appearance));
        }
    }
    const bool generic = selectors && selectors->appearance == 0;
    const auto model = generic && generic_door
        ? generic_door->model
        : door_type ? door_type->model
                    : nw::Resref{};
    const bool available = (generic
                                   ? generic_door && generic_door->valid()
                                   : door_type && door_type->valid())
        && nw::kernel::resman().contains(
            {model, nw::ResourceType::mdl});
    const auto name = generic && generic_door
        ? generic_door->editor_name()
        : door_type ? door_type->editor_name()
                    : nw::String{};

    content_markup += "<div class=\"door_appearance_editor active\">";
    if (!selectors) {
        content_markup += "<div class=\"property_tree_empty error\">"
                          "Door appearance is unavailable.</div>";
    } else {
        content_markup += "<div class=\"door_appearance_summary\">"
                          "<div class=\"door_appearance_row\"><span "
                          "class=\"door_appearance_label\">Source</span><span "
                          "class=\"door_appearance_value\">";
        content_markup += generic ? "Generic Door" : "Tileset Door";
        content_markup += "</span></div><div class=\"door_appearance_row\"><span "
                          "class=\"door_appearance_label\">Name</span><span "
                          "class=\"door_appearance_value\">";
        content_markup += escape_html(name);
        content_markup += "</span></div><div class=\"door_appearance_row\"><span "
                          "class=\"door_appearance_label\">Model</span><span "
                          "class=\"door_appearance_value\">";
        content_markup += escape_html(model.view());
        content_markup += "</span></div></div>";
        if (!available) {
            content_markup += "<div class=\"property_tree_empty error\">"
                              "Selected Door appearance is unavailable.</div>";
        }
    }
    content_markup += "<div class=\"door_appearance_actions\">"
                      "<button id=\"door_appearance_previous\" "
                      "class=\"door_appearance_cycle_button\" type=\"button\" "
                      "title=\"Previous appearance\">&#x2039;</button>"
                      "<button id=\"door_appearance_open\" "
                      "class=\"door_appearance_open appearance_catalog_field\" "
                      "data-field=\"appearance\" type=\"button\">Choose Appearance</button>"
                      "<button id=\"door_appearance_next\" "
                      "class=\"door_appearance_cycle_button\" type=\"button\" "
                      "title=\"Next appearance\">&#x203a;</button></div></div>";
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

void append_creature_inventory_markup(std::string& content_markup, const AppState& state);

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

void hydrate_door_workbench(Rml::ElementDocument* doc, const AppState& state)
{
    if (!find_el(doc, "door_surface_details")) {
        return;
    }

    struct DoorSurfaceElements {
        const char* tab_id;
        const char* surface_id;
        ObjectWorkbenchSurface surface;
    };
    static constexpr std::array surfaces{
        DoorSurfaceElements{"door_tab_details", "door_surface_details",
            ObjectWorkbenchSurface::details},
        DoorSurfaceElements{"door_tab_variables", "door_surface_variables",
            ObjectWorkbenchSurface::variables},
        DoorSurfaceElements{"door_tab_appearance", "door_surface_appearance",
            ObjectWorkbenchSurface::appearance},
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
        && state.object_details.object.type == nw::ObjectType::door;
    if (auto* header = find_el(doc, "door_object_header")) {
        header->SetClass("visible", show_header);
    }
    if (show_header) {
        if (auto* title = find_el(doc, "door_object_title")) {
            title->SetInnerRML(escape_html(
                nw::toolset::live_object_display_name(state.object_details.object)));
        }
    }

    if (state.object_workbench_surface == ObjectWorkbenchSurface::appearance) {
        std::string markup;
        if (state.appearance_selector_open) {
            append_appearance_selector_markup(markup, state);
        } else {
            append_door_appearance_markup(markup, state);
        }
        if (auto* target = find_el(doc, "door_appearance_dynamic")) {
            target->SetInnerRML(markup);
        }
    }
}

void hydrate_placeable_workbench(Rml::ElementDocument* doc, const AppState& state)
{
    if (!find_el(doc, "placeable_surface_details")) {
        return;
    }

    struct PlaceableSurfaceElements {
        const char* tab_id;
        const char* surface_id;
        ObjectWorkbenchSurface surface;
    };
    static constexpr std::array surfaces{
        PlaceableSurfaceElements{"placeable_tab_details", "placeable_surface_details",
            ObjectWorkbenchSurface::details},
        PlaceableSurfaceElements{"placeable_tab_variables", "placeable_surface_variables",
            ObjectWorkbenchSurface::variables},
        PlaceableSurfaceElements{"placeable_tab_appearance", "placeable_surface_appearance",
            ObjectWorkbenchSurface::appearance},
        PlaceableSurfaceElements{"placeable_tab_inventory", "placeable_surface_inventory",
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
        && state.object_details.object.type == nw::ObjectType::placeable;
    if (auto* header = find_el(doc, "placeable_object_header")) {
        header->SetClass("visible", show_header);
    }
    if (show_header) {
        if (auto* title = find_el(doc, "placeable_object_title")) {
            title->SetInnerRML(escape_html(
                nw::toolset::live_object_display_name(state.object_details.object)));
        }
    }

    if (state.object_workbench_surface == ObjectWorkbenchSurface::appearance) {
        std::string markup;
        if (state.appearance_selector_open) {
            append_appearance_selector_markup(markup, state);
        } else {
            append_placeable_appearance_markup(markup, state);
        }
        if (auto* target = find_el(doc, "placeable_appearance_dynamic")) {
            target->SetInnerRML(markup);
        }
    } else if (state.object_workbench_surface == ObjectWorkbenchSurface::inventory) {
        std::string markup;
        append_creature_inventory_markup(markup, state);
        if (auto* target = find_el(doc, "placeable_inventory_dynamic")) {
            target->SetInnerRML(markup);
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
    markup += " class=\"combobox_field creature_spell_filter_field";
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
    markup += "\"><span class=\"combobox_value creature_spell_filter_value\">";
    markup += escape_html(value);
    markup += "</span><span class=\"combobox_arrow\">"
              "<span class=\"combobox_arrow_indicator\"></span></span>"
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
    if (active_creature_spell_filter_matches_tab(state)
        && state.creature_spell_combobox.popup_visible()) {
        markup += "<div id=\"creature_spell_filter_options\" "
                  "class=\"combobox_options combobox_popup creature_spell_filter_options\"";
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
        markup.clear();
        if (active_color_editor_matches_tab(state)) {
            append_creature_color_selector_markup(markup, state);
        } else if (state.appearance_selector_open) {
            append_appearance_selector_markup(markup, state);
        } else {
            if (auto* main = find_el(doc, "creature_appearance_main")) {
                main->SetClass("active", true);
            }
            if (auto* modal = find_el(doc, "creature_appearance_modal_dynamic")) {
                modal->SetClass("active", false);
                modal->SetInnerRML("");
            }
            std::string primary_markup;
            append_appearance_catalog_field_markup(
                primary_markup, state, AppearanceEditorField::appearance);
            if (auto* primary = find_el(
                    doc, "creature_appearance_primary_dynamic")) {
                primary->SetInnerRML(primary_markup);
            }
            std::string secondary_markup;
            append_creature_accessories_markup(secondary_markup, state);
            append_creature_colors_markup(secondary_markup, state);
            if (auto* secondary = find_el(
                    doc, "creature_appearance_secondary_dynamic")) {
                secondary->SetInnerRML(secondary_markup);
            }
            break;
        }
        if (auto* main = find_el(doc, "creature_appearance_main")) {
            main->SetClass("active", false);
        }
        if (auto* modal = find_el(doc, "creature_appearance_modal_dynamic")) {
            modal->SetClass("active", true);
            modal->SetInnerRML(markup);
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
    if (auto* overlays = find_el(doc, "creature_workbench_dynamic_overlays")) {
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
    if (area_tab
        && state.area_workspace_surface == AreaWorkspaceSurface::objects
        && !active_object_matches_tab(state)) {
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
    if (state.object_details.object.type == nw::ObjectType::door) {
        content_markup += "<template src=\"door-workbench\"></template>";
        return;
    }
    if (state.object_details.object.type == nw::ObjectType::placeable) {
        content_markup += "<template src=\"placeable-workbench\"></template>";
        return;
    }
    const auto object_type = state.object_details.object.type;
    const bool project_module = object_type == nw::ObjectType::module
        && !state.backend.current_project_dir().empty();
    content_markup += "<div id=\"object_workbench\" class=\"object_workbench\">";
    if (area_tab && active_object_matches_tab(state)
        && state.object_details.object.type != nw::ObjectType::area) {
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
    if (object_type == nw::ObjectType::encounter) {
        content_markup += "<div class=\"object_workbench_tab";
        if (state.object_workbench_surface == ObjectWorkbenchSurface::spawns) {
            content_markup += " active";
        }
        content_markup += "\" data-surface=\"spawns\">Spawns</div>";
    } else if (object_type == nw::ObjectType::sound) {
        content_markup += "<div class=\"object_workbench_tab";
        if (state.object_workbench_surface == ObjectWorkbenchSurface::sounds) {
            content_markup += " active";
        }
        content_markup += "\" data-surface=\"sounds\">Sounds</div>";
    } else if (object_type == nw::ObjectType::store) {
        content_markup += "<div class=\"object_workbench_tab";
        if (state.object_workbench_surface == ObjectWorkbenchSurface::store_inventory) {
            content_markup += " active";
        }
        content_markup += "\" data-surface=\"store-inventory\">Inventory</div>";
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
    } else if (object_type == nw::ObjectType::encounter
        && state.object_workbench_surface == ObjectWorkbenchSurface::spawns) {
        content_markup += "<div id=\"encounter_spawn_collection\" "
                          "class=\"data_object_collection smalls_refresh\" "
                          "onrefresh=\"encounter_spawns_refresh()\">"
                          "<div class=\"data_collection_header encounter_spawn_header\">"
                          "<span>Creature</span><span>CR</span><span>Appearance</span><span>Single</span>"
                          "</div><div class=\"data_collection_summary managed_list_title\" "
                          "data-list-id=\"data.encounter.spawns\"></div>"
                          "<div class=\"data_collection_rows encounter_spawn_rows managed_list_rows\" "
                          "tabindex=\"0\" "
                          "data-list-id=\"data.encounter.spawns\" "
                          "data-empty-text=\"This encounter has no spawn entries.\"></div></div>";
    } else if (object_type == nw::ObjectType::sound
        && state.object_workbench_surface == ObjectWorkbenchSurface::sounds) {
        if (state.sound_resource_selector_open) {
            append_sound_resource_selector_markup(content_markup, state);
        } else {
            content_markup += "<div id=\"sound_resource_collection\" "
                              "class=\"data_object_collection smalls_refresh\" "
                              "onrefresh=\"sound_resources_refresh()\">"
                              "<div class=\"data_collection_header sound_resource_header\">"
                              "<span>Resource</span></div>"
                              "<div class=\"data_collection_summary managed_list_title\" "
                              "data-list-id=\"data.sound.resources\"></div>"
                              "<div id=\"sound_resource_rows\" "
                              "class=\"data_collection_rows sound_resource_rows managed_list_rows\" "
                              "tabindex=\"0\" "
                              "data-list-id=\"data.sound.resources\" "
                              "data-empty-text=\"This sound object has no sound resources.\"></div>"
                              "<div class=\"data_collection_action_bar\">"
                              "<button id=\"sound_resource_add\" type=\"button\" "
                              "class=\"data_collection_action add\" title=\"Add sound resource\">"
                              "<span class=\"data_collection_action_mark horizontal\"></span>"
                              "<span class=\"data_collection_action_mark vertical\"></span>"
                              "</button></div></div>";
        }
    } else if (object_type == nw::ObjectType::store
        && state.object_workbench_surface == ObjectWorkbenchSurface::store_inventory) {
        content_markup += "<div id=\"store_inventory_collection\" "
                          "class=\"data_object_collection smalls_refresh\" "
                          "onrefresh=\"store_inventory_refresh()\">"
                          "<div class=\"store_inventory_drop_targets\">"
                          "<div id=\"store_inventory_drop_0\" class=\"store_inventory_drop_target\" "
                          "data-category=\"0\">Armor</div>"
                          "<div id=\"store_inventory_drop_1\" class=\"store_inventory_drop_target\" "
                          "data-category=\"1\">Misc</div>"
                          "<div id=\"store_inventory_drop_2\" class=\"store_inventory_drop_target\" "
                          "data-category=\"2\">Potions</div>"
                          "<div id=\"store_inventory_drop_3\" class=\"store_inventory_drop_target\" "
                          "data-category=\"3\">Rings</div>"
                          "<div id=\"store_inventory_drop_4\" class=\"store_inventory_drop_target\" "
                          "data-category=\"4\">Weapons</div>"
                          "</div>"
                          "<div class=\"data_collection_header store_inventory_header\">"
                          "<span>Category</span><span>Item</span><span>Resref</span><span>Stack</span>"
                          "</div><div class=\"data_collection_summary managed_list_title\" "
                          "data-list-id=\"data.store.inventory\"></div>"
                          "<div class=\"data_collection_rows store_inventory_rows managed_list_rows\" "
                          "tabindex=\"0\" "
                          "data-list-id=\"data.store.inventory\" "
                          "data-empty-text=\"This store has no inventory items.\"></div></div>";
    } else {
        content_markup += "<div class=\"property_tree_header\"><span class=\"property_tree_header_name\">Field</span>";
        content_markup += "<span class=\"property_tree_header_value\">Value</span>";
        content_markup += "<span id=\"property_tree_count\" class=\"property_tree_count\">";
        content_markup += std::to_string(active_details_row_count(state));
        content_markup += "</span></div><div id=\"property_tree_rows\" class=\"property_tree_rows\">";
        content_markup += "<div class=\"property_tree_empty\">Select an object to inspect.</div></div>";
    }
    if (object_type == nw::ObjectType::sound) {
        content_markup += "<div id=\"object_details_combobox_popup\" "
                          "class=\"combobox_options combobox_popup "
                          "object_details_combobox_popup\"></div>";
    }
    content_markup += "</div>";
}

void append_area_tile_palette_markup(
    std::string& content_markup, const AppState& state)
{
    const auto& editor = state.area_tile_editor;
    std::string_view folder_label = "Tiles";
    if (editor.palette.current_folder < editor.palette.rows.size()) {
        folder_label
            = editor.palette.rows[editor.palette.current_folder].label;
    }
    content_markup += "<div id=\"area_tile_palette\" "
                      "class=\"object_workbench area_tile_palette\">"
                      "<div class=\"object_workbench_header area_tile_palette_header\">";
    if (editor.palette.current_folder != editor.palette.root_folder) {
        content_markup += "<button id=\"area_tile_editor_back\" type=\"button\" "
                          "class=\"panel_back_button\" title=\"Back\">"
                          "<span class=\"panel_back_icon\"><span class=\"panel_back_head\"></span>"
                          "<span class=\"panel_back_shaft\"></span></span></button>";
    }
    content_markup += "<div class=\"area_tile_palette_heading\">"
                      "<div class=\"object_workbench_title\">";
    content_markup += escape_html(folder_label);
    content_markup += "</div></div></div>"
                      "<div class=\"area_tile_palette_controls\">"
                      "<input id=\"area_tile_palette_search\" type=\"text\" value=\"";
    content_markup += escape_html(editor.query);
    content_markup += "\" placeholder=\"Find terrain or feature\"/>"
                      "<div id=\"area_tile_palette_feedback\" "
                      "class=\"area_tile_palette_feedback\"></div>"
                      "<div id=\"area_tile_modifier_hint\" "
                      "class=\"area_tile_modifier_hint\">"
                      "Left-click selects a tile or group &middot; "
                      "Right-click cycles its variation</div>"
                      "</div><div id=\"area_tile_selection_info\" "
                      "class=\"area_tile_selection_info\"></div>"
                      "<div id=\"area_tile_palette_rows\" "
                      "class=\"area_tile_palette_rows\"></div></div>";
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
            append_area_tile_palette_markup(content_markup, state);
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
        if (data_workbench_only(state.object_details.object.type,
                state.object_workbench_surface)) {
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
    if (tab.dirty) {
        out += " dirty";
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

void append_workspace_home_markup(std::string& content_markup, AppState& state)
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
    content_markup += "<div class=\"home_app_version\">" ROLLNW_TOOL_NAME " " ROLLNW_TOOL_VERSION "</div>";
    content_markup += "<div class=\"home_project_actions\">"
                      "<button id=\"home_import_module\"";
    if (state.project_import.active() || state.module_dialog_open) {
        content_markup += " disabled";
    }
    content_markup += ">Import Module...</button>"
                      "<button id=\"home_open_project\">Open Project...</button></div>";
    if (state.import_panel_open) {
        const bool busy = state.project_import.active() || state.module_dialog_open;
        const auto destination = state.import_parent_dir.empty() || state.import_module_path.empty()
            ? std::string{}
            : (state.import_parent_dir / std::filesystem::path{state.import_module_path}.stem()).string();
        content_markup += "<div class=\"home_import_panel\"><div class=\"home_section_title\">Import Module</div>";
        const std::array<std::array<std::string_view, 3>, 2> fields{{
            {"Source", state.import_module_path, "home_import_source"},
            {"Destination", destination, "home_import_destination"},
        }};
        for (const auto& field : fields) {
            content_markup += "<div class=\"home_import_row\"><div class=\"home_import_label\">";
            content_markup += field[0];
            content_markup += "</div><div class=\"home_import_path\" title=\"";
            content_markup += escape_html(field[1]);
            content_markup += "\">";
            content_markup += field[1].empty() ? "Not selected" : escape_html(field[1]);
            content_markup += "</div><button id=\"";
            content_markup += field[2];
            content_markup += busy ? "\" disabled>Browse...</button></div>" : "\">Browse...</button></div>";
        }
        content_markup += "<div class=\"home_import_hint\">Choose a destination parent folder. "
                          "A new folder named after the module will be created inside it; existing folders are not overwritten.</div>"
                          "<div class=\"home_project_actions\"><button id=\"home_import_start\"";
        if (busy || destination.empty()) { content_markup += " disabled"; }
        content_markup += ">Import</button><button id=\"home_import_close\"";
        if (busy) { content_markup += " disabled"; }
        content_markup += ">Close</button></div>";
        if (state.project_import.active()) {
            content_markup += "<div class=\"home_import_progress\" title=\"Import in progress\">"
                              "<div class=\"home_import_progress_fill\"></div></div>";
        }
        content_markup += "</div>";
    }
    content_markup += "<div class=\"home_import_status\">";
    content_markup += state.import_status.empty()
        ? "Import a .mod file into a new editable project, or open an existing project folder."
        : escape_html(state.import_status);
    content_markup += "</div>";

    if (!module_open) {
        nw::toolset::refresh_recent_projects(state.recent_projects);
        content_markup += "<div id=\"home_project_list\">";
        content_markup += nw::toolset::recent_projects_markup(state.recent_projects);
        content_markup += "</div>";
    }
    if (module_open) {
        content_markup += "<div id=\"home_area_browser\"><div class=\"home_area_browser_header\">";
        content_markup += "<div class=\"home_section_title\">Areas</div>";
        content_markup += "<div id=\"home_area_count\" class=\"home_area_count\">";
        content_markup += std::to_string(state.home_areas.size());
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

    state.home_areas = state.backend.list_areas(state.home_area_query);
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
    const int logical_rows = static_cast<int>((state.home_areas.size()
                                                  + static_cast<size_t>(columns) - 1)
        / static_cast<size_t>(columns));
    state.home_area_list.set_total_rows(logical_rows);
    state.home_area_list.set_viewport_height(std::max(0,
        static_cast<int>(std::lround(std::max(list->GetClientHeight(), list->GetOffsetHeight())))));
    state.home_area_list.set_scroll_top(std::max(0,
        static_cast<int>(std::lround(list->GetScrollTop()))));
    const auto range = state.home_area_list.compute_range();
    if (!force
        && state.rendered_home_area_count == state.home_areas.size()
        && state.rendered_home_area_columns == columns
        && state.rendered_home_area_range.start == range.start
        && state.rendered_home_area_range.end == range.end) {
        return false;
    }

    const float scroll_top = list->GetScrollTop();
    std::string markup;
    if (state.home_areas.empty()) {
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
                if (index < state.home_areas.size()) {
                    append_home_area_card_markup(state.home_areas[index], index, markup);
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
        count->SetInnerRML(std::to_string(state.home_areas.size()));
    }
    state.rendered_home_area_range = range;
    state.rendered_home_area_count = state.home_areas.size();
    state.rendered_home_area_columns = columns;
    return true;
}

void refresh_workspace_content(Rml::ElementDocument* doc, AppState& state)
{
    if (!doc) {
        return;
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
    state.object_workbench_tab_scroll_pending = true;
    apply_shell_layout(doc, state);
    sync_home_area_window(doc, state, true);
    hydrate_creature_workbench(doc, state);
    hydrate_item_workbench(doc, state);
    hydrate_door_workbench(doc, state);
    hydrate_placeable_workbench(doc, state);
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
    if (focus_area) focus_workspace_viewport(doc, state);
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
        if (tab.dirty && tab.kind != nw::toolset::WorkspaceTabKind::area) {
            tab_markup += " *";
        }
        tab_markup += "</span>";
        if (tab.kind == nw::toolset::WorkspaceTabKind::area) {
            tab_markup += "<span class=\"workspace_tab_dirty\" title=\"Unsaved changes\"></span>";
        }
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
    state.object_workbench_tab_scroll_pending = true;
    apply_shell_layout(doc, state);
    sync_home_area_window(doc, state, true);
    hydrate_creature_workbench(doc, state);
    hydrate_item_workbench(doc, state);
    hydrate_door_workbench(doc, state);
    hydrate_placeable_workbench(doc, state);
    refresh_smalls_elements(doc, state);
    sync_object_details_window(doc, state, true);
    nw::toolset::sync_managed_lists(
        doc, nw::toolset::ui_v1_host(), state.managed_lists, true);
    sync_creature_inventory_window(doc, state, true);
    nw::toolset::sync_dialog_view(doc, state.dialog_view, true);
    if (focus_area) focus_workspace_viewport(doc, state);
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

void reset_play_preview_input(PlayPreviewState& preview) noexcept
{
    preview.fixed_step = {};
    preview.pending_input = {};
    preview.mouse_look_x = 0.0f;
    preview.mouse_look_y = 0.0f;
    preview.mouse_sample_seconds = 0.0;
    preview.wheel_zoom = 0.0f;
}

void restore_play_preview_picker_shell(Rml::ElementDocument* doc, AppState& state)
{
    if (!state.play_preview.selecting_actor) return;
    state.play_preview.selecting_actor = false;
    if (state.play_preview.picker_was_showing_project_tree) {
        state.shell.set_showing_project_tree(true);
    } else if (state.play_preview.picker_was_showing_areas) {
        state.shell.set_showing_areas(true);
    } else {
        state.shell.set_showing_project_tree(false);
    }
    set_input_value(doc, "recent_search", state.play_preview.picker_previous_query);
    state.play_preview.picker_previous_query.clear();
    refresh_recent_list(doc, state);
    focus_workspace_viewport(doc, state);
}

void stop_play_preview(ClientRenderer& renderer,
    SystemInterface_SDL& system_interface,
    Rml::ElementDocument* doc,
    AppState& state)
{
    if (state.play_preview.visuals_attached
        || state.play_preview.session.active()) {
        if (!renderer.end_toolset_preview_visuals()) {
            append_output(state, "error", "Failed to remove play-preview visuals");
        }
    }
    state.play_preview.visuals_attached = false;
    nw::toolset::stop_toolset_preview(state.play_preview.session);
    state.play_preview.pending_actor = {};
    state.play_preview.area = nw::ObjectHandle{};
    state.play_preview.module_generation = 0;
    state.play_preview.tab_id.clear();
    state.play_preview.placement_diagnostic.clear();
    reset_play_preview_input(state.play_preview);
    apply_shell_layout(doc, state);
    system_interface.SetMouseCursor("arrow");
}

void request_play_preview_actor(Rml::ElementDocument* doc, AppState& state,
    std::string_view reason)
{
    if (!reason.empty()) append_output(state, "warn", reason);
    if (!state.play_preview.selecting_actor) {
        state.play_preview.selecting_actor = true;
        state.play_preview.picker_was_showing_project_tree
            = state.shell.showing_project_tree;
        state.play_preview.picker_was_showing_areas = state.shell.showing_areas;
        state.play_preview.picker_previous_query = get_input_value(doc, "recent_search");
        set_input_value(doc, "recent_search", "");
        state.shell.set_showing_project_tree(true);
    }
    refresh_recent_list(doc, state);
    state.viewer_viewport_focused = false;
    if (auto* search = find_el(doc, "recent_search")) search->Focus();
}

float play_preview_yaw(const ClientViewportRay& ray) noexcept
{
    const glm::vec2 direction{ray.displacement.x, ray.displacement.y};
    const float length_squared = glm::dot(direction, direction);
    if (!std::isfinite(length_squared) || length_squared <= 1.0e-8f) {
        return 0.0f;
    }
    return std::atan2(direction.y, direction.x);
}

bool start_play_preview_from_ray(ClientRenderer& renderer,
    SystemInterface_SDL& system_interface,
    Rml::ElementDocument* doc,
    AppState& state,
    const ClientViewportRay& ray)
{
    if (!state.play_preview.placement_pending()) return false;

    const nw::toolset::PreviewSessionStartInput input{
        .area = state.play_preview.area,
        .actor = state.play_preview.pending_actor,
        .spawn_ray = {
            .origin = ray.origin,
            .displacement = ray.displacement,
        },
        .camera = {.yaw = play_preview_yaw(ray)},
        .spawn_source = nw::toolset::PreviewSessionStartInput::SpawnSource::navigation_ray,
    };
    const auto started = nw::toolset::start_toolset_preview(
        state.play_preview.session, input);
    if (!started.ok()) {
        const std::string diagnostic = started.diagnostic.empty()
            ? "Play-preview startup failed"
            : started.diagnostic;
        state.play_preview.placement_diagnostic = diagnostic;
        system_interface.SetMouseCursor("cross");
        append_output(state, "error", diagnostic);
        return false;
    }

    const std::array actors{started.actor};
    if (!renderer.begin_toolset_preview_visuals(
            actors,
            nw::toolset::toolset_preview_door_visual_states(
                state.play_preview.session),
            state.play_preview.session.camera())) {
        stop_play_preview(renderer, system_interface, doc, state);
        append_output(state, "error", "Failed to attach play-preview actor visuals");
        return false;
    }

    state.play_preview.pending_actor = {};
    state.play_preview.placement_diagnostic.clear();
    state.play_preview.visuals_attached = true;
    reset_play_preview_input(state.play_preview);
    apply_shell_layout(doc, state);
    system_interface.SetMouseCursor("arrow");
    append_output(state, "info",
        started.stats.movement_disabled
            ? fmt::format(
                  "Play preview started with {} navigation polygons; this creature's authored movement rate is NOMOVE",
                  started.stats.navigation_polygon_count)
            : fmt::format("Play preview started with {} navigation polygons",
                  started.stats.navigation_polygon_count));
    return true;
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

    std::filesystem::path actor_path = selected_actor;
    if (actor_path.empty()) {
        const auto settings = nw::toolset::load_project_preview_settings(
            project_dir);
        if (!settings.ok || settings.test_actor.empty()) {
            request_play_preview_actor(doc, state,
                settings.message.empty()
                    ? std::string_view{"Choose a Creature blueprint for play preview"}
                    : std::string_view{settings.message});
            return false;
        }
        actor_path = settings.test_actor;
    }

    const nw::Resource actor = nw::Resource::from_path(actor_path, false);
    if (actor.type != nw::ResourceType::utc || !actor.valid()) {
        request_play_preview_actor(doc, state,
            "Play-preview test actor must be a Creature blueprint");
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

    state.play_preview.pending_actor = actor;
    state.play_preview.area = area;
    state.play_preview.module_generation = state.backend.module_generation();
    state.play_preview.tab_id = state.workspace.active_tab_id();
    state.play_preview.placement_diagnostic.clear();
    reset_play_preview_input(state.play_preview);
    restore_play_preview_picker_shell(doc, state);
    focus_workspace_viewport(doc, state);
    apply_shell_layout(doc, state);
    system_interface.SetMouseCursor("cross");
    append_output(state, "info", "Click a walkable area surface to start play preview");
    return true;
}

float normalized_gamepad_axis(SDL_Gamepad* gamepad, SDL_GamepadAxis axis) noexcept
{
    if (!gamepad) return 0.0f;
    const Sint16 value = SDL_GetGamepadAxis(gamepad, axis);
    return value < 0
        ? static_cast<float>(value) / 32768.0f
        : static_cast<float>(value) / 32767.0f;
}

void open_play_preview_gamepad(PlayPreviewState& preview, SDL_JoystickID id)
{
    if (preview.gamepad || id == 0) return;
    preview.gamepad = SDL_OpenGamepad(id);
    if (!preview.gamepad) {
        LOG_F(WARNING, "Play preview: failed to open gamepad: {}", SDL_GetError());
    }
}

void close_play_preview_gamepad(PlayPreviewState& preview) noexcept
{
    if (!preview.gamepad) return;
    SDL_CloseGamepad(preview.gamepad);
    preview.gamepad = nullptr;
}

nw::toolset::PreviewInputSample sample_play_preview_input(
    PlayPreviewState& preview, double frame_seconds)
{
    preview.mouse_sample_seconds += std::max(0.0, frame_seconds);
    auto sample = preview.pending_input;
    const bool* keys = SDL_GetKeyboardState(nullptr);
    glm::vec2 keyboard{
        static_cast<float>(keys[SDL_SCANCODE_E]) - static_cast<float>(keys[SDL_SCANCODE_Q]),
        static_cast<float>(keys[SDL_SCANCODE_W]) - static_cast<float>(keys[SDL_SCANCODE_S]),
    };
    sample.turn_axis = static_cast<float>(keys[SDL_SCANCODE_D])
        - static_cast<float>(keys[SDL_SCANCODE_A]);
    glm::vec2 gamepad_move{};
    glm::vec2 gamepad_look{};
    const glm::vec2 keyboard_look{
        static_cast<float>(keys[SDL_SCANCODE_RIGHT]) - static_cast<float>(keys[SDL_SCANCODE_LEFT]),
        static_cast<float>(keys[SDL_SCANCODE_DOWN]) - static_cast<float>(keys[SDL_SCANCODE_UP]),
    };
    float gamepad_zoom = 0.0f;
    if (preview.gamepad) {
        gamepad_move = nw::toolset::preview_radial_deadzone({
            normalized_gamepad_axis(preview.gamepad, SDL_GAMEPAD_AXIS_LEFTX),
            -normalized_gamepad_axis(preview.gamepad, SDL_GAMEPAD_AXIS_LEFTY),
        });
        gamepad_look = nw::toolset::preview_radial_deadzone({
            normalized_gamepad_axis(preview.gamepad, SDL_GAMEPAD_AXIS_RIGHTX),
            normalized_gamepad_axis(preview.gamepad, SDL_GAMEPAD_AXIS_RIGHTY),
        });
        gamepad_zoom = normalized_gamepad_axis(
                           preview.gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
            - normalized_gamepad_axis(
                preview.gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
        gamepad_zoom += static_cast<float>(SDL_GetGamepadButton(
                            preview.gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER))
            - static_cast<float>(SDL_GetGamepadButton(
                preview.gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
    }

    sample.move_axis = keyboard + gamepad_move;
    sample.move_axis = glm::clamp(
        sample.move_axis, glm::vec2{-1.0f}, glm::vec2{1.0f});
    sample.look_axis = keyboard_look + gamepad_look;
    constexpr float mouse_radians_per_pixel = 0.0035f;
    constexpr float look_radians_per_second = 2.5f;
    if (preview.mouse_sample_seconds > 0.0) {
        const float mouse_scale = mouse_radians_per_pixel
            / (look_radians_per_second
                * static_cast<float>(preview.mouse_sample_seconds));
        sample.look_axis += glm::vec2{
            preview.mouse_look_x * mouse_scale,
            preview.mouse_look_y * mouse_scale,
        };
    }
    sample.zoom_axis = gamepad_zoom;
    sample.zoom_delta = preview.wheel_zoom;
    return sample;
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

void sync_play_preview_viewport_overlay(Rml::ElementDocument* fps_doc,
    const std::optional<WorkspaceViewerViewportRequest>& viewer_viewport,
    const AppState& state)
{
    auto* overlay = find_el(fps_doc, "play_preview_viewport_overlay");
    if (!overlay) return;

    const bool visible = viewer_viewport && viewer_viewport->rect.valid()
        && (state.play_preview.session.active()
            || state.play_preview.placement_pending()
            || state.play_preview.selecting_actor);
    if (!visible) {
        overlay->SetProperty("display", "none");
        return;
    }

    constexpr int kOverlayMargin = 8;
    const auto& rect = viewer_viewport->rect;
    const bool navigation_debug
        = nw::toolset::toolset_preview_navigation_debug(
            state.play_preview.session)
              .enabled;
    const bool placement_failed = state.play_preview.placement_pending()
        && !state.play_preview.placement_diagnostic.empty();
    overlay->SetInnerRML(
        state.play_preview.selecting_actor
            ? "<div class=\"play_preview_viewport_title\">Area Preview — Choose Creature</div>"
              "<div class=\"play_preview_viewport_help\">Select a Creature blueprint in the left panel | F9 or Escape to cancel</div>"
            : state.play_preview.placement_pending()
            ? placement_failed
                ? fmt::format(
                      "<div class=\"play_preview_viewport_title\">Area Preview</div>"
                      "<div class=\"play_preview_viewport_error\">{}</div>"
                      "<div class=\"play_preview_viewport_help\">Click another walkable point | F9 or Escape to cancel</div>",
                      escape_html(state.play_preview.placement_diagnostic))
                : "<div class=\"play_preview_viewport_title\">Area Preview</div>"
                  "<div class=\"play_preview_viewport_help\">Click a walkable point to enter | F9 or Escape to cancel</div>"
            : navigation_debug
            ? "<div class=\"play_preview_viewport_title\">Area Preview</div>"
              "<div class=\"play_preview_viewport_help\">Nav: walkable green | blockers red | route cyan | F8 hide | F9/Escape return</div>"
            : "<div class=\"play_preview_viewport_title\">Area Preview</div>"
              "<div class=\"play_preview_viewport_help\">F9 or Escape to return | F8 navigation debug</div>");
    overlay->SetProperty("display", "block");
    overlay->SetProperty("width", placement_failed || state.play_preview.selecting_actor ? "500px" : "320px");
    overlay->SetProperty("height", placement_failed || state.play_preview.selecting_actor ? "66px" : "50px");
    overlay->SetProperty("left", std::to_string(rect.x + kOverlayMargin) + "px");
    overlay->SetProperty("top", std::to_string(rect.y + kOverlayMargin) + "px");
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
    context.play_preview_active = state.play_preview.session.active();
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

bool commit_object_details_sound_position(
    Rml::ElementDocument* doc, AppState& state, int32_t desired)
{
    if (!state.object_details_combobox_row
        || !state.object_details_combobox.select_key(desired)) {
        return false;
    }
    const auto row_index = *state.object_details_combobox_row;
    if (!active_object_details_matches_tab(state)
        || row_index >= state.object_details.rows.size()) {
        close_object_details_combobox(doc, state);
        return false;
    }
    const auto& row = state.object_details.rows[row_index];
    if (row.editor != nw::toolset::ObjectDetailsEditorKind::sound_position
        || row.edit_value < 0 || row.edit_value > 2
        || desired < 0 || desired > 2) {
        close_object_details_combobox(doc, state);
        return false;
    }
    if (desired == row.edit_value) {
        close_object_details_combobox(doc, state);
        return true;
    }

    const auto result = dispatch_command(state,
        "object.details.set_sound_position",
        {std::to_string(row_index), std::to_string(row.edit_value),
            std::to_string(desired)},
        nw::toolset::CommandSource::widget);
    append_command_result(state, result);
    close_object_details_combobox(doc, state);
    return result.ok();
}

// Object variable text inputs commit on Enter or blur. RmlUi emits Sound
// volume changes throughout a drag, so retain only the newest slider value and
// commit it when that input gesture ends.
class ObjectWorkbenchChangeListener final : public Rml::EventListener {
public:
    explicit ObjectWorkbenchChangeListener(AppState& state)
        : state_{state}
    {
    }

    void ProcessEvent(Rml::Event& event) override
    {
        if (event == Rml::EventId::Change
            && stage_sound_volume(event)) {
            return;
        }
        if (event == Rml::EventId::Blur
            && event.GetTargetElement()
            && event.GetTargetElement()->IsClassSet(
                "object_details_sound_volume")) {
            (void)commit_sound_volume();
            return;
        }
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

    bool commit_sound_volume()
    {
        if (!pending_sound_volume_) {
            return false;
        }
        const auto pending = *pending_sound_volume_;
        pending_sound_volume_.reset();
        if (pending.current == pending.desired) {
            return true;
        }
        const std::string row = std::to_string(pending.row);
        const std::string current = std::to_string(pending.current);
        const std::string desired = std::to_string(pending.desired);
        const auto result = dispatch_command(state_,
            "object.details.set_integer",
            {row, current, desired},
            nw::toolset::CommandSource::widget);
        append_command_result(state_, result);
        return result.ok();
    }

private:
    struct PendingSoundVolume {
        uint32_t row = 0;
        int32_t current = 0;
        int32_t desired = 0;
    };

    bool stage_sound_volume(Rml::Event& event)
    {
        auto* target = event.GetTargetElement();
        if (!target
            || !target->IsClassSet("object_details_sound_volume")) {
            return false;
        }
        auto* input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(target);
        if (!input) {
            return true;
        }
        const float event_value = event.GetParameter<float>("value", -1.0f);
        const int32_t editor_value = static_cast<int32_t>(std::lround(event_value));
        const auto stored_value = std::abs(
                                      event_value - static_cast<float>(editor_value))
                < 0.001f
            ? nw::toolset::sound_volume_storage_value(editor_value)
            : std::nullopt;
        const auto row = parse_decimal_int32(input->GetAttribute<Rml::String>(
            "data-row", ""));
        const auto current = parse_decimal_int32(
            input->GetAttribute<Rml::String>("data-current", ""));
        if (!stored_value || !row || *row < 0 || !current
            || !active_object_details_matches_tab(state_)
            || static_cast<size_t>(*row) >= state_.object_details.rows.size()) {
            return true;
        }
        const auto& details_row = state_.object_details.rows[static_cast<size_t>(*row)];
        if (details_row.editor
                != nw::toolset::ObjectDetailsEditorKind::sound_volume
            || details_row.edit_value != *current) {
            return true;
        }
        pending_sound_volume_ = PendingSoundVolume{
            .row = static_cast<uint32_t>(*row),
            .current = *current,
            .desired = *stored_value,
        };
        if (auto* value = input->GetNextSibling(); value
            && value->IsClassSet("object_details_sound_volume_value")) {
            value->SetInnerRML(std::to_string(editor_value));
        }
        return true;
    }

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
    std::optional<PendingSoundVolume> pending_sound_volume_;
    bool suppress_blur_commit_ = false;
};

bool commit_active_appearance_selection(AppState& state, int32_t value)
{
    const std::string selected = std::to_string(value);
    nw::toolset::CommandResult result;
    if (state.appearance_editor_field == AppearanceEditorField::appearance) {
        if (state.appearance_object.type == nw::ObjectType::door) {
            result = dispatch_command(state,
                "object.door.set_appearance",
                {std::string_view{"0"}, std::string_view{selected}},
                nw::toolset::CommandSource::widget);
        } else {
            result = dispatch_command(state,
                "object.set_appearance",
                {std::string_view{selected}},
                nw::toolset::CommandSource::widget);
        }
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

bool cycle_active_appearance(AppState& state, int direction)
{
    if (direction == 0 || !active_appearances_match_tab(state)
        || (state.appearance_object.type != nw::ObjectType::placeable
            && state.appearance_object.type != nw::ObjectType::door)) {
        return false;
    }

    const auto& catalog = active_appearance_catalog(state);
    if (catalog.status != nw::toolset::AppearanceCatalogStatus::ready
        || catalog.rows.empty()) {
        return false;
    }
    const auto current = appearance_editor_value(
        state, AppearanceEditorField::appearance);
    const auto& rows = catalog.rows;
    auto selected = current
        ? std::ranges::find(rows, *current,
              &nw::toolset::AppearanceCatalogRow::id)
        : rows.end();
    size_t next = direction < 0 ? rows.size() - 1 : 0;
    if (selected != rows.end()) {
        const size_t index = static_cast<size_t>(selected - rows.begin());
        next = direction < 0
            ? (index == 0 ? rows.size() - 1 : index - 1)
            : (index + 1 == rows.size() ? 0 : index + 1);
    }
    return commit_active_appearance_selection(state, rows[next].id);
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
    switch (object.type) {
    case nw::ObjectType::creature:
    case nw::ObjectType::door:
    case nw::ObjectType::encounter:
    case nw::ObjectType::item:
    case nw::ObjectType::placeable:
    case nw::ObjectType::sound:
    case nw::ObjectType::store:
    case nw::ObjectType::trigger:
    case nw::ObjectType::waypoint:
        return true;
    default:
        return false;
    }
}

bool snap_area_door_preview(
    nw::ObjectHandle area,
    nw::ObjectHandle door,
    glm::vec3 requested_position,
    std::unique_ptr<nw::toolset::AreaDoorHookSnapshot>& hooks,
    int32_t& hook_type,
    nw::ObjectSpatialState& preview,
    std::string& diagnostic)
{
    constexpr int32_t k_unresolved_hook_type = -2;
    constexpr int32_t k_invalid_hook_type = -3;
    if (door.type != nw::ObjectType::door) {
        return true;
    }
    if (hook_type == k_unresolved_hook_type) {
        hook_type = nw::toolset::area_door_hook_type(door)
                        .value_or(k_invalid_hook_type);
    }
    if (hook_type == k_invalid_hook_type) {
        diagnostic = "Door hook policy is invalid or unavailable";
        return false;
    }
    if (!hooks) {
        const auto* live_area = nw::kernel::objects().get<nw::Area>(area);
        if (!live_area) {
            diagnostic = "Active area is invalid or stale";
            return false;
        }
        try {
            hooks = std::make_unique<nw::toolset::AreaDoorHookSnapshot>();
        } catch (const std::bad_alloc&) {
            diagnostic = "Door-hook snapshot allocation failed";
            return false;
        }
        if (!nw::toolset::build_area_door_hooks(*live_area, *hooks, diagnostic)) {
            hooks.reset();
            return false;
        }
    }
    const auto hook = nw::toolset::nearest_area_door_hook(
        *hooks, requested_position, hook_type, door);
    if (!hook) {
        diagnostic = "No compatible unoccupied door hook is available";
        return false;
    }
    preview.position = hook->position;
    preview.orientation = hook->orientation;
    diagnostic.clear();
    return true;
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

bool validate_placement_preview(ClientRenderer& renderer,
    std::unique_ptr<nw::toolset::AreaPlacementNavigation>& navigation,
    nw::ObjectHandle area, const nw::ObjectSpatialState& spatial,
    std::string& diagnostic)
{
    const std::array rows{spatial};
    nw::toolset::AreaPlacementNavigation uncached;
    if (spatial.owner.type == nw::ObjectType::creature && !navigation) {
        try {
            navigation = std::make_unique<nw::toolset::AreaPlacementNavigation>();
        } catch (const std::bad_alloc&) {
            diagnostic = "Creature placement navigation allocation failed";
            return false;
        }
    }
    auto& snapshot = navigation ? *navigation : uncached;
    const auto result = nw::toolset::validate_area_placements(snapshot, area, rows);
    diagnostic = result.diagnostic;
    if (navigation) {
        const bool enabled = result.status != nw::nav::NavStatus::rejected
            && nw::toolset::collect_placement_navigation_debug(snapshot);
        if (!renderer.update_toolset_preview_navigation_debug({
                .triangles = snapshot.debug_triangles,
                .revision = snapshot.revision,
                .enabled = enabled,
            })) {
            LOG_F(WARNING, "Creature placement navigation overlay update failed");
        }
    }
    return result.ok();
}

bool project_area_navigation_point(
    ClientRenderer& renderer,
    std::unique_ptr<nw::toolset::AreaPlacementNavigation>& navigation,
    nw::ObjectHandle area,
    Rml::Vector2f point,
    ClientViewportRect viewport,
    glm::vec3& output,
    std::string& diagnostic)
{
    const auto ray = renderer.viewer_viewport_ray(
        point.x, point.y, viewport);
    if (!ray) {
        diagnostic = "Navigation ray could not be constructed";
        return false;
    }
    if (!navigation) {
        try {
            navigation
                = std::make_unique<nw::toolset::AreaPlacementNavigation>();
        } catch (const std::bad_alloc&) {
            diagnostic = "Area navigation allocation failed";
            return false;
        }
    }
    const std::array inputs{nw::nav::NavRayProjectionInput{
        .origin = ray->origin,
        .displacement = ray->displacement,
    }};
    std::array<nw::nav::NavRayProjectionResult, 1> projected{};
    const auto stats = nw::toolset::project_area_navigation_rays(
        *navigation, area, inputs, projected, diagnostic);
    if (stats.output_count != 1
        || projected[0].status != nw::nav::NavStatus::ok) {
        return false;
    }
    output = projected[0].position;
    return true;
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

    const uint32_t spawn_index
        = renderer.active_viewer_area_debug_subindex(object);
    const auto* geometry = object.type == nw::ObjectType::encounter
        ? nw::kernel::objects().components().find_geometry(object)
        : nullptr;
    const bool encounter_spawn = geometry
        && spawn_index < geometry->spawn_points.size();
    const nw::ObjectSpawnPoint spawn = encounter_spawn
        ? geometry->spawn_points[spawn_index]
        : nw::ObjectSpawnPoint{};
    state.area_object_drag = {
        .area = renderer.area_viewer_object(),
        .before = *spatial,
        .preview = *spatial,
        .grab_offset = spatial->position - *surface_point,
        .pointer = {viewport.rect, {point.x, point.y}},
        .active = true,
        .valid = true,
        .encounter_spawn = encounter_spawn,
        .encounter_spawn_index = spawn_index,
        .spawn_before = spawn,
        .spawn_preview = spawn,
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
    if (!state.area_object_drag.active) {
        return false;
    }
    if (viewport.kind != WorkspaceViewerViewportKind::area
        || renderer.area_viewer_object() != state.area_object_drag.area) {
        cancel_area_object_drag(renderer, state);
        return false;
    }
    const auto pointer_status = update_viewport_pointer_drag(
        state.area_object_drag.pointer, {point.x, point.y}, viewport.rect);
    if (pointer_status == ClientViewportPointerDragStatus::cancelled) {
        cancel_area_object_drag(renderer, state);
        return false;
    }
    if (pointer_status == ClientViewportPointerDragStatus::pending) return false;
    auto& drag = state.area_object_drag;
    if (drag.encounter_spawn) {
        glm::vec3 position{0.0f};
        drag.valid = project_area_navigation_point(renderer,
            drag.navigation, drag.area, point, viewport.rect,
            position, drag.diagnostic);
        if (!drag.valid) return false;
        drag.spawn_preview.position = position;
        drag.moved = drag.spawn_preview != drag.spawn_before;
        renderer.update_viewer_area_region_preview(
            {}, drag.spawn_preview.position, true);
        return true;
    }

    const auto surface_point = renderer.viewer_area_surface_point(
        point.x, point.y, viewport.rect);
    if (!surface_point) {
        state.area_object_drag.valid = false;
        state.area_object_drag.diagnostic = "Placement ray did not hit an area surface";
        return false;
    }

    const glm::vec3 position = *surface_point + drag.grab_offset;
    drag.preview.position = position;
    const bool snapped = snap_area_door_preview(
        drag.area,
        drag.before.owner,
        position,
        drag.door_hooks,
        drag.door_hook_type,
        drag.preview,
        drag.diagnostic);
    drag.moved = drag.preview.position != drag.before.position
        || drag.preview.orientation != drag.before.orientation;
    drag.valid = snapped && validate_placement_preview(renderer, drag.navigation, drag.area, drag.preview, drag.diagnostic);
    if (snapped) {
        renderer.preview_viewer_area_object_spatial(drag.preview);
    }
    return true;
}

void cancel_area_object_drag(ClientRenderer& renderer, AppState& state)
{
    if (!state.area_object_drag.active) {
        return;
    }
    if (state.area_object_drag.navigation) {
        renderer.update_toolset_preview_navigation_debug({});
    }
    if (state.area_object_drag.encounter_spawn) {
        renderer.update_viewer_area_region_preview(
            {}, std::nullopt, false);
    }
    if (state.area_object_drag.pointer.dragging) {
        renderer.sync_viewer_area_object_spatial(state.area_object_drag.before.owner);
    }
    state.area_object_drag = {};
}

void commit_area_object_drag(ClientRenderer& renderer, AppState& state)
{
    if (!state.area_object_drag.active) {
        return;
    }

    const auto drag = std::move(state.area_object_drag);
    state.area_object_drag = {};
    if (drag.navigation) renderer.update_toolset_preview_navigation_debug({});
    if (drag.encounter_spawn) {
        renderer.update_viewer_area_region_preview(
            {}, std::nullopt, false);
    }
    if (!drag.pointer.dragging) return;
    if (!drag.moved || state.smalls.active_object() != drag.before.owner) {
        renderer.sync_viewer_area_object_spatial(drag.before.owner);
        return;
    }
    if (!drag.valid) {
        if (!drag.encounter_spawn) {
            renderer.sync_viewer_area_object_spatial(drag.before.owner);
        }
        append_output(state, "warn", drag.diagnostic);
        return;
    }

    if (drag.encounter_spawn) {
        const auto* geometry
            = nw::kernel::objects().components().find_geometry(
                drag.before.owner);
        if (!geometry
            || drag.encounter_spawn_index
                >= geometry->spawn_points.size()) {
            append_output(state, "warn",
                "Encounter spawn point changed during the drag");
            return;
        }
        std::vector<nw::ObjectSpawnPoint> before{
            geometry->spawn_points.begin(),
            geometry->spawn_points.end(),
        };
        auto after = before;
        after[drag.encounter_spawn_index] = drag.spawn_preview;
        const auto result = state.backend.replace_encounter_spawn_points(
            {
                .area = drag.area,
                .encounter = drag.before.owner,
                .before = std::move(before),
                .after = std::move(after),
            },
            command_context(state,
                nw::toolset::CommandSource::renderer));
        append_command_result(state, result);
        return;
    }

    const auto result = state.backend.transform_area_object(
        {
            .object = drag.before.owner,
            .before = {
                .position = drag.before.position,
                .orientation = drag.before.orientation,
                .scale = drag.before.scale,
            },
            .after = {
                .position = drag.preview.position,
                .orientation = drag.preview.orientation,
                .scale = drag.preview.scale,
            },
            .area = drag.area,
        },
        command_context(state, nw::toolset::CommandSource::renderer));
    sync_area_object_after_command(renderer, state, result);
}

bool add_encounter_spawn_point(
    ClientRenderer& renderer,
    AppState& state,
    Rml::Vector2f point,
    const WorkspaceViewerViewportRequest& viewport)
{
    const auto encounter = renderer.active_viewer_object();
    const auto area = renderer.area_viewer_object();
    if (encounter.type != nw::ObjectType::encounter
        || area.type != nw::ObjectType::area) {
        return false;
    }
    std::unique_ptr<nw::toolset::AreaPlacementNavigation> navigation;
    glm::vec3 position{0.0f};
    std::string diagnostic;
    if (!project_area_navigation_point(renderer, navigation, area,
            point, viewport.rect, position, diagnostic)) {
        append_output(state, "warn", diagnostic);
        return true;
    }
    const auto* geometry
        = nw::kernel::objects().components().find_geometry(encounter);
    std::vector<nw::ObjectSpawnPoint> before;
    if (geometry) {
        before.assign(
            geometry->spawn_points.begin(),
            geometry->spawn_points.end());
    }
    auto after = before;
    after.push_back({
        .position = position,
        .orientation = 0.0f,
    });
    const auto result = state.backend.replace_encounter_spawn_points(
        {
            .area = area,
            .encounter = encounter,
            .before = std::move(before),
            .after = std::move(after),
        },
        command_context(state, nw::toolset::CommandSource::renderer));
    append_command_result(state, result);
    return true;
}

bool delete_selected_encounter_spawn_point(
    ClientRenderer& renderer, AppState& state)
{
    const auto encounter = renderer.active_viewer_object();
    const auto area = renderer.area_viewer_object();
    const uint32_t spawn_index
        = renderer.active_viewer_area_debug_subindex(encounter);
    const auto* geometry
        = nw::kernel::objects().components().find_geometry(encounter);
    if (encounter.type != nw::ObjectType::encounter
        || area.type != nw::ObjectType::area
        || !geometry || spawn_index >= geometry->spawn_points.size()) {
        return false;
    }
    std::vector<nw::ObjectSpawnPoint> before{
        geometry->spawn_points.begin(),
        geometry->spawn_points.end(),
    };
    auto after = before;
    after.erase(after.begin() + static_cast<ptrdiff_t>(spawn_index));
    const auto result = state.backend.replace_encounter_spawn_points(
        {
            .area = area,
            .encounter = encounter,
            .before = std::move(before),
            .after = std::move(after),
        },
        command_context(state, nw::toolset::CommandSource::shortcut));
    append_command_result(state, result);
    return true;
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
        && !state.command_form && !state.module_dialog_open
        && !state.backend.blueprint_operation_active()
        && !state.backend.blueprint_publication_pending()
        && !state.shell.command_palette_visible
        && !state.play_preview.session.active();
}

bool area_tile_stroke_context_valid(const AppState& state) noexcept
{
    return area_tile_editor_action_allowed(state)
        && state.area_tile_editor.stroke.active
        && active_workspace_area(state) == state.area_tile_editor.stroke.area;
}

std::optional<nw::toolset::AreaTileBrush> selected_area_tile_brush(
    const AppState& state, uint8_t pointer_button) noexcept
{
    const auto& editor = state.area_tile_editor;
    if (editor.selected_row < 0
        || static_cast<size_t>(editor.selected_row)
            >= editor.palette.rows.size()
        || editor.palette.rows[static_cast<size_t>(editor.selected_row)].kind
            != nw::toolset::AreaTilePaletteRowKind::action) {
        return std::nullopt;
    }
    auto brush
        = editor.palette.rows[static_cast<size_t>(editor.selected_row)].brush;
    if (brush.kind == nw::toolset::AreaTileBrushKind::group) {
        brush.orientation = editor.group_orientation;
    }
    if (pointer_button == SDL_BUTTON_LEFT) {
        return brush;
    }
    if (pointer_button == SDL_BUTTON_RIGHT
        && brush.kind == nw::toolset::AreaTileBrushKind::raise) {
        brush.kind = nw::toolset::AreaTileBrushKind::lower;
        return brush;
    }
    return std::nullopt;
}

bool area_tile_height_brush(
    nw::toolset::AreaTileBrush brush) noexcept
{
    return brush.kind == nw::toolset::AreaTileBrushKind::raise
        || brush.kind == nw::toolset::AreaTileBrushKind::lower;
}

nw::toolset::ObjectEditApplyResult build_area_tile_stroke_edits(
    nw::ObjectHandle area,
    std::span<const uint32_t> tile_indices,
    std::span<const uint32_t> corner_indices,
    nw::toolset::AreaTileBrush brush,
    uint64_t seed,
    nw::toolset::AreaTileEditBatch& output)
{
    if (area_tile_height_brush(brush)) {
        const int32_t delta
            = brush.kind == nw::toolset::AreaTileBrushKind::raise ? 1 : -1;
        return nw::toolset::build_area_tile_height_brush_edits(
            area, corner_indices, delta, seed, output);
    }
    return nw::toolset::build_area_tile_brush_edits(
        area, tile_indices, brush, seed, output);
}

bool preview_area_tile_stroke(ClientRenderer& renderer,
    AreaTileEditorState& editor,
    nw::ObjectHandle area,
    std::span<const uint32_t> tile_indices,
    std::span<const uint32_t> corner_indices,
    nw::toolset::AreaTileBrush brush)
{
    nw::toolset::AreaTileEditBatch preview;
    const auto built = build_area_tile_stroke_edits(area,
        tile_indices, corner_indices, brush,
        editor.next_random_seed, preview);
    editor.feedback = built.ok() ? std::string{} : built.diagnostic;
    const bool erasing = brush.kind == nw::toolset::AreaTileBrushKind::eraser;
    const bool erase_empty = erasing
        && built.status == nw::toolset::ObjectEditStatus::empty;
    if (erase_empty) {
        editor.feedback.clear();
    }
    try {
        editor.preview_rows.clear();
        std::vector<uint32_t> erase_cells;
        const auto erase_targets = erasing
            ? nw::toolset::resolve_area_tile_erase_cells(area, tile_indices, erase_cells)
            : nw::toolset::ObjectEditApplyResult{};
        if (erasing && !erase_targets.ok()) {
            editor.feedback = erase_targets.diagnostic;
            (void)renderer.update_viewer_area_tile_preview(area, {}, false);
            return false;
        }
        if (erasing && erase_targets.ok()) {
            const auto* live_area = nw::kernel::objects().get<nw::Area>(area);
            editor.preview_rows.reserve(erase_cells.size());
            for (const uint32_t tile_index : erase_cells) {
                const auto& tile = live_area->tiles[tile_index];
                editor.preview_rows.push_back({
                    .tile_index = tile_index,
                    .tile_id = tile.id,
                    .height = tile.height,
                    .orientation = tile.orientation,
                });
            }
        } else if (built.ok()) {
            editor.preview_rows.reserve(preview.rows.size());
            for (const auto& row : preview.rows) {
                editor.preview_rows.push_back({
                    .tile_index = row.tile_index,
                    .tile_id = row.after.id,
                    .height = row.after.height,
                    .orientation = row.after.orientation,
                });
            }
        } else {
            editor.preview_rows.reserve(tile_indices.size());
            for (const uint32_t tile_index : tile_indices) {
                editor.preview_rows.push_back({
                    .tile_index = tile_index,
                });
            }
        }
    } catch (const std::bad_alloc&) {
        editor.feedback = "Tile preview allocation failed";
        (void)renderer.update_viewer_area_tile_preview(area, {}, false);
        return false;
    } catch (const std::length_error&) {
        editor.feedback = "Tile preview exceeds container capacity";
        (void)renderer.update_viewer_area_tile_preview(area, {}, false);
        return false;
    }
    (void)renderer.update_viewer_area_tile_preview(
        area, editor.preview_rows, built.ok() || erase_empty, !erasing);
    return built.ok() || erase_empty;
}

bool append_area_tile_height_preview_cells(
    AreaTileStrokeState& stroke,
    std::span<const uint32_t> corner_indices) noexcept
{
    for (const uint32_t corner_index : corner_indices) {
        const auto cells = nw::toolset::resolve_area_tile_corner_cell(
            stroke.width, stroke.height, corner_index);
        for (uint8_t index = 0; index < cells.count; ++index) {
            const uint32_t tile_index = cells.tile_indices[index];
            if (tile_index >= stroke.previewed_tiles.size()
                || stroke.previewed_tiles[tile_index] != 0) {
                continue;
            }
            try {
                stroke.tile_indices.push_back(tile_index);
            } catch (const std::bad_alloc&) {
                return false;
            } catch (const std::length_error&) {
                return false;
            }
            stroke.previewed_tiles[tile_index] = 1;
        }
    }
    return true;
}

void cancel_area_object_placement(ClientRenderer& renderer, AppState& state);
void cancel_area_tile_stroke(ClientRenderer& renderer, AppState& state);
bool begin_area_tile_stroke(ClientRenderer& renderer,
    AppState& state,
    Rml::Vector2f point,
    const WorkspaceViewerViewportRequest& viewport,
    uint8_t pointer_button);
bool cycle_selected_area_tile_variation(
    ClientRenderer& renderer, AppState& state);

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

nw::toolset::AreaTileCellPick pick_area_tile_cell(
    ClientRenderer& renderer,
    nw::ObjectHandle area_handle,
    Rml::Vector2f point,
    ClientViewportRect viewport,
    bool use_rendered_geometry)
{
    const auto* area = nw::kernel::objects().get<nw::Area>(area_handle);
    if (!area || area->width <= 0 || area->height <= 0) {
        return {};
    }

    if (use_rendered_geometry) {
        const auto hit = renderer.viewer_area_tile_hit(
            point.x, point.y, viewport);
        if (!hit) {
            const auto ray = renderer.viewer_viewport_ray(
                point.x, point.y, viewport);
            return ray
                ? nw::toolset::pick_area_tile_cell(*area,
                      {
                          .origin = ray->origin,
                          .direction = ray->displacement,
                      })
                : nw::toolset::AreaTileCellPick{};
        }
        if (hit->tile_x >= 0 && hit->tile_x < area->width
            && hit->tile_y >= 0 && hit->tile_y < area->height) {
            return {
                .position = hit->position,
                .distance = hit->distance,
                .tile_index = static_cast<uint32_t>(hit->tile_y * area->width
                    + hit->tile_x),
                .status = nw::toolset::AreaTileCellPickStatus::hit,
            };
        }
        return {};
    }

    const auto ray = renderer.viewer_viewport_ray(
        point.x, point.y, viewport);
    return ray
        ? nw::toolset::pick_area_tile_cell(*area,
              {
                  .origin = ray->origin,
                  .direction = ray->displacement,
              })
        : nw::toolset::AreaTileCellPick{};
}

void clear_area_tile_selection(ClientRenderer& renderer, AppState& state)
{
    auto& editor = state.area_tile_editor;
    editor.selection = {};
    if (editor.stroke.active) {
        return;
    }
    editor.preview_rows.clear();
    editor.cursor_target_index = UINT32_MAX;
    editor.cursor_modifier = nw::toolset::AreaTilePointerModifier::none;
    editor.cursor_update_pending = false;
    (void)renderer.update_viewer_area_tile_preview(
        active_workspace_area(state), {});
}

bool update_area_tile_outlines(ClientRenderer& renderer,
    AppState& state,
    const nw::toolset::AreaTileSelection& selection)
{
    auto& editor = state.area_tile_editor;
    const auto* area = nw::kernel::objects().get<nw::Area>(selection.area);
    if (!selection.active() || selection.area != active_workspace_area(state)
        || !area || !area->tileset
        || std::ranges::any_of(selection.tile_indices,
            [area](uint32_t tile_index) {
                return tile_index >= area->tiles.size();
            })) {
        editor.preview_rows.clear();
        (void)renderer.update_viewer_area_tile_preview(
            active_workspace_area(state), {});
        return false;
    }

    try {
        editor.preview_rows.clear();
        editor.preview_rows.reserve(selection.tile_indices.size());
        for (const uint32_t tile_index : selection.tile_indices) {
            const auto& tile = area->tiles[tile_index];
            editor.preview_rows.push_back({
                .tile_index = tile_index,
                .tile_id = tile.id,
                .height = tile.height,
                .orientation = tile.orientation,
            });
        }
    } catch (const std::bad_alloc&) {
        editor.feedback = "Tile selection highlight allocation failed";
        editor.preview_rows.clear();
        (void)renderer.update_viewer_area_tile_preview(
            selection.area, {});
        return false;
    } catch (const std::length_error&) {
        editor.feedback = "Tile selection highlight exceeds container capacity";
        editor.preview_rows.clear();
        (void)renderer.update_viewer_area_tile_preview(
            selection.area, {});
        return false;
    }

    editor.cursor_target_index = UINT32_MAX;
    editor.cursor_update_pending = false;
    if (!renderer.update_viewer_area_tile_preview(
            selection.area, editor.preview_rows, true, false)) {
        editor.feedback = "Tile selection highlight is unavailable";
        editor.preview_rows.clear();
        (void)renderer.update_viewer_area_tile_preview(
            selection.area, {});
        return false;
    }
    return true;
}

bool update_area_tile_selection_preview(
    ClientRenderer& renderer, AppState& state)
{
    if (!update_area_tile_outlines(
            renderer, state, state.area_tile_editor.selection)) {
        clear_area_tile_selection(renderer, state);
        return false;
    }
    return true;
}

bool set_area_tile_selection(ClientRenderer& renderer,
    AppState& state,
    nw::ObjectHandle area,
    uint32_t tile_index)
{
    cancel_area_tile_stroke(renderer, state);
    auto& editor = state.area_tile_editor;
    editor.selection = {};

    nw::toolset::AreaTileSelection selection;
    const auto built = nw::toolset::build_area_tile_selection(
        area, tile_index, selection);
    if (!built.ok()) {
        editor.feedback = built.diagnostic;
        append_output(state,
            built.status == nw::toolset::ObjectEditStatus::failed
                ? "error"
                : "warn",
            built.diagnostic);
        return false;
    }
    editor.selection = std::move(selection);
    editor.feedback.clear();
    (void)update_area_tile_selection_preview(renderer, state);
    return editor.selection.active();
}

bool select_area_tiles(ClientRenderer& renderer,
    AppState& state,
    Rml::Vector2f point,
    const WorkspaceViewerViewportRequest& viewport)
{
    if (!area_tile_editor_action_allowed(state)
        || viewport.kind != WorkspaceViewerViewportKind::area) {
        return false;
    }
    cancel_area_tile_stroke(renderer, state);
    const nw::ObjectHandle area = active_workspace_area(state);
    const auto pick = pick_area_tile_cell(
        renderer, area, point, viewport.rect, true);
    if (pick.status != nw::toolset::AreaTileCellPickStatus::hit) {
        state.area_tile_editor.feedback
            = "Tile selection target is unavailable";
        return true;
    }
    (void)set_area_tile_selection(
        renderer, state, area, pick.tile_index);
    return true;
}

bool cycle_area_tile_at_point(ClientRenderer& renderer,
    AppState& state,
    Rml::Vector2f point,
    const WorkspaceViewerViewportRequest& viewport)
{
    if (!area_tile_editor_action_allowed(state)
        || viewport.kind != WorkspaceViewerViewportKind::area) {
        return false;
    }
    const nw::ObjectHandle area = active_workspace_area(state);
    const auto pick = pick_area_tile_cell(
        renderer, area, point, viewport.rect, true);
    if (pick.status != nw::toolset::AreaTileCellPickStatus::hit) {
        state.area_tile_editor.feedback
            = "Tile variation target is unavailable";
        return true;
    }
    if (set_area_tile_selection(renderer, state, area, pick.tile_index)) {
        (void)cycle_selected_area_tile_variation(renderer, state);
    }
    return true;
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
    auto& editor = state.area_tile_editor;
    const bool was_active = editor.stroke.active;
    editor.stroke = {};
    if (was_active) {
        (void)SDL_CaptureMouse(false);
    }
    editor.preview_rows.clear();
    editor.cursor_target_index = UINT32_MAX;
    editor.cursor_modifier = nw::toolset::AreaTilePointerModifier::none;
    editor.cursor_update_pending = false;
    (void)renderer.update_viewer_area_tile_preview(
        active_workspace_area(state), {});
}

bool cancel_area_tile_action(ClientRenderer& renderer, AppState& state)
{
    auto& editor = state.area_tile_editor;
    const bool had_action = editor.stroke.active || editor.selection.active()
        || editor.selected_row >= 0 || !editor.preview_rows.empty()
        || editor.cursor_update_pending;
    if (!had_action) {
        return false;
    }
    cancel_area_tile_stroke(renderer, state);
    editor.selection = {};
    editor.selected_row = -1;
    editor.group_orientation = 0;
    editor.feedback.clear();
    editor.list.set_selected(-1);
    editor.rendered = false;
    return true;
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
    editor.feedback.clear();
    editor.query.clear();
    editor.preview_rows.clear();
    editor.selection = {};
    editor.cursor_target_index = UINT32_MAX;
    editor.cursor_modifier = nw::toolset::AreaTilePointerModifier::none;
    editor.cursor_update_pending = false;
    editor.selected_row = -1;
    editor.list.set_scroll_top(0);
    if (!rebuild_area_tile_palette(state, area_handle)) {
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
    state.active_object_tab_id.clear();
    clear_active_object_details(state);
    return true;
}

bool update_area_tile_cursor(ClientRenderer& renderer,
    AppState& state,
    Rml::Vector2f point,
    const WorkspaceViewerViewportRequest& viewport)
{
    auto& editor = state.area_tile_editor;
    if (state.area_workspace_surface != AreaWorkspaceSurface::tiles
        || viewport.kind != WorkspaceViewerViewportKind::area) {
        return false;
    }

    const nw::ObjectHandle area_handle = active_workspace_area(state);
    const auto* area = nw::kernel::objects().get<nw::Area>(area_handle);
    if (!area) {
        return false;
    }
    if (!editor.stroke.active
        && area_tile_pointer_modifier(SDL_GetModState())
            == nw::toolset::AreaTilePointerModifier::select) {
        const auto pick = pick_area_tile_cell(
            renderer, area_handle, point, viewport.rect, true);
        if (pick.status != nw::toolset::AreaTileCellPickStatus::hit) {
            editor.cursor_target_index = UINT32_MAX;
            editor.preview_rows.clear();
            (void)renderer.update_viewer_area_tile_preview(area_handle, {});
            return true;
        }
        if (editor.cursor_target_index == pick.tile_index
            && !editor.preview_rows.empty()) {
            return true;
        }
        nw::toolset::AreaTileSelection hovered;
        const auto built = nw::toolset::build_area_tile_selection(
            area_handle, pick.tile_index, hovered);
        if (!built.ok()) {
            editor.feedback = built.diagnostic;
            editor.cursor_target_index = UINT32_MAX;
            editor.preview_rows.clear();
            (void)renderer.update_viewer_area_tile_preview(area_handle, {});
            return true;
        }
        if (update_area_tile_outlines(renderer, state, hovered)) {
            editor.cursor_target_index = pick.tile_index;
        }
        return true;
    }
    if (!editor.stroke.active && editor.selection.active()) {
        return true;
    }
    const auto pick = pick_area_tile_cell(
        renderer, area_handle, point, viewport.rect,
        editor.preview_rows.empty());
    if (pick.status != nw::toolset::AreaTileCellPickStatus::hit) {
        editor.cursor_target_index = UINT32_MAX;
        if (editor.stroke.active) {
            editor.stroke.has_last_target = false;
        } else {
            editor.preview_rows.clear();
            (void)renderer.update_viewer_area_tile_preview(area_handle, {});
        }
        return true;
    }

    if (!editor.stroke.active) {
        const auto brush = selected_area_tile_brush(
            state, SDL_BUTTON_LEFT);
        if (!brush) {
            editor.cursor_target_index = UINT32_MAX;
            editor.preview_rows.clear();
            (void)renderer.update_viewer_area_tile_preview(area_handle, {});
            return true;
        }
        if (area_tile_height_brush(*brush)) {
            const uint32_t corner
                = nw::toolset::pick_area_tile_corner(*area, pick);
            if (corner == UINT32_MAX) {
                editor.feedback = "Terrain height target is unavailable";
                editor.cursor_target_index = UINT32_MAX;
                editor.preview_rows.clear();
                (void)renderer.update_viewer_area_tile_preview(
                    area_handle, {}, false);
                return true;
            }
            if (editor.cursor_target_index == corner
                && !editor.preview_rows.empty()) {
                return true;
            }
            editor.cursor_target_index = corner;
            const auto cells = nw::toolset::resolve_area_tile_corner_cell(
                area->width, area->height, corner);
            const std::array corners{corner};
            (void)preview_area_tile_stroke(renderer, editor, area_handle,
                std::span<const uint32_t>{cells.tile_indices.data(),
                    cells.count},
                corners, *brush);
        } else {
            if (editor.cursor_target_index == pick.tile_index
                && !editor.preview_rows.empty()) {
                return true;
            }
            editor.cursor_target_index = pick.tile_index;
            const std::array cells{pick.tile_index};
            (void)preview_area_tile_stroke(renderer, editor, area_handle,
                cells, {}, *brush);
        }
        return true;
    }

    auto& stroke = editor.stroke;
    if (stroke.brush.kind == nw::toolset::AreaTileBrushKind::group
        && !stroke.tile_indices.empty()) {
        return true;
    }
    const bool height_brush = area_tile_height_brush(stroke.brush);
    const uint32_t target_index = height_brush
        ? nw::toolset::pick_area_tile_corner(*area, pick)
        : pick.tile_index;
    if (target_index == UINT32_MAX) {
        editor.feedback = "Terrain stroke target is unavailable";
        stroke.has_last_target = false;
        return true;
    }
    const int32_t target_width
        = height_brush ? stroke.width + 1 : stroke.width;
    const int32_t target_height
        = height_brush ? stroke.height + 1 : stroke.height;
    const nw::toolset::AreaTileCellCoord target{
        .x = static_cast<int32_t>(
            target_index % static_cast<uint32_t>(target_width)),
        .y = static_cast<int32_t>(
            target_index / static_cast<uint32_t>(target_width)),
    };
    auto& target_indices
        = height_brush ? stroke.corner_indices : stroke.tile_indices;
    const size_t appended_begin = target_indices.size();
    const auto appended = nw::toolset::append_area_tile_grid_line(
        target_width,
        target_height,
        stroke.has_last_target ? stroke.last_target : target,
        target,
        stroke.visited,
        target_indices);
    if (appended.status != nw::toolset::AreaTileLineStatus::success) {
        editor.feedback = "Tile stroke buffer update failed";
        append_output(state, "error", "Tile stroke buffer update failed");
        cancel_area_tile_stroke(renderer, state);
        return true;
    }
    if (appended.appended_count == 0) {
        stroke.last_target = target;
        stroke.has_last_target = true;
        return true;
    }
    if (height_brush
        && !append_area_tile_height_preview_cells(stroke,
            std::span<const uint32_t>{target_indices}.subspan(
                appended_begin))) {
        editor.feedback = "Tile stroke preview allocation failed";
        append_output(state, "error", editor.feedback);
        cancel_area_tile_stroke(renderer, state);
        return true;
    }
    stroke.last_target = target;
    stroke.has_last_target = true;
    (void)preview_area_tile_stroke(renderer, editor, area_handle,
        stroke.tile_indices, stroke.corner_indices, stroke.brush);
    return true;
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
    if (!editor.stroke.active && modifier != editor.cursor_modifier) {
        editor.cursor_modifier = modifier;
        editor.cursor_target_index = UINT32_MAX;
        editor.preview_rows.clear();
        (void)renderer.update_viewer_area_tile_preview(
            active_workspace_area(state), {});
        // A modifier transition refreshes even with a stationary pointer.
        editor.cursor_update_pending = true;
        if (modifier != nw::toolset::AreaTilePointerModifier::select
            && editor.selection.active()) {
            (void)update_area_tile_selection_preview(renderer, state);
            return;
        }
    }
    if (!editor.cursor_update_pending) {
        return;
    }
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
    editor.cursor_target_index = UINT32_MAX;
    if (editor.stroke.active) {
        editor.stroke.has_last_target = false;
    } else if (modifier == nw::toolset::AreaTilePointerModifier::select
        || !editor.selection.active()) {
        editor.preview_rows.clear();
        (void)renderer.update_viewer_area_tile_preview(
            active_workspace_area(state), {});
    }
}

bool begin_area_tile_stroke(ClientRenderer& renderer,
    AppState& state,
    Rml::Vector2f point,
    const WorkspaceViewerViewportRequest& viewport,
    uint8_t pointer_button)
{
    auto& editor = state.area_tile_editor;
    editor.cursor_update_pending = false;
    const auto brush = selected_area_tile_brush(state, pointer_button);
    if (!brush) {
        editor.feedback = "Choose a terrain action";
        return false;
    }
    if (area_tile_editor_action_allowed(state)
        && !synchronize_area_viewport_structure(renderer, state, true)) {
        return true;
    }
    const nw::ObjectHandle area_handle = active_workspace_area(state);
    const auto* area = nw::kernel::objects().get<nw::Area>(area_handle);
    if (!area_tile_editor_action_allowed(state) || editor.stroke.active || !area
        || !area->tileset || editor.selected_row < 0
        || static_cast<size_t>(editor.selected_row)
            >= editor.palette.rows.size()) {
        editor.feedback = "Terrain editing is unavailable";
        return false;
    }

    const uint64_t tile_count = static_cast<uint64_t>(area->width)
        * static_cast<uint64_t>(area->height);
    if (area->width <= 0 || area->height <= 0
        || area->width == std::numeric_limits<int32_t>::max()
        || area->height == std::numeric_limits<int32_t>::max()
        || tile_count != area->tiles.size()
        || tile_count > std::numeric_limits<uint32_t>::max()) {
        editor.feedback = "Area tile grid is malformed";
        append_output(state, "error", "Area tile grid is malformed");
        return true;
    }
    const bool height_brush = area_tile_height_brush(*brush);
    const uint64_t target_count = height_brush
        ? static_cast<uint64_t>(area->width + 1)
            * static_cast<uint64_t>(area->height + 1)
        : tile_count;
    if (target_count > std::numeric_limits<uint32_t>::max()) {
        editor.feedback = "Area height grid exceeds the supported index range";
        append_output(state, "error", editor.feedback);
        return true;
    }
    try {
        const auto& palette_row
            = editor.palette.rows[static_cast<size_t>(editor.selected_row)];
        editor.stroke = {
            .area = area_handle,
            .tileset = area->tileset_resref,
            .brush = *brush,
            .label = palette_row.label,
            .mutation_epoch = nw::toolset::object_mutation_state().epoch,
            .resource_generation = nw::kernel::resman().generation(),
            .width = area->width,
            .height = area->height,
            .visited = std::vector<uint8_t>(
                static_cast<size_t>(target_count), 0),
            .previewed_tiles = height_brush
                ? std::vector<uint8_t>(static_cast<size_t>(tile_count), 0)
                : std::vector<uint8_t>{},
            .pointer_button = pointer_button,
            .active = true,
        };
        editor.stroke.tile_indices.reserve(
            static_cast<size_t>(tile_count));
        if (height_brush) {
            editor.stroke.corner_indices.reserve(
                static_cast<size_t>(target_count));
        }
        editor.feedback.clear();
        (void)SDL_CaptureMouse(true);
    } catch (const std::bad_alloc&) {
        editor.stroke = {};
        editor.feedback = "Tile stroke allocation failed";
        append_output(state, "error", "Tile stroke allocation failed");
        return true;
    } catch (const std::length_error&) {
        editor.stroke = {};
        editor.feedback = "Tile stroke exceeds container capacity";
        append_output(state, "error", "Tile stroke exceeds container capacity");
        return true;
    }
    (void)update_area_tile_cursor(renderer, state, point, viewport);
    if (editor.stroke.active
        && (height_brush ? editor.stroke.corner_indices.empty()
                         : editor.stroke.tile_indices.empty())) {
        cancel_area_tile_stroke(renderer, state);
    }
    return true;
}

void commit_area_tile_stroke(ClientRenderer& renderer, AppState& state)
{
    auto& editor = state.area_tile_editor;
    if (!editor.stroke.active) {
        return;
    }
    if (!area_tile_stroke_context_valid(state)
        || state.stale_area_viewport == editor.stroke.area) {
        cancel_area_tile_stroke(renderer, state);
        return;
    }
    auto stroke = std::move(editor.stroke);
    editor.stroke = {};
    (void)SDL_CaptureMouse(false);
    (void)renderer.update_viewer_area_tile_preview(stroke.area, {});
    editor.preview_rows.clear();
    editor.cursor_target_index = UINT32_MAX;
    editor.cursor_update_pending = false;
    const auto* area = nw::kernel::objects().get<nw::Area>(stroke.area);
    if (!area || active_workspace_area(state) != stroke.area
        || area->tileset_resref != stroke.tileset
        || area->width != stroke.width || area->height != stroke.height
        || nw::kernel::resman().generation() != stroke.resource_generation
        || nw::toolset::object_mutation_state().epoch
            != stroke.mutation_epoch) {
        editor.feedback = "Tile stroke was cancelled because its area changed";
        append_output(state, "warn", "Tile stroke was cancelled because its area changed");
        return;
    }
    nw::toolset::AreaTileEditBatch edit;
    const auto built = build_area_tile_stroke_edits(stroke.area,
        stroke.tile_indices, stroke.corner_indices, stroke.brush,
        editor.next_random_seed++, edit);
    if (!built.ok()) {
        if (stroke.brush.kind == nw::toolset::AreaTileBrushKind::eraser
            && built.status == nw::toolset::ObjectEditStatus::empty) {
            editor.feedback = "Nothing to erase here";
            return;
        }
        editor.feedback = built.diagnostic;
        (void)preview_area_tile_stroke(renderer, editor, stroke.area,
            stroke.tile_indices, stroke.corner_indices, stroke.brush);
        append_output(state,
            built.status == nw::toolset::ObjectEditStatus::failed
                ? "error"
                : "warn",
            built.diagnostic);
        return;
    }
    const size_t target_count = area_tile_height_brush(stroke.brush)
        ? stroke.corner_indices.size()
        : stroke.tile_indices.size();
    const auto result = state.backend.edit_area_tiles(
        std::move(edit),
        target_count == 1 ? stroke.label : stroke.label + " stroke",
        command_context(state, nw::toolset::CommandSource::renderer));
    editor.feedback = result.ok() ? std::string{} : result.message;
    append_command_result(state, result);
}

bool cycle_selected_area_tile_variation(
    ClientRenderer& renderer, AppState& state)
{
    auto& editor = state.area_tile_editor;
    if (!area_tile_editor_action_allowed(state)
        || !editor.selection.active()) {
        return false;
    }
    if (editor.selection.is_group()) {
        editor.feedback
            = "Placed groups do not expose interchangeable group variations";
        return true;
    }
    cancel_area_tile_stroke(renderer, state);
    const nw::ObjectHandle area = editor.selection.area;
    const std::array cells{editor.selection.source_tile_index};
    nw::toolset::AreaTileEditBatch edit;
    const auto built = nw::toolset::build_area_tile_variation_edits(
        area, cells, edit);
    if (!built.ok()) {
        editor.feedback = built.diagnostic;
        append_output(state,
            built.status == nw::toolset::ObjectEditStatus::failed
                ? "error"
                : "warn",
            built.diagnostic);
        (void)update_area_tile_selection_preview(renderer, state);
        return true;
    }
    const auto result = state.backend.edit_area_tiles(std::move(edit),
        "Cycle tile variation",
        command_context(state, nw::toolset::CommandSource::renderer));
    editor.feedback = result.ok() ? std::string{} : result.message;
    append_command_result(state, result);
    (void)update_area_tile_selection_preview(renderer, state);
    return true;
}

bool placement_blueprint_resource(const nw::Resource& resource) noexcept
{
    return resource.valid()
        && (resource.type == nw::ResourceType::utc
            || resource.type == nw::ResourceType::utd
            || resource.type == nw::ResourceType::ute
            || resource.type == nw::ResourceType::utp
            || resource.type == nw::ResourceType::uti
            || resource.type == nw::ResourceType::utm
            || resource.type == nw::ResourceType::uts
            || resource.type == nw::ResourceType::utt
            || resource.type == nw::ResourceType::utw);
}

bool region_blueprint_resource(const nw::Resource& resource) noexcept
{
    return resource.type == nw::ResourceType::ute
        || resource.type == nw::ResourceType::utt;
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
    if (region_blueprint_resource(placement.resource)) {
        renderer.update_viewer_area_region_preview(
            {}, std::nullopt, false);
    }
    if (placement.navigation) renderer.update_toolset_preview_navigation_debug({});
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

bool update_area_region_placement(
    ClientRenderer& renderer,
    AreaObjectPlacementState& placement,
    nw::ObjectHandle area,
    Rml::Vector2f point,
    ClientViewportRect viewport)
{
    glm::vec3 projected{0.0f};
    if (!project_area_navigation_point(renderer,
            placement.navigation, area, point, viewport,
            projected, placement.diagnostic)) {
        placement.region_hover.reset();
        placement.region_closing_valid = false;
        placement.phase = AreaObjectPlacementPhase::ghost_invalid;
        renderer.update_viewer_area_region_preview(
            placement.region_points, std::nullopt, false);
        return true;
    }

    const auto* live_area = nw::kernel::objects().get<nw::Area>(area);
    std::vector<glm::vec3> candidate = placement.region_points;
    candidate.push_back(projected);
    std::string path_diagnostic;
    const bool open_valid = live_area
        && nw::toolset::validate_area_region_path(
            live_area->width, live_area->height,
            candidate, false, path_diagnostic);
    std::string close_diagnostic;
    placement.region_closing_valid = open_valid && candidate.size() >= 3
        && nw::toolset::validate_area_region_path(
            live_area->width, live_area->height,
            candidate, true, close_diagnostic);
    placement.region_hover = projected;
    placement.area = area;
    placement.phase = open_valid
        ? AreaObjectPlacementPhase::ghost_valid
        : AreaObjectPlacementPhase::ghost_invalid;
    placement.diagnostic = open_valid
        ? close_diagnostic
        : path_diagnostic;
    if (!renderer.update_viewer_area_region_preview(
            placement.region_points,
            placement.region_hover,
            placement.region_closing_valid)) {
        placement.phase = AreaObjectPlacementPhase::ghost_invalid;
        placement.diagnostic = "Region preview construction failed";
    }
    return true;
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
        placement.phase = AreaObjectPlacementPhase::ghost_invalid;
        if (region_blueprint_resource(placement.resource)) {
            placement.region_hover.reset();
            placement.region_closing_valid = false;
            renderer.update_viewer_area_region_preview(
                placement.region_points, std::nullopt, false);
        }
        return true;
    }

    const auto area = renderer.area_viewer_object();
    if (area.type != nw::ObjectType::area || !nw::kernel::objects().valid(area)
        || state.workspace.active_tab_id() != placement.tab_id) {
        placement.phase = AreaObjectPlacementPhase::ghost_invalid;
        placement.region_hover.reset();
        placement.region_closing_valid = false;
        return true;
    }
    if (placement.area.type != nw::ObjectType::invalid && placement.area != area) {
        placement.phase = AreaObjectPlacementPhase::ghost_invalid;
        placement.region_hover.reset();
        placement.region_closing_valid = false;
        return true;
    }

    if (region_blueprint_resource(placement.resource)
        && placement.object.type == nw::ObjectType::invalid) {
        return update_area_region_placement(
            renderer, placement, area, point, viewport->rect);
    }

    const auto surface_point = renderer.viewer_area_surface_point(
        point.x, point.y, viewport->rect);
    if (!surface_point) {
        placement.phase = AreaObjectPlacementPhase::ghost_invalid;
        placement.diagnostic = "Placement ray did not hit an area surface";
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
    const bool snapped = snap_area_door_preview(
        area,
        placement.object,
        *surface_point,
        placement.door_hooks,
        placement.door_hook_type,
        placement.preview,
        placement.diagnostic);
    if (snapped && !renderer.preview_viewer_area_object_spatial(placement.preview)) {
        append_output(state, "error", "Area object placement preview update failed");
        cancel_area_object_placement(renderer, state);
        return true;
    }
    const bool admitted = snapped && validate_placement_preview(renderer, placement.navigation, area, placement.preview, placement.diagnostic);
    placement.phase = valid_position && snapped && admitted
        ? AreaObjectPlacementPhase::ghost_valid
        : AreaObjectPlacementPhase::ghost_invalid;
    return true;
}

bool accept_area_region_point(
    ClientRenderer& renderer,
    AppState& state,
    Rml::Vector2f point,
    const WorkspaceViewerViewportRequest& viewport)
{
    auto& placement = state.area_object_placement;
    if (!placement.region_drawing
        || !update_area_region_placement(
            renderer, placement, placement.area, point, viewport.rect)
        || placement.phase != AreaObjectPlacementPhase::ghost_valid
        || !placement.region_hover) {
        if (!placement.diagnostic.empty()) {
            append_output(state, "warn", placement.diagnostic);
        }
        return false;
    }
    placement.region_points.push_back(*placement.region_hover);
    placement.region_hover.reset();
    placement.region_closing_valid = false;
    placement.diagnostic.clear();
    renderer.update_viewer_area_region_preview(
        placement.region_points, std::nullopt, false);
    return true;
}

void commit_area_object_placement(
    ClientRenderer& renderer, AppState& state);

bool complete_area_region_placement(
    ClientRenderer& renderer,
    AppState& state,
    Rml::Vector2f point,
    const WorkspaceViewerViewportRequest& viewport)
{
    auto& placement = state.area_object_placement;
    if (!placement.region_drawing
        || !update_area_region_placement(
            renderer, placement, placement.area, point, viewport.rect)
        || !placement.region_hover) {
        return false;
    }

    std::vector<glm::vec3> world_points = placement.region_points;
    const glm::vec3 final_delta = world_points.empty()
        ? glm::vec3{1.0f}
        : world_points.back() - *placement.region_hover;
    if (world_points.empty()
        || glm::dot(final_delta, final_delta) > 1.0e-8f) {
        world_points.push_back(*placement.region_hover);
    }
    const auto* area = nw::kernel::objects().get<nw::Area>(placement.area);
    nw::toolset::AreaRegionGeometry geometry;
    if (!area || !nw::toolset::build_area_region_geometry(area->width, area->height, world_points, geometry, placement.diagnostic)) {
        append_output(state, "warn", placement.diagnostic);
        return false;
    }

    const std::array placement_rows{
        nw::toolset::AreaObjectBlueprintPlacement{
            .resource = placement.resource,
            .transform = {
                .position = geometry.root_position,
                .orientation = {1.0f, 0.0f, 0.0f},
                .scale = {1.0f, 1.0f, 1.0f},
            },
            .geometry_points = geometry.local_points,
        },
    };
    auto loaded = nw::toolset::load_area_object_blueprints(
        placement.area, placement_rows);
    if (!loaded.ok() || loaded.objects.size() != 1) {
        placement.diagnostic = loaded.diagnostic.empty()
            ? "Area region placement load failed"
            : std::move(loaded.diagnostic);
        append_output(state,
            loaded.status
                    == nw::toolset::AreaObjectBlueprintLoadStatus::failed
                ? "error"
                : "warn",
            placement.diagnostic);
        return false;
    }
    placement.object = loaded.objects.front();
    const auto* spatial = nw::kernel::objects().components().find_spatial(
        placement.object);
    if (!spatial) {
        nw::kernel::objects().destroy(placement.object);
        placement.object = nw::ObjectHandle{};
        append_output(state, "error",
            "Area region placement has no spatial state");
        return false;
    }
    placement.preview = *spatial;
    placement.phase = AreaObjectPlacementPhase::ghost_valid;
    renderer.update_viewer_area_region_preview({}, std::nullopt, false);
    const std::array objects{placement.object};
    if (!renderer.append_viewer_area_object_previews(
            objects, kAreaObjectPlacementOpacity)) {
        nw::kernel::objects().destroy(placement.object);
        placement.object = nw::ObjectHandle{};
        append_output(state, "error",
            "Area region preview construction failed");
        return false;
    }
    commit_area_object_placement(renderer, state);
    return true;
}

void commit_area_object_placement(ClientRenderer& renderer, AppState& state)
{
    if (!state.area_object_placement.active()) {
        return;
    }
    if (state.area_object_placement.phase != AreaObjectPlacementPhase::ghost_valid
        || !nw::kernel::objects().valid(state.area_object_placement.object)) {
        if (!state.area_object_placement.diagnostic.empty()) {
            append_output(state, "warn", state.area_object_placement.diagnostic);
        }
        cancel_area_object_placement(renderer, state);
        return;
    }

    const auto placement = std::move(state.area_object_placement);
    state.area_object_placement = {};
    if (placement.navigation) renderer.update_toolset_preview_navigation_debug({});
    auto& components = nw::kernel::objects().components();
    const auto* current_spatial = components.find_spatial(placement.object);
    const nw::ObjectSpatialState before = current_spatial
        ? *current_spatial
        : nw::ObjectSpatialState{};
    if (!current_spatial
        || !components.set_position(placement.object, placement.preview.position)
        || !components.set_orientation(
            placement.object, placement.preview.orientation)) {
        if (current_spatial) {
            components.set_position(placement.object, before.position);
            components.set_orientation(placement.object, before.orientation);
        }
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

bool project_blueprint_drag_resource(const nw::Resource& resource,
    const std::filesystem::path& source_path,
    nw::ResourceType::type type) noexcept
{
    return resource.valid()
        && resource.type == type
        && source_path.extension() == ".json";
}

void clear_project_blueprint_drop_visuals(Rml::ElementDocument* doc,
    const ProjectBlueprintDragState& drag)
{
    if (auto* target = find_el(doc, "creature_inventory_drop_target")) {
        target->SetProperty("display", "none");
        target->SetClass("valid", false);
        target->SetClass("invalid", false);
    }
    if (drag.target.kind == ProjectBlueprintDropTargetKind::equipment
        && static_cast<uint32_t>(drag.target.slot) < 18) {
        const std::string id = "creature_equipment_slot_"
            + std::to_string(static_cast<uint32_t>(drag.target.slot));
        if (auto* slot = find_el(doc, id.c_str())) {
            slot->SetClass("drop_valid", false);
            slot->SetClass("drop_invalid", false);
        }
    }
    if (auto* spawns = find_el(doc, "encounter_spawn_collection")) {
        spawns->SetClass("drop_valid", false);
        spawns->SetClass("drop_invalid", false);
    }
    if (auto* sounds = find_el(doc, "sound_resource_collection")) {
        sounds->SetClass("drop_valid", false);
        sounds->SetClass("drop_invalid", false);
    }
    for (int32_t category = 0; category < 5; ++category) {
        const std::string id = "store_inventory_drop_" + std::to_string(category);
        if (auto* target = find_el(doc, id.c_str())) {
            target->SetClass("drop_valid", false);
            target->SetClass("drop_invalid", false);
        }
    }
}

void set_project_blueprint_drop_target(Rml::ElementDocument* doc,
    ProjectBlueprintDragState& drag,
    ProjectBlueprintDropTarget target,
    bool valid)
{
    clear_project_blueprint_drop_visuals(doc, drag);
    drag.target = target;
    drag.phase = valid
        ? ProjectBlueprintDragPhase::target_valid
        : ProjectBlueprintDragPhase::target_invalid;

    if (target.kind == ProjectBlueprintDropTargetKind::inventory) {
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
    } else if (target.kind == ProjectBlueprintDropTargetKind::equipment
        && static_cast<uint32_t>(target.slot) < 18) {
        const std::string id = "creature_equipment_slot_"
            + std::to_string(static_cast<uint32_t>(target.slot));
        if (auto* slot = find_el(doc, id.c_str())) {
            slot->SetClass("drop_valid", valid);
            slot->SetClass("drop_invalid", !valid);
        }
    } else if (target.kind == ProjectBlueprintDropTargetKind::encounter_spawns) {
        if (auto* spawns = find_el(doc, "encounter_spawn_collection")) {
            spawns->SetClass("drop_valid", valid);
            spawns->SetClass("drop_invalid", !valid);
        }
    } else if (target.kind == ProjectBlueprintDropTargetKind::sound_resources) {
        if (auto* sounds = find_el(doc, "sound_resource_collection")) {
            sounds->SetClass("drop_valid", valid);
            sounds->SetClass("drop_invalid", !valid);
        }
    } else if (target.kind == ProjectBlueprintDropTargetKind::store_inventory
        && target.category >= 0 && target.category < 5) {
        const std::string id = "store_inventory_drop_"
            + std::to_string(target.category);
        if (auto* store_target = find_el(doc, id.c_str())) {
            store_target->SetClass("drop_valid", valid);
            store_target->SetClass("drop_invalid", !valid);
        }
    }
}

bool arm_project_blueprint_drag(AppState& state,
    const nw::Resource& resource,
    const std::filesystem::path& source_path,
    Rml::Vector2f point)
{
    if (project_blueprint_drag_resource(
            resource, source_path, nw::ResourceType::uti)
        && state.object_workbench_surface == ObjectWorkbenchSurface::store_inventory
        && active_object_matches_tab(state)
        && state.object_details.object.type == nw::ObjectType::store) {
        state.project_blueprint_drag = {
            .source_path = source_path,
            .resource = resource,
            .owner = state.object_details.object,
            .drag_start = point,
            .tab_id = state.workspace.active_tab_id(),
            .kind = ProjectBlueprintDragKind::item,
            .phase = ProjectBlueprintDragPhase::armed,
        };
        return true;
    }

    if (project_blueprint_drag_resource(
            resource, source_path, nw::ResourceType::uti)
        && state.object_workbench_surface == ObjectWorkbenchSurface::inventory
        && active_creature_inventory_matches_tab(state)) {
        state.project_blueprint_drag = {
            .source_path = source_path,
            .resource = resource,
            .owner = state.creature_inventory.object,
            .drag_start = point,
            .tab_id = state.workspace.active_tab_id(),
            .kind = ProjectBlueprintDragKind::item,
            .phase = ProjectBlueprintDragPhase::armed,
        };
        return true;
    }

    if (project_blueprint_drag_resource(
            resource, source_path, nw::ResourceType::utc)
        && state.object_workbench_surface == ObjectWorkbenchSurface::spawns
        && active_object_matches_tab(state)
        && state.object_details.object.type == nw::ObjectType::encounter) {
        state.project_blueprint_drag = {
            .source_path = source_path,
            .resource = resource,
            .owner = state.object_details.object,
            .drag_start = point,
            .tab_id = state.workspace.active_tab_id(),
            .kind = ProjectBlueprintDragKind::encounter_spawn,
            .phase = ProjectBlueprintDragPhase::armed,
        };
        return true;
    }

    if (resource.valid()
        && resource.type == nw::ResourceType::wav
        && state.object_workbench_surface == ObjectWorkbenchSurface::sounds
        && active_object_matches_tab(state)
        && state.object_details.object.type == nw::ObjectType::sound) {
        state.project_blueprint_drag = {
            .source_path = source_path,
            .resource = resource,
            .owner = state.object_details.object,
            .drag_start = point,
            .tab_id = state.workspace.active_tab_id(),
            .kind = ProjectBlueprintDragKind::sound_resource,
            .phase = ProjectBlueprintDragPhase::armed,
        };
        return true;
    }

    return false;
}

bool project_blueprint_drag_context_matches(const AppState& state)
{
    const auto& drag = state.project_blueprint_drag;
    if (!drag.active() || state.workspace.active_tab_id() != drag.tab_id) {
        return false;
    }
    switch (drag.kind) {
    case ProjectBlueprintDragKind::item:
        if (drag.owner.type == nw::ObjectType::store) {
            return state.object_workbench_surface
                == ObjectWorkbenchSurface::store_inventory
                && state.object_details.object == drag.owner
                && active_object_matches_tab(state);
        }
        return state.object_workbench_surface == ObjectWorkbenchSurface::inventory
            && state.creature_inventory.object == drag.owner
            && active_creature_inventory_matches_tab(state);
    case ProjectBlueprintDragKind::encounter_spawn:
        return state.object_workbench_surface == ObjectWorkbenchSurface::spawns
            && state.object_details.object == drag.owner
            && active_object_matches_tab(state);
    case ProjectBlueprintDragKind::sound_resource:
        return state.object_workbench_surface == ObjectWorkbenchSurface::sounds
            && state.object_details.object == drag.owner
            && active_object_matches_tab(state);
    default:
        return false;
    }
}

void cancel_project_blueprint_drag(Rml::ElementDocument* doc, AppState& state)
{
    if (!state.project_blueprint_drag.active()) {
        return;
    }
    clear_project_blueprint_drop_visuals(doc, state.project_blueprint_drag);
    const auto item = state.project_blueprint_drag.item;
    state.project_blueprint_drag = {};
    if (nw::kernel::objects().valid(item)) {
        nw::kernel::objects().destroy(item);
    }
}

bool materialize_project_blueprint_drag(AppState& state)
{
    auto& drag = state.project_blueprint_drag;
    if (drag.kind == ProjectBlueprintDragKind::sound_resource) {
        if (drag.sound_resource_edit) {
            return true;
        }
        if (drag.materialization_failed) {
            return false;
        }

        if (!drag.resource.valid()
            || drag.resource.type != nw::ResourceType::wav
            || drag.resource.resref.empty()) {
            drag.materialization_failed = true;
            append_output(state, "error",
                "Sound resource drag source or target list is invalid or full");
            return false;
        }

        const std::array additions{drag.resource.resref};
        drag.sound_resource_edit = nw::toolset::make_sound_resource_additions(
            nw::kernel::runtime(), drag.owner, additions);
        if (!drag.sound_resource_edit) {
            drag.materialization_failed = true;
            append_output(state, "error",
                "Sound resource drag source or target list is invalid or full");
            return false;
        }
        return true;
    }

    if (drag.kind == ProjectBlueprintDragKind::encounter_spawn) {
        if (drag.encounter_spawn_edit) {
            return true;
        }
        if (drag.materialization_failed) {
            return false;
        }

        std::ifstream input{drag.source_path};
        input >> std::ws;
        if (!input || input.peek() != '{') {
            drag.materialization_failed = true;
            append_output(state, "warn",
                "Encounter spawn drag requires an authored component/propset JSON Creature blueprint");
            return false;
        }

        auto* creature = nw::kernel::objects().load_file<nw::Creature>(
            drag.source_path);
        if (!creature) {
            drag.materialization_failed = true;
            append_output(state, "error",
                "Encounter spawn Creature blueprint could not be loaded");
            return false;
        }

        const std::array creature_handles{creature->handle()};
        auto rows = nw::toolset::make_encounter_spawn_records(
            nw::kernel::runtime(), creature_handles);
        nw::kernel::objects().destroy(creature->handle());
        auto before = nw::toolset::snapshot_encounter_spawns(
            nw::kernel::runtime(), drag.owner);
        if (!rows || rows->size() != 1 || !before || before->size() >= 1024) {
            drag.materialization_failed = true;
            append_output(state, "error",
                "Encounter spawn drag source or target list is invalid or full");
            return false;
        }

        auto after = *before;
        after.push_back(std::move(rows->front()));
        drag.encounter_spawn_edit = nw::toolset::EncounterSpawnEdit{
            .encounter = drag.owner,
            .before = std::move(*before),
            .after = std::move(after),
        };
        return true;
    }

    if (drag.kind != ProjectBlueprintDragKind::item) {
        return false;
    }
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

bool update_project_blueprint_drag(Rml::Context* context,
    Rml::ElementDocument* doc,
    AppState& state,
    Rml::Vector2f point)
{
    auto& drag = state.project_blueprint_drag;
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

    if (!project_blueprint_drag_context_matches(state)) {
        set_project_blueprint_drop_target(doc, drag, {}, false);
        return true;
    }

    if (drag.kind == ProjectBlueprintDragKind::encounter_spawn) {
        if (!materialize_project_blueprint_drag(state)) {
            set_project_blueprint_drop_target(doc, drag, {}, false);
            return true;
        }
        auto* hit = context ? context->GetElementAtPoint(point) : nullptr;
        const bool over_spawn_collection = find_ancestor_with_id(hit, "encounter_spawn_collection") != nullptr;
        const ProjectBlueprintDropTarget target{
            .kind = over_spawn_collection
                ? ProjectBlueprintDropTargetKind::encounter_spawns
                : ProjectBlueprintDropTargetKind::none,
        };
        if (target == drag.target
            && (drag.phase == ProjectBlueprintDragPhase::target_valid
                || drag.phase == ProjectBlueprintDragPhase::target_invalid)) {
            return true;
        }
        set_project_blueprint_drop_target(doc, drag, target, over_spawn_collection);
        return true;
    }

    if (drag.kind == ProjectBlueprintDragKind::sound_resource) {
        if (!materialize_project_blueprint_drag(state)) {
            set_project_blueprint_drop_target(doc, drag, {}, false);
            return true;
        }
        auto* hit = context ? context->GetElementAtPoint(point) : nullptr;
        const bool over_sound_collection = find_ancestor_with_id(hit, "sound_resource_collection") != nullptr;
        const ProjectBlueprintDropTarget target{
            .kind = over_sound_collection
                ? ProjectBlueprintDropTargetKind::sound_resources
                : ProjectBlueprintDropTargetKind::none,
        };
        if (target == drag.target
            && (drag.phase == ProjectBlueprintDragPhase::target_valid
                || drag.phase == ProjectBlueprintDragPhase::target_invalid)) {
            return true;
        }
        set_project_blueprint_drop_target(doc, drag, target, over_sound_collection);
        return true;
    }

    if (!materialize_project_blueprint_drag(state)) {
        set_project_blueprint_drop_target(doc, drag, {}, false);
        return true;
    }

    if (drag.owner.type == nw::ObjectType::store) {
        auto* hit = context ? context->GetElementAtPoint(point) : nullptr;
        auto* store_target = find_ancestor_with_class(
            hit, "store_inventory_drop_target");
        const auto category = store_target
            ? parse_decimal_int32(
                  store_target->GetAttribute<Rml::String>("data-category", ""))
            : std::nullopt;
        ProjectBlueprintDropTarget target{
            .kind = category
                    && *category >= 0 && *category < 5
                ? ProjectBlueprintDropTargetKind::store_inventory
                : ProjectBlueprintDropTargetKind::none,
            .category = category.value_or(-1),
        };

        auto* store = nw::kernel::objects().get<nw::Store>(drag.owner);
        nw::Inventory* inventory = nullptr;
        if (store && category) {
            switch (*category) {
            case 0:
                inventory = &store->inventory().armor;
                break;
            case 1:
                inventory = &store->inventory().miscellaneous;
                break;
            case 2:
                inventory = &store->inventory().potions;
                break;
            case 3:
                inventory = &store->inventory().rings;
                break;
            case 4:
                inventory = &store->inventory().weapons;
                break;
            default:
                break;
            }
        }
        const bool valid = inventory
            && inventory->items.size() < inventory->items.capacity();
        if (target == drag.target
            && (drag.phase == ProjectBlueprintDragPhase::target_valid
                || drag.phase == ProjectBlueprintDragPhase::target_invalid)) {
            return true;
        }
        set_project_blueprint_drop_target(doc, drag, target, valid);
        return true;
    }

    auto* creature = drag.owner.type == nw::ObjectType::creature
        ? nw::kernel::objects().get<nw::Creature>(drag.owner)
        : nullptr;
    auto* owner_item = drag.owner.type == nw::ObjectType::item
        ? nw::kernel::objects().get<nw::Item>(drag.owner)
        : nullptr;
    auto* owner_placeable = drag.owner.type == nw::ObjectType::placeable
        ? nw::kernel::objects().get<nw::Placeable>(drag.owner)
        : nullptr;
    nw::Inventory* inventory = creature ? &creature->inventory()
        : owner_item                    ? &owner_item->inventory()
        : owner_placeable               ? &owner_placeable->inventory()
                                        : nullptr;
    if (!inventory) {
        set_project_blueprint_drop_target(doc, drag, {}, false);
        return true;
    }

    auto* hit = context ? context->GetElementAtPoint(point) : nullptr;
    if (creature) {
        if (auto* equipment = find_ancestor_with_class(hit, "creature_equipment_slot")) {
            const auto slot_value = parse_decimal_int32(
                equipment->GetAttribute<Rml::String>("data-slot", ""));
            ProjectBlueprintDropTarget target;
            target.kind = ProjectBlueprintDropTargetKind::equipment;
            if (slot_value && *slot_value >= 0 && *slot_value < 18) {
                target.slot = static_cast<nw::EquipIndex>(*slot_value);
            }
            if (target == drag.target
                && (drag.phase == ProjectBlueprintDragPhase::target_valid
                    || drag.phase == ProjectBlueprintDragPhase::target_invalid)) {
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
            set_project_blueprint_drop_target(doc, drag, target, valid);
            return true;
        }
    }

    auto* board = find_el(doc, "creature_inventory_board");
    if (!board || !board->IsVisible(true)
        || !board->IsPointWithinElement(point)) {
        set_project_blueprint_drop_target(doc, drag, {}, false);
        return true;
    }

    const float left = board->GetAbsoluteLeft() + board->GetClientLeft();
    const float top = board->GetAbsoluteTop() + board->GetClientTop();
    const int column = static_cast<int>((point.x - left) / kCreatureInventoryCellPx);
    const int visual_row = static_cast<int>((point.y - top) / kCreatureInventoryCellPx);
    ProjectBlueprintDropTarget target{
        .kind = ProjectBlueprintDropTargetKind::inventory,
        .page = state.creature_inventory_page,
        .row = visual_row + drag.height - 1,
        .column = column,
    };
    if (target == drag.target
        && (drag.phase == ProjectBlueprintDragPhase::target_valid
            || drag.phase == ProjectBlueprintDragPhase::target_invalid)) {
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
    set_project_blueprint_drop_target(doc, drag, target, valid);
    return true;
}

void commit_project_blueprint_drag(Rml::ElementDocument* doc, AppState& state)
{
    if (!state.project_blueprint_drag.active()) {
        return;
    }
    if (state.project_blueprint_drag.phase != ProjectBlueprintDragPhase::target_valid) {
        cancel_project_blueprint_drag(doc, state);
        return;
    }

    clear_project_blueprint_drop_visuals(doc, state.project_blueprint_drag);
    auto drag = std::move(state.project_blueprint_drag);
    state.project_blueprint_drag = {};
    if (drag.kind == ProjectBlueprintDragKind::encounter_spawn) {
        if (!drag.encounter_spawn_edit
            || drag.target.kind != ProjectBlueprintDropTargetKind::encounter_spawns) {
            return;
        }
        auto result = state.backend.replace_encounter_spawns(
            std::move(*drag.encounter_spawn_edit),
            command_context(state, nw::toolset::CommandSource::renderer));
        append_command_result(state, result);
        return;
    }
    if (drag.kind == ProjectBlueprintDragKind::sound_resource) {
        if (!drag.sound_resource_edit
            || drag.target.kind != ProjectBlueprintDropTargetKind::sound_resources) {
            return;
        }
        auto result = state.backend.replace_sound_resources(
            std::move(*drag.sound_resource_edit),
            "Add sound resource",
            command_context(state, nw::toolset::CommandSource::renderer));
        append_command_result(state, result);
        return;
    }

    if (drag.kind != ProjectBlueprintDragKind::item
        || !nw::kernel::objects().valid(drag.item)) {
        return;
    }
    const auto destroy_unowned_item = [&drag]() {
        if (nw::kernel::objects().valid(drag.item)) {
            nw::kernel::objects().destroy(drag.item);
        }
    };
    if (drag.target.kind == ProjectBlueprintDropTargetKind::store_inventory) {
        if (drag.target.category < 0 || drag.target.category >= 5) {
            destroy_unowned_item();
            return;
        }
        const std::array placements{nw::toolset::StoreItemPlacement{
            .item = drag.item,
            .category = static_cast<nw::toolset::StoreInventoryCategory>(
                drag.target.category),
        }};
        auto result = state.backend.place_store_items(
            drag.owner,
            placements,
            command_context(state, nw::toolset::CommandSource::renderer));
        if (result.status != nw::toolset::CommandStatus::success) {
            destroy_unowned_item();
        }
        append_command_result(state, result);
        return;
    }
    nw::toolset::ItemPlacement placement{
        .item = drag.item,
        .target = drag.target.kind == ProjectBlueprintDropTargetKind::equipment
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
    if (result.status != nw::toolset::CommandStatus::success) {
        destroy_unowned_item();
    }
    append_command_result(state, result);
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
    auto& editor = state.area_tile_editor;
    if (editor.stroke.active) {
        return true;
    }
    const auto brush = selected_area_tile_brush(state, SDL_BUTTON_LEFT);
    if (!brush) {
        editor.feedback = "Choose a terrain action";
        sync_area_tile_palette_window(doc, state, true);
        return true;
    }
    if (brush->kind != nw::toolset::AreaTileBrushKind::group) {
        editor.feedback = "Only tileset features have a manual rotation";
        sync_area_tile_palette_window(doc, state, true);
        return true;
    }
    editor.group_orientation = (editor.group_orientation + 1) % 4;
    editor.feedback = "Feature rotation: "
        + std::to_string(editor.group_orientation * 90) + " degrees";
    const std::string feedback = editor.feedback;
    const bool has_cursor_point = editor.cursor_update_pending
        || editor.cursor_target_index != UINT32_MAX;
    const Rml::Vector2f cursor_point = editor.pending_cursor_point;
    editor.cursor_target_index = UINT32_MAX;
    editor.cursor_update_pending = false;
    const auto viewport = active_workspace_viewer_viewport_request(
        doc, state, frame_width, frame_height);
    if (viewport && viewport->kind == WorkspaceViewerViewportKind::area
        && has_cursor_point
        && point_within_viewport(viewport->rect, cursor_point)) {
        (void)update_area_tile_cursor(
            renderer, state, cursor_point, *viewport);
    } else {
        editor.preview_rows.clear();
        (void)renderer.update_viewer_area_tile_preview(
            active_workspace_area(state), {});
    }
    if (editor.feedback.empty()) {
        editor.feedback = feedback;
    }
    sync_area_tile_palette_window(doc, state, true);
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
        || state.shell.command_palette_visible || state.module_dialog_open
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
    const auto object = renderer.active_viewer_object();
    if (!editable_area_object(object)) { return false; }

    if (key.key == SDLK_DELETE
        && delete_selected_encounter_spawn_point(renderer, state)) {
        return true;
    }
    if (key.key == SDLK_DELETE) {
        const auto result = dispatch_command(state,
            "area.object.delete", {}, nw::toolset::CommandSource::shortcut);
        append_command_result(state, result);
        return true;
    }
    const auto result = dispatch_command(state,
        "object.transform.randomize_orientation", {}, nw::toolset::CommandSource::shortcut);
    sync_area_object_after_command(renderer, state, result);
    return true;
}

void sync_command_overlay_visibility(AppState& state)
{
    auto* doc = state.command_overlay_document;
    if (!doc) { return; }
    const bool active = state.command_form || state.project_load.active()
        || state.backend.blueprint_operation_active()
        || state.backend.blueprint_publication_pending();
    if (active && !doc->IsVisible()) {
        doc->Show(Rml::ModalFlag::Modal);
    } else if (!active && doc->IsVisible()) {
        doc->Hide();
    }
}

void sync_project_load_overlay(AppState& state)
{
    sync_command_overlay_visibility(state);
    auto* host = find_el(state.command_overlay_document,
        "project_load_overlay");
    if (!host) { return; }
    host->SetClass("active", state.project_load.active());
    if (!state.project_load.active()) {
        host->SetInnerRML("");
        return;
    }

    auto* message = find_el(state.command_overlay_document,
        "project_load_message");
    if (!message) {
        std::string markup
            = "<div class=\"command_form project_load_panel\">"
              "<div class=\"command_form_title\">Opening Project</div>"
              "<div id=\"project_load_message\" class=\"command_form_message\"></div>"
              "<div class=\"home_import_progress\"><div class=\"home_import_progress_fill\"></div></div>"
              "<div class=\"project_load_path\">";
        markup += escape_html(state.project_load.path);
        markup += "</div></div>";
        host->SetInnerRML(markup);
        message = find_el(state.command_overlay_document,
            "project_load_message");
    }
    if (message) {
        message->SetInnerRML(escape_html(state.project_load.stage));
    }
}

bool queue_project_open(AppState& state, std::string path,
    nw::toolset::CommandSource source,
    bool close_import_panel_on_success = false)
{
    if (path.empty() || state.project_load.active()) { return false; }
    state.project_load = {
        .path = std::move(path),
        .source = source,
        .close_import_panel_on_success = close_import_panel_on_success,
    };
    sync_project_load_overlay(state);
    return true;
}

void close_command_form_combobox(AppState& state)
{
    if (state.command_form_combobox_field) {
        const auto id = "command_form_field_"
            + std::to_string(*state.command_form_combobox_field);
        if (auto* field = find_el(
                state.command_overlay_document, id.c_str())) {
            field->SetClass("open", false);
        }
    }
    state.command_form_combobox.close();
    state.command_form_combobox_field.reset();
    state.command_form_combobox_placement.reset();
    if (auto* popup = find_el(state.command_overlay_document,
            "command_form_combobox_popup")) {
        popup->SetClass("active", false);
        popup->SetInnerRML("");
    }
}

bool open_command_form_combobox(AppState& state, size_t field_index)
{
    if (!state.command_form
        || field_index >= state.command_form->fields.size()) {
        return false;
    }
    const auto& field = state.command_form->fields[field_index];
    if (field.choices.empty()
        || field.choices.size()
            > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
        return false;
    }
    if (state.command_form_combobox_field == field_index
        && state.command_form_combobox.is_active()) {
        if (state.command_form_combobox.popup_visible()) {
            state.command_form_combobox.hide_popup();
        } else {
            (void)state.command_form_combobox.show_popup();
        }
        state.command_form_combobox_placement.reset();
        return true;
    }

    absl::flat_hash_set<std::string_view> values;
    values.reserve(field.choices.size());
    std::vector<nw::toolset::VirtualComboBoxItem> options;
    options.reserve(field.choices.size());
    int32_t selected = -1;
    for (size_t index = 0; index < field.choices.size(); ++index) {
        const auto& choice = field.choices[index];
        if (choice.value.empty() || !values.insert(choice.value).second) {
            return false;
        }
        const auto key = static_cast<int32_t>(index);
        options.push_back({key, choice.label, {}});
        if (choice.value == field.value) { selected = key; }
    }
    if (selected < 0) { return false; }

    close_command_form_combobox(state);
    if (!state.command_form_combobox.open(
            std::move(options), selected)) {
        return false;
    }
    state.command_form_combobox_field = field_index;
    state.command_form_combobox_placement.reset();
    return true;
}

void sync_command_form_combobox(AppState& state, bool force = false)
{
    auto* doc = state.command_overlay_document;
    auto* popup = find_el(doc, "command_form_combobox_popup");
    if (!popup || !state.command_form
        || !state.command_form_combobox_field
        || !state.command_form_combobox.is_active()) {
        return;
    }
    const auto field_index = *state.command_form_combobox_field;
    if (field_index >= state.command_form->fields.size()) {
        close_command_form_combobox(state);
        return;
    }
    const auto field_id = "command_form_field_"
        + std::to_string(field_index);
    auto* field = find_el(doc, field_id.c_str());
    auto* bounds = find_el(doc, "command_form_overlay");
    if (!field || !bounds) {
        close_command_form_combobox(state);
        return;
    }

    const bool visible = state.command_form_combobox.popup_visible();
    field->SetClass("open", visible);
    popup->SetClass("active", visible);
    if (!visible) { return; }

    const nw::toolset::VirtualComboBoxRect anchor{
        .x = static_cast<int>(std::lround(
            field->GetAbsoluteLeft() - bounds->GetAbsoluteLeft())),
        .y = static_cast<int>(std::lround(
            field->GetAbsoluteTop() - bounds->GetAbsoluteTop())),
        .width = static_cast<int>(std::lround(field->GetOffsetWidth())),
        .height = static_cast<int>(std::lround(field->GetOffsetHeight())),
    };
    const nw::toolset::VirtualComboBoxRect bounds_rect{
        .width = static_cast<int>(std::lround(std::max(
            bounds->GetClientWidth(), bounds->GetOffsetWidth()))),
        .height = static_cast<int>(std::lround(std::max(
            bounds->GetClientHeight(), bounds->GetOffsetHeight()))),
    };
    const auto placement = state.command_form_combobox.place_popup(
        anchor, bounds_rect);
    if (placement.width <= 0 || placement.height <= 0) { return; }
    if (!state.command_form_combobox_placement
        || *state.command_form_combobox_placement != placement) {
        popup->SetProperty("left", std::to_string(placement.left) + "px");
        popup->SetProperty("top", std::to_string(placement.top) + "px");
        popup->SetProperty("width", std::to_string(placement.width) + "px");
        popup->SetProperty("height", std::to_string(placement.height) + "px");
        state.command_form_combobox_placement = placement;
    }

    const int observed_scroll_top = std::max(0,
        static_cast<int>(std::lround(popup->GetScrollTop())));
    auto update = state.command_form_combobox.update(
        placement.height, observed_scroll_top, force);
    if (update.replace_markup) {
        popup->SetInnerRML(update.markup);
    }
    if (update.set_scroll) {
        popup->SetScrollTop(static_cast<float>(update.scroll_top));
    }
}

void sync_command_form(AppState& state, bool force = false)
{
    sync_command_overlay_visibility(state);
    auto* doc = state.command_overlay_document;
    auto* host = find_el(doc, "command_form_overlay");
    if (!host) { return; }
    const bool rendered = state.rendered_command_form_generation != state.command_form_generation;
    if (rendered) {
        close_command_form_combobox(state);
        state.rendered_command_form_generation = state.command_form_generation;
        host->SetClass("active", bool(state.command_form));
        if (!state.command_form) {
            host->SetInnerRML("");
            return;
        }
        const auto& form = *state.command_form;
        std::optional<size_t> close_action;
        if (form.action_list) {
            const auto cancel = std::ranges::find(
                form.actions, std::string{"cancel"},
                &nw::toolset::CommandPromptAction::id);
            if (cancel != form.actions.end()) {
                close_action = static_cast<size_t>(
                    std::distance(form.actions.begin(), cancel));
            }
        }
        std::string markup = "<div class=\"command_form";
        if (form.action_list) { markup += " command_form_action_picker"; }
        markup += "\">";
        if (close_action) {
            markup += "<div class=\"command_form_title_row\"><div class=\"command_form_title\">"
                + escape_html(form.title)
                + "</div><button type=\"button\" class=\"command_form_action command_form_close\" title=\"Close\" id=\"command_form_action_"
                + std::to_string(*close_action) + "\" data-index=\""
                + std::to_string(*close_action)
                + "\"><span class=\"command_form_close_glyph\">&#215;</span></button></div>";
        } else {
            markup += "<div class=\"command_form_title\">"
                + escape_html(form.title) + "</div>";
        }
        markup += "<div class=\"command_form_message\">"
            + escape_html(form.message) + "</div>";
        for (size_t index = 0; index < form.fields.size(); ++index) {
            const auto& field = form.fields[index];
            const auto id = "command_form_field_" + std::to_string(index);
            markup += "<div class=\"command_form_row\"><label for=\"" + id + "\">" + escape_html(field.label) + "</label>";
            if (field.choices.empty()) {
                markup += "<input type=\"text\" id=\"" + id + "\" value=\"" + escape_html(field.value) + "\"/>";
            } else {
                const auto selected = std::ranges::find(
                    field.choices, field.value,
                    &nw::toolset::CommandPromptChoice::value);
                markup += "<button type=\"button\" id=\"" + id
                    + "\" class=\"combobox_field command_form_choice_field\" data-field=\""
                    + std::to_string(index)
                    + "\"><span class=\"combobox_value\">";
                if (selected != field.choices.end()) {
                    markup += escape_html(selected->label);
                }
                markup += "</span><span class=\"combobox_arrow\"><span class=\"combobox_arrow_indicator\"></span></span></button>";
            }
            if (field.directory) { markup += "<button class=\"command_form_browse\">Browse...</button>"; }
            markup += "</div>";
        }
        const bool has_feedback = !form.fields.empty() || !form.detail.empty() || !form.file_suffix.empty();
        if (has_feedback) {
            markup += "<div class=\"command_form_feedback\"><div id=\"command_form_filename\"></div><div id=\"command_form_detail\">"
                + escape_html(form.detail) + "</div><div id=\"command_form_error\"></div></div>";
        }
        markup += "<div class=\"command_form_actions";
        if (form.action_list) { markup += " command_form_action_list"; }
        markup += "\">";
        for (size_t index = 0; index < form.actions.size(); ++index) {
            if (close_action && *close_action == index) { continue; }
            markup += "<button class=\"command_form_action "
                + std::string{index == 0 ? "command_form_action_primary" : "command_form_action_secondary"}
                + "\" id=\"command_form_action_" + std::to_string(index)
                + "\" data-index=\"" + std::to_string(index) + "\">" + escape_html(form.actions[index].label) + "</button>";
        }
        markup += "</div></div><div id=\"command_form_combobox_popup\" class=\"combobox_options combobox_popup command_form_combobox_popup\"></div>";
        host->SetInnerRML(markup);
        if (auto* input = find_el(doc, "command_form_field_0")) { input->Focus(); }
    }
    if (!state.command_form) { return; }
    auto& form = *state.command_form;
    bool changed = false;
    bool complete = true;
    for (size_t index = 0; index < form.fields.size(); ++index) {
        auto& field = form.fields[index];
        if (field.choices.empty()) {
            const auto id = "command_form_field_" + std::to_string(index);
            const auto value = get_input_value(doc, id.c_str());
            changed |= field.value != value;
            field.value = value;
        }
        const auto selected = std::ranges::find(
            field.choices, field.value,
            &nw::toolset::CommandPromptChoice::value);
        complete &= (!field.required || !field.value.empty())
            && (field.choices.empty() || selected != field.choices.end());
    }
    if (!changed && !rendered && !force) {
        sync_command_form_combobox(state);
        return;
    }
    if (changed) { form.detail.clear(); }
    std::string diagnostic;
    std::string filename;
    if (!form.file_suffix.empty() && form.fields.size() >= 2) {
        std::string normalized;
        if (nw::toolset::validate_blueprint_resref(form.fields[0].value, normalized, diagnostic)) {
            const std::array destinations{nw::toolset::BlueprintDestination{
                nw::Resource::from_filename(normalized + form.file_suffix), form.fields[1].value}};
            const auto result = nw::toolset::validate_blueprint_destinations(state.backend.current_project_dir(), destinations);
            diagnostic = result[0].error;
            if (!result[0].target.empty()) { filename = result[0].target.lexically_relative(state.backend.current_project_dir()).generic_string(); }
        }
    }
    if (auto* target = find_el(doc, "command_form_filename")) { target->SetInnerRML(escape_html(filename)); }
    if (auto* detail = find_el(doc, "command_form_detail")) { detail->SetInnerRML(escape_html(form.detail)); }
    if (auto* error = find_el(doc, "command_form_error")) { error->SetInnerRML(escape_html(diagnostic)); }
    if (auto* button = find_el(doc, "command_form_action_0")) {
        const bool enabled = complete && diagnostic.empty();
        button->SetClass("disabled", !enabled);
        if (enabled) {
            button->RemoveAttribute("disabled");
        } else {
            button->SetAttribute("disabled", true);
        }
    }
    sync_command_form_combobox(state, force);
}

bool commit_command_form_combobox(AppState& state, int32_t choice_index)
{
    if (!state.command_form || !state.command_form_combobox_field
        || choice_index < 0) {
        return false;
    }
    const auto field_index = *state.command_form_combobox_field;
    if (field_index >= state.command_form->fields.size()) { return false; }
    auto& field = state.command_form->fields[field_index];
    const auto index = static_cast<size_t>(choice_index);
    if (index >= field.choices.size()
        || !state.command_form_combobox.select_key(choice_index)) {
        return false;
    }

    field.value = field.choices[index].value;
    state.command_form->detail.clear();
    const auto field_id = "command_form_field_" + std::to_string(field_index);
    auto* field_element = find_el(
        state.command_overlay_document, field_id.c_str());
    if (field_element) {
        if (auto* value = find_ancestor_with_class(
                field_element->GetChild(0), "combobox_value")) {
            value->SetInnerRML(escape_html(field.choices[index].label));
        }
    }
    close_command_form_combobox(state);
    sync_command_form(state, true);
    if (field_element) { field_element->Focus(); }
    return true;
}

void sync_blueprint_operation(AppState& state)
{
    sync_command_overlay_visibility(state);
    auto* doc = state.command_overlay_document;
    auto* host = find_el(doc, "blueprint_operation_overlay");
    if (!host) { return; }
    host->SetClass("active", (state.backend.blueprint_operation_active() || state.backend.blueprint_publication_pending()));
    if (!(state.backend.blueprint_operation_active() || state.backend.blueprint_publication_pending())) {
        if (!state.blueprint_operation_markup.empty()) {
            host->SetInnerRML("");
            state.blueprint_operation_markup.clear();
        }
        state.blueprint_review_page = 0;
        return;
    }
    const nw::toolset::BlueprintOperationProgress publication{.stage = "finalizing", .detail = "Blueprint saved; retry resource and document publication before editing.", .error = "Publication is pending"};
    const auto& progress = state.backend.blueprint_publication_pending() ? publication : state.backend.blueprint_progress();
    const auto& documents = state.backend.blueprint_updated_documents();
    const bool ready = progress.stage == "ready" && !state.backend.blueprint_worker_active();
    const bool restore = progress.stage == "restore_ready" || progress.stage == "recovery";
    const bool complete = progress.stage == "complete" || progress.stage == "failed";
    std::string label = progress.stage;
    const std::array<std::pair<const char*, const char*>, 16> labels{{std::pair{"starting", "Starting"}, {"resolving", "Resolving blueprint dependencies"},
        {"preparing_live", "Preparing live instances"},
        {"discovering", "Finding documents"}, {"scanning", "Scanning documents"}, {"preparing", "Preparing replacements"},
        {"ready", "Review replacements"}, {"checking", "Checking for changes"}, {"saving", "Saving documents"},
        {"saved", "Finalizing"}, {"finalizing", "Finalizing"}, {"restore_ready", "Review restoration"},
        {"restoring", "Restoring original files"}, {"recovery", "Recovery required"}, {"failed", "Update stopped"}, {"complete", "Complete"}}};
    for (const auto& [stage, title] : labels) {
        if (progress.stage == stage) {
            label = title;
            break;
        }
    }
    std::string markup = "<div class=\"command_form\"><div class=\"command_form_title\">Update Blueprint References</div><div class=\"blueprint_operation_stage\">"
        + escape_html(label) + "</div>";
    if (!ready && !restore && !complete) {
        markup += "<div class=\"home_import_progress\"><div class=\"";
        if (progress.total) {
            const auto percent = 100.0 * static_cast<double>(progress.completed) / static_cast<double>(progress.total);
            markup += "blueprint_progress_fill\" style=\"width:" + std::to_string(std::clamp(percent, 0.0, 100.0)) + "%\"></div></div>";
            markup += "<div class=\"blueprint_operation_progress_text\">" + std::to_string(progress.completed) + " / " + std::to_string(progress.total) + " " + escape_html(progress.unit) + "</div>";
        } else {
            markup += "home_import_progress_fill\"></div></div>";
        }
    }
    markup += "<div class=\"blueprint_operation_detail\">" + escape_html(progress.detail) + "</div>";
    if (ready || complete) {
        markup += "<div class=\"blueprint_operation_summary\">" + std::to_string(progress.instances) + " instances, " + std::to_string(documents.size()) + " changed documents";
        if (progress.covered) { markup += "; " + std::to_string(progress.covered) + " nested matches covered by a parent replacement"; }
        if (progress.reference_uses) { markup += "; " + std::to_string(progress.reference_uses) + " blueprint reference uses already point to this blueprint"; }
        markup += "</div>";
    }
    if (ready || restore) {
        constexpr size_t page_size = 20;
        const auto pages = std::max(size_t{1}, (documents.size() + page_size - 1) / page_size);
        state.blueprint_review_page = std::min(state.blueprint_review_page, pages - 1);
        const auto start = state.blueprint_review_page * page_size;
        markup += "<div class=\"blueprint_review_documents\">";
        for (size_t index = start; index < std::min(documents.size(), start + page_size); ++index) {
            markup += "<div>" + escape_html(documents[index].lexically_relative(state.backend.current_project_dir()).generic_string()) + "</div>";
        }
        markup += "</div>";
        if (pages > 1) {
            markup += "<div class=\"blueprint_operation_page_actions\"><button class=\"blueprint_operation_page\" data-delta=\"-1\">Previous</button><span>"
                + std::to_string(state.blueprint_review_page + 1) + " / " + std::to_string(pages)
                + "</span><button class=\"blueprint_operation_page\" data-delta=\"1\">Next</button></div>";
        }
    }
    if (!progress.error.empty()) { markup += "<div class=\"blueprint_operation_error\">" + escape_html(progress.error) + "</div>"; }
    const auto button = [&](const char* command, const char* title, bool primary) {
        markup += std::string{"<button class=\"blueprint_authoring_action command_form_action "}
            + (primary ? "command_form_action_primary" : "command_form_action_secondary")
            + "\" data-command=\"" + command + "\">" + title + "</button>";
    };
    markup += "<div class=\"command_form_actions\">";
    if (ready && !documents.empty()) { button("blueprint.references.apply", "Update Instances", true); }
    if (restore) { button("blueprint.references.restore_apply", "Restore Original Files", true); }
    if (progress.stage == "finalizing" && !progress.error.empty()) { button(state.backend.blueprint_publication_pending() ? "blueprint.refresh" : "blueprint.references.retry", "Retry Publication", true); }
    if (progress.stage == "finalizing" && !progress.error.empty() && !state.backend.blueprint_publication_pending()) { button("blueprint.references.restore_apply", "Restore Original Files", false); }
    if (progress.stage != "recovery" && progress.stage != "finalizing" && progress.stage != "restoring") {
        button("blueprint.references.cancel", complete || (ready && documents.empty()) ? "Close" : "Cancel", false);
    }
    markup += "</div></div>";
    if (markup != state.blueprint_operation_markup) {
        state.blueprint_operation_markup = markup;
        host->SetInnerRML(markup);
        host->Focus();
    }
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
        if (!result.prompt->fields.empty()
            || result.prompt->id.starts_with("blueprint.")) {
            state.command_form = std::move(*result.prompt);
            ++state.command_form_generation;
            result.prompt.reset();
            result.status = nw::toolset::CommandStatus::noop;
            result.output_channel = nw::toolset::CommandOutputChannel::none;
            break;
        }
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

void show_open_module_dialog(SDL_Window* window, AppState& state, bool import = false)
{
    if (state.open_module_dialog_event == 0) {
        append_output(state, "error", "Open module dialog unavailable");
        return;
    }
    if (state.module_dialog_open) {
        append_output(state, "info", "Open module dialog already active");
        return;
    }
    if (import && state.project_import.active()) {
        return;
    }

    static constexpr SDL_DialogFileFilter filters[] = {
        {"Neverwinter modules", "mod;zip"},
        {"All files", "*"},
    };
    static constexpr SDL_DialogFileFilter import_filters[] = {
        {"Neverwinter Nights modules", "mod"},
    };

    state.module_dialog_default_location = module_dialog_start_location().string();
    const char* default_location = state.module_dialog_default_location.empty()
        ? nullptr
        : state.module_dialog_default_location.c_str();

    state.module_dialog_command = import ? "import.module" : "toolset.open";
    if (import) {
        state.import_status = "Choose the .mod file to import.";
    }
    state.module_dialog_open = true;
    SDL_ShowOpenFileDialog(open_module_dialog_callback,
        new OpenModuleDialogRequest{state.open_module_dialog_event},
        window,
        import ? import_filters : filters,
        import ? 1 : static_cast<int>(sizeof(filters) / sizeof(filters[0])),
        default_location,
        false);
}

void show_open_project_dialog(SDL_Window* window, AppState& state, bool import = false)
{
    if (state.open_module_dialog_event == 0) {
        append_output(state, "error", "Open project dialog unavailable");
        return;
    }
    if (state.module_dialog_open) {
        append_output(state, "info", "Open dialog already active");
        return;
    }
    if (import && state.project_import.active()) { return; }

    state.module_dialog_default_location = project_dialog_start_location().string();
    const char* default_location = state.module_dialog_default_location.empty()
        ? nullptr
        : state.module_dialog_default_location.c_str();

    state.module_dialog_command = import ? "import.destination" : "toolset.open_project";
    state.module_dialog_open = true;
    const auto props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, window);
    if (default_location) {
        SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_LOCATION_STRING, default_location);
    }
    const std::string title = import
        ? "Choose a parent folder for the imported project"
        : "Open Project";
    SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING, title.c_str());
    SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_ACCEPT_STRING, import ? "Select Folder" : "Open Project");
    SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFOLDER, open_module_dialog_callback,
        new OpenModuleDialogRequest{state.open_module_dialog_event},
        props);
    SDL_DestroyProperties(props);
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
    auto* result = static_cast<OpenModuleDialogResult*>(event.user.data1);
    state.module_dialog_open = false;
    const std::string command = state.module_dialog_command.empty()
        ? "toolset.open"
        : state.module_dialog_command;
    state.module_dialog_command.clear();
    const bool importing = command == "import.module" || command == "import.destination";
    if (!result) {
        return;
    }

    const std::string path = std::move(result->path);
    const std::string error = std::move(result->error);
    const bool canceled = result->canceled;
    delete result;

    if (command == "blueprint.directory") {
        if (state.command_form && state.command_form_browse_generation == state.command_form_generation) {
            if (!error.empty()) {
                state.command_form->detail = error;
            } else if (!canceled && state.command_form->fields.size() >= 2) {
                state.command_form->fields[1].value = path;
            }
            ++state.command_form_generation;
            sync_command_form(state);
        }
        return;
    }

    if (!error.empty()) {
        append_output(state, "error", std::string{"File dialog failed: "} + error);
        if (importing) {
            state.import_status = "Import dialog failed: " + error;
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Import failed", error.c_str(), window);
            refresh_workspace_view(doc, state);
        }
        return;
    }
    if (canceled || path.empty()) {
        if (importing) {
            state.import_status = "Selection canceled. No project files were written.";
            refresh_workspace_view(doc, state);
        }
        return;
    }
    if (command == "import.module") {
        state.import_module_path = path;
        state.import_status = "Review the source and destination, then click Import.";
        refresh_workspace_view(doc, state);
        return;
    }
    if (command == "import.destination") {
        state.import_parent_dir = path;
        state.import_status = "Review the source and destination, then click Import.";
        refresh_workspace_view(doc, state);
        return;
    }
    if (!ensure_backend_ready(state)) {
        append_output(state, "error", "Backend initialization failed");
        return;
    }

    if (command == "toolset.open_project") {
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
        state.selected_recent_index = -1;
        set_input_value(doc, "recent_search", "");
        refresh_recent_list(doc, state);
    }
    if (command_result.ok()) {
        refresh_workspace_view(doc, state);
    }
}

void run_command_form_action(SDL_Window* window, Rml::ElementDocument* doc, AppState& state, size_t index)
{
    if (!state.command_form || index >= state.command_form->actions.size() || state.module_dialog_open) { return; }
    sync_command_form(state, true);
    const auto button_id = "command_form_action_" + std::to_string(index);
    if (auto* button = find_el(state.command_overlay_document, button_id.c_str()); button && button->HasAttribute("disabled")) { return; }
    auto action = state.command_form->actions[index];
    if (action.id != "cancel") {
        for (size_t field = 0; field < state.command_form->fields.size(); ++field) {
            const auto& prompt_field = state.command_form->fields[field];
            if (prompt_field.choices.empty()) {
                const auto id = "command_form_field_" + std::to_string(field);
                action.args.push_back(get_input_value(
                    state.command_overlay_document, id.c_str()));
            } else {
                action.args.push_back(prompt_field.value);
            }
        }
    }
    close_command_form_combobox(state);
    state.command_form.reset();
    ++state.command_form_generation;
    std::vector<std::string_view> args;
    for (const auto& argument : action.args) {
        args.push_back(argument);
    }
    (void)dispatch_command_flow(window, state, action.command_id, std::move(args), nw::toolset::CommandSource::widget);
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
        if (auto* page_button = find_ancestor_with_class(event.GetTargetElement(), "blueprint_operation_page")) {
            const auto delta = parse_decimal_int32(page_button->GetAttribute<Rml::String>("data-delta", ""));
            if (delta && *delta < 0 && state_.blueprint_review_page) {
                --state_.blueprint_review_page;
            } else if (delta && *delta > 0) {
                ++state_.blueprint_review_page;
            }
            sync_blueprint_operation(state_);
        } else if (auto* option = find_ancestor_with_class(
                       event.GetTargetElement(), "combobox_option")) {
            const auto key = parse_decimal_int32(
                option->GetAttribute<Rml::String>("data-key", ""));
            if (key) { (void)commit_command_form_combobox(state_, *key); }
        } else if (auto* field = find_ancestor_with_class(
                       event.GetTargetElement(), "command_form_choice_field")) {
            const auto index = parse_decimal_int32(
                field->GetAttribute<Rml::String>("data-field", ""));
            if (index && *index >= 0
                && open_command_form_combobox(
                    state_, static_cast<size_t>(*index))) {
                sync_command_form_combobox(state_, true);
                field->Focus();
            }
        } else if (auto* button = find_ancestor_with_class(event.GetTargetElement(), "blueprint_authoring_action")) {
            const auto command = button->GetAttribute<Rml::String>("data-command", "");
            (void)dispatch_command_flow(window_, state_, command, {}, nw::toolset::CommandSource::widget);
            refresh_recent_list(document_, state_);
            refresh_workspace_view(document_, state_);
            sync_command_form(state_);
            sync_blueprint_operation(state_);
        } else if (auto* action = find_ancestor_with_class(event.GetTargetElement(), "command_form_action")) {
            const auto index = parse_decimal_int32(action->GetAttribute<Rml::String>("data-index", ""));
            if (index && *index >= 0) { run_command_form_action(window_, document_, state_, static_cast<size_t>(*index)); }
        } else if (find_ancestor_with_class(event.GetTargetElement(), "command_form_browse")) {
            if (!state_.command_form || state_.module_dialog_open || state_.open_module_dialog_event == 0) { return; }
            close_command_form_combobox(state_);
            sync_command_form(state_);
            const auto chosen = std::filesystem::path{state_.command_form->fields[1].value};
            state_.module_dialog_default_location = (chosen.is_absolute() ? chosen : state_.backend.current_project_dir() / chosen).string();
            state_.module_dialog_command = "blueprint.directory";
            state_.command_form_browse_generation = state_.command_form_generation;
            state_.module_dialog_open = true;
            SDL_ShowOpenFolderDialog(open_module_dialog_callback,
                new OpenModuleDialogRequest{state_.open_module_dialog_event}, window_,
                state_.module_dialog_default_location.c_str(), false);
        } else if (state_.command_form_combobox.is_active()
            && !nw::toolset::combobox_contains_element(
                event.GetTargetElement())) {
            close_command_form_combobox(state_);
        } else {
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
        if (find_ancestor_with_id(event.GetTargetElement(), "home_import_module")) {
            state_.import_panel_open = true;
        } else if (find_ancestor_with_id(event.GetTargetElement(), "home_import_source")) {
            show_open_module_dialog(window_, state_, true);
        } else if (find_ancestor_with_id(event.GetTargetElement(), "home_import_destination")) {
            show_open_project_dialog(window_, state_, true);
        } else if (find_ancestor_with_id(event.GetTargetElement(), "home_import_close")) {
            if (!state_.project_import.active() && !state_.module_dialog_open) {
                state_.import_panel_open = false;
            }
        } else if (find_ancestor_with_id(event.GetTargetElement(), "home_import_start")) {
            if (state_.project_import.active() || state_.module_dialog_open) { return; }
            std::string error;
            if (state_.project_import.start(state_.client_executable, state_.import_module_path,
                    state_.import_parent_dir, error)) {
                state_.import_module_generation = state_.backend.module_generation();
                state_.import_status = "Importing... You can continue working; please wait for import to finish before quitting.";
                append_output(state_, "info", state_.import_status);
            } else {
                state_.import_status = error;
                append_output(state_, "error", error);
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Import failed", error.c_str(), window_);
            }
        } else if (find_ancestor_with_id(event.GetTargetElement(), "home_open_project")) {
            show_open_project_dialog(window_, state_);
        } else {
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
    const auto result = state.project_import.poll();
    if (!result) { return; }
    state.import_status = result->message;
    append_output(state, result->ok ? "info" : "error", result->message);
    if (!result->ok) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Import failed", result->message.c_str(), window);
    } else {
        remember_recent_project(state, result->project_dir);
        if (state.workspace.has_dirty_tabs()
            || state.backend.module_generation() != state.import_module_generation
            || state.module_dialog_open || state.play_preview.session.active()
            || state.play_preview.placement_pending()) {
            state.import_status += ". Open it from Open Project or Recent Projects when you are ready; your current work was kept open.";
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Import complete", state.import_status.c_str(), window);
        } else {
            if (!queue_project_open(state, result->project_dir.string(),
                    nw::toolset::CommandSource::widget, true)) {
                state.import_status += ". Another project is already opening.";
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                    "Could not open imported project",
                    state.import_status.c_str(), window);
            }
        }
    }
    refresh_recent_list(doc, state);
    refresh_workspace_view(doc, state);
}

std::string_view project_load_stage_message(
    nw::kernel::ModuleLoadProgressStage stage) noexcept
{
    using Stage = nw::kernel::ModuleLoadProgressStage;
    switch (stage) {
    case Stage::reset_services:
        return "Preparing module services...";
    case Stage::load_module_resource:
        return "Loading module resources...";
    case Stage::load_dependencies:
        return "Loading HAK and TLK dependencies...";
    case Stage::initialize_services:
        return "Initializing module data...";
    case Stage::instantiate_module:
        return "Instantiating module objects...";
    case Stage::complete:
        return "Finalizing project...";
    }
    return "Opening project...";
}

struct ProjectLoadPresentation {
    SDL_Window* window = nullptr;
    Rml::Context* context = nullptr;
    Rml::Context* palette_context = nullptr;
    ClientRenderer* renderer = nullptr;
    AppState* state = nullptr;
    nw::kernel::ModuleLoadProgressStage stage
        = nw::kernel::ModuleLoadProgressStage::reset_services;
    Uint64 last_present_ms = 0;
    bool has_stage = false;
};

void present_project_load_progress(
    void* user_data, nw::kernel::ModuleLoadProgressStage stage)
{
    auto* presentation = static_cast<ProjectLoadPresentation*>(user_data);
    if (!presentation || !presentation->window || !presentation->context
        || !presentation->palette_context || !presentation->renderer
        || !presentation->state
        || !presentation->state->project_load.active()) {
        return;
    }

    constexpr Uint64 k_min_present_interval_ms = 32;
    const Uint64 now = SDL_GetTicks();
    const bool stage_changed = !presentation->has_stage
        || presentation->stage != stage;
    if (!stage_changed
        && now - presentation->last_present_ms < k_min_present_interval_ms) {
        return;
    }
    presentation->stage = stage;
    presentation->has_stage = true;
    presentation->last_present_ms = now;

    presentation->state->project_load.stage
        = project_load_stage_message(stage);
    sync_project_load_overlay(*presentation->state);
    SDL_PumpEvents();

    const auto window_flags = SDL_GetWindowFlags(presentation->window);
    const auto pixels = query_window_pixels(presentation->window);
    if ((window_flags & SDL_WINDOW_MINIMIZED)
        || pixels.first <= 0 || pixels.second <= 0) {
        return;
    }

    uint32_t width = static_cast<uint32_t>(pixels.first);
    uint32_t height = static_cast<uint32_t>(pixels.second);
    if (!presentation->renderer->ensure_swapchain(
            presentation->window, width, height, presentation->context)) {
        return;
    }
    presentation->palette_context->SetDimensions(
        Rml::Vector2i(static_cast<int>(width), static_cast<int>(height)));
    presentation->palette_context->Update();
    presentation->renderer->begin_frame();
    presentation->context->Render();
    presentation->palette_context->Render();
    presentation->renderer->end_frame();
}

void poll_project_open(SDL_Window* window,
    Rml::Context* context,
    Rml::Context* palette_context,
    Rml::ElementDocument* doc,
    Rml::ElementDocument* palette_doc,
    ClientRenderer& renderer,
    AppState& state)
{
    if (!state.project_load.active() || !state.project_load.presented) {
        return;
    }

    const auto request = state.project_load;
    ProjectLoadPresentation presentation{
        .window = window,
        .context = context,
        .palette_context = palette_context,
        .renderer = &renderer,
        .state = &state,
    };
    const auto result = resolve_command_result(window, state,
        state.backend.open_project(request.path,
            {
                .callback = present_project_load_progress,
                .user_data = &presentation,
            }),
        request.source);
    state.project_load = {};
    if (result.ok()) {
        remember_recent_project(state, state.backend.current_project_dir());
        if (request.close_import_panel_on_success) {
            state.import_panel_open = false;
        }
        state.selected_recent_index = -1;
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
        << "  rollnw-client --version\n"
        << "  rollnw-client --build-info\n"
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

void start_client_kernel(const std::filesystem::path& install, const std::filesystem::path& user)
{
    nw::kernel::config().set_paths(install, user);
    nw::ConfigOptions options;
    options.profile = "nwn1";
    options.init_module = "";
    nw::kernel::config().initialize(std::move(options));
    nw::kernel::config().set_init_module("");
    nw::kernel::services().create();
    register_smalls_packages();
    nw::kernel::services().start();
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
        start_client_kernel(install.install, install.user);
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
    if (command == "blueprint-update") {
        if (argc != 4) { return 2; }
        try {
            const std::filesystem::path operation{argv[2]};
            const std::string_view phase{argv[3]};
            if (phase != "restore") {
                std::ifstream input{operation / "request.json"};
                const auto request = nlohmann::json::parse(input);
                if (request.at("version") != 1 || request.at("profile") != "nwn1") {
                    std::cerr << "Unsupported blueprint worker configuration\n";
                    return 1;
                }
                start_client_kernel(request.at("install").get<std::string>(), request.at("user").get<std::string>());
                const std::filesystem::path project{request.at("project").get<std::string>()};
                const auto options = nw::kernel::module_load_options_for_project(project);
                if (!nw::kernel::load_module(project, false, options)) {
                    std::cerr << "Cannot load blueprint operation project\n";
                    return 1;
                }
            }
            std::string error;
            if (!nw::toolset::run_blueprint_update_operation(operation, phase, error)) {
                std::cerr << error << '\n';
                return 1;
            }
            return 0;
        } catch (const std::exception& ex) {
            std::cerr << ex.what() << '\n';
            return 1;
        }
    }
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
    if (argc == 2 && std::string_view{argv[1]} == "--version") {
        std::cout << ROLLNW_TOOL_NAME " " ROLLNW_TOOL_VERSION "\n";
        return 0;
    }
    if (argc == 2 && std::string_view{argv[1]} == "--build-info") {
        std::cout << ROLLNW_TOOL_BUILD_INFO "\n";
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
    start_client_kernel(install.install, install.user);

    SDL_SetLogPriorities(SDL_LOG_PRIORITY_INFO);
    if (!SDL_SetAppMetadata("rollnw | client", ROLLNW_TOOL_VERSION, ROLLNW_CLIENT_APP_ID)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL_SetAppMetadata failed: %s", SDL_GetError());
    }
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_TYPE_STRING, "application");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
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
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED);
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
    const nw::Resource command_modals_rml{nw::Resref{"ui/command_modals"}, nw::ResourceType::rml};
    if (!ui_resources.contains(panel_rml) || !ui_resources.contains(panel_rcss) || !ui_resources.contains(command_modals_rml)) {
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
    {
        int gamepad_count = 0;
        if (SDL_JoystickID* gamepads = SDL_GetGamepads(&gamepad_count)) {
            if (gamepad_count > 0) {
                open_play_preview_gamepad(state.play_preview, gamepads[0]);
            }
            SDL_free(gamepads);
        }
    }
    ObjectWorkbenchChangeListener object_workbench_change_listener{state};
    context->AddEventListener(
        "change", &object_workbench_change_listener, false);
    context->AddEventListener(
        "blur", &object_workbench_change_listener, true);
    renderer.set_rml_generated_textures(
        &state.item_icon_cache.textures,
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

    auto* doc = load_rml_document_from_resource(*context, ui_resources, panel_rml);
    if (!doc) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadDocument failed: ui/panel.rml");
        return 1;
    }
    doc->Show();
    HomeProjectActionListener home_project_action_listener{window, doc, state};
    context->AddEventListener("click", &home_project_action_listener);
    BlueprintActionListener blueprint_action_listener{window, doc, state};
    palette_context->AddEventListener("click", &blueprint_action_listener);
    const std::filesystem::path executable_arg{argv[0]};
    state.client_executable = executable_arg.has_parent_path()
        ? std::filesystem::absolute(executable_arg)
        : client_base_path() / executable_arg;
    auto* palette_doc = load_command_palette_document(*palette_context);
    if (!palette_doc) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadDocument failed: command_palette.rml");
        return 1;
    }
    palette_doc->Show();
    // The command context renders after the native viewport. Modal UI belongs
    // here; z-index in the main document cannot cover a later native draw.
    state.command_overlay_document = load_rml_document_from_resource(*palette_context, ui_resources, command_modals_rml);
    if (!state.command_overlay_document) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadDocument failed: ui/command_modals.rml");
        return 1;
    }
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
        update_viewer_frame_metrics(state, raw_frame_delta_seconds);

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
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
            if (state.open_module_dialog_event != 0 && event.type == state.open_module_dialog_event) {
                handle_open_module_dialog_result(window, doc, state, event);
                continue;
            }
            if (consume_terminal_toggle_text_input(state, event)) {
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
                    RmlSDL::InputEventHandler(palette_context, window, event);
                    continue;
                }
            }
            if (state.command_form) {
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
                        && state.command_form_combobox.is_active()) {
                        close_command_form_combobox(state);
                        continue;
                    }
                    if (event.key.key == SDLK_TAB
                        && state.command_form_combobox.is_active()) {
                        close_command_form_combobox(state);
                    }
                    if (focused_field && *focused_field >= 0
                        && (event.key.key == SDLK_UP
                            || event.key.key == SDLK_DOWN)) {
                        const auto field_index = static_cast<size_t>(*focused_field);
                        if (state.command_form_combobox_field != field_index
                            || !state.command_form_combobox.is_active()) {
                            (void)open_command_form_combobox(
                                state, field_index);
                        } else if (!state.command_form_combobox.popup_visible()) {
                            (void)state.command_form_combobox.show_popup();
                        }
                        (void)state.command_form_combobox.move_selection(
                            event.key.key == SDLK_UP ? -1 : 1);
                        sync_command_form_combobox(state, true);
                        continue;
                    }
                    if (focused_field && *focused_field >= 0
                        && (event.key.key == SDLK_RETURN
                            || event.key.key == SDLK_KP_ENTER)) {
                        const auto field_index = static_cast<size_t>(*focused_field);
                        if (state.command_form_combobox_field != field_index
                            || !state.command_form_combobox.is_active()) {
                            if (open_command_form_combobox(
                                    state, field_index)) {
                                sync_command_form_combobox(state, true);
                            }
                        } else if (!state.command_form_combobox.popup_visible()) {
                            (void)state.command_form_combobox.show_popup();
                            sync_command_form_combobox(state, true);
                        } else if (const auto selected
                            = state.command_form_combobox.selected_key()) {
                            (void)commit_command_form_combobox(
                                state, *selected);
                        }
                        continue;
                    }
                    std::optional<size_t> action_index;
                    if (event.key.key == SDLK_ESCAPE) {
                        const auto cancel = std::find_if(state.command_form->actions.begin(), state.command_form->actions.end(), [](const auto& action) { return action.id == "cancel"; });
                        if (cancel != state.command_form->actions.end()) {
                            action_index = static_cast<size_t>(std::distance(state.command_form->actions.begin(), cancel));
                        }
                    } else if (event.key.key == SDLK_RETURN
                        && !state.command_form->actions.empty()) {
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
                    RmlSDL::InputEventHandler(palette_context, window, event);
                    continue;
                }
            }

            bool dispatched_to_rml = false;
            switch (event.type) {
            case SDL_EVENT_QUIT: {
                if (state.project_import.active()) {
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
                        const bool enabled
                            = !nw::toolset::toolset_preview_navigation_debug(
                                state.play_preview.session)
                                   .enabled;
                        const auto status
                            = nw::toolset::set_toolset_preview_navigation_debug(
                                state.play_preview.session, enabled);
                        const bool render_ok = status == nw::toolset::PreviewStatus::ok
                            && renderer.update_toolset_preview_navigation_debug(
                                nw::toolset::toolset_preview_navigation_debug(
                                    state.play_preview.session));
                        append_output(
                            state,
                            render_ok ? "info" : "error",
                            render_ok
                                ? (enabled
                                          ? "Navigation debug enabled"
                                          : "Navigation debug disabled")
                                : "Failed to update navigation debug geometry");
                    }
                    dispatched_to_rml = true;
                    break;
                }
                if (!event.key.repeat && (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_F9)
                    && state.play_preview.selecting_actor) {
                    restore_play_preview_picker_shell(doc, state);
                    dispatched_to_rml = true;
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
                    dispatched_to_rml = true;
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
                    dispatched_to_rml = true;
                    break;
                }
                if (!event.key.repeat && event.key.key == SDLK_ESCAPE
                    && state.project_blueprint_drag.active()) {
                    cancel_project_blueprint_drag(doc, state);
                    state.pressed_recent_index = -1;
                    system_interface.SetMouseCursor("arrow");
                    dispatched_to_rml = true;
                    break;
                }
                if (state.project_blueprint_drag.active()
                    && state.project_blueprint_drag.threshold_crossed) {
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
                    cancel_area_tile_stroke(renderer, state);
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
                    && state.object_details_combobox.is_active()) {
                    close_object_details_combobox(doc, state);
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
                    if (state.object_details_combobox_row != row_index
                        || !state.object_details_combobox.is_active()) {
                        (void)open_object_details_sound_position_combobox(
                            doc, state, row_index);
                    } else if (!state.object_details_combobox.popup_visible()) {
                        (void)state.object_details_combobox.show_popup();
                    }
                    (void)state.object_details_combobox.move_selection(
                        event.key.key == SDLK_UP ? -1 : 1);
                    (void)sync_object_details_combobox(doc, state, true);
                    dispatched_to_rml = true;
                    break;
                }
                if (!event.key.repeat && sound_position_focused
                    && (event.key.key == SDLK_RETURN
                        || event.key.key == SDLK_KP_ENTER)) {
                    const auto row_index = static_cast<uint32_t>(
                        *focused_sound_position_row);
                    if (state.object_details_combobox_row != row_index
                        || !state.object_details_combobox.is_active()) {
                        if (open_object_details_sound_position_combobox(
                                doc, state, row_index)) {
                            (void)sync_object_details_combobox(doc, state, true);
                        }
                    } else if (!state.object_details_combobox.popup_visible()) {
                        (void)state.object_details_combobox.show_popup();
                        (void)sync_object_details_combobox(doc, state, true);
                    } else if (const auto selected
                        = state.object_details_combobox.selected_key()) {
                        if (commit_object_details_sound_position(
                                doc, state, *selected)) {
                            refresh_workspace_content(doc, state);
                        }
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
                    && state.sound_resource_selector_open) {
                    close_sound_resource_selector(state);
                    refresh_workspace_content(doc, state);
                    dispatched_to_rml = true;
                    break;
                }

                if (!event.key.repeat && event.key.key == SDLK_ESCAPE
                    && state.managed_list_reorder.active()) {
                    nw::toolset::clear_managed_list_reorder(
                        state.managed_list_reorder, doc);
                    system_interface.SetMouseCursor("arrow");
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

                const bool sound_catalog_search_focused
                    = !state.shell.command_palette_visible
                    && active_sound_resource_selector_matches_tab(state)
                    && focused_element_has_id(context, "sound_catalog_search")
                    && !(event.key.mod
                        & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI));
                if (sound_catalog_search_focused
                    && (event.key.key == SDLK_UP
                        || event.key.key == SDLK_DOWN)) {
                    const int selected = state.sound_catalog_list.move_selection(
                        event.key.key == SDLK_UP ? -1 : 1);
                    const int scroll_top
                        = state.sound_catalog_list.scroll_top_for_index(selected);
                    state.sound_catalog_list.set_scroll_top(scroll_top);
                    if (auto* list = find_el(doc, "sound_catalog_rows")) {
                        list->SetScrollTop(static_cast<float>(scroll_top));
                    }
                    state.sound_catalog_rendered = false;
                    sync_sound_catalog_window(doc, state, true);
                    dispatched_to_rml = true;
                    break;
                }
                if (!event.key.repeat && sound_catalog_search_focused
                    && (event.key.key == SDLK_RETURN
                        || event.key.key == SDLK_KP_ENTER)) {
                    const int selected = state.sound_catalog_list.selected();
                    if (selected >= 0
                        && static_cast<size_t>(selected)
                            < state.sound_catalog_matches.size()) {
                        const uint32_t row_index
                            = state.sound_catalog_matches[static_cast<size_t>(selected)];
                        if (commit_sound_catalog_selection(state, row_index)) {
                            close_sound_resource_selector(state);
                            refresh_workspace_content(doc, state);
                        }
                    }
                    dispatched_to_rml = true;
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
                    cancel_area_tile_stroke(renderer, state);
                    if (ensure_backend_ready(state)) {
                        dispatch_command_flow(
                            window, state, "workspace.close_tab", {}, nw::toolset::CommandSource::shortcut);
                        refresh_workspace_view(doc, state);
                    }
                    dispatched_to_rml = true;
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
                    dispatched_to_rml = true;
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

                if (handle_area_tile_key(
                        renderer, context, doc, state, event.key,
                        frame_width, frame_height)
                    || handle_area_object_key(
                        renderer, context, doc, state, event.key, frame_width, frame_height)
                    || handle_viewer_viewport_key(
                        renderer, context, doc, state, event.key, frame_width, frame_height)) {
                    dispatched_to_rml = true;
                    break;
                }
                break;
            }
            case SDL_EVENT_KEY_UP:
                if (state.play_preview.session.active()) {
                    dispatched_to_rml = true;
                }
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                open_play_preview_gamepad(
                    state.play_preview, event.gdevice.which);
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                if (state.play_preview.gamepad
                    && SDL_GetGamepadID(state.play_preview.gamepad)
                        == event.gdevice.which) {
                    close_play_preview_gamepad(state.play_preview);
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
                    state.play_preview.pending_input.flags
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
                    dispatched_to_rml = true;
                    break;
                }
                if (event.button.button == SDL_BUTTON_RIGHT
                    && state.managed_list_reorder.active()) {
                    nw::toolset::clear_managed_list_reorder(
                        state.managed_list_reorder, doc);
                    system_interface.SetMouseCursor("arrow");
                    dispatched_to_rml = true;
                    break;
                }
                if (event.button.button == SDL_BUTTON_RIGHT
                    && state.project_blueprint_drag.active()) {
                    cancel_project_blueprint_drag(doc, state);
                    state.pressed_recent_index = -1;
                    system_interface.SetMouseCursor("arrow");
                    dispatched_to_rml = true;
                    break;
                }
                if (state.project_blueprint_drag.active()
                    && state.project_blueprint_drag.threshold_crossed) {
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
                    if (command_palette_contains_point(
                            palette_doc, state, point)) {
                        RmlSDL::InputEventHandler(palette_context, window, event);
                        dispatched_to_rml = true;
                        break;
                    }
                    auto* top_hit = context ? context->GetElementAtPoint(point) : nullptr;
                    if (event.button.button == SDL_BUTTON_LEFT
                        && !nw::toolset::combobox_contains_element(top_hit)) {
                        const bool closed_smalls
                            = close_active_smalls_selector(doc);
                        const bool closed_details
                            = state.object_details_combobox.is_active();
                        if (closed_details) {
                            close_object_details_combobox(doc, state);
                        }
                        const bool closed_spell
                            = state.creature_spell_combobox.is_active();
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
                        if (state.appearance_selector_open) {
                            close_appearance_selector(state);
                            rebuild_active_appearances(
                                state, state.object_details.object);
                            refresh_workspace_content(doc, state);
                        }
                        if (state.sound_resource_selector_open) {
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
                            dispatched_to_rml = true;
                            break;
                        }
                        if ((state.play_preview.session.active()
                                || state.play_preview.placement_pending())
                            && viewer_viewport->kind == WorkspaceViewerViewportKind::area) {
                            if (event.button.button == SDL_BUTTON_LEFT
                                && state.play_preview.placement_pending()) {
                                if (const auto ray
                                    = renderer.viewer_viewport_ray(
                                        point.x, point.y, viewer_viewport->rect)) {
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
                                nw::toolset::clear_preview_pointer_action(
                                    state.play_preview.pending_input);
                                const auto door_hit
                                    = renderer.viewer_area_door_hit(
                                        point.x,
                                        point.y,
                                        viewer_viewport->rect,
                                        nw::toolset::toolset_preview_door_handles(
                                            state.play_preview.session));
                                const auto door_states
                                    = nw::toolset::toolset_preview_door_visual_states(
                                        state.play_preview.session);
                                const bool door_requires_interaction = door_hit
                                    && nw::toolset::preview_door_requires_interaction(
                                        door_states, door_hit->door_index);
                                if (door_requires_interaction) {
                                    (void)nw::toolset::set_preview_click_door(
                                        state.play_preview.pending_input,
                                        door_hit->door_index,
                                        door_hit->bounds_min,
                                        door_hit->bounds_max);
                                } else if (const auto ray
                                    = renderer.viewer_viewport_ray(
                                        point.x, point.y,
                                        viewer_viewport->rect)) {
                                    const std::array projection_inputs{
                                        nw::nav::NavRayProjectionInput{
                                            .origin = ray->origin,
                                            .displacement = ray->displacement,
                                        },
                                    };
                                    std::array<nw::nav::NavRayProjectionResult, 1> projected{};
                                    nw::toolset::project_toolset_preview_rays(
                                        state.play_preview.session,
                                        projection_inputs,
                                        projected);
                                    if (projected[0].status == nw::nav::NavStatus::ok) {
                                        (void)nw::toolset::set_preview_click_target(
                                            state.play_preview.pending_input,
                                            projected[0].position);
                                    }
                                }
                            } else if (event.button.button == SDL_BUTTON_RIGHT
                                || event.button.button == SDL_BUTTON_MIDDLE) {
                                state.viewer_viewport_dragging = true;
                                state.viewer_viewport_drag_mode
                                    = ClientViewportDragMode::look;
                                system_interface.SetMouseCursor("grabbing");
                            }
                            dispatched_to_rml = true;
                            break;
                        }
                        if (handle_area_tile_pointer_down(
                                renderer, system_interface, doc, state,
                                point, *viewer_viewport,
                                event.button.button)) {
                            dispatched_to_rml = true;
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
                            dispatched_to_rml = true;
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
                    if (!state.project_blueprint_drag.active()
                        && nw::toolset::begin_managed_list_reorder(
                            state.managed_list_reorder, top_hit,
                            nw::toolset::ui_v1_host(), point.x, point.y)) {
                        dispatched_to_rml = true;
                        break;
                    }
                    if (!recent_list_hit_blocked(doc, top_hit, point, state)) {
                        if (auto* recent_item = recent_item_at_point(doc, point)) {
                            const std::string key = recent_item->GetAttribute<Rml::String>("data-key", "");
                            if (!key.empty()) {
                                state.pressed_recent_index = static_cast<int>(std::strtol(key.c_str(), nullptr, 10));
                                const size_t index = static_cast<size_t>(
                                    std::max(state.pressed_recent_index, 0));
                                if (state.shell.showing_project_tree
                                    && !state.play_preview.selecting_actor
                                    && state.pressed_recent_index >= 0
                                    && index < state.project_rows.size()) {
                                    const auto& row = state.project_rows[index].node;
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
                if (command_palette_contains_point(
                        palette_doc, state, point)) {
                    RmlSDL::InputEventHandler(palette_context, window, event);
                    dispatched_to_rml = true;
                    break;
                }
                if (nw::toolset::update_managed_list_reorder(
                        state.managed_list_reorder, doc,
                        nw::toolset::ui_v1_host(), state.managed_lists,
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
                    dispatched_to_rml = true;
                    break;
                }
                if (state.project_blueprint_drag.active()) {
                    if (update_project_blueprint_drag(context, doc, state, point)) {
                        const char* cursor = state.project_blueprint_drag.phase
                                == ProjectBlueprintDragPhase::target_valid
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
                        if (state.play_preview.session.active()) {
                            state.play_preview.mouse_look_x += dx;
                            state.play_preview.mouse_look_y += dy;
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
                    dispatched_to_rml = true;
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
                    dispatched_to_rml = true;
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
                        dispatched_to_rml = true;
                        break;
                    }
                    state.area_tile_editor.cursor_update_pending = false;
                    if (state.area_tile_editor.stroke.active) {
                        state.area_tile_editor.stroke.has_last_target = false;
                        dispatched_to_rml = true;
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
                        const auto door_hit = state.play_preview.session.active()
                            ? renderer.viewer_area_door_hit(
                                  point.x, point.y, viewer_viewport->rect,
                                  nw::toolset::toolset_preview_door_handles(
                                      state.play_preview.session))
                            : std::nullopt;
                        const bool door_requires_interaction = door_hit
                            && nw::toolset::preview_door_requires_interaction(
                                nw::toolset::toolset_preview_door_visual_states(
                                    state.play_preview.session),
                                door_hit->door_index);
                        system_interface.SetMouseCursor(
                            state.play_preview.placement_pending()
                                ? "cross"
                                : door_requires_interaction ? "pointer"
                                                            : "arrow");
                        dispatched_to_rml = true;
                        break;
                    }
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
                    dispatched_to_rml = true;
                    break;
                }
                if (state.area_object_placement.active()
                    && state.area_object_placement.threshold_crossed) {
                    dispatched_to_rml = true;
                    break;
                }
                const auto point = to_context_point(window, event.wheel.mouse_x, event.wheel.mouse_y);
                if (command_palette_contains_point(
                        palette_doc, state, point)) {
                    RmlSDL::InputEventHandler(palette_context, window, event);
                    dispatched_to_rml = true;
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
                        && !state.module_dialog_open
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
                        dispatched_to_rml = true;
                        break;
                    }
                    if (auto viewer_viewport = active_workspace_viewer_viewport_request(doc, state, frame_width, frame_height);
                        viewer_viewport && point_within_viewport(viewer_viewport->rect, point)) {
                        if (state.play_preview.session.active()) {
                            state.play_preview.wheel_zoom += event.wheel.y;
                            dispatched_to_rml = true;
                            break;
                        }
                        const auto object = renderer.active_viewer_object();
                        const bool area_object_wheel = viewer_viewport->kind == WorkspaceViewerViewportKind::area
                            && (object.type == nw::ObjectType::creature
                                || object.type == nw::ObjectType::item
                                || object.type == nw::ObjectType::placeable)
                            && !focused_text_input(context)
                            && !state.module_dialog_open;
                        const bool sound_radius_wheel
                            = viewer_viewport->kind == WorkspaceViewerViewportKind::area
                            && object.type == nw::ObjectType::sound
                            && !focused_text_input(context)
                            && !state.module_dialog_open;
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
                            dispatched_to_rml = true;
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
                            dispatched_to_rml = true;
                            break;
                        }
                        renderer.zoom_viewer_viewport(event.wheel.y, viewer_viewport->rect);
                        if (state.area_workspace_surface == AreaWorkspaceSurface::tiles) {
                            state.area_tile_editor.cursor_update_pending = true;
                        }
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
                    dispatched_to_rml = true;
                    break;
                }
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
                if (state.managed_list_reorder.active()
                    && event.button.button == SDL_BUTTON_LEFT) {
                    const auto point = to_context_point(
                        window, event.button.x, event.button.y);
                    const bool was_dragging = state.managed_list_reorder.dragging;
                    (void)nw::toolset::update_managed_list_reorder(
                        state.managed_list_reorder, doc,
                        nw::toolset::ui_v1_host(), state.managed_lists,
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
                                state.managed_lists, true);
                        }
                    } else {
                        nw::toolset::clear_managed_list_reorder(
                            state.managed_list_reorder, doc);
                    }
                    system_interface.SetMouseCursor("arrow");
                    if (dragged) {
                        dispatched_to_rml = true;
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
                        state.pressed_recent_index = -1;
                        system_interface.SetMouseCursor("arrow");
                        dispatched_to_rml = true;
                        break;
                    }
                    cancel_project_blueprint_drag(doc, state);
                }
                if (state.project_blueprint_drag.active()
                    && state.project_blueprint_drag.threshold_crossed) {
                    dispatched_to_rml = true;
                    break;
                }
                if (state.area_object_placement.active()
                    && event.button.button == SDL_BUTTON_LEFT) {
                    if (state.area_object_placement.region_drawing) {
                        dispatched_to_rml = true;
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
                                state.pressed_recent_index = -1;
                                system_interface.SetMouseCursor("cross");
                            } else {
                                cancel_area_object_placement(renderer, state);
                                system_interface.SetMouseCursor("arrow");
                            }
                            dispatched_to_rml = true;
                            break;
                        }
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

                    if (command_palette_contains_point(
                            palette_doc, state, point)) {
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

                        if (auto* area_surface_tab
                            = find_ancestor_with_class(
                                hit, "area_workspace_tab")) {
                            const std::string surface
                                = area_surface_tab->GetAttribute<Rml::String>(
                                    "data-area-surface", "");
                            release_workspace_mouse_up();
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
                            release_workspace_mouse_up();
                            auto& editor = state.area_tile_editor;
                            if (nw::toolset::leave_area_tile_palette_folder(
                                    editor.palette)) {
                                cancel_area_tile_stroke(renderer, state);
                                clear_area_tile_selection(renderer, state);
                                reset_area_tile_palette_folder_view(editor);
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
                            release_workspace_mouse_up();
                            const auto row_key = parse_decimal_int32(
                                tile_row->GetAttribute<Rml::String>(
                                    "data-key", ""));
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
                                        reset_area_tile_palette_folder_view(
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
                                (void)close_active_smalls_selector(doc);
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
                        } else if (auto* option = find_ancestor_with_class(hit, "combobox_option")) {
                            const std::string key = option->GetAttribute<Rml::String>("data-key", "");
                            const auto value = parse_decimal_int32(key);
                            if (value
                                && state.object_details_combobox.is_active()
                                && find_ancestor_with_id(option,
                                    "object_details_combobox_popup")) {
                                release_workspace_mouse_up();
                                if (commit_object_details_sound_position(
                                        doc, state, *value)) {
                                    refresh_workspace_content(doc, state);
                                }
                            } else if (value
                                && active_creature_spell_filter_matches_tab(state)) {
                                release_workspace_mouse_up();
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
                                release_workspace_mouse_up();
                                if (open_object_details_sound_position_combobox(
                                        doc, state,
                                        static_cast<uint32_t>(*row_index))) {
                                    (void)sync_object_details_combobox(
                                        doc, state, true);
                                    sound_position_field->Focus();
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
                        } else if (auto* cycle_state = find_ancestor_with_class(
                                       hit, "object_details_cycle_state")) {
                            const auto row_index = parse_decimal_int32(
                                cycle_state->GetAttribute<Rml::String>("data-row", ""));
                            const auto current = parse_decimal_int32(
                                cycle_state->GetAttribute<Rml::String>("data-current", ""));
                            if (row_index && current && *row_index >= 0
                                && *current >= 0 && *current <= 2
                                && active_object_details_matches_tab(state)
                                && static_cast<size_t>(*row_index)
                                    < state.object_details.rows.size()) {
                                const auto& row = state.object_details.rows[static_cast<size_t>(*row_index)];
                                if (row.kind == nw::toolset::ObjectDetailsRowKind::value
                                    && row.editor == nw::toolset::ObjectDetailsEditorKind::door_state
                                    && row.edit_value == *current) {
                                    const std::string desired
                                        = std::to_string((*current + 1) % 3);
                                    append_command_result(state,
                                        dispatch_command(state,
                                            "object.details.set_integer",
                                            {std::to_string(*row_index),
                                                std::to_string(*current), desired},
                                            nw::toolset::CommandSource::widget));
                                }
                            }
                            release_workspace_mouse_up();
                            handled = true;
                        } else if (find_ancestor_with_id(
                                       hit, "sound_resource_add")) {
                            release_workspace_mouse_up();
                            (void)close_active_smalls_selector(doc);
                            close_appearance_selector(state);
                            state.sound_resource_selector_open = true;
                            state.sound_catalog_query.clear();
                            rebuild_sound_catalog(state, true);
                            state.sound_catalog_list.set_scroll_top(0);
                            refresh_workspace_content(doc, state);
                            sync_sound_catalog_window(doc, state, true);
                            if (auto* input
                                = find_el(doc, "sound_catalog_search")) {
                                input->Focus();
                            }
                            handled = true;
                        } else if (find_ancestor_with_id(
                                       hit, "sound_resource_selector_back")) {
                            release_workspace_mouse_up();
                            close_sound_resource_selector(state);
                            refresh_workspace_content(doc, state);
                            handled = true;
                        } else if (auto* sound_row
                            = find_ancestor_with_class(hit, "sound_catalog_row")) {
                            release_workspace_mouse_up();
                            const auto row_index = parse_decimal_int32(
                                sound_row->GetAttribute<Rml::String>(
                                    "data-key", ""));
                            if (row_index && *row_index >= 0
                                && commit_sound_catalog_selection(state,
                                    static_cast<uint32_t>(*row_index))) {
                                close_sound_resource_selector(state);
                                refresh_workspace_content(doc, state);
                            }
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
                            clear_creature_spell_filter(state);
                            clear_color_editor(state);
                            (void)close_active_smalls_selector(doc);
                            close_appearance_selector(state);
                            close_sound_resource_selector(state);
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
                                    || state.object_details.object.type == nw::ObjectType::door
                                    || state.object_details.object.type == nw::ObjectType::item)) {
                                state.object_workbench_surface = ObjectWorkbenchSurface::appearance;
                                if (appearance_catalog_kind(
                                        state.object_details.object.type)) {
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
                                && object_has_grid_inventory(state.object_details.object.type)) {
                                state.object_workbench_surface = ObjectWorkbenchSurface::inventory;
                            } else if (surface == "spawns"
                                && state.object_details.object.type == nw::ObjectType::encounter) {
                                state.object_workbench_surface = ObjectWorkbenchSurface::spawns;
                            } else if (surface == "sounds"
                                && state.object_details.object.type == nw::ObjectType::sound) {
                                state.object_workbench_surface = ObjectWorkbenchSurface::sounds;
                            } else if (surface == "store-inventory"
                                && state.object_details.object.type == nw::ObjectType::store) {
                                state.object_workbench_surface = ObjectWorkbenchSurface::store_inventory;
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
                        } else if (find_ancestor_with_id(
                                       hit, "placeable_appearance_previous")) {
                            release_workspace_mouse_up();
                            if (cycle_active_appearance(state, -1)) {
                                rebuild_active_appearances(
                                    state, state.object_details.object);
                                refresh_workspace_content(doc, state);
                            }
                            handled = true;
                        } else if (find_ancestor_with_id(
                                       hit, "placeable_appearance_next")) {
                            release_workspace_mouse_up();
                            if (cycle_active_appearance(state, 1)) {
                                rebuild_active_appearances(
                                    state, state.object_details.object);
                                refresh_workspace_content(doc, state);
                            }
                            handled = true;
                        } else if (find_ancestor_with_id(
                                       hit, "door_appearance_previous")) {
                            release_workspace_mouse_up();
                            if (cycle_active_appearance(state, -1)) {
                                rebuild_active_appearances(
                                    state, state.object_details.object);
                                refresh_workspace_content(doc, state);
                            }
                            handled = true;
                        } else if (find_ancestor_with_id(
                                       hit, "door_appearance_next")) {
                            release_workspace_mouse_up();
                            if (cycle_active_appearance(state, 1)) {
                                rebuild_active_appearances(
                                    state, state.object_details.object);
                                refresh_workspace_content(doc, state);
                            }
                            handled = true;
                        } else if (auto* catalog_field = find_ancestor_with_class(hit, "appearance_catalog_field")) {
                            const auto selected_field = appearance_editor_field_from_name(
                                catalog_field->GetAttribute<Rml::String>("data-field", ""));
                            if (selected_field && active_appearances_match_tab(state)
                                && (*selected_field == AppearanceEditorField::appearance
                                    || state.object_details.object.type == nw::ObjectType::creature)) {
                                release_workspace_mouse_up();
                                (void)close_active_smalls_selector(doc);
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
                        } else if (const auto activation = activate_managed_list(
                                       doc, state, hit);
                            activation.activated) {
                            release_workspace_mouse_up();
                            if (activation.focus_target) {
                                (void)nw::toolset::focus_managed_list_target(
                                    doc, *activation.focus_target);
                            }
                            handled = true;
                        } else if (auto* area_card = find_ancestor_with_class(hit, "home_area_card")) {
                            const auto index = parse_decimal_int32(
                                area_card->GetAttribute<Rml::String>("data-key", ""));
                            if (index && *index >= 0
                                && static_cast<size_t>(*index) < state.home_areas.size()
                                && ensure_backend_ready(state)) {
                                const std::string resref = state.home_areas[static_cast<size_t>(*index)].resref;
                                release_workspace_mouse_up();
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
                            if (index && *index >= 0 && static_cast<size_t>(*index) < state.recent_projects.size()) {
                                release_workspace_mouse_up();
                                auto previous = state.recent_projects;
                                const std::array indices{static_cast<size_t>(*index)};
                                if (nw::toolset::forget_recent_projects(state.recent_projects, indices)
                                    && !save_ui_preferences(state)) {
                                    state.recent_projects = std::move(previous);
                                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Unable to remove recent project",
                                        "Could not save preferences. The project was kept in the recent list.", window);
                                }
                                refresh_workspace_content(doc, state);
                            }
                            handled = true;
                        } else if (auto* project_item = find_ancestor_with_class(hit, "home_project_item")) {
                            const auto index = parse_decimal_int32(project_item->GetAttribute<Rml::String>("data-key", ""));
                            if (index && *index >= 0 && static_cast<size_t>(*index) < state.recent_projects.size()) {
                                nw::toolset::refresh_recent_projects(state.recent_projects);
                                const auto project = state.recent_projects[static_cast<size_t>(*index)];
                                release_workspace_mouse_up();
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
                                                        release_workspace_mouse_up();
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
                                                release_workspace_mouse_up();
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
                                            release_workspace_mouse_up();
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
                    state.pressed_recent_index = -1;
                    if (handled) {
                        dispatched_to_rml = true;
                    }
                }
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
            case SDL_EVENT_WINDOW_MOUSE_LEAVE:
                if (state.project_blueprint_drag.active()) {
                    cancel_project_blueprint_drag(doc, state);
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
        if (state.appearance_body_preview_object.type != nw::ObjectType::invalid
            && state.active_object_tab_id.empty()
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
                    state.active_object_tab_id = state.workspace.active_tab_id();
                    state.object_workbench_surface = default_object_workbench_surface();
                    state.managed_list_reorder = {};
                    clear_active_appearances(state);
                    clear_active_sound_catalog(state);
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
                        if (object_has_grid_inventory(mutation.object.type)) {
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
                    if (state.appearance_body_preview_object == mutation.object
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
                    } else if (object_has_grid_inventory(mutation.object.type)) {
                        rebuild_active_creature_inventory(state, mutation.object);
                    }
                    const bool smalls_appearance_mutation = mutation.object.type == nw::ObjectType::door
                        || mutation.object.type == nw::ObjectType::item;
                    if (state.object_workbench_surface == ObjectWorkbenchSurface::appearance
                        && appearance_catalog_kind(mutation.object.type)
                        && mutation.object.type != nw::ObjectType::placeable) {
                        rebuild_active_appearances(state, mutation.object);
                        if (mutation.kind == nw::toolset::ObjectMutationKind::visual) {
                            refresh_workspace_content(doc, state);
                            workbench_rebuilt = true;
                        }
                    }
                    if (smalls_appearance_mutation
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
            if (mutation_focus_target
                && nw::toolset::focus_managed_list_target(
                    doc, *mutation_focus_target)) {
                state.viewer_viewport_focused = false;
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
        if (active_sound_resource_selector_matches_tab(state)) {
            const std::string query
                = get_input_value(doc, "sound_catalog_search");
            if (query != state.sound_catalog_query) {
                state.sound_catalog_query = query;
                rebuild_sound_catalog(state, true);
                state.sound_catalog_list.set_scroll_top(0);
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
            if (area_query != state.home_area_query) {
                state.home_area_query = area_query;
                refresh_home_area_catalog(state, true);
                sync_home_area_window(doc, state, true);
            } else {
                sync_home_area_window(doc, state, false);
            }
        }

        const std::string recent_query = get_input_value(doc, "recent_search");
        sync_command_form(state);
        if (recent_query != state.last_recent_query
            || (state.backend_ready && state.project_resource_generation != nw::kernel::resman().generation())) {
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
        flush_area_tile_cursor_update(
            renderer, window, context, doc, state, frame_width, frame_height);

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
        if (sync_sound_catalog_window(doc, state, false)) {
            context->Update();
        }
        if (nw::toolset::sync_managed_lists(doc,
                nw::toolset::ui_v1_host(), state.managed_lists, false)) {
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
            const auto frame_sample = sample_play_preview_input(
                state.play_preview, raw_frame_delta_seconds);
            const auto fixed_stats = nw::toolset::build_preview_tick_samples(
                state.play_preview.fixed_step,
                raw_frame_delta_seconds,
                frame_sample,
                state.play_preview.tick_inputs);
            if (fixed_stats.status != nw::toolset::PreviewStatus::ok) {
                append_output(state, "error",
                    "Play-preview input sampling failed");
                stop_play_preview(renderer, system_interface, doc, state);
            } else if (fixed_stats.tick_count > 0) {
                const auto tick_stats = nw::toolset::tick_toolset_preview(
                    state.play_preview.session,
                    std::span{state.play_preview.tick_inputs}.first(
                        fixed_stats.tick_count),
                    state.play_preview.spatial_rows,
                    state.play_preview.locomotion_rows);
                const bool tick_ok
                    = tick_stats.status == nw::toolset::PreviewStatus::ok;
                const bool visual_ok = tick_ok
                    && renderer.update_toolset_preview_visuals(
                        std::span{state.play_preview.spatial_rows}.first(
                            tick_stats.output_count),
                        std::span{state.play_preview.locomotion_rows}.first(
                            tick_stats.output_count),
                        nw::toolset::toolset_preview_door_visual_states(
                            state.play_preview.session),
                        state.play_preview.session.camera());
                const auto navigation_debug
                    = nw::toolset::toolset_preview_navigation_debug(
                        state.play_preview.session);
                const bool navigation_debug_ok = !navigation_debug.enabled
                    || renderer.update_toolset_preview_navigation_debug(
                        navigation_debug);
                state.play_preview.pending_input.flags
                    &= ~(nw::toolset::preview_input_click_target
                        | nw::toolset::preview_input_cancel
                        | nw::toolset::preview_input_click_door);
                state.play_preview.mouse_look_x = 0.0f;
                state.play_preview.mouse_look_y = 0.0f;
                state.play_preview.mouse_sample_seconds = 0.0;
                state.play_preview.wheel_zoom = 0.0f;
                if (!tick_ok || !visual_ok || !navigation_debug_ok) {
                    append_output(state, "error",
                        !tick_ok         ? "Play-preview simulation failed"
                            : !visual_ok ? "Failed to update play-preview visuals"
                                         : "Failed to update navigation debug geometry");
                    stop_play_preview(renderer, system_interface, doc, state);
                }
            }
        }
        auto* viewer_tab = state.workspace.active_tab();
        const auto viewer_project_dir = state.backend.current_project_dir();
        const bool data_workbench_preview = !viewer_viewport
            && viewer_tab
            && viewer_tab->kind == nw::toolset::WorkspaceTabKind::preview
            && !viewer_tab->detail.empty()
            && !viewer_project_dir.empty()
            && data_workbench_only(state.object_details.object.type,
                state.object_workbench_surface);
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
                        = state.active_object_tab_id == viewer_tab->id
                        && state.object_details.status
                            == nw::toolset::ObjectDetailsStatus::ready
                        && renderer.active_viewer_object()
                            == state.object_details.object;
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
                    const bool object_changed = state.object_details.object != object
                        || state.active_object_tab_id != active_tab_id
                        || state.object_details.status != nw::toolset::ObjectDetailsStatus::ready;
                    state.active_object_tab_id = active_tab_id;
                    if (object_changed) {
                        state.object_workbench_surface = default_object_workbench_surface();
                        state.managed_list_reorder = {};
                        clear_active_appearances(state);
                        clear_active_sound_catalog(state);
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
                            if (object_has_grid_inventory(object.type)) {
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
        sync_play_preview_viewport_overlay(fps_doc, viewer_viewport, state);
        {
            const ScopedClientGpuTimer gpu_timer{renderer, kClientGpuTimerOverlay};
            fps_context->Update();
            fps_context->Render();
        }
        const Uint64 overlay_end_counter = SDL_GetPerformanceCounter();
        Uint64 palette_end_counter = overlay_end_counter;
        if (state.shell.command_palette_visible || state.command_form
            || state.project_load.active()
            || state.backend.blueprint_operation_active() || state.backend.blueprint_publication_pending()) {
            const ScopedClientGpuTimer gpu_timer{renderer, kClientGpuTimerPalette};
            palette_context->Update();
            palette_context->Render();
            if (state.project_load.active()) {
                state.project_load.presented = true;
            }
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

    cancel_project_blueprint_drag(doc, state);
    cancel_area_object_placement(renderer, state);
    stop_play_preview(renderer, system_interface, doc, state);
    close_play_preview_gamepad(state.play_preview);
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
        "change", &object_workbench_change_listener, false);
    context->RemoveEventListener(
        "blur", &object_workbench_change_listener, true);
    context->RemoveEventListener("click", &home_project_action_listener);
    palette_context->RemoveEventListener("click", &blueprint_action_listener);
    state.backend.shutdown_item_editor_data_model();
    state.rml_smalls_data_model->shutdown();
    Rml::RemoveContext("command_palette");
    Rml::RemoveContext("viewer_fps");
    Rml::RemoveContext("toolset");
    Rml::Shutdown();
    renderer.shutdown();
    state.workspace.clear();
    nw::kernel::services().shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
