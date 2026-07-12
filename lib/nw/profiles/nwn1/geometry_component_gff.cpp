#include "geometry_component_gff.hpp"

#include "../../kernel/Kernel.hpp"
#include "../../objects/ObjectComponentSystem.hpp"
#include "../../objects/ObjectManager.hpp"
#include "../../serialization/Gff.hpp"
#include "../../serialization/GffBuilder.hpp"

#include <glm/glm.hpp>

#include <span>

namespace nwn1 {

namespace {

bool import_empty_geometry(nw::ObjectHandle owner)
{
    return nw::kernel::objects().components().set_geometry(owner, std::span<const glm::vec3>{});
}

bool import_trigger_geometry_points(nw::ObjectHandle owner, const nw::GffStruct& archive)
{
    const size_t sz = archive["Geometry"].size();
    nw::Vector<glm::vec3> geometry;
    geometry.reserve(sz);
    for (size_t i = 0; i < sz; ++i) {
        glm::vec3 v;
        archive["Geometry"][i].get_to("PointX", v[0]);
        archive["Geometry"][i].get_to("PointY", v[1]);
        archive["Geometry"][i].get_to("PointZ", v[2]);
        geometry.push_back(v);
    }
    return nw::kernel::objects().components().set_geometry(
        owner, std::span<const glm::vec3>{geometry.data(), geometry.size()});
}

bool import_encounter_geometry_points(nw::ObjectHandle owner, const nw::GffStruct& archive)
{
    const size_t sz = archive["Geometry"].size();
    nw::Vector<glm::vec3> geometry;
    geometry.reserve(sz);
    for (size_t i = 0; i < sz; ++i) {
        glm::vec3 v;
        archive["Geometry"][i].get_to("X", v.x);
        archive["Geometry"][i].get_to("Y", v.y);
        archive["Geometry"][i].get_to("Z", v.z);
        geometry.push_back(v);
    }
    return nw::kernel::objects().components().set_geometry(
        owner, std::span<const glm::vec3>{geometry.data(), geometry.size()});
}

bool import_encounter_spawn_points(nw::ObjectHandle owner, const nw::GffStruct& archive)
{
    const size_t sz = archive["SpawnPointList"].size();
    nw::Vector<nw::ObjectSpawnPoint> spawn_points;
    spawn_points.resize(sz);
    for (size_t i = 0; i < sz; ++i) {
        auto row = archive["SpawnPointList"][i];
        row.get_to("Orientation", spawn_points[i].orientation);
        row.get_to("X", spawn_points[i].position.x);
        row.get_to("Y", spawn_points[i].position.y);
        row.get_to("Z", spawn_points[i].position.z);
    }
    return nw::kernel::objects().components().set_spawn_points(
        owner, std::span<const nw::ObjectSpawnPoint>{spawn_points.data(), spawn_points.size()});
}

} // namespace

bool import_trigger_geometry_from_gff(
    nw::ObjectHandle owner,
    const nw::GffStruct& archive,
    nw::SerializationProfile profile)
{
    bool ok = true;
    if (profile != nw::SerializationProfile::blueprint) {
        ok = import_trigger_geometry_points(owner, archive);
    } else {
        ok = import_empty_geometry(owner);
    }
    if (!ok) { return false; }

    float highlight_height = 0.0f;
    archive.get_to("HighlightHeight", highlight_height);
    return nw::kernel::objects().components().set_highlight_height(owner, highlight_height);
}

bool export_trigger_geometry_to_gff(
    nw::ObjectHandle owner,
    nw::GffBuilderStruct& archive,
    nw::SerializationProfile profile)
{
    if (profile == nw::SerializationProfile::blueprint) { return true; }

    auto& list = archive.add_list("Geometry");
    if (const auto* geometry = nw::kernel::objects().components().find_geometry(owner)) {
        for (const auto& point : geometry->points) {
            list.push_back(3)
                .add_field("PointX", point[0])
                .add_field("PointY", point[1])
                .add_field("PointZ", point[2]);
        }
    }
    return true;
}

bool import_encounter_geometry_from_gff(
    nw::ObjectHandle owner,
    const nw::GffStruct& archive,
    nw::SerializationProfile profile)
{
    if (profile != nw::SerializationProfile::instance) {
        return import_empty_geometry(owner);
    }

    return import_encounter_geometry_points(owner, archive)
        && import_encounter_spawn_points(owner, archive);
}

bool export_encounter_geometry_to_gff(
    nw::ObjectHandle owner,
    nw::GffBuilderStruct& archive,
    nw::SerializationProfile profile)
{
    if (profile == nw::SerializationProfile::blueprint) { return true; }

    auto& geo_list = archive.add_list("Geometry");
    const auto* geometry = nw::kernel::objects().components().find_geometry(owner);
    if (geometry) {
        for (const auto& g : geometry->points) {
            geo_list.push_back(1)
                .add_field("X", g.x)
                .add_field("Y", g.y)
                .add_field("Z", g.z);
        }
    }

    auto& sp_list = archive.add_list("SpawnPointList");
    if (geometry) {
        for (const auto& sp : geometry->spawn_points) {
            sp_list.push_back(0)
                .add_field("Orientation", sp.orientation)
                .add_field("X", sp.position.x)
                .add_field("Y", sp.position.y)
                .add_field("Z", sp.position.z);
        }
    }
    return true;
}

} // namespace nwn1
