#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/legacy/Tlk.hpp>
#include <nw/log.hpp>

TEST(Tlk, LoadEnglish)
{
    nw::Tlk t = nw::Tlk("test_data/root/lang/en/data/dialog.tlk");
    EXPECT_TRUE(t.valid());
    EXPECT_GT(t.size(), 0);
    EXPECT_EQ(t.get(1000), "Silence");
    EXPECT_EQ(t.get(0xFFFFFFFF), "");
}

TEST(Tlk, LoadGerman)
{
    auto install_path = nw::kernel::config().options().install;
    nw::Tlk de{"test_data/root/lang/de/data/dialog.tlk"};
    EXPECT_TRUE(de.valid());
    EXPECT_EQ(de.get(10), "Mönch");
    de.save_as("tmp/dialog.tlk");
    nw::Tlk t2 = nw::Tlk{"tmp/dialog.tlk"};
    EXPECT_TRUE(t2.valid());
    EXPECT_EQ(de.get(10), "Mönch");
}

TEST(Tlk, Set)
{
    nw::Tlk t = nw::Tlk("test_data/root/lang/en/data/dialog.tlk");
    EXPECT_TRUE(t.valid());
    t.set(1, "Hello World");
    EXPECT_EQ(t.get(1), "Hello World");
}

TEST(Tlk, Save)
{
    nw::Tlk t = nw::Tlk("test_data/root/lang/en/data/dialog.tlk");
    EXPECT_TRUE(t.valid());
    t.set(1, "Hello World");
    t.save_as("tmp/dialog.tlk");
    nw::Tlk t2 = nw::Tlk{"tmp/dialog.tlk"};
    EXPECT_TRUE(t2.valid());
    EXPECT_TRUE(t2.size() > 0);
    EXPECT_EQ(t2.get(1), "Hello World");
    EXPECT_EQ(t2.get(1000), "Silence");
    EXPECT_EQ(t2.get(0xFFFFFFFF), "");
}
