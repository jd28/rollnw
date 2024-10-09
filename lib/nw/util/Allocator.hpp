#pragma once

#include <cstddef>

namespace nw {

struct MemoryResource;

namespace detail {
// Hide MemoryResource impl details
struct MemoryResourceInternal {
    MemoryResourceInternal(MemoryResource* resoure);
    void* allocate(size_t nbytes, size_t alignment = alignof(std::max_align_t));
    void deallocate(void* p, size_t bytes, size_t alignment = alignof(std::max_align_t));
    MemoryResource* resource_;
};

}

/// Allocator adapter for MemoryResource, so this is a polymorphic allocator.
template <typename T>
class Allocator {
public:
    using value_type = T;

    Allocator(MemoryResource* resource)
        : resource_(resource)
    {
    }

    template <typename U>
    Allocator(const Allocator<U>& other) noexcept
        : resource_(other.resource_)
    {
    }

    T* allocate(size_t n)
    {
        if (n == 0) { return nullptr; }
        return static_cast<T*>(resource_.allocate(n * sizeof(T), alignof(T)));
    }

    void deallocate(T* ptr, size_t size) noexcept
    {
        resource_.deallocate(ptr, size);
    }

    template <typename U>
    struct rebind {
        using other = Allocator<U>;
    };

    // private:
    detail::MemoryResourceInternal resource_;
};

template <typename T, typename U>
bool operator==(const Allocator<T>& a, const Allocator<U>& b) noexcept
{
    return &a.resource_.resource_ == &b.resource_.resource_;
}

template <typename T, typename U>
bool operator!=(const Allocator<T>& a, const Allocator<U>& b) noexcept
{
    return !(a == b);
}
} // namespace nw
