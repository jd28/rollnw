#include "object_edits.hpp"
#include "rml_managed_list.hpp"
#include "rml_smalls_bridge.hpp"
#include "rml_smalls_language_binding.hpp"
#include "script_commands.hpp"
#include "smalls_rmlui.hpp"
#include "smalls_ui_v1.hpp"
#include "virtual_list.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Door.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/serialization/Serialization.hpp>
#include <nw/smalls/runtime.hpp>

#include <RmlUi/Core.h>
#include <RmlUi/Core/ElementText.h>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
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

bool diagnostic_contains(const nw::toolset::RmlSmallsLanguageBinding& binding, std::string_view needle)
{
    const auto& diagnostics = binding.diagnostics();
    return std::any_of(diagnostics.begin(), diagnostics.end(), [needle](const auto& diagnostic) {
        return diagnostic.message.find(needle) != std::string::npos;
    });
}

std::string ascii_lower_copy(std::string_view value)
{
    std::string result{value};
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
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
          "</head><body><template src=\"item-workbench\"></template>"
          "<div id=\"object_variable_warning_tooltip\"></div></body></rml>";

    auto* context = Rml::CreateContext("item-workbench-template-test", {800, 600});
    ASSERT_NE(context, nullptr);
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
    EXPECT_TRUE(appearance->IsClassSet("smalls_refresh"));
    auto* property_tree = document->GetElementById("property_tree_rows");
    ASSERT_NE(property_tree, nullptr);
    EXPECT_EQ(property_tree->GetParentNode(), details);
    auto* property_surface = document->GetElementById("item_surface_item_properties");
    ASSERT_NE(property_surface, nullptr);
    property_surface->SetClass("active", true);
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

    Rml::ElementList model_rows;
    document->GetElementsByClassName(model_rows, "item_model_row");
    EXPECT_TRUE(model_rows.empty());

    document->Close();
    Rml::RemoveContext("item-workbench-template-test");
}

TEST(ClientRmlTemplates, DoorWorkbenchExpandsSmallsFirstAppearanceStructure)
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
    EXPECT_TRUE(appearance_dynamic->IsClassSet("smalls_refresh"));
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

