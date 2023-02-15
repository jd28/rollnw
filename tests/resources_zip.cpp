#include <gtest/gtest.h>

#include <nw/log.hpp>
#include <nw/resources/Zip.hpp>
#include <nw/util/platform.hpp>

#include <filesystem>
#include <regex>

namespace fs = std::filesystem;
using namespace std::literals;

TEST(Zip, Construction)
{
    nw::Zip z{"test_data/user/modules/module_as_zip.zip"};
    EXPECT_TRUE(z.valid());
    EXPECT_TRUE(z.name() == "module_as_zip.zip");
    EXPECT_TRUE(z.extract(std::regex(".*"), "tmp") == 23);

    EXPECT_THROW(nw::Zip{"test_data/user/development/non-extant.zip"}, std::runtime_error);
}

TEST(Zip, visit)
{
    nw::Zip z{"test_data/user/modules/module_as_zip.zip"};
    EXPECT_TRUE(z.valid());

    size_t count = 0;
    auto visitor = [&count](const nw::Resource&) {
        ++count;
    };

    z.visit(visitor);
    EXPECT_EQ(z.size(), count);
}
