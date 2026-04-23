#include "visual_corpus.hpp"

#include "app_runtime.hpp"
#include "mudl_cli.hpp"
#include "mudl_commands.hpp"
#include "particle_tools.hpp"
#include "renderer.hpp"
#include "viewer_runtime.hpp"

#include <nw/log.hpp>
#include <nw/render/nwn/model_loader.hpp>

#include <SDL3/SDL.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
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

struct VisualCorpusEntry {
    std::string id;
    VisualCorpusKind kind = VisualCorpusKind::creature;
    std::string input;
    std::string animation;
    ParticlePreviewView view = ParticlePreviewView::front;
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
    bool sdl_initialized = false;
    bool graphics_initialized = false;
    bool render_runtime_initialized = false;
    bool renderer_initialized = false;

    ~HeadlessViewerSession()
    {
        state.current_scene.reset();
        state.renderer.reset();
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

bool initialize_kernel_session(KernelSession& session, std::string_view module_path)
{
    if (!init_kernel_services(module_path)) {
        return false;
    }
    session.initialized = true;
    return true;
}

bool initialize_headless_viewer_session(HeadlessViewerSession& session)
{
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

    session.state.camera = std::make_unique<Camera>();
    session.state.camera->set_aspect_ratio(1280.0f / 720.0f);

    if (!init_render_runtime(session.state)) {
        LOG_F(ERROR, "Failed to initialize render runtime");
        return false;
    }
    session.render_runtime_initialized = true;

    session.state.renderer = std::make_unique<Renderer>(session.state.gfx_context);
    if (!session.state.renderer->initialize(session.state.shader_provider.get())) {
        LOG_F(ERROR, "Failed to initialize renderer");
        session.state.renderer.reset();
        return false;
    }
    session.renderer_initialized = true;

    set_dangly_debug_scale(session.state.dangly_scale);
    set_dangly_mode(session.state.dangly_mode);
    return true;
}

bool capture_viewer_preview(HeadlessViewerSession& session, const VisualCorpusEntry& entry,
    const std::filesystem::path& out_path)
{
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

    session.state.screenshot_path = out_path.string();
    session.state.screenshot_requested = true;
    session.state.screenshot_captured = false;
    session.state.screenshot_delay_frames = 2;
    session.state.running = true;

    for (int i = 0; i < 8 && !session.state.screenshot_captured; ++i) {
        render_frame(session.state);
    }

    if (!session.state.screenshot_captured) {
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
    };
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
    std::string_view module_path, size_t limit, std::string_view filter, const std::filesystem::path& ledger_path)
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
    if (!initialize_kernel_session(kernel_session, module_path)) {
        return 1;
    }

    const bool needs_viewer = std::any_of(corpus->entries.begin(), corpus->entries.end(),
        [&](const auto& entry) {
            return entry.kind != VisualCorpusKind::spell && visual_corpus_entry_matches_filter(entry, filter);
        });

    HeadlessViewerSession viewer_session;
    bool viewer_available = true;
    std::string viewer_unavailable_reason;
    if (needs_viewer && !initialize_headless_viewer_session(viewer_session)) {
        viewer_available = false;
        viewer_unavailable_reason = "headless viewer initialization failed";
    }

    json manifest = {
        {"corpus_path", corpus_path.string()},
        {"name", corpus->name},
        {"description", corpus->description},
        {"module_path", std::string(module_path)},
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