TEST(ClientRmlSmallsLanguageBinding, CompilesItemEditorWithRegisteredNativeProtocols)
{
    KernelServiceScope services;
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
    ASSERT_TRUE(load_and_compile("toolset.door_editor"));

    const auto verify_options = [&](std::string_view function, int32_t minimum_row) {
        const auto result = runtime.execute_script("nwn1.doors", function, {});
        ASSERT_TRUE(result.ok()) << function;
        ASSERT_EQ(result.value.storage, nw::smalls::ValueStorage::heap);
        const auto count = runtime.array_size(result.value.data.hptr);
        ASSERT_GT(count, 0u) << function;

        for (uint32_t index = 0; index < count; ++index) {
            nw::smalls::Value option;
            ASSERT_TRUE(runtime.array_get(result.value.data.hptr, index, option));
            ASSERT_EQ(option.storage, nw::smalls::ValueStorage::heap);
            const auto row = runtime.read_struct_field(
                option.data.hptr, option.type_id, "id");
            ASSERT_EQ(row.type_id, runtime.int_type());
            EXPECT_GE(row.data.ival, minimum_row);
            const auto label = runtime.read_struct_field(
                option.data.hptr, option.type_id, "label");
            ASSERT_EQ(label.type_id, runtime.string_type());
            EXPECT_FALSE(runtime.get_string_view(label.data.hptr).empty());
            const auto model = runtime.read_struct_field(
                option.data.hptr, option.type_id, "model");
            ASSERT_EQ(model.type_id, runtime.string_type());
            EXPECT_TRUE(nw::kernel::resman().contains(
                {runtime.get_string_view(model.data.hptr), nw::ResourceType::mdl}));
        }
    };
    verify_options("get_genericdoor_options", 0);

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

    const auto appearance_refresh = runtime.execute_script(
        "toolset.door_editor", "door_appearance_refresh", {});
    ASSERT_TRUE(appearance_refresh.ok());
    ASSERT_EQ(runtime.array_size(appearance_refresh.value.data.hptr), 1u);
    nw::smalls::Value appearance_command;
    ASSERT_TRUE(runtime.array_get(
        appearance_refresh.value.data.hptr, 0u, appearance_command));
    const auto appearance_markup = runtime.read_struct_field(
        appearance_command.data.hptr, appearance_command.type_id, "value");
    ASSERT_EQ(appearance_markup.type_id, runtime.string_type());
    const auto appearance_markup_text = runtime.get_string_view(
        appearance_markup.data.hptr);
    EXPECT_NE(appearance_markup_text.find("door_appearance_label'>Name"),
        std::string_view::npos);
    EXPECT_EQ(appearance_markup_text.find("door_appearance_label'>Row"),
        std::string_view::npos);
    EXPECT_NE(appearance_markup_text.find(resolved_label), std::string_view::npos);
    EXPECT_NE(appearance_markup_text.find("door_appearance_previous"),
        std::string_view::npos);
    EXPECT_NE(appearance_markup_text.find("door_appearance_next"),
        std::string_view::npos);

    const auto open_selector = runtime.execute_script(
        "toolset.door_editor", "open_door_appearance_selector", {});
    ASSERT_TRUE(open_selector.ok());
    ASSERT_GT(runtime.array_size(open_selector.value.data.hptr), 0u);
    nw::smalls::Value selector_command;
    ASSERT_TRUE(runtime.array_get(
        open_selector.value.data.hptr, 0u, selector_command));
    const auto selector_markup = runtime.read_struct_field(
        selector_command.data.hptr, selector_command.type_id, "value");
    ASSERT_EQ(selector_markup.type_id, runtime.string_type());
    const auto selector_markup_text = runtime.get_string_view(
        selector_markup.data.hptr);
    EXPECT_NE(selector_markup_text.find("id='door_appearance_search'"),
        std::string_view::npos);
    EXPECT_NE(selector_markup_text.find(
                  "onchange='filter_door_appearances(event)'"),
        std::string_view::npos);
    EXPECT_EQ(selector_markup_text.find("Door Types"), std::string_view::npos);
    const auto genericdoors = nw::toolset::ui_v1_host().window(
        "door.appearance.genericdoors", 300, 0);
    ASSERT_TRUE(genericdoors);
    EXPECT_TRUE(genericdoors->visible);
    ASSERT_FALSE(genericdoors->items.empty());
    EXPECT_EQ(genericdoors->columns, 1);
    EXPECT_EQ(genericdoors->items.front().cell_count, 1);
    ASSERT_FALSE(genericdoors->items.front().cells[0].empty());
    std::vector<std::string> genericdoor_labels;
    genericdoor_labels.reserve(genericdoors->items.size());
    for (const auto& item : genericdoors->items) {
        genericdoor_labels.push_back(ascii_lower_copy(item.cells[0]));
    }
    EXPECT_TRUE(std::ranges::is_sorted(genericdoor_labels));

    const auto unfiltered_genericdoor_count = genericdoors->items.size();
    const std::string first_genericdoor_label =
        genericdoors->items.front().cells[0];
    const auto event_type = runtime.type_id("core.rmlui.Event", false);
    ASSERT_NE(event_type, nw::smalls::invalid_type_id);
    nw::smalls::Runtime::ScopedRoots roots{runtime, 3};
    const auto event_ptr = runtime.alloc_struct(event_type);
    ASSERT_NE(event_ptr.value, 0u);
    const auto event = nw::smalls::Value::make_heap(event_ptr, event_type);
    roots.add(event);
    const std::string filter_text = " " + first_genericdoor_label + " ";
    const auto query = nw::smalls::Value::make_string(
        runtime.alloc_string(filter_text));
    roots.add(query);
    ASSERT_TRUE(runtime.write_struct_field(
        event_ptr, event_type, "value", query));
    const auto filter_genericdoors = runtime.execute_script(
        "toolset.door_editor", "filter_door_appearances", {event});
    ASSERT_TRUE(filter_genericdoors.ok());
    const auto filtered_genericdoors = nw::toolset::ui_v1_host().window(
        "door.appearance.genericdoors", 300, 0);
    ASSERT_TRUE(filtered_genericdoors);
    ASSERT_FALSE(filtered_genericdoors->items.empty());
    EXPECT_LT(filtered_genericdoors->items.size(), unfiltered_genericdoor_count);
    const auto expected_label = ascii_lower_copy(first_genericdoor_label);
    for (const auto& item : filtered_genericdoors->items) {
        EXPECT_EQ(ascii_lower_copy(item.cells[0]), expected_label);
    }

    const auto empty_query = nw::smalls::Value::make_string(
        runtime.alloc_string(""));
    roots.add(empty_query);
    ASSERT_TRUE(runtime.write_struct_field(
        event_ptr, event_type, "value", empty_query));
    const auto clear_genericdoor_filter = runtime.execute_script(
        "toolset.door_editor", "filter_door_appearances", {event});
    ASSERT_TRUE(clear_genericdoor_filter.ok());

    const auto restored_genericdoors = nw::toolset::ui_v1_host().window(
        "door.appearance.genericdoors", 300, 0);
    ASSERT_TRUE(restored_genericdoors);
    const std::string genericdoor_markup = nw::toolset::render_managed_list_window(
        "door.appearance.genericdoors", *restored_genericdoors, "No appearances.");
    EXPECT_NE(genericdoor_markup.find("managed_list_row"), std::string::npos);
    EXPECT_EQ(genericdoor_markup.find("managed_list_grid_row"), std::string::npos);

    nw::toolset::smalls_rmlui_host().clear_active_object();
    nw::kernel::objects().destroy(door->handle());
}

TEST(ClientRmlSmallsBridge, RuntimeReplacementRecreatesListsBeforePublishingObject)
{
    KernelServiceScope services;
    nw::toolset::RmlSmallsBridge bridge;
    ASSERT_TRUE(bridge.initialize());

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
