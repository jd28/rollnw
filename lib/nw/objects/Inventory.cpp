#include "Inventory.hpp"

#include "../kernel/Objects.hpp"
#include "../kernel/Rules.hpp"
#include "../serialization/Gff.hpp"
#include "../serialization/GffBuilder.hpp"
#include "Item.hpp"

#include <nlohmann/json.hpp>

namespace nw {

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
    , items(128, allocator) // There is some maximum number of items..
    , pages_{pages}
    , rows_{std::min(rows, max_rows)}
    , columns_{std::min(columns, max_columns)}
{
    inventory_bitset.resize(pages_);
}

void Inventory::destroy()
{
    for (auto& it : items) {
        if (!it.item.is<Item*>()) { continue; }
        auto item = it.item.as<Item*>();
        item->inventory.destroy();
        nw::kernel::objects().destroy(item->handle());
    }
    items.clear();
    inventory_bitset.clear();
}

bool Inventory::instantiate()
{
    for (auto& it : items) {
        if (it.item.is<Resref>()) {
            auto temp = kernel::objects().load<Item>(it.item.as<Resref>());
            if (temp) {
                it.item = temp;
            } else {
                LOG_F(WARNING, "failed to instantiate item, perhaps you're missing '{}.uti'?",
                    it.item.as<Resref>());
            }
        }

        if (it.item.is<nw::Item*>()) {
            auto item = it.item.as<nw::Item*>();
            auto bi = nw::kernel::rules().baseitems.get(item->baseitem);
            if (bi) {
                auto slot = xy_to_slot(it.pos_x, it.pos_y);
                auto [width, height] = bi->inventory_slot_size;
                if (pages_ <= slot.page) {
                    inventory_bitset.resize(slot.page + 1);
                    pages_ = slot.page + 1;
                }
                insert_item(slot.page, slot.row, slot.col, width, height);
            }
        }
    }
    return true;
}

bool Inventory::add_item(nw::Item* item)
{
    if (!item) { return false; }
    auto bi = nw::kernel::rules().baseitems.get(item->baseitem);
    if (!bi) { return false; }
    if (has_item(item)) { return false; }

    auto [width, height] = bi->inventory_slot_size;
    auto slot = find_slot(width, height);
    if (slot.page == -1) { return false; }
    insert_item(slot.page, slot.row, slot.col, width, height);
    auto [x, y] = slot_to_xy(slot);

    InventoryItem ii;
    ii.item = item;
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
    auto bi = nw::kernel::rules().baseitems.get(item->baseitem);
    if (!bi) { return false; }

    auto [width, height] = bi->inventory_slot_size;
    auto slot = find_slot(width, height);
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
    auto it = std::find_if(items.begin(), items.end(),
        [item](const nw::InventoryItem& ii) { return ii.item == item; });

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
    auto bi = nw::kernel::rules().baseitems.get(item->baseitem);
    if (!bi) { return false; }

    auto it = std::find_if(items.begin(), items.end(),
        [item](const nw::InventoryItem& ii) { return ii.item == item; });

    if (it == std::end(items)) { return false; }

    auto slot = xy_to_slot(it->pos_x, it->pos_y);
    auto [width, height] = bi->inventory_slot_size;
    clear_item(slot.page, slot.row, slot.col, width, height);
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
                ii.item = temp;
                if (!temp) {
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
            j.push_back({});
            auto& payload = j.back();
            auto handle = owner->handle();
            if (handle.type == ObjectType::store) {
                payload["infinite"] = it.infinite;
            }

            payload["position"] = {it.pos_x, it.pos_y};
            if (it.item.is<Item*>()) {
                if (SerializationProfile::blueprint == profile) {
                    payload["item"] = it.item.as<Item*>()->common.resref;
                } else {
                    Item::serialize(it.item.as<Item*>(), payload["item"], profile);
                }
            } else {
                if (SerializationProfile::blueprint == profile) {
                    payload["item"] = it.item.as<Resref>();
                }
            }
        }
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "inventory::to_json exception: {}", e.what());
        j.clear();
    }

    return j;
}

bool deserialize(Inventory& self, const GffStruct& archive, SerializationProfile profile)
{
    size_t sz = archive["ItemList"].size();
    for (size_t i = 0; i < sz; ++i) {
        bool valid_entry = true;
        auto st = archive["ItemList"][i];
        InventoryItem ii;
        auto handle = self.owner->handle();
        if (handle.type == ObjectType::store) {
            st.get_to("Infinite", ii.infinite, false);
        }
        st.get_to("Repos_PosX", ii.pos_x);
        st.get_to("Repos_Posy", ii.pos_y); // Not typo..

        if (SerializationProfile::blueprint == profile) {
            if (auto r = st.get<Resref>("InventoryRes")) {
                ii.item = *r;
            }
        } else if (SerializationProfile::instance == profile) {
            auto temp = kernel::objects().load_instance<Item>(st);
            ii.item = temp;
            if (!temp) {
                valid_entry = false;
            }
        }
        if (valid_entry) {
            self.items.push_back(std::move(ii));
        }
    }

    return true;
}

bool serialize(const Inventory& self, GffBuilderStruct& archive, SerializationProfile profile)
{
    if (self.items.empty()) {
        return true;
    }
    auto& list = archive.add_list("ItemList");
    for (const auto& it : self.items) {
        auto& str = list.push_back(static_cast<uint32_t>(list.size()))
                        .add_field("Repos_PosX", it.pos_x)
                        .add_field("Repos_Posy", it.pos_y);

        auto handle = self.owner->handle();
        if (handle.type == ObjectType::store && it.infinite) {
            str.add_field("Infinite", it.infinite);
        }

        if (SerializationProfile::blueprint == profile) {
            if (it.item.is<Resref>()) {
                str.add_field("InventoryRes", it.item.as<Resref>());
            } else {
                str.add_field("InventoryRes", it.item.as<Item*>()->common.resref);
            }
        } else {
            serialize(it.item.as<Item*>(), str, profile);
        }
    }
    return true;
}

} // namespace nw
