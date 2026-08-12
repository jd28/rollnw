#pragma once

#include <cstdint>
#include <filesystem>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace nw::toolset {

enum class ResourceDocumentKind : uint8_t {
    unknown,
    folder,
    file,
    resource,
    area,
    preview,
};

enum class ResourceDocumentDiagnosticSeverity : uint8_t {
    info,
    warning,
    error,
};

struct ResourceDocumentProperty {
    std::string group;
    std::string name;
    std::string value;
};

struct ResourceDocumentDiagnostic {
    ResourceDocumentDiagnosticSeverity severity = ResourceDocumentDiagnosticSeverity::info;
    std::string message;
};

struct ResourceDocument {
    bool ok = false;
    std::filesystem::path project_dir;
    std::filesystem::path relative_path;
    std::filesystem::path absolute_path;
    ResourceDocumentKind kind = ResourceDocumentKind::unknown;
    std::string title;
    std::string detail;
    std::string resource_type;
    std::string format;
    std::string message;
    uintmax_t file_size = 0;
    bool previewable = false;
    bool area = false;
    std::vector<ResourceDocumentProperty> properties;
    std::vector<ResourceDocumentDiagnostic> diagnostics;
};

[[nodiscard]] std::string resource_document_kind_label(ResourceDocumentKind kind);
[[nodiscard]] std::string resource_document_diagnostic_severity_label(ResourceDocumentDiagnosticSeverity severity);
[[nodiscard]] ResourceDocument load_project_resource_document(const std::filesystem::path& project_dir,
    const std::filesystem::path& relative_path);
[[nodiscard]] bool save_json_resource_document_atomic(const std::filesystem::path& target,
    const nlohmann::json& value,
    std::string& error);

} // namespace nw::toolset
