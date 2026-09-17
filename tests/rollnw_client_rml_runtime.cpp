#include "client_rml_runtime.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/util/scope_exit.hpp>

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace {

class NullRenderInterface final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override { return ++compiled; }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override { }
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override { ++released; }
    Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override { return 0; }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return 0; }
    void ReleaseTexture(Rml::TextureHandle) override { }
    void EnableScissorRegion(bool) override { }
    void SetScissorRegion(Rml::Rectanglei) override { }
    size_t compiled = 0;
    size_t released = 0;
};

} // namespace

class ClientRmlRuntime : public ::testing::Test {
protected:
    void SetUp() override
    {
        directory = std::filesystem::path{"tmp/client_rml_runtime"}
            / ::testing::UnitTest::GetInstance()->current_test_info()->name();
        std::filesystem::remove_all(directory);
        std::filesystem::create_directories(directory / "fonts");
        for (const auto* filename : {"Inter-Regular.ttf", "Inter-Medium.ttf", "Inter-SemiBold.ttf", "Inter-Bold.ttf"}) {
            std::filesystem::copy_file(source / "tools/client/assets/fonts/inter" / filename, directory / "fonts" / filename);
        }
        std::filesystem::copy_file(source / "external/imgui-1.92.7/misc/fonts/Cousine-Regular.ttf",
            directory / "fonts/Cousine-Regular.ttf");
    }

    void TearDown() override { std::filesystem::remove_all(directory); }

    const std::filesystem::path source{ROLLNW_TEST_SOURCE_DIR};
    std::filesystem::path directory;
    NullRenderInterface renderer;
    Rml::SystemInterface system;
};

TEST_F(ClientRmlRuntime, OwnsActualPackageFontsAndThreeContextRolesUntilExplicitShutdown)
{
    nw::toolset::ClientRmlRuntime runtime{source / "tools/client/ui", nw::kernel::resman()};
    ASSERT_TRUE(runtime.initialize(system, renderer, directory / "fonts", {1200, 700}));
    const auto& contexts = runtime.contexts();
    ASSERT_NE(contexts.toolset, nullptr);
    EXPECT_EQ(Rml::GetNumContexts(), 1);
    contexts.toolset->SetDensityIndependentPixelRatio(1.25f);
    ASSERT_TRUE(runtime.create_overlay_contexts({1200, 700}));
    ASSERT_NE(contexts.fps, nullptr);
    ASSERT_NE(contexts.palette, nullptr);
    EXPECT_EQ(Rml::GetNumContexts(), 3);
    EXPECT_EQ(contexts.toolset, Rml::GetContext("toolset"));
    EXPECT_EQ(contexts.fps, Rml::GetContext("viewer_fps"));
    EXPECT_EQ(contexts.palette, Rml::GetContext("command_palette"));
    EXPECT_FLOAT_EQ(contexts.fps->GetDensityIndependentPixelRatio(), 1.25f);
    EXPECT_FLOAT_EQ(contexts.palette->GetDensityIndependentPixelRatio(), 1.25f);
    auto* panel = runtime.load_document(*contexts.toolset, {nw::Resref{"ui/panel"}, nw::ResourceType::rml});
    ASSERT_NE(panel, nullptr);
    panel->Show();
    contexts.toolset->Update();
    contexts.toolset->Render();
    auto* modals = runtime.load_document(*contexts.palette, {nw::Resref{"ui/command_modals"}, nw::ResourceType::rml});
    ASSERT_NE(modals, nullptr);
    EXPECT_EQ(runtime.load_document(*contexts.toolset, {nw::Resref{"ui/missing"}, nw::ResourceType::rml}), nullptr);
    runtime.release_render_resources();
    runtime.shutdown();
    EXPECT_EQ(contexts.toolset, nullptr);
    EXPECT_EQ(contexts.fps, nullptr);
    EXPECT_EQ(contexts.palette, nullptr);
    EXPECT_EQ(Rml::GetRenderInterface(), nullptr);
    EXPECT_EQ(Rml::GetFileInterface(), nullptr);
    EXPECT_EQ(Rml::GetSystemInterface(), nullptr);
    runtime.shutdown();
}

TEST_F(ClientRmlRuntime, FailedFontAcquisitionCannotLeaveSdkInterfacesBorrowingADeadOwner)
{
    // Before the fix the missing-font path initializes Rml but leaves no font
    // buffers or contexts. This fallback keeps that failing characterization
    // isolated from later suites without using the expired file interface.
    const auto fallback = create_scope_exit([] {
        if (Rml::GetFileInterface()) { Rml::Shutdown(); }
    });
    {
        nw::toolset::ClientRmlRuntime runtime{source / "tools/client/ui", nw::kernel::resman()};
        EXPECT_FALSE(runtime.initialize(system, renderer, directory / "missing-fonts", {1200, 700}));
    }
    EXPECT_EQ(Rml::GetFileInterface(), nullptr);
    EXPECT_EQ(Rml::GetRenderInterface(), nullptr);
    EXPECT_EQ(Rml::GetSystemInterface(), nullptr);
}

