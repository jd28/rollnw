#pragma once

#include <cstdint>

namespace nw {

/// Opaque type.. for now.
enum class ObjectID : uint32_t {};

/// Invalid object ID
static const ObjectID object_invalid{static_cast<ObjectID>(0x7f000000)};

} // namespace nw
