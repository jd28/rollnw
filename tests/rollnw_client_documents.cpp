#include "object_document.hpp"
#include "resource_document.hpp"
#include "workspace.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/serialization/Serialization.hpp>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <memory>

namespace nw::toolset {
namespace {

TEST(ClientDocuments, ExclusiveResourceWritesAndExpectedReplacement)
{
    const std::filesystem::path root = "tmp/client_resource_publication";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto target = root / "new.uti.json";
    const std::array create{ResourceFileWrite{target, "original", ResourceFileWriteMode::create, {}}};
    auto results = write_resource_files_atomic(create);
    ASSERT_EQ(results.size(), 1u);
    ASSERT_TRUE(results[0].written) << results[0].error;

    results = write_resource_files_atomic(create);
    EXPECT_FALSE(results[0].written);
    const std::array stale{ResourceFileWrite{target, "replacement", ResourceFileWriteMode::replace, "stale"}};
    results = write_resource_files_atomic(stale);
    EXPECT_FALSE(results[0].written);
    const std::array replace{ResourceFileWrite{target, "replacement", ResourceFileWriteMode::replace, "original"}};
    results = write_resource_files_atomic(replace);
    ASSERT_TRUE(results[0].written) << results[0].error;
    std::ifstream input{target};
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>{input}, {}), "replacement");
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator{root}, std::filesystem::directory_iterator{}), 1);
}

TEST(ClientDocuments, InvalidResourceWriteBatchPublishesNothing)
{
    const std::filesystem::path root = "tmp/client_resource_publication_rejection";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto target = root / "new.uti.json";
    const std::array duplicate{
        ResourceFileWrite{target, "first", ResourceFileWriteMode::create, {}},
        ResourceFileWrite{root / "." / "new.uti.json", "second", ResourceFileWriteMode::create, {}},
    };
    const auto results = write_resource_files_atomic(duplicate);
    ASSERT_EQ(results.size(), 2u);
    EXPECT_FALSE(results[0].written);
    EXPECT_FALSE(results[1].written);
    EXPECT_FALSE(std::filesystem::exists(target));
    EXPECT_TRUE(write_resource_files_atomic({}).empty());
}

struct HistoryLifetimeProbe {
    ObjectHandle root;
    bool* released;
    ~HistoryLifetimeProbe()
    {
        EXPECT_TRUE(kernel::objects().valid(root));
        *released = true;
    }
};

TEST(ClientDocuments, TabsRetainRootsAndReleaseHistoryBeforeObjects)
{
    WorkspaceState workspace;
    workspace.ensure_default_tabs();
    auto* item = kernel::objects().make<Item>();
    ASSERT_NE(item, nullptr);
    const auto root = item->handle();
    auto& tab = workspace.open_or_replace_tab("item", "Item", WorkspaceTabKind::preview, "item.uti.json");
    ASSERT_TRUE(tab.document.adopt(root));
    EXPECT_FALSE(tab.document.adopt(root));
    item->comment = "unsaved item";
    bool history_released = false;
    auto probe = std::make_shared<HistoryLifetimeProbe>();
    probe->root = root;
    probe->released = &history_released;
    workspace.push_undo({"Edit", [probe](CommandContext&) {
                             auto* live = kernel::objects().get<Item>(probe->root);
                             EXPECT_NE(live, nullptr);
                             if (live) { live->comment = "original"; }
                             return CommandResult{}; },
        [probe](CommandContext&) {
            auto* live = kernel::objects().get<Item>(probe->root);
            EXPECT_NE(live, nullptr);
            if (live) { live->comment = "unsaved item"; }
            return CommandResult{};
        }});
    probe.reset();
    workspace.set_tab_dirty("item", true);

    for (size_t i = 0; i < 32; ++i) {
        workspace.open_tab("other:" + std::to_string(i));
    }
    ASSERT_TRUE(workspace.move_tab("item", workspace.tabs().size() - 1));
    ASSERT_TRUE(workspace.move_tab("item", 2));
    ASSERT_TRUE(workspace.set_active_tab("item"));
    EXPECT_EQ(workspace.active_tab()->document.object(), root);
    EXPECT_EQ(item->comment, "unsaved item");
    EXPECT_EQ(workspace.undo_count(), 1u);
    EXPECT_FALSE(history_released);
    EXPECT_TRUE(workspace.undo({}).ok());
    EXPECT_EQ(item->comment, "original");
    EXPECT_TRUE(workspace.redo({}).ok());
    EXPECT_EQ(item->comment, "unsaved item");
    EXPECT_EQ(workspace.request_close_tab("item").status, WorkspaceCloseStatus::dirty);
    EXPECT_TRUE(kernel::objects().valid(root));
    ASSERT_TRUE(workspace.request_close_tab("item", true).closed());
    EXPECT_TRUE(history_released);
    EXPECT_FALSE(kernel::objects().valid(root));
}

