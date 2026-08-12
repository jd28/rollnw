#include "../tools/ui/ui_v1.hpp"
#include "../tools/ui/virtual_combobox.hpp"
#include "../tools/ui/virtual_list.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace nw::toolset {
namespace {

class CountingListAdapter final : public VirtualListAdapter {
public:
    explicit CountingListAdapter(int row_count)
        : row_count_{row_count}
    {
    }

    [[nodiscard]] int size() const override { return row_count_; }

    [[nodiscard]] std::string render_row_inner(int i, bool) const override
    {
        return std::to_string(i);
    }

private:
    int row_count_ = 0;
};

size_t count_markup_rows(std::string_view markup)
{
    constexpr std::string_view row_prefix = "<div class=\"vl_row";
    size_t count = 0;
    size_t offset = 0;
    while ((offset = markup.find(row_prefix, offset)) != std::string_view::npos) {
        ++count;
        offset += row_prefix.size();
    }
    return count;
}

VirtualListController make_midpoint_controller(int row_count, int midpoint_row)
{
    VirtualListController controller;
    controller.set_row_height(30);
    controller.set_overscan(8);
    controller.set_total_rows(row_count);
    controller.set_viewport_height(300);
    controller.set_scroll_top(midpoint_row * 30);
    return controller;
}

} // namespace

