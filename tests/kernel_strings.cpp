#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Strings.hpp>
#include <nw/log.hpp>

TEST(KernelStrings, LoadTLk)
{
    nw::kernel::strings().load_dialog_tlk("test_data/root/lang/en/data/dialog.tlk");
    nw::kernel::strings().load_custom_tlk("test_data/root/lang/en/data/dialog.tlk");

    EXPECT_EQ(nw::kernel::strings().get(1000), "Silence");
    EXPECT_EQ(nw::kernel::strings().get(0x01001000), "Stay here and don't move until I return.");
    EXPECT_EQ(nw::kernel::strings().get(0xFFFFFFFF), "");

    nw::LocString test{1000};
    EXPECT_EQ(nw::kernel::strings().get(test), "Silence");
    test.add(nw::LanguageID::english, "Silencio");
    EXPECT_EQ(nw::kernel::strings().get(test), "Silencio");
}

TEST(KernelStrings, Intern)
{
    auto str = nw::kernel::strings().intern("This is a Test");
    EXPECT_EQ(str, "This is a Test");

    auto str2 = nw::kernel::strings().get_interned("asdf;lkj");
    EXPECT_FALSE(str2);

    auto str3 = nw::kernel::strings().get_interned("This is a Test");
    EXPECT_TRUE(str3);
}
