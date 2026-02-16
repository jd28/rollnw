#include "smalls_fixtures.hpp"

#include "nw/smalls/runtime.hpp"
#include <nw/log.hpp>
#include <nw/smalls/AstPrinter.hpp>
#include <nw/smalls/Context.hpp>
#include <nw/smalls/Parser.hpp>
#include <nw/smalls/Smalls.hpp>

#include <string_view>

using namespace std::literals;

TEST_F(SmallsParser, IfStatement)
{
    auto script = make_script(R"(
        fn test() {
            if (x > 0) {
                return x;
            }
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_NE(func->block, nullptr);
    ASSERT_EQ(func->block->nodes.size(), 1);

    auto* if_stmt = dynamic_cast<nw::smalls::IfStatement*>(func->block->nodes[0]);
    ASSERT_NE(if_stmt, nullptr);
    EXPECT_NE(if_stmt->expr, nullptr);
    EXPECT_NE(if_stmt->if_branch, nullptr);
    EXPECT_EQ(if_stmt->else_branch, nullptr);
}

TEST_F(SmallsParser, IfElseStatement)
{
    auto script = make_script(R"(
        fn test() {
            if (x > 0) {
                return 1;
            } else {
                return -1;
            }
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);

    auto* if_stmt = dynamic_cast<nw::smalls::IfStatement*>(func->block->nodes[0]);
    ASSERT_NE(if_stmt, nullptr);
    EXPECT_NE(if_stmt->expr, nullptr);
    EXPECT_NE(if_stmt->if_branch, nullptr);
    EXPECT_NE(if_stmt->else_branch, nullptr);
}

TEST_F(SmallsParser, IfElifElseStatement)
{
    auto script = make_script(R"(
        fn test() {
            if (x > 10) {
                return 1;
            } elif (x > 5) {
                return 2;
            } else {
                return 3;
            }
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);

    auto* if_stmt = dynamic_cast<nw::smalls::IfStatement*>(func->block->nodes[0]);
    ASSERT_NE(if_stmt, nullptr);
    ASSERT_NE(if_stmt->else_branch, nullptr);
    ASSERT_EQ(if_stmt->else_branch->nodes.size(), 1);

    auto* nested_elif = dynamic_cast<nw::smalls::IfStatement*>(if_stmt->else_branch->nodes[0]);
    ASSERT_NE(nested_elif, nullptr);
    EXPECT_NE(nested_elif->expr, nullptr);
    EXPECT_NE(nested_elif->if_branch, nullptr);
    EXPECT_NE(nested_elif->else_branch, nullptr);
}
TEST_F(SmallsParser, ForLoop)
{
    auto script = make_script(R"(
        fn test() {
            for (var i = 0; i < 10; i = i + 1) {
                return i;
            }
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->block->nodes.size(), 1);

    auto* for_stmt = dynamic_cast<nw::smalls::ForStatement*>(func->block->nodes[0]);
    ASSERT_NE(for_stmt, nullptr);
    EXPECT_NE(for_stmt->init, nullptr);
    EXPECT_NE(for_stmt->check, nullptr);
    EXPECT_NE(for_stmt->inc, nullptr);
    EXPECT_NE(for_stmt->block, nullptr);
}
TEST_F(SmallsParser, ForLoopInfinite)
{
    auto script = make_script(R"(
        fn test() {
            for {
                break;
            }
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->block->nodes.size(), 1);

    auto* for_stmt = dynamic_cast<nw::smalls::ForStatement*>(func->block->nodes[0]);
    ASSERT_NE(for_stmt, nullptr);
    EXPECT_EQ(for_stmt->init, nullptr);
    EXPECT_EQ(for_stmt->check, nullptr);
    EXPECT_EQ(for_stmt->inc, nullptr);
    EXPECT_NE(for_stmt->block, nullptr);
}
TEST_F(SmallsParser, ForLoopWhileStyle)
{
    auto script = make_script(R"(
        fn test() {
            var i = 0;
            for (i < 10) {
                i = i + 1;
            }
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->block->nodes.size(), 2);

    auto* for_stmt = dynamic_cast<nw::smalls::ForStatement*>(func->block->nodes[1]);
    ASSERT_NE(for_stmt, nullptr);
    EXPECT_EQ(for_stmt->init, nullptr);
    EXPECT_NE(for_stmt->check, nullptr);
    EXPECT_EQ(for_stmt->inc, nullptr);
    EXPECT_NE(for_stmt->block, nullptr);
}

TEST_F(SmallsParser, ForLoopCStyleEmptyClauses)
{
    auto script = make_script(R"(
        fn test() {
            for (;;) {
                break;
            }
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->block->nodes.size(), 1);

    auto* for_stmt = dynamic_cast<nw::smalls::ForStatement*>(func->block->nodes[0]);
    ASSERT_NE(for_stmt, nullptr);
    EXPECT_EQ(for_stmt->init, nullptr);
    EXPECT_EQ(for_stmt->check, nullptr);
    EXPECT_EQ(for_stmt->inc, nullptr);
    EXPECT_NE(for_stmt->block, nullptr);
}

TEST_F(SmallsParser, ForEachSingleVar)
{
    auto script = make_script(R"(
        fn test() {
            for (var x in xs) {
            }
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->block->nodes.size(), 1);

    auto* foreach_stmt = dynamic_cast<nw::smalls::ForEachStatement*>(func->block->nodes[0]);
    ASSERT_NE(foreach_stmt, nullptr);
    EXPECT_NE(foreach_stmt->var, nullptr);
    EXPECT_EQ(foreach_stmt->key_var, nullptr);
    EXPECT_EQ(foreach_stmt->value_var, nullptr);
    EXPECT_NE(foreach_stmt->collection, nullptr);
    EXPECT_NE(foreach_stmt->block, nullptr);
}

TEST_F(SmallsParser, ForEachPairVars)
{
    auto script = make_script(R"(
        fn test() {
            for (var k, v in pairs) {
            }
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->block->nodes.size(), 1);

    auto* foreach_stmt = dynamic_cast<nw::smalls::ForEachStatement*>(func->block->nodes[0]);
    ASSERT_NE(foreach_stmt, nullptr);
    EXPECT_EQ(foreach_stmt->var, nullptr);
    EXPECT_NE(foreach_stmt->key_var, nullptr);
    EXPECT_NE(foreach_stmt->value_var, nullptr);
    EXPECT_NE(foreach_stmt->collection, nullptr);
    EXPECT_NE(foreach_stmt->block, nullptr);
}

TEST_F(SmallsParser, ForEachTypedVars)
{
    auto script = make_script(R"(
        fn test() {
            for (var x: int in xs) {
            }
            for (var k: string, v: int in pairs) {
            }
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->block->nodes.size(), 2);

    auto* foreach_single = dynamic_cast<nw::smalls::ForEachStatement*>(func->block->nodes[0]);
    ASSERT_NE(foreach_single, nullptr);
    ASSERT_NE(foreach_single->var, nullptr);
    EXPECT_NE(foreach_single->var->type, nullptr);

    auto* foreach_pair = dynamic_cast<nw::smalls::ForEachStatement*>(func->block->nodes[1]);
    ASSERT_NE(foreach_pair, nullptr);
    ASSERT_NE(foreach_pair->key_var, nullptr);
    ASSERT_NE(foreach_pair->value_var, nullptr);
    EXPECT_NE(foreach_pair->key_var->type, nullptr);
    EXPECT_NE(foreach_pair->value_var->type, nullptr);
}
TEST_F(SmallsParser, SwitchStatement)
{
    auto script = make_script(R"(
        fn test() {
            switch (x) {
                case 1:
                    return "one";
                case 2:
                    return "two";
                default:
                    return "other";
            }
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->block->nodes.size(), 1);

    auto* switch_stmt = dynamic_cast<nw::smalls::SwitchStatement*>(func->block->nodes[0]);
    ASSERT_NE(switch_stmt, nullptr);
    EXPECT_NE(switch_stmt->target, nullptr);
    EXPECT_NE(switch_stmt->block, nullptr);
}
TEST_F(SmallsParser, BreakStatement)
{
    auto script = make_script(R"(
        fn test() {
            for (var i = 0; i < 10; i = i + 1) {
                break;
            }
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);

    auto* for_stmt = dynamic_cast<nw::smalls::ForStatement*>(func->block->nodes[0]);
    ASSERT_NE(for_stmt, nullptr);
    ASSERT_EQ(for_stmt->block->nodes.size(), 1);

    auto* break_stmt = dynamic_cast<nw::smalls::JumpStatement*>(for_stmt->block->nodes[0]);
    ASSERT_NE(break_stmt, nullptr);
    EXPECT_EQ(break_stmt->op.type, nw::smalls::TokenType::BREAK);
}
TEST_F(SmallsParser, ContinueStatement)
{
    auto script = make_script(R"(
        fn test() {
            for (var i = 0; i < 10; i = i + 1) {
                continue;
            }
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);

    auto* for_stmt = dynamic_cast<nw::smalls::ForStatement*>(func->block->nodes[0]);
    ASSERT_NE(for_stmt, nullptr);
    ASSERT_EQ(for_stmt->block->nodes.size(), 1);

    auto* continue_stmt = dynamic_cast<nw::smalls::JumpStatement*>(for_stmt->block->nodes[0]);
    ASSERT_NE(continue_stmt, nullptr);
    EXPECT_EQ(continue_stmt->op.type, nw::smalls::TokenType::CONTINUE);
}
TEST_F(SmallsParser, ReturnStatement)
{
    auto script = make_script(R"(
        fn test(): int {
            return 42;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->block->nodes.size(), 1);

    auto* return_stmt = dynamic_cast<nw::smalls::JumpStatement*>(func->block->nodes[0]);
    ASSERT_NE(return_stmt, nullptr);
    EXPECT_EQ(return_stmt->op.type, nw::smalls::TokenType::RETURN);
    ASSERT_EQ(return_stmt->exprs.size(), 1);
}

TEST_F(SmallsParser, PatternMatchingUnitVariant)
{
    auto script = make_script(R"(
        fn test(result: Result): int {
            switch (result) {
                case Ok:
                    return 1;
                case Err:
                    return 0;
            }
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->block->nodes.size(), 1);

    auto* switch_stmt = dynamic_cast<nw::smalls::SwitchStatement*>(func->block->nodes[0]);
    ASSERT_NE(switch_stmt, nullptr);
    ASSERT_NE(switch_stmt->block, nullptr);
    auto& cases = switch_stmt->block->nodes;
    ASSERT_GE(cases.size(), 2);

    auto* case_ok = dynamic_cast<nw::smalls::LabelStatement*>(cases[0]);
    ASSERT_NE(case_ok, nullptr);
    EXPECT_TRUE(case_ok->is_pattern_match);
    EXPECT_EQ(case_ok->bindings.size(), 0);
    EXPECT_EQ(case_ok->guard, nullptr);

    auto* case_err = dynamic_cast<nw::smalls::LabelStatement*>(cases[2]);
    ASSERT_NE(case_err, nullptr);
    EXPECT_TRUE(case_err->is_pattern_match);
    EXPECT_EQ(case_err->bindings.size(), 0);
}

TEST_F(SmallsParser, PatternMatchingWithBindings)
{
    auto script = make_script(R"(
        fn test(result: Result): int {
            switch (result) {
                case Ok(value):
                    return value;
                case Err(msg):
                    return 0;
            }
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);

    auto* switch_stmt = dynamic_cast<nw::smalls::SwitchStatement*>(func->block->nodes[0]);
    ASSERT_NE(switch_stmt, nullptr);
    ASSERT_NE(switch_stmt->block, nullptr);
    auto& cases = switch_stmt->block->nodes;
    auto* case_ok = dynamic_cast<nw::smalls::LabelStatement*>(cases[0]);
    ASSERT_NE(case_ok, nullptr);
    EXPECT_TRUE(case_ok->is_pattern_match);
    ASSERT_EQ(case_ok->bindings.size(), 1);
    EXPECT_EQ(case_ok->bindings[0]->identifier(), "value");

    auto* case_err = dynamic_cast<nw::smalls::LabelStatement*>(cases[2]);
    ASSERT_NE(case_err, nullptr);
    EXPECT_TRUE(case_err->is_pattern_match);
    ASSERT_EQ(case_err->bindings.size(), 1);
    EXPECT_EQ(case_err->bindings[0]->identifier(), "msg");
}

TEST_F(SmallsParser, PatternMatchingWithGuard)
{
    auto script = make_script(R"(
        fn test(result: Result): int {
            switch (result) {
                case Ok(x) if x > 0:
                    return x;
                default:
                    return 0;
            }
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);

    auto* switch_stmt = dynamic_cast<nw::smalls::SwitchStatement*>(func->block->nodes[0]);
    ASSERT_NE(switch_stmt, nullptr);
    ASSERT_NE(switch_stmt->block, nullptr);
    auto& cases = switch_stmt->block->nodes;
    auto* case_ok = dynamic_cast<nw::smalls::LabelStatement*>(cases[0]);
    ASSERT_NE(case_ok, nullptr);
    EXPECT_TRUE(case_ok->is_pattern_match);
    ASSERT_EQ(case_ok->bindings.size(), 1);
    ASSERT_NE(case_ok->guard, nullptr);
}

TEST_F(SmallsParser, PatternMatchingMultipleBindings)
{
    auto script = make_script(R"(
        fn test(pair: Pair): int {
            switch (pair) {
                case Both(x, y):
                    return x + y;
                default:
                    return 0;
            }
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);

    auto* switch_stmt = dynamic_cast<nw::smalls::SwitchStatement*>(func->block->nodes[0]);
    ASSERT_NE(switch_stmt, nullptr);
    ASSERT_NE(switch_stmt->block, nullptr);
    auto& cases = switch_stmt->block->nodes;
    auto* case_both = dynamic_cast<nw::smalls::LabelStatement*>(cases[0]);
    ASSERT_NE(case_both, nullptr);
    EXPECT_TRUE(case_both->is_pattern_match);
    ASSERT_EQ(case_both->bindings.size(), 2);
    EXPECT_EQ(case_both->bindings[0]->identifier(), "x");
    EXPECT_EQ(case_both->bindings[1]->identifier(), "y");
}

TEST_F(SmallsParser, PatternMatchingQualifiedUnitVariant)
{
    auto script = make_script(R"(
        fn test(value: int): int {
            switch (value) {
                case Color.Red:
                    return 1;
                default:
                    return 0;
            }
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);

    auto* switch_stmt = dynamic_cast<nw::smalls::SwitchStatement*>(func->block->nodes[0]);
    ASSERT_NE(switch_stmt, nullptr);
    auto* case_stmt = dynamic_cast<nw::smalls::LabelStatement*>(switch_stmt->block->nodes[0]);
    ASSERT_NE(case_stmt, nullptr);
    EXPECT_TRUE(case_stmt->is_pattern_match);
    auto* path = dynamic_cast<nw::smalls::PathExpression*>(case_stmt->expr);
    ASSERT_NE(path, nullptr);
    EXPECT_EQ(path->parts.size(), 2);
}

TEST_F(SmallsParser, PatternMatchingQualifiedPayloadVariant)
{
    auto script = make_script(R"(
        fn test(value: int): int {
            switch (value) {
                case Result.Ok(x):
                    return x;
                default:
                    return 0;
            }
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);

    auto* switch_stmt = dynamic_cast<nw::smalls::SwitchStatement*>(func->block->nodes[0]);
    ASSERT_NE(switch_stmt, nullptr);
    auto* case_stmt = dynamic_cast<nw::smalls::LabelStatement*>(switch_stmt->block->nodes[0]);
    ASSERT_NE(case_stmt, nullptr);
    EXPECT_TRUE(case_stmt->is_pattern_match);
    ASSERT_EQ(case_stmt->bindings.size(), 1);
    EXPECT_EQ(case_stmt->bindings[0]->identifier(), "x");
}

TEST_F(SmallsParser, PatternCaseFallsBackToExpression)
{
    auto script = make_script(R"(
        fn test(value: int): int {
            switch (value) {
                case a + b:
                    return 1;
                default:
                    return 0;
            }
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);

    auto* switch_stmt = dynamic_cast<nw::smalls::SwitchStatement*>(func->block->nodes[0]);
    ASSERT_NE(switch_stmt, nullptr);
    auto* case_stmt = dynamic_cast<nw::smalls::LabelStatement*>(switch_stmt->block->nodes[0]);
    ASSERT_NE(case_stmt, nullptr);
    EXPECT_FALSE(case_stmt->is_pattern_match);
    auto* bin = dynamic_cast<nw::smalls::BinaryExpression*>(case_stmt->expr);
    ASSERT_NE(bin, nullptr);
}

TEST_F(SmallsParser, TupleAssignmentStatementDetection)
{
    auto script = make_script(R"(
        fn pair(): (int, int) {
            return 1, 2;
        }

        fn test() {
            var a: int;
            var b: int;
            a, b = pair();
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[1]);
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->block->nodes.size(), 3);

    auto* stmt = dynamic_cast<nw::smalls::ExprStatement*>(func->block->nodes[2]);
    ASSERT_NE(stmt, nullptr);

    auto* assign = dynamic_cast<nw::smalls::AssignExpression*>(stmt->expr);
    ASSERT_NE(assign, nullptr);

    auto* lhs_tuple = dynamic_cast<nw::smalls::TupleLiteral*>(assign->lhs);
    ASSERT_NE(lhs_tuple, nullptr);
    EXPECT_EQ(lhs_tuple->elements.size(), 2);
}

// ============================================================================
// Expression Tests
// ============================================================================
