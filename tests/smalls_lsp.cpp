#include "smalls_fixtures.hpp"

#include <nw/log.hpp>
#include <nw/smalls/Context.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/runtime.hpp>

#include <string_view>

using namespace std::literals;

// ============================================================================
// Symbol Location (Go to Definition)
// ============================================================================

TEST_F(SmallsLSP, LocateLocalVariable)
{
    auto script = make_script(R"(fn test() {
    var my_var: int = 42;
    var result = my_var + 10;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    // Locate "my_var" at line 2 (the usage)
    auto symbol = script.locate_symbol("my_var", 3, 18);
    EXPECT_NE(symbol.decl, nullptr);
    EXPECT_EQ(symbol.kind, nw::smalls::SymbolKind::variable);
}

TEST_F(SmallsLSP, SemanticToolingRequiresFullDebugLevel)
{
    auto config = nw::kernel::runtime().diagnostic_config();
    config.debug_level = nw::smalls::DebugLevel::source_map;
    nw::kernel::runtime().set_diagnostic_config(config);

    auto script = make_script(R"(fn test() {
    var my_var: int = 42;
    var result = my_var + 10;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_NO_THROW(script.resolve());

    auto symbol = script.locate_symbol("my_var", 3, 18);
    EXPECT_EQ(symbol.decl, nullptr);
    EXPECT_FALSE(script.diagnostics().empty());
    EXPECT_NE(script.diagnostics().back().message.find("semantic tooling requires DebugLevel::full"), std::string::npos);
}

TEST_F(SmallsLSP, LocateFunctionDefinition)
{
    auto script = make_script(R"(fn helper(): int {
    return 42;
}

fn main(): int {
    return helper();
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    // Locate "helper" in the call expression
    auto symbol = script.locate_symbol("helper", 6, 11);
    EXPECT_NE(symbol.decl, nullptr);
    EXPECT_EQ(symbol.kind, nw::smalls::SymbolKind::function);
}

TEST_F(SmallsLSP, LocateFunctionParameter)
{
    auto script = make_script(R"(fn add(x: int, y: int): int {
    return x + y;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    // Locate parameter "x" in the return statement
    auto symbol = script.locate_symbol("x", 2, 11);
    EXPECT_NE(symbol.decl, nullptr);
    EXPECT_EQ(symbol.kind, nw::smalls::SymbolKind::variable);
}

TEST_F(SmallsLSP, LocateStructType)
{
    auto script = make_script(R"(type Point {
    x: int;
    y: int;
};

fn create_point(): Point {
    var p: Point;
    return p;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    // Locate "Point" in the function return type
    auto symbol = script.locate_symbol("Point", 6, 19);
    EXPECT_NE(symbol.decl, nullptr);
    EXPECT_EQ(symbol.kind, nw::smalls::SymbolKind::type);
}

TEST_F(SmallsLSP, LocateStructField)
{
    auto script = make_script(R"(type Point {
    x: int;
    y: int;
};

fn test() {
    var p: Point;
    var val = p.x;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    // Locate field "x" in the dot expression
    auto symbol = script.locate_symbol("x", 8, 16);
    EXPECT_NE(symbol.decl, nullptr);
    EXPECT_EQ(symbol.kind, nw::smalls::SymbolKind::field);
}

TEST_F(SmallsLSP, LocateGlobalVariable)
{
    auto script = make_script(R"(var global_count: int = 0;

fn increment() {
    global_count = global_count + 1;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    // Locate "global_count" in the function
    auto symbol = script.locate_symbol("global_count", 4, 4);
    EXPECT_NE(symbol.decl, nullptr);
    EXPECT_EQ(symbol.kind, nw::smalls::SymbolKind::variable);
}

TEST_F(SmallsLSP, LocateTypeAlias)
{
    auto script = make_script(R"(type MyInt = int;

fn use_alias(x: MyInt): MyInt {
    return x;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    // Locate "MyInt" in parameter type
    auto symbol = script.locate_symbol("MyInt", 3, 16);
    EXPECT_NE(symbol.decl, nullptr);
    EXPECT_EQ(symbol.kind, nw::smalls::SymbolKind::type);
}

TEST_F(SmallsLSP, LocateDestructuredVariable)
{
    auto script = make_script(R"(fn swap(x: int, y: int): (int, int) {
    return y, x;
}

fn test() {
    var a, b = swap(1, 2);
    var result = a + b;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    // Locate "a" in the usage
    auto symbol = script.locate_symbol("a", 7, 17);
    EXPECT_NE(symbol.decl, nullptr);
    EXPECT_EQ(symbol.kind, nw::smalls::SymbolKind::variable);
}

// ============================================================================
// Inlay Hints (Parameter Names)
// ============================================================================

TEST_F(SmallsLSP, InlayHintsSimpleCall)
{
    auto script = make_script(R"(fn add(x: int, y: int): int {
    return x + y;
}

fn main() {
    var result = add(10, 20);
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    // Get inlay hints for the entire script
    auto hints = script.inlay_hints({{1, 0}, {7, 0}});

    // Should have hints for the two parameters
    EXPECT_GE(hints.size(), 2);

    // Check that we have hints for "x" and "y"
    bool has_x = false, has_y = false;
    for (const auto& hint : hints) {
        if (hint.message == "x") has_x = true;
        if (hint.message == "y") has_y = true;
    }
    EXPECT_TRUE(has_x);
    EXPECT_TRUE(has_y);
}

TEST_F(SmallsLSP, InlayHintsNestedCalls)
{
    auto script = make_script(R"(fn multiply(a: int, b: int): int {
    return a * b;
}

fn add(x: int, y: int): int {
    return x + y;
}

fn main() {
    var result = add(multiply(2, 3), multiply(4, 5));
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    // Get inlay hints for the main function
    auto hints = script.inlay_hints({{9, 0}, {11, 0}});

    // Should have hints for all parameters: x, y, a, b (twice)
    EXPECT_GE(hints.size(), 4);
}

TEST_F(SmallsLSP, InlayHintsNoParams)
{
    auto script = make_script(R"(fn get_value(): int {
    return 42;
}

fn main() {
    var result = get_value();
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    // Get inlay hints - should be empty or minimal
    auto hints = script.inlay_hints({{5, 0}, {7, 0}});

    // Function with no parameters should not produce parameter hints
    EXPECT_EQ(hints.size(), 0);
}

// ============================================================================
// Signature Help
// ============================================================================

TEST_F(SmallsLSP, SignatureHelpBasic)
{
    auto script = make_script(R"(fn add(x: int, y: int): int {
    return x + y;
}

fn main() {
    var result = add(10, 20);
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    // Get signature help while inside the function call
    auto sig = script.signature_help(6, 21); // Inside "add(10, 20)"

    EXPECT_NE(sig.decl, nullptr);
    EXPECT_NE(sig.expr, nullptr);
}

TEST_F(SmallsLSP, SignatureHelpMultipleParams)
{
    auto script = make_script(R"(fn create_rect(x: int, y: int, width: int, height: int): int {
    return x + y + width + height;
}

fn main() {
    var rect = create_rect(10, 20, 100, 50);
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    // Get signature help at different positions in the call
    auto sig1 = script.signature_help(6, 27); // After first param
    EXPECT_NE(sig1.decl, nullptr);
    EXPECT_EQ(sig1.active_param, 0);

    auto sig2 = script.signature_help(6, 31); // After second param
    EXPECT_NE(sig2.decl, nullptr);
    EXPECT_EQ(sig2.active_param, 1);
}

TEST_F(SmallsLSP, SignatureHelpNoParams)
{
    auto script = make_script(R"(fn get_value(): int {
    return 42;
}

fn main() {
    var result = get_value();
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto sig = script.signature_help(6, 27);
    EXPECT_NE(sig.decl, nullptr);
    EXPECT_EQ(sig.active_param, 0);
}

TEST_F(SmallsLSP, SignatureHelpTupleReturn)
{
    auto script = make_script(R"(fn swap(x: int, y: int): (int, int) {
    return y, x;
}

fn test() {
    var a, b = swap(1, 2);
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    // Get signature help while inside the swap call
    auto sig = script.signature_help(6, 20);
    EXPECT_NE(sig.decl, nullptr);
    EXPECT_NE(sig.expr, nullptr);
}

// ============================================================================
// Completions - Simulating real typing scenarios
// ============================================================================

TEST_F(SmallsLSP, CompletionTypingFunctionName)
{
    auto script = make_script(R"(fn helper(): int {
    return 42;
}

fn helper_two(): int {
    return 24;
}

fn main() {
    var result = hel
})"sv);

    EXPECT_NO_THROW(script.parse());
    // Note: This will have parse errors because "hel" is incomplete, but that's expected
    EXPECT_NO_THROW(script.resolve());
    // Simulate typing "hel" and requesting completions
    // Position after "hel" on line 10
    nw::smalls::CompletionContext completions;
    script.complete_at("hel", 10, 17, completions, false);
    EXPECT_EQ(completions.completions.size(), 2);

    // Should suggest "helper" and "helper_two"
    bool has_helper = false, has_helper_two = false;
    for (const auto& comp : completions.completions) {
        if (comp.decl && comp.decl->identifier() == "helper") has_helper = true;
        if (comp.decl && comp.decl->identifier() == "helper_two") has_helper_two = true;
    }
    EXPECT_TRUE(has_helper);
    EXPECT_TRUE(has_helper_two);
}

TEST_F(SmallsLSP, CompletionTypingVariableName)
{
    auto script = make_script(R"(var apple: int = 1;
var apricot: int = 2;
var banana: int = 3;

fn main() {
    var x = ap
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_NO_THROW(script.resolve());

    // Simulate typing "ap" and requesting completions
    nw::smalls::CompletionContext completions;
    script.complete_at("ap", 6, 12, completions, false);

    // Should suggest "apple" and "apricot", but not "banana"
    bool has_apple = false, has_apricot = false, has_banana = false;
    for (const auto& comp : completions.completions) {
        if (comp.decl && comp.decl->identifier() == "apple") has_apple = true;
        if (comp.decl && comp.decl->identifier() == "apricot") has_apricot = true;
        if (comp.decl && comp.decl->identifier() == "banana") has_banana = true;
    }
    EXPECT_TRUE(has_apple);
    EXPECT_TRUE(has_apricot);
    EXPECT_FALSE(has_banana);
}

TEST_F(SmallsLSP, CompletionTypingTypeName)
{
    auto script = make_script(R"(type Point {
    x: int;
    y: int;
};

type Position {
    x: int;
    y: int;
};

fn test() {
    var p: Po
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_NO_THROW(script.resolve());

    // Simulate typing "Po" for a type name
    nw::smalls::CompletionContext completions;
    script.complete_at("Po", 12, 11, completions, false);

    // Should suggest "Point" and "Position"
    bool has_point = false, has_position = false;
    for (const auto& comp : completions.completions) {
        if (comp.decl && comp.decl->identifier() == "Point") has_point = true;
        if (comp.decl && comp.decl->identifier() == "Position") has_position = true;
    }
    EXPECT_TRUE(has_point);
    EXPECT_TRUE(has_position);
}

TEST_F(SmallsLSP, CompletionEmptyPrefix)
{
    auto script = make_script(R"(var global_var: int = 10;
const PI = 3.14;

fn helper(): int {
    return 42;
}

fn main() {
    var x =
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_NO_THROW(script.resolve());

    nw::smalls::CompletionContext completions;
    script.complete_at("", 10, 12, completions, true);
    EXPECT_GT(completions.completions.size(), 0);

    bool has_global = false, has_helper = false, has_pi = false;
    for (const auto& comp : completions.completions) {
        if (comp.decl && comp.decl->identifier() == "global_var") has_global = true;
        if (comp.decl && comp.decl->identifier() == "helper") has_helper = true;
        if (comp.decl && comp.decl->identifier() == "PI") has_pi = true;
    }
    EXPECT_TRUE(has_global);
    EXPECT_TRUE(has_helper);
    EXPECT_TRUE(has_pi);
}

TEST_F(SmallsLSP, CompletionDotExpressionTyping)
{
    auto script = make_script(R"(type Point {
    x: int;
    y: int;
    x_offset: int;
};

fn test() {
    var p: Point;
    var val = p.x
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_NO_THROW(script.resolve());

    nw::Vector<nw::smalls::Symbol> completions;
    script.complete_dot("p", 9, 16, completions, false);

    bool has_x = false, has_x_offset = false, has_y = false;
    for (const auto& comp : completions) {
        if (comp.decl && comp.decl->identifier() == "x") has_x = true;
        if (comp.decl && comp.decl->identifier() == "x_offset") has_x_offset = true;
        if (comp.decl && comp.decl->identifier() == "y") has_y = true;
    }
    EXPECT_TRUE(has_x);
    EXPECT_TRUE(has_x_offset);
    EXPECT_TRUE(has_y); // [TODO] fix complete dot filter?
}

TEST_F(SmallsLSP, CompletionDotExpressionEmpty)
{
    auto script = make_script(R"(type Point {
    x: int;
    y: int;
};

fn test() {
    var p: Point;
    var val = p.
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_NO_THROW(script.resolve());

    // Simulate just typing the dot: p.
    nw::Vector<nw::smalls::Symbol> completions;
    script.complete_dot("p", 8, 16, completions, true);

    // Should get all fields: x and y
    EXPECT_EQ(completions.size(), 2);

    bool has_x = false, has_y = false;
    for (const auto& comp : completions) {
        if (comp.decl && comp.decl->identifier() == "x") has_x = true;
        if (comp.decl && comp.decl->identifier() == "y") has_y = true;
    }
    EXPECT_TRUE(has_x);
    EXPECT_TRUE(has_y);
}

TEST_F(SmallsLSP, CompletionLocalScopeOnly)
{
    auto script = make_script(R"(var global_var: int = 10;

fn test() {
    var local_var: int = 20;
    var local_other: int = 30;
    var result = loc
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_NO_THROW(script.resolve());

    nw::smalls::CompletionContext completions;
    script.complete_at("loc", 6, 17, completions, false);

    bool has_local_var = false, has_local_other = false, has_global = false;
    for (const auto& comp : completions.completions) {
        if (comp.decl && comp.decl->identifier() == "local_var") has_local_var = true;
        if (comp.decl && comp.decl->identifier() == "local_other") has_local_other = true;
        if (comp.decl && comp.decl->identifier() == "global_var") has_global = true;
    }
    EXPECT_TRUE(has_local_var);
    EXPECT_TRUE(has_local_other);
    EXPECT_FALSE(has_global);
}

// ============================================================================
// Export Lookup
// ============================================================================

TEST_F(SmallsLSP, LocateExportFunction)
{
    auto script = make_script(R"(fn exported_func(): int {
    return 42;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto symbol = script.locate_export("exported_func", false);
    EXPECT_NE(symbol.decl, nullptr);
    EXPECT_EQ(symbol.kind, nw::smalls::SymbolKind::function);
}

TEST_F(SmallsLSP, LocateExportType)
{
    auto script = make_script(R"(type Point {
    x: int;
    y: int;
};)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto symbol = script.locate_export("Point", false);
    EXPECT_NE(symbol.decl, nullptr);
    EXPECT_EQ(symbol.kind, nw::smalls::SymbolKind::type);
}

TEST_F(SmallsLSP, LocateExportVariable)
{
    auto script = make_script(R"(var global_counter: int = 0;)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    // Locate variable export
    auto symbol = script.locate_export("global_counter", false);
    EXPECT_NE(symbol.decl, nullptr);
    EXPECT_EQ(symbol.kind, nw::smalls::SymbolKind::variable);
}

TEST_F(SmallsLSP, StructLiteralFieldCompletion)
{
    auto script = make_script(R"(type Point {
    x: int;
    y: int;
    z: int;
};

const origin = Point { x = 0, y = 0,  };)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    // Complete at the end of the struct literal (after "y = 0, ")
    nw::smalls::CompletionContext completions;
    script.complete_at("z", 7, 37, completions);

    // Should suggest "z" field
    EXPECT_GT(completions.completions.size(), 0);
    bool found_z = false;
    for (const auto& sym : completions.completions) {
        if (sym.decl->identifier() == "z") {
            found_z = true;
            EXPECT_EQ(sym.kind, nw::smalls::SymbolKind::field);
            break;
        }
    }
    EXPECT_TRUE(found_z);
}

TEST_F(SmallsLSP, StructLiteralGotoDefinitionFieldName)
{
    auto script = make_script(R"(type Point {
    x: int;
    y: int;
};

const p = Point { x = 1, y = 2 };)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto symbol = script.locate_symbol("x", 6, 19);
    EXPECT_NE(symbol.decl, nullptr);
    EXPECT_EQ(symbol.kind, nw::smalls::SymbolKind::field);
    EXPECT_EQ(symbol.view, "x");
}

TEST_F(SmallsLSP, StructLiteralGotoDefinitionTypeName)
{
    auto script = make_script(R"(type Point {
    x: int;
    y: int;
};

const p = Point { x = 1, y = 2 };)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    // Go to definition on "Point" type name in struct literal
    // Line 6 (1-indexed), column 11 (0-indexed) - on "Point"
    auto symbol = script.locate_symbol("Point", 6, 11);

    EXPECT_NE(symbol.decl, nullptr);
    EXPECT_EQ(symbol.kind, nw::smalls::SymbolKind::type);
}

// ============================================================================
// Module System - Imports
// ============================================================================

TEST_F(SmallsLSP, LocateAliasedImport)
{
    auto& runtime = nw::kernel::runtime();

    // Create a module to import
    auto* base_module = runtime.load_module_from_source("test/lsp/math", R"(
type Vector {
    x, y, z: float;
};

fn length(v: Vector): float {
    return 0.0;
}
)");
    ASSERT_NE(base_module, nullptr);
    EXPECT_EQ(base_module->errors(), 0);

    auto script = make_script(R"(import test.lsp.math as math;

fn test() {
    var v: math.Vector;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    // Click on "math" in the import statement - line 1, column 24
    auto symbol = script.locate_symbol("math", 1, 24);
    EXPECT_NE(symbol.decl, nullptr);
    EXPECT_EQ(symbol.kind, nw::smalls::SymbolKind::variable); // Imports treated as variables
}

TEST_F(SmallsLSP, LocateSelectiveImportSymbol)
{
    auto& runtime = nw::kernel::runtime();

    // Create a module to import from
    auto* base_module = runtime.load_module_from_source("test/lsp/geometry", R"(
type Point {
    x, y: float;
};

type Line {
    start, end: Point;
};
)");
    ASSERT_NE(base_module, nullptr);
    EXPECT_EQ(base_module->errors(), 0);

    auto script = make_script(R"(from test.lsp.geometry import { Point, Line };

fn test() {
    var p: Point;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    // Click on "Point" in the import statement - line 1, column 33
    auto symbol = script.locate_symbol("Point", 1, 33);
    EXPECT_NE(symbol.decl, nullptr);
    EXPECT_EQ(symbol.kind, nw::smalls::SymbolKind::type);
    EXPECT_NE(symbol.provider, nullptr);
    EXPECT_EQ(symbol.provider->name(), "test.lsp.geometry");
}

TEST_F(SmallsLSP, LocateQualifiedTypeAlias)
{
    auto& runtime = nw::kernel::runtime();

    // Create a module with a type
    auto* base_module = runtime.load_module_from_source("test/lsp/types", R"(
type Transform {
    x, y, z: float;
};
)");
    ASSERT_NE(base_module, nullptr);
    EXPECT_EQ(base_module->errors(), 0);

    auto script = make_script(R"(import test.lsp.types as types;

fn create_transform(): types.Transform {
    var t: types.Transform;
    return t;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    // Click on "types" in "types.Transform" - line 3, column 23
    auto alias_symbol = script.locate_symbol("types", 3, 23);
    EXPECT_NE(alias_symbol.decl, nullptr);
    EXPECT_EQ(alias_symbol.kind, nw::smalls::SymbolKind::variable); // Import

    // Click on "Transform" in "types.Transform" - line 3, column 29
    auto type_symbol = script.locate_symbol("Transform", 3, 29);
    EXPECT_NE(type_symbol.decl, nullptr);
    EXPECT_EQ(type_symbol.kind, nw::smalls::SymbolKind::type);
    EXPECT_NE(type_symbol.provider, nullptr);
    if (type_symbol.provider) {
        EXPECT_EQ(type_symbol.provider->name(), "test.lsp.types");
    }
}

TEST_F(SmallsLSP, LocateQualifiedTypeInVariable)
{
    auto& runtime = nw::kernel::runtime();

    // Create a module
    auto* base_module = runtime.load_module_from_source("test/lsp/entities", R"(
type Entity {
    id: int;
    name: string;
};
)");
    ASSERT_NE(base_module, nullptr);

    auto script = make_script(R"(import test.lsp.entities as ent;

fn test() {
    var entity: ent.Entity;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    // Click on "ent" in "ent.Entity" - line 4, column 16
    auto alias_symbol = script.locate_symbol("ent", 4, 16);
    EXPECT_NE(alias_symbol.decl, nullptr);
    EXPECT_EQ(alias_symbol.kind, nw::smalls::SymbolKind::variable);

    // Click on "Entity" in "ent.Entity" - line 4, column 20
    auto type_symbol = script.locate_symbol("Entity", 4, 20);
    EXPECT_NE(type_symbol.decl, nullptr);
    EXPECT_EQ(type_symbol.kind, nw::smalls::SymbolKind::type);
    EXPECT_EQ(type_symbol.provider->name(), "test.lsp.entities");
}

TEST_F(SmallsLSP, CompletionModuleDot)
{
    auto& runtime = nw::kernel::runtime();

    // Create a module with multiple exports
    auto* base_module = runtime.load_module_from_source("test/lsp/utils", R"(
type Vec2 {
    x, y: float;
};

type Vec3 {
    x, y, z: float;
};

fn add(a: Vec2, b: Vec2): Vec2 {
    return a;
}

fn subtract(a: Vec2, b: Vec2): Vec2 {
    return a;
}
)");
    ASSERT_NE(base_module, nullptr);
    EXPECT_EQ(base_module->errors(), 0);

    auto script = make_script(R"(import test.lsp.utils as utils;

fn test() {
    var v: utils.
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_NO_THROW(script.resolve());

    // Get completions after "utils."
    nw::Vector<nw::smalls::Symbol> completions;
    script.complete_dot("utils", 4, 16, completions, true);

    // Should have all 4 exports: Vec2, Vec3, add, subtract
    EXPECT_EQ(completions.size(), 4);

    bool has_vec2 = false, has_vec3 = false, has_add = false, has_subtract = false;
    for (const auto& comp : completions) {
        if (comp.decl->identifier() == "Vec2") has_vec2 = true;
        if (comp.decl->identifier() == "Vec3") has_vec3 = true;
        if (comp.decl->identifier() == "add") has_add = true;
        if (comp.decl->identifier() == "subtract") has_subtract = true;
    }

    EXPECT_TRUE(has_vec2);
    EXPECT_TRUE(has_vec3);
    EXPECT_TRUE(has_add);
    EXPECT_TRUE(has_subtract);
}

TEST_F(SmallsLSP, CompletionSelectiveImport)
{
    auto& runtime = nw::kernel::runtime();

    // Create a module
    auto* base_module = runtime.load_module_from_source("test/lsp/colors", R"(
type Color {
    r, g, b: int;
};

const RED = Color { r = 255, g = 0, b = 0 };
const GREEN = Color { r = 0, g = 255, b = 0 };
)");
    ASSERT_NE(base_module, nullptr);

    auto script = make_script(R"(from test.lsp.colors import { Color, RED };

fn test() {
    var c: Col
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_NO_THROW(script.resolve());

    // Complete "Col" - should suggest Color
    nw::smalls::CompletionContext completions;
    script.complete_at("Col", 4, 11, completions, false);

    bool has_color = false;
    for (const auto& comp : completions.completions) {
        if (comp.decl && comp.decl->identifier() == "Color") {
            has_color = true;
        }
    }
    EXPECT_TRUE(has_color);
}
