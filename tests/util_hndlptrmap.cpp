#include <nw/util/HandlePool.hpp>
#include <nw/util/HndlPtrMap.hpp>

#include <gtest/gtest.h>

using namespace nw;

TEST(HndlPtrMapTest, BasicInsertAndRetrieve)
{
    nw::HndlPtrMap<int> map;

    int value1 = 10;
    int value2 = 20;

    map.insert(1, &value1);
    map.insert(2, &value2);

    auto val1 = map.get(1);
    ASSERT_NE(val1, nullptr);
    EXPECT_EQ(*val1, 10);

    auto val2 = map.get(2);
    ASSERT_NE(val2, nullptr);
    EXPECT_EQ(*val2, 20);
}

TEST(HndlPtrMapTest, OverwriteKey)
{
    nw::HndlPtrMap<int> map;

    int value1 = 10;
    int value2 = 20;

    map.insert(1, &value1);
    auto val = map.get(1);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, 10);

    map.insert(1, &value2);
    val = map.get(1);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, 20);
}

TEST(HndlPtrMapTest, RemoveKey)
{
    nw::HndlPtrMap<int> map;

    int value1 = 10;
    map.insert(1, &value1);

    auto val = map.get(1);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, 10);

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
        auto val1 = map.get(i);
        ASSERT_NE(val1, nullptr);
        EXPECT_EQ(*val1, i * 10);
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
        auto val = map.get(i);
        ASSERT_NE(val, nullptr);
        EXPECT_EQ(*val, static_cast<int>(i));
    }
}

// Local test object for TypedHandlePool
struct TestObject {
    uint32_t a = 0;
    uint32_t b = 0;
};

TEST(TypedHandlePool, BasicAllocation)
{
    nw::TypedHandlePool pool(sizeof(TestObject));

    // Allocate an object
    TypedHandle h = pool.allocate();
    h.type = 1; // Caller sets type
    EXPECT_EQ(h.type, 1);
    EXPECT_EQ(h.id, 0);
    EXPECT_EQ(h.generation, 1); // Generation starts at 1

    // Get object
    auto* obj = static_cast<TestObject*>(pool.get(h));
    ASSERT_NE(obj, nullptr);

    // Verify zero-initialized (done by pool)
    EXPECT_EQ(obj->a, 0);
    EXPECT_EQ(obj->b, 0);

    // Set data
    obj->a = 123;
    obj->b = 456;

    // Retrieve again
    auto* obj2 = static_cast<TestObject*>(pool.get(h));
    EXPECT_EQ(obj2->a, 123);
    EXPECT_EQ(obj2->b, 456);
}

TEST(TypedHandlePool, GenerationIncrement)
{
    nw::TypedHandlePool pool(sizeof(TestObject));
    TypedHandle h1 = pool.allocate();
    EXPECT_EQ(h1.generation, 1);

    // Destroy and reallocate
    pool.destroy(h1);
    EXPECT_FALSE(pool.valid(h1));

    TypedHandle h2 = pool.allocate();
    EXPECT_EQ(h2.id, h1.id);     // Same slot
    EXPECT_EQ(h2.generation, 2); // Incremented generation

    // Old handle is invalid
    EXPECT_FALSE(pool.valid(h1));
    EXPECT_TRUE(pool.valid(h2));
}

TEST(TypedHandlePool, MultipleAllocations)
{
    nw::TypedHandlePool pool(sizeof(TestObject));

    constexpr size_t COUNT = 100;
    Vector<TypedHandle> handles;

    // Allocate many objects
    for (size_t i = 0; i < COUNT; ++i) {
        TypedHandle h = pool.allocate();
        EXPECT_TRUE(h.is_valid());
        handles.push_back(h);

        auto* obj = static_cast<TestObject*>(pool.get(h));
        ASSERT_NE(obj, nullptr);
        obj->a = static_cast<uint32_t>(i);
    }

    // Verify all are accessible
    for (size_t i = 0; i < COUNT; ++i) {
        auto* obj = static_cast<TestObject*>(pool.get(handles[i]));
        ASSERT_NE(obj, nullptr);
        EXPECT_EQ(obj->a, static_cast<uint32_t>(i));
    }

    EXPECT_EQ(pool.size(), COUNT);
}