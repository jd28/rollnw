#pragma once

#include "Archives.hpp"

#include <nlohmann/json.hpp>

namespace nw {

/// Convert a Gff to JSON (nwn-lib/neverwinter.nim format, I think.)
nlohmann::json gff_to_json(const GffInputArchive& gff);

} // namespace nw
