#include "appearance_catalog.hpp"
#include "area_object_editor.hpp"
#include "area_tile_editor.hpp"
#include "client_input.hpp"
#include "client_metrics.hpp"
#include "command_view.hpp"
#include "item_editor_data_model.hpp"
#include "loading_view.hpp"
#include "object_document.hpp"
#include "object_edits.hpp"
#include "object_workbench_view.hpp"
#include "play_preview_view.hpp"
#include "project.hpp"
#include "project_resource_drag.hpp"
#include "rml_managed_list.hpp"
#include "rml_smalls_bridge.hpp"
#include "rml_smalls_language_binding.hpp"
#include "script_commands.hpp"
#include "shell_controller.hpp"
#include "smalls_rmlui.hpp"
#include "smalls_ui_v1.hpp"
#include "toolset_backend.hpp"
#include "virtual_list.hpp"
#include "workspace.hpp"
#include "workspace_view.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/TilesetRegistry.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Door.hpp>
#include <nw/objects/Encounter.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectComponentSystem.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Placeable.hpp>
#include <nw/objects/Sound.hpp>
#include <nw/objects/Store.hpp>
#include <nw/serialization/Serialization.hpp>
#include <nw/smalls/runtime.hpp>
#include <nw/util/scope_exit.hpp>

#include <RmlUi/Core.h>
#include <RmlUi/Core/ElementScroll.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <SDL3/SDL.h>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

class NullRenderInterface final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override
    {
        return 1;
    }

    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override { }
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override { }

    Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override { return 0; }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return 0; }
    void ReleaseTexture(Rml::TextureHandle) override { }

    void EnableScissorRegion(bool) override { }
    void SetScissorRegion(Rml::Rectanglei) override { }
};

class RmlScope {
public:
    explicit RmlScope(Rml::RenderInterface& renderer)
    {
        Rml::SetRenderInterface(&renderer);
        initialized_ = Rml::Initialise();
    }

    ~RmlScope()
    {
        if (initialized_) {
            Rml::Shutdown();
        }
        Rml::SetRenderInterface(nullptr);
    }

    [[nodiscard]] bool initialized() const noexcept { return initialized_; }

private:
    bool initialized_ = false;
};

class KernelServiceScope {
public:
    KernelServiceScope() { nw::kernel::services().start(); }
    ~KernelServiceScope()
    {
        nw::kernel::services().shutdown();
        nw::kernel::services().start();
    }
};

class CurrentPathScope {
public:
    explicit CurrentPathScope(const std::filesystem::path& path)
        : previous_{std::filesystem::current_path()}
    {
        std::filesystem::current_path(path);
    }

    ~CurrentPathScope() { std::filesystem::current_path(previous_); }

private:
    std::filesystem::path previous_;
};

class ScriptCommandHostReset {
public:
    ~ScriptCommandHostReset() { nw::toolset::script_command_host().bind(nullptr, nullptr); }
};

bool diagnostic_contains(const nw::toolset::RmlSmallsLanguageBinding& binding, std::string_view needle)
{
    const auto& diagnostics = binding.diagnostics();
    return std::any_of(diagnostics.begin(), diagnostics.end(), [needle](const auto& diagnostic) {
        return diagnostic.message.find(needle) != std::string::npos;
    });
}

Rml::ElementList bound_elements_by_class(
    Rml::ElementDocument& document, const Rml::String& class_name)
{
    Rml::ElementList elements;
    document.GetElementsByClassName(elements, class_name);
    std::erase_if(elements, [](Rml::Element* element) {
        if (!element->IsVisible(true)) {
            return true;
        }
        for (auto* ancestor = element; ancestor;
            ancestor = ancestor->GetParentNode()) {
            if (ancestor->HasAttribute("data-for")) {
                return true;
            }
        }
        return false;
    });
    return elements;
}

class RmlVirtualListAdapter final : public nw::toolset::VirtualListAdapter {
public:
    explicit RmlVirtualListAdapter(int size)
        : size_{size}
    {
    }

    [[nodiscard]] int size() const override { return size_; }
    [[nodiscard]] std::string render_row_inner(int /*index*/, bool /*selected*/) const override { return {}; }

private:
    int size_ = 0;
};

class LinebreakChangeListener final : public Rml::EventListener {
public:
    void ProcessEvent(Rml::Event& event) override
    {
        if (event.GetParameter<bool>("linebreak", false)) {
            target = event.GetTargetElement();
            ++count;
        }
    }

    Rml::Element* target = nullptr;
    int count = 0;
};

class CaptureBlurListener final : public Rml::EventListener {
public:
    void ProcessEvent(Rml::Event& event) override
    {
        target = event.GetTargetElement();
        if (auto* input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(target)) {
            value = input->GetValue();
        }
        ++count;
    }

    Rml::Element* target = nullptr;
    Rml::String value;
    int count = 0;
};

} // namespace

TEST(ClientRmlTemplates, PlayPreviewHidesPersistentShellChrome)
{
    CurrentPathScope source_root{ROLLNW_TEST_SOURCE_DIR};
    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());

    auto* context = Rml::CreateContext(
        "play-preview-shell-visibility-test", {1200, 700});
    ASSERT_NE(context, nullptr);
    auto* document = context->LoadDocumentFromMemory(R"RML(
<rml>
<head><link type="text/css" href="tools/client/ui/panel.rcss"/></head>
<body>
  <div id="panel"></div>
  <div id="bottom_dock" class="visible"></div>
  <div id="workspace_shell">
    <div id="workspace_tab_bar"></div>
    <div id="area_toolbar" class="workspace_area_toolbar"></div>
    <div id="object_workbench" class="object_workbench"></div>
  </div>
</body>
</rml>)RML");
    ASSERT_NE(document, nullptr);
    document->Show();
    context->Update();

    constexpr std::array element_ids{
        "bottom_dock",
        "workspace_tab_bar",
        "area_toolbar",
        "object_workbench",
    };
    for (const auto* id : element_ids) {
        SCOPED_TRACE(id);
        auto* element = document->GetElementById(id);
        ASSERT_NE(element, nullptr);
        EXPECT_TRUE(element->IsVisible(true));
    }

    document->SetClass("play_preview_active", true);
    context->Update();
    for (const auto* id : element_ids) {
        SCOPED_TRACE(id);
        auto* element = document->GetElementById(id);
        ASSERT_NE(element, nullptr);
        EXPECT_FALSE(element->IsVisible(true));
    }

    document->Close();
    context->Update();
    Rml::RemoveContext("play-preview-shell-visibility-test");
}

TEST(ClientPlayPreviewView, SpawnYawAndActorResolutionKeepTheExistingPolicy)
{
    using namespace nw::toolset;
    EXPECT_FLOAT_EQ(play_preview_yaw({.displacement = {1, 0, -1}}), 0);
    EXPECT_NEAR(play_preview_yaw({.displacement = {0, 1, -1}}), 1.5707963268f, 1e-6f);
    EXPECT_NEAR(play_preview_yaw({.displacement = {0, -1, -1}}), -1.5707963268f, 1e-6f);
    EXPECT_NEAR(play_preview_yaw({.displacement = {-1, 0, -1}}), 3.1415926536f, 1e-6f);
    EXPECT_EQ(play_preview_yaw({.displacement = {0, 0, -1}}), 0);
    EXPECT_EQ(play_preview_yaw({.displacement = {std::numeric_limits<float>::infinity(), 1, -1}}), 0);
    EXPECT_EQ(play_preview_yaw({.displacement = {1e-5f, 1e-5f, -1}}), 0);
    const auto valid = resolve_play_preview_actor({}, "test_data/user/development/pl_agent_001.utc");
    EXPECT_EQ(valid.actor, (nw::Resource{std::string_view{"pl_agent_001"}, nw::ResourceType::utc}));
    EXPECT_TRUE(valid.diagnostic.empty());
    const auto invalid = resolve_play_preview_actor({}, "test_data/user/development/cloth028.uti");
    EXPECT_FALSE(invalid.actor.valid());
    EXPECT_EQ(invalid.diagnostic, "Play-preview test actor must be a Creature blueprint");
    const auto missing = resolve_play_preview_actor({}, {});
    EXPECT_FALSE(missing.actor.valid());
    EXPECT_FALSE(missing.diagnostic.empty());
}

TEST(ClientPlayPreviewView, RepeatedActorPickerPreservesAndRestoresTheOriginalShellQuery)
{
    using namespace nw::toolset;
    CurrentPathScope source_root{ROLLNW_TEST_SOURCE_DIR};
    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    auto* context = Rml::CreateContext("preview-picker", {1200, 700});
    ASSERT_NE(context, nullptr);
    auto* document = context->LoadDocument("tools/client/ui/panel.rml");
    ASSERT_NE(document, nullptr);
    auto* search = rmlui_dynamic_cast<Rml::ElementFormControl*>(document->GetElementById("recent_search"));
    ASSERT_NE(search, nullptr);
    search->SetValue("Original query");
    ShellController shell;
    shell.set_showing_areas(true);
    PlayPreviewState preview;
    request_play_preview_actor(document, preview, shell, "Choose actor");
    EXPECT_TRUE(preview.selecting_actor);
    EXPECT_TRUE(shell.showing_project_tree);
    EXPECT_FALSE(shell.showing_areas);
    EXPECT_TRUE(search->GetValue().empty());
    search->SetValue("Picker query");
    request_play_preview_actor(document, preview, shell, {});
    EXPECT_EQ(preview.picker_previous_query, "Original query");
    EXPECT_EQ(search->GetValue(), "Picker query");
    ASSERT_TRUE(restore_play_preview_picker_shell(document, preview, shell));
    EXPECT_FALSE(preview.selecting_actor);
    EXPECT_TRUE(shell.showing_areas);
    EXPECT_FALSE(shell.showing_project_tree);
    EXPECT_EQ(search->GetValue(), "Original query");
    EXPECT_FALSE(restore_play_preview_picker_shell(document, preview, shell));
    document->Close();
    context->Update();
    Rml::RemoveContext("preview-picker");
}

TEST(ClientPlayPreviewView, OverlayUsesCurrentViewportPickerPlacementAndDetachedSessionStates)
{
    using namespace nw::toolset;
    auto* module = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_NE(module, nullptr);
    auto* area = module->get_area(0);
    ASSERT_NE(area, nullptr);
    PlayPreviewState preview;
    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    auto* context = Rml::CreateContext("preview-overlay", {1200, 700});
    ASSERT_NE(context, nullptr);
    auto* document = load_viewer_fps_document(*context);
    ASSERT_NE(document, nullptr);
    document->Show();
    const std::optional viewport{ClientViewportRect{.x = 12, .y = 24, .width = 800, .height = 600}};
    auto* overlay = document->GetElementById("play_preview_viewport_overlay");
    ASSERT_NE(overlay, nullptr);
    sync_play_preview_viewport_overlay(document, viewport, preview);
    context->Update();
    EXPECT_FALSE(overlay->IsVisible(true));
    preview.selecting_actor = true;
    sync_play_preview_viewport_overlay(document, viewport, preview);
    context->Update();
    EXPECT_TRUE(overlay->IsVisible(true));
    EXPECT_NE(overlay->GetInnerRML().find("Choose Creature"), std::string::npos);
    EXPECT_FLOAT_EQ(overlay->GetAbsoluteLeft(), 20);
    EXPECT_FLOAT_EQ(overlay->GetAbsoluteTop(), 32);
    preview.selecting_actor = false;
    preview.pending_actor = nw::Resource{std::string_view{"pl_agent_001"}, nw::ResourceType::utc};
    preview.placement_diagnostic = "Bad <spawn> & point";
    sync_play_preview_viewport_overlay(document, viewport, preview);
    EXPECT_NE(overlay->GetInnerRML().find("Bad &lt;spawn&gt; &amp; point"), std::string::npos);
    sync_play_preview_viewport_overlay(document, std::nullopt, preview);
    context->Update();
    EXPECT_FALSE(overlay->IsVisible(true));
    ASSERT_TRUE(start_toolset_preview(preview.session, {.area = area->handle(), .actor = preview.pending_actor, .spawn_position = module->entry_position}).ok());
    preview.pending_actor = {};
    sync_play_preview_viewport_overlay(document, viewport, preview);
    EXPECT_NE(overlay->GetInnerRML().find("F8 navigation debug"), std::string::npos);
    ASSERT_EQ(set_toolset_preview_navigation_debug(preview.session, true), PreviewStatus::ok);
    sync_play_preview_viewport_overlay(document, viewport, preview);
    EXPECT_NE(overlay->GetInnerRML().find("Nav: walkable green"), std::string::npos);
    stop_toolset_preview(preview.session);
    sync_play_preview_viewport_overlay(document, viewport, preview);
    context->Update();
    EXPECT_FALSE(overlay->IsVisible(true));
    document->Close();
    context->Update();
    Rml::RemoveContext("preview-overlay");
}

TEST(ClientRmlTemplates, InputClassificationExcludesHiddenPanelsAndFocus)
{
    CurrentPathScope source_root{ROLLNW_TEST_SOURCE_DIR};
    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    ASSERT_TRUE(Rml::LoadFontFace("tools/client/assets/fonts/inter/Inter-Regular.ttf"));
    auto* context = Rml::CreateContext("input-visibility", {1200, 700});
    ASSERT_NE(context, nullptr);
    auto* document = context->LoadDocument("tools/client/ui/panel.rml");
    ASSERT_NE(document, nullptr);
    document->Show();
    auto* panel = document->GetElementById("panel");
    auto* search = document->GetElementById("recent_search");
    ASSERT_NE(panel, nullptr);
    ASSERT_NE(search, nullptr);
    panel->SetProperty("display", "flex");
    context->Update();
    const Rml::Vector2f point{search->GetAbsoluteLeft() + 2.0f,
        search->GetAbsoluteTop() + 2.0f};
    ASSERT_TRUE(search->Focus());
    EXPECT_TRUE(nw::toolset::point_within_element(document, "panel", point));
    EXPECT_TRUE(nw::toolset::focused_text_input(context));
    EXPECT_TRUE(nw::toolset::focused_element_has_id(context, "recent_search"));

    panel->SetProperty("display", "none");
    context->Update();
    EXPECT_FALSE(nw::toolset::point_within_element(document, "panel", point));
    EXPECT_FALSE(nw::toolset::focused_text_input(context));
    EXPECT_FALSE(nw::toolset::focused_element_has_id(context, "recent_search"));

    panel->SetProperty("display", "flex");
    context->Update();
    ASSERT_TRUE(search->Focus());
    EXPECT_TRUE(nw::toolset::point_within_element(document, "panel", point));
    EXPECT_TRUE(nw::toolset::focused_text_input(context));
    EXPECT_FALSE(nw::toolset::point_within_element(document, "missing", point));
    EXPECT_FALSE(nw::toolset::point_within_element(nullptr, "panel", point));
    EXPECT_FALSE(nw::toolset::focused_text_input(nullptr));
    EXPECT_FALSE(nw::toolset::focused_element_has_id(context, nullptr));
    document->Close();
    context->Update();
    Rml::RemoveContext("input-visibility");
}

TEST(ClientRmlTemplates, ItemWorkbenchExpandsBoundedAppearanceStructure)
{
    CurrentPathScope source_root{ROLLNW_TEST_SOURCE_DIR};
    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    ASSERT_TRUE(Rml::LoadFontFace(
        "tools/client/assets/fonts/inter/Inter-Regular.ttf"));

    const std::filesystem::path ui_resource_path = "tools/client/ui";
    const std::string source = "<rml><head>"
                               "<link type=\"text/css\" href=\""
        + (ui_resource_path / "panel.rcss").generic_string()
        + "\"/>"
          "<link type=\"text/css\" href=\""
        + (ui_resource_path / "item_editor.rcss").generic_string()
        + "\"/>"
          "<link type=\"text/template\" href=\""
        + (ui_resource_path / "item_editor.rml").generic_string()
        + "\"/><style>body, button, input { font-family: Inter; font-weight: normal; }</style>"
          "</head><body><div id=\"item-preview-body\" "
          "class=\"workspace_preview_body data_workbench_only\">"
          "<div id=\"workspace_viewer_viewport\" "
          "class=\"workspace_viewer_viewport\"></div>"
          "<template src=\"item-workbench\"></template></div>"
          "<div id=\"object_variable_warning_tooltip\"></div></body></rml>";

    auto* context = Rml::CreateContext("item-workbench-template-test", {1200, 700});
    ASSERT_NE(context, nullptr);
    nw::toolset::ItemEditorDataModel item_model;
    ASSERT_TRUE(item_model.initialize(*context,
        [](std::string_view, std::span<const int32_t>, std::string&) {
            return true;
        }));
    auto* document = context->LoadDocumentFromMemory(
        source, "item_workbench_template_test.rml");
    ASSERT_NE(document, nullptr);

    auto* details = document->GetElementById("item_surface_details");
    ASSERT_NE(details, nullptr);
    EXPECT_TRUE(details->IsClassSet("smalls_refresh"));
    EXPECT_EQ(document->GetElementById("item_surface_properties"), nullptr);
    EXPECT_NE(document->GetElementById("item_surface_appearance"), nullptr);
    EXPECT_NE(document->GetElementById("item_surface_item_properties"), nullptr);
    EXPECT_NE(document->GetElementById("item_surface_inventory"), nullptr);
    EXPECT_NE(document->GetElementById("object_workbench_tabs"), nullptr);
    EXPECT_NE(document->GetElementById("object_workbench_tab_track"), nullptr);
    EXPECT_NE(document->GetElementById("object_workbench_tabs_previous"), nullptr);
    EXPECT_NE(document->GetElementById("object_workbench_tabs_next"), nullptr);
    auto* variable_surface = document->GetElementById("item_surface_variables");
    ASSERT_NE(variable_surface, nullptr);
    Rml::ElementList warning_headers;
    document->GetElementsByClassName(
        warning_headers, "object_variable_header_warning");
    EXPECT_TRUE(warning_headers.empty());
    auto* variable_rows = document->GetElementById("object_variable_rows");
    ASSERT_NE(variable_rows, nullptr);
    variable_surface->SetClass("active", true);
    variable_rows->SetInnerRML(
        "<div class='object_variable_row'><div class='object_variable_cells'>"
        "<div class='object_variable_field object_variable_name_field'>"
        "<input class='object_variable_name' type='text' value='Count'/></div>"
        "<button class='object_variable_type' type='button'>String</button>"
        "<div id='warning-field' class='object_variable_field "
        "object_variable_value_field warning_type'>"
        "<span id='warning-icon' class='object_variable_field_warning' "
        "title='This string looks like an integer' "
        "data-tooltip='This string looks like an integer'>!</span>"
        "<input id='warning-input' class='object_variable_value' type='text' "
        "value='1'/></div>"
        "<button class='object_variable_remove' type='button'>&#215;</button>"
        "</div></div>");
    auto* appearance = document->GetElementById("item_appearance_dynamic");
    ASSERT_NE(appearance, nullptr);
    EXPECT_FALSE(appearance->IsClassSet("smalls_refresh"));
    auto* property_tree = document->GetElementById("property_tree_rows");
    ASSERT_NE(property_tree, nullptr);
    EXPECT_EQ(property_tree->GetParentNode(), details);
    auto* property_surface = document->GetElementById("item_surface_item_properties");
    ASSERT_NE(property_surface, nullptr);
    property_surface->SetClass("active", true);
    auto* property_catalog = document->GetElementById("item_property_catalog");
    ASSERT_NE(property_catalog, nullptr);
    auto* property_selector = document->GetElementById(
        "item_property_option_selector");
    ASSERT_NE(property_selector, nullptr);
    EXPECT_EQ(property_selector->GetParentNode(), property_catalog);
    auto* available_rows = document->GetElementById("item_available_property_rows");
    ASSERT_NE(available_rows, nullptr);
    available_rows->SetInnerRML(
        "<div class='managed_list_row'><span id='available-property-cell' "
        "class='managed_list_cell cell_0'>Damage Bonus</span></div>");
    auto* applied_rows = document->GetElementById("item_applied_property_rows");
    ASSERT_NE(applied_rows, nullptr);
    applied_rows->SetInnerRML(
        "<div class='managed_list_row'>"
        "<span id='applied-property-cell' class='managed_list_cell cell_0'>Damage Bonus</span>"
        "<span id='applied-subtype-cell' class='managed_list_cell cell_1'>Fire</span>"
        "<span id='applied-param-cell' class='managed_list_cell cell_2'>1d6</span>"
        "<span id='applied-cost-cell' class='managed_list_cell cell_3'>3</span>"
        "</div>");
    document->Show();
    context->Update();

    auto* preview_body = document->GetElementById("item-preview-body");
    auto* viewport = document->GetElementById("workspace_viewer_viewport");
    auto* workbench = document->GetElementById("object_workbench");
    ASSERT_NE(preview_body, nullptr);
    ASSERT_NE(viewport, nullptr);
    ASSERT_NE(workbench, nullptr);
    EXPECT_EQ(viewport->GetOffsetWidth(), 0.0f);
    EXPECT_GT(workbench->GetOffsetWidth(), 1000.0f);
    EXPECT_GT(property_catalog->GetOffsetWidth(), 300.0f);
    auto* applied_pane = document->QuerySelector(".item_property_pane.applied");
    ASSERT_NE(applied_pane, nullptr);
    EXPECT_GT(applied_pane->GetOffsetWidth(), property_catalog->GetOffsetWidth());

    auto* available_cell = document->GetElementById("available-property-cell");
    ASSERT_NE(available_cell, nullptr);
    EXPECT_GT(available_cell->GetOffsetWidth(), 0.0f);
    EXPECT_GT(available_cell->GetOffsetHeight(), 0.0f);
    auto* available_text = rmlui_dynamic_cast<Rml::ElementText*>(available_cell->GetFirstChild());
    ASSERT_NE(available_text, nullptr);
    ASSERT_FALSE(available_text->GetLines().empty());
    EXPECT_EQ(available_text->GetLines().front().text, "Damage Bonus");
    auto* applied_property = document->GetElementById("applied-property-cell");
    auto* applied_subtype = document->GetElementById("applied-subtype-cell");
    auto* applied_param = document->GetElementById("applied-param-cell");
    auto* applied_cost = document->GetElementById("applied-cost-cell");
    ASSERT_NE(applied_property, nullptr);
    ASSERT_NE(applied_subtype, nullptr);
    ASSERT_NE(applied_param, nullptr);
    ASSERT_NE(applied_cost, nullptr);
    EXPECT_GT(applied_property->GetOffsetWidth(), applied_subtype->GetOffsetWidth());
    EXPECT_GT(applied_subtype->GetAbsoluteLeft(), applied_property->GetAbsoluteLeft());
    EXPECT_GT(applied_param->GetAbsoluteLeft(), applied_subtype->GetAbsoluteLeft());
    EXPECT_GT(applied_cost->GetAbsoluteLeft(), applied_param->GetAbsoluteLeft());

    auto* property_option_rows = document->GetElementById(
        "item_property_option_rows");
    ASSERT_NE(property_option_rows, nullptr);
    property_option_rows->SetInnerRML(
        "<div class='managed_list_row'><span id='first-property-option' "
        "class='managed_list_cell cell_0'>Acid</span></div>"
        "<div class='managed_list_row selected'><span id='second-property-option' "
        "class='managed_list_cell cell_0 selected'>Cold</span></div>");
    property_selector->SetClass("active", true);
    context->Update();
    auto* first_property_option = document->GetElementById(
        "first-property-option");
    auto* second_property_option = document->GetElementById(
        "second-property-option");
    ASSERT_NE(first_property_option, nullptr);
    ASSERT_NE(second_property_option, nullptr);
    EXPECT_GT(first_property_option->GetOffsetWidth(), 300.0f);
    EXPECT_GT(first_property_option->GetOffsetHeight(), 0.0f);
    EXPECT_GE(second_property_option->GetAbsoluteTop(),
        first_property_option->GetAbsoluteTop()
            + first_property_option->GetOffsetHeight());

    auto* warning_field = document->GetElementById("warning-field");
    auto* warning_icon = document->GetElementById("warning-icon");
    auto* warning_input = document->GetElementById("warning-input");
    ASSERT_NE(warning_field, nullptr);
    ASSERT_NE(warning_icon, nullptr);
    ASSERT_NE(warning_input, nullptr);
    EXPECT_GT(warning_field->GetOffsetWidth(), 0.0f);
    EXPECT_GT(warning_icon->GetOffsetWidth(), 0.0f);
    EXPECT_GT(warning_icon->GetOffsetHeight(), 0.0f);
    EXPECT_GE(warning_icon->GetAbsoluteLeft(), warning_field->GetAbsoluteLeft());
    EXPECT_LE(warning_icon->GetAbsoluteLeft() + warning_icon->GetOffsetWidth(),
        warning_input->GetAbsoluteLeft());
    EXPECT_LT(warning_input->GetAbsoluteLeft(),
        warning_field->GetAbsoluteLeft() + warning_field->GetOffsetWidth());
    EXPECT_EQ(warning_icon->GetAttribute<Rml::String>("data-tooltip", ""),
        "This string looks like an integer");
    auto* warning_tooltip = document->GetElementById("object_variable_warning_tooltip");
    ASSERT_NE(warning_tooltip, nullptr);
    warning_tooltip->SetInnerRML(
        warning_icon->GetAttribute<Rml::String>("data-tooltip", ""));
    warning_tooltip->SetProperty("display", "block");
    warning_tooltip->SetProperty("width", "280px");
    warning_tooltip->SetProperty("left", "500px");
    warning_tooltip->SetProperty("top", "100px");
    context->Update();
    EXPECT_GT(warning_tooltip->GetOffsetWidth(), 0.0f);
    EXPECT_GT(warning_tooltip->GetOffsetHeight(), 0.0f);
    EXPECT_EQ(warning_tooltip->GetInnerRML(),
        "This string looks like an integer");

    LinebreakChangeListener linebreak_listener;
    context->AddEventListener("change", &linebreak_listener, false);
    auto* warning_control = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(
        warning_input);
    ASSERT_NE(warning_control, nullptr);
    warning_control->SetValue("2");
    warning_control->Focus();
    context->ProcessKeyDown(Rml::Input::KI_RETURN, 0);
    EXPECT_EQ(linebreak_listener.count, 1);
    EXPECT_EQ(linebreak_listener.target, warning_control);
    EXPECT_EQ(warning_control->GetValue(), "2");
    context->RemoveEventListener("change", &linebreak_listener, false);

    CaptureBlurListener blur_listener;
    context->AddEventListener("blur", &blur_listener, true);
    warning_control->Blur();
    EXPECT_EQ(blur_listener.count, 1);
    EXPECT_EQ(blur_listener.target, warning_control);
    EXPECT_EQ(blur_listener.value, "2");
    context->RemoveEventListener("blur", &blur_listener, true);

    auto model_rows = bound_elements_by_class(*document, "item_model_row");
    EXPECT_TRUE(model_rows.empty());

    document->Close();
    item_model.shutdown();
    Rml::RemoveContext("item-workbench-template-test");
}

