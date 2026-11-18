#pragma once

#include "../config.hpp"
#include "../log.hpp"
#include "ChunkVector.hpp"
#include "asan.hpp"
#include "templates.hpp"

#include <absl/container/flat_hash_set.h>

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
    virtual void deallocate(void* p) = 0;
};

struct GlobalMemory : MemoryResource {
    virtual void* allocate(size_t bytes, size_t alignment = alignof(std::max_align_t)) override;
    virtual void deallocate(void* p) override;
};

struct MemoryBlock {
    void* base = nullptr;
    size_t used = 0;
    size_t capacity = 0;
};

struct MemoryMarker {
    size_t block_index = 0;
    size_t position = 0;

    auto operator<=>(const MemoryMarker&) const = default;
};

/// A growable Memory Arena
struct MemoryArena : MemoryResource {
    MemoryArena(size_t capacity);
    MemoryArena(const MemoryArena&) = delete;
    MemoryArena(MemoryArena&&) = default;
    virtual ~MemoryArena();

    MemoryArena& operator=(const MemoryArena&) = delete;
    MemoryArena& operator=(MemoryArena&&) = default;

    /// Allocate memory memory on the arena.
    void* allocate(size_t size, size_t alignment = alignof(max_align_t));

    /// Total allocated capacity across all blocks
    size_t capacity() const noexcept;

    /// No-op
    virtual void deallocate(void*) { };

    /// Gets the current point in the allocator
    MemoryMarker current();

    /// Reset the arena, freeing all blocks except the first.
    void reset();

    /// Rewinds the allocator to a specific point in the allocator
    void rewind(MemoryMarker marker);

    /// Total used memory across all blocks
    size_t used() const noexcept;

private:
    std::vector<MemoryBlock> blocks_;
    size_t current_block_ = 0;
    size_t default_block_size_;

    void allocate_block(size_t capacity);
    void deallocate_blocks();
};

namespace detail {

template <typename T>
void destructor(void* ptr)
{
    static_cast<T*>(ptr)->~T();
}

struct Finalizer {
    void (*fn)(void*) = nullptr;
    void* object = nullptr;
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
    virtual void deallocate(void*) override { };

    /// Allocates a non-trivial object and stores pointer to destructor that is run when scope exits.
    template <typename T, typename... Args>
    T* alloc_obj(Args&&... args)
    {
        static_assert(!std::is_trivially_destructible_v<T>, "Use alloc_pod for POD types");
        void* mem = allocate(sizeof(detail::FinalizedObject<T>), alignof(detail::FinalizedObject<T>));
        auto fo = static_cast<detail::FinalizedObject<T>*>(mem);
        fo->f.fn = &detail::destructor<T>;
        fo->f.object = &fo->obj;
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

    size_t block_size_;
    size_t block_count_;
    Vector<void*> blocks_;
    Vector<void*> free_list_;

    void expand(size_t count);
};

} // namespace detail

struct MemoryPool : public MemoryResource {
    MemoryPool(size_t max_size, size_t count, MemoryResource* allocator = nullptr);

    virtual void* allocate(size_t bytes, size_t alignment = alignof(max_align_t)) override;
    virtual void deallocate(void* ptr) override;

    nw::MemoryResource* allocator_ = nullptr;
    ChunkVector<detail::PoolBlock> pools_;
    size_t max_size_;
    size_t count_;
};

// == ObjectPool ========================================================================
// ======================================================================================

template <typename T>
struct ObjectPool {
    ObjectPool(size_t n, nw::MemoryResource* allocator)
        : chunk_size_{n}
        , allocator_{allocator}
        , free_list_(chunk_size_, allocator_)
        , chunks_{32, allocator_}
    {
    }

    ~ObjectPool()
    {
        clear();
    }

    T* allocate()
    {
        if (free_list_.empty()) { allocate_chunk(); }

        auto result = free_list_.back();
        free_list_.pop_back();
        nw::asan::unpoison(result, sizeof(T));
        new (result) T(allocator_);
        ++allocated_;

        return result;
    }

    void clear()
    {
        // Anything not currently in free_list_ is a live object -- a prior
        // free() already ran its destructor and poisoned the slot, but
        // nothing has run ~T() for objects a caller never explicitly freed
        // (e.g. a bulk teardown that assumes the backing memory going away
        // makes individual destruction unnecessary; it doesn't, for any T
        // with a non-trivial destructor). Destruct those before handing the
        // chunk back.
        //
        // T's destructor can itself free() *other* slots in this same pool
        // (e.g. an Item's destructor destroying Items nested in its own
        // inventory) -- free_list_ grows mid-loop as a result. A one-time
        // snapshot would go stale and this would destruct an
        // already-destructed nested object a second time. free_set is kept
        // current instead: drain_new_frees() folds in whatever free() has
        // appended since the last check, immediately before each slot's
        // liveness is tested.
        absl::flat_hash_set<T*> free_set;
        free_set.reserve(free_list_.size());
        size_t drained = 0;
        auto drain_new_frees = [this, &free_set, &drained]() {
            for (; drained < free_list_.size(); ++drained) {
                free_set.insert(free_list_[drained]);
            }
        };
        drain_new_frees();
        for (size_t i = 0; i < chunks_.size(); ++i) {
            T* chunk = chunks_[i];
            for (size_t j = 0; j < chunk_size_; ++j) {
                T* slot = &chunk[j];
                drain_new_frees();
                if (!free_set.contains(slot)) {
                    slot->~T();
                    drain_new_frees();
                }
            }
            // Hand clean memory back to the underlying allocator.
            nw::asan::unpoison(chunk, sizeof(T) * chunk_size_);
            allocator_->deallocate(chunk);
        }
        free_list_.clear();
        chunks_.clear();
        allocated_ = 0;
    }

    void free(T* object)
    {
        object->~T();
        free_list_.push_back(object);
        --allocated_;
        // Destroyed slot: any later access through a stale pointer trips ASan.
        nw::asan::poison(object, sizeof(T));
    }

    void allocate_chunk()
    {
        T* chunk = static_cast<T*>(allocator_->allocate(sizeof(T) * chunk_size_, alignof(T)));
        CHECK_F(!!chunk, "Unable to allocate chunk of size {}", sizeof(T) * chunk_size_);
        // Slots start dead; allocate() unpoisons each as it hands it out. The
        // free list lives in a separate ChunkVector, so this is safe.
        nw::asan::poison(chunk, sizeof(T) * chunk_size_);
        for (size_t i = 0; i < chunk_size_; ++i) {
            free_list_.push_back(&chunk[i]);
        }
        chunks_.push_back(chunk);
    }

    [[nodiscard]] size_t size() const noexcept { return allocated_; }

    size_t allocated_ = 0;
    size_t chunk_size_;
    nw::MemoryResource* allocator_;
    ChunkVector<T*> free_list_;
    ChunkVector<T*> chunks_;
};

} // namespace nw
