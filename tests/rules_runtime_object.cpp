#include <gtest/gtest.h>

#include <nw/rules/RuntimeObject.hpp>
#include <nw/rules/effects.hpp>

using namespace nw;

TEST(RuntimeHandlePool, TypeDispatch)
{
    RuntimeObjectPool pool;

    TypedHandle eff = pool.allocate_effect();
    EXPECT_EQ(eff.type, RuntimeObjectPool::TYPE_EFFECT);
    EXPECT_TRUE(pool.valid(eff));

    TypedHandle evt = pool.allocate_event();
    EXPECT_EQ(evt.type, RuntimeObjectPool::TYPE_EVENT);
    EXPECT_TRUE(pool.valid(evt));

    auto* eff_obj = static_cast<Effect*>(pool.get(eff));
    ASSERT_NE(eff_obj, nullptr);

    auto* evt_obj = static_cast<VariantObject*>(pool.get(evt));
    ASSERT_NE(evt_obj, nullptr);
    EXPECT_EQ(evt_obj->num_ints, 6);
}

TEST(RuntimeHandlePool, InvalidHandle)
{
    RuntimeObjectPool pool;

    TypedHandle invalid;
    EXPECT_FALSE(pool.valid(invalid));
    EXPECT_EQ(pool.get(invalid), nullptr);
}

TEST(RuntimeHandle, Serialization)
{
    TypedHandle h;
    h.id = 12345;
    h.type = RuntimeObjectPool::TYPE_EFFECT;
    h.generation = 67890;

    uint64_t val = h.to_ull();
    TypedHandle h2 = TypedHandle::from_ull(val);

    EXPECT_EQ(h2.id, h.id);
    EXPECT_EQ(h2.type, h.type);
    EXPECT_EQ(h2.generation, h.generation);
}

TEST(RuntimeHandlePool, VariantObjectStringsAndTypedPoolStrideAligned)
{
    // Odd (num_ints + num_floats) must still align VariantObject::strings() to alignof(String).
    const size_t sz = VariantObject::total_size(1, 0, 2);
    TypedHandlePool pool{sz, 8};

    TypedHandle h0 = pool.allocate();
    TypedHandle h1 = pool.allocate();
    ASSERT_TRUE(pool.valid(h0));
    ASSERT_TRUE(pool.valid(h1));

    auto* v0 = new (pool.get(h0)) VariantObject();
    auto* v1 = new (pool.get(h1)) VariantObject();
    v0->num_ints = 1;
    v0->num_floats = 0;
    v0->num_strings = 2;
    v1->num_ints = 1;
    v1->num_floats = 0;
    v1->num_strings = 2;

    EXPECT_EQ(reinterpret_cast<uintptr_t>(v0) % alignof(VariantObject), 0u);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(v1) % alignof(VariantObject), 0u);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(v0->strings()) % alignof(String), 0u);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(v1->strings()) % alignof(String), 0u);

    v0->~VariantObject();
    v1->~VariantObject();
    pool.destroy(h0);
    pool.destroy(h1);
}
