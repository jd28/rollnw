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
#include <memory>
#include <string>
#include <string_view>

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
    static uint64_t generation = 0;
    static std::unique_ptr<RmlSmallsBenchmarkRuntime> runtime;
    const uint64_t current_generation = nw::kernel::services().generation();
    if (generation != current_generation || !runtime) {
        runtime.reset();
        runtime = std::make_unique<RmlSmallsBenchmarkRuntime>();
        generation = current_generation;
    }
    return *runtime;
}

constexpr const char* kDispatchSupportModulePath = "bench.rml_dispatch_actions";
constexpr const char* kDispatchSupportModuleSource = R"SMALLS(
from core.rmlui import { Event };
fn dispatch(event: Event) { }
)SMALLS";

bool ensure_dispatch_support_module()
{
    static uint64_t generation = 0;
    static bool loaded = false;
    const uint64_t current_generation = nw::kernel::services().generation();
    if (generation == current_generation) {
        return loaded;
    }

    generation = current_generation;
    loaded = [] {
        auto& runtime = nw::kernel::runtime();
        auto* script = runtime.load_module_from_source(
            kDispatchSupportModulePath, kDispatchSupportModuleSource);
        return script && runtime.get_or_compile_module(script) != nullptr;
    }();
    return loaded;
}

void BM_rml_smalls_dispatch(benchmark::State& state)
{
    auto& fixture = benchmark_runtime();
    if (!fixture.initialized) {
        state.SkipWithError("failed to initialize RmlUi Smalls benchmark runtime");
        return;
    }
    if (!ensure_dispatch_support_module()) {
        state.SkipWithError("failed to compile dispatch support module");
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
from bench.rml_dispatch_actions import { dispatch };
</script></head>
<body id="benchmark-document"><div id="target" onclick="dispatch(event)"></div></body>
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
    const auto first_dispatch_start = std::chrono::steady_clock::now();
    const bool first_dispatch_ok = target && target->DispatchEvent("click", {});
    const auto first_dispatch_end = std::chrono::steady_clock::now();
    if (!first_dispatch_ok) {
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
    state.counters["document_load_us"] = static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(compile_end - compile_start).count());
    state.counters["first_dispatch_us"] = static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            first_dispatch_end - first_dispatch_start)
            .count());
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

// == Document reload measurement =============================================
// ============================================================================
//
// Baseline for issues/smalls-document-module-arena-reclamation.md and for the
// direct-call binding that replaces generated document modules.
//
// Provider modules are warmed before the baseline is sampled. Each reload then
// parses the imports-only host block and every direct-call expression in nested
// TLS scratch storage, binds and dispatches every listener, and destroys the
// document. Runtime compiler counters must remain unchanged.

// Stands in for the toolset modules the real panel imports. Compiled once and
// warmed, so it never contributes to the measured per-reload slope.
constexpr const char* kReloadSupportModulePath = "bench.rml_reload_actions";
constexpr const char* kReloadSupportModuleSource = R"SMALLS(
from core.rmlui import { Event, Command };

fn action_00(event: Event): array!(Command) { return {}; }
fn action_01(event: Event): array!(Command) { return {}; }
fn action_02(event: Event): array!(Command) { return {}; }
fn action_03(event: Event): array!(Command) { return {}; }
fn action_04(event: Event): array!(Command) { return {}; }
fn action_05(event: Event): array!(Command) { return {}; }
fn action_06(event: Event): array!(Command) { return {}; }
fn action_07(event: Event): array!(Command) { return {}; }
fn action_08(event: Event): array!(Command) { return {}; }
fn action_09(event: Event): array!(Command) { return {}; }
fn action_10(event: Event): array!(Command) { return {}; }
fn action_11(event: Event): array!(Command) { return {}; }
fn apply_color(value: int) { }
)SMALLS";

// Representative panel with twelve independently imported actions.
constexpr int kPanelActions = 12;
// Matches the 16x11 palette rendered by toolset.item_editor.
constexpr int kPaletteCells = 176;
// Reloads performed before the counters are sampled, so first-load module
// compilation and any one-shot caches are excluded from the slope.
constexpr int kReloadWarmupCycles = 8;

