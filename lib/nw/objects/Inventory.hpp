#pragma once

#include "../serialization/Serialization.hpp"
#include "../util/FixedVector.hpp"
#include "../util/Variant.hpp"
#include "ObjectBase.hpp"

#include <bitset>
#include <cstdint>

namespace nw {

struct Item;

struct InventoryItem {
    bool infinite = false;
    uint16_t pos_x;
    uint16_t pos_y;
    Variant<Resref, Item*> item;
};

struct InventorySlot {
    int page = -1;
    int row = -1;
    int col = -1;
};

struct Inventory {
    explicit Inventory(ObjectBase* owner_, nw::MemoryResource* allocator = nw::kernel::global_allocator());
    Inventory(int pages, int rows, int columns, nw::MemoryResource* allocator = nw::kernel::global_allocator());
    Inventory(int pages, int rows, int columns, ObjectBase* owner_, nw::MemoryResource* allocator = nw::kernel::global_allocator());

    static constexpr int default_pages = 6;
    // Note that this is just an upper bound of what is available in the game,
    // there is no other meaning to these values.
    static constexpr int max_columns = 10;
    static constexpr int max_rows = 10;
    static constexpr int total_slots_per_page = max_rows * max_columns;

    using Storage = std::bitset<total_slots_per_page>;

    Inventory(const Inventory&) = delete;
    Inventory(Inventory&&) = default;
    Inventory& operator=(const Inventory&) = delete;
    Inventory& operator=(Inventory&&) = default;
    ~Inventory() = default;

    void destroy();
    bool instantiate();

    /// Adds item to inventory
    bool add_item(nw::Item* item);

    /// Adds a page to the inventory
    bool add_page();

    /// Determines if inventory can hold item
    bool can_add_item(nw::Item* item) const;

    /// Checks if slots for item of width x height is availble at a particular page, row, and column
    bool check_available(int page, int row, int col, int width, int height) const noexcept;

    /// Clears an item from the inventory grid
    bool clear_item(int page, int row, int col, int width, int height);

    /// Gets a string representation of the occupied inventory slots
    std::string debug() const;

    /// Finds first available slot for item of width x height
    InventorySlot find_slot(int width, int height) const noexcept;

    /// Determines if inventory contains an item
    bool has_item(nw::Item* item) const noexcept;

    /// Sets an item from the inventory grid
    bool insert_item(int page, int row, int col, int width, int height);

    /// Gets current number of pages
    int pages() const noexcept { return pages_; }

    /// Remove item from inventory
    bool remove_item(nw::Item* item);

    /// Sets the inventory to allow adding pages dynamically (for stores)
    void set_growable(bool value);

    /// Gets number of items in the inventory
    size_t size() const noexcept;

    /// Converts coordinates from our page, row, col to bioware x,y cooardinates
    std::pair<uint16_t, uint16_t> slot_to_xy(InventorySlot slot) const noexcept;

    /// Converts bioware x,y cooardinates to our page, row, col coordinates
    InventorySlot xy_to_slot(uint16_t x, uint16_t y) const noexcept;

    bool from_json(const nlohmann::json& archive, SerializationProfile profile);
    nlohmann::json to_json(SerializationProfile profile) const;

    ObjectBase* owner;
    FixedVector<InventoryItem> items;
    std::vector<Storage> inventory_bitset;
    int pages_ = default_pages;
    int rows_ = max_rows;
    int columns_ = max_columns;
    bool growable_ = false;
};

bool deserialize(Inventory& self, const GffStruct& archive, SerializationProfile profile);
bool serialize(const Inventory& self, GffBuilderStruct& archive, SerializationProfile profile);

} // namespace nw
