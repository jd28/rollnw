#include <gtest/gtest.h>

#include <nw/formats/Dialog.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/serialization/Gff.hpp>
#include <nw/serialization/GffBuilder.hpp>
#include <nw/serialization/gff_conversion.hpp>

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
    nlohmann::json j;
    nw::serialize(j, dlg);

    std::ofstream f{"tmp/alue_ranger.dlg.json"};
    f << std::setw(4) << j;
}

TEST(Dialog, JsonRoundTrip)
{
    nw::Gff g{"test_data/user/development/alue_ranger.dlg"};
    nw::Dialog dlg{g.toplevel()};
    nlohmann::json j;
    nw::serialize(j, dlg);

    nw::Dialog dlg2(j);
    EXPECT_TRUE(dlg2.valid());
    EXPECT_EQ(dlg.entries.size(), dlg2.entries.size());

    nlohmann::json j2;
    nw::serialize(j2, dlg2);

    EXPECT_EQ(j, j2);
}

TEST(Dialog, GffRoundTrip)
{
    nw::Gff g("test_data/user/development/alue_ranger.dlg");
    EXPECT_TRUE(g.valid());
    nw::Dialog dlg{g.toplevel()};

    nw::GffBuilder oa = nw::serialize(&dlg);
    oa.write_to("tmp/alue_ranger.dlg");

    nw::Gff g2("tmp/alue_ranger.dlg");
    EXPECT_TRUE(g2.valid());
    EXPECT_EQ(nw::gff_to_gffjson(g), nw::gff_to_gffjson(g2));

    EXPECT_EQ(oa.header.struct_offset, g.head_->struct_offset);
    EXPECT_EQ(oa.header.struct_count, g.head_->struct_count);
    EXPECT_EQ(oa.header.field_offset, g.head_->field_offset);
    EXPECT_EQ(oa.header.field_count, g.head_->field_count);
    EXPECT_EQ(oa.header.label_offset, g.head_->label_offset);
    EXPECT_EQ(oa.header.label_count, g.head_->label_count);
    EXPECT_EQ(oa.header.field_data_offset, g.head_->field_data_offset);
    EXPECT_EQ(oa.header.field_data_count, g.head_->field_data_count);
    EXPECT_EQ(oa.header.field_idx_offset, g.head_->field_idx_offset);
    EXPECT_EQ(oa.header.field_idx_count, g.head_->field_idx_count);
    EXPECT_EQ(oa.header.list_idx_offset, g.head_->list_idx_offset);
    EXPECT_EQ(oa.header.list_idx_count, g.head_->list_idx_count);
}

TEST(Dialog, GffRoundTrip2)
{
    nw::Gff g("test_data/user/development/dlg_with_params2.dlg");
    EXPECT_TRUE(g.valid());
    nw::Dialog dlg{g.toplevel()};

    nw::GffBuilder oa = nw::serialize(&dlg);
    oa.write_to("tmp/dlg_with_params2.dlg");

    nw::Gff g2("tmp/dlg_with_params2.dlg");
    EXPECT_TRUE(g2.valid());
    EXPECT_EQ(nw::gff_to_gffjson(g), nw::gff_to_gffjson(g2));

    EXPECT_EQ(oa.header.struct_offset, g.head_->struct_offset);
    EXPECT_EQ(oa.header.struct_count, g.head_->struct_count);
    EXPECT_EQ(oa.header.field_offset, g.head_->field_offset);
    EXPECT_EQ(oa.header.field_count, g.head_->field_count);
    EXPECT_EQ(oa.header.label_offset, g.head_->label_offset);
    EXPECT_EQ(oa.header.label_count, g.head_->label_count);
    EXPECT_EQ(oa.header.field_data_offset, g.head_->field_data_offset);
    EXPECT_EQ(oa.header.field_data_count, g.head_->field_data_count);
    EXPECT_EQ(oa.header.field_idx_offset, g.head_->field_idx_offset);
    EXPECT_EQ(oa.header.field_idx_count, g.head_->field_idx_count);
    EXPECT_EQ(oa.header.list_idx_offset, g.head_->list_idx_offset);
    EXPECT_EQ(oa.header.list_idx_count, g.head_->list_idx_count);
}