enum ReloadShape : int64_t {
    reload_shape_minimal = 0, ///< One direct event call.
    reload_shape_import_only, ///< Imports only, with no listeners.
    reload_shape_panel_like,  ///< Twelve direct event calls.
    reload_shape_palette,     ///< One target with 176 bound integer arguments.
    reload_shape_count,
};

const char* reload_shape_name(int64_t shape)
{
    switch (shape) {
    default:
    case reload_shape_minimal:
        return "minimal";
    case reload_shape_import_only:
        return "import_only";
    case reload_shape_panel_like:
        return "panel_like";
    case reload_shape_palette:
        return "palette";
    }
}

std::string two_digits(int value)
{
    return (value < 10 ? "0" : "") + std::to_string(value);
}

std::string build_reload_markup(int64_t shape)
{
    std::string script;
    std::string body;

    switch (shape) {
    default:
    case reload_shape_minimal:
        script = std::string("from ") + kReloadSupportModulePath
            + " import { action_00 };\n";
        body = "<div id=\"target\" onclick=\"action_00(event)\"></div>";
        break;

    case reload_shape_import_only:
        script = std::string("import core.rmlui as RmlUi;\nimport ")
            + kReloadSupportModulePath + " as Actions;\n";
        body = R"(<div id="target"></div>)";
        break;

    case reload_shape_panel_like:
        script = "from ";
        script += kReloadSupportModulePath;
        script += " import {\n";
        for (int i = 0; i < kPanelActions; ++i) {
            script += "    action_" + two_digits(i) + ",\n";
        }
        script += "};\n";
        for (int i = 0; i < kPanelActions; ++i) {
            const auto suffix = two_digits(i);
            body += "<div id=\"target_" + suffix
                + "\" onclick=\"action_" + suffix
                + "(event)\"></div>";
        }
        break;

    case reload_shape_palette:
        script = "from ";
        script += kReloadSupportModulePath;
        script += " import { apply_color };\n";
        for (int i = 0; i < kPaletteCells; ++i) {
            body += "<button id=\"palette_" + std::to_string(i)
                + "\" onclick=\"apply_color(" + std::to_string(i)
                + ")\"></button>";
        }
        break;
    }

    return "<rml>\n<head><script>\n" + script + "</script></head>\n<body id=\"reload-document\">"
        + body + "</body>\n</rml>\n";
}

const std::string& reload_markup(int64_t shape)
{
    static const std::string cache[reload_shape_count] = {
        build_reload_markup(reload_shape_minimal),
        build_reload_markup(reload_shape_import_only),
        build_reload_markup(reload_shape_panel_like),
        build_reload_markup(reload_shape_palette),
    };
    return cache[shape < 0 || shape >= reload_shape_count ? 0 : shape];
}

bool ensure_reload_support_module()
{
    static uint64_t generation = 0;
    static bool loaded = false;
    const uint64_t current_generation = nw::kernel::services().generation();
    if (generation == current_generation) {
        return loaded;
    }

    generation = current_generation;
    loaded = [] {
        auto& runtime = nw::kernel::runtime();
        auto* script = runtime.load_module_from_source(kReloadSupportModulePath, kReloadSupportModuleSource);
        return script && runtime.get_or_compile_module(script) != nullptr;
    }();
    return loaded;
}

struct ReloadCounters {
    uint64_t arena_used_bytes = 0;
    uint64_t module_count = 0;
    uint64_t compiled_module_count = 0;
    uint64_t compiled_function_count = 0;
    uint64_t export_count = 0;
    uint64_t source_map_cache_entries = 0;
    uint64_t generic_function_count = 0;
    uint64_t generic_type_count = 0;
    uint64_t bound_listener_count = 0;
    uint64_t bound_argument_count = 0;
    uint64_t interned_target_count = 0;
};