TEST(ClientVirtualList, MaterializedRowsDependOnViewportNotSourceCount)
{
    auto small_controller = make_midpoint_controller(1'000, 500);
    auto large_controller = make_midpoint_controller(100'000, 50'000);
    const CountingListAdapter small_adapter{1'000};
    const CountingListAdapter large_adapter{100'000};

    const auto small_range = small_controller.compute_range();
    const auto large_range = large_controller.compute_range();
    ASSERT_EQ(small_range.end - small_range.start, 26);
    ASSERT_EQ(large_range.end - large_range.start, 26);

    const std::string small_markup = render_virtual_list(small_controller, small_adapter);
    const std::string large_markup = render_virtual_list(large_controller, large_adapter);
    EXPECT_EQ(count_markup_rows(small_markup), 26);
    EXPECT_EQ(count_markup_rows(large_markup), 26);
}

TEST(ClientVirtualList, ClampsInvalidCountsAndEndRange)
{
    VirtualListController controller;
    controller.set_total_rows(-1);
    controller.set_viewport_height(300);
    controller.set_scroll_top(300);

    EXPECT_EQ(controller.total_rows(), 0);
    EXPECT_EQ(controller.scroll_top(), 0);
    const auto empty_range = controller.compute_range();
    EXPECT_EQ(empty_range.start, 0);
    EXPECT_EQ(empty_range.end, 0);

    controller.set_overscan(-1);
    controller.set_total_rows(5);
    controller.set_viewport_height(61);
    controller.set_scroll_top(300);
    const auto end_range = controller.compute_range();
    EXPECT_EQ(end_range.start, 2);
    EXPECT_EQ(end_range.end, 5);
    EXPECT_EQ(end_range.top_spacer_px, 60);
    EXPECT_EQ(end_range.bottom_spacer_px, 0);
}

TEST(ClientVirtualListHost, ReplacesValidatedBatchesAtomically)
{
    VirtualListHost host;
    ASSERT_TRUE(host.create("items", {.row_height = 30, .overscan = 4}));
    ASSERT_TRUE(host.set_items("items", {
                                            {.key = "one", .cells = {"One", "", "", ""}, .cell_count = 1, .enabled_mask = 1},
                                        }));
    const auto before = host.window("items", 300, 0);
    ASSERT_TRUE(before);
    ASSERT_EQ(before->items.size(), 1);

    EXPECT_FALSE(host.set_items("items", {
                                             {.key = "duplicate", .cells = {"First", "", "", ""}, .cell_count = 1, .enabled_mask = 1},
                                             {.key = "duplicate", .cells = {"Second", "", "", ""}, .cell_count = 1, .enabled_mask = 1},
                                         }));
    EXPECT_FALSE(host.set_items("items", {
                                             {.key = "bad-count", .cell_count = 0, .enabled_mask = 0},
                                         }));
    EXPECT_FALSE(host.set_items("items", {
                                             {.key = "bad-mask", .cells = {"Value", "", "", ""}, .cell_count = 1, .enabled_mask = 2},
                                         }));

    const auto after = host.window("items", 300, 0);
    ASSERT_TRUE(after);
    ASSERT_EQ(after->items.size(), 1);
    EXPECT_EQ(after->items.front().key, "one");
    EXPECT_EQ(after->revision, before->revision);
}

TEST(ClientVirtualListHost, RuntimeResetDropsRowsAndAllowsListRecreation)
{
    VirtualListHost host;
    ASSERT_TRUE(host.create("items", {.row_height = 30, .overscan = 4}));
    ASSERT_TRUE(host.set_items("items", {
                                            {.key = "old", .cells = {"Old", "", "", ""}, .cell_count = 1, .enabled_mask = 1},
                                        }));
    ASSERT_TRUE(host.register_refresh_callback("old.refresh"));
    const uint64_t generation = host.generation();

    host.reset();

    EXPECT_NE(host.generation(), generation);
    EXPECT_FALSE(host.window("items", 300, 0));
    EXPECT_TRUE(host.refresh_callbacks().empty());
    ASSERT_TRUE(host.create("items", {.row_height = 34, .overscan = 6}));
    ASSERT_TRUE(host.set_items("items", {
                                            {.key = "new", .cells = {"New", "", "", ""}, .cell_count = 1, .enabled_mask = 1},
                                        }));
    const auto recreated = host.window("items", 300, 0);
    ASSERT_TRUE(recreated);
    ASSERT_EQ(recreated->items.size(), 1);
    EXPECT_EQ(recreated->items.front().key, "new");
}

TEST(ClientVirtualListHost, MaterializesViewportWindowAtMaximumBatchScale)
{
    VirtualListHost host;
    ASSERT_TRUE(host.create("items", {.row_height = 30, .overscan = 8}));
    std::vector<UiListItem> items;
    items.reserve(65'535);
    for (int index = 0; index < 65'535; ++index) {
        items.push_back({
            .key = std::to_string(index),
            .cells = {std::to_string(index), "", "", ""},
            .cell_count = 1,
            .enabled_mask = 1,
        });
    }
    ASSERT_TRUE(host.set_items("items", std::move(items)));

    const auto window = host.window("items", 300, 30'000 * 30);
    ASSERT_TRUE(window);
    EXPECT_EQ(window->items.size(), 65'535);
    EXPECT_EQ(window->range.end - window->range.start, 26);
    EXPECT_EQ(window->range.start, 29'992);
    EXPECT_EQ(window->range.end, 30'018);
}

TEST(ClientVirtualListHost, VirtualizesFixedColumnGridAsLogicalRows)
{
    VirtualListHost host;
    ASSERT_TRUE(host.create("models", {
                                          .row_height = 96,
                                          .overscan = 1,
                                          .columns = 5,
                                      }));
    std::vector<UiListItem> items;
    items.reserve(100);
    for (int index = 0; index < 100; ++index) {
        items.push_back({
            .key = std::to_string(index),
            .cells = {std::to_string(index), "", "", ""},
            .cell_count = 1,
            .enabled_mask = 1,
        });
    }
    ASSERT_TRUE(host.set_items("models", std::move(items)));

    const auto window = host.window("models", 192, 10 * 96);
    ASSERT_TRUE(window);
    EXPECT_EQ(window->items.size(), 100);
    EXPECT_EQ(window->columns, 5);
    EXPECT_EQ(window->row_height, 96);
    EXPECT_EQ(window->range.start, 9);
    EXPECT_EQ(window->range.end, 13);
    EXPECT_EQ(window->range.top_spacer_px, 9 * 96);
    EXPECT_EQ(window->range.bottom_spacer_px, 7 * 96);

    ASSERT_TRUE(host.set_selected("models",
        {.list_id = "models", .key = "52", .index = 52, .cell = -1}, false));
    const auto next_scroll = host.move_and_activate("models", 1, 192, 0);
    ASSERT_TRUE(next_scroll);
    EXPECT_EQ(*next_scroll, 9 * 96);
    const auto selected = host.get_selected("models");
    ASSERT_TRUE(selected);
    EXPECT_EQ(selected->index, 53);
    EXPECT_EQ(selected->key, "53");
}

TEST(ClientVirtualListHost, RejectsInvalidFixedColumnConfigurations)
{
    VirtualListHost host;
    EXPECT_FALSE(host.create("zero", {
                                         .row_height = 96,
                                         .overscan = 1,
                                         .columns = 0,
                                     }));
    EXPECT_FALSE(host.create("too-many", {
                                             .row_height = 96,
                                             .overscan = 1,
                                             .columns = 17,
                                         }));
    EXPECT_TRUE(host.create("maximum", {
                                           .row_height = 96,
                                           .overscan = 1,
                                           .columns = 16,
                                       }));
}

TEST(ClientVirtualListHost, RejectsStaleSelectionsAndDisabledCells)
{
    VirtualListHost host;
    ASSERT_TRUE(host.create("items", {.row_height = 30, .overscan = 4}));
    ASSERT_TRUE(host.set_items("items", {
                                            {.key = "row", .cells = {"Name", "Disabled", "Enabled", ""}, .cell_count = 3, .enabled_mask = 5},
                                        }));
    ASSERT_TRUE(host.set_callback("items", UiListEventType::select, "on_select"));
    ASSERT_TRUE(host.set_callback("items", UiListEventType::activate, "on_activate"));

    EXPECT_FALSE(host.set_selected("items", {.list_id = "items", .key = "row", .index = 1, .cell = 0}));
    EXPECT_FALSE(host.push_activate("items", 0, 1));
    ASSERT_TRUE(host.push_activate("items", 0, 2));

    std::vector<UiListEvent> events;
    host.drain_events([&](const UiListEvent& event) { events.push_back(event); });
    ASSERT_EQ(events.size(), 2);
    EXPECT_EQ(events[0].type, UiListEventType::select);
    EXPECT_EQ(events[1].type, UiListEventType::activate);
    for (const auto& event : events) {
        EXPECT_EQ(event.selection.list_id, "items");
        EXPECT_EQ(event.selection.key, "row");
        EXPECT_EQ(event.selection.index, 0);
        EXPECT_EQ(event.selection.cell, 2);
    }
}

TEST(ClientVirtualListHost, PublishesTitleAndVisibilityByRevision)
{
    VirtualListHost host;
    ASSERT_TRUE(host.create("items", {.row_height = 30, .overscan = 4}));
    const auto initial = host.window("items", 300, 0);
    ASSERT_TRUE(initial);
    EXPECT_TRUE(initial->visible);
    EXPECT_TRUE(initial->title.empty());

    ASSERT_TRUE(host.set_title("items", "Subtype"));
    ASSERT_TRUE(host.set_visible("items", false));
    const auto changed = host.window("items", 300, 0);
    ASSERT_TRUE(changed);
    EXPECT_FALSE(changed->visible);
    EXPECT_EQ(changed->title, "Subtype");
    EXPECT_GT(changed->revision, initial->revision);
}

TEST(ClientVirtualListHost, CyclesActivatesAndScrollsOneManagedPath)
{
    VirtualListHost host;
    ASSERT_TRUE(host.create("items", {.row_height = 30, .overscan = 4}));
    std::vector<UiListItem> items;
    for (int index = 0; index < 20; ++index) {
        items.push_back({
            .key = std::to_string(index * 7),
            .cells = {std::to_string(index), "", "", ""},
            .cell_count = 1,
            .enabled_mask = 1,
        });
    }
    ASSERT_TRUE(host.set_items("items", std::move(items)));
    ASSERT_TRUE(host.set_callback("items", UiListEventType::select, "on_select"));
    ASSERT_TRUE(host.set_callback("items", UiListEventType::activate, "on_activate"));
    ASSERT_TRUE(host.set_selected("items",
        {.list_id = "items", .key = "70", .index = 10, .cell = -1}, false));

    const auto next_scroll = host.move_and_activate("items", 1, 90, 0);
    ASSERT_TRUE(next_scroll);
    EXPECT_EQ(*next_scroll, 270);
    const auto selected = host.get_selected("items");
    ASSERT_TRUE(selected);
    EXPECT_EQ(selected->index, 11);
    EXPECT_EQ(selected->key, "77");

    std::vector<UiListEvent> events;
    host.drain_events([&](const UiListEvent& event) { events.push_back(event); });
    ASSERT_EQ(events.size(), 2);
    EXPECT_EQ(events[0].type, UiListEventType::select);
    EXPECT_EQ(events[1].type, UiListEventType::activate);
    EXPECT_EQ(events[1].selection.key, "77");

    EXPECT_FALSE(host.move_and_activate("missing", 1, 90, 0));
    EXPECT_FALSE(host.move_and_activate("items", 0, 90, 0));
    EXPECT_FALSE(host.move_and_activate("items", 1, 0, 0));
}

TEST(ClientVirtualComboBox, OpensAtSelectedKeyAndMaterializesBoundedRows)
{
    std::vector<VirtualComboBoxItem> items;
    for (int32_t key = 0; key < 100; ++key) {
        items.push_back({key, std::to_string(key), {}});
    }

    VirtualComboBox combobox;
    ASSERT_TRUE(combobox.open(std::move(items), 80));
    EXPECT_EQ(combobox.selected_key(), 80);

    const auto first = combobox.update(300, 0, false);
    EXPECT_TRUE(first.replace_markup);
    EXPECT_TRUE(first.set_scroll);
    EXPECT_EQ(first.scroll_top, 2'130);
    EXPECT_LE(count_markup_rows(first.markup), 18);
    EXPECT_NE(first.markup.find("data-key=\"80\""), std::string::npos);

    const auto stable = combobox.update(300, first.scroll_top, false);
    EXPECT_FALSE(stable.replace_markup);
    EXPECT_FALSE(stable.set_scroll);

    EXPECT_EQ(combobox.move_selection(-1), 79);
    EXPECT_EQ(combobox.selected_key(), 79);
}

TEST(ClientVirtualComboBox, RejectsDuplicateKeysAndCloses)
{
    VirtualComboBox combobox;
    std::vector<VirtualComboBoxItem> items{
        {1, "First", {}},
        {1, "Duplicate", {}},
    };

    EXPECT_FALSE(combobox.open(std::move(items), 1));
    EXPECT_FALSE(combobox.is_active());
    EXPECT_EQ(combobox.size(), 0);
    EXPECT_FALSE(combobox.selected_key());
}

TEST(ClientVirtualComboBox, SelectsAndCyclesOnlyExistingSparseKeys)
{
    VirtualComboBox combobox;
    std::vector<VirtualComboBoxItem> items{
        {0, "None", {}},
        {7, "7", {}},
        {119, "119", {}},
    };

    ASSERT_TRUE(combobox.open(std::move(items), 7));
    ASSERT_TRUE(combobox.select_key(119));
    EXPECT_EQ(combobox.selected_key(), 119);
    EXPECT_FALSE(combobox.select_key(8));
    EXPECT_EQ(combobox.selected_key(), 119);

    EXPECT_EQ(combobox.move_selection(1), 2);
    EXPECT_EQ(combobox.selected_key(), 119);
    EXPECT_EQ(combobox.move_selection(-1), 1);
    EXPECT_EQ(combobox.selected_key(), 7);
    EXPECT_EQ(combobox.move_selection(-4), 0);
    EXPECT_EQ(combobox.selected_key(), 0);

    combobox.hide_popup();
    EXPECT_TRUE(combobox.is_active());
    EXPECT_FALSE(combobox.popup_visible());
    EXPECT_FALSE(combobox.update(300, 0, true).replace_markup);
    EXPECT_EQ(combobox.move_selection(1), 1);
    EXPECT_EQ(combobox.selected_key(), 7);

    ASSERT_TRUE(combobox.show_popup());
    EXPECT_TRUE(combobox.popup_visible());
    EXPECT_TRUE(combobox.update(300, 0, true).replace_markup);
    combobox.invalidate_popup_render();
    EXPECT_TRUE(combobox.update(300, 0, false).replace_markup);
}

TEST(ClientVirtualComboBox, PlacesPopupWithoutChangingHostLayout)
{
    VirtualComboBox combobox;
    const VirtualComboBoxRect bounds{0, 0, 440, 900};

    const auto below = combobox.place_popup({328, 120, 112, 30}, bounds);
    EXPECT_EQ(below, (VirtualComboBoxPopupPlacement{328, 150, 112, 300, false}));

    const auto above = combobox.place_popup({328, 760, 112, 30}, bounds);
    EXPECT_EQ(above, (VirtualComboBoxPopupPlacement{328, 460, 112, 300, true}));

    const auto constrained = combobox.place_popup({420, 10, 112, 30}, {0, 0, 440, 200});
    EXPECT_EQ(constrained, (VirtualComboBoxPopupPlacement{328, 0, 112, 200, false}));

    const auto filtered_list = place_virtual_combobox_popup(
        {10, 120, 420, 30}, bounds, 300);
    EXPECT_EQ(filtered_list, (VirtualComboBoxPopupPlacement{10, 150, 420, 300, false}));

    EXPECT_EQ(place_virtual_combobox_popup(
                  {10, 120, 420, 30}, bounds, 0),
        VirtualComboBoxPopupPlacement{});
}

} // namespace nw::toolset
