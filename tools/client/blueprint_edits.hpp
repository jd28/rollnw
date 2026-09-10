#pragma once

#include "object_document.hpp"

#include <absl/container/flat_hash_map.h>
#include <nlohmann/json.hpp>
#include <nw/resources/assets.hpp>

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace nw::toolset {

class WorkspaceState;

struct BlueprintDestination {
    Resource resource;
    std::filesystem::path directory;
};

struct BlueprintDestinationResult {
    std::filesystem::path target;
    std::string error;
};

// Interactive validation against the published registry and selected directory;
// preparation additionally scans for unregistered/duplicate project files.
[[nodiscard]] std::vector<BlueprintDestinationResult> validate_blueprint_destinations(
    const std::filesystem::path& project, std::span<const BlueprintDestination> rows);

struct InitializedBlueprints {
    std::vector<ObjectDocument> roots;
    std::string error;
    [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

struct BlueprintCreationRequest {
    Resource destination;
    // Required for Items; other types leave this unset.
    int32_t base_item = -1;
    // Required for Creatures; other types leave these unset.
    int32_t race = -1;
    int32_t class_id = -1;
};

// Typed resource keys plus plain profile selections -> independent roots with
// fresh identity. The selected profile owns all propset/default initialization.
// Uses normal object wiring. Empty is a no-op; invalid keys, types, selections,
// or profile initialization release the entire batch.
[[nodiscard]] InitializedBlueprints initialize_blueprints(std::span<const BlueprintCreationRequest> rows);

enum class BlueprintWriteKind : uint8_t { create,
    save_as,
    update };

struct BlueprintWriteRequest {
    BlueprintWriteKind kind = BlueprintWriteKind::save_as;
    ObjectHandle source{};
    Resource destination;
    std::filesystem::path directory;
};

struct PreparedBlueprintWrite {
    BlueprintWriteRequest request;
    std::filesystem::path target;
    nlohmann::json source_snapshot;
    std::string bytes;
    std::optional<std::string> expected_bytes;
    std::optional<std::string> previous_source_bytes;
    ObjectDocument document;
};

struct PreparedBlueprintWrites {
    std::filesystem::path project;
    uint64_t resource_generation = 0;
    absl::flat_hash_map<Resource, nlohmann::json> dependency_snapshots;
    std::vector<PreparedBlueprintWrite> rows;
    std::string error;
    [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

struct BlueprintWriteResult {
    std::filesystem::path relative_path;
    bool saved = false;
    bool published = false;
    std::string error;
};

[[nodiscard]] ResourceType::type blueprint_resource_type(ObjectType type) noexcept;
[[nodiscard]] ObjectType blueprint_object_type(ResourceType::type type) noexcept;
[[nodiscard]] std::filesystem::path default_blueprint_directory(ResourceType::type type);
[[nodiscard]] bool validate_blueprint_resref(std::string_view text, std::string& normalized, std::string& error);

// Captures canonical blueprints and their complete item dependency graph without
// instantiating references. Output is cleared on missing/cyclic/invalid input.
[[nodiscard]] bool snapshot_blueprints(std::span<const Resource> resources,
    absl::flat_hash_map<Resource, nlohmann::json>& output, std::string& error);

// Snapshot/validation work runs on the kernel owner's thread. Requests borrow
// live roots; prepared rows own independent copies and complete bytes. Save-as
// copies are never activated and are released after writing; history keeps bytes.
// Empty batches are a no-op. Invalid input, collisions, dirty destinations and
// lossy nested-item export reject preparation before any file is written.
[[nodiscard]] PreparedBlueprintWrites prepare_blueprint_writes(
    const std::filesystem::path& project, WorkspaceState& workspace,
    std::span<const BlueprintWriteRequest> requests);

// Revalidate the complete batch before the first write. File failures are reported
// per row; successful files share one resource refresh. saved && !published is
// retryable publication failure, never an invitation to repeat exclusive create.
[[nodiscard]] std::vector<BlueprintWriteResult> publish_blueprint_writes(
    WorkspaceState& workspace, PreparedBlueprintWrites& prepared);

// Retries publication/adoption for saved rows only; never repeats file writes.
void refresh_blueprint_writes(WorkspaceState& workspace, PreparedBlueprintWrites& prepared,
    std::span<BlueprintWriteResult> results);

} // namespace nw::toolset
