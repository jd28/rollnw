#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/resources/ResourceManifest.hpp>
#include <nw/smalls/runtime.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using namespace std::literals;

namespace {

void write_text(const fs::path& path, std::string_view contents)
{
    fs::create_directories(path.parent_path());
    std::ofstream output{path};
    output << contents;
}

nw::ResourceManifestContext manifest_context(nw::GameVersion version,
    nw::LanguageID language = nw::LanguageID::english)
{
    return {
        .install_root = "/install",
        .user_root = "test_data/user",
        .version = version,
        .language = language,
    };
}

std::vector<std::string> container_names(
    const nw::Vector<nw::ResourceManifestContainer>& containers)
{
    std::vector<std::string> result;
    result.reserve(containers.size());
    for (const auto& container : containers) {
        result.push_back(container.name);
    }
    return result;
}

} // namespace

TEST(ResourceManifest, EnhancedEditionOrderAndUserPatches)
{
    auto context = manifest_context(
        nw::GameVersion::vEE, nw::LanguageID::french);
    nw::Vector<nw::ResourceManifestContainer> containers;
    nw::String diagnostic;
    ASSERT_TRUE(nw::load_resource_manifest(
        "stdlib/nwn1", context, containers, diagnostic))
        << diagnostic;

    const std::vector<std::string> expected{
        "development",
        "portraits",
        "prt",
        "override",
        "ovr",
        "ambient",
        "music",
        "amb",
        "mus",
        "hak_with_description",
        "nwn_base_loc",
        "nwn_retail",
        "nwn_base",
    };
    EXPECT_EQ(container_names(containers), expected);
    ASSERT_EQ(containers.size(), expected.size());
    EXPECT_EQ(containers[0].precedence,
        nw::ResourceManifestPrecedence::override);
    EXPECT_EQ(containers[1].resource_type, nw::ResourceType::texture);
    EXPECT_EQ(containers[6].resource_type, nw::ResourceType::sound);
    EXPECT_EQ(containers[9].directory,
        fs::path{"test_data/user"} / "patch");
    EXPECT_EQ(containers[10].directory,
        fs::path{"/install/lang/fr/data"});
}

TEST(ResourceManifest, ServicePreflightSelectsOnePackageDirectory)
{
    EXPECT_EQ(nw::kernel::runtime().selected_package_directory(),
        fs::canonical("stdlib/nwn1"));
}

TEST(ResourceManifest, LegacyOrder)
{
    auto context = manifest_context(nw::GameVersion::v1_69);
    nw::Vector<nw::ResourceManifestContainer> containers;
    nw::String diagnostic;
    ASSERT_TRUE(nw::load_resource_manifest(
        "stdlib/nwn1", context, containers, diagnostic))
        << diagnostic;

    const std::vector<std::string> expected{
        "portraits",
        "prt",
        "override",
        "ovr",
        "ambient",
        "music",
        "amb",
        "mus",
        "hak_with_description",
        "xp2_tex_tpa",
        "xp1_tex_tpa",
        "textures_tpa",
        "tiles_tpa",
        "xp3patch",
        "xp3",
        "xp2patch",
        "xp2",
        "xp1patch",
        "xp1",
        "patch",
        "chitin",
    };
    EXPECT_EQ(container_names(containers), expected);
}

TEST(ResourceManifest, LanguageAndIncludeFlags)
{
    nw::Vector<nw::ResourceManifestContainer> containers;
    nw::String diagnostic;

    auto context = manifest_context(nw::GameVersion::vEE);
    ASSERT_TRUE(nw::load_resource_manifest(
        "stdlib/nwn1", context, containers, diagnostic));
    EXPECT_EQ(containers.size(), 12);
    const auto english_names = container_names(containers);
    EXPECT_EQ(std::ranges::find(english_names, "nwn_base_loc"),
        english_names.end());

    context = manifest_context(
        nw::GameVersion::vEE, nw::LanguageID::french);
    context.include_user = false;
    ASSERT_TRUE(nw::load_resource_manifest(
        "stdlib/nwn1", context, containers, diagnostic));
    EXPECT_EQ(container_names(containers),
        (std::vector<std::string>{
            "prt", "ovr", "amb", "mus", "nwn_base_loc",
            "nwn_retail", "nwn_base"}));

    context.include_user = true;
    context.include_install = false;
    ASSERT_TRUE(nw::load_resource_manifest(
        "stdlib/nwn1", context, containers, diagnostic));
    EXPECT_EQ(container_names(containers),
        (std::vector<std::string>{
            "development", "portraits", "override", "ambient", "music",
            "hak_with_description"}));

    context.include_user = false;
    ASSERT_TRUE(nw::load_resource_manifest(
        "stdlib/nwn1", context, containers, diagnostic));
    EXPECT_TRUE(containers.empty());
}

