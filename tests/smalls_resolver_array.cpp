#include "smalls_fixtures.hpp"

#include <nw/log.hpp>
#include <nw/smalls/Context.hpp>
#include <nw/smalls/Smalls.hpp>

using namespace nw::smalls;

TEST_F(SmallsResolver, FixedArrayBasic)
{
    nw::String code = R"(
        var grid: int[16];
    )";

    auto script = make_script(code);
    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_decl = dynamic_cast<VarDecl*>(script.ast().decls[0]);
    ASSERT_NE(var_decl, nullptr);

    // Check that type was resolved correctly
    EXPECT_NE(var_decl->type_id_, invalid_type_id);

    auto& rt = nw::kernel::runtime();
    const Type* type = rt.get_type(var_decl->type_id_);
    ASSERT_NE(type, nullptr);

    // Should be TK_fixed_array
    EXPECT_EQ(type->type_kind, TK_fixed_array);

    // Should have correct type params: (element_type, size)
    ASSERT_EQ(type->type_params.size(), 2);
    EXPECT_TRUE(type->type_params[0].is<TypeID>());
    EXPECT_EQ(type->type_params[0].as<TypeID>(), rt.int_type());
    EXPECT_TRUE(type->type_params[1].is<int32_t>());
    EXPECT_EQ(type->type_params[1].as<int32_t>(), 16);

    // Type name should be "int[16]"
    EXPECT_EQ(type->name.view(), "int[16]");
}

TEST_F(SmallsResolver, FixedArrayWithStruct)
{
    nw::String code = R"(
        type Point { x, y: float; };
        var points: Point[10];
    )";

    auto script = make_script(code);
    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_decl = dynamic_cast<VarDecl*>(script.ast().decls[1]);
    ASSERT_NE(var_decl, nullptr);

    auto& rt = nw::kernel::runtime();
    const Type* type = rt.get_type(var_decl->type_id_);
    ASSERT_NE(type, nullptr);

    EXPECT_EQ(type->type_kind, TK_fixed_array);
    EXPECT_EQ(type->name.view(), "test.Point[10]");  // Module-qualified

    // Element type should be Point struct
    ASSERT_TRUE(type->type_params[0].is<TypeID>());
    TypeID elem_type = type->type_params[0].as<TypeID>();
    const Type* elem_type_obj = rt.get_type(elem_type);
    ASSERT_NE(elem_type_obj, nullptr);
    EXPECT_EQ(elem_type_obj->type_kind, TK_struct);
    EXPECT_EQ(elem_type_obj->name.view(), "test.Point");  // Module-qualified

    // Size should be 10
    ASSERT_TRUE(type->type_params[1].is<int32_t>());
    EXPECT_EQ(type->type_params[1].as<int32_t>(), 10);
}

TEST_F(SmallsResolver, FixedArrayConstSize)
{
    nw::String code = R"(
        const SIZE = 32;
        var buffer: int[SIZE];
    )";

    auto script = make_script(code);
    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_decl = dynamic_cast<VarDecl*>(script.ast().decls[1]);
    ASSERT_NE(var_decl, nullptr);

    auto& rt = nw::kernel::runtime();
    const Type* type = rt.get_type(var_decl->type_id_);
    ASSERT_NE(type, nullptr);

    EXPECT_EQ(type->type_kind, TK_fixed_array);
    EXPECT_EQ(type->name.view(), "int[32]");

    // Size should be evaluated to 32
    ASSERT_TRUE(type->type_params[1].is<int32_t>());
    EXPECT_EQ(type->type_params[1].as<int32_t>(), 32);
}

TEST_F(SmallsResolver, DynamicArrayStillWorks)
{
    nw::String code = R"(
        var nums: array!(int);
    )";

    auto script = make_script(code);
    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_decl = dynamic_cast<VarDecl*>(script.ast().decls[0]);
    ASSERT_NE(var_decl, nullptr);

    auto& rt = nw::kernel::runtime();
    const Type* type = rt.get_type(var_decl->type_id_);
    ASSERT_NE(type, nullptr);

    // Should be TK_array (dynamic), NOT TK_fixed_array
    EXPECT_EQ(type->type_kind, TK_array);
    EXPECT_EQ(type->name.view(), "array!(int)");
}

TEST_F(SmallsResolver, FixedArrayIndexing)
{
    nw::String code = R"(
        fn test() {
            var arr: int[8];
            var x = arr[0];
        }
    )";

    auto script = make_script(code);
    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* func = dynamic_cast<FunctionDefinition*>(script.ast().decls[0]);
    ASSERT_NE(func, nullptr);

    // Second statement: var x = arr[0];
    auto* var_stmt = dynamic_cast<VarDecl*>(func->block->nodes[1]);
    ASSERT_NE(var_stmt, nullptr);

    // x should have type int (from indexing int[8])
    auto& rt = nw::kernel::runtime();
    EXPECT_EQ(var_stmt->type_id_, rt.int_type());
}

TEST_F(SmallsResolver, FixedArrayNegativeSizeError)
{
    nw::String code = R"(
        var bad: int[-5];
    )";

    auto script = make_script(code);
    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);  // Should have error about negative size
}

TEST_F(SmallsResolver, FixedArrayZeroSizeError)
{
    nw::String code = R"(
        var bad: int[0];
    )";

    auto script = make_script(code);
    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);  // Should have error about zero size
}