TEST(Dialog, NWNEEScriptParams)
{
    if (nw::kernel::config().version() == nw::GameVersion::vEE) {

        nw::Gff g("test_data/user/development/dlg_with_params.dlg");
        EXPECT_TRUE(g.valid());
        nw::Dialog dlg{g.toplevel()};

        // Get exisits
        auto val = dlg.starts[0]->get_condition_param("name_appears");
        EXPECT_TRUE(val);
        EXPECT_EQ(*val, "value_appear");
        EXPECT_EQ(dlg.starts[0]->condition_params.size(), 1);

        // Set existing
        dlg.starts[0]->set_condition_param("name_appears", "test");
        EXPECT_EQ(dlg.starts[0]->condition_params.size(), 1);
        auto val3 = dlg.starts[0]->get_condition_param("name_appears");
        EXPECT_TRUE(val3);
        EXPECT_EQ(*val3, "test");

        // Add new
        dlg.starts[0]->set_condition_param("a_new_param", "test_value");
        EXPECT_EQ(dlg.starts[0]->condition_params.size(), 2);
        auto val4 = dlg.starts[0]->get_condition_param("a_new_param");
        EXPECT_TRUE(val4);
        EXPECT_EQ(*val4, "test_value");

        dlg.starts[0]->remove_condition_param("a_new_param");
        EXPECT_EQ(dlg.starts[0]->condition_params.size(), 1);
        dlg.starts[0]->remove_condition_param(0);
        EXPECT_EQ(dlg.starts[0]->condition_params.size(), 0);

        // Actions
        auto val2 = dlg.starts[0]->node->pointers[0]->node->get_action_param("name_action");
        EXPECT_TRUE(val2);
        EXPECT_EQ(*val2, "name_value");
        EXPECT_EQ(dlg.starts[0]->node->pointers[0]->node->action_params.size(), 1);

        // Set existing
        dlg.starts[0]->node->pointers[0]->node->set_action_param("name_action", "test");
        EXPECT_EQ(dlg.starts[0]->node->pointers[0]->node->action_params.size(), 1);
        auto val5 = dlg.starts[0]->node->pointers[0]->node->get_action_param("name_action");
        EXPECT_TRUE(val5);
        EXPECT_EQ(*val5, "test");

        // Add new
        dlg.starts[0]->node->pointers[0]->node->set_action_param("a_new_param", "test_value");
        EXPECT_EQ(dlg.starts[0]->node->pointers[0]->node->action_params.size(), 2);
        auto val6 = dlg.starts[0]->node->pointers[0]->node->get_action_param("a_new_param");
        EXPECT_TRUE(val6);
        EXPECT_EQ(*val6, "test_value");

        dlg.starts[0]->node->pointers[0]->node->remove_action_param("a_new_param");
        EXPECT_EQ(dlg.starts[0]->node->pointers[0]->node->action_params.size(), 1);
        dlg.starts[0]->node->pointers[0]->node->remove_action_param(0);
        EXPECT_EQ(dlg.starts[0]->node->pointers[0]->node->action_params.size(), 0);
    }
}

