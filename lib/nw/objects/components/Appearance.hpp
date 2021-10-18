#pragma once

#include "../Serialization.hpp"

namespace nw {

struct Appearance {
    Appearance(const GffStruct gff);

    int32_t phenotype = 0;
    uint32_t tail = 0;
    uint32_t wings = 0;

    uint16_t id = 0;

    uint8_t hair = 0;
    uint8_t skin = 0;
    uint8_t tattoo1 = 0;
    uint8_t tattoo2 = 0;
};

} // namespace nw
