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

#include <array>
#include <cstring>
#include <fstream>
#include <limits>

using namespace nw;
using namespace std::literals;
namespace fs = std::filesystem;
namespace nwk = nw::kernel;

namespace {

struct TestErfHeader {
    char type[4];
    char version[4];
    uint32_t locstring_count;
    uint32_t locstring_size;
    uint32_t entry_count;
    uint32_t offset_locstring;
    uint32_t offset_keys;
    uint32_t offset_res;
    uint32_t year;
    uint32_t day_of_year;
    uint32_t desc_strref;
    char reserved[116];
};

template <size_t N>
struct TestErfKey {
    std::array<char, N> resref{};
    uint32_t id = 0;
    uint16_t type = 0;
    int16_t unused = 0;
};

struct TestErfElementInfo {
    uint32_t offset = 0;
    uint32_t size = 0;
};

struct TestKeyHeader {
    char type[4];
    char version[4];
    uint32_t bif_count;
    uint32_t key_count;
    uint32_t offset_file_table;
    uint32_t offset_key_table;
    uint32_t year;
    uint32_t day_of_year;
    char reserved[32];
};

struct TestFileTable {
    uint32_t size = 0;
    uint32_t name_offset = 0;
    uint16_t name_size = 0;
    uint16_t drives = 0;
};

struct TestBifHeader {
    char type[4];
    char version[4];
    uint32_t var_res_count;
    uint32_t fix_res_count;
    uint32_t var_table_offset;
};

struct TestBifElement {
    uint32_t id = 0;
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t type = 0;
};

static_assert(sizeof(TestErfHeader) == 160);
static_assert(sizeof(TestErfKey<16>) == 24);
static_assert(sizeof(TestErfElementInfo) == 8);
static_assert(sizeof(TestKeyHeader) == 64);
static_assert(sizeof(TestFileTable) == 12);
static_assert(sizeof(TestBifHeader) == 20);
static_assert(sizeof(TestBifElement) == 16);

template <typename T>
void write_value(std::ofstream& out, const T& value)
{
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

void write_wrapped_erf(const fs::path& path)
{
    TestErfHeader header{};
    std::memcpy(header.type, "ERF ", 4);
    std::memcpy(header.version, "V1.0", 4);
    header.entry_count = 1;
    header.offset_keys = sizeof(TestErfHeader);
    header.offset_res = header.offset_keys + sizeof(TestErfKey<16>);
    header.offset_locstring = header.offset_res + sizeof(TestErfElementInfo);

    TestErfKey<16> key{};
    std::memcpy(key.resref.data(), "wrapped", 7);
    key.type = static_cast<uint16_t>(ResourceType::txt);

    TestErfElementInfo info{};
    info.offset = std::numeric_limits<uint32_t>::max() - 7;
    info.size = 16;

    std::ofstream out{path, std::ios::binary};
    write_value(out, header);
    write_value(out, key);
    write_value(out, info);
}

void write_key_with_long_bif_name(const fs::path& path)
{
    TestKeyHeader header{};
    std::memcpy(header.type, "KEY ", 4);
    std::memcpy(header.version, "V1  ", 4);
    header.bif_count = 1;
    header.offset_file_table = sizeof(TestKeyHeader);
    header.offset_key_table = sizeof(TestKeyHeader) + sizeof(TestFileTable) + 256;

    TestFileTable table{};
    table.name_offset = sizeof(TestKeyHeader) + sizeof(TestFileTable);
    table.name_size = 256;

    std::array<char, 256> name{};
    name.fill('a');

    std::ofstream out{path, std::ios::binary};
    write_value(out, header);
    write_value(out, table);
    out.write(name.data(), name.size());
}

void write_key_and_bif_with_wrapped_resource(const fs::path& root)
{
    fs::create_directories(root / "data");

    const fs::path key_path = root / "data/wrapped.key";
    TestKeyHeader key_header{};
    std::memcpy(key_header.type, "KEY ", 4);
    std::memcpy(key_header.version, "V1  ", 4);
    key_header.bif_count = 1;
    key_header.key_count = 1;
    key_header.offset_file_table = sizeof(TestKeyHeader);
    key_header.offset_key_table = sizeof(TestKeyHeader) + sizeof(TestFileTable) + 8;

    TestFileTable file_table{};
    file_table.name_offset = sizeof(TestKeyHeader) + sizeof(TestFileTable);
    file_table.name_size = 8;

    std::array<char, 8> bif_name{};
    std::memcpy(bif_name.data(), "bad.bif", 7);

    std::array<char, 16> resref{};
    std::memcpy(resref.data(), "wrapped", 7);
    uint16_t type = static_cast<uint16_t>(ResourceType::txt);
    uint32_t id = 0;

    {
        std::ofstream out{key_path, std::ios::binary};
        write_value(out, key_header);
        write_value(out, file_table);
        out.write(bif_name.data(), bif_name.size());
        out.write(resref.data(), resref.size());
        write_value(out, type);
        write_value(out, id);
    }

    TestBifHeader bif_header{};
    std::memcpy(bif_header.type, "BIFF", 4);
    std::memcpy(bif_header.version, "V1  ", 4);
    bif_header.var_res_count = 1;
    bif_header.var_table_offset = sizeof(TestBifHeader);

    TestBifElement element{};
    element.offset = std::numeric_limits<uint32_t>::max() - 7;
    element.size = 16;
    element.type = static_cast<uint32_t>(ResourceType::txt);

    std::ofstream out{root / "bad.bif", std::ios::binary};
    write_value(out, bif_header);
    write_value(out, element);
}

} // namespace

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

TEST(Erf, DemandRejectsWrappedResourceRange)
{
    const fs::path path = "tmp/wrapped.erf";
    write_wrapped_erf(path);

    Erf e{path};
    ASSERT_TRUE(e.valid());
    auto data = e.demand({"wrapped"sv, ResourceType::txt});
    EXPECT_TRUE(data.bytes.size() == 0);
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

TEST(StaticDirectory, AuthoredJsonKeepsResourceType)
{
    const std::filesystem::path root{"tmp/static_directory_authored_json"};
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "blueprints" / "creatures");
    std::ofstream{root / "blueprints" / "creatures" / "goblin.utc.json"} << R"({"$type":"UTC"})";

    StaticDirectory d(root);
    ASSERT_TRUE(d.valid());

    nw::ResourceManager rm(nw::kernel::global_allocator());
    ASSERT_TRUE(rm.add_custom_container(&d, false));
    rm.build_registry();

    EXPECT_TRUE(rm.contains({"goblin"sv, nw::ResourceType::utc}));
    EXPECT_FALSE(rm.contains({"goblin.utc"sv, nw::ResourceType::json}));

    auto data = rm.demand({"goblin"sv, nw::ResourceType::utc});
    EXPECT_EQ(data.name, (Resource{"goblin"sv, nw::ResourceType::utc}));
    EXPECT_EQ(data.bytes.string_view(), R"({"$type":"UTC"})");
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

    const auto module = Resource{"module"sv, ResourceType::ifo};
    const ContainerKey* module_key = nullptr;
    e.visit([&](Resource res, const ContainerKey* key) {
        if (res == module) {
            module_key = key;
        }
    });

    ASSERT_NE(module_key, nullptr);
    auto data = e.demand(module_key);
    EXPECT_EQ(data.name, module);
    EXPECT_TRUE(data.bytes.size() > 0);
}

TEST(StaticErf, DemandRejectsWrappedResourceRange)
{
    const fs::path path = "tmp/static_wrapped.erf";
    write_wrapped_erf(path);

    StaticErf e{path};
    ASSERT_TRUE(e.valid());

    const ContainerKey* wrapped_key = nullptr;
    e.visit([&](Resource res, const ContainerKey* key) {
        if (res == Resource{"wrapped"sv, ResourceType::txt}) {
            wrapped_key = key;
        }
    });

    ASSERT_NE(wrapped_key, nullptr);
    auto data = e.demand(wrapped_key);
    EXPECT_TRUE(data.bytes.size() == 0);
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

TEST(Key, RejectsLongBifName)
{
    const fs::path root = "tmp/key_long_name/data";
    fs::create_directories(root);
    const fs::path path = root / "bad.key";
    write_key_with_long_bif_name(path);

    nw::StaticKey key{path};
    EXPECT_FALSE(key.valid());
}

TEST(Key, DemandRejectsWrappedBifResourceRange)
{
    const fs::path root = "tmp/key_wrapped";
    write_key_and_bif_with_wrapped_resource(root);

    nw::StaticKey key{root / "data/wrapped.key"};
    ASSERT_TRUE(key.valid());

    const nw::ContainerKey* wrapped_key = nullptr;
    key.visit([&](nw::Resource res, const nw::ContainerKey* container_key) {
        if (res == nw::Resource{"wrapped"sv, nw::ResourceType::txt}) {
            wrapped_key = container_key;
        }
    });

    ASSERT_NE(wrapped_key, nullptr);
    auto data = key.demand(wrapped_key);
    EXPECT_TRUE(data.bytes.size() == 0);
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

    std::filesystem::path p7{"module.ifo.json"};
    auto r7 = Resource::from_path(p7);
    EXPECT_TRUE(r7.valid());
    EXPECT_EQ(r7.resref.view(), "module");
    EXPECT_EQ(r7.type, ResourceType::ifo);

    std::filesystem::path p8{"blueprints/creatures/goblin.utc.json"};
    auto r8 = Resource::from_path(p8);
    EXPECT_TRUE(r8.valid());
    EXPECT_EQ(r8.resref.view(), "goblin");
    EXPECT_EQ(r8.type, ResourceType::utc);

    std::filesystem::path p9{"hak_with_package_json/test/cloth028.uti.json"};
    auto r9 = Resource::from_path(p9, true);
    EXPECT_TRUE(r9.valid());
    EXPECT_EQ(r9.resref.view(), "hak_with_package_json/test/cloth028");
    EXPECT_EQ(r9.type, ResourceType::uti);
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

    std::string p7{"module.ifo.json"};
    auto r7 = Resource::from_filename(p7);
    EXPECT_TRUE(r7.valid());
    EXPECT_EQ(r7.resref.view(), "module");
    EXPECT_EQ(r7.type, ResourceType::ifo);

    std::string p8{"hak_with_package_json/test/cloth028.uti.json"};
    auto r8 = Resource::from_filename(p8);
    EXPECT_TRUE(r8.valid());
    EXPECT_EQ(r8.resref.view(), "hak_with_package_json/test/cloth028");
    EXPECT_EQ(r8.type, ResourceType::uti);
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
    EXPECT_EQ(ResourceType::from_extension("rml"), ResourceType::rml);
    EXPECT_EQ(ResourceType::from_extension(".rcss"), ResourceType::rcss);
    EXPECT_EQ(ResourceType::from_extension("xxx"), ResourceType::invalid);

    EXPECT_EQ(ResourceType::to_string(ResourceType::txi), "txi"s);
    EXPECT_EQ(ResourceType::to_string(ResourceType::rml), "rml"s);
    EXPECT_EQ(ResourceType::to_string(ResourceType::rcss), "rcss"s);
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

TEST(KernelResources, LoadModuleHaksUsesDefaultUserRoot)
{
    auto rm = new nw::ResourceManager{nwk::global_allocator()};
    nw::Vector<nw::String> haks{"hak_with_description"};
    EXPECT_EQ(rm->load_module_haks(haks), 1);
    rm->build_registry();

    auto data = rm->demand({"build"sv, nw::ResourceType::txt});
    EXPECT_GT(data.bytes.size(), 0);
    delete rm;
}

TEST(KernelResources, LoadModuleHaksUsesSearchRootsInOrder)
{
    const fs::path root = "tmp/resman_hak_roots";
    fs::remove_all(root);
    fs::create_directories(root / "local");
    fs::create_directories(root / "fallback" / "project_dep");
    fs::copy_file("test_data/user/hak/hak_with_description.hak",
        root / "local" / "project_dep.hak",
        fs::copy_options::overwrite_existing);
    {
        std::ofstream out{root / "fallback" / "project_dep" / "build.txt"};
        ASSERT_TRUE(out);
        out << "fallback\n";
    }

    auto rm = new nw::ResourceManager{nwk::global_allocator()};
    nw::Vector<nw::String> haks{"project_dep"};
    nw::Vector<fs::path> roots{root / "local", root / "fallback"};
    EXPECT_EQ(rm->load_module_haks(haks, roots), 1);
    rm->build_registry();

    auto data = rm->demand({"build"sv, nw::ResourceType::txt});
    ASSERT_GT(data.bytes.size(), 0);
    EXPECT_NE(data.bytes.string_view(), "fallback\n");
    delete rm;
}

TEST(KernelResources, LoadModuleHaksAcceptsExtensionAndCaseVariants)
{
    const fs::path root = "tmp/resman_hak_extension_case";
    fs::remove_all(root);
    fs::create_directories(root);
    fs::copy_file("test_data/user/hak/hak_with_description.hak",
        root / "Project_Dep.HAK",
        fs::copy_options::overwrite_existing);

    auto rm = new nw::ResourceManager{nwk::global_allocator()};
    nw::Vector<nw::String> haks{"project_dep.hak"};
    nw::Vector<fs::path> roots{root};
    EXPECT_EQ(rm->load_module_haks(haks, roots), 1);
    rm->build_registry();

    auto data = rm->demand({"build"sv, nw::ResourceType::txt});
    EXPECT_GT(data.bytes.size(), 0);
    delete rm;
}

TEST(KernelResources, LoadModuleHaksAcceptsVersionDotsWithoutExtension)
{
    const fs::path root = "tmp/resman_hak_version_dots";
    fs::remove_all(root);
    fs::create_directories(root);
    fs::copy_file("test_data/user/hak/hak_with_description.hak",
        root / "project_pack_v2.2.hak",
        fs::copy_options::overwrite_existing);

    auto rm = new nw::ResourceManager{nwk::global_allocator()};
    nw::Vector<nw::String> haks{"project_pack_v2.2"};
    nw::Vector<fs::path> roots{root};
    EXPECT_EQ(rm->load_module_haks(haks, roots), 1);
    rm->build_registry();

    auto data = rm->demand({"build"sv, nw::ResourceType::txt});
    EXPECT_GT(data.bytes.size(), 0);
    delete rm;
}

TEST(KernelResources, LoadModuleHaksReportsMissing)
{
    const fs::path root = "tmp/resman_missing_hak_roots";
    fs::remove_all(root);
    fs::create_directories(root);

    auto rm = new nw::ResourceManager{nwk::global_allocator()};
    nw::Vector<nw::String> haks{"does_not_exist"};
    nw::Vector<fs::path> roots{root};
    EXPECT_EQ(rm->load_module_haks(haks, roots), 0);
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
