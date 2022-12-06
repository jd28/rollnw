#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstdint>

namespace nw {

struct Saves {
    int16_t fort = 0;
    int16_t reflex = 0;
    int16_t will = 0;
};

void from_json(const nlohmann::json& json, Saves& saves);
void to_json(nlohmann::json& json, const Saves& saves);

} // namespace nw
