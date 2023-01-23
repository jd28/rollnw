#pragma once

#include "../serialization/Archives.hpp"

namespace nw {

struct Trap {
    Trap() = default;

    bool from_json(const nlohmann::json& archive);
    nlohmann::json to_json() const;

    bool is_trapped = false;
    uint8_t type = 0;
    bool detectable = false;
    uint8_t detect_dc = 0;
    bool disarmable = false;
    uint8_t disarm_dc = 0;
    bool one_shot = false;
};

#ifdef ROLLNW_ENABLE_LEGACY
bool deserialize(Trap& self, const GffStruct& archive);
bool serialize(const Trap& self, GffBuilderStruct& archive);
#endif

} // namespace nw
