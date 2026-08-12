#include "rml_smalls_language_binding.hpp"
#include "smalls_rmlui.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/smalls/runtime.hpp>

#include <RmlUi/Core.h>

#include <benchmark/benchmark.h>

#include <chrono>
#include <cstdint>

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

struct RmlSmallsBenchmarkRuntime {
    RmlSmallsBenchmarkRuntime()
    {
        auto& runtime = nw::kernel::runtime();
        runtime.add_module_path("stdlib/core");
        nw::toolset::register_smalls_rmlui(runtime);
        auto* module = runtime.load_module("core.rmlui");

        Rml::SetRenderInterface(&renderer);
        initialized = module
            && runtime.get_or_compile_module(module)
            && Rml::Initialise()
            && binding.initialize(runtime);
    }

    ~RmlSmallsBenchmarkRuntime()
    {
        if (initialized) {
            Rml::Shutdown();
        }
        Rml::SetRenderInterface(nullptr);
    }

    NullRenderInterface renderer;
    nw::toolset::RmlSmallsLanguageBinding binding;
    bool initialized = false;
};

RmlSmallsBenchmarkRuntime& benchmark_runtime()
{
    static RmlSmallsBenchmarkRuntime runtime;
    return runtime;
}

void BM_rml_smalls_dispatch(benchmark::State& state)
{
    auto& fixture = benchmark_runtime();
    if (!fixture.initialized) {
        state.SkipWithError("failed to initialize RmlUi Smalls benchmark runtime");
        return;
    }

    auto& runtime = nw::kernel::runtime();
    auto* creature = nw::kernel::objects().make<nw::Creature>();
    if (!creature) {
        state.SkipWithError("failed to create benchmark active object");
        return;
    }
    const auto creature_handle = creature->handle();
    nw::toolset::smalls_rmlui_host().publish_active_object(creature_handle);

    const std::string context_name = "rml-smalls-benchmark-" + std::to_string(state.range(0));
    auto* context = Rml::CreateContext(context_name, {320, 200});
    if (!context) {
        nw::toolset::smalls_rmlui_host().clear_active_object();
        nw::kernel::objects().destroy(creature_handle);
        state.SkipWithError("failed to create benchmark RmlUi context");
        return;
    }

    const auto stats_before = runtime.stats();
    const auto compile_start = std::chrono::steady_clock::now();
    auto* document = context->LoadDocumentFromMemory(R"RML(
<rml>
<head><script>
from core.rmlui import { Event };
fn dispatch(event: Event) { }
</script></head>
<body id="benchmark-document"><div id="target" onclick="dispatch"></div></body>
</rml>
)RML",
        "benchmark_dispatch.rml");
    const auto compile_end = std::chrono::steady_clock::now();
    if (!document) {
        Rml::RemoveContext(context_name);
        nw::toolset::smalls_rmlui_host().clear_active_object();
        nw::kernel::objects().destroy(creature_handle);
        state.SkipWithError("failed to load benchmark RML document");
        return;
    }

    document->Show();
    context->Update();
    auto* target = document->GetElementById("target");
    if (!target || !target->DispatchEvent("click", {})) {
        document->Close();
        context->Update();
        Rml::RemoveContext(context_name);
        nw::toolset::smalls_rmlui_host().clear_active_object();
        nw::kernel::objects().destroy(creature_handle);
        state.SkipWithError("failed to warm up benchmark Smalls handler");
        return;
    }

    const auto stats_after = runtime.stats();
    const auto arena_before = stats_before["compiler_arena_used_bytes"].get<uint64_t>();
    const auto arena_after = stats_after["compiler_arena_used_bytes"].get<uint64_t>();
    state.counters["document_compile_us"] = static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(compile_end - compile_start).count());
    state.counters["compiler_arena_bytes"] = static_cast<double>(arena_after - arena_before);

    const int64_t event_count = state.range(0);
    for (auto _ : state) {
        for (int64_t i = 0; i < event_count; ++i) {
            benchmark::DoNotOptimize(target->DispatchEvent("click", {}));
        }
    }
    state.SetItemsProcessed(state.iterations() * event_count);

    document->Close();
    context->Update();
    Rml::RemoveContext(context_name);
    nw::toolset::smalls_rmlui_host().clear_active_object();
    nw::kernel::objects().destroy(creature_handle);
}

BENCHMARK(BM_rml_smalls_dispatch)->Arg(1)->Arg(16)->Arg(256);

} // namespace
