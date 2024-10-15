#pragma once

#include "../util/memory.hpp"

namespace nw::kernel {

/// Gets global allocator
[[nodiscard]] GlobalMemory* global_allocator();

/// Thread local scratch buffer.. reset every X ticks? Or never for short running scripts?
[[nodiscard]] MemoryScope* tls_scratch();

} // namespace nw::kernel
