#pragma once

#include "Equips.hpp"
#include "Inventory.hpp"

#include <optional>
#include <span>
#include <vector>

namespace nw {

// Kernel-thread protocol: 0..18 unique slots, each in [0,18). Invalid before/
// after handles mean an empty slot. Nonempty handles are borrowed live Items.
// Incoming Items must be in this owner's inventory or another changed slot.
// The same rows describe committed changes for effects/presentation consumers.
struct EquipmentChange {
    int32_t slot = -1;
    ObjectHandle before{};
    ObjectHandle after{};
};

struct EquipmentState {
    std::array<EquipItem, 18> equipment;
    std::vector<InventoryItem> inventory;
    std::vector<Inventory::Storage> occupancy;
};

struct EquipmentItemFootprint {
    ObjectHandle item{};
    int32_t width = 0;
    int32_t height = 0;
};

// Caller-owned snapshots, not ownership of the referenced Items. Valid for this
// service generation and exact expected state/footprints only. No editor/render
// dependency. Reverse publication is used by authoring history. Gameplay should
// re-prepare its request at its action/animation commit boundary.
// Only prepare_equipment_changes constructs valid batches; retain them unchanged.
struct EquipmentChangeBatch {
    ObjectHandle creature{};
    uint64_t service_generation = 0;
    int32_t rows = 0;
    int32_t columns = 0;
    std::vector<EquipmentChange> changes;
    std::vector<EquipmentItemFootprint> footprints;
    EquipmentState before;
    EquipmentState after;
};

// Invalid, duplicate, unresolved, foreign or oversized input and insufficient
// capacity reject before mutation. Empty requests prepare a no-op batch.
std::optional<EquipmentChangeBatch> prepare_equipment_changes(
    ObjectHandle creature, std::span<const EquipmentChange> changes);

// Allocation-free storage publication after complete validation. Failure leaves
// slots, inventory, occupancy and equip_version unchanged. Profile effects and
// notifications must run after success, using changes in the applied direction.
bool apply_equipment_changes(const EquipmentChangeBatch& batch, bool reverse = false);

} // namespace nw
