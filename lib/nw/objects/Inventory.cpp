#include "Inventory.hpp"

#include "Item.hpp"
#include "ObjectComponentSystem.hpp"
#include "ObjectManager.hpp"

#include <nlohmann/json.hpp>

namespace nw {

namespace {

Item* item_from_handle(ObjectHandle handle) noexcept
{
    if (handle.type != ObjectType::item) { return nullptr; }
    return nw::kernel::objects().get<Item>(handle);
}

const ObjectItemLayoutState* item_layout_for(Item* item)
{
    if (!item) { return nullptr; }
    return nw::kernel::objects().components().find_item_layout(item->handle());
}

bool serialize_item_to_json(const Item* item, nlohmann::json& out, SerializationProfile profile)
{
    bool (*serialize_json)(const Item*, nlohmann::json&, SerializationProfile) = nw::serialize;
    return serialize_json(item, out, profile);
}

} // namespace

Item* inventory_item_ptr(const InventoryItem& item) noexcept
{
    if (!item.item.is<ObjectHandle>()) { return nullptr; }
    return item_from_handle(item.item.as<ObjectHandle>());
}

Inventory::Inventory(ObjectBase* owner_, nw::MemoryResource* allocator)
    : Inventory(Inventory::default_pages, Inventory::max_rows, Inventory::max_columns,
          owner_, allocator)
{
}

Inventory::Inventory(int pages, int rows, int columns, nw::MemoryResource* allocator)
    : Inventory(pages, rows, columns, nullptr, allocator)
{
}

Inventory::Inventory(int pages, int rows, int columns, ObjectBase* owner_, nw::MemoryResource* allocator)
    : owner{owner_}
    , items(1024, allocator) // There is some maximum number of items..
    , pages_{pages}
    , rows_{std::min(rows, max_rows)}
    , columns_{std::min(columns, max_columns)}
{
    inventory_bitset.resize(pages_);
}

StoreInventory::StoreInventory(nw::MemoryResource* allocator)
    : StoreInventory{nullptr, allocator}
{
}

StoreInventory::StoreInventory(ObjectBase* owner, nw::MemoryResource* allocator)
    : armor{1, 10, 10, owner, allocator}
    , miscellaneous{1, 10, 10, owner, allocator}
    , potions{1, 10, 10, owner, allocator}
    , rings{1, 10, 10, owner, allocator}
    , weapons{1, 10, 10, owner, allocator}
{
    armor.set_growable(true);
    miscellaneous.set_growable(true);
    potions.set_growable(true);
    rings.set_growable(true);
    weapons.set_growable(true);
}

void StoreInventory::set_owner(ObjectBase* owner)
{
    armor.owner = owner;
    miscellaneous.owner = owner;
    potions.owner = owner;
    rings.owner = owner;
    weapons.owner = owner;
}

void Inventory::destroy()
{
    for (auto& it : items) {
        auto item = inventory_item_ptr(it);
        if (!item) { continue; }
        nw::kernel::objects().destroy(item->handle());
    }
    items.clear();
    inventory_bitset.clear();
}

bool Inventory::instantiate()
{
    ERRARE("[objects] instantiating object inventory");

    for (auto& it : items) {
        if (it.item.is<Resref>()) {
            auto temp = kernel::objects().load<Item>(it.item.as<Resref>());
            if (temp) {
                it.item = temp->handle();
            } else {
                LOG_F(WARNING, "failed to instantiate item, perhaps you're missing '{}.uti'?",
                    it.item.as<Resref>());
            }
        }

        if (it.item.is<ObjectHandle>()) {
            auto item = inventory_item_ptr(it);
            if (!item) { continue; }
            if (const auto* layout = item_layout_for(item)) {
                auto slot = xy_to_slot(it.pos_x, it.pos_y);
                if (pages_ <= slot.page) {
                    inventory_bitset.resize(slot.page + 1);
                    pages_ = slot.page + 1;
                }
                insert_item(slot.page, slot.row, slot.col,
                    layout->inventory_width, layout->inventory_height);
            }
        }
    }
    return true;
}

bool Inventory::add_item(nw::Item* item)
{
    if (!item) { return false; }
    const auto* layout = item_layout_for(item);
    if (!layout) { return false; }
    if (has_item(item)) { return false; }

    const int width = layout->inventory_width;
    const int height = layout->inventory_height;
    auto slot = find_slot(width, height);
    if (slot.page == -1) { return false; }
    insert_item(slot.page, slot.row, slot.col, width, height);
    auto [x, y] = slot_to_xy(slot);

    InventoryItem ii;
    ii.item = item->handle();
    ii.pos_x = x;
    ii.pos_y = y;
    items.push_back(ii);

    return true;
}

bool Inventory::add_page()
{
    if (!growable_) { return false; }
    inventory_bitset.push_back(Storage{});
    ++pages_;
    return true;
}

bool Inventory::can_add_item(nw::Item* item) const
{
    if (!item) { return false; }
    const auto* layout = item_layout_for(item);
    if (!layout) { return false; }

    auto slot = find_slot(layout->inventory_width, layout->inventory_height);
    return slot.page != -1;
}

bool Inventory::check_available(int page, int row, int col, int width, int height) const noexcept
{
    if (page < 0 || page >= pages_) { return false; }
    if (row - height + 1 < 0 || col + width > columns_) { return false; }

    for (int r = row; r > row - height; --r) {
        for (int c = col; c < col + width; ++c) {
            int index = r * columns_ + c;
            if (inventory_bitset[page].test(index)) {
                return false;
            }
        }
    }

    return true;
}

bool Inventory::clear_item(int page, int row, int col, int width, int height)
{
    if (page < 0 || page >= pages_) { return false; }
    if (row - height + 1 < 0 || col + width > columns_) { return false; }

    for (int r = row; r > row - height; --r) {
        for (int c = col; c < col + width; ++c) {
            int index = r * columns_ + c;
            inventory_bitset[page].reset(index);
        }
    }

    return true;
}

std::string Inventory::debug() const
{
    std::stringstream ss;

    for (int page = 0; page < pages_; ++page) {
        if (inventory_bitset[page].none()) { continue; }

        ss << "Page " << page + 1 << ":\n";
        for (int row = 0; row < rows_; ++row) {
            for (int col = 0; col < columns_; ++col) {
                int index = row * columns_ + col;
                ss << (inventory_bitset[page].test(index) ? "X" : ".") << " ";
            }
            ss << "\n";
        }

        ss << "\n";
    }

    return ss.str();
}

InventorySlot Inventory::find_slot(int width, int height) const noexcept
{
    for (int page = 0; page < pages_; ++page) {
        for (int row = rows_ - 1; row >= 0; --row) {
            for (int col = 0; col <= columns_ - width; ++col) {
                if (check_available(page, row, col, width, height)) {
                    return {page, row, col};
                }
            }
        }
    }
    return {};
}

bool Inventory::has_item(nw::Item* item) const noexcept
{
    if (!item) { return false; }
    const auto handle = item->handle();
    auto it = std::find_if(items.begin(), items.end(),
        [handle](const nw::InventoryItem& ii) {
            return ii.item.is<ObjectHandle>() && ii.item.as<ObjectHandle>() == handle;
        });

    return it != std::end(items);
}

bool Inventory::insert_item(int page, int row, int col, int width, int height)
{
    if (!check_available(page, row, col, width, height)) { return false; }

    for (int r = row; r > row - height; --r) {
        for (int c = col; c < col + width; ++c) {
            int index = r * columns_ + c;
            inventory_bitset[page].set(index);
        }
    }

    return true;
}

bool Inventory::remove_item(nw::Item* item)
{
    if (!item) { return false; }
    const auto* layout = item_layout_for(item);
    if (!layout) { return false; }

    auto it = std::find_if(items.begin(), items.end(),
        [handle = item->handle()](const nw::InventoryItem& ii) {
            return ii.item.is<ObjectHandle>() && ii.item.as<ObjectHandle>() == handle;
        });

    if (it == std::end(items)) { return false; }

    auto slot = xy_to_slot(it->pos_x, it->pos_y);
    clear_item(slot.page, slot.row, slot.col,
        layout->inventory_width, layout->inventory_height);
    items.erase(it);
    return true;
}

void Inventory::set_growable(bool value)
{
    growable_ = value;
}

size_t Inventory::size() const noexcept
{
    return items.size();
}

std::pair<uint16_t, uint16_t> Inventory::slot_to_xy(InventorySlot slot) const noexcept
{
    uint16_t x = static_cast<uint16_t>(slot.col);
    uint16_t y = static_cast<uint16_t>(slot.page * rows_ + (rows_ - 1 - slot.row));
    return {x, y};
}

InventorySlot Inventory::xy_to_slot(uint16_t x, uint16_t y) const noexcept
{
    int page = y / rows_;
    int row = rows_ - 1 - (y % rows_);
    int col = x;
    return {page, row, col};
}

bool Inventory::from_json(const nlohmann::json& archive, SerializationProfile profile)
{
    if (!archive.is_array()) { return false; }

    try {
        for (size_t i = 0; i < archive.size(); ++i) {
            bool valid_entry = true;
            InventoryItem ii;
            auto handle = owner->handle();
            if (handle.type == ObjectType::store) {
                archive[i].at("infinite").get_to(ii.infinite);
            }
            archive[i].at("position")[0].get_to(ii.pos_x);
            archive[i].at("position")[1].get_to(ii.pos_y);
            if (profile == SerializationProfile::blueprint) {
                ii.item = archive[i].at("item").get<Resref>();
            } else {
                auto temp = kernel::objects().load_instance<Item>(archive[i].at("item"));
                if (temp) {
                    ii.item = temp->handle();
                } else {
                    valid_entry = false;
                }
            }
            if (valid_entry) {
                items.push_back(std::move(ii));
            }
        }
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "Inventory::from_json exception: {}", e.what());
        return false;
    }

    return true;
}

nlohmann::json Inventory::to_json(SerializationProfile profile) const
{
    nlohmann::json j = nlohmann::json::array();

    try {
        for (const auto& it : items) {
            nlohmann::json payload;
            auto handle = owner->handle();
            if (handle.type == ObjectType::store) {
                payload["infinite"] = it.infinite;
            }

            payload["position"] = {it.pos_x, it.pos_y};
            if (auto* item = inventory_item_ptr(it)) {
                if (SerializationProfile::blueprint == profile) {
                    payload["item"] = item->resref;
                } else {
                    serialize_item_to_json(item, payload["item"], profile);
                }
            } else if (SerializationProfile::blueprint == profile && it.item.is<Resref>()) {
                payload["item"] = it.item.as<Resref>();
            } else {
                continue;
            }
            j.push_back(std::move(payload));
        }
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "inventory::to_json exception: {}", e.what());
        j.clear();
    }

    return j;
}

} // namespace nw
