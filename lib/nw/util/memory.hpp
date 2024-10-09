#pragma once

#include "../log.hpp"
#include "templates.hpp"

#include <assert.h>
#include <memory>
#include <stack>
#include <stdint.h>
#include <stdlib.h>
#include <vector>

namespace nw {

/// Convert kilobytes to bytes
constexpr std::uint64_t KB(std::uint64_t kb)
{
    return kb * 1024ULL;
}

/// Convert megabytes to bytes
constexpr std::uint64_t MB(std::uint64_t mb)
{
    return mb * 1024ULL * 1024ULL;
}

/// Convert gigabytes to bytes
constexpr std::uint64_t GB(std::uint64_t gb)
{
    return gb * 1024ULL * 1024ULL * 1024ULL;
}

struct MemoryBlock {
    uint8_t* block = nullptr;
    size_t position = 0;
    size_t size = 0;

    auto operator<=>(const MemoryBlock&) const = default;
};

struct MemoryMarker {
    size_t chunk_index = 0;
    size_t position = 0;

    auto operator<=>(const MemoryMarker&) const = default;
};

/// A growable Memory Arena
struct MemoryArena {
    MemoryArena(size_t blockSize = 1024);
    MemoryArena(const MemoryArena&) = delete;
    MemoryArena(MemoryArena&&) = default;
    virtual ~MemoryArena();

    MemoryArena& operator=(const MemoryArena&) = delete;
    MemoryArena& operator=(MemoryArena&&) = default;

    /// Allocate memory memory on the arena.
    void* allocate(size_t size, size_t alignment = alignof(max_align_t));

    /// Gets the current point in the allocator
    MemoryMarker current();

    /// Reset the arena, freeing all blocks except the first.
    void reset();

    /// Rewinds the allocator to a specific point in the allocator
    void rewind(MemoryMarker marker);

private:
    std::vector<MemoryBlock> blocks_;
    size_t current_block_ = 0;
    size_t size_ = 0;

    void alloc_block_(size_t size);
};

// This is very simple and naive.
template <typename T, size_t chunk_size>
struct ObjectPool {
    ObjectPool(MemoryArena* arena)
        : arena_{arena}
    {
    }

    T* allocate()
    {
        if (!arena_) { return nullptr; }
        if (free_list_.empty()) { allocate_chunk(); }

        auto result = free_list_.top();
        free_list_.pop();
        result = new (result) T;
        return result;
    }

    void clear()
    {
        free_list_ = std::stack<T*, std::vector<T*>>{};
        chunks_.clear();
    }

    void free(T* object)
    {
        if (!arena_) { return; }

        object->~T();
        free_list_.push(object);
    }

    void allocate_chunk()
    {
        if (arena_ == nullptr) { return; }

        T* chunk = static_cast<T*>(arena_->allocate(sizeof(T) * chunk_size, alignof(T)));
        CHECK_F(!!chunk, "Unable to allocate chunk of size {}", sizeof(T) * chunk_size);
        for (size_t i = 0; i < chunk_size; ++i) {
            free_list_.push(&chunk[i]);
        }
    }

    MemoryArena* arena_{chunk_size * sizeof(T)};
    std::stack<T*, std::vector<T*>> free_list_;
    std::vector<T*> chunks_;
};

namespace detail {

template <typename T>
void destructor(void* ptr)
{
    static_cast<T*>(ptr)->~T();
}

struct Finalizer {
    void (*fn)(void*) = nullptr;
    Finalizer* next = nullptr;
};

template <typename T>
struct FinalizedObject {
    Finalizer f;
    T obj;
};

}

// == MemoryScope =======================================================================
// ======================================================================================

struct MemoryScope {
    MemoryScope(MemoryArena* arena);
    MemoryScope(const MemoryScope&) = delete;
    MemoryScope(MemoryScope&& other);
    ~MemoryScope();

    MemoryScope& operator=(const MemoryScope&) = delete;
    MemoryScope& operator=(MemoryScope&& other);

    /// Allocates ``size`` bytes with ``alignment``
    void* alloc(size_t size, size_t alignment = alignof(max_align_t));

