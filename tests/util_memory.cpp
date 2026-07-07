#include "nw/config.hpp"
#include <gtest/gtest.h>

#include <nw/util/memory.hpp>

#include <cstdint>
#include <cstring>

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
        EXPECT_EQ(reinterpret_cast<uint8_t*>(scope.finalizers_) + sizeof(nw::detail::Finalizer),
            reinterpret_cast<uint8_t*>(value));
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

TEST(Memory, Pool)
{
    nw::MemoryPool mp(1024, 8);
    size_t free = mp.pools_[3].free_list_.size();
    {
        EXPECT_EQ(32 + 16, mp.pools_[3].block_size());
        nw::PString s("Hello World, this is a test.", &mp);
        EXPECT_EQ(free - 1, mp.pools_[3].free_list_.size());
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
