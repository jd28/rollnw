#include "Memory.hpp"

namespace nw::kernel {

GlobalMemory* global_allocator()
{
    static GlobalMemory malloc_;
    return &malloc_;
}

MemoryScope* tls_scratch()
{
    static thread_local MemoryArena tls_arena_(MB(1));
    static thread_local MemoryScope tls_scope_(&tls_arena_);
    return &tls_scope_;
}
}
