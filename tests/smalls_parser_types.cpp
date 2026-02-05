#include "smalls_fixtures.hpp"

#include <nw/log.hpp>
#include <nw/smalls/AstPrinter.hpp>
#include <nw/smalls/Context.hpp>
#include <nw/smalls/Parser.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/runtime.hpp>

#include <string_view>

using namespace std::literals;

TEST_F(SmallsParser, TypeNameStr)
{
    auto script1 = make_script("type Foo;"sv);
    script1.parse();

    // Just test using actual parsed types instead of manually constructing
    auto script2 = make_script("var x: int;"sv);
    script2.parse();
    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(script2.ast().decls[0]);
    EXPECT_EQ(var_decl->type->str(), "int");

    // Test generic type string representation
    auto script3 = make_script("var m: map!(string, int);"sv);
    script3.parse();
    auto* var_decl2 = dynamic_cast<nw::smalls::VarDecl*>(script3.ast().decls[0]);
    EXPECT_EQ(var_decl2->type->str(), "map!(string, int)");
}

TEST_F(SmallsParser, TypeAlias)
{
    auto script = make_script("type Gold = int;"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* alias = dynamic_cast<nw::smalls::TypeAlias*>(decls[0]);
    ASSERT_NE(alias, nullptr);
    EXPECT_EQ(alias->type->str(), "Gold");
    EXPECT_EQ(alias->aliased_type->str(), "int");
}

TEST_F(SmallsParser, TypeAliasGeneric)
{
    auto script = make_script("type StringMap = map!(string, string);"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* alias = dynamic_cast<nw::smalls::TypeAlias*>(decls[0]);
    ASSERT_NE(alias, nullptr);
    EXPECT_EQ(alias->type->str(), "StringMap");
    EXPECT_EQ(alias->aliased_type->str(), "map!(string, string)");
}

TEST_F(SmallsParser, TypeAliasSizedArray)
{
    auto script = make_script("type Inventory = int[20];"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* alias = dynamic_cast<nw::smalls::TypeAlias*>(decls[0]);
    ASSERT_NE(alias, nullptr);
    EXPECT_EQ(alias->type->str(), "Inventory");
    EXPECT_EQ(alias->aliased_type->str(), "int[20]");
    EXPECT_TRUE(alias->aliased_type->is_fixed_array);

    // Verify the type parameters are parsed correctly
    ASSERT_EQ(alias->aliased_type->params.size(), 1);

    auto* size_param = dynamic_cast<nw::smalls::LiteralExpression*>(alias->aliased_type->params[0]);
    ASSERT_NE(size_param, nullptr);
    EXPECT_EQ(size_param->literal.type, nw::smalls::TokenType::INTEGER_LITERAL);

    auto* elem_type = dynamic_cast<nw::smalls::TypeExpression*>(alias->aliased_type->name);
    ASSERT_NE(elem_type, nullptr);
    auto* elem_path = dynamic_cast<nw::smalls::PathExpression*>(elem_type->name);
    ASSERT_NE(elem_path, nullptr);
    ASSERT_EQ(elem_path->parts.size(), 1);
    auto* elem_var = dynamic_cast<nw::smalls::IdentifierExpression*>(elem_path->parts[0]);
    ASSERT_NE(elem_var, nullptr);
    EXPECT_EQ(elem_var->ident.loc.view(), "int");
}

TEST_F(SmallsParser, NewtypeDecl)
{
    auto script = make_script("type Feat(int);"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* newtype = dynamic_cast<nw::smalls::NewtypeDecl*>(decls[0]);
    ASSERT_NE(newtype, nullptr);
    EXPECT_EQ(newtype->type->str(), "Feat");
    EXPECT_EQ(newtype->wrapped_type->str(), "int");
}

TEST_F(SmallsParser, OpaqueTypeDecl)
{
    auto script = make_script("type GameObject;"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* opaque = dynamic_cast<nw::smalls::OpaqueTypeDecl*>(decls[0]);
    ASSERT_NE(opaque, nullptr);
    EXPECT_EQ(opaque->type->str(), "GameObject");
}

TEST_F(SmallsParser, OpaqueTypeDeclWithAnnotation)
{
    auto script = make_script("[[native]] type Effect;"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* opaque = dynamic_cast<nw::smalls::OpaqueTypeDecl*>(decls[0]);
    ASSERT_NE(opaque, nullptr);
    EXPECT_EQ(opaque->type->str(), "Effect");
    ASSERT_EQ(opaque->annotations_.size(), 1);
    EXPECT_EQ(opaque->annotations_[0].name.loc.view(), "native");
}

TEST_F(SmallsParser, StructSimple)
{
    auto script = make_script(R"(
        type Point {
            x: float;
            y: float;
        };
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* struct_decl = dynamic_cast<nw::smalls::StructDecl*>(decls[0]);
    ASSERT_NE(struct_decl, nullptr);
    EXPECT_EQ(struct_decl->type->str(), "Point");
    ASSERT_EQ(struct_decl->decls.size(), 2);

    auto* x_decl = dynamic_cast<nw::smalls::VarDecl*>(struct_decl->decls[0]);
    ASSERT_NE(x_decl, nullptr);
    EXPECT_EQ(x_decl->identifier_.loc.view(), "x");
    EXPECT_EQ(x_decl->type->str(), "float");

    auto* y_decl = dynamic_cast<nw::smalls::VarDecl*>(struct_decl->decls[1]);
    ASSERT_NE(y_decl, nullptr);
    EXPECT_EQ(y_decl->identifier_.loc.view(), "y");
    EXPECT_EQ(y_decl->type->str(), "float");
}

TEST_F(SmallsParser, StructMultipleIdentifiers)
{
    auto script = make_script(R"(
        type Point {
            x, y, z: float;
        };
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* struct_decl = dynamic_cast<nw::smalls::StructDecl*>(decls[0]);
    ASSERT_NE(struct_decl, nullptr);
    ASSERT_EQ(struct_decl->decls.size(), 1);

    auto* decl_list = dynamic_cast<nw::smalls::DeclList*>(struct_decl->decls[0]);
    ASSERT_NE(decl_list, nullptr);
    ASSERT_EQ(decl_list->decls.size(), 3);
    EXPECT_EQ(decl_list->type->str(), "float");

    EXPECT_EQ(decl_list->decls[0]->identifier_.loc.view(), "x");
    EXPECT_EQ(decl_list->decls[1]->identifier_.loc.view(), "y");
    EXPECT_EQ(decl_list->decls[2]->identifier_.loc.view(), "z");
}

TEST_F(SmallsParser, StructWithGenerics)
{
    auto script = make_script(R"(
        type Container {
            data: map!(string, array!(int));
            count: int;
        };
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* struct_decl = dynamic_cast<nw::smalls::StructDecl*>(decls[0]);
    ASSERT_NE(struct_decl, nullptr);
    ASSERT_EQ(struct_decl->decls.size(), 2);

    auto* data_decl = dynamic_cast<nw::smalls::VarDecl*>(struct_decl->decls[0]);
    ASSERT_NE(data_decl, nullptr);
    EXPECT_EQ(data_decl->identifier_.loc.view(), "data");
    EXPECT_EQ(data_decl->type->str(), "map!(string, array!(int))");

    auto* count_decl = dynamic_cast<nw::smalls::VarDecl*>(struct_decl->decls[1]);
    ASSERT_NE(count_decl, nullptr);
    EXPECT_EQ(count_decl->identifier_.loc.view(), "count");
    EXPECT_EQ(count_decl->type->str(), "int");
}

TEST_F(SmallsParser, StructWithSizedArrays)
{
    auto script = make_script(R"(
        type Player {
            inventory: int[20];
            scores: float[5];
            name: string;
        };
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* struct_decl = dynamic_cast<nw::smalls::StructDecl*>(decls[0]);
    ASSERT_NE(struct_decl, nullptr);
    ASSERT_EQ(struct_decl->decls.size(), 3);

    auto* inventory_decl = dynamic_cast<nw::smalls::VarDecl*>(struct_decl->decls[0]);
    ASSERT_NE(inventory_decl, nullptr);
    EXPECT_EQ(inventory_decl->identifier_.loc.view(), "inventory");
    EXPECT_EQ(inventory_decl->type->str(), "int[20]");

    auto* scores_decl = dynamic_cast<nw::smalls::VarDecl*>(struct_decl->decls[1]);
    ASSERT_NE(scores_decl, nullptr);
    EXPECT_EQ(scores_decl->identifier_.loc.view(), "scores");
    EXPECT_EQ(scores_decl->type->str(), "float[5]");
}

TEST_F(SmallsParser, RecursiveGenericTypes)
{
    auto script = make_script(R"(
        type Nested {
            complex: map!(string, map!(int, array!(float)));
        };
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* struct_decl = dynamic_cast<nw::smalls::StructDecl*>(decls[0]);
    ASSERT_NE(struct_decl, nullptr);

    auto* complex_decl = dynamic_cast<nw::smalls::VarDecl*>(struct_decl->decls[0]);
    ASSERT_NE(complex_decl, nullptr);
    EXPECT_EQ(complex_decl->type->str(), "map!(string, map!(int, array!(float)))");
}

TEST_F(SmallsParser, AllTypeFormsInOneScript)
{
    auto script = make_script(R"(
        type Gold = int;
        type Feat(int);
        type Point {
            x, y: float;
            name: string;
        };
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 3);

    ASSERT_NE(dynamic_cast<nw::smalls::TypeAlias*>(decls[0]), nullptr);
    ASSERT_NE(dynamic_cast<nw::smalls::NewtypeDecl*>(decls[1]), nullptr);
    ASSERT_NE(dynamic_cast<nw::smalls::StructDecl*>(decls[2]), nullptr);
}

// ============================================================================
// Qualified Type Name Tests
// ============================================================================

TEST_F(SmallsParser, QualifiedTypeName)
{
    auto script = make_script("type Foo { v: vector.Vector; };"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* struct_decl = dynamic_cast<nw::smalls::StructDecl*>(decls[0]);
    ASSERT_NE(struct_decl, nullptr);
    ASSERT_EQ(struct_decl->decls.size(), 1);

    auto* field = dynamic_cast<nw::smalls::VarDecl*>(struct_decl->decls[0]);
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->type->str(), "vector.Vector");
}

TEST_F(SmallsParser, NestedQualifiedTypeName)
{
    auto script = make_script("type Foo { p: math.vector.Point; };"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* struct_decl = dynamic_cast<nw::smalls::StructDecl*>(decls[0]);
    ASSERT_NE(struct_decl, nullptr);
    ASSERT_EQ(struct_decl->decls.size(), 1);

    auto* field = dynamic_cast<nw::smalls::VarDecl*>(struct_decl->decls[0]);
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->type->str(), "math.vector.Point");
}

TEST_F(SmallsParser, QualifiedTypeInFunctionSignature)
{
    auto script = make_script(R"(
        fn transform(v: vector.Vector): vector.Vector {
            return v;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);

    ASSERT_EQ(func->params.size(), 1);
    auto* param_path = dynamic_cast<nw::smalls::PathExpression*>(func->params[0]->type->name);
    ASSERT_NE(param_path, nullptr);
    ASSERT_EQ(param_path->parts.size(), 2);
    auto* param_lhs = dynamic_cast<nw::smalls::IdentifierExpression*>(param_path->parts[0]);
    auto* param_rhs = dynamic_cast<nw::smalls::IdentifierExpression*>(param_path->parts[1]);
    ASSERT_NE(param_lhs, nullptr);
    ASSERT_NE(param_rhs, nullptr);
    EXPECT_EQ(param_lhs->ident.loc.view(), "vector");
    EXPECT_EQ(param_rhs->ident.loc.view(), "Vector");

    ASSERT_NE(func->return_type, nullptr);
    auto* ret_path = dynamic_cast<nw::smalls::PathExpression*>(func->return_type->name);
    ASSERT_NE(ret_path, nullptr);
    ASSERT_EQ(ret_path->parts.size(), 2);
    auto* ret_lhs = dynamic_cast<nw::smalls::IdentifierExpression*>(ret_path->parts[0]);
    auto* ret_rhs = dynamic_cast<nw::smalls::IdentifierExpression*>(ret_path->parts[1]);
    ASSERT_NE(ret_lhs, nullptr);
    ASSERT_NE(ret_rhs, nullptr);
    EXPECT_EQ(ret_lhs->ident.loc.view(), "vector");
    EXPECT_EQ(ret_rhs->ident.loc.view(), "Vector");
}

TEST_F(SmallsParser, QualifiedTypeInVariableDeclaration)
{
    auto script = make_script("var v: vector.Vector;"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(decls[0]);
    ASSERT_NE(var_decl, nullptr);

    auto* path = dynamic_cast<nw::smalls::PathExpression*>(var_decl->type->name);
    ASSERT_NE(path, nullptr);
    ASSERT_EQ(path->parts.size(), 2);
    auto* lhs = dynamic_cast<nw::smalls::IdentifierExpression*>(path->parts[0]);
    auto* rhs = dynamic_cast<nw::smalls::IdentifierExpression*>(path->parts[1]);
    ASSERT_NE(lhs, nullptr);
    ASSERT_NE(rhs, nullptr);
    EXPECT_EQ(lhs->ident.loc.view(), "vector");
    EXPECT_EQ(rhs->ident.loc.view(), "Vector");
}

TEST_F(SmallsParser, QualifiedGenericType)
{
    auto script = make_script("var m: collections.Map!(string, int);"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(decls[0]);
    ASSERT_NE(var_decl, nullptr);

    auto* path = dynamic_cast<nw::smalls::PathExpression*>(var_decl->type->name);
    ASSERT_NE(path, nullptr);
    ASSERT_EQ(path->parts.size(), 2);
    auto* lhs = dynamic_cast<nw::smalls::IdentifierExpression*>(path->parts[0]);
    auto* rhs = dynamic_cast<nw::smalls::IdentifierExpression*>(path->parts[1]);
    ASSERT_NE(lhs, nullptr);
    ASSERT_NE(rhs, nullptr);
    EXPECT_EQ(lhs->ident.loc.view(), "collections");
    EXPECT_EQ(rhs->ident.loc.view(), "Map");

    // Type parameters should be TypeExpressions
    ASSERT_EQ(var_decl->type->params.size(), 2);
    auto* param0_type = dynamic_cast<nw::smalls::TypeExpression*>(var_decl->type->params[0]);
    auto* param1_type = dynamic_cast<nw::smalls::TypeExpression*>(var_decl->type->params[1]);
    ASSERT_NE(param0_type, nullptr);
    ASSERT_NE(param1_type, nullptr);

    auto* param0_path = dynamic_cast<nw::smalls::PathExpression*>(param0_type->name);
    auto* param1_path = dynamic_cast<nw::smalls::PathExpression*>(param1_type->name);
    ASSERT_NE(param0_path, nullptr);
    ASSERT_NE(param1_path, nullptr);
    ASSERT_EQ(param0_path->parts.size(), 1);
    ASSERT_EQ(param1_path->parts.size(), 1);
    auto* param0 = dynamic_cast<nw::smalls::IdentifierExpression*>(param0_path->parts[0]);
    auto* param1 = dynamic_cast<nw::smalls::IdentifierExpression*>(param1_path->parts[0]);
    ASSERT_NE(param0, nullptr);
    ASSERT_NE(param1, nullptr);
    EXPECT_EQ(param0->ident.loc.view(), "string");
    EXPECT_EQ(param1->ident.loc.view(), "int");
    EXPECT_EQ(var_decl->type->str(), "collections.Map!(string, int)");
}

TEST_F(SmallsParser, FullyQualifiedTypeName)
{
    auto script = make_script("var t: core.math.vector.Transform;"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(decls[0]);
    ASSERT_NE(var_decl, nullptr);
    ASSERT_NE(var_decl->type, nullptr);
    EXPECT_EQ(var_decl->type->str(), "core.math.vector.Transform");
}

TEST_F(SmallsParser, TupleTypeSimple)
{
    auto script = make_script("var x: (int, string);"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(decls[0]);
    ASSERT_NE(var_decl, nullptr);
    ASSERT_NE(var_decl->type, nullptr);
    EXPECT_EQ(var_decl->type->params.size(), 2);
}

TEST_F(SmallsParser, TupleTypeMultiple)
{
    auto script = make_script("var coord: (float, float, float);"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(decls[0]);
    ASSERT_NE(var_decl, nullptr);
    ASSERT_NE(var_decl->type, nullptr);
    EXPECT_EQ(var_decl->type->params.size(), 3);
}

TEST_F(SmallsParser, TupleTypeEmpty)
{
    auto script = make_script("var x: ();"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(decls[0]);
    ASSERT_NE(var_decl, nullptr);
    ASSERT_NE(var_decl->type, nullptr);
    EXPECT_EQ(var_decl->type->params.size(), 0);
}

TEST_F(SmallsParser, TupleTypeFunctionReturn)
{
    auto script = make_script("fn get_point(): (int, int) { return (0, 0); }"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* func_def = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func_def, nullptr);
    ASSERT_NE(func_def->return_type, nullptr);
    EXPECT_EQ(func_def->return_type->params.size(), 2);
}

TEST_F(SmallsParser, SumTypeUnitVariants)
{
    auto script = make_script("type Color = Red | Green | Blue;"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* sum = dynamic_cast<nw::smalls::SumDecl*>(decls[0]);
    ASSERT_NE(sum, nullptr);
    EXPECT_EQ(sum->type->str(), "Color");
    ASSERT_EQ(sum->variants.size(), 3);

    EXPECT_EQ(sum->variants[0]->identifier(), "Red");
    EXPECT_TRUE(sum->variants[0]->is_unit());

    EXPECT_EQ(sum->variants[1]->identifier(), "Green");
    EXPECT_TRUE(sum->variants[1]->is_unit());

    EXPECT_EQ(sum->variants[2]->identifier(), "Blue");
    EXPECT_TRUE(sum->variants[2]->is_unit());
}

TEST_F(SmallsParser, SumTypeWithPayloads)
{
    auto script = make_script("type Result = Ok(int) | Err(string);"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* sum = dynamic_cast<nw::smalls::SumDecl*>(decls[0]);
    ASSERT_NE(sum, nullptr);
    EXPECT_EQ(sum->type->str(), "Result");
    ASSERT_EQ(sum->variants.size(), 2);

    EXPECT_EQ(sum->variants[0]->identifier(), "Ok");
    EXPECT_FALSE(sum->variants[0]->is_unit());
    ASSERT_NE(sum->variants[0]->payload, nullptr);
    auto* ok_payload = dynamic_cast<nw::smalls::TypeExpression*>(sum->variants[0]->payload);
    ASSERT_NE(ok_payload, nullptr);
    EXPECT_EQ(ok_payload->str(), "int");

    EXPECT_EQ(sum->variants[1]->identifier(), "Err");
    EXPECT_FALSE(sum->variants[1]->is_unit());
    ASSERT_NE(sum->variants[1]->payload, nullptr);
    auto* err_payload = dynamic_cast<nw::smalls::TypeExpression*>(sum->variants[1]->payload);
    ASSERT_NE(err_payload, nullptr);
    EXPECT_EQ(err_payload->str(), "string");
}

TEST_F(SmallsParser, SumTypeMixedVariants)
{
    auto script = make_script("type Option = Some(int) | None;"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* sum = dynamic_cast<nw::smalls::SumDecl*>(decls[0]);
    ASSERT_NE(sum, nullptr);
    EXPECT_EQ(sum->type->str(), "Option");
    ASSERT_EQ(sum->variants.size(), 2);

    EXPECT_EQ(sum->variants[0]->identifier(), "Some");
    EXPECT_FALSE(sum->variants[0]->is_unit());

    EXPECT_EQ(sum->variants[1]->identifier(), "None");
    EXPECT_TRUE(sum->variants[1]->is_unit());
}

TEST_F(SmallsParser, SumTypeNestedType)
{
    auto script = make_script("type Tree = Leaf(int) | Node(Tree);"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* sum = dynamic_cast<nw::smalls::SumDecl*>(decls[0]);
    ASSERT_NE(sum, nullptr);
    EXPECT_EQ(sum->type->str(), "Tree");
    ASSERT_EQ(sum->variants.size(), 2);

    EXPECT_EQ(sum->variants[0]->identifier(), "Leaf");
    auto* leaf_payload = dynamic_cast<nw::smalls::TypeExpression*>(sum->variants[0]->payload);
    ASSERT_NE(leaf_payload, nullptr);
    EXPECT_EQ(leaf_payload->str(), "int");

    EXPECT_EQ(sum->variants[1]->identifier(), "Node");
    auto* node_payload = dynamic_cast<nw::smalls::TypeExpression*>(sum->variants[1]->payload);
    ASSERT_NE(node_payload, nullptr);
    EXPECT_EQ(node_payload->str(), "Tree");
}

TEST_F(SmallsParser, SumTypeComplexPayload)
{
    auto script = make_script("type Data = Numbers(array!(int)) | Mapping(map!(string, int));"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* sum = dynamic_cast<nw::smalls::SumDecl*>(decls[0]);
    ASSERT_NE(sum, nullptr);
    ASSERT_EQ(sum->variants.size(), 2);

    auto* numbers_payload = dynamic_cast<nw::smalls::TypeExpression*>(sum->variants[0]->payload);
    ASSERT_NE(numbers_payload, nullptr);
    EXPECT_EQ(numbers_payload->str(), "array!(int)");

    auto* mapping_payload = dynamic_cast<nw::smalls::TypeExpression*>(sum->variants[1]->payload);
    ASSERT_NE(mapping_payload, nullptr);
    EXPECT_EQ(mapping_payload->str(), "map!(string, int)");
}

TEST_F(SmallsParser, SumTypeVsTypeAlias)
{

    auto alias_script = make_script("type Alias = SomeType;"sv);
    EXPECT_NO_THROW(alias_script.parse());
    EXPECT_EQ(alias_script.errors(), 0);
    auto* alias = dynamic_cast<nw::smalls::TypeAlias*>(alias_script.ast().decls[0]);
    ASSERT_NE(alias, nullptr);

    auto sum_script = make_script("type Sum = Variant1 | Variant2;"sv);
    EXPECT_NO_THROW(sum_script.parse());
    EXPECT_EQ(sum_script.errors(), 0);
    auto* sum = dynamic_cast<nw::smalls::SumDecl*>(sum_script.ast().decls[0]);
    ASSERT_NE(sum, nullptr);
    ASSERT_EQ(sum->variants.size(), 2);
}

TEST_F(SmallsParser, SumTypeMultiplePayloads)
{
    auto script = make_script(R"(
        type Shape = Circle(float) | Rectangle(float, float) | Point;
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* sum = dynamic_cast<nw::smalls::SumDecl*>(decls[0]);
    ASSERT_NE(sum, nullptr);
    ASSERT_EQ(sum->variants.size(), 3);

    // Circle(float) - single payload
    auto* circle = sum->variants[0];
    EXPECT_EQ(circle->identifier(), "Circle");
    ASSERT_NE(circle->payload, nullptr);
    auto* circle_type = dynamic_cast<nw::smalls::TypeExpression*>(circle->payload);
    ASSERT_NE(circle_type, nullptr);
    EXPECT_EQ(circle_type->params.size(), 0); // Single type, no params

    // Rectangle(float, float) - tuple payload
    auto* rect = sum->variants[1];
    EXPECT_EQ(rect->identifier(), "Rectangle");
    ASSERT_NE(rect->payload, nullptr);
    auto* rect_type = dynamic_cast<nw::smalls::TypeExpression*>(rect->payload);
    ASSERT_NE(rect_type, nullptr);
    EXPECT_EQ(rect_type->params.size(), 2); // Tuple with 2 elements

    // Point - unit variant
    auto* point = sum->variants[2];
    EXPECT_EQ(point->identifier(), "Point");
    EXPECT_EQ(point->payload, nullptr);
}
