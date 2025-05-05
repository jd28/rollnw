#pragma once

#include <filesystem>
#include <variant>

namespace nw {

/// @private
struct ErfElementInfo {
    uint32_t offset;
    uint32_t size;
};

/// @private
using ErfElement = std::variant<ErfElementInfo, std::filesystem::path>;

enum struct ErfType {
    erf,
    hak,
    mod,
    sav
};

enum struct ErfVersion {
    v1_0,
    v1_1
};

} // namespace nw
