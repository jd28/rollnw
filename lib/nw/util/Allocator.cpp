#include "Allocator.hpp"
#include "memory.hpp"

namespace nw {
namespace detail {

MemoryResourceInternal::MemoryResourceInternal(MemoryResource* resource)
    : resource_{resource}
{
}

void* MemoryResourceInternal::allocate(size_t bytes, size_t alignment)
{
    return resource_->allocate(bytes, alignment);
}

void MemoryResourceInternal::deallocate(void* p, size_t bytes, size_t alignment)
{
    resource_->deallocate(p, bytes, alignment);
}

} // namespace detail
} // namespace nw
