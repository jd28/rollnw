#include <gtest/gtest.h>

#include "nw/util/memory.hpp"
#include <nw/util/ChunkVector.hpp>

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
