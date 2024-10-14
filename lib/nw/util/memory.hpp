#pragma once

#include "../config.hpp"
#include "../log.hpp"
#include "ChunkVector.hpp"
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

/// Abstracts some memory resource, i.e. global malloc/free
struct MemoryResource {
    virtual ~MemoryResource() = default;

    virtual void* allocate(size_t bytes, size_t alignment = alignof(std::max_align_t)) = 0;
    virtual void deallocate(void* p, size_t bytes, size_t alignment = alignof(std::max_align_t)) = 0;
};

struct GlobalMemory : MemoryResource {
    virtual void* allocate(size_t bytes, size_t alignment = alignof(std::max_align_t)) override;
    virtual void deallocate(void* p, size_t bytes, size_t alignment = alignof(std::max_align_t)) override;
};

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
struct MemoryArena : MemoryResource {
    MemoryArena(size_t blockSize = 1024);
    MemoryArena(const MemoryArena&) = delete;
    MemoryArena(MemoryArena&&) = default;
    virtual ~MemoryArena();

    MemoryArena& operator=(const MemoryArena&) = delete;
    MemoryArena& operator=(MemoryArena&&) = default;

    /// Allocate memory memory on the arena.
    void* allocate(size_t size, size_t alignment = alignof(max_align_t));

    /// No-op
    virtual void deallocate(void*, size_t, size_t) {};

    /// Gets the current point in the allocator
    MemoryMarker current();

    /// Reset the arena, freeing all blocks except the first.
    void reset();

    /// Rewinds the allocator to a specific point in the allocator
    void rewind(MemoryMarker marker);

private:
    Vector<MemoryBlock> blocks_;
    size_t current_block_ = 0;
    size_t size_ = 0;

    void alloc_block_(size_t size);
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

struct MemoryScope : public MemoryResource {
    MemoryScope(MemoryArena* arena);
    MemoryScope(const MemoryScope&) = delete;
    MemoryScope(MemoryScope&& other);
    ~MemoryScope();

    MemoryScope& operator=(const MemoryScope&) = delete;
    MemoryScope& operator=(MemoryScope&& other);

    /// Allocates ``size`` bytes with ``alignment``
    virtual void* allocate(size_t bytes, size_t alignment = alignof(max_align_t)) override;

    /// No-op
    virtual void deallocate(void*, size_t, size_t) override {};

    /// Allocates a non-trivial object and stores pointer to destructor that is run when scope exits.
    template <typename T, typename... Args>
    T* alloc_obj(Args&&... args)
    {
        static_assert(!std::is_trivially_destructible_v<T>, "Use alloc_pod for POD types");
        void* mem = allocate(sizeof(detail::FinalizedObject<T>), alignof(detail::FinalizedObject<T>));
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
        static_assert(std::is_trivially_destructible_v<T>, "Use alloc_obj for non-trivial types");
        void* mem = allocate(sizeof(T), alignof(T));
        return new (mem) T();
    }

    /// Resets a memory scope to its original state.
    void reset();

    // private:
    MemoryArena* arena_ = nullptr;
    MemoryMarker marker_;
    detail::Finalizer* finalizers_ = nullptr;
};

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
    Vector<void*> blocks_;
    Vector<void*> free_list_;

    void expand(size_t count);
};

} // namespace detail

struct MemoryPool : public MemoryResource {
    MemoryPool(size_t max_size, size_t count);

    virtual void* allocate(size_t bytes, size_t alignment = alignof(max_align_t)) override;
    virtual void deallocate(void* ptr, size_t bytes = 0, size_t alignment = alignof(std::max_align_t)) override;

    // private:
    Vector<detail::PoolBlock> pools_;
    size_t max_size_;
    size_t count_;
};

// == ObjectPool ========================================================================
// ======================================================================================

template <typename T, size_t N, class Alloc = Allocator<T>>
struct ObjectPool {
    ObjectPool(Alloc allocator)
        : allocator_{allocator}
        , free_list_(N, allocator_)
        , chunks_{32, allocator_}
    {
    }

    ~ObjectPool()
    {
        for (size_t i = 0; i < chunks_.size(); ++i) {
            allocator_.deallocate(chunks_[i], N);
        }
    }

    T* allocate()
    {
        if (free_list_.empty()) { allocate_chunk(); }

        auto result = free_list_.back();
        free_list_.pop_back();
        result = new (result) T;
        return result;
    }

    void clear()
    {
        free_list_.clear();
        chunks_.clear();
    }

    void free(T* object)
    {
        object->~T();
        free_list_.push_back(object);
    }

    void allocate_chunk()
    {
        T* chunk = static_cast<T*>(allocator_.allocate(N));
        CHECK_F(!!chunk, "Unable to allocate chunk of size {}", sizeof(T) * N);
        for (size_t i = 0; i < N; ++i) {
            free_list_.push_back(&chunk[i]);
        }
    }

    Alloc allocator_;
    ChunkVector<T*> free_list_;
    ChunkVector<T*> chunks_;
};

} // namespace nw
