#pragma once

#include <nw/formats/Dialog.hpp>
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
#include <nw/resources/assets.hpp>
#include <nw/script/Nss.hpp>

#include <glm/vec3.hpp>
#include <nanobind/nanobind.h>
#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/vector.h>

#include <memory>
#include <string>
#include <vector>

NB_MAKE_OPAQUE(std::vector<glm::vec3>)
NB_MAKE_OPAQUE(std::vector<nw::model::Vertex>)
NB_MAKE_OPAQUE(std::vector<nw::model::SkinVertex>)
NB_MAKE_OPAQUE(std::vector<nw::model::Node*>)
NB_MAKE_OPAQUE(std::vector<nw::ClassEntry>)
NB_MAKE_OPAQUE(std::vector<nw::InventoryItem>)
NB_MAKE_OPAQUE(std::vector<nw::Resref>)
NB_MAKE_OPAQUE(std::vector<nw::Resource>)
NB_MAKE_OPAQUE(std::vector<nw::LevelUp>)
NB_MAKE_OPAQUE(std::vector<int64_t>)
NB_MAKE_OPAQUE(std::vector<int32_t>)
NB_MAKE_OPAQUE(std::vector<int16_t>)
NB_MAKE_OPAQUE(std::vector<int8_t>)
NB_MAKE_OPAQUE(std::vector<uint64_t>)
NB_MAKE_OPAQUE(std::vector<uint32_t>)
NB_MAKE_OPAQUE(std::vector<uint16_t>)
NB_MAKE_OPAQUE(std::vector<uint8_t>)
NB_MAKE_OPAQUE(std::vector<std::string>)
NB_MAKE_OPAQUE(std::vector<nw::Area*>)
NB_MAKE_OPAQUE(std::vector<nw::AreaTile>)
NB_MAKE_OPAQUE(std::vector<nw::Creature*>)
NB_MAKE_OPAQUE(std::vector<nw::Door*>)
NB_MAKE_OPAQUE(std::vector<nw::Encounter*>)
NB_MAKE_OPAQUE(std::vector<nw::Item*>)
NB_MAKE_OPAQUE(std::vector<nw::Placeable*>)
NB_MAKE_OPAQUE(std::vector<nw::Sound*>)
NB_MAKE_OPAQUE(std::vector<nw::Store*>)
NB_MAKE_OPAQUE(std::vector<nw::Trigger*>)
NB_MAKE_OPAQUE(std::vector<nw::Waypoint*>)
NB_MAKE_OPAQUE(std::vector<nw::script::Symbol>)
NB_MAKE_OPAQUE(std::vector<nw::DialogPtr*>)

void bind_opaque_types(nanobind::module_& m);
