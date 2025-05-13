#include <nw/serialization/Serialization.hpp>

#include <nanobind/nanobind.h>

void init_serialization(nanobind::module_& m)
{
    nanobind::enum_<nw::SerializationProfile>(m, "SerializationProfile")
        .value("any", nw::SerializationProfile::any)
        .value("blueprint", nw::SerializationProfile::blueprint)
        .value("instance", nw::SerializationProfile::instance)
        .value("savegame", nw::SerializationProfile::savegame);
}