TEST(ClientDocuments, ReusesOnePinnedAreaTabAndProtectsItsUnsavedDocument)
{
    WorkspaceState workspace;
    workspace.ensure_default_tabs();
    const auto first_id = workspace.open_area_tab("first.caf.json", "First").id;
    EXPECT_EQ(first_id, "area");
    auto* first = kernel::objects().make<Area>();
    ASSERT_NE(first, nullptr);
    const auto root = first->handle();
    ASSERT_TRUE(workspace.active_tab()->document.adopt(root));
    workspace.set_tab_dirty(first_id, true);
    workspace.push_undo({"First edit", [](CommandContext&) { return CommandResult{}; },
        [](CommandContext&) { return CommandResult{}; }});
    const auto second_id = workspace.open_area_tab("second.caf.json", "Second").id;
    EXPECT_EQ(first_id, second_id);
    EXPECT_EQ(workspace.tabs().size(), 2u);
    EXPECT_EQ(workspace.active_tab()->detail, "first.caf.json");
    workspace.open_tab("blueprint");
    EXPECT_EQ(workspace.open_area_tab("first.caf.json", "First").id, first_id);
    EXPECT_EQ(workspace.undo_count(), 1u);
    EXPECT_TRUE(workspace.active_tab()->dirty);
    EXPECT_EQ(workspace.active_tab()->document.object(), root);
    workspace.open_or_replace_tab(first_id, "Replacement", WorkspaceTabKind::area, "replacement.caf.json");
    EXPECT_EQ(workspace.active_tab()->detail, "first.caf.json");
    EXPECT_EQ(workspace.active_tab()->title, "First");
    EXPECT_EQ(workspace.undo_count(), 1u);
    EXPECT_FALSE(workspace.request_close_tab(first_id, true).closed());
    EXPECT_FALSE(workspace.move_tab(first_id, 2));
    workspace.set_tab_dirty(first_id, false);
    auto& replacement = workspace.open_area_tab("second.caf.json", "Second");
    EXPECT_EQ(replacement.id, "area");
    EXPECT_EQ(replacement.detail, "second.caf.json");
    EXPECT_TRUE(replacement.undo_stack.empty());
    EXPECT_EQ(replacement.document.object().type, ObjectType::invalid);
    EXPECT_FALSE(kernel::objects().valid(root));
    workspace.ensure_default_tabs();
    EXPECT_EQ(workspace.tabs().size(), 3u);
    EXPECT_EQ(workspace.tabs()[1].id, "area");
    EXPECT_FALSE(workspace.tabs()[1].closable);
    EXPECT_FALSE(workspace.tabs()[1].movable);
}

