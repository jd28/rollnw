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
    EXPECT_TRUE(dlg.starts[0]->is_start);
    EXPECT_EQ(dlg.starts[0]->node->text.get(nw::LanguageID::english),
        "Have you managed to get rid of the Bandit Leader?");
}

TEST(Dialog, PtrAddRemove)
{
    nw::Gff g{"test_data/user/development/alue_ranger.dlg"};
    nw::Dialog dlg{g.toplevel()};
    EXPECT_TRUE(dlg.valid());
    EXPECT_EQ(dlg.starts[0]->node->pointers.size(), 2);

    auto ptr1 = dlg.starts[0]->add_string("This is a test");
    EXPECT_TRUE(ptr1);
    EXPECT_EQ(dlg.starts[0]->node->pointers.size(), 3);
    EXPECT_EQ(ptr1->type, nw::DialogNodeType::reply);

    dlg.starts[0]->remove_ptr(ptr1);
    EXPECT_EQ(dlg.starts[0]->node->pointers.size(), 2);

    auto ptr2 = dlg.starts[0]->add_ptr(dlg.starts[0]->node->pointers[0], true);
    EXPECT_TRUE(ptr2);
    EXPECT_TRUE(ptr2->is_link);

    EXPECT_EQ(dlg.starts[0]->node->pointers.size(), 3);
    EXPECT_EQ(ptr2->type, nw::DialogNodeType::reply);

    auto ptr3 = ptr2->add();
    EXPECT_EQ(ptr3->type, nw::DialogNodeType::entry);
}

TEST(Dialog, StartsAddRemove)
{
    nw::Gff g{"test_data/user/development/alue_ranger.dlg"};
    nw::Dialog dlg{g.toplevel()};
    EXPECT_TRUE(dlg.valid());
    EXPECT_EQ(dlg.starts.size(), 2);

    auto ptr1 = dlg.add();
    EXPECT_TRUE(ptr1);
    EXPECT_TRUE(ptr1->is_start);
    EXPECT_EQ(dlg.starts.size(), 3);

    auto ptr2 = dlg.add_string("This is a test");
    EXPECT_TRUE(ptr2);
    EXPECT_TRUE(ptr2->is_start);
    EXPECT_EQ(dlg.starts.size(), 4);

    auto ptr3 = dlg.add_ptr(ptr2, true);
    EXPECT_TRUE(ptr3);
    EXPECT_TRUE(ptr3->is_start);
    EXPECT_TRUE(ptr3->is_link);
    EXPECT_EQ(dlg.starts.size(), 5);
}

TEST(Dialog, JsonSerialize)
{
    nw::Gff g{"test_data/user/development/alue_ranger.dlg"};
    nw::Dialog dlg{g.toplevel()};
    nlohmann::json j = dlg;

    std::ofstream f{"tmp/alue_ranger.dlg.json"};
    f << std::setw(4) << j;
}
