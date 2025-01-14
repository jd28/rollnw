#include "nw/util/HndlPtrMap.hpp"

#include <gtest/gtest.h>

#if defined(ROLLNW_OS_WINDOWS)
#pragma warning(push)
#pragma warning(disable : 6011)
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#endif

TEST(HndlPtrMapTest, BasicInsertAndRetrieve)
{
    nw::HndlPtrMap<int> map;

    int value1 = 10;
    int value2 = 20;

    map.insert(1, &value1);
    map.insert(2, &value2);

    EXPECT_EQ(*map.get(1), 10);
    EXPECT_EQ(*map.get(2), 20);
}

TEST(HndlPtrMapTest, OverwriteKey)
{
    nw::HndlPtrMap<int> map;

    int value1 = 10;
    int value2 = 20;

    map.insert(1, &value1);
    EXPECT_EQ(*map.get(1), 10);

    map.insert(1, &value2);
    EXPECT_EQ(*map.get(1), 20);
}

TEST(HndlPtrMapTest, RemoveKey)
{
    nw::HndlPtrMap<int> map;

    int value1 = 10;
    map.insert(1, &value1);

    EXPECT_EQ(*map.get(1), 10);

    EXPECT_TRUE(map.remove(1));
    EXPECT_EQ(map.get(1), nullptr);

    EXPECT_FALSE(map.remove(1));
}

TEST(HndlPtrMapTest, ResizeMap)
{
    nw::HndlPtrMap<int> map(2);

    int values[10];
    for (int i = 0; i < 10; ++i) {
        values[i] = i * 10;
        map.insert(i, &values[i]);
    }

    EXPECT_GE(map.capacity(), 10);
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(*map.get(i), i * 10);
    }
}

TEST(HndlPtrMapTest, EmptyMap)
{
    nw::HndlPtrMap<int> map;

    EXPECT_EQ(map.size(), 0);
    EXPECT_EQ(map.get(1), nullptr);
    EXPECT_FALSE(map.remove(1));
}

TEST(HndlPtrMapTest, LargeInsertions)
{
    const size_t num_elements = 10000;
    nw::HndlPtrMap<int> map(16, nw::kernel::tls_scratch());

    std::vector<int> values(num_elements);
    for (size_t i = 0; i < num_elements; ++i) {
        values[i] = static_cast<int>(i);
        map.insert(i, &values[i]);
    }

    EXPECT_EQ(map.size(), num_elements);
    for (size_t i = 0; i < num_elements; ++i) {
        EXPECT_EQ(*map.get(i), static_cast<int>(i));
    }
}

#if defined(ROLLNW_OS_WINDOWS)
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#endif
