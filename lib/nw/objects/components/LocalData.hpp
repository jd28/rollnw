#pragma once

#include "../ObjectBase.hpp"
#include "Location.hpp"

#include <absl/container/flat_hash_map.h>

#include <bitset>

namespace nw {

struct LocalVarType {
    static const uint32_t FLOAT = 1;
    static const uint32_t INTEGER = 2;
    static const uint32_t OBJECT = 3;
    static const uint32_t STRING = 4;
    static const uint32_t LOCATION = 5;
};

struct LocalVar {
    float float_;
    int32_t integer;
    ObjectID object;
    std::string string;
    Location loc;

    std::bitset<8> flags;
};

using LocalVarTable = absl::flat_hash_map<std::string, LocalVar>;

struct LocalData {
    LocalData() = default;

    bool from_gff(const GffInputArchiveStruct& archive);
    bool from_json(const nlohmann::json& archive);
    nlohmann::json to_json(SerializationProfile profile) const;

    float get_local_float(std::string_view var) const;
    int32_t get_local_int(std::string_view var) const;
    ObjectID get_local_object(std::string_view var) const;
    std::string get_local_string(std::string_view var) const;
    Location get_local_location(std::string_view var) const;
    size_t size() const noexcept { return vars_.size(); }

private:
    LocalVarTable vars_;
};

// [TODO] NWNX:EE POS, Sqlite3

} // namespace nw
