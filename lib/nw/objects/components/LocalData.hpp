#pragma once

#include "../ObjectBase.hpp"
#include "Location.hpp"

#include <absl/container/flat_hash_map.h>

namespace nw {

struct LocalVar {
    float float_;
    int32_t integer;
    ObjectID object;
    std::string string;
    Location loc;
};

using LocalVarTable = absl::flat_hash_map<std::string, LocalVar>;

struct LocalData {
    LocalData() = default;
    LocalData(const GffInputArchiveStruct gff);

    float get_local_float(absl::string_view var) const;
    int32_t get_local_int(absl::string_view var) const;
    ObjectID get_local_object(absl::string_view var) const;
    std::string get_local_string(absl::string_view var) const;
    Location get_local_location(absl::string_view var) const;
    size_t size() const noexcept { return vars_.size(); }

private:
    LocalVarTable vars_;
};

// [TODO] NWNX:EE POS, Sqlite3

} // namespace nw
