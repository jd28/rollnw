#include "client_input_routes.hpp"

#include <algorithm>

namespace nw::toolset {
namespace {

bool valid_facts(const ClientInputFacts& row)
{
    if ((row.map != ClientInputMap::editor && row.map != ClientInputMap::pc)
        || row.category > ClientInputCategory::held || row.edge > ClientInputEdge::wheel
        || row.target > ClientInputTarget::world || row.focus > ClientInputFocus::command_text
        || row.pointer_owner > ClientPointerOwner::pc || !row.pointer_valid) { return false; }
    switch (row.category) {
    case ClientInputCategory::key:
        if (row.edge != ClientInputEdge::down && row.edge != ClientInputEdge::up) { return false; }
        break;
    case ClientInputCategory::pointer:
        if (row.edge == ClientInputEdge::none) { return false; }
        break;
    case ClientInputCategory::gamepad:
        if (row.edge != ClientInputEdge::none && row.edge != ClientInputEdge::down && row.edge != ClientInputEdge::up) { return false; }
        break;
    default:
        if (row.edge != ClientInputEdge::none) { return false; }
        break;
    }
    if (row.lifecycle_key && (row.category != ClientInputCategory::key || row.edge != ClientInputEdge::down)) { return false; }
    if (row.release_before_native && (row.category != ClientInputCategory::pointer || row.edge != ClientInputEdge::up || (row.target != ClientInputTarget::toolset && row.target != ClientInputTarget::command))) { return false; }
    return !(row.map == ClientInputMap::pc && row.pointer_owner == ClientPointerOwner::editor)
        && !(row.map == ClientInputMap::editor && row.pointer_owner == ClientPointerOwner::pc);
}

ClientInputRoute resolve(const ClientInputFacts& row)
{
    ClientInputRoute result;
    if (!valid_facts(row)) { return result; }
    result.disposition = ClientInputDisposition::native;
    const bool blocked = row.command_modal || row.world_input_blocked;
    if (row.map == ClientInputMap::pc && row.world_available) {
        result.sources = {
            !blocked && row.focus == ClientInputFocus::none,
            !blocked,
            !blocked && (row.pointer_owner == ClientPointerOwner::pc || (row.pointer_owner == ClientPointerOwner::none && row.target != ClientInputTarget::toolset && row.target != ClientInputTarget::command)),
        };
    }
    if (row.category == ClientInputCategory::held) {
        result.native = row.map == ClientInputMap::pc && row.world_available ? ClientNativeRecipient::pc : ClientNativeRecipient::none;
        if (row.map == ClientInputMap::pc && !row.world_available) { result.disposition = ClientInputDisposition::unavailable_world; }
        return result;
    }
    result.rml = row.target == ClientInputTarget::command || row.pointer_owner == ClientPointerOwner::command
        ? ClientRmlRecipient::command
        : ClientRmlRecipient::toolset;
    result.phase = row.release_before_native ? ClientRmlForwardPhase::before_native : ClientRmlForwardPhase::after_native;
    if (row.category == ClientInputCategory::platform) {
        result.native = ClientNativeRecipient::lifecycle;
    } else if (row.command_modal) {
        result.native = ClientNativeRecipient::ui;
        result.rml = ClientRmlRecipient::command;
        result.disposition = ClientInputDisposition::ui;
    } else if (row.lifecycle_key) {
        result.native = ClientNativeRecipient::lifecycle;
    } else if (((row.target == ClientInputTarget::command || row.target == ClientInputTarget::toolset)
                   && !(row.category == ClientInputCategory::pointer
                       && (row.pointer_owner == ClientPointerOwner::editor || row.pointer_owner == ClientPointerOwner::pc)))
        || row.pointer_owner == ClientPointerOwner::toolset || row.pointer_owner == ClientPointerOwner::command
        || (row.category == ClientInputCategory::key && row.focus != ClientInputFocus::none)
        || row.category == ClientInputCategory::text) {
        result.native = ClientNativeRecipient::ui;
        result.disposition = ClientInputDisposition::ui;
    } else if (row.world_input_blocked) {
        result.native = ClientNativeRecipient::none;
        result.disposition = ClientInputDisposition::unavailable_world;
    } else if (row.map == ClientInputMap::pc) {
        result.native = row.world_available ? ClientNativeRecipient::pc : ClientNativeRecipient::none;
        if (!row.world_available) { result.disposition = ClientInputDisposition::unavailable_world; }
    } else {
        result.native = ClientNativeRecipient::editor;
    }
    return result;
}
} // namespace

ClientInputMap client_input_map(ClientControlRole role, bool editor_preview) noexcept
{
    switch (role) {
    case ClientControlRole::editor:
        return editor_preview ? ClientInputMap::pc : ClientInputMap::editor;
    case ClientControlRole::player:
    case ClientControlRole::dm:
        return ClientInputMap::pc;
    }
    return ClientInputMap::invalid;
}

bool resolve_client_input_routes(std::span<const ClientInputFacts> inputs, std::span<ClientInputRoute> outputs) noexcept
{
    if (inputs.size() != outputs.size()) {
        std::fill(outputs.begin(), outputs.end(), ClientInputRoute{});
        return false;
    }
    for (size_t index = 0; index < inputs.size(); ++index) {
        outputs[index] = resolve(inputs[index]);
    }
    return true;
}
} // namespace nw::toolset
