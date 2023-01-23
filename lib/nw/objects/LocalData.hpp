#pragma once

#include "Location.hpp"
#include "ObjectBase.hpp"

#include <absl/container/flat_hash_map.h>

#include <bitset>

namespace nw {

struct LocalVarType {
    static constexpr uint32_t integer = 1;
    static constexpr uint32_t float_ = 2;
    static constexpr uint32_t string = 3;
    static constexpr uint32_t object = 4;
    static constexpr uint32_t location = 5;
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

    bool from_json(const nlohmann::json& archive);
    nlohmann::json to_json(SerializationProfile profile) const;

    void delete_float(std::string_view var);
    void delete_int(std::string_view var);
    void delete_object(std::string_view var);
    void delete_string(std::string_view var);
    void delete_location(std::string_view var);

    float get_float(std::string_view var) const;
    int32_t get_int(std::string_view var) const;
    ObjectID get_object(std::string_view var) const;
    std::string get_string(std::string_view var) const;
    Location get_location(std::string_view var) const;

    void set_float(std::string_view var, float value);
    void set_int(std::string_view var, int32_t value);
    void set_object(std::string_view var, ObjectID value);
    void set_string(std::string_view var, std::string_view value);
    void set_location(std::string_view var, Location value);

    size_t size() const noexcept { return vars_.size(); }

#ifdef ROLLNW_ENABLE_LEGACY
    friend bool deserialize(LocalData& self, const GffStruct& archive);
    friend bool serialize(const LocalData& self, GffBuilderStruct& archive, SerializationProfile profile);
#endif

private:
    LocalVarTable vars_;
};

// [TODO] NWNX:EE POS, Sqlite3

#ifdef ROLLNW_ENABLE_LEGACY
bool deserialize(LocalData& self, const GffStruct& archive);
bool serialize(const LocalData& self, GffBuilderStruct& archive, SerializationProfile profile);
#endif

} // namespace nw
