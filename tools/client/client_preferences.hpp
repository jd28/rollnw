#pragma once

#include "dock_layout.hpp"
#include "project.hpp"

#include <nw/i18n/Language.hpp>

#include <filesystem>
#include <span>
#include <vector>

namespace nw::toolset {

// One application preference file. Caller owns the dock/recent data; no borrow
// survives I/O. Invalid fields keep the baseline ignore/clamp policy. Missing
// files leave defaults; errors are logged and save reports failure.
[[nodiscard]] std::filesystem::path client_preferences_path();
void load_ui_preferences(const std::filesystem::path& path,
    DockLayout& docks, std::vector<RecentProjectEntry>& recent_projects,
    LanguageID* toolset_language = nullptr);
bool save_ui_preferences(const std::filesystem::path& path,
    const DockLayout& docks, std::span<const RecentProjectEntry> recent_projects);
// Independent of the engine's resource-encoding language. Unknown stored
// values are ignored; valid changes preserve every other preference key.
bool save_toolset_language_preference(const std::filesystem::path& path,
    LanguageID language);

enum class RecentProjectForgetStatus { rejected,
    saved,
    save_failed };
// Caller owns history/indices for this call. Invalid indices reject all; empty
// batches succeed without I/O. Save failure restores the complete original rows.
RecentProjectForgetStatus forget_recent_project_preferences(const std::filesystem::path& path,
    const DockLayout& docks, std::vector<RecentProjectEntry>& projects,
    std::span<const size_t> indices);
// One preference history update: canonicalize the opened project, deduplicate
// its row, move it first, bound history and persist the existing dock/history.
void remember_recent_project(const std::filesystem::path& path, const DockLayout& docks,
    std::vector<RecentProjectEntry>& recent_projects, const std::filesystem::path& project_dir);

} // namespace nw::toolset
