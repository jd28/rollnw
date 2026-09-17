#pragma once

#include "pc_input_eligibility.hpp"

#include <cstdint>
#include <span>

namespace nw::toolset {

enum class ClientInputMap : uint8_t { invalid,
    editor,
    pc };
enum class ClientControlRole : uint8_t { editor,
    player,
    dm };
// Platform includes SDK-only events with no existing native feature bindings
// (touch and text-editing candidates), in addition to window/device lifecycle.
enum class ClientInputCategory : uint8_t { key,
    text,
    pointer,
    gamepad,
    platform,
    held };
enum class ClientInputEdge : uint8_t { none,
    down,
    up,
    motion,
    wheel };
enum class ClientInputTarget : uint8_t { none,
    toolset,
    command,
    world };
enum class ClientInputFocus : uint8_t { none,
    toolset_text,
    command_text };
enum class ClientPointerOwner : uint8_t { none,
    toolset,
    command,
    editor,
    pc };
enum class ClientNativeRecipient : uint8_t { none,
    ui,
    editor,
    pc,
    lifecycle };
enum class ClientRmlRecipient : uint8_t { none,
    toolset,
    command };
enum class ClientRmlForwardPhase : uint8_t { none,
    before_native,
    after_native };
enum class ClientInputDisposition : uint8_t { invalid_input,
    ui,
    native,
    unavailable_world };

// Schema revision 1, in-process protocol. Each input row describes one event's
// independently captured current context; rows contain no borrowed payloads.
// The caller owns equal-length contiguous input/output batches for this call.
// Valid enums/edge combinations are required; raw pointer validation precedes
// capture. Missing world identity cannot select an editor fallback in the PC map.
struct ClientInputFacts {
    ClientInputCategory category = ClientInputCategory::platform;
    ClientInputEdge edge = ClientInputEdge::none;
    ClientInputMap map = ClientInputMap::invalid;
    ClientInputTarget target = ClientInputTarget::none;
    ClientInputFocus focus = ClientInputFocus::none;
    ClientPointerOwner pointer_owner = ClientPointerOwner::none;
    bool pointer_valid = true;
    bool world_available = false;
    bool command_modal = false;
    bool world_input_blocked = false;
    bool lifecycle_key = false;
    bool release_before_native = false;
};
struct ClientInputRoute {
    ClientNativeRecipient native = ClientNativeRecipient::none;
    ClientRmlRecipient rml = ClientRmlRecipient::none;
    ClientRmlForwardPhase phase = ClientRmlForwardPhase::none;
    ClientInputDisposition disposition = ClientInputDisposition::invalid_input;
    PcSourceEligibility sources{false, false, false};
};

ClientInputMap client_input_map(ClientControlRole role, bool editor_preview) noexcept;
// Invalid rows reject independently. Unequal spans clear every output to invalid
// and return false; no partial actions or previous output survive rejection.
bool resolve_client_input_routes(std::span<const ClientInputFacts> inputs,
    std::span<ClientInputRoute> outputs) noexcept;

} // namespace nw::toolset
