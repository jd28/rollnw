#include "object_workbench.hpp"

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
    switch (type) {
    case nw::ObjectType::sound:
    case nw::ObjectType::store:
    case nw::ObjectType::trigger:
        return true;
    default:
        return type == nw::ObjectType::item
            && surface == ObjectWorkbenchSurface::item_properties;
    }
}

bool object_has_grid_inventory(nw::ObjectType type) noexcept
{
    return type == nw::ObjectType::creature
        || type == nw::ObjectType::item
        || type == nw::ObjectType::placeable;
}

} // namespace nw::toolset
