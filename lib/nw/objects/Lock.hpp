#pragma once

#include "../serialization/Serialization.hpp"

namespace nw {

/// Component for lockable objects
struct Lock {
    Lock() = default;

    bool from_json(const nlohmann::json& archive);
    nlohmann::json to_json() const;

    String key_name;
    bool key_required = false;
    bool lockable = false;
    bool locked = false;
    uint8_t lock_dc = 0;
    uint8_t unlock_dc = 0;
    bool remove_key = false;
};

bool deserialize(Lock& self, const GffStruct& archive);
bool serialize(const Lock& self, GffBuilderStruct& archive);

} // namespace nw
