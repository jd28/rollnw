#include "opaque_types.hpp"

#include <nw/objects/Item.hpp>

namespace py = pybind11;

void bind_opaque_types(py::module& m)
{
    py::bind_vector<std::vector<int64_t>>(m, "Int64Vector");
    py::bind_vector<std::vector<int32_t>>(m, "Int32Vector");
    py::bind_vector<std::vector<int16_t>>(m, "Int16Vector");
    py::bind_vector<std::vector<int8_t>>(m, "Int8Vector");
    py::bind_vector<std::vector<uint64_t>>(m, "UInt64Vector");
    py::bind_vector<std::vector<uint32_t>>(m, "UInt32Vector");
    py::bind_vector<std::vector<uint16_t>>(m, "UInt16Vector");
    py::bind_vector<std::vector<uint8_t>>(m, "UInt8Vector");
    py::bind_vector<std::vector<std::string>>(m, "StringVector");
    py::bind_vector<std::vector<glm::vec3>>(m, "Vec3Vector");
    py::bind_vector<std::vector<nw::model::Vertex>>(m, "VertexVector");
    py::bind_vector<std::vector<nw::model::SkinVertex>>(m, "SkinVertexVector");
    py::bind_vector<std::vector<nw::model::Node*>>(m, "NodePtrVector");
    py::bind_vector<std::vector<nw::ClassEntry>>(m, "ClassEntryVector");
    py::bind_vector<std::vector<nw::InventoryItem>>(m, "InventoryItemVector");
    py::bind_vector<std::vector<nw::LevelUp>>(m, "LevelUpVector");
    py::bind_vector<std::vector<nw::Resref>>(m, "ResrefVector");
    py::bind_vector<std::vector<nw::Resource>>(m, "ResourceVector");
    py::bind_vector<std::vector<nw::ResourceDescriptor>>(m, "ResourceDescriptorVector");
    py::bind_vector<std::vector<nw::Tile>>(m, "TileVector");

    py::bind_vector<std::vector<nw::Area*>>(m, "AreaVector");
    py::bind_vector<std::vector<nw::Creature*>>(m, "CreatureVector");
    py::bind_vector<std::vector<nw::Door*>>(m, "DoorVector");
    py::bind_vector<std::vector<nw::Encounter*>>(m, "EncounterVector");
    py::bind_vector<std::vector<nw::Item*>>(m, "ItemVector");
    py::bind_vector<std::vector<nw::Placeable*>>(m, "PlaceableVector");
    py::bind_vector<std::vector<nw::Sound*>>(m, "SoundVector");
    py::bind_vector<std::vector<nw::Store*>>(m, "StoreVector");
    py::bind_vector<std::vector<nw::Trigger*>>(m, "TriggerVector");
    py::bind_vector<std::vector<nw::Waypoint*>>(m, "WaypointVector");
}