TEST(ClientRmlTemplates, ItemAppearanceModelOwnsRowsEventsAndFocus)
{
    CurrentPathScope source_root{ROLLNW_TEST_SOURCE_DIR};
    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    ASSERT_TRUE(Rml::LoadFontFace(
        "tools/client/assets/fonts/inter/Inter-Regular.ttf"));

    auto* context = Rml::CreateContext(
        "item-appearance-data-model-test", {800, 600});
    ASSERT_NE(context, nullptr);
    bool outer_live = true;
    auto outer_model = context->CreateDataModel("toolset_presentation");
    ASSERT_TRUE(static_cast<bool>(outer_model));
    ASSERT_TRUE(outer_model.Bind("outer_live", &outer_live));

    nw::ObjectHandle object;
    object.type = nw::ObjectType::item;
    std::vector<nw::toolset::ItemEditorPart> parts{
        {
            .part = 2,
            .value = 37,
            .label = "Composite",
            .detail = "Model 3, variation 7",
            .split_model_variation = true,
        },
        {
            .part = 4,
            .value = 9,
            .label = "Layer",
            .detail = "Layer Nine",
            .per_part_colors = true,
        },
    };
    std::vector<nw::toolset::ItemEditorColor> colors{
        {
            .part = 4,
            .color = 0,
            .value = 17,
            .stored_value = 17,
            .palette = 0,
            .label = "Cloth 1",
        },
        {
            .part = 4,
            .color = 1,
            .value = 34,
            .stored_value = 255,
            .palette = 1,
            .label = "Leather 1",
            .inherited = true,
        },
        {
            .part = 4,
            .color = 2,
            .value = 176,
            .stored_value = 176,
            .palette = 2,
            .label = "Malformed",
        },
        {
            .part = 4,
            .color = 1,
            .value = 35,
            .stored_value = 35,
            .palette = 1,
            .label = "Duplicate",
        },
    };
    nw::toolset::ItemEditorAppearanceInput input{
        .object = object,
        .parts = parts,
        .colors = colors,
    };

    struct Invocation {
        std::string command;
        std::vector<int32_t> arguments;
    };
    std::vector<Invocation> invocations;
    nw::toolset::ItemEditorDataModel item_model;
    ASSERT_TRUE(item_model.initialize(*context,
        [&](std::string_view command, std::span<const int32_t> arguments,
            std::string&) {
            invocations.push_back({std::string{command},
                std::vector<int32_t>{arguments.begin(), arguments.end()}});
            if (command == "toolset.item.appearance.open_model") {
                if (input.mode
                        == nw::toolset::ItemEditorAppearanceMode::model
                    && input.model_part == arguments[0]
                    && input.model_axis == arguments[1]) {
                    input.mode
                        = nw::toolset::ItemEditorAppearanceMode::main;
                } else {
                    input.mode
                        = nw::toolset::ItemEditorAppearanceMode::model;
                    input.model_part = arguments[0];
                    input.model_axis = arguments[1];
                }
            } else if (command == "toolset.item.appearance.close") {
                input.mode = nw::toolset::ItemEditorAppearanceMode::main;
            } else if (command == "toolset.item.appearance.open_color") {
                input.mode = nw::toolset::ItemEditorAppearanceMode::color;
                input.color_part = arguments[0];
                input.color_channel = arguments[1];
            } else if (command
                == "toolset.item.appearance.select_color") {
                input.color_channel = arguments[0];
            } else if (command == "toolset.item.appearance.apply_color") {
                const auto selected = std::ranges::find_if(colors,
                    [&](const auto& row) {
                        return row.part == input.color_part
                            && row.color == input.color_channel;
                    });
                if (selected != colors.end()) {
                    selected->value = arguments[0];
                }
            }
            item_model.refresh(input);
            return true;
        }));
    item_model.refresh(input);

    const std::filesystem::path ui_resource_path = "tools/client/ui";
    const std::string source = "<rml><head>"
                               "<link type=\"text/css\" href=\""
        + (ui_resource_path / "panel.rcss").generic_string()
        + "\"/>"
          "<link type=\"text/css\" href=\""
        + (ui_resource_path / "item_editor.rcss").generic_string()
        + "\"/>"
          "<link type=\"text/template\" href=\""
        + (ui_resource_path / "item_editor.rml").generic_string()
        + "\"/><style>body, button, input { font-family: Inter; font-weight: normal; }</style>"
          "</head><body data-model=\"toolset_presentation\">"
          "<template src=\"item-workbench\"></template></body></rml>";
    auto* document = context->LoadDocumentFromMemory(
        source, "item_appearance_data_model_test.rml");
    ASSERT_NE(document, nullptr);
    document->Show();
    context->Update();
    auto* appearance_surface = document->GetElementById(
        "item_surface_appearance");
    ASSERT_NE(appearance_surface, nullptr);
    appearance_surface->SetClass("active", true);
    context->Update();

    auto model_rows = bound_elements_by_class(*document, "item_model_row");
    ASSERT_EQ(model_rows.size(), 2);
    auto color_fields = bound_elements_by_class(*document, "item_color_field");
    ASSERT_EQ(color_fields.size(), 2);
    auto model_fields = bound_elements_by_class(*document, "item_model_field");
    ASSERT_EQ(model_fields.size(), 3);
    auto composite_labels = bound_elements_by_class(
        *document, "item_composite_model_label");
    ASSERT_EQ(composite_labels.size(), 2);
    EXPECT_EQ(composite_labels[0]->GetInnerRML(), "Model");
    EXPECT_EQ(composite_labels[1]->GetInnerRML(), "Variation");

    const auto layer_model = std::ranges::find_if(model_fields,
        [](const Rml::Element* element) {
            return element->GetInnerRML().find("Layer Nine")
                != Rml::String::npos;
        });
    ASSERT_NE(layer_model, model_fields.end());
    ASSERT_TRUE((*layer_model)->DispatchEvent("click", {}));
    ASSERT_EQ(invocations.size(), 1);
    EXPECT_EQ(invocations.back().command,
        "toolset.item.appearance.open_model");
    EXPECT_EQ(invocations.back().arguments, (std::vector<int32_t>{4, 0}));
    context->Update();
    auto* option_rows = document->GetElementById("item_option_rows");
    ASSERT_NE(option_rows, nullptr);
    EXPECT_TRUE(item_model.apply_pending_focus(document));
    auto* focused_model = document->GetElementById("item_model_field_4_0");
    ASSERT_NE(focused_model, nullptr);
    EXPECT_EQ(context->GetFocusElement(), focused_model);
    EXPECT_TRUE(focused_model->IsClassSet("combobox_field"));
    EXPECT_TRUE(focused_model->IsClassSet("managed_list_cycle"));
    EXPECT_TRUE(focused_model->IsClassSet("open"));
    EXPECT_EQ(focused_model->GetAttribute<Rml::String>(
                  "data-focus-after-activate", ""),
        "item_model_field_4_0");
    EXPECT_EQ(option_rows->GetAttribute<Rml::String>(
                  "data-focus-after-activate", ""),
        "item_model_field_4_0");
    EXPECT_NE(document->GetElementById("item_appearance_main"), nullptr);
    auto* model_dropdown = document->GetElementById(
        "item_model_dropdown");
    ASSERT_NE(model_dropdown, nullptr);
    EXPECT_TRUE(model_dropdown->IsVisible(true));
    EXPECT_TRUE(model_dropdown->IsClassSet("combobox_popup"));
    EXPECT_TRUE(model_dropdown->IsClassSet("combobox_options"));
    EXPECT_EQ(model_dropdown->GetAttribute<Rml::String>(
                  "data-anchor-element-class", ""),
        "item_model_dropdown_anchor");
    auto model_anchors = bound_elements_by_class(
        *document, "item_model_dropdown_anchor");
    ASSERT_EQ(model_anchors.size(), 1);
    EXPECT_TRUE(model_anchors.front()->IsVisible(true));
    (void)nw::toolset::position_managed_list_popups(document);
    option_rows->SetInnerRML(
        "<div class='managed_list_row' data-list-id='item.appearance.models' data-index='0'>"
        "<span class='managed_list_cell cell_0' data-cell='0'>Model 6</span>"
        "<span class='managed_list_cell cell_1' data-cell='1'>wplss_t_031</span>"
        "</div>");
    context->Update();
    Rml::ElementList option_labels;
    Rml::ElementList option_details;
    option_rows->GetElementsByClassName(option_labels, "cell_0");
    option_rows->GetElementsByClassName(option_details, "cell_1");
    ASSERT_EQ(option_labels.size(), 1u);
    ASSERT_EQ(option_details.size(), 1u);
    EXPECT_GT(option_labels.front()->GetOffsetWidth(), 20.0f);
    EXPECT_GT(option_details.front()->GetOffsetWidth(), 40.0f);

    model_fields = bound_elements_by_class(*document, "item_model_field");
    const auto open_layer_model = std::ranges::find_if(model_fields,
        [](const Rml::Element* element) {
            return element->GetInnerRML().find("Layer Nine")
                != Rml::String::npos;
        });
    ASSERT_NE(open_layer_model, model_fields.end());
    ASSERT_TRUE((*open_layer_model)->DispatchEvent("click", {}));
    context->Update();
    EXPECT_EQ(invocations.back().command,
        "toolset.item.appearance.open_model");
    EXPECT_EQ(invocations.back().arguments, (std::vector<int32_t>{4, 0}));
    model_dropdown = document->GetElementById("item_model_dropdown");
    ASSERT_NE(model_dropdown, nullptr);
    EXPECT_FALSE(model_dropdown->IsVisible(true));
    focused_model = document->GetElementById("item_model_field_4_0");
    ASSERT_NE(focused_model, nullptr);
    EXPECT_TRUE(focused_model->IsClassSet("managed_list_cycle"));
    EXPECT_FALSE(focused_model->IsClassSet("open"));
    EXPECT_TRUE(item_model.apply_pending_focus(document));
    EXPECT_EQ(context->GetFocusElement(), focused_model);
    focused_model->Blur();
    item_model.request_model_focus();
    item_model.refresh(input);
    context->Update();
    ASSERT_TRUE(item_model.apply_pending_focus(document));
    focused_model = document->GetElementById("item_model_field_4_0");
    ASSERT_NE(focused_model, nullptr);
    EXPECT_EQ(context->GetFocusElement(), focused_model);
    color_fields = bound_elements_by_class(*document, "item_color_field");
    ASSERT_EQ(color_fields.size(), 2);
    ASSERT_TRUE(color_fields.front()->DispatchEvent("click", {}));
    ASSERT_EQ(invocations.back().command,
        "toolset.item.appearance.open_color");
    EXPECT_EQ(invocations.back().arguments, (std::vector<int32_t>{4, 0}));
    context->Update();

    auto channels = bound_elements_by_class(*document, "item_color_channel");
    ASSERT_EQ(channels.size(), 2);
    auto palette_cells = bound_elements_by_class(
        *document, "item_color_palette_cell");
    ASSERT_EQ(palette_cells.size(),
        static_cast<size_t>(nw::toolset::item_editor_palette_cell_count));
    auto* color_close = document->GetElementById(
        "item_color_selector_close");
    ASSERT_NE(color_close, nullptr);
    EXPECT_TRUE(item_model.apply_pending_focus(document));
    EXPECT_EQ(context->GetFocusElement(), color_close);

    ASSERT_TRUE(channels[1]->DispatchEvent("click", {}));
    context->Update();
    EXPECT_EQ(invocations.back().command,
        "toolset.item.appearance.select_color");
    EXPECT_EQ(invocations.back().arguments, (std::vector<int32_t>{1}));
    palette_cells = bound_elements_by_class(
        *document, "item_color_palette_cell");
    ASSERT_GT(palette_cells.size(), 42);
    ASSERT_TRUE(palette_cells[42]->DispatchEvent("click", {}));
    context->Update();
    EXPECT_EQ(invocations.back().command,
        "toolset.item.appearance.apply_color");
    EXPECT_EQ(invocations.back().arguments, (std::vector<int32_t>{42}));
    auto selections = bound_elements_by_class(
        *document, "item_color_selection");
    ASSERT_EQ(selections.size(), 1);
    auto* selection = selections.front();
    ASSERT_NE(selection->GetProperty("left"), nullptr);
    ASSERT_NE(selection->GetProperty("top"), nullptr);
    EXPECT_EQ(selection->GetProperty("left")->ToString(), "240px");
    EXPECT_EQ(selection->GetProperty("top")->ToString(), "48px");

    input.color_channel = 2;
    item_model.refresh(input);
    context->Update();
    const auto appearance_rml = document->GetElementById(
                                            "item_appearance_dynamic")
                                    ->GetInnerRML();
    EXPECT_NE(appearance_rml.find("Item color data is unavailable."),
        Rml::String::npos);

    document->Close();
    item_model.shutdown();
    Rml::RemoveContext("item-appearance-data-model-test");
}

TEST(ClientRmlTemplates, DoorWorkbenchExpandsNativeAppearanceStructure)
{
    CurrentPathScope source_root{ROLLNW_TEST_SOURCE_DIR};
    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    ASSERT_TRUE(Rml::LoadFontFace(
        "tools/client/assets/fonts/inter/Inter-Regular.ttf"));

    const std::filesystem::path ui_resource_path = "tools/client/ui";
    const std::string source = "<rml><head>"
                               "<link type=\"text/css\" href=\""
        + (ui_resource_path / "panel.rcss").generic_string()
        + "\"/>"
          "<link type=\"text/css\" href=\""
        + (ui_resource_path / "door_editor.rcss").generic_string()
        + "\"/>"
          "<link type=\"text/template\" href=\""
        + (ui_resource_path / "door_editor.rml").generic_string()
        + "\"/><style>body, button, input { font-family: Inter; font-weight: normal; }</style>"
          "</head><body><template src=\"door-workbench\"></template>"
          "<div id=\"object_variable_warning_tooltip\"></div></body></rml>";

    auto* context = Rml::CreateContext(
        "door-workbench-template-test", {800, 600});
    ASSERT_NE(context, nullptr);
    auto* document = context->LoadDocumentFromMemory(
        source, "door_workbench_template_test.rml");
    ASSERT_NE(document, nullptr);

    auto* details = document->GetElementById("door_surface_details");
    auto* variables = document->GetElementById("door_surface_variables");
    auto* appearance = document->GetElementById("door_surface_appearance");
    auto* appearance_dynamic = document->GetElementById(
        "door_appearance_dynamic");
    ASSERT_NE(details, nullptr);
    ASSERT_NE(variables, nullptr);
    ASSERT_NE(appearance, nullptr);
    ASSERT_NE(appearance_dynamic, nullptr);
    EXPECT_FALSE(appearance_dynamic->IsClassSet("smalls_refresh"));
    EXPECT_NE(document->GetElementById("property_tree_rows"), nullptr);
    EXPECT_NE(document->GetElementById("object_variable_rows"), nullptr);
    EXPECT_NE(document->GetElementById("object_workbench_tabs"), nullptr);
    EXPECT_NE(document->GetElementById("object_workbench_tab_track"), nullptr);
    EXPECT_NE(document->GetElementById("object_workbench_tabs_previous"), nullptr);
    EXPECT_NE(document->GetElementById("object_workbench_tabs_next"), nullptr);

    details->SetClass("active", false);
    appearance->SetClass("active", true);
    appearance_dynamic->SetInnerRML(
        "<div class='door_appearance_editor active'>"
        "<div id='door-layout-actions' class='door_appearance_actions'>"
        "<button class='door_appearance_cycle_button'>&#x2039;</button>"
        "<button class='door_appearance_open'>Choose Appearance</button>"
        "<button class='door_appearance_cycle_button'>&#x203a;</button>"
        "</div></div>");
    document->Show();
    context->Update();
    auto* actions = document->GetElementById("door-layout-actions");
    ASSERT_NE(actions, nullptr);
    EXPECT_GT(actions->GetOffsetWidth(), 0.0f);
    EXPECT_NEAR(actions->GetAbsoluteLeft() + actions->GetOffsetWidth() * 0.5f,
        appearance_dynamic->GetAbsoluteLeft()
            + appearance_dynamic->GetOffsetWidth() * 0.5f,
        0.5f);

    appearance_dynamic->SetInnerRML(
        "<div class='door_appearance_selector active'>"
        "<div class='door_appearance_search_row'>"
        "<input id='door-layout-search' class='door_appearance_search' type='text' />"
        "</div>"
        "<div id='door-layout-rows' class='door_appearance_rows'>"
        "<div id='door-layout-row' class='managed_list_row'>"
        "<span id='door-layout-name' class='managed_list_cell cell_0'>Wall Door</span>"
        "</div></div></div>");
    document->Show();
    context->Update();
    EXPECT_GT(appearance->GetOffsetWidth(), 0.0f);
    auto* search = document->GetElementById("door-layout-search");
    auto* layout_rows = document->GetElementById("door-layout-rows");
    auto* layout_row = document->GetElementById("door-layout-row");
    auto* name = document->GetElementById("door-layout-name");
    ASSERT_NE(search, nullptr);
    ASSERT_NE(layout_rows, nullptr);
    ASSERT_NE(layout_row, nullptr);
    ASSERT_NE(name, nullptr);
    EXPECT_GT(search->GetOffsetWidth(), 0.0f);
    EXPECT_EQ(search->GetOffsetHeight(), 28.0f);
    ASSERT_NE(search->GetProperty("line-height"), nullptr);
    EXPECT_EQ(search->GetProperty("line-height")->ToString(), "28px");
    EXPECT_GT(layout_row->GetOffsetWidth(), 0.0f);
    EXPECT_EQ(layout_row->GetOffsetHeight(), 34.0f);
    EXPECT_FLOAT_EQ(name->GetOffsetWidth(), layout_row->GetOffsetWidth());

    document->Close();
    context->Update();
    Rml::RemoveContext("door-workbench-template-test");
}

TEST(ClientRmlTemplates, PlaceableWorkbenchExpandsNativeAppearanceStructure)
{
    CurrentPathScope source_root{ROLLNW_TEST_SOURCE_DIR};
    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    ASSERT_TRUE(Rml::LoadFontFace(
        "tools/client/assets/fonts/inter/Inter-Regular.ttf"));

    const std::filesystem::path ui_resource_path = "tools/client/ui";
    const std::string source = "<rml><head>"
                               "<link type=\"text/css\" href=\""
        + (ui_resource_path / "panel.rcss").generic_string()
        + "\"/>"
          "<link type=\"text/css\" href=\""
        + (ui_resource_path / "placeable_editor.rcss").generic_string()
        + "\"/>"
          "<link type=\"text/template\" href=\""
        + (ui_resource_path / "placeable_editor.rml").generic_string()
        + "\"/><style>body, button, input { font-family: Inter; font-weight: normal; }</style>"
          "</head><body><template src=\"placeable-workbench\"></template>"
          "<div id=\"object_variable_warning_tooltip\"></div></body></rml>";

    auto* context = Rml::CreateContext(
        "placeable-workbench-template-test", {800, 600});
    ASSERT_NE(context, nullptr);
    auto* document = context->LoadDocumentFromMemory(
        source, "placeable_workbench_template_test.rml");
    ASSERT_NE(document, nullptr);

    auto* details = document->GetElementById("placeable_surface_details");
    auto* variables = document->GetElementById("placeable_surface_variables");
    auto* appearance = document->GetElementById("placeable_surface_appearance");
    auto* inventory = document->GetElementById("placeable_surface_inventory");
    auto* appearance_dynamic = document->GetElementById(
        "placeable_appearance_dynamic");
    ASSERT_NE(details, nullptr);
    ASSERT_NE(variables, nullptr);
    ASSERT_NE(appearance, nullptr);
    ASSERT_NE(inventory, nullptr);
    ASSERT_NE(appearance_dynamic, nullptr);
    EXPECT_FALSE(appearance_dynamic->IsClassSet("smalls_refresh"));
    EXPECT_NE(document->GetElementById("property_tree_rows"), nullptr);
    EXPECT_NE(document->GetElementById("object_variable_rows"), nullptr);
    EXPECT_NE(document->GetElementById("placeable_inventory_dynamic"), nullptr);
    EXPECT_NE(document->GetElementById("object_workbench_tabs"), nullptr);
    EXPECT_NE(document->GetElementById("object_workbench_tab_track"), nullptr);
    EXPECT_NE(document->GetElementById("object_workbench_tabs_previous"), nullptr);
    EXPECT_NE(document->GetElementById("object_workbench_tabs_next"), nullptr);

    details->SetClass("active", false);
    appearance->SetClass("active", true);
    appearance_dynamic->SetInnerRML(
        "<div class='placeable_appearance_editor active'>"
        "<div id='placeable-layout-actions' class='placeable_appearance_actions'>"
        "<button class='placeable_appearance_cycle_button'>&#x2039;</button>"
        "<button class='placeable_appearance_open'>Choose Appearance</button>"
        "<button class='placeable_appearance_cycle_button'>&#x203a;</button>"
        "</div></div>");
    document->Show();
    context->Update();
    auto* actions = document->GetElementById("placeable-layout-actions");
    ASSERT_NE(actions, nullptr);
    EXPECT_GT(actions->GetOffsetWidth(), 0.0f);
    EXPECT_NEAR(actions->GetAbsoluteLeft() + actions->GetOffsetWidth() * 0.5f,
        appearance_dynamic->GetAbsoluteLeft()
            + appearance_dynamic->GetOffsetWidth() * 0.5f,
        0.5f);

    appearance_dynamic->SetInnerRML(
        "<div class='placeable_appearance_selector active'>"
        "<div class='placeable_appearance_search_row'>"
        "<input id='placeable-layout-search' class='placeable_appearance_search' type='text' />"
        "</div>"
        "<div id='placeable-layout-rows' class='placeable_appearance_rows'>"
        "<div id='placeable-layout-row' class='managed_list_row'>"
        "<span id='placeable-layout-name' class='managed_list_cell cell_0'>Chest</span>"
        "<span class='managed_list_cell cell_1'>plc_c01</span>"
        "</div></div></div>");
    document->Show();
    context->Update();
    auto* search = document->GetElementById("placeable-layout-search");
    auto* layout_row = document->GetElementById("placeable-layout-row");
    auto* name = document->GetElementById("placeable-layout-name");
    ASSERT_NE(search, nullptr);
    ASSERT_NE(layout_row, nullptr);
    ASSERT_NE(name, nullptr);
    EXPECT_EQ(search->GetOffsetHeight(), 28.0f);
    EXPECT_EQ(layout_row->GetOffsetHeight(), 34.0f);
    EXPECT_GT(name->GetOffsetWidth(), 0.0f);

    document->Close();
    context->Update();
    Rml::RemoveContext("placeable-workbench-template-test");
}

TEST(ClientRmlTemplates, CreatureWorkbenchOwnsBodyPartListStructure)
{
    CurrentPathScope source_root{ROLLNW_TEST_SOURCE_DIR};
    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    ASSERT_TRUE(Rml::LoadFontFace(
        "tools/client/assets/fonts/inter/Inter-Regular.ttf"));

    const std::string source = "<rml><head>"
                               "<link type=\"text/css\" href=\"tools/client/ui/panel.rcss\"/>"
                               "<link type=\"text/css\" href=\"tools/client/ui/creature_appearance.rcss\"/>"
                               "<link type=\"text/template\" href=\""
                               "tools/client/ui/creature_editor.rml\"/>"
                               "<style>body { font-family: Inter; }"
                               ".body_part_rows .managed_list_cell.cell_1 {"
                               "font-family: Inter; font-weight: normal; }"
                               "#body_part_option_popup .managed_list_cell {"
                               "font-family: Inter; font-weight: normal; }</style></head>"
                               "<body><template src=\"creature-workbench\"></template></body></rml>";
    auto* context = Rml::CreateContext(
        "creature-workbench-template-test", {800, 600});
    ASSERT_NE(context, nullptr);
    auto* document = context->LoadDocumentFromMemory(
        source, "creature_workbench_template_test.rml");
    ASSERT_NE(document, nullptr);

    auto* rows = document->GetElementById("body_part_rows");
    auto* editor = document->GetElementById("body_part_editor");
    auto* popup = document->GetElementById("body_part_option_popup");
    auto* option_rows = document->GetElementById("body_part_option_rows");
    ASSERT_NE(rows, nullptr);
    ASSERT_NE(editor, nullptr);
    EXPECT_TRUE(editor->IsClassSet("smalls_refresh"));
    ASSERT_NE(popup, nullptr);
    ASSERT_NE(option_rows, nullptr);
    EXPECT_TRUE(rows->IsClassSet("managed_list_rows"));
    EXPECT_TRUE(rows->IsClassSet("managed_list_cycle"));
    EXPECT_EQ(rows->GetAttribute<Rml::String>("data-list-id", ""),
        "creature.appearance.body_parts");
    EXPECT_EQ(rows->GetAttribute<Rml::String>("data-cycle-list-id", ""),
        "creature.appearance.body_part_options");
    EXPECT_EQ(rows->GetAttribute<Rml::String>(
                  "data-focus-after-activate", ""),
        "body_part_rows");
    EXPECT_EQ(rows->GetAttribute<int>("data-focus-cell", 0), -1);
    EXPECT_TRUE(popup->IsClassSet("managed_list_popup"));
    EXPECT_TRUE(popup->IsClassSet("smalls_selector"));
    EXPECT_EQ(popup->GetAttribute<Rml::String>(
                  "data-anchor-list-id", ""),
        "creature.appearance.body_parts");
    EXPECT_EQ(option_rows->GetAttribute<Rml::String>("data-list-id", ""),
        "creature.appearance.body_part_options");
    EXPECT_EQ(option_rows->GetAttribute<Rml::String>(
                  "data-focus-after-activate", ""),
        "body_part_rows");
    EXPECT_EQ(option_rows->GetAttribute<int>("data-focus-cell", 0), -1);
    Rml::ElementList close_buttons;
    popup->GetElementsByClassName(close_buttons, "smalls_selector_close");
    EXPECT_EQ(close_buttons.size(), 1u);

    auto* appearance_surface = document->GetElementById(
        "creature_surface_appearance");
    ASSERT_NE(appearance_surface, nullptr);
    appearance_surface->SetClass("active", true);
    editor->SetClass("active", true);
    rows->SetInnerRML(
        "<div class='managed_list_row selected' "
        "data-list-id='creature.appearance.body_parts' data-index='0'>"
        "<span id='body-part-label' class='managed_list_cell cell_0'>Head</span>"
        "<span id='body-part-model' class='managed_list_cell cell_1'>119</span>"
        "</div>");
    document->Show();
    context->Update();

    auto* label = document->GetElementById("body-part-label");
    auto* model = document->GetElementById("body-part-model");
    ASSERT_NE(label, nullptr);
    ASSERT_NE(model, nullptr);
    auto* label_text = rmlui_dynamic_cast<Rml::ElementText*>(
        label->GetFirstChild());
    auto* model_text = rmlui_dynamic_cast<Rml::ElementText*>(
        model->GetFirstChild());
    ASSERT_NE(label_text, nullptr);
    ASSERT_NE(model_text, nullptr);
    ASSERT_FALSE(label_text->GetLines().empty());
    ASSERT_FALSE(model_text->GetLines().empty());
    EXPECT_EQ(label_text->GetLines().front().text, "Head");
    EXPECT_EQ(model_text->GetLines().front().text, "119");

    const auto focus_target = nw::toolset::managed_list_focus_target(rows);
    ASSERT_TRUE(focus_target);
    EXPECT_EQ(focus_target->element_id, "body_part_rows");
    EXPECT_EQ(focus_target->cell, -1);
    ASSERT_TRUE(nw::toolset::focus_managed_list_target(
        document, *focus_target));
    EXPECT_EQ(context->GetFocusElement(), rows);
    EXPECT_TRUE(rows->IsPseudoClassSet("focus"));

    nw::toolset::VirtualListHost cycle_host;
    ASSERT_TRUE(cycle_host.create(
        "creature.appearance.body_part_options", {}));
    ASSERT_TRUE(cycle_host.set_items(
        "creature.appearance.body_part_options",
        {
            {.key = "0", .cells = {"0", "", "", ""}},
            {.key = "1", .cells = {"1", "", "", ""}},
            {.key = "2", .cells = {"2", "", "", ""}},
        }));
    ASSERT_TRUE(cycle_host.set_selected(
        "creature.appearance.body_part_options",
        {.list_id = "creature.appearance.body_part_options",
            .key = "1",
            .index = 1,
            .cell = -1}));
    ASSERT_TRUE(nw::toolset::cycle_managed_list_element(
        rows, cycle_host, 1));
    const auto cycled = cycle_host.get_selected(
        "creature.appearance.body_part_options");
    ASSERT_TRUE(cycled);
    EXPECT_EQ(cycled->index, 2);

    rows->Blur();
    EXPECT_FALSE(rows->IsPseudoClassSet("focus"));

    popup->SetClass("active", true);
    option_rows->SetInnerRML(
        "<div class='managed_list_row' "
        "data-list-id='creature.appearance.body_part_options' data-index='2'>"
        "<span id='body-part-option-number' class='managed_list_cell cell_0' "
        "data-cell='0'>2</span>"
        "</div>");
    context->Update();
    auto* option_number = document->GetElementById(
        "body-part-option-number");
    ASSERT_NE(option_number, nullptr);
    EXPECT_GE(option_number->GetOffsetWidth(), 30.0f);
    auto* option_number_text = rmlui_dynamic_cast<Rml::ElementText*>(
        option_number->GetFirstChild());
    ASSERT_NE(option_number_text, nullptr);
    ASSERT_FALSE(option_number_text->GetLines().empty());
    EXPECT_EQ(option_number_text->GetLines().front().text, "2");
    ASSERT_TRUE(nw::toolset::position_managed_list_popups(document));
    context->Update();
    const Rml::Vector2f option_point{
        option_number->GetAbsoluteLeft() + option_number->GetOffsetWidth() * 0.5f,
        option_number->GetAbsoluteTop() + option_number->GetOffsetHeight() * 0.5f,
    };
    auto* option_hit = context->GetElementAtPoint(option_point);
    ASSERT_NE(option_hit, nullptr);
    EXPECT_TRUE(nw::toolset::combobox_popup_contains_element(option_hit));
    EXPECT_TRUE(nw::toolset::activate_managed_list_element(
        option_hit, cycle_host));
    const auto clicked = cycle_host.get_selected(
        "creature.appearance.body_part_options");
    ASSERT_TRUE(clicked);
    EXPECT_EQ(clicked->index, 2);

    auto* secondary = document->GetElementById(
        "creature_appearance_secondary_dynamic");
    ASSERT_NE(secondary, nullptr);
    secondary->SetInnerRML(
        "<div class='creature_accessory_editor'>"
        "<div id='accessories-title' class='creature_accessory_title'>Accessories</div>"
        "<div id='wings-field' class='appearance_catalog_editor'>"
        "<div class='appearance_field_label'>Wings</div>"
        "<div class='appearance_field'><span>None</span></div>"
        "</div></div>");
    context->Update();
    auto* accessories_title = document->GetElementById("accessories-title");
    auto* wings_field = document->GetElementById("wings-field");
    ASSERT_NE(accessories_title, nullptr);
    ASSERT_NE(wings_field, nullptr);
    EXPECT_GE(wings_field->GetAbsoluteTop(),
        accessories_title->GetAbsoluteTop()
            + accessories_title->GetOffsetHeight());

    document->Close();
    context->Update();
    Rml::RemoveContext("creature-workbench-template-test");
}

TEST(ClientRmlTemplates, PlacedObjectRowsGenerateEscapedTargetsAndRejectMalformedIdentities)
{
    using namespace nw::toolset;
    ASSERT_TRUE(nw::kernel::load_module("test_data/user/modules/DockerDemo.mod"));
    auto* area = nw::kernel::objects().make<nw::Area>();
    auto* creature = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(area, nullptr);
    ASSERT_NE(creature, nullptr);
    area->creatures.push_back(creature);
    std::vector<PlacedAreaObjectRow> rows;
    build_placed_area_object_rows(*area, rows);
    ASSERT_EQ(rows.size(), 1u);
    rows.front().name = "Name <&\" β";
    std::string markup;
    append_placed_area_object_list_markup(markup, rows, true);
    EXPECT_NE(markup.find("Name &lt;&amp;&quot; β"), std::string::npos);
    EXPECT_NE(markup.find("area_object_list_count\">1"), std::string::npos);
    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    auto* context = Rml::CreateContext("placed-object-targets", {900, 600});
    ASSERT_NE(context, nullptr);
    auto* document = context->LoadDocumentFromMemory("<rml><body>" + markup
        + "<button class='area_object_list_back'><span id='back'>Back</span></button>"
          "<button id='unrelated'>Other</button></body></rml>");
    ASSERT_NE(document, nullptr);
    document->Show();
    context->Update();
    Rml::ElementList elements;
    document->GetElementsByClassName(elements, "area_object_row");
    ASSERT_EQ(elements.size(), 1u);
    auto* row = elements.front();
    ASSERT_GT(row->GetNumChildren(), 0);
    auto click = capture_placed_area_object_click(row->GetChild(0));
    ASSERT_TRUE(click);
    EXPECT_EQ(click->kind, PlacedAreaObjectClickKind::select);
    EXPECT_EQ(click->object, creature->handle());
    const auto packed = std::to_string(creature->handle().to_ull());
    for (const auto& malformed : {std::string{}, packed + "tail", "+" + packed,
             " " + packed, std::string{"-1"}, std::string{"18446744073709551616"}}) {
        row->SetAttribute("data-object", malformed);
        click = capture_placed_area_object_click(row->GetChild(0));
        ASSERT_TRUE(click);
        EXPECT_EQ(click->kind, PlacedAreaObjectClickKind::none);
    }
    row->SetAttribute("data-object", packed);
    area->creatures.clear();
    nw::kernel::objects().destroy(creature->handle());
    click = capture_placed_area_object_click(row);
    ASSERT_TRUE(click);
    EXPECT_EQ(click->kind, PlacedAreaObjectClickKind::none);
    click = capture_placed_area_object_click(document->GetElementById("back"));
    ASSERT_TRUE(click);
    EXPECT_EQ(click->kind, PlacedAreaObjectClickKind::back);
    EXPECT_FALSE(capture_placed_area_object_click(document->GetElementById("unrelated")));
    EXPECT_FALSE(capture_placed_area_object_click(nullptr));
    markup.clear();
    append_placed_area_object_list_markup(markup, {}, true);
    EXPECT_NE(markup.find("This area has no placed objects."), std::string::npos);
    markup.clear();
    append_placed_area_object_list_markup(markup, {}, false);
    EXPECT_NE(markup.find("Loading placed objects..."), std::string::npos);
    document->Close();
    context->Update();
    Rml::RemoveContext("placed-object-targets");
    nw::kernel::objects().destroy(area->handle());
}

