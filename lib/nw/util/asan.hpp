#pragma once

// Central sanitizer poisoning helpers for rollNW's custom allocators.
//
// Our arenas and pools (MemoryArena, MemoryPool, ObjectPool, smalls::ScriptHeap)
// carve objects out of memory they own via malloc/mmap. AddressSanitizer only
// tracks the lifetime of malloc/free/stack/global allocations, so it is blind
// to use-after-free and use-after-scope *inside* those arenas: a dangling
// pointer into a freed pool slot or a rewound arena reads live-but-wrong memory
// with no report. These helpers let each allocator hand its own free/live
// transitions to ASan. They compile to nothing when ASan is not enabled.

#include <cstddef>

#if defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define ROLLNW_ASAN_ENABLED 1
#  endif
#endif
#if !defined(ROLLNW_ASAN_ENABLED) && defined(__SANITIZE_ADDRESS__)
#  define ROLLNW_ASAN_ENABLED 1
#endif

#if defined(ROLLNW_ASAN_ENABLED)
#  include <sanitizer/asan_interface.h>
#endif

namespace nw::asan {

/// Marks ``[addr, addr + size)`` as unusable; any access is a fatal ASan error.
/// Call when an allocator hands memory back to its free list / rewinds past it.
inline void poison(const volatile void* addr, size_t size) noexcept
{
#if defined(ROLLNW_ASAN_ENABLED)
    if (size) { __asan_poison_memory_region(addr, size); }
#else
    (void)addr;
    (void)size;
#endif
}

/// Marks ``[addr, addr + size)`` as usable again. Call before writing into a
/// slot the allocator is about to hand out (it may have been poisoned by a
/// prior free/reset).
inline void unpoison(const volatile void* addr, size_t size) noexcept
{
#if defined(ROLLNW_ASAN_ENABLED)
    if (size) { __asan_unpoison_memory_region(addr, size); }
#else
    (void)addr;
    (void)size;
#endif
}

/// True when ASan instrumentation is compiled in.
constexpr bool enabled() noexcept
{
#if defined(ROLLNW_ASAN_ENABLED)
    return true;
#else
    return false;
#endif
}

} // namespace nw::asan
