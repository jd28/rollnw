#pragma once

#include "blueprint_references.hpp"

namespace nw::toolset {

// Version 1 on disk: request.json owns project/scope/source bytes; manifest.json
// owns ordered project-relative document rows; numbered before/after files own
// exact recovery bytes. No live handles or process-local resource identities.
// One operation is a UI singleton over a closed-document batch. The editor's
// live area, when present, is excluded and updated through native ownership.
struct BlueprintOperationProgress {
    std::string stage;
    size_t completed = 0;
    size_t total = 0; // zero means indeterminate
    size_t instances = 0;
    size_t covered = 0;
    size_t reference_uses = 0;
    std::string detail;
    std::string error;
    std::string unit = "documents";
};

[[nodiscard]] std::filesystem::path create_blueprint_update_operation(
    const std::filesystem::path& project, Resource source,
    const std::filesystem::path& current_area, std::string& error,
    const std::filesystem::path& live_area = {});
[[nodiscard]] BlueprintOperationProgress read_blueprint_operation_progress(const std::filesystem::path& operation);
[[nodiscard]] std::vector<std::filesystem::path> blueprint_operation_documents(const std::filesystem::path& operation);
[[nodiscard]] std::vector<std::filesystem::path> find_unfinished_blueprint_operations(const std::filesystem::path& project);
[[nodiscard]] std::filesystem::path latest_blueprint_operation(const std::filesystem::path& project);

// Separate process entry point. prepare/commit require the project's kernel;
// restore uses only guarded bytes. Each phase owns at most one document's JSON
// plus the frozen dependency closure. Authored file replacement is serial.
[[nodiscard]] bool run_blueprint_update_operation(const std::filesystem::path& operation,
    std::string_view phase, std::string& error);
[[nodiscard]] bool cancel_blueprint_update_operation(const std::filesystem::path& operation, std::string& error);
[[nodiscard]] bool finish_blueprint_update_operation(const std::filesystem::path& operation, std::string& error);

} // namespace nw::toolset
