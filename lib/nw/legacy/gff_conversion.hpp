#pragma once

#include <nlohmann/json_fwd.hpp>

namespace nw {

struct Gff;

/// Convert a Gff to JSON (nwn-lib/neverwinter.nim format, I think.)
nlohmann::json gff_to_gffjson(const Gff& gff);

} // namespace nw
