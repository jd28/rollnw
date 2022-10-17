#include <catch2/catch_all.hpp>

#include <nw/log.hpp>
#include <nw/resources/Erf.hpp>
#include <nw/util/string.hpp>

using namespace nw;
using namespace std::literals;
namespace fs = std::filesystem;

TEST_CASE("erf: loading", "[containers]")
{
    Erf e("test_data/user/modules/DockerDemo.mod");
    REQUIRE(e.valid());
    REQUIRE(e.name() == "DockerDemo.mod");
    REQUIRE(e.size() > 0);
    REQUIRE(e.all().size() > 0);
    REQUIRE_FALSE(Erf{"doesnotexist.hak"}.valid());

    Erf e2{"test_data/user/hak/hak_with_description.hak"};
    REQUIRE(nw::string::startswith(e2.description.get(nw::LanguageID::english), "test\nhttp://example.com"));
}

TEST_CASE("erf: demand", "[containers]")
{
    Erf e("test_data/user/modules/DockerDemo.mod");
    REQUIRE(e.valid());
    auto ba = e.demand({"module"sv, ResourceType::ifo});
    REQUIRE(ba.size() > 0);
}

TEST_CASE("erf: extract", "[containers]")
{
    Erf e("test_data/user/modules/DockerDemo.mod");
    REQUIRE(e.valid());
    auto ba = e.demand({"module"sv, ResourceType::ifo});
    REQUIRE(e.extract(std::regex("module.ifo"), "tmp/") == 1);
    REQUIRE(ba.size() == std::filesystem::file_size("tmp/module.ifo"));
}

TEST_CASE("erf: add", "[containers]")
{
    Erf e{"test_data/user/hak/hak_with_description.hak"};
    Resource r{"cloth028"sv, nw::ResourceType::uti};
    e.add("test_data/user/development/cloth028.uti");
    auto rd = e.stat(r);
    REQUIRE(rd.name == r);
    REQUIRE(rd.size == 1969);

    nw::ByteArray ba = nw::ByteArray::from_file(u8"test_data/user/development/cloth028.uti");
    auto ba2 = e.demand(r);
    REQUIRE(ba == ba2);

    REQUIRE_FALSE(e.add("this_is_invalid_path_to_resource.xxxx"));
}

TEST_CASE("Erf::erase", "[containers]")
{
    Resource r{"build"sv, nw::ResourceType::txt};
    Erf e{"test_data/user/hak/hak_with_description.hak"};
    REQUIRE(e.erase(r) != 0);
    REQUIRE(e.size() == 0);
}

TEST_CASE("Erf::merge", "[containers]")
{
    Resource r{"build"sv, nw::ResourceType::txt};
    Erf e1("test_data/user/modules/DockerDemo.mod");
    Erf e2{"test_data/user/hak/hak_with_description.hak"};
    REQUIRE(e1.merge(&e2));
    REQUIRE(e1.stat(r).size == e2.stat(r).size);
}

TEST_CASE("Erf::save", "[containers]")
{
    Erf e{"test_data/user/hak/hak_with_description.hak"};
    REQUIRE(e.valid());
    REQUIRE(e.save());
    REQUIRE(e.reload());
    Resource r2{"build"sv, nw::ResourceType::txt};
    auto ba = e.demand(r2);
    REQUIRE(ba.size() > 0);
}

TEST_CASE("Erf::save_as", "[containers]")
{
    Erf e{"test_data/user/hak/hak_with_description.hak"};
    REQUIRE(e.valid());
    Resource r2{"build"sv, nw::ResourceType::txt};
    auto ba = e.demand(r2);

    Resource r{"cloth028"sv, nw::ResourceType::uti};
    e.add("test_data/user/development/cloth028.uti");
    REQUIRE(e.save_as(fs::path("tmp/hak_with_description.hak")));
    Erf e2{"tmp/hak_with_description.hak"};
    auto ba2 = e2.demand(r2);

    REQUIRE(ba == ba2);
    REQUIRE(e2.size() == 2);

    auto ba3 = ByteArray::from_file("test_data/user/development/cloth028.uti");
    auto ba4 = e2.demand(r);
    REQUIRE(ba3 == ba4);
    e2.extract_by_glob("build.txt", "tmp/");
}
