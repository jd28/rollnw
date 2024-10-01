#pragma once

#include "../serialization/Serialization.hpp"
#include "Location.hpp"

#include <absl/container/flat_hash_map.h>

#include <bitset>
#include <string>

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
    String string;
    Location loc;

    std::bitset<8> flags;
};

using LocalVarTable = absl::flat_hash_map<String, LocalVar>;

struct LocalData {
    LocalData() = default;

    bool from_json(const nlohmann::json& archive);
    nlohmann::json to_json(SerializationProfile profile) const;

    LocalVarTable::iterator begin() { return vars_.begin(); }
    LocalVarTable::const_iterator begin() const { return vars_.begin(); }
    LocalVarTable::iterator end() { return vars_.end(); }
    LocalVarTable::const_iterator end() const { return vars_.end(); }

    /// Clears a variable by type
    void clear(StringView var, uint32_t type);

    /// Clears all variables by type
    void clear_all(uint32_t type);

    void delete_float(StringView var);
    void delete_int(StringView var);
    void delete_object(StringView var);
    void delete_string(StringView var);
    void delete_location(StringView var);

    float get_float(StringView var) const;
    int32_t get_int(StringView var) const;
    ObjectID get_object(StringView var) const;
    String get_string(StringView var) const;
    Location get_location(StringView var) const;

    void set_float(StringView var, float value);
    void set_int(StringView var, int32_t value);
    void set_object(StringView var, ObjectID value);
    void set_string(StringView var, StringView value);
    void set_location(StringView var, Location value);

    size_t size() const noexcept { return vars_.size(); }

    friend bool deserialize(LocalData& self, const GffStruct& archive);
    friend bool serialize(const LocalData& self, GffBuilderStruct& archive, SerializationProfile profile);

private:
    LocalVarTable vars_;
};

// [TODO] NWNX:EE POS, Sqlite3

bool deserialize(LocalData& self, const GffStruct& archive);
bool serialize(const LocalData& self, GffBuilderStruct& archive, SerializationProfile profile);

} // namespace nw
