#include <gtest/gtest.h>

#include <nw/i18n/Tlk.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>

#include <filesystem>
#include <fstream>
#include <limits>
#include <utility>

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
    auto install_path = nw::kernel::config().install_path();
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

TEST(Tlk, MoveRebuildsElementPointers)
{
    nw::Tlk source{"test_data/root/lang/en/data/dialog.tlk"};
    ASSERT_TRUE(source.valid());

    nw::Tlk assigned;
    assigned = std::move(source);
    EXPECT_TRUE(assigned.valid());
    EXPECT_EQ(assigned.get(1000), "Silence");
    EXPECT_FALSE(source.valid());

    nw::Tlk constructed{std::move(assigned)};
    EXPECT_TRUE(constructed.valid());
    EXPECT_EQ(constructed.get(1000), "Silence");
    EXPECT_FALSE(assigned.valid());
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

TEST(Tlk, RejectsOutOfRangeStrrefAndWrappedStringRange)
{
    std::filesystem::create_directories("tmp");

    {
        nw::TlkHeader header{};
        header.str_count = 0;
        header.str_offset = sizeof(nw::TlkHeader);

        std::ofstream out{"tmp/empty.tlk", std::ios::binary};
        out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    }

    nw::Tlk empty{"tmp/empty.tlk"};
    EXPECT_TRUE(empty.valid());
    EXPECT_EQ(empty.get(0), "");

    {
        nw::TlkHeader header{};
        header.str_count = 1;
        header.str_offset = sizeof(nw::TlkHeader) + sizeof(nw::TlkElement);

        nw::TlkElement element{};
        element.flags = nw::TlkFlags::text;
        element.offset = std::numeric_limits<uint32_t>::max();
        element.size = 4;

        std::ofstream out{"tmp/wrapped.tlk", std::ios::binary};
        out.write(reinterpret_cast<const char*>(&header), sizeof(header));
        out.write(reinterpret_cast<const char*>(&element), sizeof(element));
    }

    nw::Tlk wrapped{"tmp/wrapped.tlk"};
    EXPECT_TRUE(wrapped.valid());
    EXPECT_EQ(wrapped.get(0), "");
    EXPECT_EQ(wrapped.get(1), "");
}

TEST(Tlk, MalformedFileIsInvalidWithoutThrowing)
{
    std::filesystem::create_directories("tmp");

    {
        std::ofstream out{"tmp/not-a-tlk.tlk", std::ios::binary};
        out << "Tnot-a-tlk\n";
    }

    nw::Tlk tlk{"tmp/not-a-tlk.tlk"};
    EXPECT_FALSE(tlk.valid());
    EXPECT_EQ(tlk.size(), 0);
    EXPECT_EQ(tlk.get(0), "");
}