ReloadCounters sample_reload_counters()
{
    const auto stats = nw::kernel::runtime().stats();
    const auto value = [&stats](const char* key) -> uint64_t {
        const auto it = stats.find(key);
        return it == stats.end() || !it->is_number_integer() ? 0 : it->get<uint64_t>();
    };

    const auto binding = benchmark_runtime().binding.stats();
    return ReloadCounters{
        .arena_used_bytes = value("compiler_arena_used_bytes"),
        .module_count = value("module_count"),
        .compiled_module_count = value("compiled_module_count"),
        .compiled_function_count = value("compiled_function_count"),
        .export_count = value("export_count"),
        .source_map_cache_entries = value("source_map_cache_entries"),
        .generic_function_count = value("instantiated_generic_function_count"),
        .generic_type_count = value("instantiated_generic_type_count"),
        .bound_listener_count = binding.bound_listener_count,
        .bound_argument_count = binding.bound_argument_count,
        .interned_target_count = binding.interned_target_count,
    };
}

// Signed so a counter that shrinks is visible rather than wrapping.
double counter_delta(uint64_t before, uint64_t after)
{
    return static_cast<double>(static_cast<int64_t>(after) - static_cast<int64_t>(before));
}

bool dispatch_reload_listeners(Rml::ElementDocument& document, int64_t shape)
{
    int listener_count = 0;
    std::string id_prefix;
    switch (shape) {
    case reload_shape_minimal:
        listener_count = 1;
        id_prefix = "target";
        break;
    case reload_shape_panel_like:
        listener_count = kPanelActions;
        id_prefix = "target_";
        break;
    case reload_shape_palette:
        listener_count = kPaletteCells;
        id_prefix = "palette_";
        break;
    case reload_shape_import_only:
    default:
        return true;
    }

    for (int i = 0; i < listener_count; ++i) {
        const std::string id = shape == reload_shape_minimal
            ? id_prefix
            : id_prefix + (shape == reload_shape_panel_like ? two_digits(i) : std::to_string(i));
        auto* element = document.GetElementById(id);
        if (!element || !element->DispatchEvent("click", {})) {
            return false;
        }
    }
    return true;
}

bool reload_document_once(Rml::Context* context, const std::string& markup,
    const std::string& source_url, int64_t shape)
{
    auto* document = context->LoadDocumentFromMemory(markup, source_url);
    if (!document) {
        return false;
    }
    document->Show();
    context->Update();
    const bool dispatched = dispatch_reload_listeners(*document, shape);
    document->Close();
    context->Update();
    return dispatched;
}

void record_reload_counters(benchmark::State& state, const ReloadCounters& before,
    const ReloadCounters& after, double reloads)
{
    const double arena = counter_delta(before.arena_used_bytes, after.arena_used_bytes);
    state.counters["arena_bytes_total"] = arena;
    state.counters["arena_bytes_per_reload"] = reloads > 0.0 ? arena / reloads : 0.0;
    state.counters["module_count_delta"] = counter_delta(before.module_count, after.module_count);
    state.counters["compiled_module_delta"]
        = counter_delta(before.compiled_module_count, after.compiled_module_count);
    state.counters["compiled_function_delta"]
        = counter_delta(before.compiled_function_count, after.compiled_function_count);
    state.counters["export_count_delta"] = counter_delta(before.export_count, after.export_count);
    state.counters["source_map_entry_delta"]
        = counter_delta(before.source_map_cache_entries, after.source_map_cache_entries);
    state.counters["generic_function_delta"]
        = counter_delta(before.generic_function_count, after.generic_function_count);
    state.counters["generic_type_delta"]
        = counter_delta(before.generic_type_count, after.generic_type_count);
    state.counters["bound_listeners_per_reload"] = reloads > 0.0
        ? counter_delta(before.bound_listener_count, after.bound_listener_count) / reloads
        : 0.0;
    state.counters["bound_arguments_per_reload"] = reloads > 0.0
        ? counter_delta(before.bound_argument_count, after.bound_argument_count) / reloads
        : 0.0;
    state.counters["interned_targets_per_reload"] = reloads > 0.0
        ? counter_delta(before.interned_target_count, after.interned_target_count) / reloads
        : 0.0;
    state.counters["reloads"] = reloads;
}

