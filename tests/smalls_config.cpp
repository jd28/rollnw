#include "smalls_fixtures.hpp"

#include <nw/log.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/runtime.hpp>

#include <gtest/gtest.h>

#include <filesystem>

namespace fs = std::filesystem;

class SmallsConfig : public ::testing::Test {
protected:
    void SetUp() override
    {
        nw::kernel::services().start();
        nw::kernel::runtime().add_module_path(fs::path("test_data/smalls/config"));
    }

    void TearDown() override
    {
        nw::kernel::services().shutdown();
    }
};

// == Pure data config ========================================================

struct TestPoint {
    float x;
    float y;
};

TEST_F(SmallsConfig, LoadConfigPoint)
{
    auto& rt = nw::kernel::runtime();

    nw::smalls::ModuleBuilder mb(&rt, "test_types");
    mb.native_struct<TestPoint>("Point")
        .field("x", &TestPoint::x)
        .field("y", &TestPoint::y)
        .end_struct();
    mb.finalize();

    auto* p = rt.load_config<TestPoint>("test_point", "test_types");
    ASSERT_NE(p, nullptr);
    EXPECT_FLOAT_EQ(p->x, 1.5f);
    EXPECT_FLOAT_EQ(p->y, 2.5f);
}

// == Config with string ======================================================

struct TestSkill {
    nw::smalls::ScriptString name;
    int32_t ability;
    bool armor_check;
};

TEST_F(SmallsConfig, LoadConfigSkill)
{
    auto& rt = nw::kernel::runtime();

    nw::smalls::ModuleBuilder mb(&rt, "skill_types");
    mb.native_struct<TestSkill>("Skill")
        .field("name", &TestSkill::name)
        .field("ability", &TestSkill::ability)
        .field("armor_check", &TestSkill::armor_check)
        .end_struct();
    mb.finalize();

    auto* s = rt.load_config<TestSkill>("test_skill", "skill_types");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->name.view(rt), "Hide");
    EXPECT_EQ(s->ability, 1);
    EXPECT_TRUE(s->armor_check);
}

// == Config with closure =====================================================

struct TestAction {
    nw::smalls::ScriptString name;
    nw::smalls::ScriptClosure execute;
};

TEST_F(SmallsConfig, LoadConfigAction)
{
    auto& rt = nw::kernel::runtime();

    nw::smalls::ModuleBuilder mb(&rt, "action_types");
    mb.native_struct<TestAction>("Action")
        .field("name", &TestAction::name)
        .field("execute", &TestAction::execute)
        .end_struct();
    mb.finalize();

    auto* a = rt.load_config<TestAction>("test_action", "action_types");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->name.view(rt), "double");
    EXPECT_TRUE(a->execute);

    auto result = rt.call_closure(a->execute, {nw::smalls::Value::make_int(5)});
    EXPECT_EQ(result.data.ival, 10);
}

// == User prelude persistence ================================================

TEST_F(SmallsConfig, UserPreludePersistence)
{
    auto& rt = nw::kernel::runtime();

    nw::smalls::ModuleBuilder mb(&rt, "test_types");
    mb.native_struct<TestPoint>("Point")
        .field("x", &TestPoint::x)
        .field("y", &TestPoint::y)
        .end_struct();
    mb.finalize();

    rt.set_user_prelude("test_types");
    auto* p = rt.load_config<TestPoint>("test_point");
    ASSERT_NE(p, nullptr);
    EXPECT_FLOAT_EQ(p->x, 1.5f);
}
