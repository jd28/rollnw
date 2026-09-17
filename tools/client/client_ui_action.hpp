#pragma once

#include "client_input_routes.hpp"
#include "object_workbench.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace nw::toolset {

class WorkspaceState;

enum class ClientPreviewPhase : uint8_t {
    inactive,
    selecting_actor,
    placing_actor,
    running,
};

// Cold facts copied from actual owners immediately around synchronous dispatch.
// Engine IDs keep their existing generational table ABI; no DOM borrows occur.
struct ClientUiActionContext {
    ObjectHandle displayed_object{};
    ObjectHandle script_object{};
    ObjectHandle script_area{};
    uint64_t module_generation = 0;
    uint64_t resource_generation = 0;
    ClientInputMap map = ClientInputMap::invalid;
    ObjectWorkbenchSurface surface = ObjectWorkbenchSurface::details;
    ClientPreviewPhase preview = ClientPreviewPhase::inactive;
    uint8_t area_surface = 0; // 0 properties, 1 objects, 2 tiles.
    uint8_t live_objects = 0; // Bits 0..2: displayed, script object, script area.
    bool command_modal = false;
    bool world_blocked = false;

    bool operator==(const ClientUiActionContext&) const = default;
};

// In-process protocol schema 1: fixed header and an owned UTF-8 payload.
// Payload concatenates tab ID, resource detail and subtab ID in that order;
// lengths delimit them without ambiguity. Bytes last until this owner is freed.
// Capturing one displayed Workspace is a true singleton operation. Comparing
// owners is a batch transform with equal input/output counts, no retained borrows.
struct ClientUiActionHeader {
    ClientUiActionContext context;
    ObjectHandle document_object{};
    std::array<uint32_t, 3> text_lengths{};
    uint8_t tab_kind = 0; // Existing WorkspaceTabKind values 0..7.
    bool available = false;

    bool operator==(const ClientUiActionHeader&) const = default;
};

struct ClientUiActionOwner {
    ClientUiActionHeader header;
    std::string text;
};

ClientUiActionOwner capture_client_ui_action_owner(
    const WorkspaceState& workspace, ClientUiActionContext context);
// Invalid rows produce false independently; unequal spans clear all output.
bool match_client_ui_action_owners(std::span<const ClientUiActionOwner> before,
    std::span<const ClientUiActionOwner> after, std::span<bool> matches) noexcept;
// One synchronous release is a batch of size one over the same comparison.
bool same_client_ui_action_owner(const ClientUiActionOwner& before,
    const ClientUiActionOwner& after) noexcept;

} // namespace nw::toolset