// Shared setup for both reload benchmarks. Returns the context name on success.
bool prepare_reload_context(benchmark::State& state, int64_t shape, std::string_view suffix,
    std::string& context_name, Rml::Context*& context)
{
    auto& fixture = benchmark_runtime();
    if (!fixture.initialized) {
        state.SkipWithError("failed to initialize RmlUi Smalls benchmark runtime");
        return false;
    }
    if (!ensure_reload_support_module()) {
        state.SkipWithError("failed to compile the reload support module");
        return false;
    }

    context_name = std::string("rml-smalls-reload-") + reload_shape_name(shape) + "-" + std::string(suffix);
    context = Rml::CreateContext(context_name, {320, 200});
    if (!context) {
        state.SkipWithError("failed to create benchmark RmlUi context");
        return false;
    }

    state.SetLabel(reload_shape_name(shape));
    return true;
}

// One load/show/close cycle per iteration. Reports wall time alongside the
// per-reload compiler-arena slope; the slope is the number that gates the
// direct-call binding, the timing is reported without a budget.
void BM_rml_smalls_document_reload(benchmark::State& state)
{
    const int64_t shape = state.range(0);
    std::string context_name;
    Rml::Context* context = nullptr;
    if (!prepare_reload_context(state, shape, "timed", context_name, context)) {
        return;
    }

    const auto& markup = reload_markup(shape);
    const std::string source_url = std::string("benchmark_reload_") + reload_shape_name(shape) + ".rml";

    for (int i = 0; i < kReloadWarmupCycles; ++i) {
        if (!reload_document_once(context, markup, source_url, shape)) {
            Rml::RemoveContext(context_name);
            state.SkipWithError("failed to load benchmark RML document");
            return;
        }
    }

    const auto before = sample_reload_counters();
    for (auto _ : state) {
        benchmark::DoNotOptimize(
            reload_document_once(context, markup, source_url, shape));
    }
    const auto after = sample_reload_counters();

    record_reload_counters(state, before, after, static_cast<double>(state.iterations()));
    state.SetItemsProcessed(state.iterations());
    Rml::RemoveContext(context_name);
}

BENCHMARK(BM_rml_smalls_document_reload)
    ->Arg(reload_shape_minimal)
    ->Arg(reload_shape_import_only)
    ->Arg(reload_shape_panel_like)
    ->Arg(reload_shape_palette);

// Fixed-count soak. Running the same shape at 100/1,000/10,000 reloads shows
// whether the arena slope is constant, so a single 10,000-cycle number is not
// mistaken for a plateau. Any positive arena_bytes_per_reload that holds steady
// across the three counts is unbounded growth over a session's lifetime.
void BM_rml_smalls_document_reload_soak(benchmark::State& state)
{
    const int64_t shape = state.range(0);
    const int64_t reloads = state.range(1);
    std::string context_name;
    Rml::Context* context = nullptr;
    if (!prepare_reload_context(state, shape, std::to_string(reloads), context_name, context)) {
        return;
    }

    const auto& markup = reload_markup(shape);
    const std::string source_url = std::string("benchmark_soak_") + reload_shape_name(shape) + ".rml";

    for (int i = 0; i < kReloadWarmupCycles; ++i) {
        if (!reload_document_once(context, markup, source_url, shape)) {
            Rml::RemoveContext(context_name);
            state.SkipWithError("failed to load benchmark RML document");
            return;
        }
    }

    ReloadCounters before = sample_reload_counters();
    ReloadCounters after = before;
    bool failed = false;
    for (auto _ : state) {
        before = sample_reload_counters();
        for (int64_t i = 0; i < reloads && !failed; ++i) {
            failed = !reload_document_once(context, markup, source_url, shape);
        }
        after = sample_reload_counters();
    }

    if (failed) {
        Rml::RemoveContext(context_name);
        state.SkipWithError("failed to load benchmark RML document during soak");
        return;
    }

    record_reload_counters(state, before, after, static_cast<double>(reloads));
    state.SetItemsProcessed(reloads);
    Rml::RemoveContext(context_name);
}

BENCHMARK(BM_rml_smalls_document_reload_soak)
    ->Iterations(1)
    ->ArgsProduct({
        {reload_shape_minimal, reload_shape_import_only, reload_shape_panel_like, reload_shape_palette},
        {100, 1000, 10000},
    });

} // namespace
