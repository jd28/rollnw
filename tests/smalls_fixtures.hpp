#include "nw/config.hpp"
#include "nw/kernel/Kernel.hpp"
#include "nw/smalls/Context.hpp"
#include "nw/smalls/Smalls.hpp"
#include "nw/smalls/runtime.hpp"

#include <gtest/gtest.h>
#include <memory>
#include <string_view>

inline nw::smalls::Script make_script(std::string_view source)
{
    return nw::smalls::Script("test", source, nw::kernel::runtime().diagnostic_context());
}

class SmallsLexer : public ::testing::Test {
protected:
    void SetUp() override
    {
        nw::kernel::services().start();
    }

    void TearDown() override
    {
        nw::kernel::services().shutdown();
    }
};

class SmallsLSP : public ::testing::Test {
protected:
    void SetUp() override
    {
        nw::kernel::services().start();
        auto config = nw::kernel::runtime().diagnostic_config();
        config.debug_level = nw::smalls::DebugLevel::full;
        nw::kernel::runtime().set_diagnostic_config(config);
    }

    void TearDown() override
    {
        nw::kernel::services().shutdown();
    }
};

class SmallsParser : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        nw::kernel::services().start();
    }

    static void TearDownTestSuite()
    {
        nw::kernel::services().shutdown();
    }
};

class SmallsResolver : public ::testing::Test {
protected:
    void SetUp() override
    {
        nw::kernel::services().start();
        nw::kernel::runtime().add_module_path(std::filesystem::path("stdlib/core"));
    }

    void TearDown() override
    {
        nw::kernel::services().shutdown();
    }
};
