#include <gtest/gtest.h>

#include "../tools/client/dialog_document.hpp"

#include <nw/formats/Dialog.hpp>
#include <nw/serialization/Gff.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace {

using namespace nw::toolset;

TEST(ClientDialogDocument, FlattensGffIntoDisplayRows)
{
    DialogDocumentSnapshot document;
    load_dialog_document("test_data/user/development/alue_ranger.dlg", document);

    ASSERT_EQ(document.status, DialogDocumentStatus::ready) << document.diagnostic;
    ASSERT_FALSE(document.rows.empty());
    EXPECT_EQ(document.start_count, 2);
    EXPECT_GT(document.entry_count, 0);
    EXPECT_GT(document.reply_count, 0);
    EXPECT_TRUE(document.rows.front().is_start);
    EXPECT_EQ(document.rows.front().type, nw::DialogNodeType::entry);
    EXPECT_EQ(document.rows.front().depth, 0);
    EXPECT_EQ(document.rows.front().parent, dialog_no_row);
    EXPECT_EQ(document.text_view(document.rows.front().text),
        "Have you managed to get rid of the Bandit Leader?");
}

TEST(ClientDialogDocument, JsonAndGffProduceTheSameRows)
{
    nw::Gff archive{"test_data/user/development/alue_ranger.dlg"};
    ASSERT_TRUE(archive.valid());
    nw::Dialog dialog{archive.toplevel()};
    ASSERT_TRUE(dialog.valid());

    nlohmann::json json;
    nw::serialize(json, dialog);
    const std::filesystem::path json_path{"tmp/client_dialog_document.dlg.json"};
    {
        std::ofstream output{json_path, std::ios::binary};
        ASSERT_TRUE(output);
        output << json;
    }

    DialogDocumentSnapshot gff_document;
    DialogDocumentSnapshot json_document;
    load_dialog_document("test_data/user/development/alue_ranger.dlg", gff_document);
    load_dialog_document(json_path, json_document);

    ASSERT_EQ(gff_document.status, DialogDocumentStatus::ready) << gff_document.diagnostic;
    ASSERT_EQ(json_document.status, DialogDocumentStatus::ready) << json_document.diagnostic;
    ASSERT_EQ(json_document.rows.size(), gff_document.rows.size());
    ASSERT_EQ(json_document.parameters.size(), gff_document.parameters.size());
    EXPECT_EQ(json_document.entry_count, gff_document.entry_count);
    EXPECT_EQ(json_document.reply_count, gff_document.reply_count);
    EXPECT_EQ(json_document.link_count, gff_document.link_count);
    for (size_t index = 0; index < gff_document.rows.size(); ++index) {
        const auto& lhs = gff_document.rows[index];
        const auto& rhs = json_document.rows[index];
        EXPECT_EQ(rhs.parent, lhs.parent);
        EXPECT_EQ(rhs.node_index, lhs.node_index);
        EXPECT_EQ(rhs.depth, lhs.depth);
        EXPECT_EQ(rhs.type, lhs.type);
        EXPECT_EQ(rhs.is_start, lhs.is_start);
        EXPECT_EQ(rhs.is_link, lhs.is_link);
        EXPECT_EQ(json_document.text_view(rhs.text),
            gff_document.text_view(lhs.text));
    }
}

TEST(ClientDialogDocument, LinkRowsAreTerminal)
{
    DialogDocumentSnapshot document;
    load_dialog_document("test_data/user/development/dlg_with_link.dlg", document);

    ASSERT_EQ(document.status, DialogDocumentStatus::ready) << document.diagnostic;
    ASSERT_GT(document.link_count, 0);
    bool found_link = false;
    for (size_t index = 0; index < document.rows.size(); ++index) {
        if (!document.rows[index].is_link) {
            continue;
        }
        found_link = true;
        for (size_t child = index + 1; child < document.rows.size(); ++child) {
            EXPECT_NE(document.rows[child].parent, index);
        }
    }
    EXPECT_TRUE(found_link);
}

TEST(ClientDialogDocument, RejectsOutOfRangeJsonPointers)
{
    nlohmann::json json = {
        {"$type", "DLG"},
        {"$version", 1},
        {"entries", nlohmann::json::array()},
        {"replies", nlohmann::json::array()},
        {"starts", nlohmann::json::array({
                       {{"index", 0}, {"script_appears", ""}, {"is_start", true}, {"is_link", false}, {"comment", ""}, {"condition_params", nlohmann::json::array()}},
                   })},
        {"script_abort", ""},
        {"script_end", ""},
        {"delay_entry", 0},
        {"delay_reply", 0},
        {"word_count", 0},
        {"prevent_zoom", false},
    };
    const std::filesystem::path path{"tmp/client_dialog_document_invalid.dlg.json"};
    {
        std::ofstream output{path, std::ios::binary};
        ASSERT_TRUE(output);
        output << json;
    }

    DialogDocumentSnapshot document;
    load_dialog_document(path, document);

    EXPECT_EQ(document.status, DialogDocumentStatus::invalid_data);
    EXPECT_TRUE(document.rows.empty());
    EXPECT_FALSE(document.diagnostic.empty());
}

TEST(ClientDialogDocument, RejectsNonLinkCycles)
{
    nw::Gff archive{"test_data/user/development/alue_ranger.dlg"};
    ASSERT_TRUE(archive.valid());
    nw::Dialog dialog{archive.toplevel()};
    ASSERT_TRUE(dialog.valid());
    ASSERT_FALSE(dialog.starts.empty());
    ASSERT_FALSE(dialog.starts.front()->node->pointers.empty());

    nw::DialogNode* root = dialog.starts.front()->node;
    nw::DialogNode* reply = root->pointers.front()->node;
    nw::DialogPtr* cycle = dialog.create_ptr();
    cycle->parent = &dialog;
    cycle->type = nw::DialogNodeType::entry;
    cycle->index = static_cast<uint32_t>(dialog.node_index(root, nw::DialogNodeType::entry));
    cycle->node = root;
    reply->pointers.push_back(cycle);

    nlohmann::json json;
    nw::serialize(json, dialog);
    const std::filesystem::path path{"tmp/client_dialog_document_cycle.dlg.json"};
    {
        std::ofstream output{path, std::ios::binary};
        ASSERT_TRUE(output);
        output << json;
    }

    DialogDocumentSnapshot document;
    load_dialog_document(path, document);

    EXPECT_EQ(document.status, DialogDocumentStatus::invalid_data);
    EXPECT_TRUE(document.rows.empty());
    EXPECT_EQ(document.diagnostic, "Dialog contains a non-link cycle.");
}

} // namespace
