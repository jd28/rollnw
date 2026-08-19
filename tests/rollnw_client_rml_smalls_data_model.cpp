#include "rml_smalls_data_model.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/smalls/runtime.hpp>

#include <RmlUi/Core.h>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <string>

namespace {

class NullRenderInterface final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(
        Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override
    {
        return 1;
    }

    void RenderGeometry(
        Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override
    {
    }
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override { }

    Rml::TextureHandle LoadTexture(
        Rml::Vector2i&, const Rml::String&) override
    {
        return 0;
    }
    Rml::TextureHandle GenerateTexture(
        Rml::Span<const Rml::byte>, Rml::Vector2i) override
    {
        return 0;
    }
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

class DestroyedViewWarningCapture final : public Rml::SystemInterface {
public:
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override
    {
        if (message == "Could not retrieve element in view, was it destroyed?") {
            ++count;
        }
        return Rml::SystemInterface::LogMessage(type, message);
    }

    size_t count = 0;
};

class SystemInterfaceScope {
public:
    explicit SystemInterfaceScope(Rml::SystemInterface& system)
        : previous_{Rml::GetSystemInterface()}
    {
        Rml::SetSystemInterface(&system);
    }

    ~SystemInterfaceScope() { Rml::SetSystemInterface(previous_); }

private:
    Rml::SystemInterface* previous_ = nullptr;
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

} // namespace

TEST(ClientRmlSmallsDataModel, ReadsAndRefreshesModuleGlobalStructArrays)
{
    KernelServiceScope kernel;
    auto& runtime = nw::kernel::runtime();
    auto* script = runtime.load_module_from_source("test.rml_smalls_data_model", R"(
        type Row {
            label: string;
            value: int;
            enabled: bool;
        };

        type Model {
            title: string;
            rows: array!(Row);
        };

        var rml_model: Model = {
            title = "Initial",
            rows = {
                Row { label = "Alpha", value = 7, enabled = true },
                Row { label = "Beta", value = 11, enabled = false },
            },
        };

        fn replace_model() {
            rml_model = {
                title = "Changed",
                rows = {
                    Row { label = "Gamma", value = 13, enabled = true },
                },
            };
        }
    )");
    ASSERT_NE(script, nullptr);
    ASSERT_NE(runtime.get_or_compile_module(script), nullptr);

    NullRenderInterface renderer;
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    auto* context = Rml::CreateContext(
        "rml-smalls-data-model-test", Rml::Vector2i{640, 480});
    ASSERT_NE(context, nullptr);

    nw::toolset::RmlSmallsDataModel missing_model;
    const std::array missing_bindings{
        nw::toolset::RmlSmallsGlobalBinding{
            .variable = "missing",
            .module = "test.rml_smalls_data_model",
            .global = "not_a_global",
        },
    };
    EXPECT_FALSE(missing_model.initialize(
        *context, runtime, "missing_smalls_test", missing_bindings));

    nw::toolset::RmlSmallsDataModel model;
    const std::array bindings{
        nw::toolset::RmlSmallsGlobalBinding{
            .variable = "editor",
            .module = "test.rml_smalls_data_model",
            .global = "rml_model",
        },
    };
    ASSERT_TRUE(model.initialize(
        *context, runtime, "smalls_test", bindings));

    auto* document = context->LoadDocumentFromMemory(R"(
        <rml>
        <body data-model="smalls_test">
            <div id="title">{{ editor.title }}</div>
            <div id="rows">
                <span data-for="row : editor.rows">{{ row.label }}={{ row.value }};</span>
            </div>
            <div id="missing">{{ editor.rows[99].label }}</div>
        </body>
        </rml>
    )");
    ASSERT_NE(document, nullptr);
    document->Show();
    context->Update();

    EXPECT_EQ(document->GetElementById("title")->GetInnerRML(), "Initial");
    const Rml::String initial_rows = document->GetElementById("rows")->GetInnerRML();
    EXPECT_NE(initial_rows.find("<span>Alpha=7;</span>"), Rml::String::npos);
    EXPECT_NE(initial_rows.find("<span>Beta=11;</span>"), Rml::String::npos);
    EXPECT_TRUE(document->GetElementById("missing")->GetInnerRML().empty());

    const auto result = runtime.execute_script(
        "test.rml_smalls_data_model", "replace_model", {});
    ASSERT_TRUE(result.ok());
    model.dirty_all();
    context->Update();

    EXPECT_EQ(document->GetElementById("title")->GetInnerRML(), "Changed");
    const Rml::String changed_rows = document->GetElementById("rows")->GetInnerRML();
    EXPECT_NE(changed_rows.find("<span>Gamma=13;</span>"), Rml::String::npos);
    EXPECT_EQ(changed_rows.find("<span>Beta=11;</span>"), Rml::String::npos);

    document->Close();
    model.shutdown();
    EXPECT_TRUE(Rml::RemoveContext("rml-smalls-data-model-test"));
}

TEST(ClientRmlSmallsDataModel, RendersCreatureSheetFromToolsetPresentationBatch)
{
    KernelServiceScope kernel;
    CurrentPathScope source_root{ROLLNW_TEST_SOURCE_DIR};
    auto& runtime = nw::kernel::runtime();
    runtime.add_module_path("build/tests/stdlib/core");
    runtime.add_module_path("build/tests/stdlib/nwn1");
    runtime.add_module_path("build/tests/stdlib/toolset");
    ASSERT_NE(runtime.load_module("toolset.ui"), nullptr);

    auto* creature = nw::kernel::objects().load_file<nw::Creature>(
        "build/tests/test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(creature, nullptr);
    runtime.init_object_propsets(creature->handle());
    auto object = nw::smalls::Value::make_object(creature->handle());
    object.type_id = runtime.object_subtype_for_tag(creature->handle().type);

    NullRenderInterface renderer;
    DestroyedViewWarningCapture warning_capture;
    SystemInterfaceScope system_interface{warning_capture};
    RmlScope rml{renderer};
    ASSERT_TRUE(rml.initialized());
    ASSERT_TRUE(Rml::LoadFontFace(
        "tools/client/assets/fonts/inter/Inter-Regular.ttf"));
    auto* context = Rml::CreateContext(
        "creature-sheet-data-model-test", Rml::Vector2i{800, 600});
    ASSERT_NE(context, nullptr);

    nw::toolset::RmlSmallsDataModel model;
    const std::array bindings{
        nw::toolset::RmlSmallsGlobalBinding{
            .variable = "toolset",
            .module = "toolset.ui",
            .global = "rml_model",
        },
    };
    ASSERT_TRUE(model.initialize(
        *context, runtime, "toolset_presentation", bindings));

    const std::string source = R"RML(
        <rml>
        <head>
          <link type="text/template" href="tools/client/ui/creature_editor.rml" />
          <style>body, button, input { font-family: Inter; font-weight: normal; }</style>
        </head>
        <body data-model="toolset_presentation">
          <div id="workspace_content"></div>
        </body>
        </rml>
    )RML";
    auto* document = context->LoadDocumentFromMemory(
        source, "creature_sheet_data_model_test.rml");
    ASSERT_NE(document, nullptr);
    document->Show();
    context->Update();

    auto* workspace = document->GetElementById("workspace_content");
    ASSERT_NE(workspace, nullptr);
    workspace->SetInnerRML(
        "<template src=\"creature-workbench\"></template>");
    workspace->SetInnerRML(
        "<template src=\"creature-workbench\"></template>");
    auto* rows = document->GetElementById("creature_sheet_rows");
    ASSERT_NE(rows, nullptr);

    const auto refreshed = runtime.execute_script(
        "toolset.ui", "refresh_creature_sheet_model", {object});
    ASSERT_TRUE(refreshed.ok()) << refreshed.error_message;
    model.dirty_all();
    context->Update();

    EXPECT_NE(document->GetElementById("creature_tab_details"), nullptr);
    EXPECT_NE(document->GetElementById("creature_tab_sheet"), nullptr);
    EXPECT_NE(document->GetElementById("creature_tab_inventory"), nullptr);
    const Rml::String markup = rows->GetInnerRML();
    EXPECT_NE(markup.find("Summary"), Rml::String::npos);
    EXPECT_NE(markup.find("Agent"), Rml::String::npos);
    EXPECT_NE(markup.find("Armor Class"), Rml::String::npos);
    EXPECT_NE(markup.find("Strength"), Rml::String::npos);
    EXPECT_NE(markup.find("Saving Throws"), Rml::String::npos);

    Rml::ElementList labels;
    rows->GetElementsByClassName(labels, "property_tree_label");
    ASSERT_FALSE(labels.empty());
    Rml::ElementList values;
    rows->GetElementsByClassName(values, "property_tree_value");
    Rml::ElementList twisties;
    rows->GetElementsByClassName(twisties, "tree_twisty");
    EXPECT_EQ(labels.size(), values.size());
    EXPECT_EQ(labels.size(), twisties.size());
    for (const auto* label : labels) {
        ASSERT_NE(label, nullptr);
        EXPECT_FALSE(label->GetInnerRML().empty());
    }

    workspace->SetInnerRML(
        "<template src=\"creature-workbench\"></template>");
    rows = document->GetElementById("creature_sheet_rows");
    ASSERT_NE(rows, nullptr);
    model.dirty_all();
    context->Update();
    EXPECT_NE(rows->GetInnerRML().find("Summary"), Rml::String::npos);
    EXPECT_EQ(warning_capture.count, 0);

    auto invalid = nw::smalls::Value::make_object(nw::ObjectHandle{});
    const auto cleared = runtime.execute_script(
        "toolset.ui", "refresh_creature_sheet_model", {invalid});
    ASSERT_TRUE(cleared.ok()) << cleared.error_message;
    model.dirty_all();
    context->Update();

    const Rml::String cleared_markup = rows->GetInnerRML();
    EXPECT_NE(cleared_markup.find("Waiting for a live Creature"),
        Rml::String::npos);
    EXPECT_EQ(cleared_markup.find("Summary"), Rml::String::npos);

    const auto restored = runtime.execute_script(
        "toolset.ui", "refresh_creature_sheet_model", {object});
    ASSERT_TRUE(restored.ok()) << restored.error_message;
    model.dirty_all();
    context->Update();
    EXPECT_NE(rows->GetInnerRML().find("Summary"), Rml::String::npos);

    document->Close();
    model.shutdown();
    EXPECT_TRUE(Rml::RemoveContext("creature-sheet-data-model-test"));
    nw::kernel::objects().destroy(creature->handle());
}
