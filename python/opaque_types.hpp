#pragma once

#include <nw/components/Area.hpp>
#include <nw/components/Creature.hpp>
#include <nw/components/Door.hpp>
#include <nw/components/Encounter.hpp>
#include <nw/components/Item.hpp>
#include <nw/components/Module.hpp>
#include <nw/components/ObjectBase.hpp>
#include <nw/components/Placeable.hpp>
#include <nw/components/Sound.hpp>
#include <nw/components/Store.hpp>
#include <nw/components/Trigger.hpp>
#include <nw/components/Waypoint.hpp>
#include <nw/resources/ResourceDescriptor.hpp>
#include <nw/resources/Resref.hpp>

#include <glm/vec3.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl_bind.h>

#include <memory>
#include <string>
#include <vector>

PYBIND11_MAKE_OPAQUE(std::vector<glm::vec3>);
PYBIND11_MAKE_OPAQUE(std::vector<nw::InventoryItem>);
PYBIND11_MAKE_OPAQUE(std::vector<nw::Resref>);
PYBIND11_MAKE_OPAQUE(std::vector<nw::Resource>);
PYBIND11_MAKE_OPAQUE(std::vector<nw::ResourceDescriptor>);
PYBIND11_MAKE_OPAQUE(std::vector<int64_t>);
PYBIND11_MAKE_OPAQUE(std::vector<int32_t>);
PYBIND11_MAKE_OPAQUE(std::vector<int16_t>);
PYBIND11_MAKE_OPAQUE(std::vector<int8_t>);
PYBIND11_MAKE_OPAQUE(std::vector<uint64_t>);
PYBIND11_MAKE_OPAQUE(std::vector<uint32_t>);
PYBIND11_MAKE_OPAQUE(std::vector<uint16_t>);
PYBIND11_MAKE_OPAQUE(std::vector<uint8_t>);
PYBIND11_MAKE_OPAQUE(std::vector<std::string>);
PYBIND11_MAKE_OPAQUE(std::vector<nw::Area*>);
PYBIND11_MAKE_OPAQUE(std::vector<nw::Tile>);
PYBIND11_MAKE_OPAQUE(std::vector<nw::Creature*>);
PYBIND11_MAKE_OPAQUE(std::vector<nw::Door*>);
PYBIND11_MAKE_OPAQUE(std::vector<nw::Encounter*>);
PYBIND11_MAKE_OPAQUE(std::vector<nw::Item*>);
PYBIND11_MAKE_OPAQUE(std::vector<nw::Placeable*>);
PYBIND11_MAKE_OPAQUE(std::vector<nw::Sound*>);
PYBIND11_MAKE_OPAQUE(std::vector<nw::Store*>);
PYBIND11_MAKE_OPAQUE(std::vector<nw::Trigger*>);
PYBIND11_MAKE_OPAQUE(std::vector<nw::Waypoint*>);

void bind_opaque_types(pybind11::module& m);
