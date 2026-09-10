#pragma once

#include <nw/objects/ObjectHandle.hpp>

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nw {
struct Area;
}

namespace nw::toolset {

class WorkspaceState;
struct CommandResult;

// One open document owns one ObjectManager root and its contained objects.
// Graphics borrow the root; history must be released before the document.
// Documents must be cleared before replacing/shutting down the object service.
class ObjectDocument {
public:
    ObjectDocument() = default;
    ~ObjectDocument();
    ObjectDocument(const ObjectDocument&) = delete;
    ObjectDocument& operator=(const ObjectDocument&) = delete;
    ObjectDocument(ObjectDocument&& other) noexcept;
    ObjectDocument& operator=(ObjectDocument&& other) noexcept;

    [[nodiscard]] ObjectHandle object() const noexcept { return object_; }
    // Transfers a live, independently owned root into an empty document.
    // Rejection leaves ownership with the caller. A viewport load is singular.
    [[nodiscard]] bool adopt(ObjectHandle object);
    // Transfers this root to a native area/container owner without destroying it.
    [[nodiscard]] ObjectHandle release() noexcept;
    void reset() noexcept;

private:
    ObjectHandle object_{};
};

// Borrowed tab IDs -> independent atomic saves, in input order. Empty batches
// are a no-op; empty/duplicate IDs reject the protocol before any writes.
// Missing/stale/unsupported documents or invalid paths fail individually and
// preserve their dirty state; later rows still run. Successful rows become clean, preserving
// tab order, active selection, live roots and undo history. No batch rollback.
[[nodiscard]] CommandResult save_workspace_documents(WorkspaceState& workspace,
    const std::filesystem::path& project_dir, std::span<const std::string_view> tab_ids);

struct PlacedAreaObjectRow {
    ObjectHandle object{};
    std::string name;
};

[[nodiscard]] std::string_view placed_area_object_type_label(ObjectType type) noexcept;

// Builds one flat UI row batch in the area's stored category and member order.
// Null members and invalid handles are dropped explicitly.
void build_placed_area_object_rows(
    const Area& area, std::vector<PlacedAreaObjectRow>& rows);

// The active object is an explicit toolset singleton, so this cold label
// transform is singular rather than a separate batch path.
[[nodiscard]] std::string live_object_display_name(ObjectHandle object);

[[nodiscard]] bool save_live_blueprint_json_atomic(
    ObjectHandle object, const std::filesystem::path& target, std::string& error);

[[nodiscard]] bool save_live_area_json_atomic(
    ObjectHandle object, const std::filesystem::path& target, std::string& error);

} // namespace nw::toolset
