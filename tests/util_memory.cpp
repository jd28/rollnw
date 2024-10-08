#include <gtest/gtest.h>

#include <nw/util/memory.hpp>

using namespace std::literals;

TEST(Memory, Helpers)
{
    EXPECT_EQ(nw::KB(1), 1024ULL);
    EXPECT_EQ(nw::MB(1), 1024ULL * 1024ULL);
    EXPECT_EQ(nw::GB(1), 1024ULL * 1024ULL * 1024ULL);
}

TEST(Memory, Allocator)
{
    nw::MemoryArena arena;
    std::basic_string<char, std::char_traits<char>, std::pmr::polymorphic_allocator<char>> arena_string(&arena);
    arena_string = "Hello, World";
    EXPECT_EQ(arena_string, "Hello, World");
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
    nw::MemoryArena arena;
    auto current = arena.current();

    {
        nw::MemoryScope scope(&arena);
        scope.alloc(60);
        EXPECT_NE(current, arena.current());
    }
    EXPECT_EQ(current, arena.current());

    bool result = false;
    {
        nw::MemoryScope scope(&arena);
        auto value = scope.alloc_obj<TestStruct>(&result);
        EXPECT_TRUE(!!scope.finalizers_);
        EXPECT_EQ((uint8_t*)scope.finalizers_ + sizeof(nw::detail::Finalizer), (uint8_t*)value);
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
}
