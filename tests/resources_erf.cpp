#include <gtest/gtest.h>

#include <nw/log.hpp>
#include <nw/resources/Erf.hpp>
#include <nw/util/string.hpp>

using namespace nw;
using namespace std::literals;
namespace fs = std::filesystem;

TEST(Erf, Construction)
{
    Erf e("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(e.valid());
    EXPECT_EQ(e.name(), "DockerDemo.mod");
    EXPECT_TRUE(e.size() > 0);
    EXPECT_TRUE(e.all().size() > 0);
    EXPECT_FALSE(Erf{"doesnotexist.hak"}.valid());

    Erf e2{"test_data/user/hak/hak_with_description.hak"};
    EXPECT_TRUE(nw::string::startswith(e2.description.get(nw::LanguageID::english), "test\nhttp://example.com"));
}

TEST(Erf, demand)
{
    Erf e("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(e.valid());
    auto data = e.demand({"module"sv, ResourceType::ifo});
    EXPECT_TRUE(data.bytes.size() > 0);
}

TEST(Erf, extract)
{
    Erf e("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(e.valid());
    auto data = e.demand({"module"sv, ResourceType::ifo});
    EXPECT_EQ(e.extract(std::regex("module.ifo"), "tmp/"), 1);
    EXPECT_EQ(data.bytes.size(), std::filesystem::file_size("tmp/module.ifo"));
}

TEST(Erf, add)
{
    Erf e{"test_data/user/hak/hak_with_description.hak"};
    Resource r{"cloth028"sv, nw::ResourceType::uti};
    e.add("test_data/user/development/cloth028.uti");
    auto rd = e.stat(r);
    EXPECT_EQ(rd.name, r);
    EXPECT_EQ(rd.size, 1969);

    nw::ResourceData data = nw::ResourceData::from_file(u8"test_data/user/development/cloth028.uti");
    auto data2 = e.demand(r);
    EXPECT_EQ(data, data2);

    EXPECT_FALSE(e.add("this_is_invalid_path_to_resource.xxxx"));
}

TEST(Erf, erase)
{
    Resource r{"build"sv, nw::ResourceType::txt};
    Erf e{"test_data/user/hak/hak_with_description.hak"};
    EXPECT_TRUE(e.erase(r) != 0);
    EXPECT_EQ(e.size(), 0);
}

TEST(Erf, merge)
{
    Resource r{"build"sv, nw::ResourceType::txt};
    Erf e1("test_data/user/modules/DockerDemo.mod");
    Erf e2{"test_data/user/hak/hak_with_description.hak"};
    EXPECT_TRUE(e1.merge(&e2));
    EXPECT_EQ(e1.stat(r).size, e2.stat(r).size);
}

TEST(Erf, save)
{
    Erf e{"test_data/user/hak/hak_with_description.hak"};
    EXPECT_TRUE(e.valid());
    EXPECT_TRUE(e.save());
    EXPECT_TRUE(e.reload());
    Resource r2{"build"sv, nw::ResourceType::txt};
    auto data = e.demand(r2);
    EXPECT_TRUE(data.bytes.size() > 0);
}

TEST(Erf, save_as)
{
    Erf e{"test_data/user/hak/hak_with_description.hak"};
    EXPECT_TRUE(e.valid());
    Resource r2{"build"sv, nw::ResourceType::txt};
    auto data = e.demand(r2);

    Resource r{"cloth028"sv, nw::ResourceType::uti};
    e.add("test_data/user/development/cloth028.uti");
    EXPECT_TRUE(e.save_as(fs::path("tmp/hak_with_description.hak")));
    Erf e2{"tmp/hak_with_description.hak"};
    auto data2 = e2.demand(r2);

    EXPECT_EQ(data, data2);
    EXPECT_EQ(e2.size(), 2);

    auto data3 = ResourceData::from_file("test_data/user/development/cloth028.uti");
    auto data4 = e2.demand(r);
    EXPECT_EQ(data3, data4);
    e2.extract_by_glob("build.txt", "tmp/");
}

TEST(Erf, visit)
{
    Erf e("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(e.valid());
    EXPECT_EQ(e.name(), "DockerDemo.mod");
    size_t count = 0;
    auto visitor = [&count](const nw::Resource&) {
        ++count;
    };
    e.visit(visitor);
    EXPECT_EQ(e.size(), count);
}

TEST(Erf, v1_1)
{
    Erf e("test_data/user/hak/westgate2.hak");
    EXPECT_TRUE(e.valid());
    EXPECT_EQ(e.name(), "westgate2.hak");
    EXPECT_EQ(e.version, nw::ErfVersion::v1_1);
}
