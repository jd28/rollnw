#include <nw/serialization/Serialization.hpp>

#include <pybind11/pybind11.h>

void init_serialization(pybind11::module& m)
{
    pybind11::enum_<nw::SerializationProfile>(m, "SerializationProfile")
        .value("any", nw::SerializationProfile::any)
        .value("blueprint", nw::SerializationProfile::blueprint)
        .value("instance", nw::SerializationProfile::instance)
        .value("savegame", nw::SerializationProfile::savegame);
}