TEST_F(ClientRmlRuntime, EveryMissingFontAndAnEmptyFileUnwindOtherLiveFontBuffers)
{
    for (const auto* filename : {"Inter-Regular.ttf", "Inter-Medium.ttf", "Inter-SemiBold.ttf", "Inter-Bold.ttf", "Cousine-Regular.ttf"}) {
        SCOPED_TRACE(filename);
        const auto path = directory / "fonts" / filename;
        const auto backup = directory / filename;
        std::filesystem::rename(path, backup);
        const auto restore = create_scope_exit([&] { std::filesystem::rename(backup, path); });
        {
            nw::toolset::ClientRmlRuntime runtime{source / "tools/client/ui", nw::kernel::resman()};
            EXPECT_FALSE(runtime.initialize(system, renderer, directory / "fonts", {1200, 700}));
            EXPECT_NE(Rml::GetFileInterface(), nullptr);
        }
        EXPECT_EQ(Rml::GetFileInterface(), nullptr);
        EXPECT_EQ(Rml::GetFontEngineInterface(), nullptr);
    }
    {
        std::ofstream empty{directory / "fonts/Inter-Regular.ttf", std::ios::binary | std::ios::trunc};
    }
    {
        nw::toolset::ClientRmlRuntime runtime{source / "tools/client/ui", nw::kernel::resman()};
        EXPECT_FALSE(runtime.initialize(system, renderer, directory / "fonts", {1200, 700}));
    }
    EXPECT_EQ(Rml::GetFileInterface(), nullptr);
}

TEST_F(ClientRmlRuntime, InvalidPackagesAndDimensionsDoNotAcquireTheSdkSingleton)
{
    std::filesystem::create_directories(directory / "incomplete-ui");
    std::filesystem::copy_file(source / "tools/client/ui/panel.rml", directory / "incomplete-ui/panel.rml");
    for (const auto& path : {directory / "incomplete-ui", directory / "missing-ui"}) {
        SCOPED_TRACE(path.string());
        nw::toolset::ClientRmlRuntime runtime{path, nw::kernel::resman()};
        EXPECT_FALSE(runtime.initialize(system, renderer, directory / "fonts", {1200, 700}));
        EXPECT_EQ(Rml::GetFileInterface(), nullptr);
        EXPECT_EQ(Rml::GetRenderInterface(), nullptr);
    }
    nw::toolset::ClientRmlRuntime runtime{source / "tools/client/ui", nw::kernel::resman()};
    EXPECT_FALSE(runtime.create_overlay_contexts({1200, 700}));
    EXPECT_FALSE(runtime.initialize(system, renderer, directory / "fonts", {0, 700}));
    EXPECT_FALSE(runtime.initialize(system, renderer, directory / "fonts", {1200, -1}));
    EXPECT_EQ(Rml::GetFileInterface(), nullptr);
}

TEST_F(ClientRmlRuntime, PrimaryAndPartiallyCreatedOverlaysUnwindAutomatically)
{
    for (const auto* conflicting_name : {"", "viewer_fps", "command_palette"}) {
        SCOPED_TRACE(conflicting_name);
        {
            nw::toolset::ClientRmlRuntime runtime{source / "tools/client/ui", nw::kernel::resman()};
            ASSERT_TRUE(runtime.initialize(system, renderer, directory / "fonts", {1200, 700}));
            if (conflicting_name[0]) {
                ASSERT_NE(Rml::CreateContext(conflicting_name, {1200, 700}), nullptr);
                EXPECT_FALSE(runtime.create_overlay_contexts({1200, 700}));
                EXPECT_EQ(Rml::GetNumContexts(), conflicting_name == std::string_view{"viewer_fps"} ? 2 : 3);
            }
        }
        EXPECT_EQ(Rml::GetFileInterface(), nullptr);
        EXPECT_EQ(Rml::GetRenderInterface(), nullptr);
        EXPECT_EQ(Rml::GetSystemInterface(), nullptr);
        // A fresh owner must be able to acquire the same SDK/context identities.
        nw::toolset::ClientRmlRuntime fresh{source / "tools/client/ui", nw::kernel::resman()};
        ASSERT_TRUE(fresh.initialize(system, renderer, directory / "fonts", {1200, 700}));
        EXPECT_TRUE(fresh.create_overlay_contexts({1200, 700}));
    }
}

TEST_F(ClientRmlRuntime, AutomaticShutdownReleasesRenderedGeometryAndRejectsRepeatedAcquisition)
{
    {
        nw::toolset::ClientRmlRuntime runtime{source / "tools/client/ui", nw::kernel::resman()};
        ASSERT_TRUE(runtime.initialize(system, renderer, directory / "fonts", {1200, 700}));
        auto* primary = runtime.contexts().toolset;
        EXPECT_FALSE(runtime.initialize(system, renderer, directory / "fonts", {1200, 700}));
        EXPECT_EQ(runtime.contexts().toolset, primary);
        EXPECT_FALSE(runtime.create_overlay_contexts({0, 700}));
        ASSERT_TRUE(runtime.create_overlay_contexts({1200, 700}));
        EXPECT_FALSE(runtime.create_overlay_contexts({1200, 700}));
        EXPECT_EQ(Rml::GetNumContexts(), 3);
        auto* document = runtime.load_document(*primary, {nw::Resref{"ui/panel"}, nw::ResourceType::rml});
        ASSERT_NE(document, nullptr);
        document->GetElementById("workspace_content")->SetInnerRML("<p>Automatic font and geometry lifetime</p>");
        document->Show();
        primary->Update();
        primary->Render();
        EXPECT_GT(renderer.compiled, 0);
    }
    EXPECT_EQ(renderer.released, renderer.compiled);
    EXPECT_EQ(Rml::GetFontEngineInterface(), nullptr);
    EXPECT_EQ(Rml::GetFileInterface(), nullptr);
}