TEST(ClientDocuments, SaveBatchRetainsSelectionHistoryAndReloadsMultipleEditedGraphs)
{
    ASSERT_NE(kernel::load_module("test_data/user/modules/DockerDemo.mod", false), nullptr);
    const std::filesystem::path project = "tmp/client_documents_batch";
    std::filesystem::remove_all(project);
    std::filesystem::create_directories(project);
    WorkspaceState workspace;
    workspace.ensure_default_tabs();
    std::array<ObjectHandle, 3> roots;
    std::array<ObjectHandle, 1> children;
    const std::array<std::string_view, 3> ids{"item1", "item2", "area"};
    for (size_t i = 0; i < ids.size(); ++i) {
        const bool is_area = i >= 2;
        ObjectBase* root = is_area ? static_cast<ObjectBase*>(kernel::objects().make_area(Resref{"test_area"}))
                                   : kernel::objects().load_file<Item>("test_data/user/development/cloth028.uti");
        ASSERT_NE(root, nullptr);
        roots[i] = root->handle();
        root->comment = std::string{ids[i]};
        if (is_area) {
            auto* area = root->as_area();
            ASSERT_NE(area, nullptr);
            area->comments = std::string{ids[i]};
            ASSERT_FALSE(area->creatures.empty());
            auto* child = area->creatures.front();
            child->comment = "retained area child";
            children[i - 2] = child->handle();
        }
        const auto path = std::string{ids[i]} + (is_area ? ".caf.json" : ".uti.json");
        {
            std::ofstream original{project / path};
            ASSERT_TRUE(original);
            original << "{}\n";
        }
        auto& tab = is_area ? workspace.open_area_tab(path, std::string{ids[i]})
                            : workspace.open_or_replace_tab(std::string{ids[i]}, std::string{ids[i]},
                                  WorkspaceTabKind::preview, path);
        ASSERT_TRUE(tab.document.adopt(root->handle()));
        tab.dirty = true;
        workspace.push_undo({"Edit", [](CommandContext&) { return CommandResult{}; },
            [](CommandContext&) { return CommandResult{}; }});
    }
    workspace.set_active_tab("home");
    auto result = save_workspace_documents(workspace, project, ids);
    ASSERT_TRUE(result.ok()) << result.message;
    EXPECT_EQ(workspace.active_tab_id(), "home");
    EXPECT_FALSE(workspace.has_dirty_tabs());
    for (size_t i = 0; i < ids.size(); ++i) {
        const auto* tab = workspace.find_tab(ids[i]);
        ASSERT_NE(tab, nullptr);
        EXPECT_EQ(tab->document.object(), roots[i]);
        EXPECT_EQ(tab->undo_stack.size(), 1u);
    }
    workspace.clear();
    for (const auto root : roots) {
        EXPECT_FALSE(kernel::objects().valid(root));
    }
    for (const auto child : children) {
        EXPECT_FALSE(kernel::objects().valid(child));
    }

    for (size_t i = 0; i < ids.size(); ++i) {
        ObjectDocument reloaded;
        ObjectBase* root = nullptr;
        if (i < 2) {
            root = kernel::objects().load_file<Item>(project / (std::string{ids[i]} + ".uti.json"));
        } else {
            auto* area = kernel::objects().make<Area>();
            ASSERT_TRUE(reloaded.adopt(area->handle()));
            std::ifstream input{project / (std::string{ids[i]} + ".caf.json")};
            ASSERT_TRUE(input);
            const auto data = nlohmann::json::parse(input);
            ASSERT_TRUE(deserialize(area, data));
            EXPECT_EQ(area->comments, ids[i]);
            ASSERT_FALSE(area->creatures.empty());
            EXPECT_EQ(area->creatures.front()->comment, "retained area child");
            root = area;
        }
        ASSERT_NE(root, nullptr);
        if (i < 2) { ASSERT_TRUE(reloaded.adopt(root->handle())); }
        if (i < 2) { EXPECT_EQ(root->comment, ids[i]); }
    }
}

TEST(ClientDocuments, BatchContinuesAfterFailuresAndRejectsInvalidProtocolBeforeWrites)
{
    const std::filesystem::path project = "tmp/client_documents_partial_save";
    std::filesystem::remove_all(project);
    std::filesystem::create_directories(project / "blocked.uti.json");
    {
        std::ofstream original{project / "good.uti.json", std::ios::binary};
        ASSERT_TRUE(original);
        original << "{}\n";
    }
    ASSERT_EQ(std::filesystem::file_size(project / "good.uti.json"), 3u);
    WorkspaceState workspace;
    const std::array<std::string_view, 3> ids{"good", "blocked", "escape"};
    const std::array paths{"good.uti.json", "blocked.uti.json", "../escape.uti.json"};
    for (size_t i = 0; i < ids.size(); ++i) {
        auto* item = kernel::objects().load_file<Item>("test_data/user/development/cloth028.uti");
        ASSERT_NE(item, nullptr);
        auto& tab = workspace.open_or_replace_tab(std::string{ids[i]}, {}, WorkspaceTabKind::preview, paths[i]);
        ASSERT_TRUE(tab.document.adopt(item->handle()));
        tab.dirty = true;
    }
    const std::array<std::string_view, 2> duplicates{"good", "good"};
    EXPECT_EQ(save_workspace_documents(workspace, project, duplicates).status, CommandStatus::rejected);
    EXPECT_EQ(std::filesystem::file_size(project / "good.uti.json"), 3u);
    const std::array<std::string_view, 2> empty_id{"good", ""};
    EXPECT_EQ(save_workspace_documents(workspace, project, empty_id).status, CommandStatus::rejected);
    EXPECT_EQ(std::filesystem::file_size(project / "good.uti.json"), 3u);
    const std::array<std::string_view, 4> partial{"blocked", "missing", "good", "escape"};
    const auto result = save_workspace_documents(workspace, project, partial);
    EXPECT_EQ(result.status, CommandStatus::failed);
    EXPECT_NE(result.message.find("Saved 1 of 4"), std::string::npos);
    EXPECT_NE(result.message.find("blocked:"), std::string::npos);
    EXPECT_NE(result.message.find("missing:"), std::string::npos);
    EXPECT_NE(result.message.find("escape:"), std::string::npos);
    EXPECT_FALSE(workspace.find_tab("good")->dirty);
    EXPECT_TRUE(workspace.find_tab("blocked")->dirty);
    EXPECT_TRUE(workspace.find_tab("escape")->dirty);
    EXPECT_TRUE(std::filesystem::exists(project / "good.uti.json"));
    EXPECT_FALSE(std::filesystem::exists(project / "good.uti.json.rollnw-client-save.tmp"));
    EXPECT_EQ(save_workspace_documents(workspace, project, {}).status, CommandStatus::noop);

    workspace.ensure_default_tabs();
    const std::array<std::string_view, 1> home{"home"};
    EXPECT_EQ(save_workspace_documents(workspace, project, home).status, CommandStatus::failed);
    EXPECT_FALSE(workspace.find_tab("home")->dirty);

    workspace.find_tab("blocked")->detail = "fixed.uti.json";
    {
        std::ofstream original{project / "fixed.uti.json"};
        ASSERT_TRUE(original);
        original << "{}\n";
    }
    const std::array<std::string_view, 1> fixed{"blocked"};
    EXPECT_TRUE(save_workspace_documents(workspace, project, fixed).ok());
    EXPECT_FALSE(workspace.find_tab("blocked")->dirty);
    workspace.find_tab("escape")->document.reset();
    const std::array<std::string_view, 1> stale{"escape"};
    EXPECT_EQ(save_workspace_documents(workspace, project, stale).status, CommandStatus::failed);
    EXPECT_TRUE(workspace.find_tab("escape")->dirty);
}

