#include <catch2/catch_all.hpp>

#include <nw/formats/Dialog.hpp>
#include <nw/serialization/Archives.hpp>

#include <nlohmann/json.hpp>

#include <iostream>

TEST_CASE("dialog: from gff", "[objects]")
{
    nw::Gff g{"test_data/user/development/alue_ranger.dlg"};
    nw::Dialog dlg{g.toplevel()};

    REQUIRE(dlg.valid());
    REQUIRE(dlg.starts.size() > 0);
    REQUIRE(dlg.starts[0].is_start);
    REQUIRE(dlg.starts[0].node()->text.get(nw::LanguageID::english) == "Have you managed to get rid of the Bandit Leader?");
}

TEST_CASE("dialog: to json", "[objects]")
{
    nw::Gff g{"test_data/user/development/alue_ranger.dlg"};
    nw::Dialog dlg{g.toplevel()};
    nlohmann::json j = dlg;

    std::ofstream f{"tmp/alue_ranger.dlg.json"};
    f << std::setw(4) << j;
}
