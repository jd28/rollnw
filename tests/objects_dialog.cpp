#include <catch2/catch.hpp>

#include <nw/formats/Gff.hpp>
#include <nw/objects/Dialog.hpp>

#include <iostream>

TEST_CASE("Loading dialog", "[objects]")
{
    nw::Gff g{"test_data/alue_ranger.dlg"};
    nw::Dialog dlg{g.toplevel()};

    REQUIRE(dlg.valid());
    REQUIRE(dlg.starts.size() > 0);
    REQUIRE(dlg.starts[0].is_start);
    REQUIRE(dlg.starts[0].node()->text.get(0) == "Have you managed to get rid of the Bandit Leader?");
}
