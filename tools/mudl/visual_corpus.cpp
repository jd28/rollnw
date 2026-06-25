#include "visual_corpus.hpp"

#include "app_runtime.hpp"
#include "mudl_cli.hpp"
#include "mudl_commands.hpp"
#include "particle_tools.hpp"
#include "viewer_runtime.hpp"

#include <nw/log.hpp>
#include <nw/render/nwn/model_loader.hpp>
#include <nw/render/viewer/preview_render_resources.hpp>

#include <SDL3/SDL.h>

#include <glm/glm.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace mudl {
namespace {

using json = nlohmann::json;
using nw::render::nwn::set_dangly_debug_scale;
using nw::render::nwn::set_dangly_mode;

enum class VisualCorpusKind {
    area,
    creature,
    item,
    model,
    spell,
};

enum class VisualCorpusCameraMode {
    none,
    orbit,
    free,
    area_gameplay,
};

struct VisualCorpusCamera {
    VisualCorpusCameraMode mode = VisualCorpusCameraMode::none;
    std::optional<glm::vec3> position;
    std::optional<glm::vec3> target;
    float radius = 0.0f;
    float yaw_degrees = -90.0f;
    float pitch_degrees = 20.0f;
    float fov_degrees = 0.0f;
};

struct VisualCorpusStaticPbrEnvironment {
    std::optional<std::string> environment_path;
    std::optional<bool> ibl_requested;
};

struct VisualCorpusEntry {
    std::string id;
    VisualCorpusKind kind = VisualCorpusKind::creature;
    std::string input;
    std::string animation;
    ParticlePreviewView view = ParticlePreviewView::front;
    VisualCorpusCamera camera;
    VisualCorpusStaticPbrEnvironment static_pbr_environment;
    int frame = 0;
    std::vector<std::string> category;
    std::string notes;
};

struct VisualCorpus {
    std::string name;
    std::string description;
    std::vector<VisualCorpusEntry> entries;
};

struct KernelSession {
    bool initialized = false;

    ~KernelSession()
    {
        if (initialized) {
            nw::kernel::services().shutdown();
        }
    }
};

struct HeadlessViewerSession {
    AppState state;
    std::string default_static_pbr_environment_path;
    bool default_static_pbr_ibl_requested = true;
    bool sdl_initialized = false;
    bool graphics_initialized = false;
    bool render_runtime_initialized = false;
    bool preview_resources_initialized = false;

