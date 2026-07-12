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
    EXPECT_EQ(str.view(), "This is a Test");

    auto str2 = nw::kernel::strings().get_interned("asdf;lkj");
    EXPECT_FALSE(str2);

    auto str3 = nw::kernel::strings().get_interned("This is a Test");
    EXPECT_TRUE(str3);
    EXPECT_EQ(str3, str);

    auto str4 = nw::kernel::strings().intern("");
    EXPECT_FALSE(str4);

    auto str5 = nw::kernel::strings().intern(0);
    EXPECT_TRUE(str5);
    EXPECT_EQ(str5.view(), "Bad Strref");
}

TEST(KernelStrings, TextRefResolution)
{
    nw::kernel::strings().load_dialog_tlk("test_data/root/lang/en/data/dialog.tlk");

    nw::TextRef ref = nw::kernel::strings().make_text_ref(1000);
    EXPECT_EQ(nw::kernel::strings().get(ref), "Silence");

    EXPECT_TRUE(nw::kernel::strings().set_text(ref, nw::LanguageID::english, "Inline Silence"));
    EXPECT_EQ(nw::kernel::strings().get(ref), "Inline Silence");

    EXPECT_TRUE(nw::kernel::strings().set_text_override(ref, nw::LanguageID::english, "Runtime Silence"));
    EXPECT_EQ(nw::kernel::strings().get(ref), "Runtime Silence");

    nw::kernel::strings().clear_text_override(ref, nw::LanguageID::english);
    EXPECT_EQ(nw::kernel::strings().get(ref), "Inline Silence");

    nw::TextRef copy = nw::kernel::strings().clone_text_ref(ref);
    EXPECT_TRUE(nw::kernel::strings().set_text_override(copy, nw::LanguageID::english, "Copy Silence"));
    EXPECT_EQ(nw::kernel::strings().get(copy), "Copy Silence");
    EXPECT_EQ(nw::kernel::strings().get(ref), "Inline Silence");
}
