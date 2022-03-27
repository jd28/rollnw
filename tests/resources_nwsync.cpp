#include <catch2/catch.hpp>

#include <nw/log.hpp>
#include <nw/resources/NWSync.hpp>
#include <nw/resources/ResourceType.hpp>
#include <nw/util/game_install.hpp>

namespace fs = std::filesystem;
using namespace std::literals;

TEST_CASE("nwsync", "[containers]")
{
    nw::InstallInfo paths;
    try {
        paths = nw::probe_nwn_install();
    } catch (...) {
    }

    if (fs::exists(paths.user / "nwsync/nwsyncmeta.sqlite3")) {
        auto n = nw::NWSync(paths.user / "nwsync/");
        REQUIRE(n.is_loaded());
        auto manifests = n.manifests();
        if (manifests.size() > 0) {
            auto manifest = n.get(manifests[0]);
            auto resource = manifest.all();
            REQUIRE(resource.size() > 0);

            auto ba = manifest.demand(resource[0].name);
            REQUIRE(ba.size());
            REQUIRE(manifest.extract(std::regex(resource[0].name.filename()), "tmp/"));
        }
    }
}