TEST(ClientRmlTemplates, AreaSelectionKeepsViewportRectangleStableAcrossWorkbenchTypes)
{
    CurrentPathScope source_root{ROLLNW_TEST_SOURCE_DIR};
    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    ASSERT_TRUE(Rml::LoadFontFace("tools/client/assets/fonts/inter/Inter-Regular.ttf"));
    const std::string source = R"RML(
<rml><head>
<link type="text/css" href="tools/client/ui/panel.rcss"/>
<link type="text/css" href="tools/client/ui/creature_editor.rcss"/>
<link type="text/css" href="tools/client/ui/item_editor.rcss"/>
<link type="text/css" href="tools/client/ui/door_editor.rcss"/>
<link type="text/css" href="tools/client/ui/placeable_editor.rcss"/>
<style>body { font-family: Inter; font-weight: normal; }</style>
</head><body>
<div id="area_body" class="workspace_preview_body workspace_area_body" style="width:100%; height:100%;">
  <div id="viewport" class="workspace_viewer_viewport"></div>
  <div id="workbench" class="object_workbench area_object_list_workbench"></div>
</div>
</body></rml>)RML";
    auto* context = Rml::CreateContext("area-selection-layout-test", {1600, 900});
    ASSERT_NE(context, nullptr);
    auto* document = context->LoadDocumentFromMemory(source, "area_selection_layout_test.rml");
    ASSERT_NE(document, nullptr);
    document->Show();
    auto* viewport = document->GetElementById("viewport");
    auto* workbench = document->GetElementById("workbench");
    auto* body = document->GetElementById("area_body");
    ASSERT_NE(viewport, nullptr);
    ASSERT_NE(workbench, nullptr);
    ASSERT_NE(body, nullptr);
    for (const int width : {1600, 900}) {
        context->SetDimensions({width, 900});
        workbench->SetClassNames("object_workbench area_object_list_workbench");
        context->Update();
        const auto left = viewport->GetAbsoluteLeft();
        const auto top = viewport->GetAbsoluteTop();
        const auto viewport_width = viewport->GetClientWidth();
        const auto viewport_height = viewport->GetClientHeight();
        for (const auto* type : {"creature_workbench", "item_workbench", "door_workbench",
                 "placeable_workbench", "area_object_list_workbench"}) {
            SCOPED_TRACE(type);
            workbench->SetClassNames(std::string{"object_workbench "} + type);
            context->Update();
            EXPECT_FLOAT_EQ(viewport->GetAbsoluteLeft(), left);
            EXPECT_FLOAT_EQ(viewport->GetAbsoluteTop(), top);
            EXPECT_FLOAT_EQ(viewport->GetClientWidth(), viewport_width);
            EXPECT_FLOAT_EQ(viewport->GetClientHeight(), viewport_height);
        }
    }
    context->SetDimensions({1600, 900});
    body->SetClass("workspace_area_body", false);
    workbench->SetClassNames("object_workbench item_workbench");
    context->Update();
    EXPECT_FLOAT_EQ(workbench->GetOffsetWidth(), 560.0f);
    document->Close();
    context->Update();
    Rml::RemoveContext("area-selection-layout-test");
}

TEST(ClientRmlTemplates, WorkspaceTabBarProvidesOverflowControls)
{
    CurrentPathScope source_root{ROLLNW_TEST_SOURCE_DIR};
    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    ASSERT_TRUE(Rml::LoadFontFace(
        "tools/client/assets/fonts/inter/Inter-Regular.ttf"));

    const std::string source = R"RML(
<rml>
<head>
<link type="text/css" href="tools/client/ui/panel.rcss"/>
<style>body, button { font-family: Inter; font-weight: normal; }</style>
</head>
<body>
<div id="workspace_shell">
  <div id="workspace_tab_bar">
    <div id="workspace_tabs">
      <div id="workspace_tab_track">
        <div class="workspace_tab">Home</div>
        <div class="workspace_tab">Area</div>
        <div class="workspace_tab">Creature</div>
        <div class="workspace_tab">Conversation</div>
      </div>
    </div>
    <button id="workspace_tabs_previous" class="workspace_tab_scroll_button disabled" type="button">&#x2039;</button>
    <button id="workspace_tabs_next" class="workspace_tab_scroll_button disabled" type="button">&#x203a;</button>
  </div>
  <div id="workspace_content"></div>
</div>
</body>
</rml>
)RML";

    auto* context = Rml::CreateContext("workspace-tab-bar-template-test", {600, 200});
    ASSERT_NE(context, nullptr);
    auto* document = context->LoadDocumentFromMemory(
        source, "workspace_tab_bar_template_test.rml");
    ASSERT_NE(document, nullptr);
    document->Show();
    context->Update();

    auto* bar = document->GetElementById("workspace_tab_bar");
    auto* tabs = document->GetElementById("workspace_tabs");
    auto* previous = document->GetElementById("workspace_tabs_previous");
    auto* next = document->GetElementById("workspace_tabs_next");
    ASSERT_NE(bar, nullptr);
    ASSERT_NE(tabs, nullptr);
    ASSERT_NE(previous, nullptr);
    ASSERT_NE(next, nullptr);
    EXPECT_GT(previous->GetOffsetWidth(), 0.0f);
    EXPECT_GT(next->GetOffsetWidth(), 0.0f);
    EXPECT_LT(tabs->GetClientWidth(), bar->GetClientWidth());
    EXPECT_GT(tabs->GetScrollWidth(), tabs->GetClientWidth());
    previous->SetClass("disabled", true);
    next->SetClass("disabled", false);
    EXPECT_TRUE(previous->IsClassSet("disabled"));
    EXPECT_FALSE(next->IsClassSet("disabled"));

    document->Close();
    context->Update();
    Rml::RemoveContext("workspace-tab-bar-template-test");
}

TEST(ClientRmlTemplates, DropdownMarkupUsesSharedComboboxInfrastructure)
{
    const auto source_root = std::filesystem::path{ROLLNW_TEST_SOURCE_DIR};
    const std::array roots{
        source_root / "tools/client",
        source_root / "tools/ui",
    };
    for (const auto& root : roots) {
        for (const auto& entry :
            std::filesystem::recursive_directory_iterator{root}) {
            if (!entry.is_regular_file()) { continue; }
            const auto extension = entry.path().extension();
            if (extension != ".cpp" && extension != ".hpp"
                && extension != ".rml" && extension != ".smalls") {
                continue;
            }
            SCOPED_TRACE(entry.path().string());
            std::ifstream input{entry.path(), std::ios::binary};
            ASSERT_TRUE(input);
            std::string contents{
                std::istreambuf_iterator<char>{input}, {}};
            std::ranges::transform(contents, contents.begin(),
                [](unsigned char ch) {
                    return static_cast<char>(std::tolower(ch));
                });
            EXPECT_EQ(contents.find("<select"), std::string::npos)
                << "Native select controls bypass VirtualComboBox";
        }
    }
}

TEST(ClientRmlTemplates, BlueprintModalsKeepVisibleControlsInsideTheCommandOverlay)
{
    CurrentPathScope source_root{ROLLNW_TEST_SOURCE_DIR};
    std::ifstream font_file{"tools/client/assets/fonts/inter/Inter-Medium.ttf", std::ios::binary};
    const std::vector<Rml::byte> font{std::istreambuf_iterator<char>{font_file}, {}};
    ASSERT_FALSE(font.empty());
    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    ASSERT_TRUE(Rml::LoadFontFace({font.data(), font.size()}, "RollnwSans", Rml::Style::FontStyle::Normal,
        static_cast<Rml::Style::FontWeight>(500)));
    auto* context = Rml::CreateContext("blueprint-command-overlay", {900, 600});
    ASSERT_NE(context, nullptr);
    auto* document = context->LoadDocument("tools/client/ui/command_modals.rml");
    ASSERT_NE(document, nullptr);
    auto* form = document->GetElementById("command_form_overlay");
    auto* progress = document->GetElementById("blueprint_operation_overlay");
    ASSERT_NE(form, nullptr);
    ASSERT_NE(progress, nullptr);
    form->SetInnerRML(R"RML(
<div id="dialog" class="command_form">
  <div id="title" class="command_form_title">New Creature Blueprint</div>
  <div id="message" class="command_form_message">ResRefs must be unique for this resource type across the module. Folders organize the files.</div>
  <div id="resref-row" class="command_form_row"><label for="resref">ResRef</label><input id="resref" type="text" value="pl_agent_001"/></div>
  <div id="directory-row" class="command_form_row"><label for="directory">Directory</label><input id="directory" type="text" value="shared/blueprints/creatures"/><button id="browse" class="command_form_browse">Browse...</button></div>
  <div id="race-row" class="command_form_row"><label for="race">Race</label><button type="button" id="race" class="combobox_field command_form_choice_field"><span class="combobox_value">Human</span><span class="combobox_arrow"><span class="combobox_arrow_indicator"></span></span></button></div>
  <div id="class-row" class="command_form_row"><label for="class">Base Class</label><button type="button" id="class" class="combobox_field command_form_choice_field"><span class="combobox_value">Fighter</span><span class="combobox_arrow"><span class="combobox_arrow_indicator"></span></span></button></div>
  <div id="feedback" class="command_form_feedback">
    <div id="command_form_filename">shared/blueprints/creatures/pl_agent_001.utc.json</div>
    <div id="command_form_detail"></div>
    <div id="command_form_error">ResRef already exists: pl_agent_001.utc in /home/josh/projects/the_awakening/shared</div>
  </div>
  <div id="actions" class="command_form_actions"><button id="create" class="command_form_action command_form_action_primary disabled" disabled>Create</button><button id="cancel" class="command_form_action command_form_action_secondary">Cancel</button></div>
</div>)RML");
    form->SetClass("active", true);
    document->Show(Rml::ModalFlag::Modal);
    for (const int width : {900, 460}) {
        SCOPED_TRACE(width);
        context->SetDimensions({width, 600});
        context->Update();
        auto* dialog = document->GetElementById("dialog");
        ASSERT_NE(dialog, nullptr);
        EXPECT_GT(dialog->GetAbsoluteLeft(), 0.0f);
        EXPECT_LE(dialog->GetAbsoluteLeft() + dialog->GetOffsetWidth(), float(width));
        auto* title = document->GetElementById("title");
        auto* message = document->GetElementById("message");
        auto* resref_row = document->GetElementById("resref-row");
        auto* directory_row = document->GetElementById("directory-row");
        auto* race_row = document->GetElementById("race-row");
        auto* class_row = document->GetElementById("class-row");
        auto* feedback = document->GetElementById("feedback");
        auto* filename = document->GetElementById("command_form_filename");
        auto* detail = document->GetElementById("command_form_detail");
        auto* error = document->GetElementById("command_form_error");
        auto* actions = document->GetElementById("actions");
        ASSERT_NE(title, nullptr);
        ASSERT_NE(message, nullptr);
        ASSERT_NE(resref_row, nullptr);
        ASSERT_NE(directory_row, nullptr);
        ASSERT_NE(race_row, nullptr);
        ASSERT_NE(class_row, nullptr);
        ASSERT_NE(feedback, nullptr);
        ASSERT_NE(filename, nullptr);
        ASSERT_NE(detail, nullptr);
        ASSERT_NE(error, nullptr);
        ASSERT_NE(actions, nullptr);
        EXPECT_GE(message->GetAbsoluteTop(), title->GetAbsoluteTop() + title->GetOffsetHeight());
        EXPECT_GE(resref_row->GetAbsoluteTop(), message->GetAbsoluteTop() + message->GetOffsetHeight());
        EXPECT_GE(directory_row->GetAbsoluteTop(), resref_row->GetAbsoluteTop() + resref_row->GetOffsetHeight());
        EXPECT_GE(race_row->GetAbsoluteTop(), directory_row->GetAbsoluteTop() + directory_row->GetOffsetHeight());
        EXPECT_GE(class_row->GetAbsoluteTop(), race_row->GetAbsoluteTop() + race_row->GetOffsetHeight());
        EXPECT_GE(feedback->GetAbsoluteTop(), class_row->GetAbsoluteTop() + class_row->GetOffsetHeight());
        EXPECT_GE(detail->GetAbsoluteTop(), filename->GetAbsoluteTop() + filename->GetOffsetHeight());
        EXPECT_GE(error->GetAbsoluteTop(), detail->GetAbsoluteTop() + detail->GetOffsetHeight());
        EXPECT_GE(actions->GetAbsoluteTop(), error->GetAbsoluteTop() + error->GetOffsetHeight());
        for (const auto* id : {"resref", "directory", "race", "class", "browse", "create", "cancel"}) {
            SCOPED_TRACE(id);
            auto* control = document->GetElementById(id);
            ASSERT_NE(control, nullptr);
            EXPECT_TRUE(control->IsVisible(true));
            EXPECT_GT(control->GetOffsetWidth(), 0.0f);
            EXPECT_GE(control->GetOffsetHeight(), 30.0f);
            EXPECT_GE(control->GetProperty<float>("border-left-width"), 1.0f);
            EXPECT_GT(control->GetProperty<Rml::Colourb>("background-color").alpha, 0);
            EXPECT_LE(control->GetAbsoluteLeft() + control->GetOffsetWidth(), dialog->GetAbsoluteLeft() + dialog->GetOffsetWidth());
        }
        auto* create = document->GetElementById("create");
        auto* cancel = document->GetElementById("cancel");
        ASSERT_NE(create, nullptr);
        ASSERT_NE(cancel, nullptr);
        EXPECT_TRUE(create->IsClassSet("command_form_action_primary"));
        EXPECT_TRUE(cancel->IsClassSet("command_form_action_secondary"));
        create->SetClass("disabled", true);
        create->SetAttribute("disabled", true);
        context->Update();
        const auto disabled_red = create->GetProperty<Rml::Colourb>("background-color").red;
        create->SetClass("disabled", false);
        create->RemoveAttribute("disabled");
        context->Update();
        const auto primary_red = create->GetProperty<Rml::Colourb>("background-color").red;
        const auto secondary_red = cancel->GetProperty<Rml::Colourb>("background-color").red;
        EXPECT_NE(primary_red, disabled_red);
        EXPECT_NE(primary_red, secondary_red);
        EXPECT_TRUE(cancel->Focus());
        context->Update();
        EXPECT_EQ(context->GetFocusElement(), cancel);
        auto* input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(document->GetElementById("resref"));
        ASSERT_NE(input, nullptr);
        input->SetValue("");
        EXPECT_TRUE(input->Focus());
        EXPECT_EQ(context->GetFocusElement(), input);
        (void)context->ProcessTextInput(Rml::String{"auth_human"});
        EXPECT_EQ(input->GetValue(), "auth_human");
        auto* hit = context->GetElementAtPoint({cancel->GetAbsoluteLeft() + cancel->GetOffsetWidth() / 2,
            cancel->GetAbsoluteTop() + cancel->GetOffsetHeight() / 2});
        while (hit && hit != cancel) {
            hit = hit->GetParentNode();
        }
        EXPECT_EQ(hit, cancel);
        EXPECT_EQ(context->GetElementAtPoint({1, 1}), form);
    }
    form->SetInnerRML(R"RML(
<div id="type-dialog" class="command_form command_form_action_picker">
  <div class="command_form_title_row"><div id="type-title" class="command_form_title">New Blueprint</div><button id="type-close" class="command_form_action command_form_close"><span class="command_form_close_glyph">&#215;</span></button></div>
  <div class="command_form_message">Choose the blueprint type.</div>
  <div id="type-actions" class="command_form_actions command_form_action_list">
    <button id="type-creature" class="command_form_action command_form_action_primary">Creature</button>
    <button class="command_form_action command_form_action_secondary">Door</button>
    <button class="command_form_action command_form_action_secondary">Encounter</button>
    <button class="command_form_action command_form_action_secondary">Item</button>
    <button class="command_form_action command_form_action_secondary">Placeable</button>
    <button class="command_form_action command_form_action_secondary">Sound</button>
    <button class="command_form_action command_form_action_secondary">Store</button>
    <button class="command_form_action command_form_action_secondary">Trigger</button>
    <button class="command_form_action command_form_action_secondary">Waypoint</button>
  </div>
</div>)RML");
    context->SetDimensions({460, 460});
    context->Update();
    auto* type_dialog = document->GetElementById("type-dialog");
    auto* type_actions = document->GetElementById("type-actions");
    auto* type_creature = document->GetElementById("type-creature");
    auto* type_title = document->GetElementById("type-title");
    auto* type_close = document->GetElementById("type-close");
    ASSERT_NE(type_dialog, nullptr);
    ASSERT_NE(type_actions, nullptr);
    ASSERT_NE(type_creature, nullptr);
    ASSERT_NE(type_title, nullptr);
    ASSERT_NE(type_close, nullptr);
    EXPECT_LE(type_dialog->GetAbsoluteTop() + type_dialog->GetOffsetHeight(),
        460.0f);
    EXPECT_LE(type_dialog->GetOffsetWidth(), 420.0f);
    EXPECT_EQ(type_actions->GetNumChildren(), 9u);
    EXPECT_GE(type_close->GetAbsoluteLeft(),
        type_title->GetAbsoluteLeft() + type_title->GetOffsetWidth());
    EXPECT_LT(type_close->GetAbsoluteTop(),
        type_title->GetAbsoluteTop() + type_title->GetOffsetHeight());
    EXPECT_EQ(type_close->GetOffsetWidth(), 24.0f);
    EXPECT_EQ(type_creature->GetProperty<float>("border-top-width"), 0.0f);
    form->SetClass("active", false);
    progress->SetClass("active", true);
    progress->SetInnerRML(R"RML(
<div id="operation-dialog" class="command_form">
  <div id="operation-title" class="command_form_title">Update Blueprint References</div>
  <div id="operation-stage" class="blueprint_operation_stage">Scanning documents</div>
  <div id="operation-progress" class="home_import_progress"><div id="fill" class="blueprint_progress_fill" style="width:50%"></div></div>
  <div id="operation-count" class="blueprint_operation_progress_text">5 / 10 documents</div>
  <div id="operation-detail" class="blueprint_operation_detail">Preparing replacements in the current area</div>
  <div id="operation-actions" class="command_form_actions"><button class="blueprint_authoring_action command_form_action command_form_action_secondary">Cancel</button></div>
</div>)RML");
    context->Update();
    EXPECT_EQ(context->GetElementAtPoint({1, 1}), progress);
    auto* operation_title = document->GetElementById("operation-title");
    auto* operation_stage = document->GetElementById("operation-stage");
    auto* operation_progress = document->GetElementById("operation-progress");
    auto* operation_count = document->GetElementById("operation-count");
    auto* operation_detail = document->GetElementById("operation-detail");
    auto* operation_actions = document->GetElementById("operation-actions");
    ASSERT_NE(operation_title, nullptr);
    ASSERT_NE(operation_stage, nullptr);
    ASSERT_NE(operation_progress, nullptr);
    ASSERT_NE(operation_count, nullptr);
    ASSERT_NE(operation_detail, nullptr);
    ASSERT_NE(operation_actions, nullptr);
    EXPECT_GE(operation_stage->GetAbsoluteTop(), operation_title->GetAbsoluteTop() + operation_title->GetOffsetHeight());
    EXPECT_GE(operation_progress->GetAbsoluteTop(), operation_stage->GetAbsoluteTop() + operation_stage->GetOffsetHeight());
    EXPECT_GE(operation_count->GetAbsoluteTop(), operation_progress->GetAbsoluteTop() + operation_progress->GetOffsetHeight());
    EXPECT_GE(operation_detail->GetAbsoluteTop(), operation_count->GetAbsoluteTop() + operation_count->GetOffsetHeight());
    EXPECT_GE(operation_actions->GetAbsoluteTop(), operation_detail->GetAbsoluteTop() + operation_detail->GetOffsetHeight());
    auto* fill = document->GetElementById("fill");
    ASSERT_NE(fill, nullptr);
    EXPECT_GT(fill->GetOffsetWidth(), 0.0f);
    EXPECT_FLOAT_EQ(fill->GetOffsetHeight(), 8.0f);
    document->Hide();
    context->Update();
    EXPECT_FALSE(form->IsVisible(true));
    EXPECT_FALSE(progress->IsVisible(true));
}

TEST(ClientRmlTemplates, CommandFormRefreshPreservesInputAndInvalidatesGenerationPopup)
{
    using namespace nw::toolset;
    CurrentPathScope source_root{ROLLNW_TEST_SOURCE_DIR};
    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    auto* context = Rml::CreateContext("command-form-refresh", {900, 600});
    ASSERT_NE(context, nullptr);
    auto* document = context->LoadDocument("tools/client/ui/command_modals.rml");
    ASSERT_NE(document, nullptr);
    ToolsetBackend backend;
    CommandViewState state;
    state.command_overlay_document = document;
    state.command_form = CommandPrompt{
        .title = "Title <&>",
        .message = "Message",
        .actions = {{"create", "Create"}, {"cancel", "Cancel"}},
        .fields = {
            {.label = "Name", .value = "initial"},
            {.label = "Choice", .value = "first", .choices = {{"first", "First"}, {"second", "Second <&>"}}},
        },
    };
    ++state.command_form_generation;
    sync_command_form(state, backend, false);
    context->Update();
    ASSERT_TRUE(document->IsVisible());
    auto* name = rmlui_dynamic_cast<Rml::ElementFormControl*>(document->GetElementById("command_form_field_0"));
    ASSERT_NE(name, nullptr);
    name->SetValue("edited");
    ASSERT_TRUE(name->Focus());
    sync_command_form(state, backend, false);
    EXPECT_EQ(state.command_form->fields[0].value, "edited");
    EXPECT_EQ(document->GetElementById("command_form_field_0"), name);
    EXPECT_EQ(context->GetFocusElement(), name);
    EXPECT_NE(document->GetElementById("command_form_overlay")->GetInnerRML().find("Title &lt;&amp;&gt;"), std::string::npos);

    ASSERT_TRUE(open_command_form_combobox(state, 1));
    EXPECT_FALSE(commit_command_form_combobox(state, backend, false, -1));
    EXPECT_FALSE(commit_command_form_combobox(state, backend, false, 2));
    EXPECT_EQ(state.command_form->fields[1].value, "first");
    ASSERT_TRUE(commit_command_form_combobox(state, backend, false, 1));
    EXPECT_EQ(state.command_form->fields[1].value, "second");
    EXPECT_FALSE(state.command_form_combobox.is_active());
    EXPECT_NE(document->GetElementById("command_form_field_1")->GetInnerRML().find("Second &lt;&amp;&gt;"), std::string::npos);
    ASSERT_TRUE(open_command_form_combobox(state, 1));
    state.command_form = CommandPrompt{.title = "Replacement", .actions = {{"cancel", "Cancel"}}};
    ++state.command_form_generation;
    sync_command_form(state, backend, false);
    EXPECT_FALSE(state.command_form_combobox.is_active());
    EXPECT_FALSE(state.command_form_combobox_field);
    EXPECT_EQ(document->GetElementById("command_form_field_0"), nullptr);
    EXPECT_FALSE(open_command_form_combobox(state, 0));
    state.command_form.reset();
    ++state.command_form_generation;
    sync_command_form(state, backend, false);
    EXPECT_FALSE(document->IsVisible());
    EXPECT_TRUE(document->GetElementById("command_form_overlay")->GetInnerRML().empty());
    sync_command_overlay_visibility(state, true, false);
    EXPECT_TRUE(document->IsVisible());
    sync_command_overlay_visibility(state, false, true);
    EXPECT_TRUE(document->IsVisible());
    sync_command_overlay_visibility(state, false, false);
    EXPECT_FALSE(document->IsVisible());
    document->Close();
    context->Update();
    Rml::RemoveContext("command-form-refresh");
}

TEST(ClientRmlTemplates, CommandFormKeysUseLiveFocusAndPreservePopupActionOrdering)
{
    using namespace nw::toolset;
    CurrentPathScope source_root{ROLLNW_TEST_SOURCE_DIR};
    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    auto* context = Rml::CreateContext("command-form-keys", {900, 600});
    ASSERT_NE(context, nullptr);
    auto* document = context->LoadDocument("tools/client/ui/command_modals.rml");
    ASSERT_NE(document, nullptr);
    ToolsetBackend backend;
    CommandViewState state;
    state.command_overlay_document = document;
    state.command_form = CommandPrompt{
        .actions = {{"create", "Create"}, {"cancel", "Cancel"}},
        .fields = {{.label = "Choice", .value = "first", .choices = {{"first", "First"}, {"second", "Second"}}}},
    };
    ++state.command_form_generation;
    sync_command_form(state, backend, false);
    context->Update();
    auto* choice = document->GetElementById("command_form_field_0");
    ASSERT_NE(choice, nullptr);
    ASSERT_TRUE(choice->Focus());
    const auto press = [&](SDL_Keycode code, SDL_Keymod mods = SDL_KMOD_NONE, bool repeat = false) {
        SDL_KeyboardEvent key{};
        key.type = SDL_EVENT_KEY_DOWN;
        key.key = code;
        key.mod = mods;
        key.repeat = repeat;
        return handle_command_form_key(state, backend, false, context, key);
    };
    EXPECT_FALSE(press(SDLK_DOWN, SDL_KMOD_NONE, true).handled);
    EXPECT_FALSE(press(SDLK_DOWN, SDL_KMOD_CTRL).handled);
    EXPECT_FALSE(state.command_form_combobox.is_active());
    for (const auto* malformed : {"0tail", "+0", "-1", "2147483648"}) {
        choice->SetAttribute("data-field", malformed);
        EXPECT_FALSE(press(SDLK_DOWN).handled);
        EXPECT_FALSE(state.command_form_combobox.is_active());
    }
    choice->SetAttribute("data-field", "0");
    auto key = press(SDLK_DOWN);
    EXPECT_TRUE(key.handled);
    EXPECT_FALSE(key.action_index);
    ASSERT_TRUE(state.command_form_combobox.is_active());
    EXPECT_EQ(state.command_form_combobox.selected_key(), 1);
    EXPECT_TRUE(state.command_form_combobox.popup_visible());
    EXPECT_EQ(state.command_form->fields[0].value, "first");
    EXPECT_TRUE(press(SDLK_KP_ENTER).handled);
    EXPECT_EQ(state.command_form->fields[0].value, "second");
    EXPECT_FALSE(state.command_form_combobox.is_active());

    EXPECT_TRUE(press(SDLK_RETURN).handled);
    EXPECT_TRUE(state.command_form_combobox.popup_visible());
    key = press(SDLK_ESCAPE);
    EXPECT_TRUE(key.handled);
    EXPECT_FALSE(key.action_index);
    EXPECT_FALSE(state.command_form_combobox.is_active());
    key = press(SDLK_ESCAPE);
    ASSERT_TRUE(key.action_index);
    EXPECT_EQ(*key.action_index, 1u);
    ASSERT_TRUE(state.command_form);

    EXPECT_TRUE(press(SDLK_RETURN).handled);
    key = press(SDLK_TAB);
    EXPECT_FALSE(key.handled);
    EXPECT_FALSE(key.action_index);
    EXPECT_FALSE(state.command_form_combobox.is_active());
    key = press(SDLK_RETURN, SDL_KMOD_CTRL);
    ASSERT_TRUE(key.action_index);
    EXPECT_EQ(*key.action_index, 0u);
    choice->Blur();
    EXPECT_FALSE(press(SDLK_KP_ENTER).handled);
    key = press(SDLK_RETURN);
    EXPECT_EQ(key.action_index, 0u);
    state.command_form->actions.clear();
    EXPECT_FALSE(press(SDLK_RETURN).handled);
    EXPECT_TRUE(press(SDLK_ESCAPE).handled);
    state.command_form.reset();
    EXPECT_FALSE(press(SDLK_ESCAPE).handled);
    document->Close();
    context->Update();
    Rml::RemoveContext("command-form-keys");
}

TEST(ClientRmlTemplates, CommandFormRejectsMalformedChoicesWithoutDiscardingCurrentPopup)
{
    using namespace nw::toolset;
    CommandViewState state;
    state.command_form = CommandPrompt{.fields = {
                                           {.value = "one", .choices = {{"one", "One"}, {"two", "Two"}}},
                                           {.value = "duplicate", .choices = {{"duplicate", "First"}, {"duplicate", "Second"}}},
                                           {.value = "missing", .choices = {{"other", "Other"}}},
                                           {.value = "", .choices = {{"", "Empty"}}},
                                       }};
    ASSERT_TRUE(open_command_form_combobox(state, 0));
    EXPECT_FALSE(open_command_form_combobox(state, 1));
    EXPECT_FALSE(open_command_form_combobox(state, 2));
    EXPECT_FALSE(open_command_form_combobox(state, 3));
    EXPECT_FALSE(open_command_form_combobox(state, 4));
    EXPECT_EQ(state.command_form_combobox_field, 0u);
    EXPECT_TRUE(state.command_form_combobox.is_active());
    close_command_form_combobox(state);
    EXPECT_FALSE(state.command_form_combobox.is_active());
}

TEST(ClientRmlTemplates, CommandPaletteRetainsOriginalFocusAcrossRepeatedOpen)
{
    using namespace nw::toolset;
    CurrentPathScope ui_root{std::filesystem::path{ROLLNW_TEST_SOURCE_DIR} / "tools/client/ui"};
    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    auto* context = Rml::CreateContext("palette-main-focus", {1200, 700});
    auto* palette_context = Rml::CreateContext("palette-focus", {1200, 700});
    ASSERT_NE(context, nullptr);
    ASSERT_NE(palette_context, nullptr);
    auto* document = context->LoadDocument("panel.rml");
    auto* palette_document = load_command_palette_document(*palette_context);
    ASSERT_NE(document, nullptr);
    ASSERT_NE(palette_document, nullptr);
    document->Show();
    palette_document->Show();
    document->GetElementById("panel")->SetProperty("display", "flex");
    context->Update();
    auto* search = document->GetElementById("recent_search");
    ASSERT_NE(search, nullptr);
    ASSERT_TRUE(search->Focus());
    CommandViewState state;
    bool viewport_focused = false;
    set_command_palette_visibility(state, context, palette_context, document, palette_document, viewport_focused, true);
    palette_context->Update();
    ASSERT_TRUE(palette_document->GetElementById("command_input")->Focus());
    EXPECT_EQ(state.command_palette_restore_focus_id, "recent_search");
    ASSERT_TRUE(document->GetElementById("output_filter")->Focus());
    set_command_palette_visibility(state, context, palette_context, document, palette_document, viewport_focused, true);
    EXPECT_EQ(state.command_palette_restore_focus_id, "recent_search");
    set_command_palette_visibility(state, context, palette_context, document, palette_document, viewport_focused, false);
    EXPECT_EQ(context->GetFocusElement(), search);
    EXPECT_FALSE(focused_element_has_id(palette_context, "command_input"));
    EXPECT_FALSE(viewport_focused);
    EXPECT_FALSE(state.command_palette_restore_captured);
    EXPECT_TRUE(state.command_palette_restore_focus_id.empty());

    // A hidden or replaced input must not receive restored keyboard ownership.
    for (const bool hidden : {true, false}) {
        ASSERT_TRUE(search->Focus());
        set_command_palette_visibility(state, context, palette_context, document, palette_document, viewport_focused, true);
        search->Blur();
        if (hidden) {
            search->SetProperty("display", "none");
        } else {
            search->SetId("replacement_search");
        }
        context->Update();
        set_command_palette_visibility(state, context, palette_context, document, palette_document, viewport_focused, false);
        EXPECT_NE(context->GetFocusElement(), search);
        EXPECT_FALSE(viewport_focused);
        EXPECT_FALSE(state.command_palette_restore_captured);
        search->RemoveProperty("display");
        search->SetId("recent_search");
        context->Update();
    }
    document->Close();
    palette_document->Close();
    context->Update();
    palette_context->Update();
    Rml::RemoveContext("palette-focus");
    Rml::RemoveContext("palette-main-focus");
}