TEST(ResourceManifest, IniSeriesStopsAtFirstEmptyValue)
{
    const fs::path root{"tmp/resource_manifest_ini"};
    write_text(root / "package/resources.json", R"([
      {
        "kind": "ini_series",
        "root": "user",
        "file": "userpatch.ini",
        "section": "Patch",
        "key_prefix": "PatchFile",
        "digits": 3,
        "container_directory": "patch",
        "precedence": "base"
      }
    ])");
    write_text(root / "user/userpatch.ini", R"([Patch]
PatchFile000=first
PatchFile001=
PatchFile002=not_loaded
)");

    auto context = manifest_context(nw::GameVersion::vEE);
    context.user_root = root / "user";
    context.include_install = false;
    nw::Vector<nw::ResourceManifestContainer> containers;
    nw::String diagnostic;
    ASSERT_TRUE(nw::load_resource_manifest(
        root / "package", context, containers, diagnostic))
        << diagnostic;
    ASSERT_EQ(containers.size(), 1);
    EXPECT_EQ(containers[0].name, "first");
}

TEST(ResourceManifest, RejectsMalformedRowsBeforePublication)
{
    const fs::path package{"tmp/resource_manifest_invalid/package"};
    const auto context = manifest_context(nw::GameVersion::vEE);
    nw::Vector<nw::ResourceManifestContainer> containers;
    nw::String diagnostic;
    const std::array malformed{
        R"({})"sv,
        R"([{"kind":"unknown"}])"sv,
        R"([{"kind":"container","root":"user","directory":"../escape","name":"bad","precedence":"base","resource_type":"any"}])"sv,
        R"([{"kind":"container","root":"install","directory":"/absolute","name":"bad","precedence":"base","resource_type":"any"}])"sv,
        R"([{"kind":"container","version":"future","root":"user","directory":"","name":"bad","precedence":"base","resource_type":"any"}])"sv,
        R"([{"kind":"container","root":"user","directory":"","name":"bad","precedence":"middle","resource_type":"any"}])"sv,
        R"([{"kind":"container","root":"user","directory":"","name":"bad","precedence":"base","resource_type":"unknown"}])"sv,
        R"([{"kind":"container","root":"user","directory":"","name":"bad","precedence":"base","resource_type":"any","extra":1}])"sv,
        R"([{"kind":"container","root":"user","directory":"","name":"same","precedence":"base","resource_type":"any"},{"kind":"container","root":"user","directory":"","name":"same","precedence":"base","resource_type":"any"}])"sv,
        R"([{"kind":"ini_series","root":"user","file":"a.ini","section":"P","key_prefix":"K","digits":3,"container_directory":"patch","precedence":"base"},{"kind":"ini_series","root":"user","file":"b.ini","section":"P","key_prefix":"K","digits":3,"container_directory":"patch","precedence":"base"}])"sv,
    };

    for (const auto document : malformed) {
        write_text(package / "resources.json", document);
        containers.push_back({.name = "sentinel"});
        EXPECT_FALSE(nw::load_resource_manifest(
            package, context, containers, diagnostic));
        EXPECT_TRUE(containers.empty());
        EXPECT_FALSE(diagnostic.empty());
    }
}

TEST(ResourceManifest, MissingManifestAndContainersAreExplicit)
{
    const fs::path root{"tmp/resource_manifest_missing"};
    auto context = manifest_context(nw::GameVersion::vEE);
    context.install_root = root / "install";
    context.user_root = root / "user";
    nw::Vector<nw::ResourceManifestContainer> containers;
    nw::String diagnostic;
    EXPECT_FALSE(nw::load_resource_manifest(
        root / "no_package", context, containers, diagnostic));

    write_text(root / "package/resources.json", R"([
      {
        "kind": "container",
        "root": "install",
        "directory": "data",
        "name": "missing",
        "precedence": "base",
        "resource_type": "any"
      }
    ])");
    ASSERT_TRUE(nw::load_resource_manifest(
        root / "package", context, containers, diagnostic))
        << diagnostic;
    ASSERT_EQ(containers.size(), 1);

    nw::ResourceManager resources{nw::kernel::global_allocator()};
    EXPECT_FALSE(resources.add_base_container(containers[0].directory,
        containers[0].name, containers[0].resource_type));
}
