#pragma once

#include "GffInputArchive.hpp"
#include "GffOutputArchive.hpp"

#include <nlohmann/json_fwd.hpp>

namespace nw {

/// Convert a Gff to JSON (nwn-lib/neverwinter.nim format, I think.)
nlohmann::json gff_to_gffjson(const GffInputArchive& gff);

} // namespace nw
