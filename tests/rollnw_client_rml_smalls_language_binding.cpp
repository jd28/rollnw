#include "rml_managed_list.hpp"
#include "rml_smalls_bridge.hpp"
#include "rml_smalls_language_binding.hpp"
#include "script_commands.hpp"
#include "smalls_rmlui.hpp"
#include "smalls_ui_v1.hpp"
#include "virtual_list.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/serialization/Serialization.hpp>
#include <nw/smalls/runtime.hpp>

#include <RmlUi/Core.h>
#include <RmlUi/Core/ElementText.h>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
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
          "</head><body><template src=\"item-workbench\"></template></body></rml>";

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

    Rml::ElementList model_rows;
    document->GetElementsByClassName(model_rows, "item_model_row");
    EXPECT_TRUE(model_rows.empty());

    document->Close();
    Rml::RemoveContext("item-workbench-template-test");
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

TEST(ClientRmlSmallsLanguageBinding, MutatesSharedActiveObjectAndAppliesCommands)
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

    const std::filesystem::path external_script_path = "tmp/rml_smalls_external.smalls";
    {
        std::ofstream external_script{external_script_path};
        ASSERT_TRUE(external_script);
        external_script << R"(
from core.rmlui import { Event };
from nwn1.propsets import { CreatureAppearance };

fn external_select(event: Event) {
    var creature = event.active_object as Creature;
    var appearance = get_propset!(CreatureAppearance)(creature);
    appearance.appearance = appearance.appearance + 10;
}
)";
    }

    auto* document = context->LoadDocumentFromMemory(R"RML(
<rml>
<head>
  <script>
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

fn runtime_failure(event: Event) {
    assert(false);
}

fn build_dynamic(event: Event): array!(Command) {
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
  </script>
  <script src="tmp/rml_smalls_external.smalls"></script>
</head>
<body id="binding-document">
  <button id="select" onclick="select_appearance">Select</button>
  <button id="external-select" onclick="external_select">External</button>
  <button id="missing" onclick="missing_handler">Missing</button>
  <button id="runtime-fail" onclick="runtime_failure">Runtime failure</button>
  <button id="build-dynamic" onclick="build_dynamic">Build</button>
  <div id="result">unchanged</div>
  <div id="dynamic"></div>
  <div id="refresh-dynamic" class="smalls_refresh" onrefresh="refresh_dynamic"></div>
</body>
</rml>
)RML",
        "binding_inline.rml");
    ASSERT_NE(document, nullptr);
    document->Show();
    context->Update();
    ASSERT_FALSE(diagnostic_contains(binding, "error:"));

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

    auto* external_select = document->GetElementById("external-select");
    ASSERT_NE(external_select, nullptr);
    EXPECT_TRUE(external_select->DispatchEvent("click", {}));
    const auto externally_updated = runtime.read_struct_value_field(appearance, appearance_definition, appearance_field);
    EXPECT_EQ(externally_updated.data.ival, 17);

    nlohmann::json serialized_creature;
    bool (*serialize_json)(const nw::Creature*, nlohmann::json&, nw::SerializationProfile) = nw::serialize;
    ASSERT_TRUE(serialize_json(creature, serialized_creature, nw::SerializationProfile::blueprint));
    ASSERT_TRUE(serialized_creature.contains("nwn1.propsets.CreatureAppearance"));
    EXPECT_EQ(serialized_creature["nwn1.propsets.CreatureAppearance"]["appearance"], 17);

    binding.clear_diagnostics();
    ASSERT_NE(document->GetElementById("missing"), nullptr);
    EXPECT_TRUE(document->GetElementById("missing")->DispatchEvent("click", {}));
    EXPECT_TRUE(diagnostic_contains(binding, "handler not found: missing_handler"));

    binding.clear_diagnostics();
    ASSERT_NE(document->GetElementById("runtime-fail"), nullptr);
    EXPECT_TRUE(document->GetElementById("runtime-fail")->DispatchEvent("click", {}));
    EXPECT_TRUE(diagnostic_contains(binding, "binding_inline.rml"));

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

    auto* terminal_module = runtime.load_module_from_source("test.rmlui_terminal", R"(
from core.rmlui import { active_object };
fn selected(): object { return active_object(); }
)");
    ASSERT_NE(terminal_module, nullptr);
    ASSERT_NE(runtime.get_or_compile_module(terminal_module), nullptr);
    const auto terminal_result = runtime.execute_script("test.rmlui_terminal", "selected", {});
    ASSERT_TRUE(terminal_result.ok());
    EXPECT_EQ(terminal_result.value.data.oval, creature_handle);

    nw::kernel::objects().destroy(creature_handle);
    ASSERT_FALSE(nw::kernel::objects().valid(creature_handle));
    binding.clear_diagnostics();
    EXPECT_TRUE(select->DispatchEvent("click", {}));
    EXPECT_TRUE(diagnostic_contains(binding, "no active object"));
    EXPECT_EQ(nw::toolset::smalls_rmlui_host().active_object().type, nw::ObjectType::invalid);

    nw::toolset::smalls_rmlui_host().clear_active_object();
    invalid_document->Close();
    document->Close();
    context->Update();
    EXPECT_FALSE(runtime.evict_module("toolset.rml.document_2_block_0"));
    EXPECT_FALSE(runtime.evict_module("toolset.rml.document_2_block_1"));
    std::filesystem::remove(external_script_path);
    Rml::RemoveContext("smalls-language-binding-test");
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
from core.rmlui import { Event, Command, command_set_text };

fn select(event: Event): array!(Command) {
    return {{
        operation = command_set_text,
        element_id = "result",
        value = "current runtime",
        state = false,
    }};
}
</script></head>
<body><button id="select" onclick="select">Select</button><div id="result">stale</div></body>
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
    ASSERT_TRUE(load_and_compile("toolset.item_editor"));
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
