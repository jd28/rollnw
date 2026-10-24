#pragma once

#include "ObjectHandle.hpp"

#include <cstdint>
#include <span>

namespace nw {
namespace nav {
struct NavWorldState;
}

enum class ItemTransferDirection : uint8_t { pickup,
    drop };
struct ItemTransfer {
    ObjectHandle item{};
    ItemTransferDirection direction = ItemTransferDirection::pickup;
};

enum class ItemTransferStatus : uint8_t {
    success,
    invalid_input,
    invalid_ownership,
    invalid_inventory,
    out_of_reach,
    blocked,
    no_space,
};

struct ItemTransferResult {
    ItemTransferStatus status = ItemTransferStatus::invalid_input;
    uint32_t transferred = 0;
    [[nodiscard]] bool ok() const noexcept { return status == ItemTransferStatus::success; }
};

// Synchronous kernel-owner batch, at most 1024 distinct live Items. The trusted
// simulation supplies its area/navigation world and finite positive pickup reach.
// Actor must belong to that area. Pickups require a strict ground position and a
// complete route within reach without closed-door traversals. Drops use the
// actor's admitted position and accept inventory Items only (unequip first).
// Process rows in input order in scratch, then publish membership/occupancy/spatial
// changes. Rejection leaves all objects unchanged; empty input is a no-op.
// Inputs are borrowed for the call; ownership passes between Area and Inventory.
// Implemented in nw-nav; core nw does not depend on navigation.
ItemTransferResult transfer_area_items(ObjectHandle area, ObjectHandle actor,
    const nav::NavWorldState& navigation, float pickup_reach,
    std::span<const ItemTransfer> transfers);

const char* item_transfer_message(ItemTransferStatus status) noexcept;

} // namespace nw
