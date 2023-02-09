#include <gtest/gtest.h>

#include <nw/formats/Dialog.hpp>
#include <nw/serialization/Archives.hpp>

#include <nlohmann/json.hpp>

#include <iostream>

TEST(Dialog, GffDeserialize)
{
    nw::Gff g{"test_data/user/development/alue_ranger.dlg"};
    nw::Dialog dlg{g.toplevel()};
    EXPECT_TRUE(dlg.valid());
    EXPECT_GT(dlg.starts.size(), 0);
    EXPECT_TRUE(dlg.starts[0].is_start);
    EXPECT_EQ(dlg.starts[0].node()->text.get(nw::LanguageID::english),
        "Have you managed to get rid of the Bandit Leader?");
}

TEST(Dialog, JsonSerialize)
{
    nw::Gff g{"test_data/user/development/alue_ranger.dlg"};
    nw::Dialog dlg{g.toplevel()};
    nlohmann::json j = dlg;

    std::ofstream f{"tmp/alue_ranger.dlg.json"};
    f << std::setw(4) << j;
}
