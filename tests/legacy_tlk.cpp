#include <gtest/gtest.h>

#include "test_nwn_root.hpp"

#include <nw/i18n/Tlk.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>

#include <filesystem>
#include <fstream>
#include <limits>
#include <utility>

namespace {

// The English TLK comes from the dedicated-server data, not from the repo.
// main() already refuses to run without that data, so this never returns a
// path that does not exist.
std::filesystem::path english_tlk()
{
    auto path = nw::test::dedicated_dialog_tlk();
    return path ? *path : std::filesystem::path{};
}

} // namespace

TEST(Tlk, LoadEnglish)
{
    nw::Tlk t = nw::Tlk(english_tlk());
    EXPECT_TRUE(t.valid());
    EXPECT_GT(t.size(), 0);
    EXPECT_EQ(t.get(1000), "Silence");
    EXPECT_EQ(t.get(0xFFFFFFFF), "");
}

// Was a German dialog.tlk round trip, which existed only to prove non-ASCII
// text survives save and reload. Writing the text ourselves proves the same
// thing without a second 13 MB language file in the repo, and pins exact
// strings rather than whichever ones the shipping table happened to carry.
//
// Deliberately round-trips through a loaded table rather than a
// default-constructed one: Tlk::save_as() treats its `max_strref` as both a
// count and an index (`ele.resize(max_strref)`), so building a table from
// nothing drops the highest strref set and can fault. That is a real defect,
// recorded in issues/test-data-game-content-removal.md, not something this
// test should paper over by avoiding non-ASCII.
TEST(Tlk, NonAsciiTextSurvivesSaveAndReload)
{
    std::filesystem::create_directories("tmp");

    nw::Tlk t{english_tlk()};
    ASSERT_TRUE(t.valid());
    t.set(1, "Mönch");
    t.set(2, "Épée");
    t.save_as("tmp/dialog_nonascii.tlk");

    nw::Tlk reloaded{"tmp/dialog_nonascii.tlk"};
    EXPECT_TRUE(reloaded.valid());
    EXPECT_EQ(reloaded.get(1), "Mönch");
    EXPECT_EQ(reloaded.get(2), "Épée");
}

// save_as() used to treat its bound as both a count and an index, so a strref
// set at or beyond the table's current size was written into a table too small
// to hold it and vanished on reload.
TEST(Tlk, SetBeyondTheEndSurvivesSaveAndReload)
{
    std::filesystem::create_directories("tmp");

    nw::Tlk t{english_tlk()};
    ASSERT_TRUE(t.valid());
    const uint32_t past_end = static_cast<uint32_t>(t.size());

    t.set(past_end, "appended entry");
    EXPECT_EQ(t.get(past_end), "appended entry");
    t.save_as("tmp/dialog_appended.tlk");

    nw::Tlk reloaded{"tmp/dialog_appended.tlk"};
    ASSERT_TRUE(reloaded.valid());
    EXPECT_EQ(reloaded.size(), past_end + 1)
        << "the table must grow to include the new strref";
    EXPECT_EQ(reloaded.get(past_end), "appended entry");
    // The entry before it must be untouched.
    EXPECT_EQ(reloaded.get(1000), "Silence");
}

// A table built from nothing rather than loaded from a file. save_as() used to
// reassign header_.str_count before its second loop and then index elements_ by
// the new count, so this faulted outright: a default-constructed Tlk has no
// element array at all.
TEST(Tlk, BuildsFromScratch)
{
    std::filesystem::create_directories("tmp");

    nw::Tlk built;
    EXPECT_TRUE(built.valid());
    EXPECT_EQ(built.size(), 0u);

    built.set(0, "first");
    built.set(10, "Mönch");
    built.set(11, "last");
    built.save_as("tmp/dialog_scratch.tlk");

    nw::Tlk reloaded{"tmp/dialog_scratch.tlk"};
    ASSERT_TRUE(reloaded.valid());
    EXPECT_EQ(reloaded.size(), 12u) << "the highest strref set must be included";
    EXPECT_EQ(reloaded.get(0), "first");
    EXPECT_EQ(reloaded.get(10), "Mönch");
    EXPECT_EQ(reloaded.get(11), "last");
    // The strrefs never set are present but empty, not garbage.
    EXPECT_EQ(reloaded.get(5), "");
    EXPECT_EQ(reloaded.get(12), "");
}

// Appending past a gap: strrefs between the old end and the new one were not
// modified, so the fallback branch ran for indices beyond the loaded element
// array.
TEST(Tlk, AppendingPastAGapDoesNotReadOffTheEnd)
{
    std::filesystem::create_directories("tmp");

    nw::Tlk t{english_tlk()};
    ASSERT_TRUE(t.valid());
    const uint32_t end = static_cast<uint32_t>(t.size());

    t.set(end + 4, "beyond a gap");
    t.save_as("tmp/dialog_gap.tlk");

    nw::Tlk reloaded{"tmp/dialog_gap.tlk"};
    ASSERT_TRUE(reloaded.valid());
    EXPECT_EQ(reloaded.size(), end + 5);
    EXPECT_EQ(reloaded.get(end + 4), "beyond a gap");
    EXPECT_EQ(reloaded.get(end + 1), "") << "the gap must be empty, not stale data";
    EXPECT_EQ(reloaded.get(1000), "Silence") << "existing entries must survive";
}

TEST(Tlk, Set)
{
    nw::Tlk t = nw::Tlk(english_tlk());
    EXPECT_TRUE(t.valid());
    t.set(1, "Hello World");
    EXPECT_EQ(t.get(1), "Hello World");
}

TEST(Tlk, MoveRebuildsElementPointers)
{
    nw::Tlk source{english_tlk()};
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
    nw::Tlk t = nw::Tlk(english_tlk());
    EXPECT_TRUE(t.valid());
    t.set(1, "Hello World");
    t.save_as("tmp/dialog_en_modified.tlk");
    nw::Tlk t2 = nw::Tlk{"tmp/dialog_en_modified.tlk"};
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

TEST(Tlk, RepeatedMalformedLoadsLeaveEmptyInvalidState)
{
    std::filesystem::create_directories("tmp");

    {
        std::ofstream out{"tmp/tiny-malformed.tlk", std::ios::binary};
        out << '\x8b';
    }

    for (int i = 0; i < 512; ++i) {
        nw::Tlk tlk{"tmp/tiny-malformed.tlk"};
        EXPECT_FALSE(tlk.valid());
        EXPECT_EQ(tlk.size(), 0);
    }
}
