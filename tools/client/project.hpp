#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace nw::toolset {

struct ProjectResult {
    bool ok = false;
    bool initialized = false;
    size_t resource_count = 0;
    size_t area_map_count = 0;
    size_t area_map_degraded_count = 0;
    size_t area_map_failure_count = 0;
    std::string message;
};

enum class ProjectImportFormat {
    legacy,
    json,
};

struct ProjectImportOptions {
    ProjectImportFormat format = ProjectImportFormat::legacy;
};

enum class ProjectTreeNodeKind : uint8_t {
    directory,
    area,
    resource,
    file,
};

struct ProjectTreeNode {
    std::string id;
    std::string label;
    std::filesystem::path path;
    std::filesystem::path relative_path;
    std::string resource_type;
    std::string detail;
    ProjectTreeNodeKind kind = ProjectTreeNodeKind::file;
    std::vector<ProjectTreeNode> children;

    [[nodiscard]] bool is_directory() const noexcept { return kind == ProjectTreeNodeKind::directory; }
    [[nodiscard]] bool is_container() const noexcept { return is_directory(); }
};

struct ProjectTreeResult {
    bool ok = false;
    size_t node_count = 0;
    ProjectTreeNode root;
    std::string message;
};

struct ProjectModuleSummary {
    bool ok = false;
    std::vector<std::string> haks;
    std::string message;
};

struct ProjectPreviewSettings {
    bool ok = false;
    std::filesystem::path test_actor;
    std::string message;
};

[[nodiscard]] bool is_project_directory(const std::filesystem::path& path);
[[nodiscard]] std::string project_display_name(const std::filesystem::path& project_dir);
[[nodiscard]] bool project_resource_is_area(const std::filesystem::path& relative_path);
[[nodiscard]] bool project_resource_is_dialog(const std::filesystem::path& relative_path);
[[nodiscard]] bool project_resource_is_preview_blueprint(const std::filesystem::path& relative_path);
[[nodiscard]] std::string project_resource_display_name(const std::filesystem::path& project_dir,
    const std::filesystem::path& relative_path);
[[nodiscard]] ProjectTreeResult load_project_tree(const std::filesystem::path& project_dir, std::string_view query = {});
[[nodiscard]] ProjectModuleSummary load_project_module_summary(const std::filesystem::path& project_dir);
[[nodiscard]] ProjectPreviewSettings load_project_preview_settings(
    const std::filesystem::path& project_dir);
[[nodiscard]] ProjectResult save_project_preview_test_actor(
    const std::filesystem::path& project_dir,
    const std::filesystem::path& relative_actor_path);
[[nodiscard]] ProjectResult initialize_project(const std::filesystem::path& project_dir, std::string module_name = {});
[[nodiscard]] ProjectResult import_module_project(const std::filesystem::path& module_path,
    const std::filesystem::path& project_dir,
    const ProjectImportOptions& options = {});

} // namespace nw::toolset