    ~HeadlessViewerSession()
    {
        replace_current_scene_after_gpu_idle(state, {});
        reset_viewer_renderers(state);
        state.shader_provider.reset();
        state.camera.reset();
        if (graphics_initialized) {
            shutdown_graphics(state);
        }
        if (sdl_initialized) {
            SDL_Quit();
        }
    }
};

std::string normalize_token(std::string_view value)
{
    std::string result;
    result.reserve(value.size());
    for (char ch : value) {
        if (std::isalnum(static_cast<unsigned char>(ch))) {
            result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
    }
    return result;
}

std::string effective_pbr_environment_path(std::string_view value)
{
    return value.empty() ? std::string{kDefaultStaticPbrEnvironmentPath} : std::string{value};
}

std::string_view visual_corpus_kind_name(VisualCorpusKind kind)
{
    switch (kind) {
    case VisualCorpusKind::area:
        return "area";
    case VisualCorpusKind::creature:
        return "creature";
    case VisualCorpusKind::item:
        return "item";
    case VisualCorpusKind::model:
        return "model";
    case VisualCorpusKind::spell:
        return "spell";
    }
    return "creature";
}

std::optional<VisualCorpusKind> parse_visual_corpus_kind(std::string_view value)
{
    const auto normalized = normalize_token(value);
    if (normalized == "area") {
        return VisualCorpusKind::area;
    }
    if (normalized == "creature") {
        return VisualCorpusKind::creature;
    }
    if (normalized == "item") {
        return VisualCorpusKind::item;
    }
    if (normalized == "model") {
        return VisualCorpusKind::model;
    }
    if (normalized == "spell") {
        return VisualCorpusKind::spell;
    }
    return std::nullopt;
}

std::optional<ParticlePreviewView> parse_particle_preview_view(std::string_view value)
{
    const auto normalized = normalize_token(value);
    if (normalized.empty() || normalized == "front") {
        return ParticlePreviewView::front;
    }
    if (normalized == "top") {
        return ParticlePreviewView::top;
    }
    if (normalized == "side") {
        return ParticlePreviewView::side;
    }
    return std::nullopt;
}

std::optional<VisualCorpusCameraMode> parse_visual_corpus_camera_mode(std::string_view value)
{
    const auto normalized = normalize_token(value);
    if (normalized.empty() || normalized == "none" || normalized == "fit") {
        return VisualCorpusCameraMode::none;
    }
    if (normalized == "orbit") {
        return VisualCorpusCameraMode::orbit;
    }
    if (normalized == "free" || normalized == "custom") {
        return VisualCorpusCameraMode::free;
    }
    if (normalized == "areagameplay" || normalized == "gameplay") {
        return VisualCorpusCameraMode::area_gameplay;
    }
    return std::nullopt;
}

std::string_view visual_corpus_camera_mode_name(VisualCorpusCameraMode mode)
{
    switch (mode) {
    case VisualCorpusCameraMode::none:
        return "none";
    case VisualCorpusCameraMode::orbit:
        return "orbit";
    case VisualCorpusCameraMode::free:
        return "free";
    case VisualCorpusCameraMode::area_gameplay:
        return "area_gameplay";
    }
    return "none";
}

bool read_finite_float_field(const json& object, const char* field, float& value, const std::string& entry_id)
{
    const auto it = object.find(field);
    if (it == object.end()) {
        return true;
    }
    if (!it->is_number()) {
        LOG_F(ERROR, "Corpus entry '{}' camera.{} must be a number", entry_id, field);
        return false;
    }
    value = it->get<float>();
    if (!std::isfinite(value)) {
        LOG_F(ERROR, "Corpus entry '{}' camera.{} must be finite", entry_id, field);
        return false;
    }
    return true;
}

bool read_vec3_field(const json& object, const char* field, std::optional<glm::vec3>& value,
    const std::string& entry_id)
{
    const auto it = object.find(field);
    if (it == object.end()) {
        return true;
    }
    if (!it->is_array() || it->size() != 3) {
        LOG_F(ERROR, "Corpus entry '{}' camera.{} must be a three-number array", entry_id, field);
        return false;
    }

    glm::vec3 parsed{0.0f};
    for (size_t i = 0; i < 3; ++i) {
        if (!(*it)[i].is_number()) {
            LOG_F(ERROR, "Corpus entry '{}' camera.{} must be a three-number array", entry_id, field);
            return false;
        }
        parsed[static_cast<glm::length_t>(i)] = (*it)[i].get<float>();
        if (!std::isfinite(parsed[static_cast<glm::length_t>(i)])) {
            LOG_F(ERROR, "Corpus entry '{}' camera.{} values must be finite", entry_id, field);
            return false;
        }
    }
    value = parsed;
    return true;
}

std::optional<VisualCorpusCamera> parse_visual_corpus_camera(const json& item, const std::string& entry_id)
{
    VisualCorpusCamera camera;
    const auto camera_it = item.find("camera");
    if (camera_it == item.end() || camera_it->is_null()) {
        return camera;
    }
    if (!camera_it->is_object()) {
        LOG_F(ERROR, "Corpus entry '{}' camera must be an object", entry_id);
        return std::nullopt;
    }

    std::string camera_mode;
    if (const auto mode_it = camera_it->find("mode"); mode_it != camera_it->end()) {
        if (!mode_it->is_string()) {
            LOG_F(ERROR, "Corpus entry '{}' camera.mode must be a string", entry_id);
            return std::nullopt;
        }
        camera_mode = mode_it->get<std::string>();
    }
    const auto mode = parse_visual_corpus_camera_mode(camera_mode);
    if (!mode) {
        LOG_F(ERROR, "Corpus entry '{}' has unsupported camera mode", entry_id);
        return std::nullopt;
    }
    camera.mode = *mode;

    if (!read_vec3_field(*camera_it, "position", camera.position, entry_id)
        || !read_vec3_field(*camera_it, "target", camera.target, entry_id)
        || !read_finite_float_field(*camera_it, "radius", camera.radius, entry_id)
        || !read_finite_float_field(*camera_it, "yaw", camera.yaw_degrees, entry_id)
        || !read_finite_float_field(*camera_it, "pitch", camera.pitch_degrees, entry_id)
        || !read_finite_float_field(*camera_it, "fov", camera.fov_degrees, entry_id)) {
        return std::nullopt;
    }

    if (camera.fov_degrees < 0.0f) {
        LOG_F(ERROR, "Corpus entry '{}' camera.fov must be non-negative", entry_id);
        return std::nullopt;
    }
    if (camera.fov_degrees > 0.0f) {
        camera.fov_degrees = std::clamp(camera.fov_degrees, 10.0f, 120.0f);
    }
    if (camera.mode == VisualCorpusCameraMode::orbit && camera.radius <= 0.0f) {
        LOG_F(ERROR, "Corpus entry '{}' camera.radius must be positive for orbit mode", entry_id);
        return std::nullopt;
    }
    if (camera.mode == VisualCorpusCameraMode::free && (!camera.position || !camera.target)) {
        LOG_F(ERROR, "Corpus entry '{}' free camera requires position and target", entry_id);
        return std::nullopt;
    }
    if (camera.position && camera.target
        && glm::dot(*camera.target - *camera.position, *camera.target - *camera.position) <= 1.0e-8f) {
        LOG_F(ERROR, "Corpus entry '{}' camera position and target must be distinct", entry_id);
        return std::nullopt;
    }
    return camera;
}

std::optional<VisualCorpusStaticPbrEnvironment> parse_visual_corpus_static_pbr_environment(
    const json& item, const std::string& entry_id)
{
    VisualCorpusStaticPbrEnvironment environment;
    const auto env_it = item.find("static_pbr_environment");
    if (env_it == item.end() || env_it->is_null()) {
        return environment;
    }
    if (!env_it->is_object()) {
        LOG_F(ERROR, "Corpus entry '{}' static_pbr_environment must be an object", entry_id);
        return std::nullopt;
    }

    if (const auto path_it = env_it->find("environment_path"); path_it != env_it->end()) {
        if (!path_it->is_string()) {
            LOG_F(ERROR, "Corpus entry '{}' static_pbr_environment.environment_path must be a string", entry_id);
            return std::nullopt;
        }
        auto path = path_it->get<std::string>();
        if (path.empty()) {
            LOG_F(ERROR, "Corpus entry '{}' static_pbr_environment.environment_path must be non-empty", entry_id);
            return std::nullopt;
        }
        environment.environment_path = std::move(path);
    }

    if (const auto ibl_it = env_it->find("ibl_requested"); ibl_it != env_it->end()) {
        if (!ibl_it->is_boolean()) {
            LOG_F(ERROR, "Corpus entry '{}' static_pbr_environment.ibl_requested must be a boolean", entry_id);
            return std::nullopt;
        }
        environment.ibl_requested = ibl_it->get<bool>();
    }

    return environment;
}

bool write_text_file(const std::filesystem::path& path, std::string_view text)
{
    std::error_code ec;
    if (const auto parent = path.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            LOG_F(ERROR, "Failed to create output directory '{}': {}", parent.string(), ec.message());
            return false;
        }
    }

    std::ofstream output{path};
    if (!output.is_open()) {
        LOG_F(ERROR, "Failed to open output path '{}'", path.string());
        return false;
    }

    output << text;
    if (!output.good()) {
        LOG_F(ERROR, "Failed to write output path '{}'", path.string());
        return false;
    }

    return true;
}

std::string iso8601_now_utc()
{
    const auto now = std::chrono::system_clock::now();
    const auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    char buffer[32]{};
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm) == 0) {
        return {};
    }
    return buffer;
}

