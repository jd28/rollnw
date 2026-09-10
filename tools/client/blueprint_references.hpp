#pragma once

#include "blueprint_edits.hpp"

namespace nw::toolset {

[[nodiscard]] ObjectType blueprint_document_object_type(
    ResourceType::type type) noexcept;

struct BlueprintReferenceRow {
    // Generated from known inventory/equipment/CAF fields; never accepted from UI.
    std::string path;
    std::string owner_path;
    ObjectType owner_type = ObjectType::invalid;
    bool placed = false;
    bool covered = false;
};

struct BlueprintReferences {
    std::vector<BlueprintReferenceRow> rows;
    size_t reference_uses = 0;
    std::string error;
};

// One JSON document owns the payload; rows are independent paths into that frozen
// document. CAF arrays and nested items are traversed in stored order. Blueprint
// roots are definitions, string children are reference-only uses. Malformed known
// fields reject the complete document. Matching ancestors cover their descendants.
[[nodiscard]] BlueprintReferences collect_blueprint_references(
    const nlohmann::json& document, ResourceType::type type, Resource blueprint);

struct BlueprintUpdateDocument {
    ResourceType::type type = ResourceType::invalid;
    nlohmann::json value;
    BlueprintReferences references;
};

enum class BlueprintInstanceOwner : uint8_t {
    area,
    inventory,
    equipment,
    store_armor,
    store_miscellaneous,
    store_potions,
    store_rings,
    store_weapons,
};

struct BlueprintInstanceRow {
    ObjectHandle object{};
    ObjectHandle owner{};
    BlueprintInstanceOwner attachment = BlueprintInstanceOwner::area;
    size_t index = 0;
};

// Modal-operation-owned native batch. Rows borrow the pinned area's existing
// objects; replacements own detached roots until publication. No live JSON is
// patched or area root reloaded. Preparation and publication use the kernel thread.
struct LiveBlueprintUpdates {
    ObjectHandle area{};
    Resource source;
    std::vector<BlueprintInstanceRow> rows;
    std::vector<ObjectDocument> replacements;
    absl::flat_hash_map<Resource, nlohmann::json> blueprints;
    size_t expected_objects = 0;
    size_t covered = 0;
    bool applied = false;
};

// Discover all matching native ownership locations; a matching ancestor covers
// its descendants. Missing/stale/repeated ownership rejects the entire batch.
[[nodiscard]] bool collect_live_blueprint_references(ObjectHandle area, Resource source,
    LiveBlueprintUpdates& output, std::string& error);
// Prepare up to count additional replacements, allowing progress between frames.
// Any failure releases every detached replacement and leaves the area untouched.
[[nodiscard]] bool prepare_live_blueprint_updates(LiveBlueprintUpdates& updates,
    size_t count, std::string& error);
// Recheck the frozen source and native owner slots before file or live publication.
[[nodiscard]] bool validate_live_blueprint_updates(const LiveBlueprintUpdates& updates, std::string& error);
// Caller clears handle-based area history before publication. Swaps owner slots,
// destroys old roots, and remaps selection; all unrelated native objects survive.
[[nodiscard]] bool publish_live_blueprint_updates(LiveBlueprintUpdates& updates,
    ObjectHandle& selection, std::string& error);

// Batch of complete documents -> complete replacement documents. Frozen blueprints
// include the acyclic dependency closure. Failure discards every output. Existing
// root placement/UUID and container positions survive; all other fields are fresh.
// Runs on the kernel owner's thread (the preparation process in the editor).
[[nodiscard]] bool prepare_blueprint_updates(std::span<BlueprintUpdateDocument> documents,
    Resource source, const absl::flat_hash_map<Resource, nlohmann::json>& blueprints,
    std::string& error);

// Detached roots for validation and live publication. Output owns the whole batch;
// invalid input or a dropped child rejects and releases the complete batch.
[[nodiscard]] bool load_blueprint_update_documents(std::span<const BlueprintUpdateDocument> documents,
    std::vector<ObjectDocument>& output, std::string& error);

} // namespace nw::toolset
