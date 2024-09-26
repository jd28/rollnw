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
    nw::ArenaAllocator<char> allocator(&arena);
    std::basic_string<char, std::char_traits<char>, nw::ArenaAllocator<char>> arena_string(allocator);
    arena_string = "Hello, World";
    EXPECT_EQ(arena_string, "Hello, World");
}
