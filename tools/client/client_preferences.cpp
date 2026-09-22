#include "client_preferences.hpp"
#include "resource_document.hpp"

#include <SDL3/SDL.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <exception>
#include <fstream>
#include <string>

namespace nw::toolset {
namespace {

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

bool write_preferences(const std::filesystem::path& path, const nlohmann::json& prefs)
{
    std::error_code ec;
    if (const auto parent = path.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to create rollnw client preferences directory: %s", ec.message().c_str());
            return false;
        }
    }
    if (std::filesystem::exists(path, ec)) {
        std::string error;
        if (!nw::toolset::save_json_resource_document_atomic(path, prefs, error)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to save rollnw client preferences: %s", error.c_str());
            return false;
        }
        return true;
    }
    std::ofstream output{path};
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

} // namespace

std::filesystem::path client_preferences_path()
{
    return preferences_path("client");
}

void load_ui_preferences(const std::filesystem::path& path,
    DockLayout& docks, std::vector<RecentProjectEntry>& recent_projects,
    LanguageID* toolset_language)
{
    if (path.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Unable to resolve rollnw client preferences path: %s", SDL_GetError());
        return;
    }

    std::ifstream input{path};
    if (!input) {
        return;
    }

    try {
        nlohmann::json prefs;
        input >> prefs;
        if (!prefs.is_object()) {
            return;
        }
        load_dock_preferences(prefs, docks);
        nw::toolset::load_recent_project_preferences(prefs, recent_projects);
        if (toolset_language) {
            const auto ui = prefs.find("ui");
            if (ui != prefs.end() && ui->is_object()) {
                const auto value = ui->find("toolset_language");
                if (value != ui->end() && value->is_string()) {
                    const auto language = Language::from_string(value->get<std::string>());
                    if (language != LanguageID::invalid) { *toolset_language = language; }
                }
            }
        }
    } catch (const std::exception& e) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to read rollnw client preferences: %s", e.what());
    }
}

bool save_ui_preferences(const std::filesystem::path& path,
    const DockLayout& docks, std::span<const RecentProjectEntry> recent_projects)
{
    if (path.empty()) {
        return false;
    }

    nlohmann::json prefs = nlohmann::json::object();
    if (std::ifstream input{path}; input) {
        try {
            input >> prefs;
            if (!prefs.is_object()) {
                prefs = nlohmann::json::object();
            }
        } catch (const std::exception&) {
            prefs = nlohmann::json::object();
        }
    }

    write_dock_preferences(prefs, docks);
    nw::toolset::write_recent_project_preferences(prefs, recent_projects);
    prefs.erase("left_dock_width_px");
    prefs.erase("bottom_dock_height_px");
    prefs.erase("terminal_height_px");

    return write_preferences(path, prefs);
}

bool save_toolset_language_preference(const std::filesystem::path& path,
    LanguageID language)
{
    if (path.empty() || Language::to_string(language).empty()) { return false; }
    nlohmann::json prefs = nlohmann::json::object();
    if (std::ifstream input{path}; input) {
        try {
            input >> prefs;
            if (!prefs.is_object()) { prefs = nlohmann::json::object(); }
        } catch (const std::exception&) {
            prefs = nlohmann::json::object();
        }
    }
    auto& ui = prefs["ui"];
    if (!ui.is_object()) { ui = nlohmann::json::object(); }
    ui["toolset_language"] = std::string{Language::to_string(language)};
    return write_preferences(path, prefs);
}

void remember_recent_project(const std::filesystem::path& preferences_path, const DockLayout& docks,
    std::vector<RecentProjectEntry>& recent_projects, const std::filesystem::path& project_dir)
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

    recent_projects.erase(std::remove_if(recent_projects.begin(), recent_projects.end(), [&path](const nw::toolset::RecentProjectEntry& entry) {
        return entry.path == path;
    }),
        recent_projects.end());

    recent_projects.insert(recent_projects.begin(), nw::toolset::RecentProjectEntry{
                                                        nw::toolset::project_display_name(normalized),
                                                        path,
                                                    });
    if (recent_projects.size() > nw::toolset::kMaxRecentProjects) {
        recent_projects.resize(nw::toolset::kMaxRecentProjects);
    }
    save_ui_preferences(preferences_path, docks, recent_projects);
}

RecentProjectForgetStatus forget_recent_project_preferences(const std::filesystem::path& path,
    const DockLayout& docks, std::vector<RecentProjectEntry>& projects,
    std::span<const size_t> indices)
{
    if (indices.empty()) { return RecentProjectForgetStatus::saved; }
    auto previous = projects;
    if (!forget_recent_projects(projects, indices)) { return RecentProjectForgetStatus::rejected; }
    if (!save_ui_preferences(path, docks, projects)) {
        projects = std::move(previous);
        return RecentProjectForgetStatus::save_failed;
    }
    return RecentProjectForgetStatus::saved;
}

} // namespace nw::toolset
