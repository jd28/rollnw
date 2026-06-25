#pragma once

#include <cstdint>
#include <limits>

namespace nw::render {

using ModelAttachmentPointIndex = uint32_t;

inline constexpr ModelAttachmentPointIndex kInvalidModelAttachmentPointIndex =
    std::numeric_limits<ModelAttachmentPointIndex>::max();

} // namespace nw::render
