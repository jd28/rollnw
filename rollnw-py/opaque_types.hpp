#pragma once

#include <nw/model/Mdl.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Door.hpp>
#include <nw/objects/Encounter.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/Module.hpp>
#include <nw/objects/ObjectBase.hpp>
#include <nw/objects/Placeable.hpp>
#include <nw/objects/Sound.hpp>
#include <nw/objects/Store.hpp>
#include <nw/objects/Trigger.hpp>
#include <nw/objects/Waypoint.hpp>
#include <nw/resources/ResourceDescriptor.hpp>
#include <nw/resources/Resref.hpp>

#include <glm/vec3.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl_bind.h>

#include <memory>
#include <string>
#include <vector>

PYBIND11_MAKE_OPAQUE(std::vector<glm::vec3>)
PYBIND11_MAKE_OPAQUE(std::vector<nw::model::Vertex>)
PYBIND11_MAKE_OPAQUE(std::vector<nw::model::SkinVertex>)
PYBIND11_MAKE_OPAQUE(std::vector<nw::model::Node*>)
PYBIND11_MAKE_OPAQUE(std::vector<nw::ClassEntry>)
PYBIND11_MAKE_OPAQUE(std::vector<nw::InventoryItem>)
PYBIND11_MAKE_OPAQUE(std::vector<nw::Resref>)
PYBIND11_MAKE_OPAQUE(std::vector<nw::Resource>)
PYBIND11_MAKE_OPAQUE(std::vector<nw::ResourceDescriptor>)
PYBIND11_MAKE_OPAQUE(std::vector<nw::LevelUp>)
PYBIND11_MAKE_OPAQUE(std::vector<int64_t>)
PYBIND11_MAKE_OPAQUE(std::vector<int32_t>)
PYBIND11_MAKE_OPAQUE(std::vector<int16_t>)
PYBIND11_MAKE_OPAQUE(std::vector<int8_t>)
PYBIND11_MAKE_OPAQUE(std::vector<uint64_t>)
PYBIND11_MAKE_OPAQUE(std::vector<uint32_t>)
PYBIND11_MAKE_OPAQUE(std::vector<uint16_t>)
PYBIND11_MAKE_OPAQUE(std::vector<uint8_t>)
PYBIND11_MAKE_OPAQUE(std::vector<std::string>)
PYBIND11_MAKE_OPAQUE(std::vector<nw::Area*>)
PYBIND11_MAKE_OPAQUE(std::vector<nw::Tile>)
PYBIND11_MAKE_OPAQUE(std::vector<nw::Creature*>)
PYBIND11_MAKE_OPAQUE(std::vector<nw::Door*>)
PYBIND11_MAKE_OPAQUE(std::vector<nw::Encounter*>)
PYBIND11_MAKE_OPAQUE(std::vector<nw::Item*>)
PYBIND11_MAKE_OPAQUE(std::vector<nw::Placeable*>)
PYBIND11_MAKE_OPAQUE(std::vector<nw::Sound*>)
PYBIND11_MAKE_OPAQUE(std::vector<nw::Store*>)
PYBIND11_MAKE_OPAQUE(std::vector<nw::Trigger*>)
PYBIND11_MAKE_OPAQUE(std::vector<nw::Waypoint*>)

void bind_opaque_types(pybind11::module& m);
