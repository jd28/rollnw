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
