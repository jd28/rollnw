#include "smalls_fixtures.hpp"

#include <nw/log.hpp>
#include <nw/smalls/AstPrinter.hpp>
#include <nw/smalls/Context.hpp>
#include <nw/smalls/Parser.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/runtime.hpp>

#include <string_view>

using namespace std::literals;

TEST_F(SmallsParser, ImportAliased)
{
    auto script = make_script("import core.math.vector as vec;"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& imports = script.ast().imports;
    ASSERT_EQ(imports.size(), 1);

    auto* aliased = dynamic_cast<nw::smalls::AliasedImportDecl*>(imports[0]);
    ASSERT_NE(aliased, nullptr);
    EXPECT_EQ(aliased->module_path, "core.math.vector");
    EXPECT_EQ(aliased->alias.loc.view(), "vec");
}
TEST_F(SmallsParser, ImportSelective)
{
    auto script = make_script("from core.math.vector import { Vector, Point };"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& imports = script.ast().imports;
    ASSERT_EQ(imports.size(), 1);

    auto* selective = dynamic_cast<nw::smalls::SelectiveImportDecl*>(imports[0]);
    ASSERT_NE(selective, nullptr);
    EXPECT_EQ(selective->module_path, "core.math.vector");
    ASSERT_EQ(selective->imported_symbols.size(), 2);
    EXPECT_EQ(selective->imported_symbols[0].loc.view(), "Vector");
    EXPECT_EQ(selective->imported_symbols[1].loc.view(), "Point");
}
TEST_F(SmallsParser, ImportSelectiveSingle)
{
    auto script = make_script("from core.creature import { Creature };"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& imports = script.ast().imports;
    ASSERT_EQ(imports.size(), 1);

    auto* selective = dynamic_cast<nw::smalls::SelectiveImportDecl*>(imports[0]);
    ASSERT_NE(selective, nullptr);
    EXPECT_EQ(selective->module_path, "core.creature");
    ASSERT_EQ(selective->imported_symbols.size(), 1);
    EXPECT_EQ(selective->imported_symbols[0].loc.view(), "Creature");
}
TEST_F(SmallsParser, ImportSelectiveTrailingComma)
{
    auto script = make_script("from core.math import { add, subtract, };"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& imports = script.ast().imports;
    ASSERT_EQ(imports.size(), 1);

    auto* selective = dynamic_cast<nw::smalls::SelectiveImportDecl*>(imports[0]);
    ASSERT_NE(selective, nullptr);
    EXPECT_EQ(selective->module_path, "core.math");
    ASSERT_EQ(selective->imported_symbols.size(), 2);
    EXPECT_EQ(selective->imported_symbols[0].loc.view(), "add");
    EXPECT_EQ(selective->imported_symbols[1].loc.view(), "subtract");
}
TEST_F(SmallsParser, ImportMultiple)
{
    auto script = make_script(R"(
        import core.math.vector as vec;
        import core.creature as creature;
        from core.combat import { Attack, Damage };
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& imports = script.ast().imports;
    ASSERT_EQ(imports.size(), 3);

    auto* aliased1 = dynamic_cast<nw::smalls::AliasedImportDecl*>(imports[0]);
    ASSERT_NE(aliased1, nullptr);
    EXPECT_EQ(aliased1->module_path, "core.math.vector");
    EXPECT_EQ(aliased1->alias.loc.view(), "vec");

    auto* aliased2 = dynamic_cast<nw::smalls::AliasedImportDecl*>(imports[1]);
    ASSERT_NE(aliased2, nullptr);
    EXPECT_EQ(aliased2->module_path, "core.creature");
    EXPECT_EQ(aliased2->alias.loc.view(), "creature");

    auto* selective = dynamic_cast<nw::smalls::SelectiveImportDecl*>(imports[2]);
    ASSERT_NE(selective, nullptr);
    EXPECT_EQ(selective->module_path, "core.combat");
}
TEST_F(SmallsParser, ImportMixedWithDeclarations)
{
    auto script = make_script(R"(
        import core.math as math;

        type Point { x, y: float; };

        from core.utils import { clamp };

        fn distance(p1: Point, p2: Point): float {
            return 0.0;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& imports = script.ast().imports;
    ASSERT_EQ(imports.size(), 2);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 4); // 2 imports + Point + distance function
}
TEST_F(SmallsParser, ImportScoped)
{
    auto script = make_script(R"(
        fn calculate(): float {
            import core.math as math;
            from core.utils import { clamp };

            return 0.0;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->identifier_.loc.view(), "calculate");

    auto* block = func->block;
    ASSERT_NE(block, nullptr);
    ASSERT_EQ(block->nodes.size(), 3); // 2 imports + return statement

    auto* aliased = dynamic_cast<nw::smalls::AliasedImportDecl*>(block->nodes[0]);
    ASSERT_NE(aliased, nullptr);
    EXPECT_EQ(aliased->module_path, "core.math");
    EXPECT_EQ(aliased->alias.loc.view(), "math");

    auto* selective = dynamic_cast<nw::smalls::SelectiveImportDecl*>(block->nodes[1]);
    ASSERT_NE(selective, nullptr);
    EXPECT_EQ(selective->module_path, "core.utils");
    ASSERT_EQ(selective->imported_symbols.size(), 1);
    EXPECT_EQ(selective->imported_symbols[0].loc.view(), "clamp");
}

// ============================================================================
// Module System Runtime Tests
// ============================================================================
TEST_F(SmallsParser, ModuleLoadingBasics)
{
    auto& runtime = nw::kernel::runtime();

    // Test: Load a standalone module (use slash-separated paths)
    auto* standalone = runtime.load_module_from_source("test/runtime/standalone", R"(
        type Point { x, y: float; };
    )");
    ASSERT_NE(standalone, nullptr);
    EXPECT_EQ(standalone->errors(), 0);

    // Test: Load module with import chain
    auto* base = runtime.load_module_from_source("test/runtime/base", R"(
        type Vector { x, y, z: float; };
    )");
    ASSERT_NE(base, nullptr);

    auto* consumer = runtime.load_module_from_source("test/runtime/consumer", R"(
        import test.runtime.base as base;
        type Transform { position: base.Vector; };
    )");
    ASSERT_NE(consumer, nullptr);
    EXPECT_EQ(consumer->errors(), 0);

    // Test: Verify module caching works
    auto* cached = runtime.get_module("test/runtime/standalone");
    EXPECT_EQ(cached, standalone);
}

// NOTE: Circular dependency detection is implemented in Runtime::load_module()
// and Runtime::load_module_from_source() via the loading_stack_ mechanism.
// Full testing would require a mock resource manager or test .smalls files.
// The detection will log an error and return nullptr when a cycle is found.