TEST(ClientRmlTemplates, CommandPaletteRestoresViewportAndRefreshesBackendMatches)
{
    using namespace nw::toolset;
    CurrentPathScope ui_root{std::filesystem::path{ROLLNW_TEST_SOURCE_DIR} / "tools/client/ui"};
    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    auto* context = Rml::CreateContext("palette-matches", {1200, 700});
    ASSERT_NE(context, nullptr);
    auto* document = load_command_palette_document(*context);
    ASSERT_NE(document, nullptr);
    document->Show();
    CommandViewState state;
    bool viewport_focused = true;
    set_command_palette_visibility(state, nullptr, context, nullptr, document, viewport_focused, true);
    EXPECT_FALSE(viewport_focused);
    ShellController shell;
    WorkspaceState workspace;
    ToolsetBackend backend;
    ScriptCommandHostReset script_host;
    backend.bind(nullptr, &shell, &workspace);
    refresh_command_palette(document, state, backend);
    const auto expected = backend.list_commands("");
    ASSERT_FALSE(expected.empty());
    ASSERT_EQ(state.commands.size(), expected.size());
    EXPECT_EQ(state.commands.front().id, expected.front().id);
    EXPECT_NE(document->GetElementById("command_list_items")->GetInnerRML().find(expected.front().id), std::string::npos);
    auto* input = rmlui_dynamic_cast<Rml::ElementFormControl*>(document->GetElementById("command_input"));
    ASSERT_NE(input, nullptr);
    input->SetValue("no-such-command-987654321");
    refresh_command_palette_query(document, state, backend, false);
    EXPECT_EQ(state.commands.size(), expected.size());
    refresh_command_palette_query(document, state, backend, true);
    EXPECT_TRUE(state.commands.empty());
    EXPECT_EQ(document->GetElementById("command_list_items")->GetInnerRML(), "<div class=\"nw_list_empty\">No matching commands.</div>");
    EXPECT_TRUE(document->GetElementById("command_details")->GetInnerRML().empty());
    set_command_palette_visibility(state, nullptr, context, nullptr, document, viewport_focused, false);
    EXPECT_TRUE(viewport_focused);
    set_command_palette_visibility(state, nullptr, nullptr, nullptr, nullptr, viewport_focused, false);
    EXPECT_TRUE(viewport_focused);
    refresh_command_palette(nullptr, state, backend);
    document->Close();
    context->Update();
    Rml::RemoveContext("palette-matches");
}

TEST(ClientRmlTemplates, CommandSubmissionOwnsArgumentsAndRejectsDisabledActions)
{
    using namespace nw::toolset;
    CurrentPathScope source_root{ROLLNW_TEST_SOURCE_DIR};
    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    auto* context = Rml::CreateContext("command-submit", {900, 600});
    ASSERT_NE(context, nullptr);
    auto* document = context->LoadDocument("tools/client/ui/command_modals.rml");
    ASSERT_NE(document, nullptr);
    ToolsetBackend backend;
    CommandViewState state;
    state.command_overlay_document = document;
    CommandResult result{.prompt = CommandPrompt{
                             .actions = {{"create", "Create", "example.create", {"fixed"}}, {"cancel", "Cancel", "example.cancel", {"cancel-fixed"}}},
                             .fields = {{.label = "Name", .value = ""}, {.label = "Choice", .value = "first", .choices = {{"first", "First"}, {"second", "Second"}}}},
                         }};
    ASSERT_TRUE(take_command_form_prompt(state, result));
    EXPECT_FALSE(result.prompt);
    EXPECT_EQ(result.status, CommandStatus::noop);
    EXPECT_FALSE(result.should_log());
    sync_command_form(state, backend, false);
    EXPECT_FALSE(take_command_form_action(state, backend, false, false, 0));
    EXPECT_FALSE(take_command_form_action(state, backend, false, false, 2));
    EXPECT_FALSE(take_command_form_action(state, backend, false, true, 1));
    ASSERT_TRUE(state.command_form);
    auto* input = rmlui_dynamic_cast<Rml::ElementFormControl*>(document->GetElementById("command_form_field_0"));
    ASSERT_NE(input, nullptr);
    input->SetValue("edited <&>");
    auto* choice = document->GetElementById("command_form_field_1");
    ASSERT_NE(choice, nullptr);
    choice->SetAttribute("data-field", "invalid");
    EXPECT_EQ(handle_command_overlay_target(state, backend, false, choice).kind, CommandOverlayActionKind::handled);
    EXPECT_FALSE(state.command_form_combobox.is_active());
    choice->SetAttribute("data-field", "1");
    EXPECT_EQ(handle_command_overlay_target(state, backend, false, choice->GetChild(0)).kind, CommandOverlayActionKind::handled);
    EXPECT_TRUE(state.command_form_combobox.is_active());
    EXPECT_EQ(handle_command_overlay_target(state, backend, false, document->GetElementById("command_form_overlay")).kind, CommandOverlayActionKind::handled);
    EXPECT_FALSE(state.command_form_combobox.is_active());
    const auto submit = handle_command_overlay_target(state, backend, false, document->GetElementById("command_form_action_0"));
    EXPECT_EQ(submit.kind, CommandOverlayActionKind::submit);
    EXPECT_EQ(submit.form_action_index, 0u);
    const auto action = take_command_form_action(state, backend, false, false, submit.form_action_index);
    ASSERT_TRUE(action);
    EXPECT_EQ(action->command_id, "example.create");
    EXPECT_EQ(action->args, (std::vector<std::string>{"fixed", "edited <&>", "first"}));
    EXPECT_FALSE(state.command_form);
    sync_command_form(state, backend, false);
    EXPECT_TRUE(document->GetElementById("command_form_overlay")->GetInnerRML().empty());
    EXPECT_EQ(action->args[1], "edited <&>");
    state.command_form = CommandPrompt{.actions = {{"create", "Create", "example.create"}, {"cancel", "Cancel", "example.cancel", {"cancel-fixed"}}}, .fields = {{.value = ""}}};
    ++state.command_form_generation;
    const auto cancel = take_command_form_action(state, backend, false, false, 1);
    ASSERT_TRUE(cancel);
    EXPECT_EQ(cancel->args, (std::vector<std::string>{"cancel-fixed"}));
    EXPECT_EQ(handle_command_overlay_target(state, backend, false, nullptr).kind, CommandOverlayActionKind::none);
    document->Close();
    context->Update();
    Rml::RemoveContext("command-submit");
}

TEST(ClientCommandView, BrowseResultsRejectStaleGenerationsAndPreserveCancellation)
{
    using namespace nw::toolset;
    CommandViewState state;
    state.command_form = CommandPrompt{.fields = {{.value = "name"}, {.value = "old-directory"}}};
    state.command_form_generation = 3;
    state.command_form_browse_generation = 2;
    EXPECT_FALSE(apply_command_form_directory_result(state, "late-directory", "", false));
    EXPECT_EQ(state.command_form->fields[1].value, "old-directory");
    state.command_form_browse_generation = 3;
    ASSERT_TRUE(apply_command_form_directory_result(state, "chosen-directory", "", false));
    EXPECT_EQ(state.command_form->fields[1].value, "chosen-directory");
    EXPECT_EQ(state.command_form_generation, 4u);
    state.command_form_browse_generation = 4;
    ASSERT_TRUE(apply_command_form_directory_result(state, "ignored", "", true));
    EXPECT_EQ(state.command_form->fields[1].value, "chosen-directory");
    state.command_form_browse_generation = 5;
    ASSERT_TRUE(apply_command_form_directory_result(state, "ignored", "dialog failed", false));
    EXPECT_EQ(state.command_form->detail, "dialog failed");
    EXPECT_EQ(state.command_form->fields[1].value, "chosen-directory");
    state.command_form.reset();
    EXPECT_FALSE(apply_command_form_directory_result(state, "ignored", "", false));
    CommandResult native{.prompt = CommandPrompt{.id = "save"}};
    EXPECT_FALSE(take_command_form_prompt(state, native));
    EXPECT_TRUE(native.prompt);
    native.prompt->id = "blueprint.confirm";
    EXPECT_TRUE(take_command_form_prompt(state, native));
    EXPECT_EQ(state.command_form->id, "blueprint.confirm");
}

TEST(ClientCommandView, ResultBatchesPreserveChannelsAndSuppressUnloggedResults)
{
    using namespace nw::toolset;
    ShellController shell;
    const std::array results{
        CommandResult{.message = "first", .output_channel = CommandOutputChannel::warn},
        CommandResult{.message = "hidden", .output_channel = CommandOutputChannel::none},
        CommandResult{.status = CommandStatus::failed, .message = "failed", .output_channel = CommandOutputChannel::error},
    };
    append_command_results(shell, results);
    ASSERT_EQ(shell.output_lines.size(), 2u);
    EXPECT_EQ(shell.output_lines[0], (std::pair<std::string, std::string>{"warn", "first"}));
    EXPECT_EQ(shell.output_lines[1], (std::pair<std::string, std::string>{"error", "failed"}));
    EXPECT_TRUE(shell.terminal_lines.empty());
    append_terminal_results(shell, results);
    EXPECT_EQ(shell.output_lines.size(), 4u);
    ASSERT_EQ(shell.terminal_lines.size(), 2u);
    EXPECT_EQ(shell.terminal_lines[0], shell.output_lines[0]);
    EXPECT_EQ(shell.terminal_lines[1], shell.output_lines[1]);
}

TEST(ClientRmlTemplates, TilePaletteUsesLiveTilesetAndPreservesStableVirtualRows)
{
    using namespace nw::toolset;
    auto* tileset = nw::kernel::tilesets().load("ttr01");
    ASSERT_NE(tileset, nullptr);
    auto* area = nw::kernel::objects().make<nw::Area>();
    ASSERT_NE(area, nullptr);
    ObjectDocument owner;
    ASSERT_TRUE(owner.adopt(area->handle()));
    area->tileset = tileset;
    area->tileset_resref = "ttr01";
    area->width = 1;
    area->height = 1;
    area->tiles = {{.id = 0}};
    AreaTileEditorState editor;
    ASSERT_TRUE(reset_area_tile_editor(editor, area->handle()));
    ASSERT_EQ(editor.palette.matches.size(), 3u);
    CurrentPathScope source_root{ROLLNW_TEST_SOURCE_DIR};
    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    auto* context = Rml::CreateContext("tile-palette-view", {1200, 700});
    ASSERT_NE(context, nullptr);
    auto* document = context->LoadDocument("tools/client/ui/panel.rml");
    ASSERT_NE(document, nullptr);
    std::string markup;
    append_area_tile_palette_markup(markup, editor);
    document->GetElementById("workspace_content")->SetInnerRML(markup);
    document->Show();
    context->Update();
    ASSERT_TRUE(sync_area_tile_palette_window(document, editor, area->handle(), true, AreaTilePointerModifier::none, true));
    auto* rows = document->GetElementById("area_tile_palette_rows");
    ASSERT_NE(rows, nullptr);
    auto* first_row = rows->GetChild(0);
    ASSERT_NE(first_row, nullptr);
    EXPECT_FALSE(sync_area_tile_palette_window(document, editor, area->handle(), true, AreaTilePointerModifier::none, false));
    EXPECT_EQ(rows->GetChild(0), first_row);
    EXPECT_FALSE(sync_area_tile_palette_window(document, editor, area->handle(), false, AreaTilePointerModifier::none, true));
    EXPECT_FALSE(sync_area_tile_palette_window(nullptr, editor, area->handle(), true, AreaTilePointerModifier::none, true));

    editor.query = "no-such-tile-action-987654321";
    ASSERT_TRUE(rebuild_area_tile_palette(editor, area->handle()));
    EXPECT_TRUE(editor.palette.matches.empty());
    ASSERT_TRUE(sync_area_tile_palette_window(document, editor, area->handle(), true, AreaTilePointerModifier::select, false));
    EXPECT_NE(rows->GetInnerRML().find("No actions match this filter."), std::string::npos);
    EXPECT_TRUE(document->GetElementById("area_tile_modifier_hint")->IsClassSet("visible"));
    ASSERT_TRUE(build_area_tile_selection(area->handle(), 0, editor.selection).ok());
    sync_area_tile_selection_info(document, editor, area->handle());
    EXPECT_TRUE(document->GetElementById("area_tile_selection_info")->IsClassSet("visible"));
    area->width = 0;
    sync_area_tile_selection_info(document, editor, area->handle());
    EXPECT_FALSE(document->GetElementById("area_tile_selection_info")->IsClassSet("visible"));
    area->width = 1;
    document->Close();
    context->Update();
    Rml::RemoveContext("tile-palette-view");
}

class ClientResourceDrag : public ::testing::Test {
protected:
    void SetUp() override
    {
        directory = std::filesystem::absolute(std::filesystem::path{"tmp/client_resource_drag"}
            / ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::remove_all(directory);
        std::filesystem::create_directories(directory);
        ASSERT_TRUE(nw::kernel::load_module("test_data/user/modules/DockerDemo.mod"));
        auto* creature = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
        ASSERT_NE(creature, nullptr);
        ASSERT_TRUE(owner.adopt(creature->handle()));
        auto* item = nw::kernel::objects().load<nw::Item>("x2_it_mbelt001");
        ASSERT_NE(item, nullptr);
        nw::toolset::ObjectDocument source_owner;
        ASSERT_TRUE(source_owner.adopt(item->handle()));
        source = directory / "drag-item.uti.json";
        std::ofstream{source} << "{}";
        std::string error;
        ASSERT_TRUE(nw::toolset::save_live_blueprint_json_atomic(item->handle(), source, error)) << error;
        workspace.open_tab("preview:drag", "Drag owner", nw::toolset::WorkspaceTabKind::preview);
        view = {
            .active_tab_id = workspace.active_tab_id(),
            .inventory_object = creature->handle(),
            .surface = nw::toolset::ObjectWorkbenchSurface::inventory,
            .inventory_matches_tab = true,
        };
        backend.bind(nullptr, &shell, &workspace);
    }

    void TearDown() override
    {
        nw::toolset::cancel_project_blueprint_drag(nullptr, drag);
        nw::toolset::script_command_host().bind(nullptr, nullptr);
        workspace.clear();
        owner.reset();
        std::filesystem::remove_all(directory);
    }

    std::filesystem::path directory;
    std::filesystem::path source;
    nw::toolset::ObjectDocument owner;
    nw::toolset::WorkspaceState workspace;
    nw::toolset::ShellController shell;
    nw::toolset::ToolsetBackend backend;
    nw::toolset::ProjectResourceDragContext view;
    nw::toolset::ProjectBlueprintDragState drag;
    int pressed_row = 7;
};

TEST_F(ClientResourceDrag, JitterDoesNotMaterializeAndContextSwitchCancelsTheTarget)
{
    using namespace nw::toolset;
    const auto resource = nw::Resource::from_filename(source.filename().string());
    ASSERT_TRUE(arm_project_blueprint_drag(drag, view, resource, source, {100, 200}));
    EXPECT_FALSE(update_project_blueprint_drag(nullptr, nullptr, drag, view, {104, 204}, 0, pressed_row, shell));
    EXPECT_EQ(pressed_row, 7);
    EXPECT_EQ(drag.item.type, nw::ObjectType::invalid);
    EXPECT_FALSE(drag.threshold_crossed);
    ASSERT_TRUE(update_project_blueprint_drag(nullptr, nullptr, drag, view, {105, 200}, 0, pressed_row, shell));
    EXPECT_EQ(pressed_row, -1);
    const auto item = drag.item;
    ASSERT_TRUE(nw::kernel::objects().valid(item));
    EXPECT_EQ(drag.phase, ProjectBlueprintDragPhase::target_invalid);
    EXPECT_TRUE(project_blueprint_drag_context_matches(drag, view));
    auto stale_view = view;
    stale_view.active_tab_id = "other-tab";
    EXPECT_FALSE(project_blueprint_drag_context_matches(drag, stale_view));
    ASSERT_TRUE(update_project_blueprint_drag(nullptr, nullptr, drag, stale_view, {105, 200}, 0, pressed_row, shell));
    EXPECT_EQ(drag.target.kind, ProjectBlueprintDropTargetKind::none);
    EXPECT_EQ(drag.phase, ProjectBlueprintDragPhase::target_invalid);
    cancel_project_blueprint_drag(nullptr, drag);
    EXPECT_FALSE(nw::kernel::objects().valid(item));
    EXPECT_FALSE(drag.active());
}

TEST_F(ClientResourceDrag, MalformedSourcesAreNotRetriedUntilAnotherArm)
{
    using namespace nw::toolset;
    const auto resource = nw::Resource::from_filename(source.filename().string());
    std::ifstream original_file{source};
    const std::string original{std::istreambuf_iterator<char>{original_file}, {}};
    original_file.close();
    {
        std::ofstream invalid{source};
        invalid << "invalid-json";
    }
    ASSERT_TRUE(arm_project_blueprint_drag(drag, view, resource, source, {0, 0}));
    ASSERT_TRUE(update_project_blueprint_drag(nullptr, nullptr, drag, view, {5, 0}, 0, pressed_row, shell));
    EXPECT_TRUE(drag.materialization_failed);
    const auto logged = shell.output_lines.size();
    {
        std::ofstream restored{source};
        restored << original;
    }
    ASSERT_TRUE(update_project_blueprint_drag(nullptr, nullptr, drag, view, {6, 0}, 0, pressed_row, shell));
    EXPECT_EQ(drag.item.type, nw::ObjectType::invalid);
    EXPECT_EQ(shell.output_lines.size(), logged);
    cancel_project_blueprint_drag(nullptr, drag);
    ASSERT_TRUE(arm_project_blueprint_drag(drag, view, resource, source, {0, 0}));
    ASSERT_TRUE(update_project_blueprint_drag(nullptr, nullptr, drag, view, {5, 0}, 0, pressed_row, shell));
    EXPECT_TRUE(nw::kernel::objects().valid(drag.item));
}

TEST_F(ClientResourceDrag, InventoryCommitTransfersTheTemporaryItemAndUndoRetainsIt)
{
    using namespace nw::toolset;
    auto* creature = nw::kernel::objects().get<nw::Creature>(view.inventory_object);
    ASSERT_NE(creature, nullptr);
    const auto before_count = creature->inventory().items.size();
    const auto resource = nw::Resource::from_filename(source.filename().string());
    ASSERT_TRUE(arm_project_blueprint_drag(drag, view, resource, source, {0, 0}));
    ASSERT_TRUE(update_project_blueprint_drag(nullptr, nullptr, drag, view, {5, 0}, 0, pressed_row, shell));
    const auto item = drag.item;
    ASSERT_TRUE(nw::kernel::objects().valid(item));
    const auto slot = creature->inventory().find_slot(drag.width, drag.height);
    ASSERT_GE(slot.page, 0);

    CurrentPathScope source_root{ROLLNW_TEST_SOURCE_DIR};
    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    auto* context = Rml::CreateContext("resource-drop", {1200, 700});
    ASSERT_NE(context, nullptr);
    auto* document = context->LoadDocument("tools/client/ui/panel.rml");
    ASSERT_NE(document, nullptr);
    document->GetElementById("workspace_content")->SetInnerRML("<div id='creature_inventory_board' class='creature_inventory_board' style='width:" + std::to_string(creature->inventory().columns() * 32) + "px;height:" + std::to_string(creature->inventory().rows() * 32) + "px;'><div id='creature_inventory_drop_target' class='creature_inventory_drop_target'></div></div>");
    document->Show();
    context->Update();
    auto* board = document->GetElementById("creature_inventory_board");
    ASSERT_NE(board, nullptr);
    const Rml::Vector2f point{
        board->GetAbsoluteLeft() + board->GetClientLeft() + static_cast<float>(slot.col * 32 + 16),
        board->GetAbsoluteTop() + board->GetClientTop() + static_cast<float>((slot.row - drag.height + 1) * 32 + 16)};
    ASSERT_TRUE(update_project_blueprint_drag(context, document, drag, view, point, slot.page, pressed_row, shell));
    ASSERT_EQ(drag.phase, ProjectBlueprintDragPhase::target_valid);
    EXPECT_TRUE(document->GetElementById("creature_inventory_drop_target")->IsClassSet("valid"));
    CommandContext command_context{
        .active_tab_id = workspace.active_tab_id(),
        .source = CommandSource::renderer,
        .workspace = &workspace};
    commit_project_blueprint_drag(document, drag, backend, command_context, shell);
    EXPECT_FALSE(drag.active());
    EXPECT_TRUE(nw::kernel::objects().valid(item));
    EXPECT_EQ(creature->inventory().items.size(), before_count + 1);
    ASSERT_EQ(workspace.undo_count(), 1u);
    ASSERT_TRUE(workspace.undo(command_context).ok());
    EXPECT_EQ(creature->inventory().items.size(), before_count);
    EXPECT_TRUE(nw::kernel::objects().valid(item));
    ASSERT_TRUE(workspace.redo(command_context).ok());
    EXPECT_EQ(creature->inventory().items.size(), before_count + 1);
    cancel_project_blueprint_drag(document, drag);
    EXPECT_TRUE(nw::kernel::objects().valid(item));
    document->Close();
    context->Update();
    Rml::RemoveContext("resource-drop");
}

TEST(ClientAreaObjectEditor, PlacementBoundsRejectNonfiniteOutOfRangeAndStaleAreas)
{
    using namespace nw::toolset;
    auto* area = nw::kernel::objects().make<nw::Area>();
    ASSERT_NE(area, nullptr);
    ObjectDocument owner;
    ASSERT_TRUE(owner.adopt(area->handle()));
    area->width = 2;
    area->height = 3;
    const auto handle = area->handle();
    EXPECT_TRUE(area_object_placement_position_valid(handle, {0, 0, 0}));
    EXPECT_TRUE(area_object_placement_position_valid(handle, {20, 30, -100}));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (const glm::vec3 point : {glm::vec3{-1, 0, 0}, glm::vec3{0, -1, 0},
             glm::vec3{21, 0, 0}, glm::vec3{0, 31, 0}, glm::vec3{nan, 0, 0},
             glm::vec3{0, nan, 0}, glm::vec3{0, 0, nan}, glm::vec3{inf, 0, 0},
             glm::vec3{0, inf, 0}, glm::vec3{0, 0, inf}}) {
        EXPECT_FALSE(area_object_placement_position_valid(handle, point));
    }
    area->width = 0;
    EXPECT_FALSE(area_object_placement_position_valid(handle, {0, 0, 0}));
    owner.reset();
    EXPECT_FALSE(area_object_placement_position_valid(handle, {0, 0, 0}));
    EXPECT_FALSE(area_object_placement_position_valid(nw::ObjectHandle{}, {0, 0, 0}));
}

TEST(ClientAreaTileEditor, BrushSelectionAndRotationInvalidateStationaryCursor)
{
    using namespace nw::toolset;
    AreaTileEditorState editor;
    editor.palette.rows = {
        {.kind = AreaTilePaletteRowKind::folder},
        {.kind = AreaTilePaletteRowKind::action, .brush = {.kind = AreaTileBrushKind::group, .value = 1}},
        {.kind = AreaTilePaletteRowKind::action, .brush = {.kind = AreaTileBrushKind::raise}},
    };
    EXPECT_FALSE(selected_area_tile_brush(editor, AreaTilePointerButton::primary));
    EXPECT_FALSE(rotate_area_tile_group_orientation(editor));
    editor.selected_row = 0;
    EXPECT_FALSE(selected_area_tile_brush(editor, AreaTilePointerButton::primary));
    editor.selected_row = 1;
    EXPECT_FALSE(selected_area_tile_brush(editor, AreaTilePointerButton::secondary));
    EXPECT_FALSE(selected_area_tile_brush(editor, static_cast<AreaTilePointerButton>(255)));
    for (int32_t rotation = 1; rotation <= 4; ++rotation) {
        editor.cursor_target_index = 5;
        editor.cursor_update_pending = true;
        editor.pending_cursor_point = {100, 200};
        ASSERT_TRUE(rotate_area_tile_group_orientation(editor));
        EXPECT_EQ(editor.group_orientation, rotation % 4);
        EXPECT_EQ(editor.cursor_target_index, UINT32_MAX);
        EXPECT_FALSE(editor.cursor_update_pending);
        EXPECT_EQ(editor.pending_cursor_point, (Rml::Vector2f{100, 200}));
        EXPECT_EQ(selected_area_tile_brush(editor, AreaTilePointerButton::primary)->orientation, rotation % 4);
    }
    editor.stroke.active = true;
    EXPECT_FALSE(rotate_area_tile_group_orientation(editor));
    EXPECT_EQ(editor.group_orientation, 0);
    editor.stroke.active = false;
    editor.selected_row = 2;
    ASSERT_TRUE(selected_area_tile_brush(editor, AreaTilePointerButton::secondary));
    EXPECT_EQ(selected_area_tile_brush(editor, AreaTilePointerButton::secondary)->kind, AreaTileBrushKind::lower);
    EXPECT_FALSE(rotate_area_tile_group_orientation(editor));
    editor.selected_row = 3;
    EXPECT_FALSE(selected_area_tile_brush(editor, AreaTilePointerButton::primary));
}

TEST(ClientAreaTileEditor, HeightPreviewCellsRemainUniqueAcrossOverlappingCorners)
{
    using namespace nw::toolset;
    AreaTileStrokeState stroke;
    stroke.width = 2;
    stroke.height = 2;
    stroke.previewed_tiles.resize(4);
    const std::array corners{0u, 4u, 8u, UINT32_MAX};
    ASSERT_TRUE(append_area_tile_height_preview_cells(stroke, corners));
    EXPECT_EQ(stroke.tile_indices, (std::vector<uint32_t>{0, 1, 2, 3}));
    EXPECT_EQ(stroke.previewed_tiles, (std::vector<uint8_t>{1, 1, 1, 1}));
    ASSERT_TRUE(append_area_tile_height_preview_cells(stroke, corners));
    EXPECT_EQ(stroke.tile_indices.size(), 4u);
}

TEST(ClientAreaTileEditor, ViewportContainsPointsWithTheExistingHalfOpenEdges)
{
    const ClientViewportRect rect{.x = -10, .y = 20, .width = 100, .height = 50};
    EXPECT_TRUE(rect.contains_point(-10, 20));
    EXPECT_TRUE(rect.contains_point(89, 69));
    EXPECT_FALSE(rect.contains_point(90, 69));
    EXPECT_FALSE(rect.contains_point(89, 70));
    EXPECT_FALSE(rect.contains_point(-11, 20));
    EXPECT_FALSE(ClientViewportRect{}.contains_point(0, 0));
}

TEST(ClientRmlTemplates, ImportPanelShowsPathsActionsAndBusyBarWithinBounds)
{
    CurrentPathScope source_root{ROLLNW_TEST_SOURCE_DIR};
    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    ASSERT_TRUE(Rml::LoadFontFace("tools/client/assets/fonts/inter/Inter-Regular.ttf"));
    const std::string source = R"RML(
<rml>
<head>
<link type="text/css" href="tools/client/ui/panel.rcss"/>
<style>body, button { font-family: Inter; font-weight: normal; }</style>
</head>
<body>
<div id="import-panel" class="home_import_panel">
  <div class="home_section_title">Import Module</div>
  <div class="home_import_row">
    <div class="home_import_label">Source</div>
    <div id="source" class="home_import_path">/a/very/long/path/to/Neverwinter Nights/modules/example.mod</div>
    <button id="browse" disabled>Browse...</button>
  </div>
  <div class="home_import_row">
    <div class="home_import_label">Destination</div>
    <div id="destination" class="home_import_path">/another/long/path/to/projects/example</div>
    <button disabled>Browse...</button>
  </div>
  <div class="home_project_actions"><button id="start" disabled>Import</button><button disabled>Close</button></div>
  <div id="progress" class="home_import_progress"><div id="fill" class="home_import_progress_fill"></div></div>
</div>
</body>
</rml>
)RML";
    auto* context = Rml::CreateContext("import-panel-test", {800, 400});
    ASSERT_NE(context, nullptr);
    auto* document = context->LoadDocumentFromMemory(source, "import_panel_test.rml");
    ASSERT_NE(document, nullptr);
    document->Show();
    for (const int width : {800, 460}) {
        context->SetDimensions({width, 400});
        context->Update();
        auto* panel = document->GetElementById("import-panel");
        ASSERT_NE(panel, nullptr);
        for (const auto* id : {"source", "destination", "browse", "start", "progress", "fill"}) {
            SCOPED_TRACE(id);
            auto* element = document->GetElementById(id);
            ASSERT_NE(element, nullptr);
            EXPECT_TRUE(element->IsVisible(true));
            EXPECT_GT(element->GetOffsetWidth(), 0.0f);
            EXPECT_GT(element->GetOffsetHeight(), 0.0f);
            EXPECT_LE(element->GetAbsoluteLeft() + element->GetOffsetWidth(),
                panel->GetAbsoluteLeft() + panel->GetOffsetWidth());
        }
        EXPECT_EQ(document->GetElementById("progress")->GetOffsetHeight(), 8.0f);
        EXPECT_TRUE(document->GetElementById("start")->HasAttribute("disabled"));
    }
    document->Close();
    context->Update();
    Rml::RemoveContext("import-panel-test");
}

TEST(ClientRmlTemplates, AreaTabDirtyIndicatorIsVisibleWithoutMovingItsIcon)
{
    CurrentPathScope source_root{ROLLNW_TEST_SOURCE_DIR};
    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    const std::string source = R"RML(
<rml>
<head><link type="text/css" href="tools/client/ui/panel.rcss"/></head>
<body>
<div id="area-tab" class="workspace_tab workspace_tab_area locked active">
  <span class="workspace_tab_title">
    <span id="area-icon" class="workspace_tab_graphic workspace_tab_area_graphic"></span>
  </span>
  <span id="dirty-indicator" class="workspace_tab_dirty" title="Unsaved changes"></span>
</div>
</body>
</rml>
)RML";
    auto* context = Rml::CreateContext("area-tab-dirty-test", {200, 100});
    ASSERT_NE(context, nullptr);
    auto* document = context->LoadDocumentFromMemory(source, "area_tab_dirty_test.rml");
    ASSERT_NE(document, nullptr);
    document->Show();
    context->Update();
    auto* tab = document->GetElementById("area-tab");
    auto* icon = document->GetElementById("area-icon");
    auto* indicator = document->GetElementById("dirty-indicator");
    ASSERT_NE(tab, nullptr);
    ASSERT_NE(icon, nullptr);
    ASSERT_NE(indicator, nullptr);
    const auto clean_width = tab->GetOffsetWidth();
    const auto clean_icon_offset = icon->GetAbsoluteOffset();
    EXPECT_FALSE(indicator->IsVisible(true));

    tab->SetClass("dirty", true);
    context->Update();
    EXPECT_TRUE(indicator->IsVisible(true));
    EXPECT_GE(indicator->GetOffsetWidth(), 8.0f);
    EXPECT_GE(indicator->GetOffsetHeight(), 8.0f);
    EXPECT_EQ(tab->GetOffsetWidth(), clean_width);
    EXPECT_EQ(icon->GetAbsoluteOffset(), clean_icon_offset);
    const auto offset = indicator->GetAbsoluteOffset() - tab->GetAbsoluteOffset();
    EXPECT_GE(offset.x, 0.0f);
    EXPECT_GE(offset.y, 0.0f);
    EXPECT_LE(offset.x + indicator->GetOffsetWidth(), tab->GetOffsetWidth());
    EXPECT_LE(offset.y + indicator->GetOffsetHeight(), tab->GetOffsetHeight());

    tab->SetClass("active", false);
    context->Update();
    EXPECT_TRUE(indicator->IsVisible(true));
    EXPECT_EQ(tab->GetOffsetWidth(), clean_width);
    tab->SetClass("dirty", false);
    context->Update();
    EXPECT_FALSE(indicator->IsVisible(true));
    EXPECT_EQ(tab->GetOffsetWidth(), clean_width);
    document->Close();
    context->Update();
    Rml::RemoveContext("area-tab-dirty-test");
}

