#include "object_workbench.hpp"

namespace nw::toolset {

ObjectWorkbenchSurface default_object_workbench_surface()
{
    return ObjectWorkbenchSurface::details;
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
