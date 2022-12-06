#include "Saves.hpp"

#include <nlohmann/json.hpp>

namespace nw {

void from_json(const nlohmann::json& json, Saves& saves)
{
    json.at("fort").get_to(saves.fort);
    json.at("reflex").get_to(saves.reflex);
    json.at("will").get_to(saves.will);
}

void to_json(nlohmann::json& json, const Saves& saves)
{
    json["fort"] = saves.fort;
    json["reflex"] = saves.reflex;
    json["will"] = saves.will;
}

} // namespace nw