TEST(ClientRmlTemplates, ObjectWorkbenchTabBarProvidesOverflowControls)
{
    CurrentPathScope source_root{ROLLNW_TEST_SOURCE_DIR};
    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    ASSERT_TRUE(Rml::LoadFontFace(
        "tools/client/assets/fonts/inter/Inter-Regular.ttf"));

    const std::string source = R"RML(
<rml>
<head>
<link type="text/css" href="tools/client/ui/panel.rcss"/>
<style>body, button { font-family: Inter; font-weight: normal; }</style>
</head>
<body>
<div class="object_workbench" style="width: 440px;">
  <div id="object_workbench_tab_bar" class="object_workbench_tab_bar">
    <div id="object_workbench_tabs" class="object_workbench_tabs">
      <div id="object_workbench_tab_track" class="object_workbench_tab_track">
        <div class="object_workbench_tab active">Details</div>
        <div class="object_workbench_tab">Variables</div>
        <div class="object_workbench_tab">Classes</div>
        <div class="object_workbench_tab">Appearance</div>
        <div class="object_workbench_tab">Feats</div>
        <div class="object_workbench_tab">Spells</div>
        <div class="object_workbench_tab">Inventory</div>
      </div>
    </div>
    <button id="object_workbench_tabs_previous" class="object_workbench_tab_scroll_button disabled" type="button">&#x2039;</button>
    <button id="object_workbench_tabs_next" class="object_workbench_tab_scroll_button disabled" type="button">&#x203a;</button>
  </div>
</div>
</body>
</rml>
)RML";

    auto* context = Rml::CreateContext(
        "object-workbench-tab-bar-template-test", {800, 200});
    ASSERT_NE(context, nullptr);
    auto* document = context->LoadDocumentFromMemory(
        source, "object_workbench_tab_bar_template_test.rml");
    ASSERT_NE(document, nullptr);
    document->Show();
    context->Update();

    auto* bar = document->GetElementById("object_workbench_tab_bar");
    auto* tabs = document->GetElementById("object_workbench_tabs");
    auto* previous = document->GetElementById(
        "object_workbench_tabs_previous");
    auto* next = document->GetElementById("object_workbench_tabs_next");
    ASSERT_NE(bar, nullptr);
    ASSERT_NE(tabs, nullptr);
    ASSERT_NE(previous, nullptr);
    ASSERT_NE(next, nullptr);
    EXPECT_GT(previous->GetOffsetWidth(), 0.0f);
    EXPECT_GT(next->GetOffsetWidth(), 0.0f);
    EXPECT_LT(tabs->GetClientWidth(), bar->GetClientWidth());
    EXPECT_GT(tabs->GetScrollWidth(), tabs->GetClientWidth());
    previous->SetClass("disabled", false);
    next->SetClass("disabled", true);
    EXPECT_FALSE(previous->IsClassSet("disabled"));
    EXPECT_TRUE(next->IsClassSet("disabled"));

    document->Close();
    context->Update();
    Rml::RemoveContext("object-workbench-tab-bar-template-test");
}

TEST(ClientRmlVirtualList, SpacerExtentAndFinalWindowRepresentAllRows)
{
    constexpr int row_count = 4'340;
    constexpr int row_height = 30;
    constexpr int viewport_height = 300;

    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());

    auto* context = Rml::CreateContext("virtual-list-rml-test", {800, 600});
    ASSERT_NE(context, nullptr);
    auto* document = context->LoadDocumentFromMemory(R"RML(
<rml>
<head><style>
#list { display: block; width: 300px; height: 300px; overflow-y: auto; }
.vl_row { display: block; width: 100%; height: 30px; }
</style></head>
<body><div id="list"></div></body>
</rml>
)RML");
    ASSERT_NE(document, nullptr);
    document->Show();
    context->Update();

    nw::toolset::VirtualListController controller;
    controller.set_row_height(row_height);
    controller.set_overscan(4);
    controller.set_total_rows(row_count);
    controller.set_viewport_height(viewport_height);
    const RmlVirtualListAdapter adapter{row_count};

    auto* list = document->GetElementById("list");
    ASSERT_NE(list, nullptr);
    list->SetInnerRML(nw::toolset::render_virtual_list(controller, adapter));
    context->Update();
    EXPECT_FLOAT_EQ(list->GetClientHeight(), viewport_height);
    EXPECT_FLOAT_EQ(list->GetScrollHeight(), row_count * row_height);

    controller.set_scroll_top(row_count * row_height);
    controller.set_selected(row_count - 1);
    const auto final_range = controller.compute_range();
    ASSERT_EQ(final_range.end, row_count);
    list->SetInnerRML(nw::toolset::render_virtual_list(controller, adapter));
    list->SetScrollTop(static_cast<float>(controller.scroll_top()));
    context->Update();
    EXPECT_FLOAT_EQ(list->GetScrollTop(), row_count * row_height - viewport_height);
    EXPECT_NE(list->GetInnerRML().find("data-key=\"4339\""), std::string::npos);
    EXPECT_NE(list->GetInnerRML().find("class=\"vl_row selected\" data-key=\"4339\""),
        std::string::npos);

    document->Close();
    context->Update();
    Rml::RemoveContext("virtual-list-rml-test");
}

TEST(ClientRmlManagedList, FixedColumnGridMaterializesOnlyVisibleLogicalRows)
{
    constexpr int item_count = 100;
    constexpr int columns = 5;
    constexpr int row_height = 96;
    constexpr int viewport_height = 192;

    nw::toolset::VirtualListHost host;
    ASSERT_TRUE(host.create("models", {
                                          .row_height = row_height,
                                          .overscan = 1,
                                          .columns = columns,
                                      }));
    std::vector<nw::toolset::UiListItem> items;
    items.reserve(item_count);
    for (int index = 0; index < item_count; ++index) {
        items.push_back({
            .key = std::to_string(index),
            .cells = {std::to_string(index), "", "", ""},
            .cell_count = 1,
            .enabled_mask = 1,
        });
    }
    ASSERT_TRUE(host.set_items("models", std::move(items)));
    const auto window = host.window("models", viewport_height, 0);
    ASSERT_TRUE(window);
    const std::string markup = nw::toolset::render_managed_list_window(
        "models", *window, "No models.");

    constexpr std::string_view item_class = "managed_list_grid_item";
    size_t materialized = 0;
    size_t offset = 0;
    while ((offset = markup.find(item_class, offset)) != std::string::npos) {
        ++materialized;
        offset += item_class.size();
    }
    EXPECT_EQ(materialized, 15);
    EXPECT_EQ(window->range.end - window->range.start, 3);
    EXPECT_EQ(window->range.bottom_spacer_px, 17 * row_height);
    EXPECT_EQ(markup.find("data-index=\"15\""), std::string::npos);

    ASSERT_TRUE(host.set_selected("models",
        {.list_id = "models", .key = "99", .index = 99, .cell = -1}, false));
    const auto final_window = host.window(
        "models", viewport_height, item_count * row_height);
    ASSERT_TRUE(final_window);
    EXPECT_EQ(final_window->range.end, item_count / columns);
    const std::string final_markup = nw::toolset::render_managed_list_window(
        "models", *final_window, "No models.");
    EXPECT_NE(final_markup.find("data-index=\"99\""), std::string::npos);
    EXPECT_NE(final_markup.find("managed_list_grid_item selected"),
        std::string::npos);
}

TEST(ClientRmlManagedList, ReorderDestinationUsesOriginalInsertionSlots)
{
    nw::toolset::ManagedListReorderState state{
        .list_id = "sounds",
        .item_count = 4,
        .source_index = 1,
        .insertion_index = 0,
        .dragging = true,
    };
    EXPECT_EQ(nw::toolset::managed_list_reorder_destination(state), 0);

    state.insertion_index = 1;
    EXPECT_FALSE(nw::toolset::managed_list_reorder_destination(state));
    state.insertion_index = 2;
    EXPECT_FALSE(nw::toolset::managed_list_reorder_destination(state));
    state.insertion_index = 4;
    EXPECT_EQ(nw::toolset::managed_list_reorder_destination(state), 3);

    state.insertion_index = 5;
    EXPECT_FALSE(nw::toolset::managed_list_reorder_destination(state));
    state.source_index = -1;
    EXPECT_FALSE(nw::toolset::managed_list_reorder_destination(state));
}

TEST(ClientRmlManagedList, ReorderGestureUsesRowHalvesAndMarksTheDropBoundary)
{
    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    auto* context = Rml::CreateContext(
        "managed-list-reorder-test", {300, 180});
    ASSERT_NE(context, nullptr);
    auto* document = context->LoadDocumentFromMemory(R"RML(
<rml><head><style>
body { margin: 0px; }
#sounds { display: block; width: 200px; height: 90px; overflow-y: auto; }
.managed_list_row { display: block; width: 200px; height: 30px; }
</style></head><body>
<div id="sounds" class="managed_list_rows" data-list-id="sounds">
  <div id="sound-0" class="managed_list_row" data-list-id="sounds" data-index="0">A</div>
  <div id="sound-1" class="managed_list_row" data-list-id="sounds" data-index="1">B</div>
  <div id="sound-2" class="managed_list_row" data-list-id="sounds" data-index="2">C</div>
</div>
</body></rml>)RML");
    ASSERT_NE(document, nullptr);
    document->Show();
    context->Update();

    auto* list = document->GetElementById("sounds");
    auto* row0 = document->GetElementById("sound-0");
    auto* row1 = document->GetElementById("sound-1");
    auto* row2 = document->GetElementById("sound-2");
    ASSERT_NE(list, nullptr);
    ASSERT_NE(row0, nullptr);
    ASSERT_NE(row1, nullptr);
    ASSERT_NE(row2, nullptr);

    nw::toolset::VirtualListHost host;
    ASSERT_TRUE(host.create("sounds", {
                                          .row_height = 30,
                                          .overscan = 1,
                                      }));
    ASSERT_TRUE(host.set_items("sounds", {
                                             {.key = "a", .cells = {"A", "", "", ""}, .cell_count = 1, .enabled_mask = 1},
                                             {.key = "b", .cells = {"B", "", "", ""}, .cell_count = 1, .enabled_mask = 1},
                                             {.key = "c", .cells = {"C", "", "", ""}, .cell_count = 1, .enabled_mask = 1},
                                         }));
    ASSERT_TRUE(host.set_callback(
        "sounds", nw::toolset::UiListEventType::reorder, "on_reorder"));

    nw::toolset::ManagedListReorderState state;
    nw::toolset::ManagedListRenderState render_state;
    const float start_y = row1->GetAbsoluteTop()
        + 0.5f * row1->GetOffsetHeight();
    ASSERT_TRUE(nw::toolset::begin_managed_list_reorder(
        state, row1, host, 10.0f, start_y));
    ASSERT_TRUE(nw::toolset::update_managed_list_reorder(state, document,
        host, render_state, 10.0f, row0->GetAbsoluteTop() + 1.0f,
        5.0f, 15.0f, 10.0f));
    EXPECT_EQ(nw::toolset::managed_list_reorder_destination(state), 0);
    EXPECT_TRUE(row1->IsClassSet("reorder_source"));
    EXPECT_TRUE(row0->IsClassSet("reorder_before"));

    ASSERT_TRUE(nw::toolset::update_managed_list_reorder(state, document,
        host, render_state, 10.0f,
        row2->GetAbsoluteTop() + row2->GetOffsetHeight() - 1.0f,
        5.0f, 15.0f, 10.0f));
    EXPECT_EQ(nw::toolset::managed_list_reorder_destination(state), 2);
    EXPECT_FALSE(row0->IsClassSet("reorder_before"));
    EXPECT_TRUE(row2->IsClassSet("reorder_after"));

    ASSERT_TRUE(nw::toolset::commit_managed_list_reorder(
        state, document, host));
    EXPECT_FALSE(state.active());
    EXPECT_FALSE(row1->IsClassSet("reorder_source"));
    EXPECT_FALSE(row2->IsClassSet("reorder_after"));

    const auto selected = host.get_selected("sounds");
    ASSERT_TRUE(selected);
    EXPECT_EQ(selected->index, 2);
    std::vector<nw::toolset::UiListEvent> events;
    host.drain_events([&](const nw::toolset::UiListEvent& event) {
        events.push_back(event);
    });
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events.front().type, nw::toolset::UiListEventType::reorder);
    EXPECT_EQ(events.front().list_id(), "sounds");
    EXPECT_EQ(events.front().reorder.source_index, 1);
    EXPECT_EQ(events.front().reorder.destination_index, 2);

    ASSERT_TRUE(nw::toolset::begin_managed_list_reorder(
        state, row0, host, 10.0f, row0->GetAbsoluteTop() + 1.0f));
    list->SetProperty("display", "none");
    context->Update();
    ASSERT_TRUE(nw::toolset::update_managed_list_reorder(state, document,
        host, render_state, 10.0f, row0->GetAbsoluteTop() + 1.0f,
        5.0f, 15.0f, 10.0f));
    EXPECT_FALSE(state.active());
    EXPECT_FALSE(row0->IsClassSet("reorder_source"));

    document->Close();
    context->Update();
    Rml::RemoveContext("managed-list-reorder-test");
}

TEST(ClientRmlManagedList, ReorderGestureAutoScrollsOverflowingList)
{
    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    auto* context = Rml::CreateContext(
        "managed-list-reorder-scroll-test", {300, 180});
    ASSERT_NE(context, nullptr);
    auto* document = context->LoadDocumentFromMemory(R"RML(
<rml><head><style>
body { margin: 0px; }
#items { display: block; width: 200px; height: 60px; overflow-y: auto; }
.managed_list_row { display: block; width: 200px; height: 30px; }
</style></head><body>
<div id="items" class="managed_list_rows" data-list-id="items"></div>
</body></rml>)RML");
    ASSERT_NE(document, nullptr);
    document->Show();
    context->Update();

    nw::toolset::VirtualListHost host;
    ASSERT_TRUE(host.create("items", {
                                         .row_height = 30,
                                         .overscan = 1,
                                     }));
    std::vector<nw::toolset::UiListItem> items;
    for (int index = 0; index < 10; ++index) {
        items.push_back({
            .key = std::to_string(index),
            .cells = {std::to_string(index), "", "", ""},
            .cell_count = 1,
            .enabled_mask = 1,
        });
    }
    ASSERT_TRUE(host.set_items("items", std::move(items)));
    ASSERT_TRUE(host.set_callback(
        "items", nw::toolset::UiListEventType::reorder, "on_reorder"));
    nw::toolset::ManagedListRenderState render_state;
    ASSERT_TRUE(nw::toolset::sync_managed_lists(
        document, host, render_state, true));
    context->Update();

    auto* list = document->GetElementById("items");
    ASSERT_NE(list, nullptr);
    EXPECT_TRUE(list->IsClassSet("reorderable"));
    Rml::ElementList rows;
    list->GetElementsByClassName(rows, "managed_list_row");
    ASSERT_GE(rows.size(), 2);

    nw::toolset::ManagedListReorderState state;
    const float start_y = rows.front()->GetAbsoluteTop()
        + 0.5f * rows.front()->GetOffsetHeight();
    ASSERT_TRUE(nw::toolset::begin_managed_list_reorder(
        state, rows.front(), host, 10.0f, start_y));
    ASSERT_TRUE(nw::toolset::update_managed_list_reorder(state, document,
        host, render_state, 10.0f,
        list->GetAbsoluteTop() + list->GetClientHeight() - 1.0f,
        5.0f, 15.0f, 10.0f));
    EXPECT_GT(list->GetScrollTop(), 0.0f);
    nw::toolset::clear_managed_list_reorder(state, document);
    ASSERT_TRUE(host.destroy("items"));
    EXPECT_TRUE(nw::toolset::sync_managed_lists(
        document, host, render_state, false));
    EXPECT_FALSE(list->IsClassSet("reorderable"));

    document->Close();
    context->Update();
    Rml::RemoveContext("managed-list-reorder-scroll-test");
}

TEST(ClientRmlManagedList, LargeSingleColumnSourceMaterializesOnlyViewportAndOverscan)
{
    constexpr int item_count = 4096;
    constexpr int row_height = 30;
    constexpr int viewport_rows = 10;
    constexpr int overscan = 4;

    nw::toolset::VirtualListHost host;
    ASSERT_TRUE(host.create("body-parts", {
                                              .row_height = row_height,
                                              .overscan = overscan,
                                              .columns = 1,
                                          }));
    std::vector<nw::toolset::UiListItem> items;
    items.reserve(item_count);
    for (int index = 0; index < item_count; ++index) {
        items.push_back({
            .key = std::to_string(index),
            .cells = {"Part " + std::to_string(index),
                std::to_string(index), "", ""},
            .cell_count = 2,
            .enabled_mask = 3,
        });
    }
    ASSERT_TRUE(host.set_items("body-parts", std::move(items)));

    const auto window = host.window(
        "body-parts", viewport_rows * row_height, item_count * row_height / 2);
    ASSERT_TRUE(window);
    const std::string markup = nw::toolset::render_managed_list_window(
        "body-parts", *window, "No body parts.");

    constexpr std::string_view row_class = "class=\"managed_list_row";
    size_t materialized = 0;
    for (size_t offset = 0;
        (offset = markup.find(row_class, offset)) != std::string::npos;
        offset += row_class.size()) {
        ++materialized;
    }
    EXPECT_EQ(materialized, viewport_rows + 2 * overscan);
    EXPECT_EQ(window->range.end - window->range.start,
        viewport_rows + 2 * overscan);
    EXPECT_LT(materialized, static_cast<size_t>(item_count));
}

TEST(ClientRmlManagedList, ActivationReturnsFocusAndCyclesDeclaredTarget)
{
    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    auto* context = Rml::CreateContext(
        "managed-list-cycle-target-test", {440, 500});
    ASSERT_NE(context, nullptr);
    auto* document = context->LoadDocumentFromMemory(R"RML(
<rml><body>
  <div id="parts" class="managed_list_rows managed_list_cycle" tabindex="0"
       data-list-id="parts" data-cycle-list-id="models"
       data-focus-after-activate="parts" data-focus-cell="1">
    <div class="managed_list_row selected" data-list-id="parts" data-index="0">
      <span class="managed_list_cell" data-cell="0">Head</span>
      <span id="model-field" class="managed_list_cell cell_1" data-cell="1">119</span>
    </div>
  </div>
  <div id="models" class="managed_list_rows managed_list_cycle"
       data-list-id="models" data-focus-after-activate="parts"
       data-focus-cell="1">
    <div class="managed_list_row" data-list-id="models" data-index="1">
      <span id="model-hit" class="managed_list_cell" data-cell="0">1</span>
    </div>
  </div>
  <button id="combobox-cycle-field" class="combobox_field managed_list_cycle"
          data-list-id="models"
          data-focus-after-activate="combobox-cycle-field">1</button>
</body></rml>
)RML");
    ASSERT_NE(document, nullptr);
    document->Show();
    context->Update();

    nw::toolset::VirtualListHost host;
    ASSERT_TRUE(host.create("parts", {}));
    ASSERT_TRUE(host.create("models", {}));
    ASSERT_TRUE(host.set_items("parts", {{
                                            .key = "head",
                                            .cells = {"Head", "", "", ""},
                                        }}));
    ASSERT_TRUE(host.set_items("models", {
                                             {.key = "0", .cells = {"0", "", "", ""}},
                                             {.key = "1", .cells = {"1", "", "", ""}},
                                             {.key = "2", .cells = {"2", "", "", ""}},
                                         }));

    auto* parts = document->GetElementById("parts");
    auto* model_hit = document->GetElementById("model-hit");
    ASSERT_NE(parts, nullptr);
    ASSERT_NE(model_hit, nullptr);
    const auto activation_focus = nw::toolset::managed_list_focus_target(
        model_hit);
    ASSERT_TRUE(activation_focus);
    ASSERT_TRUE(nw::toolset::activate_managed_list_element(model_hit, host));
    parts->SetInnerRML(
        "<div class='managed_list_row selected' data-list-id='parts' data-index='0'>"
        "<span class='managed_list_cell cell_0' data-cell='0'>Head</span>"
        "<span id='model-field-after-activation' class='managed_list_cell cell_1' "
        "data-cell='1'>1</span></div>");
    context->Update();
    ASSERT_TRUE(nw::toolset::focus_managed_list_target(
        document, *activation_focus));
    auto* model_field = document->GetElementById(
        "model-field-after-activation");
    ASSERT_NE(model_field, nullptr);
    EXPECT_EQ(context->GetFocusElement(), model_field);
    auto selected = host.get_selected("models");
    ASSERT_TRUE(selected);
    EXPECT_EQ(selected->index, 1);

    ASSERT_TRUE(host.set_visible("models", false));
    const auto cycle_focus = nw::toolset::managed_list_focus_target(
        model_field);
    ASSERT_TRUE(cycle_focus);
    ASSERT_TRUE(nw::toolset::cycle_managed_list_element(
        model_field, host, 1));
    parts->SetInnerRML(
        "<div class='managed_list_row selected' data-list-id='parts' data-index='0'>"
        "<span class='managed_list_cell cell_0' data-cell='0'>Head</span>"
        "<span id='model-field-after-cycle' class='managed_list_cell cell_1' "
        "data-cell='1'>2</span></div>");
    context->Update();
    ASSERT_TRUE(nw::toolset::focus_managed_list_target(
        document, *cycle_focus));
    model_field = document->GetElementById("model-field-after-cycle");
    ASSERT_NE(model_field, nullptr);
    EXPECT_EQ(context->GetFocusElement(), model_field);

    const auto mutation_focus = nw::toolset::managed_list_focus_target(
        context->GetFocusElement());
    ASSERT_TRUE(mutation_focus);
    parts->SetInnerRML(
        "<div class='managed_list_row selected' data-list-id='parts' data-index='0'>"
        "<span class='managed_list_cell cell_0' data-cell='0'>Head</span>"
        "<span id='model-field-after-mutation' class='managed_list_cell cell_1' "
        "data-cell='1'>2</span></div>");
    context->Update();
    ASSERT_TRUE(nw::toolset::focus_managed_list_target(
        document, *mutation_focus));
    model_field = document->GetElementById("model-field-after-mutation");
    ASSERT_NE(model_field, nullptr);
    EXPECT_EQ(context->GetFocusElement(), model_field);
    EXPECT_TRUE(model_field->IsPseudoClassSet("focus"));

    ASSERT_TRUE(nw::toolset::cycle_managed_list_element(
        model_field, host, -1));
    selected = host.get_selected("models");
    ASSERT_TRUE(selected);
    EXPECT_EQ(selected->index, 1);
    ASSERT_TRUE(nw::toolset::cycle_managed_list_element(
        model_field, host, 1));

    EXPECT_FALSE(nw::toolset::focus_managed_list_target(document,
        {.element_id = "parts", .cell = 7}));
    EXPECT_EQ(context->GetFocusElement(), model_field);
    selected = host.get_selected("models");
    ASSERT_TRUE(selected);
    EXPECT_EQ(selected->index, 2);
    const auto selected_part = host.get_selected("parts");
    ASSERT_TRUE(selected_part);
    EXPECT_EQ(selected_part->index, -1);

    auto* combobox_field = document->GetElementById(
        "combobox-cycle-field");
    ASSERT_NE(combobox_field, nullptr);
    const auto combobox_focus = nw::toolset::managed_list_focus_target(
        combobox_field);
    ASSERT_TRUE(combobox_focus);
    EXPECT_EQ(combobox_focus->element_id, "combobox-cycle-field");
    ASSERT_TRUE(nw::toolset::cycle_managed_list_element(
        combobox_field, host, -1));
    EXPECT_FLOAT_EQ(combobox_field->GetScrollTop(), 0.0f);
    ASSERT_TRUE(nw::toolset::focus_managed_list_target(
        document, *combobox_focus));
    EXPECT_EQ(context->GetFocusElement(), combobox_field);

    combobox_field->Blur();
    EXPECT_FALSE(combobox_field->IsPseudoClassSet("focus"));
    EXPECT_NE(context->GetFocusElement(), combobox_field);

    document->Close();
    context->Update();
    Rml::RemoveContext("managed-list-cycle-target-test");
}

TEST(ClientRmlManagedList, RevealsChangedSelectionWithoutTrappingUserScroll)
{
    constexpr int item_count = 256;
    constexpr int selected_index = 119;
    constexpr int row_height = 30;
    constexpr int viewport_height = 300;

    CurrentPathScope source_root{ROLLNW_TEST_SOURCE_DIR};
    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    ASSERT_TRUE(Rml::LoadFontFace(
        "tools/client/assets/fonts/inter/Inter-Regular.ttf"));
    auto* context = Rml::CreateContext(
        "managed-list-selection-scroll-test", {440, 500});
    ASSERT_NE(context, nullptr);
    auto* document = context->LoadDocumentFromMemory(R"RML(
<rml>
<head><style>
body { font-family: Inter; }
#rows { display: block; width: 112px; height: 300px; overflow-y: auto; }
.managed_list_spacer { display: block; }
.managed_list_row { display: block; width: 112px; height: 30px; }
</style></head>
<body><div id="rows" class="managed_list_rows"
  data-list-id="options" data-scroll-selected="true"></div></body>
</rml>
)RML");
    ASSERT_NE(document, nullptr);
    document->Show();
    context->Update();

    nw::toolset::VirtualListHost host;
    ASSERT_TRUE(host.create("options", {
                                           .row_height = row_height,
                                           .overscan = 4,
                                           .columns = 1,
                                       }));
    std::vector<nw::toolset::UiListItem> items;
    items.reserve(item_count);
    for (int index = 0; index < item_count; ++index) {
        items.push_back({
            .key = std::to_string(index),
            .cells = {std::to_string(index), "", "", ""},
            .cell_count = 1,
            .enabled_mask = 1,
        });
    }
    ASSERT_TRUE(host.set_items("options", std::move(items)));
    ASSERT_TRUE(host.set_selected("options",
        {.list_id = "options",
            .key = std::to_string(selected_index),
            .index = selected_index,
            .cell = -1},
        false));

    nw::toolset::ManagedListRenderState render_state;
    EXPECT_TRUE(nw::toolset::sync_managed_lists(
        document, host, render_state, false));
    context->Update();
    auto* rows = document->GetElementById("rows");
    ASSERT_NE(rows, nullptr);
    EXPECT_TRUE(nw::toolset::sync_managed_lists(
        document, host, render_state, false));
    context->Update();
    EXPECT_FLOAT_EQ(rows->GetScrollTop(),
        selected_index * row_height + row_height - viewport_height);
    EXPECT_NE(rows->GetInnerRML().find("data-index=\"119\""),
        std::string::npos);

    rows->SetScrollTop(0.0f);
    context->Update();
    EXPECT_TRUE(nw::toolset::sync_managed_lists(
        document, host, render_state, false));
    context->Update();
    EXPECT_FLOAT_EQ(rows->GetScrollTop(), 0.0f);
    EXPECT_NE(rows->GetInnerRML().find("data-index=\"0\""),
        std::string::npos);

    document->Close();
    context->Update();
    Rml::RemoveContext("managed-list-selection-scroll-test");
}

TEST(ClientRmlManagedList, PopupPlacementUsesSelectedCellAndDeclaredBounds)
{
    CurrentPathScope source_root{ROLLNW_TEST_SOURCE_DIR};
    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    ASSERT_TRUE(Rml::LoadFontFace(
        "tools/client/assets/fonts/inter/Inter-Regular.ttf"));
    auto* context = Rml::CreateContext(
        "managed-list-popup-test", Rml::Vector2i{440, 500});
    ASSERT_NE(context, nullptr);
    auto* document = context->LoadDocumentFromMemory(R"RML(
<rml>
<head><style>
body { display: block; width: 440px; height: 500px; font-family: Inter; }
#bounds { display: block; position: relative; width: 440px; height: 500px; }
#spacer { height: 100px; }
.managed_list_row { position: absolute; left: 0px; top: 100px; width: 300px; height: 30px; }
.cell_0 { position: absolute; left: 0px; top: 0px; width: 188px; height: 30px; }
.cell_1 { position: absolute; left: 188px; top: 0px; width: 112px; height: 30px; }
.managed_list_popup { position: absolute; display: block; }
.field_anchor { position: absolute; left: 40px; top: 420px; width: 160px; height: 30px; }
</style></head>
<body>
  <div id="bounds">
    <div id="spacer"></div>
    <div class="managed_list_row selected" data-list-id="parts">
      <span class="managed_list_cell cell_0">Head</span>
      <span class="managed_list_cell cell_1">119</span>
    </div>
    <div id="popup" class="managed_list_popup active"
         data-anchor-list-id="parts" data-anchor-cell="1"
         data-popup-bounds-id="bounds" data-popup-height="300"></div>
    <button id="field_anchor" class="combobox_field field_anchor" type="button">Variation 44</button>
    <div id="field_popup" class="combobox_popup managed_list_popup active"
         data-anchor-element-class="field_anchor"
         data-popup-bounds-id="bounds" data-popup-height="300">
      <span id="field_popup_child">Choice</span>
    </div>
    <div id="outside"></div>
  </div>
</body>
</rml>
)RML");
    ASSERT_NE(document, nullptr);
    document->Show();
    context->Update();

    auto* popup = document->GetElementById("popup");
    auto* field_popup = document->GetElementById("field_popup");
    auto* bounds = document->GetElementById("bounds");
    ASSERT_NE(popup, nullptr);
    ASSERT_NE(field_popup, nullptr);
    ASSERT_NE(bounds, nullptr);
    auto* field_anchor = document->GetElementById("field_anchor");
    auto* field_popup_child = document->GetElementById("field_popup_child");
    auto* outside = document->GetElementById("outside");
    ASSERT_NE(field_anchor, nullptr);
    ASSERT_NE(field_popup_child, nullptr);
    ASSERT_NE(outside, nullptr);
    EXPECT_TRUE(nw::toolset::combobox_contains_element(field_anchor));
    EXPECT_TRUE(nw::toolset::combobox_contains_element(field_popup));
    EXPECT_FALSE(nw::toolset::combobox_contains_element(outside));
    EXPECT_FALSE(nw::toolset::combobox_contains_element(nullptr));
    EXPECT_FALSE(
        nw::toolset::combobox_popup_contains_element(field_anchor));
    EXPECT_TRUE(
        nw::toolset::combobox_popup_contains_element(field_popup));
    EXPECT_TRUE(
        nw::toolset::combobox_popup_contains_element(field_popup_child));
    EXPECT_FALSE(nw::toolset::combobox_popup_contains_element(outside));
    EXPECT_FALSE(nw::toolset::combobox_popup_contains_element(nullptr));
    EXPECT_TRUE(popup->IsClassSet("active"));
    EXPECT_GT(bounds->GetOffsetWidth(), 0.0f);
    EXPECT_GT(bounds->GetOffsetHeight(), 0.0f);
    Rml::ElementList cells;
    document->GetElementsByClassName(cells, "cell_1");
    ASSERT_EQ(cells.size(), 1u);
    EXPECT_GT(cells.front()->GetOffsetWidth(), 0.0f);
    EXPECT_GT(cells.front()->GetOffsetHeight(), 0.0f);
    EXPECT_TRUE(nw::toolset::position_managed_list_popups(document));
    const std::string placement = popup->GetAttribute<Rml::String>(
        "data-popup-placement", "");
    EXPECT_FALSE(placement.empty());
    EXPECT_NE(placement.rfind(":112:300"), std::string::npos);
    const std::string field_placement
        = field_popup->GetAttribute<Rml::String>(
            "data-popup-placement", "");
    EXPECT_FALSE(field_placement.empty());
    EXPECT_NE(field_placement.rfind(":160:300"), std::string::npos);
    EXPECT_FALSE(nw::toolset::position_managed_list_popups(document));

    document->Close();
    context->Update();
    Rml::RemoveContext("managed-list-popup-test");
}

