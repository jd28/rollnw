#include "rml_smalls_expression_binding.hpp"
#include "smalls_rmlui.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/smalls/runtime.hpp>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <array>
#include <string>
#include <string_view>

namespace {

class KernelServiceScope {
public:
    KernelServiceScope() { nw::kernel::services().start(); }
    ~KernelServiceScope()
    {
        nw::kernel::services().shutdown();
        nw::kernel::services().start();
    }
};

nw::smalls::Runtime& prepare_runtime()
{
    auto& runtime = nw::kernel::runtime();
    runtime.add_module_path("stdlib/core");
    nw::toolset::register_smalls_rmlui(runtime);
    EXPECT_NE(runtime.load_module("core.rmlui"), nullptr);

    auto* provider = runtime.load_module_from_source("test.rml_direct_provider", R"(
from core.rmlui import { Event };

type PaletteIndex(int);
const palette_index = PaletteIndex(42);

fn select(event: Event, value: int, scale: float, enabled: bool, label: string) { }
fn select_palette(event: Event, value: PaletteIndex) { }
fn with_default(event: Event, value: int = 7) { }
fn identity(value: int): int { return value; }
)");
    EXPECT_NE(provider, nullptr);
    EXPECT_NE(runtime.get_or_compile_module(provider), nullptr);
    return runtime;
}

constexpr std::string_view import_source = R"(
from test.rml_direct_provider import {
    select, select_palette, with_default, palette_index
};
import test.rml_direct_provider as Provider;
)";

constexpr std::array<std::string_view, 8> runtime_counter_names = {
    "module_count",
    "compiled_module_count",
    "compiled_function_count",
    "export_count",
    "source_map_cache_entries",
    "instantiated_generic_function_count",
    "instantiated_generic_type_count",
    "compiler_arena_used_bytes",
};

} // namespace

TEST(ClientRmlSmallsExpressionBinding, ResolvesTypedDirectCallsFromHostImports)
{
    KernelServiceScope services;
    auto& runtime = prepare_runtime();

    nw::toolset::RmlSmallsImportScope scope;
    std::string error;
    ASSERT_TRUE(nw::toolset::parse_rml_smalls_import_scope(
        runtime, import_source, scope, error))
        << error;
    EXPECT_EQ(scope.symbols.size(), 4);
    EXPECT_EQ(scope.aliases.size(), 1);

    nw::toolset::RmlSmallsResolvedCall call;
    ASSERT_TRUE(nw::toolset::resolve_rml_smalls_call(runtime, scope,
        R"(select(event, -42, +1.5, true, "bound"))", call, error))
        << error;
    ASSERT_TRUE(call.valid());
    EXPECT_EQ(call.module_path, "test.rml_direct_provider");
    EXPECT_EQ(call.function_name, "select");
    ASSERT_EQ(call.arguments.size(), 5);
    EXPECT_EQ(call.arguments[0].kind, nw::toolset::RmlSmallsArgumentKind::event);
    EXPECT_EQ(call.arguments[1].integer, -42);
    EXPECT_FLOAT_EQ(call.arguments[2].floating, 1.5f);
    EXPECT_TRUE(call.arguments[3].boolean);
    EXPECT_EQ(call.arguments[4].string, "bound");

    ASSERT_TRUE(nw::toolset::resolve_rml_smalls_call(runtime, scope,
        "select_palette(event, palette_index)", call, error))
        << error;
    ASSERT_EQ(call.arguments.size(), 2);
    EXPECT_EQ(call.arguments[1].kind,
        nw::toolset::RmlSmallsArgumentKind::integer);
    EXPECT_EQ(call.arguments[1].integer, 42);
    EXPECT_NE(call.arguments[1].type_id, runtime.int_type());

    ASSERT_TRUE(nw::toolset::resolve_rml_smalls_call(runtime, scope,
        R"(Provider.select(event, 1, 2.0, false, "alias"))", call, error))
        << error;
    EXPECT_EQ(call.function_name, "select");
}

TEST(ClientRmlSmallsExpressionBinding, RejectsWorkOutsideTheBoundedCallGrammar)
{
    KernelServiceScope services;
    auto& runtime = prepare_runtime();

    nw::toolset::RmlSmallsImportScope scope;
    std::string error;
    ASSERT_TRUE(nw::toolset::parse_rml_smalls_import_scope(
        runtime, import_source, scope, error))
        << error;

    nw::toolset::RmlSmallsResolvedCall call;
    EXPECT_FALSE(nw::toolset::resolve_rml_smalls_call(
        runtime, scope, "select", call, error));
    EXPECT_NE(error.find("direct call"), std::string::npos);

    EXPECT_FALSE(nw::toolset::resolve_rml_smalls_call(runtime, scope,
        R"(select(event, Provider.identity(1), 2.0, false, "nested"))",
        call, error));
    EXPECT_NE(error.find("argument 2"), std::string::npos);

    EXPECT_FALSE(nw::toolset::resolve_rml_smalls_call(
        runtime, scope, "with_default(event)", call, error));
    EXPECT_NE(error.find("every declared parameter"), std::string::npos);

    EXPECT_FALSE(nw::toolset::resolve_rml_smalls_call(
        runtime, scope, "Provider.identity(1)", call, error));
    EXPECT_NE(error.find("return void"), std::string::npos);

    EXPECT_FALSE(nw::toolset::parse_rml_smalls_import_scope(runtime,
        "from test.rml_direct_provider import { select }; fn extra() { }",
        scope, error));
    EXPECT_NE(error.find("imports only"), std::string::npos);

    const std::string oversized_expression(1025, 'x');
    EXPECT_FALSE(nw::toolset::resolve_rml_smalls_call(
        runtime, scope, oversized_expression, call, error));
    EXPECT_NE(error.find("1 KiB"), std::string::npos);
}

TEST(ClientRmlSmallsExpressionBinding, WarmedRebindingDoesNotGrowRuntimeCompilerState)
{
    KernelServiceScope services;
    auto& runtime = prepare_runtime();

    nw::toolset::RmlSmallsImportScope scope;
    nw::toolset::RmlSmallsResolvedCall call;
    std::string error;
    ASSERT_TRUE(nw::toolset::parse_rml_smalls_import_scope(
        runtime, import_source, scope, error))
        << error;
    ASSERT_TRUE(nw::toolset::resolve_rml_smalls_call(runtime, scope,
        R"(select(event, 42, 1.5, true, "warm"))", call, error))
        << error;

    const auto before = runtime.stats();
    for (int i = 0; i < 10'000; ++i) {
        ASSERT_TRUE(nw::toolset::parse_rml_smalls_import_scope(
            runtime, import_source, scope, error))
            << error;
        ASSERT_TRUE(nw::toolset::resolve_rml_smalls_call(runtime, scope,
            R"(select(event, 42, 1.5, true, "warm"))", call, error))
            << error;
    }
    const auto after = runtime.stats();

    for (const auto name : runtime_counter_names) {
        EXPECT_EQ(after.at(name), before.at(name)) << name;
    }
}
