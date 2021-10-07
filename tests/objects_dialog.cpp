#include <catch2/catch.hpp>

#include <nw/formats/Gff.hpp>
#include <nw/objects/Dialog.hpp>

#include <iostream>

TEST_CASE("Loading dialog", "[objects]")
{
    nw::Gff g{"test_data/nr_binx3_convo.dlg"};
    nw::Dialog dlg{g.toplevel()};

    REQUIRE(dlg.starts.size() > 0);
    REQUIRE(dlg.starts[0].is_start);
    REQUIRE(dlg.starts[0].node()->text.get(0) == "You did it!");
    std::cout << dlg;
}
