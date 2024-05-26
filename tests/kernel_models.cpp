#include <gtest/gtest.h>

#include <nw/kernel/ModelCache.hpp>
#include <nw/log.hpp>

namespace nwk = nw::kernel;

TEST(KernelModels, Load)
{
    auto* model1 = nwk::models().load("c_orcus");
    EXPECT_TRUE(model1);
    auto* model2 = nwk::models().load("c_orcus");
    EXPECT_EQ(model1, model2);

    // [NOTE] There are two references to Orcus above.
    nwk::models().release("c_orcus");
    EXPECT_EQ(nwk::models().map_.size(), 1);
    nwk::models().release("c_orcus");
    EXPECT_EQ(nwk::models().map_.size(), 0);
}
