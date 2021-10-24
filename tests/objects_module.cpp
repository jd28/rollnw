#include <catch2/catch.hpp>

#include <nw/formats/Gff.hpp>
#include <nw/objects/Module.hpp>

#include <iostream>

TEST_CASE("Loading module-ish", "[objects]")
{
    nw::Gff g{"test_data/module.ifo"};
    REQUIRE(g.valid());
    nw::Module m{g.toplevel()};
    REQUIRE(m.haks.size() == 0);
    REQUIRE(!m.is_save_game);
    REQUIRE(m.name.get(0) == "test_module");
}