std::optional<VisualCorpus> load_visual_corpus(const std::filesystem::path& path)
{
    std::ifstream input{path};
    if (!input.is_open()) {
        LOG_F(ERROR, "Failed to open corpus file '{}'", path.string());
        return std::nullopt;
    }

    json payload;
    try {
        input >> payload;
    } catch (const std::exception& ex) {
        LOG_F(ERROR, "Failed to parse corpus file '{}': {}", path.string(), ex.what());
        return std::nullopt;
    }

    if (!payload.is_object()) {
        LOG_F(ERROR, "Corpus file '{}' must contain a JSON object", path.string());
        return std::nullopt;
    }

    VisualCorpus corpus;
    corpus.name = payload.value("name", path.stem().string());
    corpus.description = payload.value("description", "");

    const auto entries_it = payload.find("entries");
    if (entries_it == payload.end() || !entries_it->is_array()) {
        LOG_F(ERROR, "Corpus file '{}' is missing an 'entries' array", path.string());
        return std::nullopt;
    }

    for (const auto& item : *entries_it) {
        if (!item.is_object()) {
            LOG_F(ERROR, "Corpus entry in '{}' must be an object", path.string());
            return std::nullopt;
        }

        VisualCorpusEntry entry;
        entry.id = item.value("id", "");
        entry.input = item.value("input", "");
        entry.animation = item.value("animation", "");
        entry.frame = std::max(0, item.value("frame", 0));
        entry.notes = item.value("notes", "");

        const auto kind = parse_visual_corpus_kind(item.value("kind", ""));
        if (!kind) {
            LOG_F(ERROR, "Corpus entry '{}' has unsupported kind", entry.id.empty() ? "<unnamed>" : entry.id);
            return std::nullopt;
        }
        entry.kind = *kind;

        if (auto view = parse_particle_preview_view(item.value("view", "front"))) {
            entry.view = *view;
        } else {
            LOG_F(ERROR, "Corpus entry '{}' has unsupported view", entry.id);
            return std::nullopt;
        }

        auto camera = parse_visual_corpus_camera(item, entry.id.empty() ? "<unnamed>" : entry.id);
        if (!camera) {
            return std::nullopt;
        }
        entry.camera = *camera;

        auto static_pbr_environment = parse_visual_corpus_static_pbr_environment(
            item, entry.id.empty() ? "<unnamed>" : entry.id);
        if (!static_pbr_environment) {
            return std::nullopt;
        }
        entry.static_pbr_environment = std::move(*static_pbr_environment);

        if (const auto categories = item.find("category"); categories != item.end()) {
            if (!categories->is_array()) {
                LOG_F(ERROR, "Corpus entry '{}' has non-array category field", entry.id);
                return std::nullopt;
            }
            for (const auto& category : *categories) {
                if (category.is_string()) {
                    entry.category.push_back(category.get<std::string>());
                }
            }
        }

        if (entry.id.empty() || entry.input.empty()) {
            LOG_F(ERROR, "Corpus entries in '{}' require non-empty id and input", path.string());
            return std::nullopt;
        }

        corpus.entries.push_back(std::move(entry));
    }

    return corpus;
}

