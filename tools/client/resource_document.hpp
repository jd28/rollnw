#pragma once

#include <cstdint>
#include <filesystem>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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

enum class ResourceFileWriteMode : uint8_t { create,
    replace };

struct ResourceFileWrite {
    std::filesystem::path target;
    std::string_view bytes;
    ResourceFileWriteMode mode = ResourceFileWriteMode::create;
    std::optional<std::string_view> expected_bytes;
};

struct ResourceFileWriteResult {
    bool written = false;
    std::string error;
};

// Borrowed bytes remain valid throughout this synchronous batch. Empty input is
// a no-op; invalid modes/paths and duplicate targets reject the complete batch.
// Writes then run independently in input order, each publishing a complete file.
// Create never overwrites. Replace requires expected bytes and checks them just
// before publication; this is not a filesystem compare-and-swap against external
// writers. Results own diagnostics; multi-file recovery belongs to the operation.
[[nodiscard]] std::vector<ResourceFileWriteResult> write_resource_files_atomic(
    std::span<const ResourceFileWrite> writes);

[[nodiscard]] std::string resource_document_kind_label(ResourceDocumentKind kind);
[[nodiscard]] std::string resource_document_diagnostic_severity_label(ResourceDocumentDiagnosticSeverity severity);
[[nodiscard]] ResourceDocument load_project_resource_document(const std::filesystem::path& project_dir,
    const std::filesystem::path& relative_path);
[[nodiscard]] bool save_json_resource_document_atomic(const std::filesystem::path& target,
    const nlohmann::json& value,
    std::string& error);

} // namespace nw::toolset
