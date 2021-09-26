#pragma once

#include "../formats/Gff.hpp"

#include <nlohmann/json.hpp>

namespace nw {

// JSON
nlohmann::json gff_to_json(const Gff& gff);
bool gff_to_json(const GffField field, nlohmann::json& cursor);
bool gff_to_json(const GffStruct str, nlohmann::json& cursor);

} // namespace nw
