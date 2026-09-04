#include "appearance_catalog.hpp"
#include "item_editor_data_model.hpp"
#include "object_edits.hpp"
#include "rml_managed_list.hpp"
#include "rml_smalls_bridge.hpp"
#include "rml_smalls_language_binding.hpp"
#include "script_commands.hpp"
#include "smalls_rmlui.hpp"
#include "smalls_ui_v1.hpp"
#include "toolset_backend.hpp"
#include "virtual_list.hpp"
#include "workspace.hpp"

#include <nw/kernel/Kernel.hpp>
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

#include <RmlUi/Core.h>
#include <RmlUi/Core/ElementText.h>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
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
        "<div class='managed_list_row selected'>"
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
        "<div class='managed_list_row selected'>"
        "<span id='body-part-option-number' class='managed_list_cell cell_0'>119</span>"
        "<span id='body-part-option-detail' class='managed_list_cell cell_1'>Current</span>"
        "</div>");
    context->Update();
    auto* option_number = document->GetElementById(
        "body-part-option-number");
    auto* option_detail = document->GetElementById(
        "body-part-option-detail");
    ASSERT_NE(option_number, nullptr);
    ASSERT_NE(option_detail, nullptr);
    EXPECT_GE(option_number->GetOffsetWidth(), 30.0f);
    EXPECT_GE(option_detail->GetOffsetWidth(), 50.0f);
    auto* option_number_text = rmlui_dynamic_cast<Rml::ElementText*>(
        option_number->GetFirstChild());
    ASSERT_NE(option_number_text, nullptr);
    ASSERT_FALSE(option_number_text->GetLines().empty());
    EXPECT_EQ(option_number_text->GetLines().front().text, "119");

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
                event.selection.list_id, event.type);
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
    sound_resources_after.push_back(sound_resources_before->front());
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
