#include "client_rml_file_interface.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/resources/StaticDirectory.hpp>
#include <nw/util/scope_exit.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

namespace {

const auto source_root = std::filesystem::path{__FILE__}.parent_path().parent_path();

std::string file_bytes(const std::filesystem::path& path)
{
    std::ifstream stream{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

} // namespace

TEST(ClientRmlFiles, ReadsTheActualUiPackageAcrossItsUrlForms)
{
    nw::StaticDirectory package{source_root / "tools/client/ui"};
    ASSERT_TRUE(package.valid());
    nw::ResourceManager ui{nw::kernel::global_allocator()};
    nw::ResourceManager game{nw::kernel::global_allocator()};
    ASSERT_TRUE(ui.add_custom_container(&package, false));
    ui.build_registry();
    ASSERT_TRUE(ui.contains(nw::Resource{nw::Resref{"ui/panel"}, nw::ResourceType::rml}));
    nw::toolset::ClientRmlFileInterface files{ui, game};
    const auto expected = file_bytes(source_root / "tools/client/ui/panel.rml");
    ASSERT_FALSE(expected.empty());
    for (const auto* path : {"ui/panel.rml", "panel.rml", "ui://panel.rml?version=1", "/ui/panel.rml?version=1", "ui\\panel.rml"}) {
        SCOPED_TRACE(path);
        const auto handle = files.Open(path);
        ASSERT_NE(handle, 0);
        const auto close = create_scope_exit([&] { files.Close(handle); });
        EXPECT_EQ(files.Length(handle), expected.size());
        EXPECT_EQ(files.Tell(handle), 0);
        std::string result(expected.size() + 3, '\0');
        EXPECT_EQ(files.Read(result.data(), result.size(), handle), expected.size());
        result.resize(expected.size());
        EXPECT_EQ(result, expected);
        EXPECT_EQ(files.Tell(handle), expected.size());
        EXPECT_EQ(files.Read(result.data(), 1, handle), 0);
    }
}

TEST(ClientRmlFiles, OwnedResourceBytesSurviveRegistryReplacementAndKeepMemorySeekBounds)
{
    nw::StaticDirectory package{source_root / "tools/client/ui"};
    nw::ResourceManager ui{nw::kernel::global_allocator()};
    nw::ResourceManager game{nw::kernel::global_allocator()};
    ASSERT_TRUE(ui.add_custom_container(&package, false));
    ui.build_registry();
    nw::toolset::ClientRmlFileInterface files{ui, game};
    const auto expected = file_bytes(source_root / "tools/client/ui/panel.rcss");
    ASSERT_GT(expected.size(), 3);
    const auto handle = files.Open("ui/panel.rcss");
    ASSERT_NE(handle, 0);
    const auto close = create_scope_exit([&] { files.Close(handle); });
    const auto generation = ui.generation();
    ui.unfreeze();
    EXPECT_NE(ui.generation(), generation);
    EXPECT_FALSE(ui.is_frozen());
    ASSERT_TRUE(files.Seek(handle, 2, SEEK_SET));
    EXPECT_EQ(files.Tell(handle), 2);
    ASSERT_TRUE(files.Seek(handle, -1, SEEK_CUR));
    EXPECT_EQ(files.Tell(handle), 1);
    EXPECT_FALSE(files.Seek(handle, -2, SEEK_CUR));
    EXPECT_EQ(files.Tell(handle), 1);
    EXPECT_FALSE(files.Seek(handle, 0, 12345));
    EXPECT_EQ(files.Tell(handle), 1);
    ASSERT_TRUE(files.Seek(handle, -2, SEEK_END));
    EXPECT_EQ(files.Tell(handle), expected.size() - 2);
    EXPECT_FALSE(files.Seek(handle, 1, SEEK_END));
    EXPECT_EQ(files.Tell(handle), expected.size() - 2);
    std::array<char, 3> tail{};
    EXPECT_EQ(files.Read(tail.data(), tail.size(), handle), 2);
    EXPECT_EQ(std::string(tail.data(), 2), expected.substr(expected.size() - 2));
    ASSERT_TRUE(files.Seek(handle, 0, SEEK_SET));
    std::string result(expected.size(), '\0');
    EXPECT_EQ(files.Read(nullptr, result.size(), handle), 0);
    EXPECT_EQ(files.Read(result.data(), 0, handle), 0);
    EXPECT_EQ(files.Tell(handle), 0);
    EXPECT_EQ(files.Read(result.data(), result.size(), handle), result.size());
    EXPECT_EQ(result, expected);
}

TEST(ClientRmlFiles, GameTgaFallbackReadsTheExistingBinaryFixture)
{
    const auto directory = source_root / "tests/test_data/renderer/nwn_dump_user/development/tga";
    nw::StaticDirectory textures{directory};
    ASSERT_TRUE(textures.valid());
    nw::ResourceManager ui{nw::kernel::global_allocator()};
    nw::ResourceManager game{nw::kernel::global_allocator()};
    ASSERT_TRUE(game.add_custom_container(&textures, false));
    game.build_registry();
    nw::toolset::ClientRmlFileInterface files{ui, game};
    const auto expected = file_bytes(directory / "black.tga");
    ASSERT_FALSE(expected.empty());
    const auto handle = files.Open("ui/black.tga");
    ASSERT_NE(handle, 0);
    const auto close = create_scope_exit([&] { files.Close(handle); });
    EXPECT_EQ(files.Length(handle), expected.size());
    std::string result(expected.size(), '\0');
    EXPECT_EQ(files.Read(result.data(), result.size(), handle), result.size());
    EXPECT_EQ(result, expected);
    EXPECT_EQ(files.Open("ui/black.png"), 0);
}

TEST(ClientRmlFiles, FilesystemAndFileProtocolPreservePositionsAndExplicitMissingResults)
{
    nw::ResourceManager ui{nw::kernel::global_allocator()};
    nw::ResourceManager game{nw::kernel::global_allocator()};
    nw::toolset::ClientRmlFileInterface files{ui, game};
    const auto path = source_root / "tools/client/ui/panel.rml";
    const auto expected = file_bytes(path);
    ASSERT_GT(expected.size(), 3);
    for (const auto& url : {path.string(), "file://" + path.generic_string()}) {
        SCOPED_TRACE(url);
        const auto handle = files.Open(url);
        ASSERT_NE(handle, 0);
        const auto close = create_scope_exit([&] { files.Close(handle); });
        ASSERT_TRUE(files.Seek(handle, 2, SEEK_SET));
        EXPECT_EQ(files.Length(handle), expected.size());
        EXPECT_EQ(files.Tell(handle), 2);
        std::array<char, 2> result{};
        EXPECT_EQ(files.Read(result.data(), result.size(), handle), result.size());
        EXPECT_EQ(std::string(result.data(), result.size()), expected.substr(2, 2));
        EXPECT_EQ(files.Tell(handle), 4);
    }
    EXPECT_EQ(files.Open((source_root / "missing-client-file.rml").string()), 0);
    std::array<char, 2> result{};
    EXPECT_EQ(files.Read(result.data(), result.size(), 0), 0);
    EXPECT_EQ(files.Tell(0), 0);
    EXPECT_EQ(files.Length(0), 0);
    EXPECT_FALSE(files.Seek(0, 0, SEEK_SET));
    files.Close(0);
}

TEST(ClientRmlFiles, ExtremeMemorySeekOffsetsRejectWithoutChangingTheCursor)
{
    nw::StaticDirectory package{source_root / "tools/client/ui"};
    nw::ResourceManager ui{nw::kernel::global_allocator()};
    nw::ResourceManager game{nw::kernel::global_allocator()};
    ASSERT_TRUE(ui.add_custom_container(&package, false));
    ui.build_registry();
    nw::toolset::ClientRmlFileInterface files{ui, game};
    const auto handle = files.Open("ui/panel.rcss");
    ASSERT_NE(handle, 0);
    const auto close = create_scope_exit([&] { files.Close(handle); });
    ASSERT_TRUE(files.Seek(handle, 1, SEEK_SET));
    for (const auto origin : {SEEK_CUR, SEEK_END, SEEK_SET}) {
        SCOPED_TRACE(origin);
        EXPECT_FALSE(files.Seek(handle, std::numeric_limits<long>::max(), origin));
        EXPECT_EQ(files.Tell(handle), 1);
        EXPECT_FALSE(files.Seek(handle, std::numeric_limits<long>::min(), origin));
        EXPECT_EQ(files.Tell(handle), 1);
    }
    std::array<char, 2> result{};
    EXPECT_EQ(files.Read(result.data(), result.size(), handle), result.size());
    EXPECT_EQ(std::string(result.data(), result.size()),
        file_bytes(source_root / "tools/client/ui/panel.rcss").substr(1, 2));
}
