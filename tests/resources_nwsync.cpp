#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/resources/NWSync.hpp>
#include <nw/resources/ResourceType.hpp>

namespace fs = std::filesystem;
using namespace std::literals;

TEST(NWSync, Construction)
{
    auto path = nw::kernel::config().alias_path(nw::PathAlias::nwsync);
    auto n = nw::NWSync(path);
    EXPECT_TRUE(n.is_loaded());
    EXPECT_TRUE(n.shard_count() == 1);

    auto manifests = n.manifests();
    if (manifests.size() > 0) {
        auto manifest = n.get(manifests[0]);
        auto resource = manifest->all();
        EXPECT_TRUE(resource.size() > 0);

        EXPECT_TRUE(manifest->contains(resource[0].name));
        auto data = manifest->demand(resource[0].name);
        EXPECT_TRUE(data.bytes.size());
        EXPECT_TRUE(manifest->extract(std::regex(resource[0].name.filename()), "tmp/"));

        auto rd = manifest->stat(resource[0].name);
        EXPECT_TRUE(rd.mtime == 1648999682);
    }
}
