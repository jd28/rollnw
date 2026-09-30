#include "object_workbench.hpp"

#include <absl/container/flat_hash_set.h>
#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Placeable.hpp>
#include <nw/objects/Store.hpp>

#include <array>
#include <utility>

namespace nw::toolset {

ObjectWorkbenchSurface default_object_workbench_surface()
{
    return ObjectWorkbenchSurface::details;
}

std::optional<ObjectWorkbenchSurface> object_workbench_surface_from_name(std::string_view name) noexcept
{
    static constexpr std::array names{
        std::pair{"details", ObjectWorkbenchSurface::details},
        std::pair{"locstring", ObjectWorkbenchSurface::locstring},
        std::pair{"sheet", ObjectWorkbenchSurface::sheet},
        std::pair{"variables", ObjectWorkbenchSurface::variables},
        std::pair{"haks", ObjectWorkbenchSurface::haks},
        std::pair{"classes", ObjectWorkbenchSurface::classes},
        std::pair{"appearance", ObjectWorkbenchSurface::appearance},
        std::pair{"item-properties", ObjectWorkbenchSurface::item_properties},
        std::pair{"feats", ObjectWorkbenchSurface::feats},
        std::pair{"spells", ObjectWorkbenchSurface::spells},
        std::pair{"inventory", ObjectWorkbenchSurface::inventory},
        std::pair{"spawns", ObjectWorkbenchSurface::spawns},
        std::pair{"sounds", ObjectWorkbenchSurface::sounds},
        std::pair{"store-inventory", ObjectWorkbenchSurface::store_inventory},
    };
    for (const auto& [text, surface] : names) {
        if (name == text) { return surface; }
    }
    return std::nullopt;
}

bool data_workbench_only(nw::ObjectType type,
    ObjectWorkbenchSurface surface) noexcept
{
    (void)surface;
    switch (type) {
    case nw::ObjectType::item:
    case nw::ObjectType::sound:
    case nw::ObjectType::store:
    case nw::ObjectType::trigger:
        return true;
    default:
        return false;
    }
}

bool object_has_grid_inventory(nw::ObjectType type) noexcept
{
    return type == nw::ObjectType::creature
        || type == nw::ObjectType::item
        || type == nw::ObjectType::placeable;
}

PlacedItemOwners collect_placed_item_owners(ObjectHandle area_handle)
{
    PlacedItemOwners result;
    const auto* area = kernel::objects().get<Area>(area_handle);
    if (!area) {
        result.error = "The containing Area is unavailable";
        return result;
    }
    const auto append = [&](ObjectHandle object, uint32_t parent, uint32_t root, ObjectHandle visual) {
        if (result.rows.size() >= UINT32_MAX) { return false; }
        result.rows.push_back({object, parent, root, visual});
        return true;
    };
    const auto roots = [&](const auto& objects) {
        for (const auto* object : objects) {
            if (!object || !append(object->handle(), UINT32_MAX, static_cast<uint32_t>(result.rows.size()), object->handle())) { return false; }
        }
        return true;
    };
    bool valid = roots(area->creatures) && roots(area->items)
        && roots(area->placeables) && roots(area->stores);
    absl::flat_hash_set<uint64_t> visited;
    for (uint32_t index = 0; valid && index < result.rows.size(); ++index) {
        const auto row = result.rows[index];
        const auto* object = kernel::objects().get_object_base(row.object);
        if (!object || !visited.insert(row.object.to_ull()).second) {
            valid = false;
            break;
        }
        const auto inventory = [&](const Inventory& source) {
            for (const auto& entry : source.items) {
                const auto* item = inventory_item_ptr(entry);
                if (!item || !append(item->handle(), index, row.root, ObjectHandle{})) { return false; }
            }
            return true;
        };
        if (const auto* source = kernel::objects().components().find_inventory(*object)) {
            valid = inventory(*source);
        }
        if (const auto* creature = object->as_creature()) {
            for (const auto& entry : creature->equipment.equips) {
                if (entry.empty()) { continue; }
                const auto* item = equip_item_ptr(entry);
                if (!item || !append(item->handle(), index, row.root, row.object)) {
                    valid = false;
                    break;
                }
            }
        } else if (const auto* store = object->as_store()) {
            const auto& source = store->inventory();
            valid = valid && inventory(source.armor) && inventory(source.miscellaneous)
                && inventory(source.potions) && inventory(source.rings) && inventory(source.weapons);
        }
    }
    if (!valid) {
        result.rows.clear();
        result.error = "The Area contains missing, unresolved or repeated item ownership";
    }
    return result;
}

} // namespace nw::toolset
