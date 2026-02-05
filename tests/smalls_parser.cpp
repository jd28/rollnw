#include "smalls_fixtures.hpp"

#include <nw/log.hpp>
#include <nw/smalls/AstPrinter.hpp>
#include <nw/smalls/Context.hpp>
#include <nw/smalls/Parser.hpp>
#include <nw/smalls/Smalls.hpp>

#include <string_view>

using namespace std::literals;

TEST_F(SmallsParser, MissingSemicolonError)
{
    auto script = make_script(R"(
        type Point {
            x: float
        };
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_GT(script.errors(), 0); // Should have at least one error for missing semicolon
}
TEST_F(SmallsParser, SimpleAnnotation)
{
    auto script = make_script(R"(
        [[replicated]]
        type Player {
            hp: int;
        };
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* type_decl = dynamic_cast<nw::smalls::StructDecl*>(decls[0]);
    ASSERT_NE(type_decl, nullptr);
    ASSERT_EQ(type_decl->annotations_.size(), 1);
    EXPECT_EQ(type_decl->annotations_[0].name.loc.view(), "replicated");
    EXPECT_EQ(type_decl->annotations_[0].args.size(), 0);
}
TEST_F(SmallsParser, AnnotationWithPositionalArgs)
{
    auto script = make_script(R"(
        [[deprecated("use v2 instead", "2024-01-01")]]
        fn old_func() {}
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->annotations_.size(), 1);
    ASSERT_EQ(func->annotations_[0].args.size(), 2);
    EXPECT_FALSE(func->annotations_[0].args[0].is_named);
    EXPECT_FALSE(func->annotations_[0].args[1].is_named);
}
TEST_F(SmallsParser, AnnotationWithNamedArgs)
{
    auto script = make_script(R"(
        type Stats {
            [[editor(min=1, max=100)]]
            damage: int;
        };
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* struct_decl = dynamic_cast<nw::smalls::StructDecl*>(decls[0]);
    ASSERT_NE(struct_decl, nullptr);
    auto* field = dynamic_cast<nw::smalls::VarDecl*>(struct_decl->decls[0]);
    ASSERT_NE(field, nullptr);
    ASSERT_EQ(field->annotations_.size(), 1);
    ASSERT_EQ(field->annotations_[0].args.size(), 2);
    EXPECT_TRUE(field->annotations_[0].args[0].is_named);
    EXPECT_EQ(field->annotations_[0].args[0].name.loc.view(), "min");
    EXPECT_TRUE(field->annotations_[0].args[1].is_named);
    EXPECT_EQ(field->annotations_[0].args[1].name.loc.view(), "max");
}
