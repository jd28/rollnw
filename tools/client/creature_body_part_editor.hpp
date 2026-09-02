#pragma once

#include <nw/objects/ObjectHandle.hpp>
#include <nw/rules/creature_body_parts.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace nw::toolset {

class VirtualListHost;
struct UiListSelection;

struct CreatureBodyPartEditorInput {
    ObjectHandle object{};
    int32_t assembly = -1;
    // Indexed by stable profile part_id. The caller owns the span for this
    // call; refresh copies it into editor-local state.
    std::span<const int32_t> values;
};

struct CreatureBodyPartEdit {
    int32_t part = -1;
    int32_t value = -1;
};

// Native UI singleton for the one active Creature document. It joins one
// SmallS-published value batch to the immutable profile catalog, then owns the
// resulting list rows, selection, and popup lifetime until the next refresh.
class CreatureBodyPartEditor {
public:
    [[nodiscard]] ObjectHandle object() const noexcept { return object_; }

    bool refresh(const CreatureBodyPartEditorInput& input,
        const CreatureBodyPartCatalog& catalog,
        VirtualListHost& host);
    bool clear(VirtualListHost& host);
    bool close_options(VirtualListHost& host);
    bool activate_part(const UiListSelection& selection,
        const CreatureBodyPartCatalog& catalog,
        VirtualListHost& host);
    [[nodiscard]] std::optional<CreatureBodyPartEdit> activate_option(
        const UiListSelection& selection) const;
    bool hide_options(VirtualListHost& host);

private:
    bool ensure_lists(VirtualListHost& host);
    bool refresh_options(
        const CreatureBodyPartCatalog& catalog, VirtualListHost& host);
    [[nodiscard]] int32_t selected_value() const noexcept;

    ObjectHandle object_{};
    int32_t assembly_ = -1;
    std::vector<int32_t> values_;
    std::vector<int32_t> part_ids_;
    std::vector<int32_t> option_ids_;
    int32_t selected_part_ = -1;
    bool options_open_ = false;
    uint64_t host_generation_ = 0;
};

} // namespace nw::toolset