TEST(ClientRmlSmallsLanguageBinding, DispatchesDirectCallsAndAppliesCommands)
{
    KernelServiceScope services;
    auto& runtime = nw::kernel::runtime();
    runtime.add_module_path("stdlib/core");
    runtime.add_module_path("stdlib/toolset");
    ASSERT_TRUE(runtime.load_module("core.creature"));

    nw::toolset::register_smalls_rmlui(runtime);
    auto* rmlui_module = runtime.load_module("core.rmlui");
    ASSERT_NE(rmlui_module, nullptr);
    ASSERT_NE(runtime.get_or_compile_module(rmlui_module), nullptr);

    auto* actions = runtime.load_module_from_source("test.rml_direct_actions", R"(
from core.rmlui import { Event, Command, command_set_rml, command_set_text };
from nwn1.propsets import { CreatureAppearance };

fn select_appearance(event: Event): array!(Command) {
    var creature = event.active_object as Creature;
    var appearance = get_propset!(CreatureAppearance)(creature);
    appearance.appearance = appearance.appearance + 1;
    return {
        { operation = command_set_text, element_id = "result", value = "changed", state = false }
    };
}

fn filter_value(event: Event): array!(Command) {
    return {
        { operation = command_set_text, element_id = "result", value = event.value, state = false }
    };
}

fn runtime_failure() {
    assert(false);
}

fn build_dynamic(): array!(Command) {
    return {{
        operation = command_set_rml,
        element_id = "dynamic",
        value = "<span id='dynamic-result'>built</span>",
        state = false,
    }};
}

fn refresh_dynamic(event: Event): array!(Command) {
    return {{
        operation = command_set_rml,
        element_id = event.element_id,
        value = "<span id='refresh-result'>refreshed</span>",
        state = false,
    }};
}

fn no_active_object(): array!(Command) {
    return {
        { operation = command_set_text, element_id = "result", value = "no object", state = false }
    };
}
)");
    ASSERT_NE(actions, nullptr);
    ASSERT_NE(runtime.get_or_compile_module(actions), nullptr);

    auto* creature = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(creature, nullptr);
    const auto creature_handle = creature->handle();
    runtime.init_object_propsets(creature_handle);
    const auto appearance_type = runtime.type_id("nwn1.propsets.CreatureAppearance", false);
    ASSERT_NE(appearance_type, nw::smalls::invalid_type_id);
    const auto appearance = runtime.get_or_create_propset_ref(appearance_type, creature_handle);
    ASSERT_NE(appearance.type_id, nw::smalls::invalid_type_id);
    const auto* appearance_definition = runtime.get_struct_def(appearance_type);
    ASSERT_NE(appearance_definition, nullptr);
    const auto appearance_field = appearance_definition->field_index("appearance");
    ASSERT_NE(appearance_field, UINT32_MAX);
    ASSERT_TRUE(runtime.write_struct_value_field(
        appearance, appearance_definition, appearance_field, nw::smalls::Value::make_int(6)));

    nw::toolset::RmlSmallsLanguageBinding binding;
    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    ASSERT_TRUE(binding.initialize(runtime));

    auto* context = Rml::CreateContext("smalls-language-binding-test", {800, 600});
    ASSERT_NE(context, nullptr);
    nw::toolset::smalls_rmlui_host().publish_active_object(creature_handle);

    auto* document = context->LoadDocumentFromMemory(R"RML(
<rml>
<head>
  <script>
from test.rml_direct_actions import {
    build_dynamic,
    filter_value,
    no_active_object,
    refresh_dynamic,
    runtime_failure,
    select_appearance,
};
  </script>
</head>
<body id="binding-document">
  <input id="filter" type="text" onchange="filter_value(event)" />
  <button id="select" onclick="select_appearance(event)">Select</button>
  <button id="no-object" onclick="no_active_object()">No object</button>
  <button id="missing" onclick="missing_handler()">Missing</button>
  <button id="runtime-fail" onclick="runtime_failure()">Runtime failure</button>
  <button id="build-dynamic" onclick="build_dynamic()">Build</button>
  <div id="result">unchanged</div>
  <div id="dynamic"></div>
  <div id="refresh-dynamic" class="smalls_refresh" onrefresh="refresh_dynamic(event)"></div>
</body>
</rml>
)RML",
        "binding_inline.rml");
    ASSERT_NE(document, nullptr);
    document->Show();
    context->Update();
    ASSERT_FALSE(diagnostic_contains(binding, "error:"));

    auto* filter = document->GetElementById("filter");
    ASSERT_NE(filter, nullptr);
    filter->Focus();
    (void)context->ProcessTextInput(Rml::String{"dome"});
    ASSERT_NE(document->GetElementById("result"), nullptr);
    EXPECT_EQ(document->GetElementById("result")->GetInnerRML(), "dome");

    auto* select = document->GetElementById("select");
    ASSERT_NE(select, nullptr);
    EXPECT_TRUE(select->DispatchEvent("click", {}));

    const auto updated_appearance = runtime.read_struct_value_field(appearance, appearance_definition, appearance_field);
    EXPECT_EQ(updated_appearance.data.ival, 7);
    ASSERT_NE(document->GetElementById("result"), nullptr);
    EXPECT_EQ(document->GetElementById("result")->GetInnerRML(), "changed");

    ASSERT_NE(document->GetElementById("build-dynamic"), nullptr);
    EXPECT_TRUE(document->GetElementById("build-dynamic")->DispatchEvent("click", {}));
    ASSERT_NE(document->GetElementById("dynamic-result"), nullptr);
    EXPECT_EQ(document->GetElementById("dynamic-result")->GetInnerRML(), "built");

    binding.refresh_elements(document);
    ASSERT_NE(document->GetElementById("refresh-result"), nullptr);
    EXPECT_EQ(document->GetElementById("refresh-result")->GetInnerRML(), "refreshed");

    nlohmann::json serialized_creature;
    bool (*serialize_json)(const nw::Creature*, nlohmann::json&, nw::SerializationProfile) = nw::serialize;
    ASSERT_TRUE(serialize_json(creature, serialized_creature, nw::SerializationProfile::blueprint));
    ASSERT_TRUE(serialized_creature.contains("nwn1.propsets.CreatureAppearance"));
    EXPECT_EQ(serialized_creature["nwn1.propsets.CreatureAppearance"]["appearance"], 7);

    binding.clear_diagnostics();
    ASSERT_NE(document->GetElementById("missing"), nullptr);
    EXPECT_TRUE(document->GetElementById("missing")->DispatchEvent("click", {}));
    EXPECT_TRUE(diagnostic_contains(binding,
        "symbol is not present in the host import scope: missing_handler"));

    binding.clear_diagnostics();
    ASSERT_NE(document->GetElementById("runtime-fail"), nullptr);
    EXPECT_TRUE(document->GetElementById("runtime-fail")->DispatchEvent("click", {}));
    EXPECT_TRUE(diagnostic_contains(binding, "binding-document"));

    auto* invalid_document = context->LoadDocumentFromMemory(R"RML(
<rml>
<head><script>fn broken( {</script></head>
<body id="invalid-document"><div id="still-loaded">Loaded</div></body>
</rml>
)RML",
        "binding_invalid.rml");
    ASSERT_NE(invalid_document, nullptr);
    invalid_document->Show();
    context->Update();
    EXPECT_TRUE(diagnostic_contains(binding, "binding_invalid.rml"));
    EXPECT_NE(invalid_document->GetElementById("still-loaded"), nullptr);

    nw::kernel::objects().destroy(creature_handle);
    ASSERT_FALSE(nw::kernel::objects().valid(creature_handle));
    binding.clear_diagnostics();
    auto* no_object = document->GetElementById("no-object");
    ASSERT_NE(no_object, nullptr);
    EXPECT_TRUE(no_object->DispatchEvent("click", {}));
    EXPECT_EQ(document->GetElementById("result")->GetInnerRML(), "no object");
    EXPECT_TRUE(binding.diagnostics().empty());
    EXPECT_EQ(nw::toolset::smalls_rmlui_host().active_object().type, nw::ObjectType::invalid);

    constexpr size_t retained_diagnostic_limit = 256;
    constexpr size_t extra_failure_count = 3;
    binding.clear_diagnostics();
    const auto stats_before_failures = binding.stats();
    auto* runtime_failure = document->GetElementById("runtime-fail");
    ASSERT_NE(runtime_failure, nullptr);
    for (size_t i = 0; i < retained_diagnostic_limit + extra_failure_count; ++i) {
        EXPECT_TRUE(runtime_failure->DispatchEvent("click", {}));
    }
    const auto stats_after_failures = binding.stats();
    EXPECT_EQ(binding.diagnostics().size(), retained_diagnostic_limit);
    EXPECT_EQ(stats_after_failures.suppressed_diagnostic_count
            - stats_before_failures.suppressed_diagnostic_count,
        extra_failure_count);

    nw::toolset::smalls_rmlui_host().clear_active_object();
    invalid_document->Close();
    document->Close();
    context->Update();
    Rml::RemoveContext("smalls-language-binding-test");
}

TEST(ClientRmlSmallsLanguageBinding, RejectsExternalScripts)
{
    KernelServiceScope services;
    auto& runtime = nw::kernel::runtime();
    runtime.add_module_path("stdlib/core");
    nw::toolset::register_smalls_rmlui(runtime);
    auto* rmlui_module = runtime.load_module("core.rmlui");
    ASSERT_NE(rmlui_module, nullptr);
    ASSERT_NE(runtime.get_or_compile_module(rmlui_module), nullptr);

    nw::toolset::RmlSmallsLanguageBinding binding;
    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    ASSERT_TRUE(binding.initialize(runtime));
    auto* context = Rml::CreateContext("smalls-external-script-test", {800, 600});
    ASSERT_NE(context, nullptr);
    auto* document = context->LoadDocumentFromMemory(R"RML(
<rml><head><script src="external.smalls"></script></head><body></body></rml>
)RML",
        "external_script.rml");
    ASSERT_NE(document, nullptr);
    EXPECT_TRUE(diagnostic_contains(binding,
        "external Smalls scripts are not supported"));
    document->Close();
    context->Update();
    Rml::RemoveContext("smalls-external-script-test");
}

TEST(ClientRmlSmallsLanguageBinding, NamesTemplateSourceForMissingHostImport)
{
    KernelServiceScope services;
    auto& runtime = nw::kernel::runtime();
    runtime.add_module_path("stdlib/core");
    nw::toolset::register_smalls_rmlui(runtime);
    auto* rmlui_module = runtime.load_module("core.rmlui");
    ASSERT_NE(rmlui_module, nullptr);
    ASSERT_NE(runtime.get_or_compile_module(rmlui_module), nullptr);

    nw::toolset::RmlSmallsLanguageBinding binding;
    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    ASSERT_TRUE(binding.initialize(runtime));
    auto* context = Rml::CreateContext("smalls-template-scope-test", {800, 600});
    ASSERT_NE(context, nullptr);
    auto* document = context->LoadDocumentFromMemory(R"RML(
<rml>
<head><script>from core.rmlui import { command_set_text };</script></head>
<body><div data-smalls-source="incomplete_template.rml">
  <button id="missing" onclick="template_action()">Missing</button>
</div></body>
</rml>
)RML",
        "template_host.rml");
    ASSERT_NE(document, nullptr);
    ASSERT_NE(document->GetElementById("missing"), nullptr);
    EXPECT_TRUE(document->GetElementById("missing")->DispatchEvent("click", {}));
    EXPECT_TRUE(diagnostic_contains(binding, "incomplete_template.rml"));
    EXPECT_TRUE(diagnostic_contains(binding,
        "symbol is not present in the host import scope: template_action"));
    document->Close();
    context->Update();
    Rml::RemoveContext("smalls-template-scope-test");
}

TEST(ClientRmlSmallsLanguageBinding, InternsOneTargetForPaletteArguments)
{
    KernelServiceScope services;
    auto& runtime = nw::kernel::runtime();
    runtime.add_module_path("stdlib/core");
    nw::toolset::register_smalls_rmlui(runtime);
    auto* rmlui_module = runtime.load_module("core.rmlui");
    ASSERT_NE(rmlui_module, nullptr);
    ASSERT_NE(runtime.get_or_compile_module(rmlui_module), nullptr);
    auto* actions = runtime.load_module_from_source(
        "test.rml_palette_actions", "fn apply_color(value: int) {}\n");
    ASSERT_NE(actions, nullptr);
    ASSERT_NE(runtime.get_or_compile_module(actions), nullptr);

    nw::toolset::RmlSmallsLanguageBinding binding;
    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    ASSERT_TRUE(binding.initialize(runtime));
    auto* context = Rml::CreateContext("smalls-palette-binding-test", {800, 600});
    ASSERT_NE(context, nullptr);

    std::string markup = R"RML(<rml><head><script>
from test.rml_palette_actions import { apply_color };
</script></head><body>)RML";
    for (int value = 0; value < 176; ++value) {
        markup += "<button id='color_" + std::to_string(value)
            + "' onclick='apply_color(" + std::to_string(value)
            + ")'>Color</button>";
    }
    markup += "</body></rml>";

    const auto before = binding.stats();
    auto* document = context->LoadDocumentFromMemory(
        markup, "palette_binding.rml");
    ASSERT_NE(document, nullptr);
    for (int value = 0; value < 176; ++value) {
        auto* element = document->GetElementById(
            "color_" + std::to_string(value));
        ASSERT_NE(element, nullptr);
        EXPECT_TRUE(element->DispatchEvent("click", {}));
    }
    const auto after = binding.stats();
    EXPECT_EQ(after.interned_target_count - before.interned_target_count, 1);
    EXPECT_EQ(after.bound_listener_count - before.bound_listener_count, 176);
    EXPECT_EQ(after.bound_argument_count - before.bound_argument_count, 176);
    EXPECT_TRUE(binding.diagnostics().empty());

    document->Close();
    context->Update();
    Rml::RemoveContext("smalls-palette-binding-test");
}

TEST(ClientRmlSmallsLanguageBinding, RecompilesDocumentAfterKernelServiceReplacement)
{
    KernelServiceScope services;

    const auto prepare_runtime = []() -> nw::smalls::Runtime& {
        auto& runtime = nw::kernel::runtime();
        runtime.add_module_path("stdlib/core");
        runtime.add_module_path("stdlib/toolset");
        nw::toolset::register_smalls_rmlui(runtime);
        auto* rmlui_module = runtime.load_module("core.rmlui");
        EXPECT_NE(rmlui_module, nullptr);
        if (rmlui_module) {
            EXPECT_NE(runtime.get_or_compile_module(rmlui_module), nullptr);
        }
        auto* actions = runtime.load_module_from_source(
            "test.rml_runtime_actions", R"(
from core.rmlui import { Command, command_set_text };
fn select(): array!(Command) {
    return {{
        operation = command_set_text,
        element_id = "result",
        value = "current runtime",
        state = false,
    }};
}
)");
        EXPECT_NE(actions, nullptr);
        if (actions) {
            EXPECT_NE(runtime.get_or_compile_module(actions), nullptr);
        }
        return runtime;
    };

    auto& first_runtime = prepare_runtime();
    const uint64_t first_generation = nw::kernel::services().generation();

    nw::toolset::RmlSmallsLanguageBinding binding;
    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    ASSERT_TRUE(binding.initialize(first_runtime));

    auto* context = Rml::CreateContext("smalls-runtime-replacement-test", {800, 600});
    ASSERT_NE(context, nullptr);
    auto* document = context->LoadDocumentFromMemory(R"RML(
<rml>
<head><script>
from test.rml_runtime_actions import { select };
</script></head>
<body><button id="select" onclick="select()">Select</button><div id="result">stale</div></body>
</rml>
)RML",
        "runtime_replacement.rml");
    ASSERT_NE(document, nullptr);
    document->Show();
    context->Update();

    auto* first_creature = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(first_creature, nullptr);
    nw::toolset::smalls_rmlui_host().publish_active_object(first_creature->handle());
    ASSERT_NE(document->GetElementById("select"), nullptr);
    EXPECT_TRUE(document->GetElementById("select")->DispatchEvent("click", {}));
    EXPECT_EQ(document->GetElementById("result")->GetInnerRML(), "current runtime");

    nw::kernel::services().shutdown();
    nw::kernel::services().start();
    auto& second_runtime = prepare_runtime();
    EXPECT_GT(nw::kernel::services().generation(), first_generation);
    ASSERT_TRUE(binding.initialize(second_runtime));

    auto* second_creature = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(second_creature, nullptr);
    nw::toolset::smalls_rmlui_host().publish_active_object(second_creature->handle());
    document->GetElementById("result")->SetInnerRML("stale");
    EXPECT_TRUE(document->GetElementById("select")->DispatchEvent("click", {}));
    EXPECT_EQ(document->GetElementById("result")->GetInnerRML(), "current runtime");

    nw::toolset::smalls_rmlui_host().clear_active_object();
    nw::kernel::objects().destroy(second_creature->handle());
    document->Close();
    context->Update();
    Rml::RemoveContext("smalls-runtime-replacement-test");
}

