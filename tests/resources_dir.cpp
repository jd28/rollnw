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

    EXPECT_TRUE(d.contains(Resource{"test"sv, ResourceType::nss}));
    auto ba = d.demand(Resource{"test"sv, ResourceType::nss});
    EXPECT_TRUE(ba.size());
    EXPECT_EQ(ba.size(), std::filesystem::file_size("./test_data/user/development/test.nss"));
    EXPECT_GT(d.all().size(), 0);

    EXPECT_FALSE(Directory{"./test_data/user/development/test.nss"}.valid());
    EXPECT_FALSE(Directory{"./doesnotexist"}.valid());

    auto rd = d.stat(Resource{"test"sv, ResourceType::nss});
    EXPECT_EQ(ba.size(), rd.size);
}
