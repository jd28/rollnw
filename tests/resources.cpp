#include <gtest/gtest.h>

#include <nw/kernel/Memory.hpp>
#include <nw/resources/Erf.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/resources/StaticDirectory.hpp>
#include <nw/resources/StaticErf.hpp>
#include <nw/resources/StaticKey.hpp>
#include <nw/resources/StaticZip.hpp>
#include <nw/resources/assets.hpp>

#include <nlohmann/json.hpp>

using namespace nw;
using namespace std::literals;
namespace fs = std::filesystem;
namespace nwk = nw::kernel;

// == Erf =====================================================================
// ============================================================================

TEST(Erf, Construction)
{
    Erf e("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(e.valid());
    EXPECT_EQ(e.name(), "DockerDemo.mod");
    EXPECT_GT(e.size(), 0);
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
    auto rd = e.demand(r);
    EXPECT_EQ(rd.name, r);
    EXPECT_EQ(rd.bytes.size(), 1969);

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
    StaticErf e2{"test_data/user/hak/hak_with_description.hak"};
    EXPECT_TRUE(e1.merge(&e2));
    EXPECT_GT(e1.demand(r).bytes.size(), 0);
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
    e2.extract(nw::string::glob_to_regex("build.txt"), "tmp/");
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

// == StaticDirectory =========================================================
// ============================================================================

TEST(StaticDirectory, Construction)
{
    std::filesystem::path p{"./test_data/user/"};
    StaticDirectory d(p);
    EXPECT_TRUE(d.valid());
    EXPECT_EQ(d.name(), "user");
    EXPECT_EQ(d.path(), std::filesystem::canonical(p));

    nw::ResourceManager rm(nw::kernel::global_allocator());
    ASSERT_TRUE(rm.add_custom_container(&d, false));
    rm.build_registry();

    auto data = rm.demand({"feat"sv, ResourceType::twoda});
    EXPECT_TRUE(data.bytes.size());
    EXPECT_EQ(data.bytes.size(), std::filesystem::file_size("./test_data/user/development/feat.2da"));

    EXPECT_FALSE(StaticDirectory{"./test_data/user/development/feat.2da"}.valid());
    EXPECT_FALSE(StaticDirectory{"./doesnotexist"}.valid());
}

TEST(StaticDirectory, WithPackageJson)
{
    std::filesystem::path p{"./test_data/user/hak/hak_with_package_json"};
    StaticDirectory d(p);
    ASSERT_TRUE(d.valid());
    EXPECT_EQ(d.name(), "hak_with_package_json");
    EXPECT_EQ(d.path(), std::filesystem::canonical(p));

    nw::ResourceManager rm(nw::kernel::global_allocator());
    ASSERT_TRUE(rm.add_custom_container(&d, false));
    rm.build_registry();

    EXPECT_TRUE(rm.contains({"hak_with_package_json/test/cloth028"sv, nw::ResourceType::uti}));
}

// == StaticErf ===============================================================
// ============================================================================

TEST(StaticErf, Construction)
{
    StaticErf e("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(e.valid());
    EXPECT_EQ(e.name(), "DockerDemo.mod");
    EXPECT_GT(e.size(), 0);
    EXPECT_FALSE(StaticErf{"doesnotexist.hak"}.valid());

    StaticErf e2{"test_data/user/hak/hak_with_description.hak"};
    EXPECT_TRUE(nw::string::startswith(e2.description.get(nw::LanguageID::english), "test\nhttp://example.com"));
}

TEST(StaticErf, demand)
{
    StaticErf e("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(e.valid());
    // auto data = e.demand({"module"sv, ResourceType::ifo});
    // EXPECT_TRUE(data.bytes.size() > 0);
}

// == StaticKey ===============================================================
// ============================================================================

TEST(Key, Construction)
{
    auto install_path = nw::kernel::config().install_path();
    if (fs::exists(install_path / "data/nwn_base.key")) {
        nw::StaticKey k{install_path / "data/nwn_base.key"};
        EXPECT_GT(k.size(), 0);

        // nw::ResourceData data = k.demand(nw::resource_to_uri("nwscript"sv, nw::ResourceType::nss));
        // EXPECT_TRUE(data.bytes.size());
        // EXPECT_EQ(k.extract(std::regex("nwscript\\.nss"), "tmp/"), 1);
    }
}

TEST(Key, visit)
{

    auto install_path = nw::kernel::config().install_path();
    if (fs::exists(install_path / "data/nwn_base.key")) {
        nw::StaticKey k{install_path / "data/nwn_base.key"};

        size_t count = 0;
        auto visitor = [&count](nw::Resource, const nw::ContainerKey*) {
            ++count;
        };
        k.visit(visitor);
        EXPECT_EQ(k.size(), count);
    }
}

// == StaticZip ===============================================================
// ============================================================================

TEST(StaticZip, Construction)
{
    nw::StaticZip z{"test_data/user/modules/module_as_zip.zip"};
    EXPECT_TRUE(z.valid());
    EXPECT_TRUE(z.name() == "module_as_zip.zip");
    EXPECT_EQ(z.size(), 23);

    EXPECT_THROW(nw::StaticZip{"test_data/user/development/non-extant.zip"}, std::runtime_error);
}

TEST(StaticZip, visit)
{
    nw::StaticZip z{"test_data/user/modules/module_as_zip.zip"};
    EXPECT_TRUE(z.valid());

    size_t count = 0;
    auto visitor = [&count](nw::Resource, const nw::ContainerKey*) {
        ++count;
    };

    z.visit(visitor);
    EXPECT_EQ(z.size(), count);
}

// == Resref ==================================================================
// ============================================================================

TEST(Resref, JsonConversion)
{
    Resref r{"test"};
    nlohmann::json json_resref = r;
    EXPECT_TRUE(json_resref.is_string());
    Resref r2 = json_resref.get<Resref>();
    EXPECT_EQ(r, r2);
    EXPECT_TRUE(r2.length());
}

// == Resource ================================================================
// ============================================================================

TEST(Resource, FromPath)
{
    std::filesystem::path p1{"test.utc"};
    auto r1 = Resource::from_path(p1);
    EXPECT_TRUE(r1.valid());

    std::filesystem::path p2{"test.xxx"};
    auto r2 = Resource::from_path(p2);
    EXPECT_FALSE(r2.valid());

    std::filesystem::path p3{".xxx"};
    auto r3 = Resource::from_path(p3);
    EXPECT_FALSE(r3.valid());

    std::filesystem::path p4{""};
    auto r4 = Resource::from_path(p4);
    EXPECT_FALSE(r4.valid());

    std::filesystem::path p5{"test/test.ini"};
    auto r5 = Resource::from_path(p5);
    EXPECT_TRUE(r5.valid());

    // This is OK now..
    std::filesystem::path p6{"test/test_this_is_too_long_for_resref_of_32_chars.ini"};
    auto r6 = Resource::from_path(p6);
    EXPECT_TRUE(r6.valid());
}

TEST(Resource, FromFilename)
{
    std::string p1{"test.utc"};
    auto r1 = Resource::from_filename(p1);
    EXPECT_TRUE(r1.valid());

    std::string p2{"test.xxx"};
    auto r2 = Resource::from_filename(p2);
    EXPECT_FALSE(r2.valid());

    std::string p3{".xxx"};
    auto r3 = Resource::from_filename(p3);
    EXPECT_FALSE(r3.valid());

    std::string p4{""};
    auto r4 = Resource::from_filename(p4);
    EXPECT_FALSE(r4.valid());

    // This is OK now..
    std::string p6{"test_this_is_too_long_for_resref_of_32_chars.ini"};
    auto r6 = Resource::from_filename(p6);
    EXPECT_TRUE(r6.valid());
}

TEST(Resource, Resref)
{
    std::array<char, 16> test1{'t', 'e', 's', 't'};
    nw::Resref resref1(test1);
    EXPECT_EQ(resref1.view(), "test");

    std::array<char, 16> test2{'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a'};
    nw::Resref resref2(test2);
    EXPECT_EQ(resref2.view(), "aaaaaaaaaaaaaaaa");

    std::array<char, 17> test3{'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a'};
    nw::Resref resref3(test3);
    EXPECT_FALSE(resref3.empty());

    std::string_view test4{"aaaaaaaaaaaaaaaa"};
    nw::Resref resref4(test4);
    EXPECT_EQ(resref4.view(), "aaaaaaaaaaaaaaaa");

    std::string_view test5{"test"};
    nw::Resref resref5(test5);
    EXPECT_EQ(resref5.view(), "test");
}

TEST(Resource, JsonConversion)
{
    Resource r{"test"sv, ResourceType::twoda};
    nlohmann::json resource_json = r;
    EXPECT_EQ(r.filename(), resource_json.get<std::string>());

    Resource r2 = resource_json.get<Resource>();
    EXPECT_EQ(r, r2);
}

// == ResourceType ============================================================
// ============================================================================

TEST(ResourceType, Conversion)
{
    EXPECT_EQ(ResourceType::from_extension("2da"), ResourceType::twoda);
    EXPECT_EQ(ResourceType::from_extension(".2da"), ResourceType::twoda);
    EXPECT_EQ(ResourceType::from_extension("xxx"), ResourceType::invalid);

    EXPECT_EQ(ResourceType::to_string(ResourceType::txi), "txi"s);
}

// == ResourceManager =========================================================
// ============================================================================

TEST(KernelResources, AddContainer)
{
    auto rm = new nw::ResourceManager{nwk::global_allocator()};
    auto sz = rm->size();
    nw::StaticErf e("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(rm->add_custom_container(&e, false));
    rm->build_registry();
    EXPECT_EQ(rm->size(), sz + e.size());

    nw::ResourceManager rm2{nwk::global_allocator(), rm};

    delete rm;
}

TEST(KernelResources, Extract)
{
    auto rm = new nw::ResourceManager{nwk::global_allocator()};
    nw::StaticErf se1("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(rm->add_custom_container(&se1, false));
    nw::StaticZip sz1("test_data/user/modules/module_as_zip.zip");
    EXPECT_TRUE(rm->add_custom_container(&sz1, false));
    nw::StaticZip sz2("test_data/user/modules/module_as_zip.zip");
    EXPECT_FALSE(rm->add_custom_container(&sz2, false));
    delete rm;
}

TEST(KernelResources, LoadModule)
{
    auto rm = new nw::ResourceManager{nwk::global_allocator()};
    EXPECT_TRUE(rm->load_module("test_data/user/modules/DockerDemo.mod"));
    delete rm;
}

TEST(KernelResources, LoadPlayerCharacter)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto data = nwk::resman().demand_server_vault("CDKEY", "testsorcpc1");
    EXPECT_TRUE(data.bytes.size());

    data = nwk::resman().demand_server_vault("WRONGKEY", "testsorcpc1");
    EXPECT_FALSE(data.bytes.size());

    data = nwk::resman().demand_server_vault("CDKEY", "WRONGNAME");
    EXPECT_FALSE(data.bytes.size());
}

TEST(KernelResources, Teture)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto tex1 = nwk::resman().texture("doesn'texist"sv);
    EXPECT_FALSE(tex1);

    auto tex2 = nwk::resman().texture("tno01_wtcliff01"sv);
    EXPECT_TRUE(tex2);
    EXPECT_TRUE(tex2->valid());
}

TEST(KernelResources, visit)
{
    auto rm = new nw::ResourceManager{nwk::global_allocator()};
    auto sz = rm->size();
    nw::StaticErf e("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(rm->add_custom_container(&e, false));
    rm->build_registry();

    size_t count = 0;
    auto visitor = [&count](const nw::Resource&) {
        ++count;
    };

    rm->visit(visitor);
    EXPECT_EQ(rm->size(), sz + e.size());
    delete rm;
}
