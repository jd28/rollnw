#include "rml_smalls_data_model.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/smalls/runtime.hpp>

#include <RmlUi/Core.h>

#include <gtest/gtest.h>

#include <array>

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

class KernelServiceScope {
public:
    KernelServiceScope() { nw::kernel::services().start(); }
    ~KernelServiceScope()
    {
        nw::kernel::services().shutdown();
        nw::kernel::services().start();
    }
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
    const Rml::String initial_rows =
        document->GetElementById("rows")->GetInnerRML();
    EXPECT_NE(initial_rows.find("<span>Alpha=7;</span>"), Rml::String::npos);
    EXPECT_NE(initial_rows.find("<span>Beta=11;</span>"), Rml::String::npos);
    EXPECT_TRUE(document->GetElementById("missing")->GetInnerRML().empty());

    const auto result = runtime.execute_script(
        "test.rml_smalls_data_model", "replace_model", {});
    ASSERT_TRUE(result.ok());
    model.dirty_all();
    context->Update();

    EXPECT_EQ(document->GetElementById("title")->GetInnerRML(), "Changed");
    const Rml::String changed_rows =
        document->GetElementById("rows")->GetInnerRML();
    EXPECT_NE(changed_rows.find("<span>Gamma=13;</span>"), Rml::String::npos);
    EXPECT_EQ(changed_rows.find("<span>Beta=11;</span>"), Rml::String::npos);

    document->Close();
    model.shutdown();
    EXPECT_TRUE(Rml::RemoveContext("rml-smalls-data-model-test"));
}
