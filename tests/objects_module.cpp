#include <catch2/catch.hpp>

#include <nw/objects/Module.hpp>
#include <nw/serialization/Serialization.hpp>

#include <nlohmann/json.hpp>

#include <fstream>

TEST_CASE("module: from_gff", "[objects]")
{
    nw::GffInputArchive g{"test_data/module.ifo"};
    REQUIRE(g.valid());
    nw::Module m{g.toplevel()};
    REQUIRE(m.haks.size() == 0);
    REQUIRE(!m.is_save_game);
    REQUIRE(m.name.get(0) == "test_module");
    REQUIRE(m.scripts.on_load == "x2_mod_def_load");
}

TEST_CASE("module: to_json", "[objects]")
{
    nw::GffInputArchive g{"test_data/module.ifo"};
    REQUIRE(g.valid());
    nw::Module m{g.toplevel()};

    nlohmann::json j = m.to_json();

    std::ofstream f{"tmp/module.ifo.json"};
    f << std::setw(4) << j;
}

TEST_CASE("module: json to and from", "[objects]")
{
    nw::GffInputArchive g{"test_data/module.ifo"};
    REQUIRE(g.valid());
    nw::Module m{g.toplevel()};
    nlohmann::json j = m.to_json();
    nw::Module m2{j};
    nlohmann::json j2 = m2.to_json();
    REQUIRE(j == j2);
}
