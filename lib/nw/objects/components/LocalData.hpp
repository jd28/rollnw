#pragma once

#include "../ObjectBase.hpp"
#include "Location.hpp"

#include <absl/container/flat_hash_map.h>

#include <bitset>

namespace nw {

struct LocalVarType {
    static constexpr uint32_t INTEGER = 1;
    static constexpr uint32_t FLOAT = 2;
    static constexpr uint32_t STRING = 3;
    static constexpr uint32_t OBJECT = 4;
    static constexpr uint32_t LOCATION = 5;
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
    bool to_gff(GffOutputArchiveStruct& archive) const;
    nlohmann::json to_json(SerializationProfile profile) const;

    void delete_local_float(std::string_view var);
    void delete_local_int(std::string_view var);
    void delete_local_object(std::string_view var);
    void delete_local_string(std::string_view var);
    void delete_local_location(std::string_view var);

    float get_local_float(std::string_view var) const;
    int32_t get_local_int(std::string_view var) const;
    ObjectID get_local_object(std::string_view var) const;
    std::string get_local_string(std::string_view var) const;
    Location get_local_location(std::string_view var) const;

    void set_local_float(std::string_view var, float value);
    void set_local_int(std::string_view var, int32_t value);
    void set_local_object(std::string_view var, ObjectID value);
    void set_local_string(std::string_view var, std::string_view value);
    void set_local_location(std::string_view var, Location value);

    size_t size() const noexcept { return vars_.size(); }

private:
    LocalVarTable vars_;
};

// [TODO] NWNX:EE POS, Sqlite3

} // namespace nw
