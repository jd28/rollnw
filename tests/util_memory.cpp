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
