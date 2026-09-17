#include "client_runtime.hpp"
#include "loading_view.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/smalls/runtime.hpp>

#include <nw/util/scope_exit.hpp>

#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <optional>
#include <string>

class ClientSdlRuntime : public ::testing::Test {
protected:
    void SetUp() override
    {
        if (SDL_WasInit(0)) { GTEST_SKIP() << "The process already owns SDL subsystems"; }
        isolated = true;
        if (const auto* hint = SDL_GetHint(SDL_HINT_VIDEO_DRIVER)) { previous_driver = hint; }
        ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy"));
    }

    void TearDown() override
    {
        if (!isolated) { return; }
        SDL_Quit();
        if (previous_driver) {
            SDL_SetHint(SDL_HINT_VIDEO_DRIVER, previous_driver->c_str());
        } else {
            SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
        }
    }

    std::optional<std::string> previous_driver;
    bool isolated = false;
};

TEST_F(ClientSdlRuntime, DummyVideoKeepsActualVulkanWindowFailureUntilExplicitShutdown)
{
    nw::toolset::ClientSdlRuntime runtime;
    ASSERT_TRUE(runtime.initialize_video("test-version", "org.rollnw.client.tests"));
    ASSERT_STREQ(SDL_GetCurrentVideoDriver(), "dummy");
    EXPECT_STREQ(SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_VERSION_STRING), "test-version");
    EXPECT_STREQ(SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_IDENTIFIER_STRING), "org.rollnw.client.tests");
    EXPECT_EQ(SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD),
        SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD);
    EXPECT_FALSE(runtime.create_window());
    EXPECT_EQ(runtime.window(), nullptr);
    EXPECT_NE(SDL_WasInit(SDL_INIT_VIDEO), 0);
    runtime.shutdown();
    EXPECT_EQ(SDL_WasInit(0), 0);
    runtime.shutdown();
}

TEST_F(ClientSdlRuntime, InvalidDriverReturnsFailureWithoutAWindow)
{
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "client-sdl-no-such-driver"));
    nw::toolset::ClientSdlRuntime runtime;
    EXPECT_FALSE(runtime.initialize_video("test-version", "org.rollnw.client.tests"));
    EXPECT_EQ(runtime.window(), nullptr);
    runtime.shutdown();
    EXPECT_EQ(SDL_WasInit(SDL_INIT_VIDEO), 0);
}

TEST_F(ClientSdlRuntime, FailedWindowAcquisitionCannotLeaveOwnedVideoLiveAfterDestruction)
{
    {
        nw::toolset::ClientSdlRuntime runtime;
        ASSERT_TRUE(runtime.initialize_video("test-version", "org.rollnw.client.tests"));
        ASSERT_STREQ(SDL_GetCurrentVideoDriver(), "dummy");
        EXPECT_FALSE(runtime.create_window());
    }
    EXPECT_EQ(SDL_WasInit(0), 0);
}

TEST_F(ClientSdlRuntime, InvalidAndRepeatedAcquisitionRejectWithoutChangingLiveOwnership)
{
    nw::toolset::ClientSdlRuntime runtime;
    EXPECT_FALSE(runtime.create_window());
    EXPECT_EQ(SDL_WasInit(0), 0);
    ASSERT_TRUE(runtime.initialize_video("first-version", "org.rollnw.client.tests"));
    EXPECT_FALSE(runtime.initialize_video("other-version", "org.rollnw.client.other"));
    EXPECT_STREQ(SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_VERSION_STRING), "first-version");
    EXPECT_NE(SDL_WasInit(SDL_INIT_VIDEO), 0);
    {
        nw::toolset::ClientSdlRuntime untouched;
        EXPECT_FALSE(untouched.create_window());
    }
    EXPECT_NE(SDL_WasInit(SDL_INIT_VIDEO), 0);
    runtime.shutdown();
    EXPECT_EQ(SDL_WasInit(0), 0);
    EXPECT_FALSE(runtime.create_window());
    // SDL_Quit resets hints; the restart must configure its dummy driver again.
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy"));
    EXPECT_TRUE(runtime.initialize_video("fresh-version", "org.rollnw.client.tests"));
}

TEST_F(ClientSdlRuntime, FailedVideoAcquisitionUnwindsAndAFreshOwnerCanStart)
{
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "client-sdl-no-such-driver"));
    {
        nw::toolset::ClientSdlRuntime runtime;
        EXPECT_FALSE(runtime.initialize_video("test-version", "org.rollnw.client.tests"));
        EXPECT_FALSE(runtime.create_window());
    }
    EXPECT_EQ(SDL_WasInit(0), 0);
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy"));
    {
        nw::toolset::ClientSdlRuntime fresh;
        EXPECT_TRUE(fresh.initialize_video("test-version", "org.rollnw.client.tests"));
    }
    EXPECT_EQ(SDL_WasInit(0), 0);
}

TEST_F(ClientSdlRuntime, KernelAndNativePayloadsCloseBeforeProcessVideo)
{
    auto& config = nw::kernel::config();
    const auto install = config.install_path();
    const auto user = config.user_path();
    const auto options = config.options();
    const auto restore = create_scope_exit([&] {
        nw::kernel::services().shutdown();
        config.set_paths(install, user);
        config.initialize(options);
    });
    nw::kernel::services().shutdown();
    const auto generation = nw::kernel::services().generation();
    {
        nw::toolset::ClientSdlRuntime desktop;
        {
            nw::toolset::ClientKernelRuntime kernel{install, user};
            EXPECT_GT(nw::kernel::services().generation(), generation);
            EXPECT_NE(nw::kernel::services().get<nw::smalls::Runtime>(), nullptr);
            EXPECT_NE(nw::kernel::services().get<nw::ResourceManager>(), nullptr);
            ASSERT_TRUE(desktop.initialize_video("test-version", "org.rollnw.client.tests"));
            nw::toolset::LoadingViewState loading;
            loading.open_module_dialog_event = SDL_RegisterEvents(1);
            ASSERT_NE(loading.open_module_dialog_event, 0);
            const char* canceled_files[]{nullptr};
            auto* request = new nw::toolset::OpenModuleDialogRequest{
                loading.open_module_dialog_event, loading.native_dialog_delivery};
            nw::toolset::open_module_dialog_callback(request, canceled_files, 0);
            EXPECT_EQ(nw::toolset::close_loading_dialog_delivery(loading), 1);
        }
        EXPECT_EQ(nw::kernel::services().get<nw::smalls::Runtime>(), nullptr);
        EXPECT_EQ(nw::kernel::services().get<nw::ResourceManager>(), nullptr);
        EXPECT_NE(SDL_WasInit(SDL_INIT_VIDEO), 0);
    }
    EXPECT_EQ(SDL_WasInit(0), 0);
}