TEST(ClientDocuments, SaveRejectsBinaryAndSymlinkEscapesWithoutOverwritingFiles)
{
    const std::filesystem::path root = "tmp/client_documents_save_paths";
    std::filesystem::remove_all(root);
    const auto project = root / "project";
    std::filesystem::create_directories(project);
    for (const auto& path : {project / "item.uti", root / "outside.uti.json"}) {
        std::ofstream original{path};
        ASSERT_TRUE(original);
        original << "original";
    }
    std::filesystem::create_symlink("../outside.uti.json", project / "link.uti.json");
    WorkspaceState workspace;
    auto* item = kernel::objects().load_file<Item>("test_data/user/development/cloth028.uti");
    ASSERT_NE(item, nullptr);
    auto& tab = workspace.open_or_replace_tab("item", "Item", WorkspaceTabKind::preview, "item.uti");
    ASSERT_TRUE(tab.document.adopt(item->handle()));
    tab.dirty = true;
    const std::array<std::string_view, 1> ids{"item"};
    auto result = save_workspace_documents(workspace, project, ids);
    EXPECT_FALSE(result.ok());
    EXPECT_NE(result.message.find("binary"), std::string::npos);
    EXPECT_TRUE(tab.dirty);
    EXPECT_EQ(std::filesystem::file_size(project / "item.uti"), 8u);
    tab.detail = "link.uti.json";
    result = save_workspace_documents(workspace, project, ids);
    EXPECT_FALSE(result.ok());
    EXPECT_NE(result.message.find("outside"), std::string::npos);
    EXPECT_TRUE(tab.dirty);
    EXPECT_EQ(std::filesystem::file_size(root / "outside.uti.json"), 8u);
    tab.detail = "missing.uti.json";
    EXPECT_FALSE(save_workspace_documents(workspace, project, ids).ok());
    EXPECT_TRUE(tab.dirty);
    EXPECT_FALSE(std::filesystem::exists(project / tab.detail));
}

TEST(ClientDocuments, DerivedMapFailureDoesNotFailAnAuthoredAreaSave)
{
    ASSERT_NE(kernel::load_module("test_data/user/modules/DockerDemo.mod", false), nullptr);
    const std::filesystem::path project = "tmp/client_documents_map_warning";
    std::filesystem::remove_all(project);
    std::filesystem::create_directories(project);
    for (const auto* path : {"test_area.caf.json", ".rollnw"}) {
        std::ofstream original{project / path};
        ASSERT_TRUE(original);
        original << "{}\n";
    }
    WorkspaceState workspace;
    auto* area = kernel::objects().make_area(Resref{"test_area"});
    ASSERT_NE(area, nullptr);
    auto& tab = workspace.open_area_tab("test_area.caf.json", "Area");
    ASSERT_TRUE(tab.document.adopt(area->handle()));
    tab.dirty = true;
    const std::array<std::string_view, 1> ids{tab.id};
    const auto saved = save_workspace_documents(workspace, project, ids);
    EXPECT_TRUE(saved.ok()) << saved.message;
    EXPECT_EQ(saved.output_channel, CommandOutputChannel::warn);
    EXPECT_NE(saved.message.find("Area map unavailable"), std::string::npos);
    EXPECT_FALSE(tab.dirty);
    EXPECT_GT(std::filesystem::file_size(project / "test_area.caf.json"), 3u);
}

} // namespace
} // namespace nw::toolset
