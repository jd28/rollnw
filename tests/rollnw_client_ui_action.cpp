#include "client_ui_action.hpp"
#include "workspace.hpp"

#include <gtest/gtest.h>

#include <array>
#include <limits>

using namespace nw::toolset;

TEST(ClientUiAction, OwnsTabResourceAndSubtabBytesAcrossWorkspaceChanges)
{
    WorkspaceState workspace;
    const ClientUiActionContext context{.map = ClientInputMap::editor};
    EXPECT_FALSE(capture_client_ui_action_owner(workspace, context).header.available);
    workspace.open_tab("resource", "Resource", WorkspaceTabKind::resource);
    workspace.find_tab("resource")->detail = "objects/Agent.utc";
    ASSERT_NE(workspace.open_subtab("resource", "variables", "Variables"), nullptr);
    ASSERT_TRUE(workspace.set_active_tab("resource"));
    const auto before = capture_client_ui_action_owner(workspace, context);
    ASSERT_TRUE(before.header.available);
    EXPECT_EQ(before.text, "resourceobjects/Agent.utcvariables");
    EXPECT_TRUE(same_client_ui_action_owner(before, capture_client_ui_action_owner(workspace, context)));
    workspace.find_tab("resource")->detail = "objects/Ranger.utc";
    EXPECT_FALSE(same_client_ui_action_owner(before, capture_client_ui_action_owner(workspace, context)));
    EXPECT_EQ(before.text, "resourceobjects/Agent.utcvariables");
    workspace.find_tab("resource")->active_subtab_index = 99;
    const auto invalid = capture_client_ui_action_owner(workspace, context);
    EXPECT_FALSE(invalid.header.available);
    EXPECT_FALSE(same_client_ui_action_owner(invalid, invalid));
    ::testing::Test::RecordProperty("ui_action_context_bytes", sizeof(ClientUiActionContext));
    ::testing::Test::RecordProperty("ui_action_header_bytes", sizeof(ClientUiActionHeader));
}

TEST(ClientUiAction, DistinguishesIdentitiesWithEqualConcatenatedBytes)
{
    WorkspaceState workspace;
    const ClientUiActionContext context{.map = ClientInputMap::editor};
    workspace.open_tab("ab");
    workspace.find_tab("ab")->detail = "c";
    ASSERT_TRUE(workspace.set_active_tab("ab"));
    const auto first = capture_client_ui_action_owner(workspace, context);
    workspace.open_tab("a");
    workspace.find_tab("a")->detail = "bc";
    ASSERT_TRUE(workspace.set_active_tab("a"));
    const auto second = capture_client_ui_action_owner(workspace, context);
    ASSERT_EQ(first.text, second.text);
    EXPECT_FALSE(same_client_ui_action_owner(first, second));
}

TEST(ClientUiAction, DoesNotReuseClicksAfterWorldIdentityModeOrOperationChanges)
{
    WorkspaceState workspace;
    workspace.open_tab("preview");
    ASSERT_TRUE(workspace.set_active_tab("preview"));
    ClientUiActionContext facts{
        .displayed_object = {.id = nw::ObjectID{42}, .type = nw::ObjectType::creature, .version = 1},
        .script_object = {.id = nw::ObjectID{42}, .type = nw::ObjectType::creature, .version = 1},
        .module_generation = 1,
        .resource_generation = 1,
        .map = ClientInputMap::pc,
        .preview = ClientPreviewPhase::running,
        .live_objects = 3,
    };
    const auto before = capture_client_ui_action_owner(workspace, facts);
    facts.displayed_object.version = 2;
    EXPECT_FALSE(same_client_ui_action_owner(before, capture_client_ui_action_owner(workspace, facts)));
    facts = before.header.context;
    facts.live_objects = 0;
    EXPECT_FALSE(same_client_ui_action_owner(before, capture_client_ui_action_owner(workspace, facts)));
    facts = before.header.context;
    ++facts.module_generation;
    EXPECT_FALSE(same_client_ui_action_owner(before, capture_client_ui_action_owner(workspace, facts)));
    facts = before.header.context;
    ++facts.resource_generation;
    EXPECT_FALSE(same_client_ui_action_owner(before, capture_client_ui_action_owner(workspace, facts)));
    facts = before.header.context;
    facts.map = ClientInputMap::editor;
    facts.preview = ClientPreviewPhase::inactive;
    EXPECT_FALSE(same_client_ui_action_owner(before, capture_client_ui_action_owner(workspace, facts)));
    facts = before.header.context;
    facts.command_modal = true;
    EXPECT_FALSE(same_client_ui_action_owner(before, capture_client_ui_action_owner(workspace, facts)));
    facts = before.header.context;
    facts.world_blocked = true;
    EXPECT_FALSE(same_client_ui_action_owner(before, capture_client_ui_action_owner(workspace, facts)));
}

TEST(ClientUiAction, RejectsMalformedRowsIndependentlyAndClearsMismatchedOutput)
{
    WorkspaceState workspace;
    workspace.open_tab("test");
    ASSERT_TRUE(workspace.set_active_tab("test"));
    const auto owner = capture_client_ui_action_owner(workspace, {.map = ClientInputMap::editor});
    std::array<ClientUiActionOwner, 10> before;
    std::array<ClientUiActionOwner, 10> after;
    before.fill(owner);
    after.fill(owner);
    after[0].header.context.map = static_cast<ClientInputMap>(255);
    after[1].header.context.surface = static_cast<ObjectWorkbenchSurface>(255);
    after[2].header.context.preview = static_cast<ClientPreviewPhase>(255);
    after[3].header.context.area_surface = 3;
    after[4].header.tab_kind = 8;
    after[5].header.context.live_objects = 8;
    after[6].header.text_lengths[0] = std::numeric_limits<uint32_t>::max();
    after[7].header.context.script_area.type = nw::ObjectType::item;
    after[8].header.available = false;
    std::array<bool, 10> matches;
    matches.fill(true);
    ASSERT_TRUE(match_client_ui_action_owners(before, after, matches));
    for (size_t index = 0; index < 9; ++index) {
        EXPECT_FALSE(matches[index]) << index;
    }
    EXPECT_TRUE(matches[9]);
    EXPECT_FALSE(match_client_ui_action_owners(before, std::span{after}.first(9), matches));
    for (const auto match : matches) {
        EXPECT_FALSE(match);
    }
    EXPECT_FALSE(capture_client_ui_action_owner(workspace, {.map = ClientInputMap::invalid}).header.available);
}
