#include <gtest/gtest.h>

#include <nw/log.hpp>
#include <nw/resources/Directory.hpp>

using namespace nw;
using namespace std::literals;

TEST(Directory, Construction)
{
    std::filesystem::path p{"./test_data/user/development/"};
    Directory d(p);
    EXPECT_TRUE(d.valid());
    EXPECT_EQ(d.name(), "development");
    EXPECT_EQ(d.path(), std::filesystem::canonical(p));

    EXPECT_TRUE(d.contains(Resource{"feat"sv, ResourceType::twoda}));
    auto data = d.demand(Resource{"feat"sv, ResourceType::twoda});
    EXPECT_TRUE(data.bytes.size());
    EXPECT_EQ(data.bytes.size(), std::filesystem::file_size("./test_data/user/development/feat.2da"));
    EXPECT_GT(d.all().size(), 0);

    EXPECT_FALSE(Directory{"./test_data/user/development/feat.2da"}.valid());
    EXPECT_FALSE(Directory{"./doesnotexist"}.valid());

    auto rd = d.stat(Resource{"feat"sv, ResourceType::twoda});
    EXPECT_EQ(data.bytes.size(), rd.size);
}

TEST(Directory, visit)
{
    std::filesystem::path p{"./test_data/user/development/"};
    Directory d(p);
    EXPECT_TRUE(d.valid());

    size_t count = 0;
    auto visitor = [&count](const nw::Resource&) {
        ++count;
    };

    d.visit(visitor);
    EXPECT_EQ(d.size(), count);
}
