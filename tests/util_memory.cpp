#include "nw/config.hpp"
#include <gtest/gtest.h>

#include <nw/util/memory.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace std::literals;

TEST(Memory, Helpers)
{
    EXPECT_EQ(nw::KB(1), 1024ULL);
    EXPECT_EQ(nw::MB(1), 1024ULL * 1024ULL);
    EXPECT_EQ(nw::GB(1), 1024ULL * 1024ULL * 1024ULL);
}

struct TestStruct {
    TestStruct(bool* am_i_destrcuted)
        : am_i_destrcuted_{am_i_destrcuted}
    {
    }
    ~TestStruct()
    {
        *am_i_destrcuted_ = true;
    }

    bool* am_i_destrcuted_;
};

struct TestPOD {
    bool value;
    int test;
};

TEST(Memory, Scope)
{
    nw::MemoryArena arena{4096};
    auto current = arena.current();

    {
        nw::MemoryScope scope(&arena);
        scope.allocate(60);
        EXPECT_NE(current, arena.current());
    }
    EXPECT_EQ(current, arena.current());

    bool result = false;
    {
        nw::MemoryScope scope(&arena);
        auto value = scope.alloc_obj<TestStruct>(&result);
        EXPECT_TRUE(!!scope.finalizers_);
        EXPECT_EQ(scope.finalizers_->object, value);
        EXPECT_EQ(&result, value->am_i_destrcuted_);
        EXPECT_NE(current, arena.current());
    }
    EXPECT_TRUE(result);
    EXPECT_EQ(current, arena.current());

    {
        nw::MemoryScope scope(&arena);
        scope.alloc_pod<TestPOD>();
        EXPECT_NE(current, arena.current());
    }
    EXPECT_EQ(current, arena.current());

    result = false;
    {
        nw::MemoryScope scope(&arena);
        auto vec = scope.alloc_obj<std::vector<TestStruct, nw::Allocator<TestStruct>>>(&scope);
        vec->push_back(TestStruct(&result));
        EXPECT_NE(current, arena.current());
    }
    EXPECT_TRUE(result);
    EXPECT_EQ(current, arena.current());
}

struct alignas(64) OverAlignedScopeObject {
    explicit OverAlignedScopeObject(uintptr_t* destroyed_at_)
        : destroyed_at{destroyed_at_}
    {
    }

    ~OverAlignedScopeObject()
    {
        *destroyed_at = reinterpret_cast<uintptr_t>(this);
    }

    uintptr_t* destroyed_at = nullptr;
};

TEST(Memory, ScopeDestroysOverAlignedObjectAtCorrectAddress)
{
    nw::MemoryArena arena{4096};
    uintptr_t destroyed_at = 0;

    uintptr_t object_address = 0;
    {
        nw::MemoryScope scope(&arena);
        auto* value = scope.alloc_obj<OverAlignedScopeObject>(&destroyed_at);
        object_address = reinterpret_cast<uintptr_t>(value);
        EXPECT_EQ(object_address % alignof(OverAlignedScopeObject), 0u);
        EXPECT_EQ(scope.finalizers_->object, value);
    }

    EXPECT_EQ(destroyed_at, object_address);
}

TEST(Memory, GlobalMemoryAlignsHeaderAndPayload)
{
    nw::GlobalMemory memory;
    for (size_t alignment : std::vector<size_t>{1, 8, 16, 64}) {
        constexpr size_t size = 96;
        void* ptr = memory.allocate(size, alignment);
        ASSERT_NE(ptr, nullptr);
        EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % alignment, 0u);
        std::memset(ptr, 0xAB, size);
        memory.deallocate(ptr);
    }
}

TEST(Memory, Pool)
{
    nw::MemoryPool mp(1024, 8);
    size_t free = mp.pools_[3].free_list_.size();
    {
        EXPECT_EQ(32 + 16, mp.pools_[3].block_size());
        void* ptr = mp.allocate(16, alignof(std::max_align_t));
        ASSERT_NE(ptr, nullptr);
        EXPECT_EQ(free - 1, mp.pools_[3].free_list_.size());
        mp.deallocate(ptr);
    }
    EXPECT_EQ(free, mp.pools_[3].free_list_.size());
}

TEST(Memory, PoolFallbackAllocatesHeaderAndPayload)
{
    nw::MemoryPool mp(128, 1);
    constexpr size_t size = 256;
    constexpr size_t alignment = 64;

    void* ptr = mp.allocate(size, alignment);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % alignment, 0u);

    std::memset(ptr, 0xAB, size);
    mp.deallocate(ptr);
}

TEST(Memory, PoolExercisesBucketsAndFallback)
{
    nw::MemoryPool mp(512, 2);

    struct Allocation {
        void* ptr = nullptr;
        size_t size = 0;
    };

    const std::vector<size_t> sizes = {1, 8, 17, 64, 96, 129, 180, 240, 300, 480, 768};
    std::vector<Allocation> allocations;
    allocations.reserve(sizes.size());

    for (size_t size : sizes) {
        const size_t alignment = size % 3 == 0 ? 32 : 8;
        void* ptr = mp.allocate(size, alignment);
        ASSERT_NE(ptr, nullptr);
        EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % alignment, 0u);
        std::memset(ptr, 0xCD, size);
        allocations.push_back({ptr, size});
    }

    for (auto it = allocations.rbegin(); it != allocations.rend(); ++it) {
        mp.deallocate(it->ptr);
    }
}

TEST(Memory, ArenaResetFreesExtraBlocks)
{
    nw::MemoryArena arena{64};
    const size_t initial_capacity = arena.capacity();

    arena.allocate(32);
    arena.allocate(128);
    EXPECT_GT(arena.capacity(), initial_capacity);
    EXPECT_GT(arena.used(), 0u);

    arena.reset();
    EXPECT_EQ(arena.used(), 0u);
    EXPECT_EQ(arena.capacity(), initial_capacity);

    void* ptr = arena.allocate(16);
    EXPECT_NE(ptr, nullptr);
}

TEST(Memory, ArenaRewindReusesLivePrefix)
{
    nw::MemoryArena arena{128};
    auto start = arena.current();

    auto* prefix = static_cast<int*>(arena.allocate(sizeof(int), alignof(int)));
    ASSERT_NE(prefix, nullptr);
    *prefix = 42;

    auto marker = arena.current();
    void* scratch = arena.allocate(64, 16);
    ASSERT_NE(scratch, nullptr);
    std::memset(scratch, 0xA5, 64);

    arena.rewind(marker);
    EXPECT_EQ(*prefix, 42);

    void* replacement = arena.allocate(64, 16);
    ASSERT_NE(replacement, nullptr);
    std::memset(replacement, 0x5A, 64);

    arena.rewind(start);
    EXPECT_EQ(arena.used(), 0u);
}
