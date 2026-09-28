#pragma once

#include <filesystem>
#include <regex>
#include <span>
#include <string>
#include <vector>

namespace nw::toolset {

// Rules belong to one tree scan. The scanner appends each directory's rules
// before visiting its entries and removes them on return. Paths use generic
// separators, relative to the project root. No Git installation/index is read.
struct ProjectIgnoreRule {
    std::regex pattern;
    size_t base_length = 0;
    bool basename_only = false;
    bool directories_only = false;
    bool include = false;
};

// Missing files and symlinks add no rules. Invalid patterns never match;
// filesystem/read failures return false with an error for the tree result.
bool append_project_ignore_rules(const std::filesystem::path& directory,
    const std::filesystem::path& relative_directory,
    std::vector<ProjectIgnoreRule>& rules, std::string& error);

// Stable in-place filtering, before grouping resources or descending into
// directories. Rules are borrowed for this call; retained entries stay owned
// by the scanner. A child cannot re-include a pruned parent directory.
void filter_project_entries(const std::filesystem::path& relative_directory,
    std::span<const ProjectIgnoreRule> rules,
    std::vector<std::filesystem::directory_entry>& entries);

} // namespace nw::toolset
