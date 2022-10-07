#pragma once

#include "../serialization/Archives.hpp"

namespace nw {

struct Trap {
    Trap() = default;

    bool from_gff(const GffStruct& archive);
    bool from_json(const nlohmann::json& archive);
    bool to_gff(GffBuilderStruct& archive) const;
    nlohmann::json to_json() const;

    bool is_trapped = false;
    uint8_t type = 0;
    bool detectable = false;
    uint8_t detect_dc = 0;
    bool disarmable = false;
    uint8_t disarm_dc = 0;
    bool one_shot = false;
};

} // namespace nw