TEST(ClientRmlSmallsLanguageBinding, CompilesRegisteredToolsetEditors)
{
    KernelServiceScope services;
    auto loaded_module = nw::kernel::load_module(
        "test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(loaded_module);
    auto& runtime = nw::kernel::runtime();
    runtime.add_module_path("stdlib/core");
    runtime.add_module_path("stdlib/nwn1");
    runtime.add_module_path("stdlib/toolset");

    const auto load_and_compile = [&](std::string_view module_path) {
        auto* module = runtime.load_module(module_path);
        EXPECT_NE(module, nullptr) << module_path;
        return module && runtime.get_or_compile_module(module) != nullptr;
    };

    ASSERT_TRUE(load_and_compile("core.ui"));
    nw::toolset::register_smalls_ui_v1(runtime);
    ASSERT_TRUE(load_and_compile("core.ui.v1"));

    ASSERT_TRUE(load_and_compile("core.commands"));
    nw::toolset::register_smalls_commands_v1(runtime);
    ASSERT_TRUE(load_and_compile("core.commands.v1"));

    nw::toolset::register_smalls_rmlui(runtime);
    ASSERT_TRUE(load_and_compile("core.rmlui"));
    ASSERT_TRUE(load_and_compile("toolset.ui"));
    ASSERT_TRUE(load_and_compile("toolset.rmlui"));
    ASSERT_TRUE(load_and_compile("toolset.item_editor"));
    ASSERT_TRUE(load_and_compile("toolset.creature_editor"));
    ASSERT_TRUE(load_and_compile("toolset.data_object_editor"));
    nw::toolset::RmlSmallsBridge list_bridge;
    nw::toolset::WorkspaceState workspace;
    workspace.open_tab("creature-preview", "Creature",
        nw::toolset::WorkspaceTabKind::preview);
    nw::toolset::ToolsetBackend backend;
    backend.bind(&list_bridge, nullptr, &workspace);
    ASSERT_TRUE(backend.initialize());

    auto* creature = nw::kernel::objects().load_file<nw::Creature>(
        "test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(creature, nullptr);
    nw::toolset::smalls_rmlui_host().publish_active_object(creature->handle());

    const auto body_parts_refresh = runtime.execute_script(
        "toolset.creature_editor", "body_parts_refresh", {});
    ASSERT_TRUE(body_parts_refresh.ok());
    const auto body_parts = nw::toolset::ui_v1_host().window(
        "creature.appearance.body_parts", 570, 0);
    ASSERT_TRUE(body_parts);
    EXPECT_TRUE(body_parts->visible);
    ASSERT_EQ(body_parts->items.size(), 19u);
    EXPECT_EQ(body_parts->columns, 1);
    for (const auto& item : body_parts->items) {
        EXPECT_EQ(item.cell_count, 2);
        EXPECT_FALSE(item.key.empty());
        EXPECT_FALSE(item.cells[0].empty());
        EXPECT_FALSE(item.cells[1].empty());
    }

    const auto find_body_part = [&](std::string_view label) {
        return std::ranges::find(body_parts->items, label,
            [](const nw::toolset::UiListItem& item) -> std::string_view {
                return item.cells[0];
            });
    };
    const auto head = find_body_part("Head");
    const auto left_bicep = find_body_part("Bicep, Left");
    ASSERT_NE(head, body_parts->items.end());
    ASSERT_NE(left_bicep, body_parts->items.end());
    const int head_index = static_cast<int>(head - body_parts->items.begin());
    const int head_part = std::stoi(head->key);
    const int left_bicep_index = static_cast<int>(
        left_bicep - body_parts->items.begin());

    const auto call_selection = [&](std::string_view function,
                                    std::string_view list_id,
                                    std::string_view key,
                                    int index) {
        const auto selection_type = runtime.type_id(
            "core.ui.ListSelection", false);
        EXPECT_NE(selection_type, nw::smalls::invalid_type_id);
        nw::smalls::Runtime::ScopedRoots roots{runtime, 3};
        const auto selection_ptr = runtime.alloc_struct(selection_type);
        EXPECT_NE(selection_ptr.value, 0u);
        auto selection = nw::smalls::Value::make_heap(
            selection_ptr, selection_type);
        roots.add(selection);
        auto list_value = nw::smalls::Value::make_string(
            runtime.alloc_string(list_id));
        roots.add(list_value);
        auto key_value = nw::smalls::Value::make_string(
            runtime.alloc_string(key));
        roots.add(key_value);
        EXPECT_TRUE(runtime.write_struct_field(
            selection_ptr, selection_type, "list_id", list_value));
        EXPECT_TRUE(runtime.write_struct_field(
            selection_ptr, selection_type, "key", key_value));
        EXPECT_TRUE(runtime.write_struct_field(selection_ptr, selection_type,
            "index", nw::smalls::Value::make_int(index)));
        EXPECT_TRUE(runtime.write_struct_field(selection_ptr, selection_type,
            "cell", nw::smalls::Value::make_int(-1)));
        return runtime.execute_script(
            "toolset.creature_editor", function, {selection});
    };

    const auto dispatch_managed_list_events = [&]() {
        auto& host = nw::toolset::ui_v1_host();
        bool dispatched = false;
        bool succeeded = true;
        host.drain_events([&](const nw::toolset::UiListEvent& event) {
            const auto* callback = host.callback_ptr(
                event.list_id(), event.type);
            if (!callback) {
                return;
            }
            const std::string qualified_function = *callback;
            dispatched = true;
            succeeded = succeeded
                && list_bridge
                       .call_ui_list_callback(qualified_function, event)
                       .ok;
        });
        return dispatched && succeeded;
    };
    const auto activate_managed_list = [&](std::string_view list_id,
                                           int index,
                                           int cell = -1) {
        auto& host = nw::toolset::ui_v1_host();
        return host.push_activate(list_id, index, cell)
            && dispatch_managed_list_events();
    };
    const auto cycle_managed_list = [&](std::string_view list_id, int delta) {
        auto& host = nw::toolset::ui_v1_host();
        return host.move_and_activate(list_id, delta, 300, 0)
            && dispatch_managed_list_events();
    };

    ASSERT_TRUE(activate_managed_list(
        "creature.appearance.body_parts", head_index));
    auto body_part_options = nw::toolset::ui_v1_host().window(
        "creature.appearance.body_part_options", 300, 0);
    ASSERT_TRUE(body_part_options);
    EXPECT_TRUE(body_part_options->visible);
    ASSERT_FALSE(body_part_options->items.empty());
    for (const auto& item : body_part_options->items) {
        EXPECT_EQ(item.cells[0], item.key);
        EXPECT_FALSE(item.cells[0].empty());
    }
    EXPECT_EQ(body_part_options->items.front().key, "0");
    EXPECT_NE(std::ranges::find(body_part_options->items, "1",
                  &nw::toolset::UiListItem::key),
        body_part_options->items.end());
    EXPECT_NE(std::ranges::find(body_part_options->items, "119",
                  &nw::toolset::UiListItem::key),
        body_part_options->items.end());
    EXPECT_EQ(std::ranges::find(body_part_options->items, "255",
                  &nw::toolset::UiListItem::key),
        body_part_options->items.end());
    EXPECT_LT(body_part_options->items.size(), 255u);
    const auto plain_body_part_option = std::ranges::find_if(
        body_part_options->items,
        [](const nw::toolset::UiListItem& item) {
            return item.cells[1].empty();
        });
    ASSERT_NE(plain_body_part_option, body_part_options->items.end());
    EXPECT_EQ(plain_body_part_option->cell_count, 1);
    EXPECT_EQ(plain_body_part_option->enabled_mask, 1u);
    const auto tagged_body_part_option = std::ranges::find_if(
        body_part_options->items,
        [](const nw::toolset::UiListItem& item) {
            return !item.cells[1].empty();
        });
    ASSERT_NE(tagged_body_part_option, body_part_options->items.end());
    EXPECT_EQ(tagged_body_part_option->cell_count, 2);
    EXPECT_EQ(tagged_body_part_option->enabled_mask, 3u);
    ASSERT_GE(body_part_options->selected_index, 0);
    ASSERT_LT(static_cast<size_t>(body_part_options->selected_index),
        body_part_options->items.size());
    EXPECT_EQ(body_part_options->items[static_cast<size_t>(
                                           body_part_options->selected_index)]
                  .key,
        "119");

    const auto body_parts_before = nw::toolset::editable_creature_body_parts(
        runtime, creature->handle());
    ASSERT_GT(body_parts_before.size(), static_cast<size_t>(head_part));
    ASSERT_EQ(body_parts_before[static_cast<size_t>(head_part)], 119);
    const auto one = std::ranges::find(body_part_options->items, "1",
        &nw::toolset::UiListItem::key);
    ASSERT_NE(one, body_part_options->items.end());
    const int one_index = static_cast<int>(
        one - body_part_options->items.begin());

    ASSERT_TRUE(call_selection("on_body_part_option_activate",
        "creature.appearance.body_part_options", "not-the-row-key",
        one_index)
            .ok());
    EXPECT_EQ(nw::toolset::editable_creature_body_parts(
                  runtime, creature->handle())[static_cast<size_t>(head_part)],
        119);
    EXPECT_EQ(workspace.undo_count(), 0u);

    ASSERT_TRUE(activate_managed_list(
        "creature.appearance.body_part_options", one_index));
    EXPECT_EQ(nw::toolset::editable_creature_body_parts(
                  runtime, creature->handle())[static_cast<size_t>(head_part)],
        1);
    EXPECT_EQ(workspace.undo_count(), 1u);
    body_part_options = nw::toolset::ui_v1_host().window(
        "creature.appearance.body_part_options", 300, 0);
    ASSERT_TRUE(body_part_options);
    EXPECT_FALSE(body_part_options->visible);
    EXPECT_FALSE(body_part_options->items.empty());
    ASSERT_GE(body_part_options->selected_index, 0);
    EXPECT_EQ(body_part_options->items[static_cast<size_t>(
                                           body_part_options->selected_index)]
                  .key,
        "1");

    // A committed model keeps the selected part and its option batch active
    // while the popup is hidden, so the focused body-part list can cycle it.
    ASSERT_LT(one_index + 1,
        static_cast<int>(body_part_options->items.size()));
    const int next_value = std::stoi(
        body_part_options->items[static_cast<size_t>(one_index + 1)].key);
    ASSERT_TRUE(cycle_managed_list(
        "creature.appearance.body_part_options", 1));
    EXPECT_EQ(nw::toolset::editable_creature_body_parts(
                  runtime, creature->handle())[static_cast<size_t>(head_part)],
        next_value);
    EXPECT_EQ(workspace.undo_count(), 2u);

    ASSERT_TRUE(cycle_managed_list(
        "creature.appearance.body_part_options", -1));
    EXPECT_EQ(nw::toolset::editable_creature_body_parts(
                  runtime, creature->handle())[static_cast<size_t>(head_part)],
        1);
    EXPECT_EQ(workspace.undo_count(), 3u);
    body_part_options = nw::toolset::ui_v1_host().window(
        "creature.appearance.body_part_options", 300, 0);
    ASSERT_TRUE(body_part_options);
    EXPECT_FALSE(body_part_options->visible);
    EXPECT_FALSE(body_part_options->items.empty());
    EXPECT_EQ(workspace.undo_count(), 3u);

    nw::toolset::CommandContext undo_context;
    undo_context.workspace = &workspace;
    undo_context.active_tab_id = workspace.active_tab_id();
    ASSERT_TRUE(workspace.undo(undo_context).ok());
    EXPECT_EQ(nw::toolset::editable_creature_body_parts(
                  runtime, creature->handle())[static_cast<size_t>(head_part)],
        next_value);
    ASSERT_TRUE(workspace.redo(undo_context).ok());
    EXPECT_EQ(nw::toolset::editable_creature_body_parts(
                  runtime, creature->handle())[static_cast<size_t>(head_part)],
        1);

    ASSERT_TRUE(activate_managed_list(
        "creature.appearance.body_parts", left_bicep_index));
    body_part_options = nw::toolset::ui_v1_host().window(
        "creature.appearance.body_part_options", 300, 0);
    ASSERT_TRUE(body_part_options);
    EXPECT_FALSE(body_part_options->items.empty());
    EXPECT_GE(body_part_options->selected_index, 0);
    EXPECT_TRUE(body_part_options->visible);
    EXPECT_NE(std::ranges::find(body_part_options->items, "255",
                  &nw::toolset::UiListItem::key),
        body_part_options->items.end());

    ASSERT_TRUE(activate_managed_list(
        "creature.appearance.body_parts", left_bicep_index));
    body_part_options = nw::toolset::ui_v1_host().window(
        "creature.appearance.body_part_options", 300, 0);
    ASSERT_TRUE(body_part_options);
    EXPECT_FALSE(body_part_options->visible);
    EXPECT_FALSE(body_part_options->items.empty());
    ASSERT_TRUE(activate_managed_list(
        "creature.appearance.body_parts", left_bicep_index));

    const auto close_body_part_options = runtime.execute_script(
        "toolset.creature_editor", "close_body_part_options", {});
    ASSERT_TRUE(close_body_part_options.ok());
    body_part_options = nw::toolset::ui_v1_host().window(
        "creature.appearance.body_part_options", 300, 0);
    ASSERT_TRUE(body_part_options);
    EXPECT_FALSE(body_part_options->visible);
    ASSERT_TRUE(activate_managed_list(
        "creature.appearance.body_parts", left_bicep_index));
    body_part_options = nw::toolset::ui_v1_host().window(
        "creature.appearance.body_part_options", 300, 0);
    ASSERT_TRUE(body_part_options);
    EXPECT_FALSE(body_part_options->items.empty());
    EXPECT_GE(body_part_options->selected_index, 0);
    EXPECT_TRUE(body_part_options->visible);

    auto* second_creature = nw::kernel::objects().load_file<nw::Creature>(
        "test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(second_creature, nullptr);
    const auto first_parts_before_stale_event
        = nw::toolset::editable_creature_body_parts(
            runtime, creature->handle());
    const auto second_parts_before_stale_event
        = nw::toolset::editable_creature_body_parts(
            runtime, second_creature->handle());
    const size_t undo_count_before_stale_event = workspace.undo_count();
    nw::toolset::smalls_rmlui_host().publish_active_object(
        second_creature->handle());
    ASSERT_TRUE(activate_managed_list(
        "creature.appearance.body_part_options", 0));
    EXPECT_EQ(nw::toolset::editable_creature_body_parts(
                  runtime, creature->handle()),
        first_parts_before_stale_event);
    EXPECT_EQ(nw::toolset::editable_creature_body_parts(
                  runtime, second_creature->handle()),
        second_parts_before_stale_event);
    EXPECT_EQ(workspace.undo_count(), undo_count_before_stale_event);
    nw::toolset::smalls_rmlui_host().publish_active_object(creature->handle());
    nw::kernel::objects().destroy(second_creature->handle());

    nw::kernel::objects().destroy(creature->handle());
    const auto stale_refresh = runtime.execute_script(
        "toolset.creature_editor", "refresh", {});
    ASSERT_TRUE(stale_refresh.ok());
    ASSERT_EQ(stale_refresh.value.type_id, runtime.bool_type());
    EXPECT_TRUE(stale_refresh.value.data.bval);
    const auto cleared_body_parts = nw::toolset::ui_v1_host().window(
        "creature.appearance.body_parts", 570, 0);
    const auto cleared_options = nw::toolset::ui_v1_host().window(
        "creature.appearance.body_part_options", 300, 0);
    ASSERT_TRUE(cleared_body_parts);
    ASSERT_TRUE(cleared_options);
    EXPECT_FALSE(cleared_body_parts->visible);
    EXPECT_TRUE(cleared_body_parts->items.empty());
    EXPECT_FALSE(cleared_options->visible);
    EXPECT_TRUE(cleared_options->items.empty());
    nw::toolset::smalls_rmlui_host().clear_active_object();

    auto* first_item = nw::kernel::objects().load<nw::Item>(
        "x2_it_mbelt001");
    auto* second_item = nw::kernel::objects().load<nw::Item>(
        "x2_it_mbelt001");
    ASSERT_NE(first_item, nullptr);
    ASSERT_NE(second_item, nullptr);
    nw::toolset::smalls_rmlui_host().publish_active_object(
        first_item->handle());
    nw::toolset::CommandContext item_context;
    item_context.workspace = &workspace;
    item_context.active_tab_id = workspace.active_tab_id();
    ASSERT_TRUE(backend.execute_command(
                           "toolset.item.initialize", {}, item_context)
            .ok());

    auto available_properties = nw::toolset::ui_v1_host().window(
        "item.properties.available", 300, 0);
    ASSERT_TRUE(available_properties);
    const auto ability_bonus = std::ranges::find(
        available_properties->items, "Ability Bonus",
        [](const nw::toolset::UiListItem& item) -> std::string_view {
            return item.cells[0];
        });
    ASSERT_NE(ability_bonus, available_properties->items.end());
    const int ability_bonus_index = static_cast<int>(
        ability_bonus - available_properties->items.begin());
    ASSERT_TRUE(nw::toolset::ui_v1_host().set_selected(
        "item.properties.available",
        {
            .list_id = "item.properties.available",
            .key = ability_bonus->key,
            .index = ability_bonus_index,
            .cell = 0,
        },
        false));
    const auto properties_before_add = nw::toolset::snapshot_item_property_records(
        runtime, first_item->handle());
    ASSERT_TRUE(properties_before_add);
    ASSERT_TRUE(backend.execute_command(
                           "toolset.item.properties.add", {}, item_context)
            .ok());

    auto applied_properties = nw::toolset::ui_v1_host().window(
        "item.properties.applied", 300, 0);
    ASSERT_TRUE(applied_properties);
    ASSERT_EQ(applied_properties->items.size(),
        properties_before_add->size() + 1);
    const int inserted_property_index = static_cast<int>(
        properties_before_add->size());
    const auto& inserted_property = applied_properties->items[static_cast<size_t>(inserted_property_index)];
    EXPECT_EQ(inserted_property.cells[0], "Ability Bonus");
    EXPECT_EQ(inserted_property.enabled_mask & 2u, 2u);

    const std::array<std::string, 4> property_name_args{
        "item.properties.applied", inserted_property.key,
        std::to_string(inserted_property_index), "0"};
    const std::vector<std::string_view> property_name_views{
        property_name_args.begin(), property_name_args.end()};
    EXPECT_TRUE(backend.execute_command(
                           "toolset.item.properties.applied.activate",
                           property_name_views, item_context)
            .ok());
    auto property_options = nw::toolset::ui_v1_host().window(
        "item.properties.options", 300, 0);
    ASSERT_TRUE(property_options);
    EXPECT_FALSE(property_options->visible);

    ASSERT_TRUE(activate_managed_list(
        "item.properties.applied", inserted_property_index, 1));
    property_options = nw::toolset::ui_v1_host().window(
        "item.properties.options", 300, 0);
    ASSERT_TRUE(property_options);
    ASSERT_TRUE(property_options->visible);
    ASSERT_GT(property_options->items.size(), 1u);
    ASSERT_GE(property_options->selected_index, 0);

    // A managed-list activation refreshes every .smalls_refresh element after
    // dispatch. The Item details refresh must not immediately discard the
    // selector opened by the activation callback.
    ASSERT_TRUE(backend.execute_command(
                           "toolset.item.details", {}, item_context)
            .ok());
    property_options = nw::toolset::ui_v1_host().window(
        "item.properties.options", 300, 0);
    ASSERT_TRUE(property_options);
    ASSERT_TRUE(property_options->visible);

    const int replacement_property_option
        = property_options->selected_index == 0 ? 1 : 0;
    const int32_t replacement_subtype = std::stoi(
        property_options->items[static_cast<size_t>(replacement_property_option)]
            .key);
    const auto properties_before_value = nw::toolset::snapshot_item_property_records(
        runtime, first_item->handle());
    ASSERT_TRUE(properties_before_value);
    const int32_t original_subtype = (*properties_before_value)[static_cast<size_t>(inserted_property_index)]
                                         .subtype;
    ASSERT_NE(replacement_subtype, original_subtype);
    const size_t undo_count_before_value = workspace.undo_count();
    ASSERT_TRUE(activate_managed_list(
        "item.properties.options", replacement_property_option));
    property_options = nw::toolset::ui_v1_host().window(
        "item.properties.options", 300, 0);
    ASSERT_TRUE(property_options);
    EXPECT_FALSE(property_options->visible);
    const auto properties_after_value = nw::toolset::snapshot_item_property_records(
        runtime, first_item->handle());
    ASSERT_TRUE(properties_after_value);
    EXPECT_EQ((*properties_after_value)[static_cast<size_t>(
                                            inserted_property_index)]
                  .subtype,
        replacement_subtype);
    EXPECT_EQ(workspace.undo_count(), undo_count_before_value + 1);
    ASSERT_TRUE(workspace.undo(item_context).ok());
    const auto properties_after_undo = nw::toolset::snapshot_item_property_records(
        runtime, first_item->handle());
    ASSERT_TRUE(properties_after_undo);
    EXPECT_EQ((*properties_after_undo)[static_cast<size_t>(
                                           inserted_property_index)]
                  .subtype,
        original_subtype);

    bool model_selector_open = false;
    int opened_item_part = -1;
    for (size_t part = 0;
        part < nw::ObjectItemVisualState::model_part_count
        && !model_selector_open;
        ++part) {
        const std::array<std::string, 2> args{
            std::to_string(part), "0"};
        const std::vector<std::string_view> views{args.begin(), args.end()};
        model_selector_open = backend.execute_command(
                                         "toolset.item.appearance.open_model", views, item_context)
                                  .ok();
        if (model_selector_open) {
            opened_item_part = static_cast<int>(part);
        }
    }
    ASSERT_TRUE(model_selector_open);
    ASSERT_GE(opened_item_part, 0);
    const auto model_options = nw::toolset::ui_v1_host().window(
        "item.appearance.models", 300, 0);
    ASSERT_TRUE(model_options);
    ASSERT_FALSE(model_options->items.empty());
    ASSERT_GE(model_options->selected_index, 0);
    const std::string stale_model_key = model_options->items.front().key;
    const auto* first_visuals = nw::kernel::objects().components().find_item_visuals(first_item->handle());
    const auto* second_visuals = nw::kernel::objects().components().find_item_visuals(second_item->handle());
    ASSERT_NE(first_visuals, nullptr);
    ASSERT_NE(second_visuals, nullptr);

    int replacement_index = -1;
    for (size_t index = 0; index < model_options->items.size(); ++index) {
        if (static_cast<int>(index) != model_options->selected_index) {
            replacement_index = static_cast<int>(index);
            break;
        }
    }
    ASSERT_GE(replacement_index, 0);
    const int32_t first_model_before_edit = first_visuals->model_parts[static_cast<size_t>(opened_item_part)];
    const uint64_t mutation_epoch_before_edit
        = nw::toolset::object_mutation_state().epoch;
    const size_t undo_count_before_edit = workspace.undo_count();
    ASSERT_TRUE(activate_managed_list(
        "item.appearance.models", replacement_index));
    EXPECT_NE(nw::kernel::objects().components().find_item_visuals(first_item->handle())->model_parts[static_cast<size_t>(opened_item_part)],
        first_model_before_edit);
    EXPECT_EQ(nw::toolset::object_mutation_state().epoch,
        mutation_epoch_before_edit + 1);
    EXPECT_EQ(workspace.undo_count(), undo_count_before_edit + 1);

    auto retained_model_options = nw::toolset::ui_v1_host().window(
        "item.appearance.models", 300, 0);
    ASSERT_TRUE(retained_model_options);
    EXPECT_FALSE(retained_model_options->visible);
    EXPECT_EQ(retained_model_options->items.size(),
        model_options->items.size());
    EXPECT_EQ(retained_model_options->selected_index, replacement_index);
    const int cycle_delta
        = replacement_index + 1
            < static_cast<int>(retained_model_options->items.size())
        ? 1
        : -1;
    const int cycled_index = replacement_index + cycle_delta;
    const int32_t model_before_cycle
        = nw::kernel::objects().components().find_item_visuals(first_item->handle())->model_parts[static_cast<size_t>(opened_item_part)];
    const uint64_t mutation_epoch_before_cycle
        = nw::toolset::object_mutation_state().epoch;
    const size_t undo_count_before_cycle = workspace.undo_count();
    ASSERT_TRUE(cycle_managed_list(
        "item.appearance.models", cycle_delta));
    EXPECT_NE(nw::kernel::objects().components().find_item_visuals(first_item->handle())->model_parts[static_cast<size_t>(opened_item_part)],
        model_before_cycle);
    EXPECT_EQ(nw::toolset::object_mutation_state().epoch,
        mutation_epoch_before_cycle + 1);
    EXPECT_EQ(workspace.undo_count(), undo_count_before_cycle + 1);
    retained_model_options = nw::toolset::ui_v1_host().window(
        "item.appearance.models", 300, 0);
    ASSERT_TRUE(retained_model_options);
    EXPECT_FALSE(retained_model_options->visible);
    EXPECT_EQ(retained_model_options->selected_index, cycled_index);

    const auto first_models_before_stale_event
        = nw::kernel::objects().components().find_item_visuals(first_item->handle())->model_parts;
    const auto second_models_before_stale_event
        = nw::kernel::objects().components().find_item_visuals(second_item->handle())->model_parts;
    const size_t item_undo_count_before_stale_event = workspace.undo_count();
    nw::toolset::smalls_rmlui_host().publish_active_object(
        second_item->handle());
    const std::array<std::string, 4> stale_model_args{
        "item.appearance.models", stale_model_key, "0", "-1"};
    const std::vector<std::string_view> stale_model_views{
        stale_model_args.begin(), stale_model_args.end()};
    const auto stale_model_result = backend.execute_command(
        "toolset.item.appearance.model.activate", stale_model_views,
        item_context);
    EXPECT_EQ(stale_model_result.status,
        nw::toolset::CommandStatus::rejected);
    EXPECT_EQ(nw::kernel::objects().components().find_item_visuals(first_item->handle())->model_parts,
        first_models_before_stale_event);
    EXPECT_EQ(nw::kernel::objects().components().find_item_visuals(second_item->handle())->model_parts,
        second_models_before_stale_event);
    EXPECT_EQ(workspace.undo_count(), item_undo_count_before_stale_event);
    nw::toolset::smalls_rmlui_host().clear_active_object();
    nw::kernel::objects().destroy(first_item->handle());
    nw::kernel::objects().destroy(second_item->handle());

    nw::toolset::AppearanceCatalog door_catalog;
    ASSERT_TRUE(nw::toolset::build_appearance_catalog(
        nw::toolset::AppearanceCatalogKind::door, door_catalog));
    ASSERT_EQ(door_catalog.status,
        nw::toolset::AppearanceCatalogStatus::ready);
    ASSERT_FALSE(door_catalog.rows.empty());
    EXPECT_TRUE(std::ranges::is_sorted(door_catalog.rows, {},
        &nw::toolset::AppearanceCatalogRow::sort_key));
    for (const auto& row : door_catalog.rows) {
        EXPECT_GE(row.id, 0);
        EXPECT_FALSE(row.name.empty());
        EXPECT_TRUE(nw::kernel::resman().contains(
            {row.model, nw::ResourceType::mdl}));
    }

    auto* door = nw::kernel::objects().load_file<nw::Door>(
        "test_data/user/development/door_ttr_002.utd");
    ASSERT_NE(door, nullptr);
    nw::toolset::smalls_rmlui_host().publish_active_object(door->handle());

    const auto selectors = nw::toolset::door_appearance(runtime, door->handle());
    ASSERT_TRUE(selectors);
    nw::Vector<nw::smalls::Value> resolve_args{
        nw::smalls::Value::make_int(selectors->appearance),
        nw::smalls::Value::make_int(selectors->generic_type),
    };
    const auto resolved = runtime.execute_script(
        "nwn1.doors", "resolve_door_model_by_state", resolve_args);
    ASSERT_TRUE(resolved.ok());
    const auto label = runtime.read_struct_field(
        resolved.value.data.hptr, resolved.value.type_id, "label");
    ASSERT_EQ(label.type_id, runtime.string_type());
    const std::string resolved_label{runtime.get_string_view(label.data.hptr)};
    ASSERT_FALSE(resolved_label.empty());

    nw::toolset::smalls_rmlui_host().clear_active_object();
    nw::kernel::objects().destroy(door->handle());

    const auto verify_data_list = [&](nw::ObjectHandle handle,
                                      std::string_view refresh_function,
                                      std::string_view list_id,
                                      uint8_t cell_count) {
        nw::toolset::smalls_rmlui_host().publish_active_object(handle);
        const auto refresh = runtime.execute_script(
            "toolset.data_object_editor", refresh_function, {});
        ASSERT_TRUE(refresh.ok()) << refresh_function;
        const auto window = nw::toolset::ui_v1_host().window(list_id, 170, 0);
        ASSERT_TRUE(window) << list_id;
        EXPECT_TRUE(window->visible);
        ASSERT_FALSE(window->items.empty()) << list_id;
        EXPECT_LE(window->items.size(), 1024u);
        EXPECT_EQ(window->items.front().cell_count, cell_count);
        const auto markup = nw::toolset::render_managed_list_window(
            list_id, *window, "No entries.");
        EXPECT_NE(markup.find("managed_list_row"), std::string::npos);
    };

    auto* encounter = nw::kernel::objects().load_file<nw::Encounter>(
        "test_data/user/development/boundelementallo.ute");
    ASSERT_NE(encounter, nullptr);
    verify_data_list(encounter->handle(), "encounter_spawns_refresh",
        "data.encounter.spawns", 4);

    const auto encounter_spawns_before = nw::toolset::snapshot_encounter_spawns(
        runtime, encounter->handle());
    ASSERT_TRUE(encounter_spawns_before);
    ASSERT_FALSE(encounter_spawns_before->empty());
    auto encounter_spawns_after = *encounter_spawns_before;
    encounter_spawns_after.pop_back();
    const nw::toolset::EncounterSpawnEdit encounter_edit{
        .encounter = encounter->handle(),
        .before = *encounter_spawns_before,
        .after = encounter_spawns_after,
    };
    ASSERT_EQ(nw::toolset::apply_encounter_spawn_edit(
                  runtime, encounter_edit,
                  nw::toolset::ObjectEditDirection::forward)
                  .status,
        nw::toolset::ObjectEditStatus::success);
    ASSERT_TRUE(runtime.execute_script(
                           "toolset.data_object_editor", "encounter_spawns_refresh", {})
            .ok());
    const auto edited_encounter_window = nw::toolset::ui_v1_host().window(
        "data.encounter.spawns", 170, 0);
    ASSERT_TRUE(edited_encounter_window);
    EXPECT_EQ(edited_encounter_window->items.size(),
        encounter_spawns_after.size());
    ASSERT_EQ(nw::toolset::apply_encounter_spawn_edit(
                  runtime, encounter_edit,
                  nw::toolset::ObjectEditDirection::inverse)
                  .status,
        nw::toolset::ObjectEditStatus::success);
    nw::kernel::objects().destroy(encounter->handle());

    auto* sound = nw::kernel::objects().load_file<nw::Sound>(
        "test_data/user/development/blue_bell.uts");
    ASSERT_NE(sound, nullptr);
    verify_data_list(sound->handle(), "sound_resources_refresh",
        "data.sound.resources", 1);

    const auto sound_resources_before = nw::toolset::snapshot_sound_resources(
        runtime, sound->handle());
    ASSERT_TRUE(sound_resources_before);
    ASSERT_FALSE(sound_resources_before->empty());
    auto sound_resources_after = *sound_resources_before;
    sound_resources_after.push_back(nw::Resref{"zz_reorder"});
    const nw::toolset::SoundResourceEdit sound_edit{
        .sound = sound->handle(),
        .before = *sound_resources_before,
        .after = sound_resources_after,
    };
    ASSERT_EQ(nw::toolset::apply_sound_resource_edit(
                  runtime, sound_edit,
                  nw::toolset::ObjectEditDirection::forward)
                  .status,
        nw::toolset::ObjectEditStatus::success);
    ASSERT_TRUE(runtime.execute_script(
                           "toolset.data_object_editor", "sound_resources_refresh", {})
            .ok());
    const auto edited_sound_window = nw::toolset::ui_v1_host().window(
        "data.sound.resources", 170, 0);
    ASSERT_TRUE(edited_sound_window);
    EXPECT_EQ(edited_sound_window->items.size(), sound_resources_after.size());
    auto& list_host = nw::toolset::ui_v1_host();
    EXPECT_EQ(list_host.callback("data.sound.resources",
                  nw::toolset::UiListEventType::reorder),
        "toolset.data_object_editor.on_sound_resource_reorder");

    const auto reorder_snapshot = list_host.reorder_snapshot(
        "data.sound.resources");
    ASSERT_TRUE(reorder_snapshot);
    const int source_index = reorder_snapshot->item_count - 1;
    const size_t undo_count_before_reorder = workspace.undo_count();
    ASSERT_TRUE(list_host.push_reorder("data.sound.resources",
        source_index, 0, reorder_snapshot->revision));
    ASSERT_TRUE(dispatch_managed_list_events());

    auto reordered_sound_resources = sound_resources_after;
    std::rotate(reordered_sound_resources.begin(),
        reordered_sound_resources.end() - 1,
        reordered_sound_resources.end());
    EXPECT_EQ(nw::toolset::snapshot_sound_resources(
                  runtime, sound->handle()),
        std::optional{reordered_sound_resources});
    EXPECT_EQ(workspace.undo_count(), undo_count_before_reorder + 1);

    ASSERT_TRUE(runtime.execute_script(
                           "toolset.data_object_editor", "sound_resources_refresh", {})
            .ok());
    const auto reordered_sound_window = list_host.window(
        "data.sound.resources", 170, 0);
    ASSERT_TRUE(reordered_sound_window);
    EXPECT_EQ(reordered_sound_window->selected_index, 0);
    ASSERT_TRUE(workspace.undo(undo_context).ok());
    EXPECT_EQ(nw::toolset::snapshot_sound_resources(
                  runtime, sound->handle()),
        std::optional{sound_resources_after});

    ASSERT_EQ(nw::toolset::apply_sound_resource_edit(
                  runtime, sound_edit,
                  nw::toolset::ObjectEditDirection::inverse)
                  .status,
        nw::toolset::ObjectEditStatus::success);
    nw::kernel::objects().destroy(sound->handle());

    auto* store = nw::kernel::objects().load_file<nw::Store>(
        "test_data/user/development/storethief002.utm");
    ASSERT_NE(store, nullptr);
    verify_data_list(store->handle(), "store_inventory_refresh",
        "data.store.inventory", 4);
    nw::kernel::objects().destroy(store->handle());

    nw::toolset::smalls_rmlui_host().clear_active_object();
    nw::toolset::script_command_host().bind(nullptr, nullptr);
}

TEST(ClientRmlSmallsBridge, SaveAllUsesRetainedTabsAndProtectsProjectReplacement)
{
    KernelServiceScope services;
    const std::filesystem::path project = "tmp/client_save_all_commands";
    std::filesystem::remove_all(project);
    nw::toolset::ProjectImportOptions options;
    options.format = nw::toolset::ProjectImportFormat::json;
    const auto imported = nw::toolset::import_module_project(
        "test_data/user/modules/DockerDemo.mod", project, options);
    ASSERT_TRUE(imported.ok) << imported.message;
    nw::toolset::RmlSmallsBridge bridge;
    nw::toolset::WorkspaceState workspace;
    nw::toolset::ToolsetBackend backend;
    backend.bind(&bridge, nullptr, &workspace);
    const auto opened = backend.open_project(project.string());
    ASSERT_TRUE(opened.ok()) << opened.message;
    const auto original_generation = backend.module_generation();
    auto* item = nw::kernel::objects().load_file<nw::Item>("test_data/user/development/cloth028.uti");
    ASSERT_NE(item, nullptr);
    const auto item_handle = item->handle();
    ASSERT_TRUE(item->save(project / "item.uti.json", "json"));
    auto& tab = workspace.open_or_replace_tab("item", "Item", nw::toolset::WorkspaceTabKind::preview, "item.uti.json");
    ASSERT_TRUE(tab.document.adopt(item_handle));
    tab.dirty = true;
    workspace.open_or_replace_tab("broken", "Broken", nw::toolset::WorkspaceTabKind::preview, "broken.uti.json").dirty = true;
    workspace.set_active_tab("home");
    EXPECT_EQ(backend.open_project(project.string()).status, nw::toolset::CommandStatus::rejected);
    EXPECT_EQ(backend.open_module("test_data/user/modules/DockerDemo.mod").status, nw::toolset::CommandStatus::rejected);
    EXPECT_EQ(backend.module_generation(), original_generation);
    EXPECT_TRUE(nw::kernel::objects().valid(item_handle));

    const auto saved = backend.execute_command("saveall", {}, {});
    EXPECT_EQ(saved.status, nw::toolset::CommandStatus::failed);
    EXPECT_NE(saved.message.find("Saved 1 of 2"), std::string::npos);
    EXPECT_EQ(workspace.active_tab_id(), "home");
    EXPECT_FALSE(workspace.find_tab("item")->dirty);
    EXPECT_TRUE(workspace.find_tab("broken")->dirty);
    EXPECT_TRUE(std::filesystem::exists(project / "item.uti.json"));
    const auto failed_close = backend.execute_command("workspace.save_and_close_tab", {"broken"}, {});
    EXPECT_FALSE(failed_close.ok());
    EXPECT_NE(workspace.find_tab("broken"), nullptr);
    EXPECT_TRUE(workspace.request_close_tab("broken", true).closed());
    EXPECT_EQ(backend.execute_command("toolset.save_all", {}, {}).status, nw::toolset::CommandStatus::noop);
    workspace.set_tab_dirty("item", true);
    EXPECT_TRUE(backend.execute_command("workspace.save_and_close_tab", {"item"}, {}).ok());
    EXPECT_EQ(workspace.find_tab("item"), nullptr);
    EXPECT_FALSE(nw::kernel::objects().valid(item_handle));

    auto* retained = nw::kernel::objects().make<nw::Item>();
    ASSERT_NE(retained, nullptr);
    ASSERT_TRUE(workspace.open_tab("retained").document.adopt(retained->handle()));
    const auto reopened = backend.open_project(project.string());
    EXPECT_TRUE(reopened.ok()) << reopened.message;
    EXPECT_EQ(workspace.find_tab("retained"), nullptr);
    EXPECT_GT(backend.module_generation(), original_generation);
}

TEST(ClientWorkspaceView, AreaRowsKeepTheirIdentityAcrossIndependentFilters)
{
    using namespace nw::toolset;
    NullRenderInterface renderer;
    RmlScope rml(renderer);
    ASSERT_TRUE(rml.initialized());
    auto* context = Rml::CreateContext("area-row-identities", {800, 600});
    ASSERT_NE(context, nullptr);
    auto* document = context->LoadDocumentFromMemory(
        "<rml><body><div id=\"sidebar\"></div><div id=\"home\"></div></body></rml>");
    ASSERT_NE(document, nullptr);
    auto* sidebar = document->GetElementById("sidebar");
    auto* home = document->GetElementById("home");
    ASSERT_NE(sidebar, nullptr);
    ASSERT_NE(home, nullptr);
    const std::array areas{
        LoadedAreaEntry{.name = "Start & Arrival", .resref = "start"},
        LoadedAreaEntry{.name = "Second", .resref = "second"},
    };
    sidebar->SetInnerRML(area_rows_markup(areas));
    home->SetInnerRML(area_rows_markup(std::span{areas}.subspan(1)));

    const auto row_resref = [](Rml::Element* list, int index) {
        return list->GetChild(index)->GetChild(0)->GetAttribute<Rml::String>("data-resref", "");
    };
    ASSERT_EQ(sidebar->GetNumChildren(), 2);
    ASSERT_EQ(home->GetNumChildren(), 1);
    EXPECT_EQ(row_resref(sidebar, 0), "start");
    EXPECT_EQ(row_resref(home, 0), "second");
    home->SetInnerRML(area_rows_markup({}));
    EXPECT_EQ(row_resref(sidebar, 1), "second");

    // Quotes and markup in authored names/identities must stay data.
    const std::array escaped{LoadedAreaEntry{.name = "<b>Area</b>", .resref = "a\"&b"}};
    home->SetInnerRML(area_rows_markup(escaped));
    ASSERT_EQ(home->GetNumChildren(), 1);
    EXPECT_EQ(row_resref(home, 0), escaped[0].resref);
    EXPECT_EQ(home->GetChild(0)->GetChild(0)->GetChild(0)->GetNumChildren(), 1);
}

TEST(ClientWorkspaceView, RecentProjectErrorsAndRemovalHaveSeparateHitTargets)
{
    using namespace nw::toolset;
    CurrentPathScope source_root{ROLLNW_TEST_SOURCE_DIR};
    NullRenderInterface renderer;
    RmlScope rml(renderer);
    ASSERT_TRUE(rml.initialized());
    ASSERT_TRUE(Rml::LoadFontFace("tools/client/assets/fonts/inter/Inter-Regular.ttf"));
    auto* context = Rml::CreateContext("recent-project-actions", {800, 600});
    ASSERT_NE(context, nullptr);
    const std::array projects{
        RecentProjectEntry{"Available", "/example/available", {}},
        RecentProjectEntry{"Removed <project>", "/example/removed", "Project folder not found"},
    };
    const auto source = std::string{
                            "<rml><head><link type=\"text/css\" href=\"tools/client/ui/panel.rcss\"/>"
                            "<style>body, button { font-family: Inter; }"
                            "#recent_fixture { display: block; width: 600px; }</style></head>"
                            "<body><div id=\"recent_fixture\">"}
        + recent_projects_markup(projects) + "</div></body></rml>";
    auto* document = context->LoadDocumentFromMemory(source);
    ASSERT_NE(document, nullptr);
    document->Show();
    context->Update();
    auto* list = document->GetElementById("recent_fixture");
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->GetNumChildren(), 2);
    for (int i = 0; i < list->GetNumChildren(); ++i) {
        auto* row = list->GetChild(i);
        ASSERT_EQ(row->GetNumChildren(), 2);
        auto* open = row->GetChild(0);
        auto* remove = row->GetChild(1);
        EXPECT_TRUE(open->IsClassSet("home_project_item"));
        EXPECT_TRUE(remove->IsClassSet("home_project_remove"));
        EXPECT_EQ(open->IsClassSet("unavailable"), i == 1);
        EXPECT_EQ(remove->GetAttribute<Rml::String>("data-key", ""), std::to_string(i));
        EXPECT_EQ(remove->GetInnerRML(), "×");
        EXPECT_NE(remove->GetAttribute<Rml::String>("title", "").find("Project files are not deleted"), std::string::npos);
        const auto color = remove->GetProperty<Rml::Colourb>("color");
        EXPECT_GT(color.red, color.green);
        EXPECT_GT(color.red, color.blue);
        EXPECT_GT(open->GetOffsetWidth(), 0.0f);
        EXPECT_FLOAT_EQ(remove->GetOffsetWidth(), 28.0f);
        EXPECT_FLOAT_EQ(remove->GetOffsetHeight(), 28.0f);
        EXPECT_GE(remove->GetAbsoluteLeft(), open->GetAbsoluteLeft() + open->GetOffsetWidth());
        const Rml::Vector2f point{
            remove->GetAbsoluteLeft() + remove->GetOffsetWidth() / 2.0f,
            remove->GetAbsoluteTop() + remove->GetOffsetHeight() / 2.0f};
        auto* hit = context->GetElementAtPoint(point);
        while (hit && hit != remove)
            hit = hit->GetParentNode();
        EXPECT_EQ(hit, remove);
    }
    auto* error = list->GetChild(1)->GetChild(0)->GetChild(2);
    ASSERT_NE(error, nullptr);
    EXPECT_TRUE(error->IsClassSet("home_project_error"));
    EXPECT_GT(error->GetOffsetHeight(), 0.0f);
    EXPECT_NE(error->GetInnerRML().find("Project folder not found"), std::string::npos);
    list->SetInnerRML(recent_projects_markup({}));
    ASSERT_EQ(list->GetNumChildren(), 1);
    EXPECT_TRUE(list->GetChild(0)->IsClassSet("home_empty"));
}

TEST(ClientWorkspaceView, OnlyOpeningAnAreaRequestsViewportFocus)
{
    using namespace nw::toolset;
    NullRenderInterface renderer;
    RmlScope rml(renderer);
    ASSERT_TRUE(rml.initialized());
    auto* context = Rml::CreateContext("area-focus-transitions", {800, 600});
    ASSERT_NE(context, nullptr);
    auto* document = context->LoadDocumentFromMemory("<rml><body></body></rml>");
    ASSERT_NE(document, nullptr);
    WorkspaceState workspace;
    workspace.ensure_default_tabs();
    EXPECT_FALSE(area_viewport_changed(document, workspace.active_tab()));
    workspace.set_active_tab("area");
    EXPECT_FALSE(area_viewport_changed(document, workspace.active_tab()));

    workspace.open_area_tab("shared/areas/start.caf.json", "Start");
    EXPECT_TRUE(area_viewport_changed(document, workspace.active_tab()));
    document->SetInnerRML(
        "<div id=\"workspace_viewer_viewport\" data-resource=\"shared/areas/start.caf.json\"></div>");
    EXPECT_FALSE(area_viewport_changed(document, workspace.active_tab()));
    workspace.set_tab_dirty("area", true);
    EXPECT_FALSE(area_viewport_changed(document, workspace.active_tab()));
    // A rejected dirty replacement keeps the same document and focus target.
    workspace.open_area_tab("shared/areas/second.caf.json", "Second");
    EXPECT_FALSE(area_viewport_changed(document, workspace.active_tab()));
    workspace.set_tab_dirty("area", false);
    workspace.open_area_tab("shared/areas/second.caf.json", "Second");
    EXPECT_TRUE(area_viewport_changed(document, workspace.active_tab()));
    workspace.open_tab("preview", "Creature", WorkspaceTabKind::preview);
    EXPECT_FALSE(area_viewport_changed(document, workspace.active_tab()));
    EXPECT_FALSE(area_viewport_changed(document, nullptr));
    EXPECT_FALSE(area_viewport_changed(nullptr, workspace.find_tab("area")));
}

TEST(ClientRmlSmallsBridge, BlueprintFormsCreateAndCopyThroughTheCommandBus)
{
    using namespace nw::toolset;
    KernelServiceScope services;
    const std::filesystem::path project = "tmp/client_blueprint_commands";
    std::filesystem::remove_all(project);
    ProjectImportOptions options;
    options.format = ProjectImportFormat::json;
    const auto imported = import_module_project("test_data/user/modules/DockerDemo.mod", project, options);
    ASSERT_TRUE(imported.ok) << imported.message;
    RmlSmallsBridge bridge;
    WorkspaceState workspace;
    ToolsetBackend backend;
    backend.bind(&bridge, nullptr, &workspace);
    ASSERT_TRUE(backend.open_project(project.string()).ok());
    EXPECT_EQ(workspace.active_tab_id(), "home");

    const auto chooser = backend.execute_command("blueprint.new", {}, {});
    ASSERT_TRUE(chooser.prompt) << chooser.message;
    EXPECT_TRUE(chooser.prompt->action_list);
    ASSERT_EQ(chooser.prompt->actions.size(), blueprint_types().size() + 1);
    for (size_t index = 0; index < blueprint_types().size(); ++index) {
        const auto& definition = blueprint_types()[index];
        EXPECT_EQ(chooser.prompt->actions[index].label, definition.label);
        EXPECT_EQ(chooser.prompt->actions[index].args,
            std::vector<std::string>{std::string{nw::ResourceType::to_string(
                definition.resource_type)}});
    }
    EXPECT_EQ(chooser.prompt->actions.back().id, "cancel");

    for (const auto& definition : blueprint_types()) {
        const auto type = nw::ResourceType::to_string(definition.resource_type);
        const auto result = backend.execute_command("blueprint.new", {type}, {});
        ASSERT_TRUE(result.prompt) << result.message;
        EXPECT_EQ(result.prompt->fields[0].label, "ResRef");
        EXPECT_TRUE(result.prompt->fields[1].directory);
        const auto field_count = type == "uti" ? 4u
            : type == "utc"                    ? 6u
                                               : 3u;
        EXPECT_EQ(result.prompt->fields.size(), field_count);
        if (type == "utc") {
            EXPECT_EQ(result.prompt->fields[2].label, "First Name");
            EXPECT_TRUE(result.prompt->fields[2].required);
            EXPECT_EQ(result.prompt->fields[3].label, "Last Name");
            EXPECT_FALSE(result.prompt->fields[3].required);
            EXPECT_EQ(result.prompt->fields[4].label, "Race");
            EXPECT_EQ(result.prompt->fields[4].value, "6");
            EXPECT_FALSE(result.prompt->fields[4].choices.empty());
            EXPECT_EQ(result.prompt->fields[5].label, "Base Class");
            EXPECT_EQ(result.prompt->fields[5].value, "4");
            EXPECT_FALSE(result.prompt->fields[5].choices.empty());
        } else {
            EXPECT_EQ(result.prompt->fields[2].label, "Name");
        }
        EXPECT_EQ(result.prompt->file_suffix,
            "." + std::string{type} + ".json");
        ASSERT_TRUE(backend.execute_command("blueprint.cancel", {}, {}).ok());
    }
    auto creature_prompt = backend.execute_command("blueprint.new", {"utc"}, {});
    ASSERT_TRUE(creature_prompt.prompt);
    const auto creature_created = backend.execute_command("blueprint.submit",
        {"auth_cmd_creature", "shared/blueprints/creatures", "Command", "Creature", "0", "1"}, {});
    ASSERT_TRUE(creature_created.ok()) << creature_created.message;
    const std::filesystem::path creature_path
        = "shared/blueprints/creatures/auth_cmd_creature.utc.json";
    EXPECT_TRUE(std::filesystem::is_regular_file(project / creature_path));
    EXPECT_EQ(project_resource_display_name(project, creature_path),
        "Command Creature");
    auto prompt = backend.execute_command("blueprint.new", {"uti"}, {});
    ASSERT_TRUE(prompt.prompt);
    ASSERT_EQ(prompt.prompt->fields.size(), 4u);
    EXPECT_EQ(prompt.prompt->fields[3].label, "Base item type");
    EXPECT_FALSE(prompt.prompt->fields[3].choices.empty());
    EXPECT_EQ(prompt.prompt->fields[3].value,
        prompt.prompt->fields[3].choices.front().value);
    EXPECT_FALSE(backend.execute_command("blueprint.submit", {"auth_cmd_item", "shared/blueprints/items", "Command Item", "invalid"}, {}).ok());
    const std::vector<std::string_view> values{"auth_cmd_item", "shared/blueprints/items", "Command Item", "0"};
    const auto created = backend.execute_command("blueprint.submit", values, {});
    ASSERT_TRUE(created.ok()) << created.message;
    EXPECT_FALSE(created.prompt);
    EXPECT_TRUE(std::filesystem::is_regular_file(project / "shared/blueprints/items/auth_cmd_item.uti.json"));
    EXPECT_EQ(project_resource_display_name(project,
                  "shared/blueprints/items/auth_cmd_item.uti.json"),
        "Command Item");
    ASSERT_NE(workspace.active_tab(), nullptr);
    EXPECT_EQ(workspace.active_tab()->kind, WorkspaceTabKind::preview);
    EXPECT_FALSE(workspace.active_tab()->dirty);
    EXPECT_EQ(live_object_display_name(workspace.active_tab()->document.object()), "Command Item");
    const auto source_handle = workspace.active_tab()->document.object();
    bridge.publish_active_object(source_handle);
    workspace.active_tab()->dirty = true;
    const auto source_tab = workspace.active_tab_id();
    const auto tab_count = workspace.tabs().size();
    const auto object_count = nw::kernel::objects().object_count();
    const auto copied_path = project / "shared/blueprints/items/auth_cmd_copy.uti.json";
    const nw::Resource copied_resource{nw::Resref{"auth_cmd_copy"}, nw::ResourceType::uti};
    const auto tree_contains_copy = [&] {
        const auto tree = backend.list_project_tree("");
        EXPECT_TRUE(tree.ok) << tree.message;
        std::vector<const ProjectTreeNode*> pending{&tree.root};
        for (size_t index = 0; index < pending.size(); ++index) {
            if (pending[index]->relative_path.filename() == copied_path.filename()) { return true; }
            for (const auto& child : pending[index]->children) {
                pending.push_back(&child);
            }
        }
        return false;
    };
    ASSERT_TRUE(backend.execute_command("blueprint.save_as", {}, {}).prompt);
    const auto copied = backend.execute_command("blueprint.submit", {"auth_cmd_copy", "shared/blueprints/items"}, {});
    ASSERT_TRUE(copied.ok()) << copied.message;
    EXPECT_EQ(workspace.active_tab_id(), source_tab);
    EXPECT_EQ(workspace.tabs().size(), tab_count);
    EXPECT_EQ(workspace.active_tab()->document.object(), source_handle);
    EXPECT_TRUE(workspace.active_tab()->dirty);
    EXPECT_EQ(nw::kernel::objects().object_count(), object_count);
    EXPECT_EQ(workspace.undo_count(), 1u);
    EXPECT_TRUE(tree_contains_copy());
    const auto copied_data = nw::kernel::resman().demand(copied_resource);
    const std::string copied_bytes{copied_data.bytes.string_view()};
    ASSERT_FALSE(copied_bytes.empty());
    ASSERT_TRUE(backend.execute_command("command.undo", {}, {}).ok());
    EXPECT_FALSE(std::filesystem::exists(copied_path));
    EXPECT_FALSE(nw::kernel::resman().contains(copied_resource));
    EXPECT_FALSE(tree_contains_copy());
    EXPECT_TRUE(workspace.active_tab()->dirty);
    EXPECT_EQ(workspace.redo_count(), 1u);

    // A replacement resource in another folder still occupies the same key.
    const auto conflict_path = project / "shared/auth_cmd_copy.uti.json";
    std::ofstream{conflict_path} << copied_bytes;
    EXPECT_FALSE(backend.execute_command("command.redo", {}, {}).ok());
    EXPECT_EQ(workspace.redo_count(), 1u);
    EXPECT_FALSE(std::filesystem::exists(copied_path));
    std::filesystem::remove(conflict_path);
    ASSERT_TRUE(backend.execute_command("command.redo", {}, {}).ok());
    EXPECT_EQ(nw::kernel::resman().demand(copied_resource).bytes.string_view(), copied_bytes);
    EXPECT_TRUE(tree_contains_copy());

    std::ofstream{copied_path} << copied_bytes << "\n";
    EXPECT_FALSE(backend.execute_command("command.undo", {}, {}).ok());
    EXPECT_TRUE(std::filesystem::exists(copied_path));
    EXPECT_EQ(workspace.undo_count(), 1u);
    std::ofstream{copied_path} << copied_bytes;
    ASSERT_TRUE(backend.execute_command("command.undo", {}, {}).ok());
    EXPECT_FALSE(tree_contains_copy());
    EXPECT_EQ(bridge.active_object(), source_handle);
    EXPECT_EQ(nw::kernel::objects().object_count(), object_count);
    workspace.active_tab()->dirty = false;
#ifdef ROLLNW_TEST_CLIENT_EXECUTABLE
    const auto blueprint_handle = workspace.active_tab()->document.object();
    auto* area = nw::kernel::objects().make_area(nw::Resref{"start"});
    ASSERT_NE(area, nullptr);
    auto& area_tab = workspace.open_area_tab("shared/areas/start.caf.json", "Start");
    ASSERT_TRUE(area_tab.document.adopt(area->handle()));
    const auto previous_area = area->handle();
    auto* placed = nw::kernel::objects().load<nw::Item>(nw::Resref{"auth_cmd_item"});
    ASSERT_NE(placed, nullptr);
    placed->comment = "Placed override";
    const auto previous_item = placed->handle();
    auto* spatial = nw::kernel::objects().components().get_or_create_spatial(placed->handle());
    spatial->position = {5, 5, 0.5f};
    spatial->orientation = {0, 1, 0};
    spatial->area = previous_area.id;
    area->items.push_back(placed);
    std::string error;
    ASSERT_TRUE(save_live_area_json_atomic(previous_area, project / area_tab.detail, error)) << error;
    const auto read_area_bytes = [](const auto& path) {
        std::ifstream input{path};
        return std::string{std::istreambuf_iterator<char>{input}, {}};
    };
    const auto live_path = project / area_tab.detail;
    const auto saved_live_bytes = read_area_bytes(live_path);
    const auto closed_path = project / "shared/areas/auth_closed.caf.json";
    std::ofstream{closed_path} << saved_live_bytes;
    ASSERT_TRUE(nw::kernel::resman().refresh_module_resources(error)) << error;
    area->comment = "Keep this unsaved area edit";
    area_tab.dirty = true;
    bridge.publish_active_object(placed->handle());
    bridge.publish_active_area(area->handle());
    auto scope = backend.execute_command("blueprint.references", {}, {});
    ASSERT_TRUE(scope.prompt) << scope.message;
    ASSERT_EQ(scope.prompt->fields.size(), 1u);
    EXPECT_EQ(scope.prompt->fields[0].value, "area");
    EXPECT_EQ(scope.prompt->fields[0].choices.size(), 2u);
    const auto scanned = backend.execute_command("blueprint.references.scan", {"area"}, {});
    ASSERT_TRUE(scanned.ok()) << scanned.message;
    EXPECT_FALSE(scanned.prompt);
    EXPECT_FALSE(backend.execute_command("toolset.save_all", {}, {}).ok());
    const auto wait_for_stage = [&](std::string_view stage) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{60};
        while (std::chrono::steady_clock::now() < deadline) {
            const auto result = backend.poll_blueprint_updates(ROLLNW_TEST_CLIENT_EXECUTABLE);
            if (result && !result->ok()) {
                ADD_FAILURE() << result->message;
                return false;
            }
            if (backend.blueprint_progress().stage == stage && !backend.blueprint_worker_active()) { return true; }
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
        ADD_FAILURE() << "Timed out waiting for " << stage;
        return false;
    };
    ASSERT_TRUE(wait_for_stage("ready"));
    EXPECT_EQ(backend.blueprint_updated_documents().size(), 1u);
    ASSERT_TRUE(backend.execute_command("blueprint.references.apply", {}, {}).ok());
    ASSERT_TRUE(wait_for_stage("complete"));
    EXPECT_TRUE(nw::kernel::objects().valid(previous_area));
    EXPECT_FALSE(nw::kernel::objects().valid(previous_item));
    EXPECT_TRUE(nw::kernel::objects().valid(blueprint_handle));
    ASSERT_TRUE(nw::kernel::objects().valid(bridge.active_object()));
    EXPECT_TRUE(nw::kernel::objects().get_object_base(bridge.active_object())->comment.empty());
    EXPECT_TRUE(workspace.find_tab("area")->dirty);
    EXPECT_EQ(workspace.find_tab("area")->document.object(), previous_area);
    EXPECT_EQ(area->comment, "Keep this unsaved area edit");
    EXPECT_EQ(read_area_bytes(live_path), saved_live_bytes);
    EXPECT_EQ(read_area_bytes(closed_path), saved_live_bytes);
    EXPECT_TRUE(latest_blueprint_operation(project).empty());
    ASSERT_TRUE(backend.execute_command("blueprint.references.cancel", {}, {}).ok());

    // Whole Module updates unopened files and replaces the live instances without
    // saving/reloading the dirty area. File restoration excludes the live area.
    const auto current_item = bridge.active_object();
    nw::kernel::objects().get_object_base(current_item)->comment = "Another live override";
    ASSERT_TRUE(backend.execute_command("blueprint.references", {}, {}).prompt);
    const auto module_scan = backend.execute_command("blueprint.references.scan", {"module"}, {});
    ASSERT_TRUE(module_scan.ok()) << module_scan.message;
    EXPECT_FALSE(module_scan.prompt);
    ASSERT_TRUE(wait_for_stage("ready"));
    EXPECT_EQ(backend.blueprint_updated_documents().size(), 2u);
    ASSERT_TRUE(backend.execute_command("blueprint.references.apply", {}, {}).ok());
    ASSERT_TRUE(wait_for_stage("complete"));
    EXPECT_TRUE(nw::kernel::objects().valid(previous_area));
    EXPECT_FALSE(nw::kernel::objects().valid(current_item));
    EXPECT_TRUE(workspace.find_tab("area")->dirty);
    EXPECT_EQ(area->comment, "Keep this unsaved area edit");
    EXPECT_EQ(read_area_bytes(live_path), saved_live_bytes);
    EXPECT_NE(read_area_bytes(closed_path), saved_live_bytes);
    const auto module_item = bridge.active_object();
    ASSERT_TRUE(backend.execute_command("blueprint.references.cancel", {}, {}).ok());
    ASSERT_TRUE(backend.execute_command("blueprint.references.restore", {}, {}).ok());
    ASSERT_TRUE(backend.execute_command("blueprint.references.restore_apply", {}, {}).ok());
    ASSERT_TRUE(wait_for_stage("complete"));
    EXPECT_EQ(bridge.active_object(), module_item);
    EXPECT_EQ(workspace.find_tab("area")->document.object(), previous_area);
    EXPECT_TRUE(workspace.find_tab("area")->dirty);
    EXPECT_EQ(read_area_bytes(live_path), saved_live_bytes);
    EXPECT_EQ(read_area_bytes(closed_path), saved_live_bytes);
    ASSERT_TRUE(backend.execute_command("blueprint.references.cancel", {}, {}).ok());
#endif
    CommandContext preview;
    preview.play_preview_active = true;
    EXPECT_FALSE(backend.execute_command("blueprint.new", {"uti"}, preview).ok());
}

TEST(ClientRmlSmallsBridge, AreaOpeningReusesPinnedTabWithSaveDiscardAndCancel)
{
    using namespace nw::toolset;
    KernelServiceScope services;
    const std::filesystem::path project = "tmp/client_single_area_commands";
    std::filesystem::remove_all(project);
    ProjectImportOptions options;
    options.format = ProjectImportFormat::json;
    const auto imported = import_module_project("test_data/user/modules/DockerDemo.mod", project, options);
    ASSERT_TRUE(imported.ok) << imported.message;
    const std::string first = "shared/areas/start.caf.json";
    const std::string second = "shared/areas/second.caf.json";
    std::filesystem::copy_file(project / first, project / second);

    RmlSmallsBridge bridge;
    WorkspaceState workspace;
    ToolsetBackend backend;
    backend.bind(&bridge, nullptr, &workspace);
    ASSERT_TRUE(backend.open_project(project.string()).ok());
    ASSERT_TRUE(backend.execute_command("toolset.open_resource", {first}, {}).ok());
    EXPECT_EQ(workspace.active_tab_id(), "area");
    auto* live = nw::kernel::objects().make_area(nw::Resref{"start"});
    ASSERT_NE(live, nullptr);
    auto root = live->handle();
    ASSERT_TRUE(workspace.active_tab()->document.adopt(root));
    live->comments = "saved before switching";
    workspace.set_tab_dirty("area", true);
    workspace.push_undo({"Area edit", [](CommandContext&) { return CommandResult{}; },
        [](CommandContext&) { return CommandResult{}; }});
    workspace.open_tab("blueprint");
    ASSERT_TRUE(backend.execute_command("toolset.select_area", {"start"}, {}).ok());
    EXPECT_EQ(workspace.active_tab()->document.object(), root);
    EXPECT_EQ(workspace.undo_count(), 1u);
    EXPECT_TRUE(workspace.active_tab()->dirty);
    workspace.set_active_tab("home");

    const auto run_action = [&backend](const CommandPromptAction& action) {
        std::vector<std::string_view> args;
        for (const auto& arg : action.args) {
            args.push_back(arg);
        }
        return backend.execute_command(action.command_id, args, {});
    };
    const auto requested = backend.execute_command("toolset.select_area", {"second"}, {});
    ASSERT_TRUE(requested.prompt);
    const auto prompt = *requested.prompt;
    ASSERT_EQ(prompt.actions.size(), 3u);
    EXPECT_EQ(prompt.actions[2].id, "cancel");
    EXPECT_TRUE(prompt.actions[2].command_id.empty());
    // Cancel dispatches nothing: the active tab, root, dirty flag and undo stay.
    EXPECT_EQ(workspace.active_tab_id(), "home");
    EXPECT_EQ(workspace.find_tab("area")->detail, first);
    EXPECT_EQ(workspace.find_tab("area")->document.object(), root);
    EXPECT_TRUE(workspace.find_tab("area")->dirty);
    EXPECT_EQ(workspace.find_tab("area")->undo_stack.size(), 1u);

    // A missing destination is rejected before applying a discard confirmation.
    std::filesystem::rename(project / second, project / "second.saved");
    EXPECT_FALSE(backend.execute_command("toolset.open_resource", {second, "--discard-current-area", first}, {}).ok());
    EXPECT_TRUE(workspace.find_tab("area")->dirty);
    std::filesystem::rename(project / "second.saved", project / second);

    // A failed Save leaves the first document untouched.
    std::filesystem::rename(project / first, project / "first.saved");
    EXPECT_FALSE(run_action(prompt.actions[0]).ok());
    EXPECT_EQ(workspace.find_tab("area")->document.object(), root);
    EXPECT_TRUE(workspace.find_tab("area")->dirty);
    EXPECT_EQ(workspace.active_tab_id(), "home");
    std::filesystem::rename(project / "first.saved", project / first);
    ASSERT_TRUE(run_action(prompt.actions[0]).ok());
    EXPECT_EQ(workspace.active_tab_id(), "area");
    EXPECT_EQ(workspace.active_tab()->detail, second);
    EXPECT_FALSE(workspace.active_tab()->dirty);
    EXPECT_EQ(workspace.undo_count(), 0u);
    EXPECT_FALSE(nw::kernel::objects().valid(root));
    {
        std::ifstream saved{project / first};
        ASSERT_TRUE(saved);
        EXPECT_EQ(nlohmann::json::parse(saved).at("comments"), "saved before switching");
    }
    EXPECT_FALSE(run_action(prompt.actions[1]).ok()); // stale confirmation
    EXPECT_EQ(workspace.active_tab()->detail, second);

    live = nw::kernel::objects().make_area(nw::Resref{"start"});
    ASSERT_NE(live, nullptr);
    root = live->handle();
    ASSERT_TRUE(workspace.active_tab()->document.adopt(root));
    live->comments = "discard this edit";
    workspace.set_tab_dirty("area", true);
    workspace.set_active_tab("home");
    const auto discard_request = backend.execute_command("toolset.open_resource", {first}, {});
    ASSERT_TRUE(discard_request.prompt);
    ASSERT_TRUE(run_action(discard_request.prompt->actions[1]).ok());
    EXPECT_FALSE(nw::kernel::objects().valid(root));
    EXPECT_EQ(workspace.active_tab()->detail, first);
    EXPECT_FALSE(workspace.active_tab()->dirty);
    {
        std::ifstream unchanged{project / second};
        ASSERT_TRUE(unchanged);
        EXPECT_NE(nlohmann::json::parse(unchanged).at("comments"), "discard this edit");
    }
    ASSERT_TRUE(backend.execute_command("toolset.open_resource", {second}, {}).ok());
    EXPECT_EQ(workspace.tabs().size(), 3u); // Home, Area, blueprint
    EXPECT_EQ(workspace.tabs()[1].id, "area");
    EXPECT_FALSE(workspace.tabs()[1].closable);
    EXPECT_FALSE(workspace.tabs()[1].movable);
    EXPECT_FALSE(workspace.move_tab("area", 2));
    EXPECT_FALSE(workspace.request_close_tab("area", true).closed());
}

TEST(ClientRmlSmallsBridge, ConfiguredPackagesSurviveBootstrapAndRepeatedProjectOpens)
{
    using namespace nw::toolset;
    namespace fs = std::filesystem;
    auto& services = nw::kernel::services();
    auto& config = nw::kernel::config();
    const auto previous_options = config.options();
    const auto install = config.install_path();
    const auto user = config.user_path();
    const auto restore = create_scope_exit([&] {
        services.shutdown();
        config.set_paths(install, user);
        config.initialize(previous_options);
    });
    const auto root = fs::absolute("tmp/bridge_configured_packages");
    fs::remove_all(root);
    fs::create_directories(root);
    const auto packages = root / "stdlib with spaces";
    fs::copy("stdlib", packages, fs::copy_options::recursive);
    services.shutdown();
    auto options = previous_options;
    options.stdlib_path = packages;
    config.initialize(options);
    services.start();
    const auto verify_root = [&] {
        EXPECT_EQ(config.options().stdlib_path, packages);
        EXPECT_EQ(nw::kernel::runtime().select_package_directory("nwn1"), fs::canonical(packages / "nwn1"));
        const auto& paths = nw::kernel::runtime().module_paths();
        EXPECT_EQ(std::count_if(paths.begin(), paths.end(), [](const fs::path& path) {
            return path.filename() == "nwn1";
        }),
            1);
    };
    RmlSmallsBridge bridge;
    ShellController shell;
    WorkspaceState workspace;
    ToolsetBackend backend;
    backend.bind(&bridge, &shell, &workspace);
    const auto unbind = create_scope_exit([] { script_command_host().bind(nullptr, nullptr); });
    ASSERT_TRUE(backend.initialize());
    verify_root();
    for (int index = 0; index < 2; ++index) {
        const auto project = root / std::to_string(index);
        const auto imported = import_module_project("test_data/user/modules/DockerDemo.mod", project, {ProjectImportFormat::json});
        ASSERT_TRUE(imported.ok) << imported.message;
        const auto opened = backend.open_project(project.string());
        ASSERT_TRUE(opened.ok()) << opened.message;
        ASSERT_TRUE(backend.initialize());
        EXPECT_EQ(backend.current_project_dir(), project);
        verify_root();
    }
}

TEST(ClientRmlSmallsBridge, BootstrapsMissingKernelAndRetainsPackagesOnReload)
{
    auto& services = nw::kernel::services();
    auto& config = nw::kernel::config();
    const auto previous_options = config.options();
    const auto install = config.install_path();
    const auto user = config.user_path();
    const auto* root_value = SDL_getenv_unsafe("NWN_ROOT");
    const std::optional<std::string> previous_root = root_value
        ? std::optional<std::string>{root_value}
        : std::nullopt;
    const auto restore = create_scope_exit([&] {
        if (previous_root) {
            SDL_setenv_unsafe("NWN_ROOT", previous_root->c_str(), 1);
        } else {
            SDL_unsetenv_unsafe("NWN_ROOT");
        }
        services.shutdown();
        config.set_paths(install, user);
        config.initialize(previous_options);
    });
    const auto install_text = std::filesystem::absolute(install).string();
    ASSERT_EQ(SDL_setenv_unsafe("NWN_ROOT", install_text.c_str(), 1), 0);
    services.shutdown();
    nw::toolset::RmlSmallsBridge bridge;
    nw::toolset::WorkspaceState workspace;
    nw::toolset::ToolsetBackend backend;
    backend.bind(&bridge, nullptr, &workspace);
    const auto unbind = create_scope_exit([] { nw::toolset::script_command_host().bind(nullptr, nullptr); });
    ASSERT_TRUE(backend.initialize());
    const auto root = config.options().stdlib_path;
    EXPECT_TRUE(root.is_absolute());
    ASSERT_NE(nw::kernel::load_module("test_data/user/modules/DockerDemo.mod", false), nullptr);
    ASSERT_TRUE(backend.initialize());
    EXPECT_EQ(config.options().stdlib_path, root);
    EXPECT_EQ(nw::kernel::runtime().select_package_directory("nwn1"), std::filesystem::canonical(root / "nwn1"));
}

TEST(ClientRmlSmallsBridge, RuntimeReplacementRecreatesListsBeforePublishingObject)
{
    KernelServiceScope services;
    nw::toolset::RmlSmallsBridge bridge;
    nw::toolset::WorkspaceState workspace;
    nw::toolset::ToolsetBackend backend;
    backend.bind(&bridge, nullptr, &workspace);
    ASSERT_TRUE(backend.initialize());

    auto* first = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(first, nullptr);
    bridge.publish_active_object(first->handle());
    EXPECT_EQ(bridge.active_object(), first->handle());

    auto* module = nw::kernel::load_module(
        "test_data/user/modules/DockerDemo.mod", true);
    ASSERT_NE(module, nullptr);
    auto* second = nw::kernel::objects().load<nw::Item>("x2_it_mbelt001");
    ASSERT_NE(second, nullptr);
    bridge.publish_active_object(second->handle());

    EXPECT_EQ(bridge.active_object(), second->handle());
    const auto available = nw::toolset::ui_v1_host().window(
        "item.properties.available", 300, 0);
    ASSERT_TRUE(available);
    ASSERT_FALSE(available->items.empty());
    EXPECT_FALSE(available->items.front().cells[0].empty());
    const std::string markup = nw::toolset::render_managed_list_window(
        "item.properties.available", *available, "No properties.");
    EXPECT_NE(markup.find(available->items.front().cells[0]), std::string::npos);
    EXPECT_FALSE(nw::toolset::ui_v1_host().refresh_callbacks().empty());

    bridge.clear_active_object();
    nw::kernel::objects().destroy(second->handle());
}

TEST(ClientLoadingPresentation, OverlayKeepsTheCurrentPathAndChangesStageWithoutRebuilding)
{
    using namespace nw::toolset;
    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    auto* context = Rml::CreateContext("loading-presentation", {1200, 700});
    ASSERT_NE(context, nullptr);
    const auto template_path = std::filesystem::path{ROLLNW_TEST_SOURCE_DIR} / "tools/client/ui/command_modals.rml";
    auto* document = context->LoadDocument(template_path.string());
    ASSERT_NE(document, nullptr);
    LoadingViewState state;
    sync_loading_overlay(document, state.project_load);
    auto* host = document->GetElementById("project_load_overlay");
    ASSERT_NE(host, nullptr);
    EXPECT_FALSE(host->IsClassSet("active"));
    EXPECT_TRUE(host->GetInnerRML().empty());
    ASSERT_TRUE(queue_loading_project(state, "project <&>", CommandSource::widget));
    sync_loading_overlay(document, state.project_load);
    EXPECT_TRUE(host->IsClassSet("active"));
    auto* message = document->GetElementById("project_load_message");
    ASSERT_NE(message, nullptr);
    EXPECT_NE(host->GetInnerRML().find("project &lt;&amp;&gt;"), std::string::npos);
    state.project_load.stage = project_load_stage_message(nw::kernel::ModuleLoadProgressStage::load_dependencies);
    sync_loading_overlay(document, state.project_load);
    EXPECT_EQ(document->GetElementById("project_load_message"), message);
    EXPECT_EQ(message->GetInnerRML(), "Loading HAK and TLK dependencies...");
    EXPECT_FALSE(state.project_load.presented);
    state.project_load = {};
    sync_loading_overlay(document, state.project_load);
    EXPECT_FALSE(host->IsClassSet("active"));
    EXPECT_TRUE(host->GetInnerRML().empty());
    sync_loading_overlay(nullptr, state.project_load);
    document->Close();
    context->Update();
    Rml::RemoveContext("loading-presentation");
}

TEST(ClientRmlScrollLifetime, ReplacingAndClosingElementsDetachesScrollbarListenersBeforeChildren)
{
    NullRenderInterface renderer;
    RmlScope scope{renderer};
    ASSERT_TRUE(scope.initialized());
    auto* context = Rml::CreateContext("scrollbar-lifetime", {600, 400});
    ASSERT_NE(context, nullptr);
    auto* document = context->CreateDocument();
    ASSERT_NE(document, nullptr);
    const Rml::String markup = "<div id='scroll' style='width:100px;height:80px;overflow:scroll;'>"
                               "<div style='width:400px;height:300px;'></div></div>";
    document->SetInnerRML(markup);
    document->Show();
    context->Update();
    auto* scroll = document->GetElementById("scroll");
    ASSERT_NE(scroll, nullptr);
    ASSERT_NE(scroll->GetElementScroll()->GetScrollbar(Rml::ElementScroll::VERTICAL), nullptr);
    ASSERT_NE(scroll->GetElementScroll()->GetScrollbar(Rml::ElementScroll::HORIZONTAL), nullptr);
    document->SetInnerRML(markup);
    context->Update();
    document->Close();
    context->Update();
    Rml::RemoveContext("scrollbar-lifetime");
}
