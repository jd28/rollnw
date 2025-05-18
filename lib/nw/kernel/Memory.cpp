#include "Memory.hpp"

namespace nw::kernel {

GlobalMemory* global_allocator()
{
    static GlobalMemory malloc_;
    return &malloc_;
}

namespace {
thread_local MemoryArena tls_arena_(MB(1));
thread_local MemoryScope tls_scope_(&tls_arena_);
}

MemoryScope* tls_scratch()
{
    return &tls_scope_;
}
}
