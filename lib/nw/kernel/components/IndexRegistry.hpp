#pragma once

#include "../../rules/Index.hpp"
#include "../../util/InternedString.hpp"

namespace nw {

// == IndexRegistry ========================================================

struct IndexRegistry {
    using container_type = absl::flat_hash_map<
        InternedString,
        size_t,
        InternedStringHash,
        InternedStringEq>;

    /// Adds index
    Index add(std::string_view name, size_t value);

    /// Clears all indicies
    void clear();

    /// Gets index
    Index get(std::string_view name) const;

private:
    container_type constants_;
};

} // namespace nw
