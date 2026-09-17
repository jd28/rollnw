#pragma once

#include "dock_layout.hpp"
#include "project.hpp"

#include <filesystem>
#include <span>
#include <vector>

namespace nw::toolset {

// One application preference file. Caller owns the dock/recent data; no borrow
// survives I/O. Invalid fields keep the baseline ignore/clamp policy. Missing
// files leave defaults; errors are logged and save reports failure.
[[nodiscard]] std::filesystem::path client_preferences_path();
void load_ui_preferences(const std::filesystem::path& path,
    DockLayout& docks, std::vector<RecentProjectEntry>& recent_projects);
bool save_ui_preferences(const std::filesystem::path& path,
    const DockLayout& docks, std::span<const RecentProjectEntry> recent_projects);

} // namespace nw::toolset