TEST(Dialog, InternalNodeOps)
{
    nw::Gff g("test_data/user/development/alue_ranger.dlg");
    EXPECT_TRUE(g.valid());
    nw::Dialog dlg{g.toplevel()};

    size_t entries = dlg.entries.size();
    auto node1 = dlg.create_node(nw::DialogNodeType::entry);
    dlg.add_node_internal(node1, nw::DialogNodeType::entry);
    EXPECT_EQ(entries + 1, dlg.entries.size());
    dlg.remove_node_internal(node1, nw::DialogNodeType::entry);
    EXPECT_EQ(entries, dlg.entries.size());

    size_t replies = dlg.replies.size();
    auto node2 = dlg.create_node(nw::DialogNodeType::reply);
    dlg.add_node_internal(node2, nw::DialogNodeType::reply);
    EXPECT_EQ(replies + 1, dlg.replies.size());
    dlg.remove_node_internal(node2, nw::DialogNodeType::reply);
    EXPECT_EQ(replies, dlg.replies.size());

    auto ptr1 = dlg.starts[0];
    dlg.remove_ptr(ptr1);
    EXPECT_EQ(replies - 2, dlg.replies.size());
    EXPECT_EQ(entries - 1, dlg.entries.size());

    dlg.add_ptr(ptr1);
    EXPECT_EQ(replies, dlg.replies.size());
    EXPECT_EQ(entries, dlg.entries.size());

    auto ptr2 = dlg.starts[0]->node->pointers[0];
    dlg.starts[0]->remove_ptr(ptr2);
    EXPECT_EQ(replies - 2, dlg.replies.size());
    EXPECT_EQ(entries - 1, dlg.entries.size());

    dlg.starts[0]->add_ptr(ptr2);
    EXPECT_EQ(replies, dlg.replies.size());
    EXPECT_EQ(entries, dlg.entries.size());
}

TEST(Dialog, Copy)
{
    nw::Gff g("test_data/user/development/alue_ranger.dlg");
    EXPECT_TRUE(g.valid());
    nw::Dialog dlg{g.toplevel()};

    EXPECT_TRUE(dlg.valid());
    EXPECT_GT(dlg.starts.size(), 0);
    EXPECT_TRUE(dlg.starts[0]->is_start);
    EXPECT_EQ(dlg.starts[0]->node->text.get(nw::LanguageID::english),
        "Have you managed to get rid of the Bandit Leader?");

    auto ptr1 = dlg.starts[0]->copy();
    dlg.add_ptr(ptr1);
    EXPECT_TRUE(dlg.starts.back()->is_start);
    EXPECT_EQ(dlg.starts.back()->node->text.get(nw::LanguageID::english),
        "Have you managed to get rid of the Bandit Leader?");
}

TEST(Dialog, RemoveLinkedNode)
{
    nw::Gff g("test_data/user/development/dlg_with_link.dlg");
    EXPECT_TRUE(g.valid());
    nw::Dialog dlg{g.toplevel()};

    EXPECT_TRUE(dlg.valid());
    EXPECT_GT(dlg.starts.size(), 0);
    EXPECT_EQ(dlg.starts[0]->node->pointers.size(), 2);
    dlg.starts[0]->remove_ptr(dlg.starts[0]->node->pointers[0]);
    EXPECT_EQ(dlg.starts[0]->node->pointers.size(), 0);
}

TEST(Dialog, DeleteNode)
{
    nw::Gff g("test_data/user/development/dlg_with_link.dlg");
    EXPECT_TRUE(g.valid());
    nw::Dialog dlg{g.toplevel()};

    dlg.delete_node(nullptr);
    dlg.delete_ptr(nullptr);

    EXPECT_TRUE(dlg.valid());
    EXPECT_GT(dlg.starts.size(), 0);
    EXPECT_EQ(dlg.starts[0]->node->pointers.size(), 2);
    auto ptr1 = dlg.starts[0]->node->pointers[0];
    dlg.starts[0]->remove_ptr(ptr1);
    dlg.delete_ptr(ptr1);

    nw::GffBuilder oa = nw::serialize(&dlg);
    oa.write_to("tmp/test_delete.dlg");

    nlohmann::json j;
    nw::serialize(j, dlg);
    std::ofstream f{"tmp/test_delete.dlg.json"};
    f << std::setw(4) << j;
}