bool visual_corpus_entry_matches_filter(const VisualCorpusEntry& entry, std::string_view filter)
{
    if (filter.empty()) {
        return true;
    }

    const auto normalized_filter = normalize_token(filter);
    if (normalized_filter.empty()) {
        return true;
    }

    if (normalize_token(entry.id).find(normalized_filter) != std::string::npos) {
        return true;
    }
    if (normalize_token(visual_corpus_kind_name(entry.kind)).find(normalized_filter) != std::string::npos) {
        return true;
    }
    for (const auto& category : entry.category) {
        if (normalize_token(category).find(normalized_filter) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool initialize_kernel_session(KernelSession& session, std::string_view module_path, std::string_view user_path)
{
    if (!init_kernel_services(module_path, user_path)) {
        return false;
    }
    session.initialized = true;
    return true;
}

bool initialize_headless_viewer_session(HeadlessViewerSession& session, std::string_view pbr_environment_path,
    bool pbr_ibl_enabled)
{
    session.default_static_pbr_environment_path = effective_pbr_environment_path(pbr_environment_path);
    session.default_static_pbr_ibl_requested = pbr_ibl_enabled;
    session.state.static_pbr_environment_path = session.default_static_pbr_environment_path;
    session.state.static_pbr_ibl_requested = session.default_static_pbr_ibl_requested;

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOG_F(ERROR, "SDL_Init failed: {}", SDL_GetError());
        return false;
    }
    session.sdl_initialized = true;

    if (!init_graphics(session.state, nullptr)) {
        LOG_F(ERROR, "Failed to initialize graphics");
        return false;
    }
    session.graphics_initialized = true;

    session.state.camera = std::make_unique<nw::render::viewer::Camera>();
    session.state.camera->set_aspect_ratio(1280.0f / 720.0f);

    if (!init_render_runtime(session.state)) {
        LOG_F(ERROR, "Failed to initialize render runtime");
        return false;
    }
    session.render_runtime_initialized = true;

    if (!init_viewer_renderers(session.state)) {
        LOG_F(ERROR, "Failed to initialize viewer renderers");
        return false;
    }
    session.preview_resources_initialized = true;

    set_dangly_debug_scale(session.state.dangly_scale);
    set_dangly_mode(session.state.dangly_mode);
    return true;
}

json vec3_json(const glm::vec3& value)
{
    return {
        {"x", value.x},
        {"y", value.y},
        {"z", value.z},
    };
}

json visual_corpus_camera_json(const VisualCorpusCamera& camera)
{
    json payload = {
        {"mode", std::string{visual_corpus_camera_mode_name(camera.mode)}},
    };

    if (camera.mode == VisualCorpusCameraMode::free && camera.position) {
        payload["position"] = vec3_json(*camera.position);
    }
    if ((camera.mode == VisualCorpusCameraMode::orbit || camera.mode == VisualCorpusCameraMode::free)
        && camera.target) {
        payload["target"] = vec3_json(*camera.target);
    }
    if (camera.mode == VisualCorpusCameraMode::orbit && camera.radius > 0.0f) {
        payload["radius"] = camera.radius;
        payload["yaw"] = camera.yaw_degrees;
        payload["pitch"] = camera.pitch_degrees;
    }
    if (camera.fov_degrees > 0.0f) {
        payload["fov"] = camera.fov_degrees;
    }
    return payload;
}

json visual_corpus_static_pbr_environment_json(const VisualCorpusStaticPbrEnvironment& environment)
{
    json payload = json::object();
    if (environment.environment_path) {
        payload["environment_path"] = *environment.environment_path;
    }
    if (environment.ibl_requested) {
        payload["ibl_requested"] = *environment.ibl_requested;
    }
    return payload;
}

void apply_visual_corpus_static_pbr_environment(HeadlessViewerSession& session, const VisualCorpusEntry& entry)
{
    session.state.static_pbr_environment_path = entry.static_pbr_environment.environment_path.value_or(
        session.default_static_pbr_environment_path);
    session.state.static_pbr_ibl_requested = entry.static_pbr_environment.ibl_requested.value_or(
        session.default_static_pbr_ibl_requested);
}

void apply_visual_corpus_camera(AppState& state, const VisualCorpusEntry& entry)
{
    if (!state.camera || entry.camera.mode == VisualCorpusCameraMode::none) {
        return;
    }

    auto& camera = *state.camera;
    if (entry.camera.fov_degrees > 0.0f) {
        camera.set_fov(entry.camera.fov_degrees);
    }

    switch (entry.camera.mode) {
    case VisualCorpusCameraMode::none:
        break;
    case VisualCorpusCameraMode::orbit:
        camera.set_orbit_view(
            entry.camera.target.value_or(camera.get_target()),
            entry.camera.radius,
            entry.camera.yaw_degrees,
            entry.camera.pitch_degrees,
            nw::render::viewer::Camera::ProjectionMode::perspective);
        break;
    case VisualCorpusCameraMode::free:
        camera.set_free_view(
            *entry.camera.position,
            *entry.camera.target,
            nw::render::viewer::Camera::ProjectionMode::perspective);
        break;
    case VisualCorpusCameraMode::area_gameplay:
        if (state.current_scene) {
            camera.set_area_gameplay_view(
                state.current_scene->current_bounds(),
                entry.camera.fov_degrees > 0.0f ? entry.camera.fov_degrees : 65.0f);
        }
        break;
    }
}

bool capture_viewer_preview(HeadlessViewerSession& session, const VisualCorpusEntry& entry,
    const std::filesystem::path& out_path)
{
    apply_visual_corpus_static_pbr_environment(session, entry);
    session.state.animation_override = entry.animation;
    switch (entry.kind) {
    case VisualCorpusKind::area:
        load_area(session.state, entry.input);
        break;
    case VisualCorpusKind::creature:
    case VisualCorpusKind::item:
    case VisualCorpusKind::model:
        load_model(session.state, entry.input);
        break;
    case VisualCorpusKind::spell:
        return false;
    }
    if (!session.state.current_scene) {
        LOG_F(ERROR, "Failed to load visual preview '{}'", entry.input);
        return false;
    }

    if (entry.kind != VisualCorpusKind::area && !entry.animation.empty()
        && !select_model_animation(session.state, 0, entry.animation)) {
        LOG_F(ERROR, "Failed to select animation '{}' for '{}'", entry.animation, entry.input);
        return false;
    }

    apply_visual_corpus_camera(session.state, entry);

    if (!run_screenshot_capture(session.state, out_path)) {
        LOG_F(ERROR, "Failed to capture visual preview '{}'", entry.input);
        return false;
    }

    std::string loaded_animation;
    if (!session.state.current_scene->models.empty()
        && session.state.current_scene->models.front()
        && session.state.current_scene->models.front()->anim_) {
        loaded_animation = session.state.current_scene->models.front()->anim_->name;
    }

    json metadata = {
        {"id", entry.id},
        {"kind", visual_corpus_kind_name(entry.kind)},
        {"input", entry.input},
        {"animation_requested", entry.animation},
        {"animation_loaded", loaded_animation},
        {"screenshot", out_path.filename().string()},
        {"static_pbr_environment_policy",
            std::string{static_pbr_environment_policy_name(session.state.static_pbr_environment_policy)}},
    };
    if (entry.camera.mode != VisualCorpusCameraMode::none) {
        metadata["camera"] = visual_corpus_camera_json(entry.camera);
    }
    if (entry.static_pbr_environment.environment_path || entry.static_pbr_environment.ibl_requested) {
        metadata["static_pbr_environment_override"] = visual_corpus_static_pbr_environment_json(
            entry.static_pbr_environment);
    }
    if (!session.state.current_scene->static_models.empty()) {
        metadata["pbr_environment_path"] = session.state.static_pbr_environment_path;
        metadata["pbr_ibl_enabled"] = session.state.static_pbr_ibl_requested;
        metadata["static_pbr_environment_path"] = session.state.static_pbr_environment_path;
        metadata["static_pbr_ibl_requested"] = session.state.static_pbr_ibl_requested;
        metadata["static_pbr_ibl_enabled"] = session.state.static_pbr_ibl_enabled;
        metadata["static_pbr_ibl_active"] = static_pbr_ibl_active(session.state);
        metadata["static_pbr_ibl_strength"] = session.state.static_pbr_ibl_strength;
        metadata["static_pbr_exposure"] = session.state.static_pbr_exposure;
    }
    return write_text_file(out_path.parent_path() / "preview.json", metadata.dump(2));
}

bool run_spell_corpus_entry(const VisualCorpusEntry& entry, const std::filesystem::path& entry_dir)
{
    auto sequence = resolve_spell_sequence(entry.input);
    if (!sequence) {
        LOG_F(ERROR, "Failed to resolve spell sequence '{}'", entry.input);
        return false;
    }

    AppState export_state{};
    if (run_spell_export_live_command(export_state, *sequence, entry.input, entry_dir / "live.json") != 0) {
        return false;
    }

    AppState preview_state{};
    ParticlePreviewMetadataOptions metadata;
    metadata.write = true;
    if (run_spell_preview_live_command(preview_state, *sequence, entry.input, entry_dir / "preview.png",
            entry.frame, entry.view, metadata)
        != 0) {
        return false;
    }

    return true;
}

json make_status_json(const VisualCorpusEntry& entry, std::string_view result, const std::filesystem::path& entry_dir,
    std::string_view detail = {})
{
    json outputs = json::array();
    std::error_code ec;
    for (const auto& child : std::filesystem::directory_iterator(entry_dir, ec)) {
        if (ec) {
            break;
        }
        if (child.is_regular_file()) {
            outputs.push_back(child.path().filename().string());
        }
    }

    json payload = {
        {"id", entry.id},
        {"kind", visual_corpus_kind_name(entry.kind)},
        {"input", entry.input},
        {"result", result},
        {"outputs", outputs},
        {"category", entry.category},
        {"notes", entry.notes},
    };
    if (!detail.empty()) {
        payload["detail"] = detail;
    }
    return payload;
}

bool update_visual_audit_ledger(const std::filesystem::path& ledger_path, const VisualCorpus& corpus,
    const json& manifest, const std::filesystem::path& out_dir)
{
    json ledger;
    if (std::filesystem::exists(ledger_path)) {
        std::ifstream input{ledger_path};
        if (!input.is_open()) {
            LOG_F(ERROR, "Failed to open ledger path '{}'", ledger_path.string());
            return false;
        }
        try {
            input >> ledger;
        } catch (const std::exception& ex) {
            LOG_F(ERROR, "Failed to parse ledger '{}': {}", ledger_path.string(), ex.what());
            return false;
        }
        if (!ledger.is_object()) {
            LOG_F(ERROR, "Ledger '{}' must contain a JSON object", ledger_path.string());
            return false;
        }
    } else {
        ledger = json::object();
    }

    if (!ledger.contains("$type")) {
        ledger["$type"] = "VisualAuditLedger";
    }
    if (!ledger.contains("version")) {
        ledger["version"] = 1;
    }
    if (!ledger.contains("entries") || !ledger["entries"].is_array()) {
        ledger["entries"] = json::array();
    }
    ledger["name"] = ledger.value("name", "nwn1_visual_audit_ledger");
    ledger["description"] = ledger.value("description",
        "Tracked corpus-backed visual audit status across NWN asset families.");
    ledger["updated_at_utc"] = iso8601_now_utc();

    std::map<std::string, size_t, std::less<>> existing;
    auto& entries = ledger["entries"];
    for (size_t i = 0; i < entries.size(); ++i) {
        if (!entries[i].is_object()) {
            continue;
        }
        const auto id = entries[i].value("id", "");
        if (!id.empty()) {
            existing[id] = i;
        }
    }

    const auto manifest_entries_it = manifest.find("entries");
    if (manifest_entries_it == manifest.end() || !manifest_entries_it->is_array()) {
        LOG_F(ERROR, "Corpus manifest is missing an 'entries' array");
        return false;
    }

    const auto update_entry = [&](json& target, const json& status) {
        target["id"] = status.value("id", "");
        target["kind"] = status.value("kind", "");
        target["input"] = status.value("input", "");
        target["category"] = status.value("category", json::array());
        target["notes"] = status.value("notes", "");
        target["outputs"] = status.value("outputs", json::array());
        target["result"] = status.value("result", "");
        target["detail"] = status.value("detail", "");
        target["corpus_name"] = corpus.name;
        target["corpus_path"] = manifest.value("corpus_path", "");
        target["module_path"] = manifest.value("module_path", "");
        target["last_output_dir"] = out_dir.string();
        target["last_run_manifest"] = (out_dir / "manifest.json").string();
        target["last_run_at_utc"] = iso8601_now_utc();

        if (!target.contains("review_state")) {
            target["review_state"] = "unreviewed";
        }
        if (!target.contains("disposition")) {
            target["disposition"] = "open";
        }
        if (!target.contains("owner")) {
            target["owner"] = "";
        }
        if (!target.contains("issue")) {
            target["issue"] = "";
        }
        if (!target.contains("review_notes")) {
            target["review_notes"] = "";
        }
        if (!target.contains("accepted_difference")) {
            target["accepted_difference"] = false;
        }
    };

    for (const auto& status : *manifest_entries_it) {
        if (!status.is_object()) {
            continue;
        }
        const auto id = status.value("id", "");
        if (id.empty()) {
            continue;
        }

        auto it = existing.find(id);
        if (it == existing.end()) {
            json entry = json::object();
            update_entry(entry, status);
            entries.push_back(std::move(entry));
            existing[id] = entries.size() - 1;
        } else {
            update_entry(entries[it->second], status);
        }
    }

    std::sort(entries.begin(), entries.end(), [](const json& lhs, const json& rhs) {
        return lhs.value("id", "") < rhs.value("id", "");
    });
    return write_text_file(ledger_path, ledger.dump(2));
}

} // namespace

int run_visual_corpus_command(const std::filesystem::path& corpus_path, const std::filesystem::path& output_dir,
    std::string_view module_path, std::string_view user_path, std::string_view pbr_environment_path,
    bool pbr_ibl_enabled, size_t limit, std::string_view filter, const std::filesystem::path& ledger_path)
{
    auto corpus = load_visual_corpus(corpus_path);
    if (!corpus) {
        return 1;
    }

    const auto out_dir = output_dir.empty() ? std::filesystem::path{"."} : output_dir;
    std::error_code ec;
    std::filesystem::create_directories(out_dir, ec);
    if (ec) {
        LOG_F(ERROR, "Failed to create corpus output directory '{}': {}", out_dir.string(), ec.message());
        return 1;
    }

    KernelSession kernel_session;
    if (!initialize_kernel_session(kernel_session, module_path, user_path)) {
        return 1;
    }

    const bool needs_viewer = std::any_of(corpus->entries.begin(), corpus->entries.end(),
        [&](const auto& entry) {
            return entry.kind != VisualCorpusKind::spell && visual_corpus_entry_matches_filter(entry, filter);
        });

    HeadlessViewerSession viewer_session;
    bool viewer_available = true;
    std::string viewer_unavailable_reason;
    if (needs_viewer && !initialize_headless_viewer_session(viewer_session, pbr_environment_path, pbr_ibl_enabled)) {
        viewer_available = false;
        viewer_unavailable_reason = "headless viewer initialization failed";
    }

    const std::string effective_environment_path = effective_pbr_environment_path(pbr_environment_path);
    json manifest = {
        {"corpus_path", corpus_path.string()},
        {"name", corpus->name},
        {"description", corpus->description},
        {"module_path", std::string(module_path)},
        {"user_path", std::string(user_path)},
        {"pbr_environment_path", effective_environment_path},
        {"pbr_ibl_enabled", pbr_ibl_enabled},
        {"static_pbr_environment", {
                                       {"static_model_policy", std::string{static_pbr_environment_policy_name(StaticPbrEnvironmentPolicy::reference_ibl)}},
                                       {"non_static_policy", std::string{static_pbr_environment_policy_name(StaticPbrEnvironmentPolicy::scene_authored)}},
                                       {"environment_path", effective_environment_path},
                                       {"ibl_requested", pbr_ibl_enabled},
                                       {"reference_ibl_strength", kStaticPbrReferenceIblStrength},
                                       {"reference_exposure", kStaticPbrReferenceExposure},
                                   }},
        {"filter", std::string(filter)},
        {"entries", json::array()},
    };

    size_t processed = 0;
    size_t failures = 0;
    size_t unsupported = 0;
    for (const auto& entry : corpus->entries) {
        if (!visual_corpus_entry_matches_filter(entry, filter)) {
            continue;
        }
        if (limit > 0 && processed >= limit) {
            break;
        }

        ++processed;
        const auto entry_dir = out_dir / entry.id;
        std::filesystem::create_directories(entry_dir, ec);
        if (ec) {
            LOG_F(ERROR, "Failed to create entry directory '{}': {}", entry_dir.string(), ec.message());
            ++failures;
            continue;
        }

        std::string result = "failed";
        std::string detail;
        switch (entry.kind) {
        case VisualCorpusKind::area:
        case VisualCorpusKind::creature:
        case VisualCorpusKind::item:
        case VisualCorpusKind::model:
            if (!viewer_available) {
                result = "unsupported";
                detail = viewer_unavailable_reason;
                ++unsupported;
            } else if (capture_viewer_preview(viewer_session, entry, entry_dir / "preview.png")) {
                result = "pass";
            }
            break;
        case VisualCorpusKind::spell:
            if (run_spell_corpus_entry(entry, entry_dir)) {
                result = "pass";
            }
            break;
        }

        if (result == "failed") {
            ++failures;
        }

        const auto status = make_status_json(entry, result, entry_dir, detail);
        if (!write_text_file(entry_dir / "status.json", status.dump(2))) {
            ++failures;
        }
        manifest["entries"].push_back(status);
    }

    manifest["processed"] = processed;
    manifest["failures"] = failures;
    manifest["unsupported"] = unsupported;
    if (!write_text_file(out_dir / "manifest.json", manifest.dump(2))) {
        return 1;
    }
    if (!ledger_path.empty() && !update_visual_audit_ledger(ledger_path, *corpus, manifest, out_dir)) {
        return 1;
    }

    LOG_F(INFO, "Processed visual corpus '{}' ({} entries, {} failures, {} unsupported)",
        corpus->name, processed, failures, unsupported);
    return failures == 0 ? 0 : 1;
}

} // namespace mudl
