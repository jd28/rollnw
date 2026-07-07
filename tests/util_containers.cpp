#include <gtest/gtest.h>

#include "nw/util/memory.hpp"
#include <nw/util/ByteArray.hpp>
#include <nw/util/ChunkVector.hpp>

#include <limits>

using namespace std::literals;

TEST(ChunkVector, Basics)
{
    nw::MemoryArena ma{4096};
    nw::ChunkVector<std::string> cv(3, &ma);

    cv.push_back("1");
    cv.push_back("2");
    cv.push_back("3");
    cv.push_back("4");
    cv.push_back("5");
    EXPECT_EQ(cv[3], "4");

    cv.emplace_back("6");

    EXPECT_EQ(cv.back(), "6");

    cv.clear();
    EXPECT_TRUE(cv.empty());
    EXPECT_EQ(cv.capacity(), 6);

    cv.resize(10);
    EXPECT_EQ(cv[9], "");
    cv.resize(0);
    EXPECT_EQ(cv.size(), 0);
}

TEST(ChunkVector, ReserveAllocatesCapacity)
{
    nw::MemoryArena ma{4096};
    nw::ChunkVector<std::string> cv(3, &ma);

    cv.reserve(7);
    EXPECT_EQ(cv.size(), 0u);
    EXPECT_GE(cv.capacity(), 7u);
}

TEST(ByteArray, ReadAtAllowsEndBoundary)
{
    nw::ByteArray bytes;
    const uint8_t input[] = {1, 2, 3};
    bytes.append(input, sizeof(input));

    uint8_t value = 0;
    EXPECT_TRUE(bytes.read_at(2, &value, 1));
    EXPECT_EQ(value, 3);
    EXPECT_TRUE(bytes.read_at(3, &value, 0));
    EXPECT_FALSE(bytes.read_at(3, &value, 1));
    EXPECT_FALSE(bytes.read_at(std::numeric_limits<size_t>::max(), &value, 1));
}
