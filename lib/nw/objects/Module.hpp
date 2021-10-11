#pragma once

#include "../formats/Gff.hpp"
#include "Serialization.hpp"

namespace nw {

// For our purpose..  Module is going to be synonym to module.ifo.  NOT the Erf Container.

struct Module {
    explicit Module(const GffStruct gff);
};

} // namespace nw