    /// Allocates a non-trivial object and stores pointer to destructor that is run when scope exits.
    template <typename T, typename... Args>
    T* alloc_obj(Args&&... args)
    {
        static_assert(!(std::is_standard_layout_v<T> && std::is_trivial_v<T>), "Use alloc_pod for POD types");
        void* mem = alloc(sizeof(detail::FinalizedObject<T>), alignof(detail::FinalizedObject<T>));
        auto fo = static_cast<detail::FinalizedObject<T>*>(mem);
        fo->f.fn = &detail::destructor<T>;
        fo->f.next = finalizers_; // last in, first destructed.
        finalizers_ = &fo->f;
        return new (&fo->obj) T(std::forward<Args>(args)...);
    }

    /// Allocates a trivial object no destructor is run on scope exit.
    template <typename T>
    T* alloc_pod()
    {
        static_assert(std::is_standard_layout_v<T> && std::is_trivial_v<T>, "Use alloc_obj for non-trivial types");
        void* mem = alloc(sizeof(T), alignof(T));
        return new (mem) T();
    }

    // private:
    MemoryArena* arena_ = nullptr;
    MemoryMarker marker_;
    detail::Finalizer* finalizers_ = nullptr;
};

template <typename T>
class ScopeAllocator {
public:
    using value_type = T;

    // Constructor accepting MemoryScope reference
    ScopeAllocator(MemoryScope* scope)
        : scope_(scope)
    {
    }

    // Copy constructor (needed by allocator interface)
    template <typename U>
    ScopeAllocator(const ScopeAllocator<U>& other) noexcept
        : scope_(other.scope_)
    {
    }

    T* allocate(std::size_t n)
    {
        if (n == 0)
            return nullptr;
        return static_cast<T*>(scope_->alloc(n * sizeof(T), alignof(T)));
    }

    void deallocate(T*, std::size_t) noexcept
    {
    }

    template <typename U, typename... Args>
    void construct(U* p, Args&&... args)
    {
        ::new (static_cast<void*>(p)) U(std::forward<Args>(args)...);
    }

    template <typename U>
    void destroy(U* p) noexcept
    {
        p->~U();
    }

    template <typename U>
    struct rebind {
        using other = ScopeAllocator<U>;
    };

    // private:
    MemoryScope* scope_;
};

template <typename T, typename U>
bool operator==(const ScopeAllocator<T>& a, const ScopeAllocator<U>& b) noexcept
{
    return &a.scope_ == &b.scope_;
}

template <typename T, typename U>
bool operator!=(const ScopeAllocator<T>& a, const ScopeAllocator<U>& b) noexcept
{
    return !(a == b);
}

// == MemoryPool ========================================================================
// ======================================================================================

namespace detail {

struct PoolBlock {
public:
    PoolBlock(size_t block_size, size_t block_count);
    PoolBlock(const PoolBlock&) = delete;
    PoolBlock(PoolBlock&&) = default;
    ~PoolBlock();

    PoolBlock& operator=(const PoolBlock&) = delete;
    PoolBlock& operator=(PoolBlock&&) = default;

    void* allocate();
    size_t block_size() const noexcept;
    void deallocate(void* ptr);

    // private:
    size_t block_size_;
    size_t block_count_;
    std::vector<void*> blocks_;
    std::vector<void*> free_list_;

    void expand(size_t count);
};

} // namespace detail

struct MemoryPool {
    MemoryPool(size_t max_size, size_t count);
    void* allocate(size_t size);
    void deallocate(void* ptr, size_t size);

    // private:
    std::vector<detail::PoolBlock> pools_;
    size_t max_size_;
    size_t count_;
};

// Standard allocator wrapper for MemoryPool
template <typename T>
class PoolAllocator {
public:
    using value_type = T;

    PoolAllocator(MemoryPool* pool)
        : pool_(pool)
    {
    }

    /// Allocates memory for n objects of type T
    T* allocate(size_t n)
    {
        size_t bytes = n * sizeof(T);
        return static_cast<T*>(pool_->allocate(bytes));
    }

    /// Deallocate memory for n objects of type T
    void deallocate(T* p, size_t n)
    {
        size_t bytes = n * sizeof(T);
        pool_->deallocate(static_cast<void*>(p), bytes);
    }

    template <typename U>
    struct rebind {
        using other = PoolAllocator<U>;
    };

    // Comparison operators for allocators
    bool operator==(const PoolAllocator& other) const noexcept
    {
        return &pool_ == &other.pool_;
    }

    bool operator!=(const PoolAllocator& other) const noexcept
    {
        return !(*this == other);
    }

private:
    MemoryPool* pool_;
};

} // namespace nw
