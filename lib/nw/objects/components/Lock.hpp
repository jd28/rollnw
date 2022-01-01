#pragma once

#include "../../serialization/Archives.hpp"

namespace nw {

struct Lock {
    Lock() = default;

    bool from_gff(const GffInputArchiveStruct& archive);
    bool from_json(const nlohmann::json& archive);
    bool to_gff(GffOutputArchiveStruct& archive) const;
    nlohmann::json to_json() const;

    std::string key_name;
    bool key_required = false;
    bool lockable = false;
    bool locked = false;
    uint8_t lock_dc = 0;
    uint8_t unlock_dc = 0;
    bool remove_key = false;
};

} // namespace nw
