#pragma once

#include "../serialization/Serialization.hpp"

#include <nlohmann/json.hpp>

namespace nw {

/// Convert a Gff to JSON (nwn-lib/neverwinter.nim format, I think.)
nlohmann::json gff_to_json(const Gff& gff);

} // namespace nw
