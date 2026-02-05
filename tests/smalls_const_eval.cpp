#include "smalls_fixtures.hpp"

#include <nw/log.hpp>
#include <nw/smalls/AstConstEvaluator.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/runtime.hpp>
#include <nw/smalls/types.hpp>

#include <gtest/gtest.h>

#include <string_view>

using namespace std::literals;

class SmallsConstEval : public ::testing::Test {
protected:
    void SetUp() override
    {
        nw::kernel::services().start();
    }

    void TearDown() override
    {
        nw::kernel::services().shutdown();
    }
};

TEST_F(SmallsConstEval, StructLiteralEvaluation)
{
    auto& rt = nw::kernel::runtime();

    auto script = make_script(R"(
type Point { x, y: int; };
const p: Point = { x = 10, y = 20 };
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[1]);
    ASSERT_NE(var_decl, nullptr);
    ASSERT_NE(var_decl->init, nullptr);

    nw::smalls::AstConstEvaluator eval{&script, var_decl->init};

    EXPECT_FALSE(eval.failed_);
    ASSERT_EQ(eval.result_.size(), 1);

    auto result = eval.result_.back();
    EXPECT_NE(result.type_id, nw::smalls::invalid_type_id);

    const nw::smalls::Type* type = rt.get_type(result.type_id);
    ASSERT_NE(type, nullptr);
    EXPECT_EQ(type->type_kind, nw::smalls::TK_struct);

    void* struct_data = rt.heap_.get_ptr(result.data.hptr);
    ASSERT_NE(struct_data, nullptr);

    ASSERT_TRUE(type->type_params[0].is<nw::smalls::StructID>());
    auto struct_id = type->type_params[0].as<nw::smalls::StructID>();
    const nw::smalls::StructDef* struct_def = rt.type_table_.get(struct_id);
    ASSERT_NE(struct_def, nullptr);
    ASSERT_EQ(struct_def->field_count, 2);

    const nw::smalls::FieldDef* x_field = nullptr;
    const nw::smalls::FieldDef* y_field = nullptr;
    for (uint32_t i = 0; i < struct_def->field_count; ++i) {
        if (struct_def->fields[i].name.view() == "x") {
            x_field = &struct_def->fields[i];
        } else if (struct_def->fields[i].name.view() == "y") {
            y_field = &struct_def->fields[i];
        }
    }
    ASSERT_NE(x_field, nullptr);
    ASSERT_NE(y_field, nullptr);

    int32_t* x_ptr = reinterpret_cast<int32_t*>(static_cast<char*>(struct_data) + x_field->offset);
    int32_t* y_ptr = reinterpret_cast<int32_t*>(static_cast<char*>(struct_data) + y_field->offset);

    EXPECT_EQ(*x_ptr, 10);
    EXPECT_EQ(*y_ptr, 20);
}

TEST_F(SmallsConstEval, StructLiteralNested)
{
    auto& rt = nw::kernel::runtime();
    auto script = make_script(R"(
type Point { x, y: int; };
type Line { start, end: Point; };
const line: Line = {
    start = { x = 1, y = 2 },
    end = { x = 10, y = 20 }
};
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[2]);
    ASSERT_NE(var_decl, nullptr);

    nw::smalls::AstConstEvaluator eval{&script, var_decl->init};
    EXPECT_FALSE(eval.failed_);
    ASSERT_EQ(eval.result_.size(), 1);

    auto result = eval.result_.back();
    const nw::smalls::Type* type = rt.get_type(result.type_id);
    ASSERT_NE(type, nullptr);

    void* struct_data = rt.heap_.get_ptr(result.data.hptr);
    ASSERT_NE(struct_data, nullptr);

    auto struct_id = type->type_params[0].as<nw::smalls::StructID>();
    const nw::smalls::StructDef* struct_def = rt.type_table_.get(struct_id);
    ASSERT_NE(struct_def, nullptr);
    ASSERT_EQ(struct_def->field_count, 2);

    const nw::smalls::FieldDef* start_field = nullptr;
    const nw::smalls::FieldDef* end_field = nullptr;
    for (uint32_t i = 0; i < struct_def->field_count; ++i) {
        if (struct_def->fields[i].name.view() == "start") {
            start_field = &struct_def->fields[i];
        } else if (struct_def->fields[i].name.view() == "end") {
            end_field = &struct_def->fields[i];
        }
    }
    ASSERT_NE(start_field, nullptr);
    ASSERT_NE(end_field, nullptr);

    nw::smalls::HeapPtr* start_ptr = reinterpret_cast<nw::smalls::HeapPtr*>(
        static_cast<char*>(struct_data) + start_field->offset);
    nw::smalls::HeapPtr* end_ptr = reinterpret_cast<nw::smalls::HeapPtr*>(
        static_cast<char*>(struct_data) + end_field->offset);

    void* start_data = rt.heap_.get_ptr(*start_ptr);
    void* end_data = rt.heap_.get_ptr(*end_ptr);
    ASSERT_NE(start_data, nullptr);
    ASSERT_NE(end_data, nullptr);

    auto point_type = rt.get_type(start_field->type_id);
    ASSERT_NE(point_type, nullptr);
    auto point_struct_id = point_type->type_params[0].as<nw::smalls::StructID>();
    const nw::smalls::StructDef* point_def = rt.type_table_.get(point_struct_id);
    ASSERT_NE(point_def, nullptr);

    uint32_t x_offset = 0, y_offset = 0;
    for (uint32_t i = 0; i < point_def->field_count; ++i) {
        if (point_def->fields[i].name.view() == "x") {
            x_offset = point_def->fields[i].offset;
        } else if (point_def->fields[i].name.view() == "y") {
            y_offset = point_def->fields[i].offset;
        }
    }

    int32_t* start_x = reinterpret_cast<int32_t*>(static_cast<char*>(start_data) + x_offset);
    int32_t* start_y = reinterpret_cast<int32_t*>(static_cast<char*>(start_data) + y_offset);
    int32_t* end_x = reinterpret_cast<int32_t*>(static_cast<char*>(end_data) + x_offset);
    int32_t* end_y = reinterpret_cast<int32_t*>(static_cast<char*>(end_data) + y_offset);

    EXPECT_EQ(*start_x, 1);
    EXPECT_EQ(*start_y, 2);
    EXPECT_EQ(*end_x, 10);
    EXPECT_EQ(*end_y, 20);
}

TEST_F(SmallsConstEval, StructLiteralMixedTypes)
{
    auto& rt = nw::kernel::runtime();
    auto script = make_script(R"(
type MixedData {
    flag: bool;
    count: int;
    value: float;
    id: int;
};
const data: MixedData = {
    flag = true,
    count = 42,
    value = 3.14,
    id = 100
};
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[1]);
    ASSERT_NE(var_decl, nullptr);

    nw::smalls::AstConstEvaluator eval{&script, var_decl->init};
    EXPECT_FALSE(eval.failed_);
    ASSERT_EQ(eval.result_.size(), 1);

    auto result = eval.result_.back();
    void* struct_data = rt.heap_.get_ptr(result.data.hptr);
    ASSERT_NE(struct_data, nullptr);

    const nw::smalls::Type* type = rt.get_type(result.type_id);
    auto struct_id = type->type_params[0].as<nw::smalls::StructID>();
    const nw::smalls::StructDef* struct_def = rt.type_table_.get(struct_id);
    ASSERT_NE(struct_def, nullptr);

    for (uint32_t i = 0; i < struct_def->field_count; ++i) {
        auto& field = struct_def->fields[i];
        EXPECT_LT(field.offset, type->size)
            << "Field " << field.name.view() << " offset beyond struct bounds";

        if (field.name.view() == "flag") {
            bool* ptr = reinterpret_cast<bool*>(static_cast<char*>(struct_data) + field.offset);
            EXPECT_EQ(*ptr, true);
        } else if (field.name.view() == "count") {
            int32_t* ptr = reinterpret_cast<int32_t*>(static_cast<char*>(struct_data) + field.offset);
            EXPECT_EQ(*ptr, 42);
        } else if (field.name.view() == "value") {
            float* ptr = reinterpret_cast<float*>(static_cast<char*>(struct_data) + field.offset);
            EXPECT_FLOAT_EQ(*ptr, 3.14f);
        } else if (field.name.view() == "id") {
            int32_t* ptr = reinterpret_cast<int32_t*>(static_cast<char*>(struct_data) + field.offset);
            EXPECT_EQ(*ptr, 100);
        }
    }
}

TEST_F(SmallsConstEval, StructLiteralLargeStruct)
{
    auto& rt = nw::kernel::runtime();
    auto script = make_script(R"(
type LargeStruct {
    f1, f2, f3, f4, f5, f6, f7, f8: int;
    f9, f10: float;
    f11, f12, f13: int;
};
const data: LargeStruct = {
    f1 = 1, f2 = 2, f3 = 3, f4 = 4,
    f5 = 5, f6 = 6, f7 = 7, f8 = 8,
    f9 = 9.0, f10 = 10.0,
    f11 = 11, f12 = 12, f13 = 13
};
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[1]);
    ASSERT_NE(var_decl, nullptr);

    nw::smalls::AstConstEvaluator eval{&script, var_decl->init};
    EXPECT_FALSE(eval.failed_);
    ASSERT_EQ(eval.result_.size(), 1);

    auto result = eval.result_.back();
    void* struct_data = rt.heap_.get_ptr(result.data.hptr);
    ASSERT_NE(struct_data, nullptr);

    const nw::smalls::Type* type = rt.get_type(result.type_id);
    auto struct_id = type->type_params[0].as<nw::smalls::StructID>();
    const nw::smalls::StructDef* struct_def = rt.type_table_.get(struct_id);
    ASSERT_NE(struct_def, nullptr);
    ASSERT_EQ(struct_def->field_count, 13);

    for (uint32_t i = 0; i < struct_def->field_count; ++i) {
        auto& field = struct_def->fields[i];

        const nw::smalls::Type* field_type = rt.get_type(field.type_id);
        ASSERT_NE(field_type, nullptr);

        uint32_t field_size = field_type->size;
        EXPECT_LE(field.offset + field_size, type->size)
            << "Field " << field.name.view()
            << " extends beyond struct bounds (offset=" << field.offset
            << ", size=" << field_size
            << ", struct_size=" << type->size << ")";
    }

    for (uint32_t i = 0; i < 8; ++i) {
        std::string field_name = "f" + std::to_string(i + 1);
        for (uint32_t j = 0; j < struct_def->field_count; ++j) {
            if (struct_def->fields[j].name.view() == field_name) {
                int32_t* ptr = reinterpret_cast<int32_t*>(
                    static_cast<char*>(struct_data) + struct_def->fields[j].offset);
                EXPECT_EQ(*ptr, static_cast<int32_t>(i + 1)) << "Field " << field_name;
                break;
            }
        }
    }
}

TEST_F(SmallsConstEval, StructLiteralOutOfOrderFields)
{
    auto& rt = nw::kernel::runtime();
    auto script = make_script(R"(
type Point { x, y, z: int; };
const p: Point = { z = 30, x = 10, y = 20 };
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[1]);
    ASSERT_NE(var_decl, nullptr);

    nw::smalls::AstConstEvaluator eval{&script, var_decl->init};
    EXPECT_FALSE(eval.failed_);
    ASSERT_EQ(eval.result_.size(), 1);

    auto result = eval.result_.back();
    void* struct_data = rt.heap_.get_ptr(result.data.hptr);
    ASSERT_NE(struct_data, nullptr);

    const nw::smalls::Type* type = rt.get_type(result.type_id);
    auto struct_id = type->type_params[0].as<nw::smalls::StructID>();
    const nw::smalls::StructDef* struct_def = rt.type_table_.get(struct_id);

    uint32_t x_offset = 0, y_offset = 0, z_offset = 0;
    for (uint32_t i = 0; i < struct_def->field_count; ++i) {
        if (struct_def->fields[i].name.view() == "x") {
            x_offset = struct_def->fields[i].offset;
        } else if (struct_def->fields[i].name.view() == "y") {
            y_offset = struct_def->fields[i].offset;
        } else if (struct_def->fields[i].name.view() == "z") {
            z_offset = struct_def->fields[i].offset;
        }
    }

    int32_t* x_ptr = reinterpret_cast<int32_t*>(static_cast<char*>(struct_data) + x_offset);
    int32_t* y_ptr = reinterpret_cast<int32_t*>(static_cast<char*>(struct_data) + y_offset);
    int32_t* z_ptr = reinterpret_cast<int32_t*>(static_cast<char*>(struct_data) + z_offset);

    EXPECT_EQ(*x_ptr, 10);
    EXPECT_EQ(*y_ptr, 20);
    EXPECT_EQ(*z_ptr, 30);
}

TEST_F(SmallsConstEval, StructLiteralPartialInit)
{
    auto& rt = nw::kernel::runtime();
    auto script = make_script(R"(
type Point { x, y, z: int; };
const p: Point = { x = 10 };
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[1]);
    ASSERT_NE(var_decl, nullptr);

    nw::smalls::AstConstEvaluator eval{&script, var_decl->init};
    EXPECT_FALSE(eval.failed_);

    auto result = eval.result_.back();
    void* struct_data = rt.heap_.get_ptr(result.data.hptr);
    ASSERT_NE(struct_data, nullptr);

    const nw::smalls::Type* type = rt.get_type(result.type_id);
    auto struct_id = type->type_params[0].as<nw::smalls::StructID>();
    const nw::smalls::StructDef* struct_def = rt.type_table_.get(struct_id);

    uint32_t x_offset = 0, y_offset = 0, z_offset = 0;
    for (uint32_t i = 0; i < struct_def->field_count; ++i) {
        if (struct_def->fields[i].name.view() == "x") {
            x_offset = struct_def->fields[i].offset;
        } else if (struct_def->fields[i].name.view() == "y") {
            y_offset = struct_def->fields[i].offset;
        } else if (struct_def->fields[i].name.view() == "z") {
            z_offset = struct_def->fields[i].offset;
        }
    }

    int32_t* x_ptr = reinterpret_cast<int32_t*>(static_cast<char*>(struct_data) + x_offset);
    int32_t* y_ptr = reinterpret_cast<int32_t*>(static_cast<char*>(struct_data) + y_offset);
    int32_t* z_ptr = reinterpret_cast<int32_t*>(static_cast<char*>(struct_data) + z_offset);

    EXPECT_EQ(*x_ptr, 10);
    EXPECT_EQ(*y_ptr, 0);
    EXPECT_EQ(*z_ptr, 0);
}

TEST_F(SmallsConstEval, StructLiteralStringFields)
{
    auto& rt = nw::kernel::runtime();
    auto script = make_script(R"(
type Person { name: string; age: int; };
const p: Person = { name = "Alice", age = 30 };
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[1]);
    ASSERT_NE(var_decl, nullptr);

    nw::smalls::AstConstEvaluator eval{&script, var_decl->init};
    EXPECT_FALSE(eval.failed_);

    auto result = eval.result_.back();
    void* struct_data = rt.heap_.get_ptr(result.data.hptr);
    ASSERT_NE(struct_data, nullptr);

    const nw::smalls::Type* type = rt.get_type(result.type_id);
    auto struct_id = type->type_params[0].as<nw::smalls::StructID>();
    const nw::smalls::StructDef* struct_def = rt.type_table_.get(struct_id);

    const nw::smalls::FieldDef* name_field = nullptr;
    const nw::smalls::FieldDef* age_field = nullptr;
    for (uint32_t i = 0; i < struct_def->field_count; ++i) {
        if (struct_def->fields[i].name.view() == "name") {
            name_field = &struct_def->fields[i];
        } else if (struct_def->fields[i].name.view() == "age") {
            age_field = &struct_def->fields[i];
        }
    }
    ASSERT_NE(name_field, nullptr);
    ASSERT_NE(age_field, nullptr);

    nw::smalls::HeapPtr* name_ptr = reinterpret_cast<nw::smalls::HeapPtr*>(
        static_cast<char*>(struct_data) + name_field->offset);
    int32_t* age_ptr = reinterpret_cast<int32_t*>(
        static_cast<char*>(struct_data) + age_field->offset);

    nw::StringView name_sv = rt.get_string_view(*name_ptr);
    EXPECT_EQ(name_sv, "Alice");
    EXPECT_EQ(*age_ptr, 30);
}

TEST_F(SmallsConstEval, StructLiteralAlignmentStress)
{
    auto& rt = nw::kernel::runtime();
    auto script = make_script(R"(
type AlignmentTest {
    b1: bool;
    i1: int;
    b2: bool;
    f1: float;
    b3: bool;
    i2: int;
};
const data: AlignmentTest = {
    b1 = true, i1 = 100, b2 = false,
    f1 = 2.5, b3 = true, i2 = 200
};
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[1]);
    ASSERT_NE(var_decl, nullptr);

    nw::smalls::AstConstEvaluator eval{&script, var_decl->init};
    EXPECT_FALSE(eval.failed_);

    auto result = eval.result_.back();
    void* struct_data = rt.heap_.get_ptr(result.data.hptr);
    ASSERT_NE(struct_data, nullptr);

    const nw::smalls::Type* type = rt.get_type(result.type_id);
    auto struct_id = type->type_params[0].as<nw::smalls::StructID>();
    const nw::smalls::StructDef* struct_def = rt.type_table_.get(struct_id);

    for (uint32_t i = 0; i < struct_def->field_count; ++i) {
        auto& field = struct_def->fields[i];
        const nw::smalls::Type* field_type = rt.get_type(field.type_id);

        EXPECT_EQ(field.offset % field_type->alignment, 0u)
            << "Field " << field.name.view() << " not properly aligned";

        EXPECT_LE(field.offset + field_type->size, type->size)
            << "Field " << field.name.view() << " extends beyond struct bounds";
    }

    for (uint32_t i = 0; i < struct_def->field_count; ++i) {
        auto& field = struct_def->fields[i];
        if (field.name.view() == "b1") {
            bool* ptr = reinterpret_cast<bool*>(static_cast<char*>(struct_data) + field.offset);
            EXPECT_EQ(*ptr, true);
        } else if (field.name.view() == "i1") {
            int32_t* ptr = reinterpret_cast<int32_t*>(static_cast<char*>(struct_data) + field.offset);
            EXPECT_EQ(*ptr, 100);
        } else if (field.name.view() == "i2") {
            int32_t* ptr = reinterpret_cast<int32_t*>(static_cast<char*>(struct_data) + field.offset);
            EXPECT_EQ(*ptr, 200);
        }
    }
}

TEST_F(SmallsConstEval, StructLiteralListBased)
{
    auto& rt = nw::kernel::runtime();
    auto script = make_script(R"(
type Point { x, y: int; };
const p: Point = { 10, 20 };
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[1]);
    ASSERT_NE(var_decl, nullptr);

    nw::smalls::AstConstEvaluator eval{&script, var_decl->init};
    EXPECT_FALSE(eval.failed_);
    ASSERT_EQ(eval.result_.size(), 1);

    auto result = eval.result_.back();
    void* struct_data = rt.heap_.get_ptr(result.data.hptr);
    ASSERT_NE(struct_data, nullptr);

    const nw::smalls::Type* type = rt.get_type(result.type_id);
    auto struct_id = type->type_params[0].as<nw::smalls::StructID>();
    const nw::smalls::StructDef* struct_def = rt.type_table_.get(struct_id);

    int32_t* x_ptr = nullptr;
    int32_t* y_ptr = nullptr;

    for (uint32_t i = 0; i < struct_def->field_count; ++i) {
        auto& field = struct_def->fields[i];
        if (field.name.view() == "x") {
            x_ptr = reinterpret_cast<int32_t*>(static_cast<char*>(struct_data) + field.offset);
        } else if (field.name.view() == "y") {
            y_ptr = reinterpret_cast<int32_t*>(static_cast<char*>(struct_data) + field.offset);
        }
    }

    ASSERT_NE(x_ptr, nullptr);
    ASSERT_NE(y_ptr, nullptr);
    EXPECT_EQ(*x_ptr, 10);
    EXPECT_EQ(*y_ptr, 20);
}

TEST_F(SmallsConstEval, PrimitiveArithmeticOperators)
{
    auto script = make_script(R"(
const a = 10 + 5;
const b = 10 - 3;
const c = 4 * 7;
const d = 20 / 4;
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    // Test addition
    auto* var_a = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[0]);
    ASSERT_NE(var_a, nullptr);
    nw::smalls::AstConstEvaluator eval_a{&script, var_a->init};
    EXPECT_FALSE(eval_a.failed_);
    ASSERT_EQ(eval_a.result_.size(), 1);
    EXPECT_EQ(eval_a.result_.back().data.ival, 15);

    // Test subtraction
    auto* var_b = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[1]);
    ASSERT_NE(var_b, nullptr);
    nw::smalls::AstConstEvaluator eval_b{&script, var_b->init};
    EXPECT_FALSE(eval_b.failed_);
    ASSERT_EQ(eval_b.result_.size(), 1);
    EXPECT_EQ(eval_b.result_.back().data.ival, 7);

    // Test multiplication
    auto* var_c = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[2]);
    ASSERT_NE(var_c, nullptr);
    nw::smalls::AstConstEvaluator eval_c{&script, var_c->init};
    EXPECT_FALSE(eval_c.failed_);
    ASSERT_EQ(eval_c.result_.size(), 1);
    EXPECT_EQ(eval_c.result_.back().data.ival, 28);

    // Test division
    auto* var_d = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[3]);
    ASSERT_NE(var_d, nullptr);
    nw::smalls::AstConstEvaluator eval_d{&script, var_d->init};
    EXPECT_FALSE(eval_d.failed_);
    ASSERT_EQ(eval_d.result_.size(), 1);
    EXPECT_EQ(eval_d.result_.back().data.ival, 5);
}

TEST_F(SmallsConstEval, PrimitiveFloatOperators)
{
    auto script = make_script(R"(
const a = 10.5 + 2.5;
const b = 10.0 * 3.0;
const c = 15.0 / 3.0;
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_a = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[0]);
    ASSERT_NE(var_a, nullptr);
    nw::smalls::AstConstEvaluator eval_a{&script, var_a->init};
    EXPECT_FALSE(eval_a.failed_);
    ASSERT_EQ(eval_a.result_.size(), 1);
    EXPECT_FLOAT_EQ(eval_a.result_.back().data.fval, 13.0f);

    auto* var_b = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[1]);
    ASSERT_NE(var_b, nullptr);
    nw::smalls::AstConstEvaluator eval_b{&script, var_b->init};
    EXPECT_FALSE(eval_b.failed_);
    ASSERT_EQ(eval_b.result_.size(), 1);
    EXPECT_FLOAT_EQ(eval_b.result_.back().data.fval, 30.0f);

    auto* var_c = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[2]);
    ASSERT_NE(var_c, nullptr);
    nw::smalls::AstConstEvaluator eval_c{&script, var_c->init};
    EXPECT_FALSE(eval_c.failed_);
    ASSERT_EQ(eval_c.result_.size(), 1);
    EXPECT_FLOAT_EQ(eval_c.result_.back().data.fval, 5.0f);
}

TEST_F(SmallsConstEval, PrimitiveMixedTypeOperators)
{
    auto script = make_script(R"(
const a = 10 + 2.5;
const b = 3.5 * 2;
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    // int + float -> float
    auto* var_a = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[0]);
    ASSERT_NE(var_a, nullptr);
    nw::smalls::AstConstEvaluator eval_a{&script, var_a->init};
    EXPECT_FALSE(eval_a.failed_);
    ASSERT_EQ(eval_a.result_.size(), 1);
    EXPECT_FLOAT_EQ(eval_a.result_.back().data.fval, 12.5f);

    // float * int -> float
    auto* var_b = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[1]);
    ASSERT_NE(var_b, nullptr);
    nw::smalls::AstConstEvaluator eval_b{&script, var_b->init};
    EXPECT_FALSE(eval_b.failed_);
    ASSERT_EQ(eval_b.result_.size(), 1);
    EXPECT_FLOAT_EQ(eval_b.result_.back().data.fval, 7.0f);
}

TEST_F(SmallsConstEval, PrimitiveComparisonOperators)
{
    auto script = make_script(R"(
const a = 10 < 20;
const b = 20 < 10;
const c = 5 == 5;
const d = 5 == 7;
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_a = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[0]);
    ASSERT_NE(var_a, nullptr);
    nw::smalls::AstConstEvaluator eval_a{&script, var_a->init};
    EXPECT_FALSE(eval_a.failed_);
    ASSERT_EQ(eval_a.result_.size(), 1);
    EXPECT_TRUE(eval_a.result_.back().data.bval); // 10 < 20 = true

    auto* var_b = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[1]);
    ASSERT_NE(var_b, nullptr);
    nw::smalls::AstConstEvaluator eval_b{&script, var_b->init};
    EXPECT_FALSE(eval_b.failed_);
    ASSERT_EQ(eval_b.result_.size(), 1);
    EXPECT_FALSE(eval_b.result_.back().data.bval); // 20 < 10 = false

    auto* var_c = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[2]);
    ASSERT_NE(var_c, nullptr);
    nw::smalls::AstConstEvaluator eval_c{&script, var_c->init};
    EXPECT_FALSE(eval_c.failed_);
    ASSERT_EQ(eval_c.result_.size(), 1);
    EXPECT_TRUE(eval_c.result_.back().data.bval); // 5 == 5 = true

    auto* var_d = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[3]);
    ASSERT_NE(var_d, nullptr);
    nw::smalls::AstConstEvaluator eval_d{&script, var_d->init};
    EXPECT_FALSE(eval_d.failed_);
    ASSERT_EQ(eval_d.result_.size(), 1);
    EXPECT_FALSE(eval_d.result_.back().data.bval); // 5 == 7 = false
}

TEST_F(SmallsConstEval, SynthesizedComparisonOperators)
{
    auto script = make_script(R"(
const a = 10 > 5;
const b = 5 >= 5;
const c = 3 != 4;
const d = 10 <= 20;
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_a = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[0]);
    ASSERT_NE(var_a, nullptr);
    nw::smalls::AstConstEvaluator eval_a{&script, var_a->init};
    EXPECT_FALSE(eval_a.failed_);
    ASSERT_EQ(eval_a.result_.size(), 1);
    EXPECT_TRUE(eval_a.result_.back().data.bval); // 10 > 5 = true

    auto* var_b = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[1]);
    ASSERT_NE(var_b, nullptr);
    nw::smalls::AstConstEvaluator eval_b{&script, var_b->init};
    EXPECT_FALSE(eval_b.failed_);
    ASSERT_EQ(eval_b.result_.size(), 1);
    EXPECT_TRUE(eval_b.result_.back().data.bval); // 5 >= 5 = true

    auto* var_c = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[2]);
    ASSERT_NE(var_c, nullptr);
    nw::smalls::AstConstEvaluator eval_c{&script, var_c->init};
    EXPECT_FALSE(eval_c.failed_);
    ASSERT_EQ(eval_c.result_.size(), 1);
    EXPECT_TRUE(eval_c.result_.back().data.bval); // 3 != 4 = true

    auto* var_d = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[3]);
    ASSERT_NE(var_d, nullptr);
    nw::smalls::AstConstEvaluator eval_d{&script, var_d->init};
    EXPECT_FALSE(eval_d.failed_);
    ASSERT_EQ(eval_d.result_.size(), 1);
    EXPECT_TRUE(eval_d.result_.back().data.bval); // 10 <= 20 = true
}

TEST_F(SmallsConstEval, StructFieldAccess)
{
    auto script = make_script(R"(
type Point { x, y: int; };
const origin: Point = { x = 10, y = 20 };
const x_val = origin.x;
const y_val = origin.y;
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_x = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[2]);
    ASSERT_NE(var_x, nullptr);
    nw::smalls::AstConstEvaluator eval_x{&script, var_x->init};
    EXPECT_FALSE(eval_x.failed_);
    ASSERT_EQ(eval_x.result_.size(), 1);
    EXPECT_EQ(eval_x.result_.back().data.ival, 10);

    auto* var_y = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[3]);
    ASSERT_NE(var_y, nullptr);
    nw::smalls::AstConstEvaluator eval_y{&script, var_y->init};
    EXPECT_FALSE(eval_y.failed_);
    ASSERT_EQ(eval_y.result_.size(), 1);
    EXPECT_EQ(eval_y.result_.back().data.ival, 20);
}

TEST_F(SmallsConstEval, StructFieldAccessNested)
{
    auto script = make_script(R"(
type Point { x, y: int; };
type Line { start, end: Point; };
const line: Line = {
    start = { x = 1, y = 2 },
    end = { x = 10, y = 20 }
};
const start_x = line.start.x;
const end_y = line.end.y;
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_start_x = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[3]);
    ASSERT_NE(var_start_x, nullptr);
    nw::smalls::AstConstEvaluator eval_start_x{&script, var_start_x->init};
    EXPECT_FALSE(eval_start_x.failed_);
    ASSERT_EQ(eval_start_x.result_.size(), 1);
    EXPECT_EQ(eval_start_x.result_.back().data.ival, 1);

    auto* var_end_y = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[4]);
    ASSERT_NE(var_end_y, nullptr);
    nw::smalls::AstConstEvaluator eval_end_y{&script, var_end_y->init};
    EXPECT_FALSE(eval_end_y.failed_);
    ASSERT_EQ(eval_end_y.result_.size(), 1);
    EXPECT_EQ(eval_end_y.result_.back().data.ival, 20);
}

TEST_F(SmallsConstEval, StructFieldAccessInArithmetic)
{
    auto script = make_script(R"(
type Point { x, y: int; };
const p1: Point = { x = 10, y = 20 };
const p2: Point = { x = 5, y = 15 };
const sum_x = p1.x + p2.x;
const diff_y = p1.y - p2.y;
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_sum = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[3]);
    ASSERT_NE(var_sum, nullptr);
    nw::smalls::AstConstEvaluator eval_sum{&script, var_sum->init};
    EXPECT_FALSE(eval_sum.failed_);
    ASSERT_EQ(eval_sum.result_.size(), 1);
    EXPECT_EQ(eval_sum.result_.back().data.ival, 15); // 10 + 5

    auto* var_diff = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[4]);
    ASSERT_NE(var_diff, nullptr);
    nw::smalls::AstConstEvaluator eval_diff{&script, var_diff->init};
    EXPECT_FALSE(eval_diff.failed_);
    ASSERT_EQ(eval_diff.result_.size(), 1);
    EXPECT_EQ(eval_diff.result_.back().data.ival, 5); // 20 - 15
}

TEST_F(SmallsConstEval, StructFieldAccessMixedTypes)
{
    auto script = make_script(R"(
type Data { count: int; scale: float; active: bool; };
const data: Data = { count = 42, scale = 2.5, active = true };
const c = data.count;
const s = data.scale;
const a = data.active;
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_c = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[2]);
    ASSERT_NE(var_c, nullptr);
    nw::smalls::AstConstEvaluator eval_c{&script, var_c->init};
    EXPECT_FALSE(eval_c.failed_);
    ASSERT_EQ(eval_c.result_.size(), 1);
    EXPECT_EQ(eval_c.result_.back().data.ival, 42);

    auto* var_s = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[3]);
    ASSERT_NE(var_s, nullptr);
    nw::smalls::AstConstEvaluator eval_s{&script, var_s->init};
    EXPECT_FALSE(eval_s.failed_);
    ASSERT_EQ(eval_s.result_.size(), 1);
    EXPECT_FLOAT_EQ(eval_s.result_.back().data.fval, 2.5f);

    auto* var_a = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[4]);
    ASSERT_NE(var_a, nullptr);
    nw::smalls::AstConstEvaluator eval_a{&script, var_a->init};
    EXPECT_FALSE(eval_a.failed_);
    ASSERT_EQ(eval_a.result_.size(), 1);
    EXPECT_TRUE(eval_a.result_.back().data.bval);
}

TEST_F(SmallsConstEval, TupleLiteralSimple)
{
    auto script = make_script(R"(
const tuple: (int, float, bool) = (42, 3.14, true);
    )"sv);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_tuple = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[0]);
    ASSERT_NE(var_tuple, nullptr);
    nw::smalls::AstConstEvaluator eval(&script, var_tuple->init);
    EXPECT_FALSE(eval.failed_);
    ASSERT_EQ(eval.result_.size(), 1);
    EXPECT_NE(eval.result_.back().data.hptr.value, 0);
}

TEST_F(SmallsConstEval, TupleElementAccess)
{
    auto script = make_script(R"(
const tuple: (int, float, bool) = (42, 3.14, true);
const first = tuple[0];
const second = tuple[1];
const third = tuple[2];
    )"sv);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_first = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[1]);
    ASSERT_NE(var_first, nullptr);
    nw::smalls::AstConstEvaluator eval_first(&script, var_first->init);
    EXPECT_FALSE(eval_first.failed_);
    ASSERT_EQ(eval_first.result_.size(), 1);
    EXPECT_EQ(eval_first.result_.back().data.ival, 42);

    auto* var_second = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[2]);
    ASSERT_NE(var_second, nullptr);
    nw::smalls::AstConstEvaluator eval_second(&script, var_second->init);
    EXPECT_FALSE(eval_second.failed_);
    ASSERT_EQ(eval_second.result_.size(), 1);
    EXPECT_FLOAT_EQ(eval_second.result_.back().data.fval, 3.14f);

    auto* var_third = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[3]);
    ASSERT_NE(var_third, nullptr);
    nw::smalls::AstConstEvaluator eval_third(&script, var_third->init);
    EXPECT_FALSE(eval_third.failed_);
    ASSERT_EQ(eval_third.result_.size(), 1);
    EXPECT_TRUE(eval_third.result_.back().data.bval);
}

TEST_F(SmallsConstEval, TupleNestedAccess)
{
    auto script = make_script(R"(
const nested: ((int, int), (int, int)) = ((1, 2), (3, 4));
const inner_first = nested[0];
const value = nested[0][1];
    )"sv);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_inner = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[1]);
    ASSERT_NE(var_inner, nullptr);
    nw::smalls::AstConstEvaluator eval_inner(&script, var_inner->init);
    EXPECT_FALSE(eval_inner.failed_);
    ASSERT_EQ(eval_inner.result_.size(), 1);
    EXPECT_NE(eval_inner.result_.back().data.hptr.value, 0);

    auto* var_value = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[2]);
    ASSERT_NE(var_value, nullptr);
    nw::smalls::AstConstEvaluator eval_value(&script, var_value->init);
    EXPECT_FALSE(eval_value.failed_);
    ASSERT_EQ(eval_value.result_.size(), 1);
    EXPECT_EQ(eval_value.result_.back().data.ival, 2);
}

TEST_F(SmallsConstEval, TupleInSwitchCase)
{
    auto script = make_script(R"(
const coords: (int, int) = (10, 20);
const x_coord = coords[0];

fn test(value: int) {
    switch (value) {
        case x_coord: return;
        default: return;
    }
}
    )"sv);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_x = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[1]);
    ASSERT_NE(var_x, nullptr);
    nw::smalls::AstConstEvaluator eval(&script, var_x->init);
    EXPECT_FALSE(eval.failed_);
    ASSERT_EQ(eval.result_.size(), 1);
    EXPECT_EQ(eval.result_.back().data.ival, 10);
}

TEST_F(SmallsConstEval, FStringBasic)
{
    auto& rt = nw::kernel::runtime();
    auto script = make_script(R"###(
const x = 42;
const msg = f"The answer is {x}";
)###"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_msg = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[1]);
    ASSERT_NE(var_msg, nullptr);
    nw::smalls::AstConstEvaluator eval{&script, var_msg->init};
    EXPECT_FALSE(eval.failed_);
    ASSERT_EQ(eval.result_.size(), 1);

    auto result = eval.result_.back();
    EXPECT_EQ(result.type_id, rt.string_type());

    nw::StringView str = rt.get_string_view(result.data.hptr);
    EXPECT_EQ(str, "The answer is 42");
}

TEST_F(SmallsConstEval, FStringMultipleInterpolations)
{
    auto& rt = nw::kernel::runtime();
    auto script = make_script(R"###(
const x = 10;
const y = 20;
const msg = f"Point({x}, {y})";
)###"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_msg = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[2]);
    ASSERT_NE(var_msg, nullptr);
    nw::smalls::AstConstEvaluator eval{&script, var_msg->init};
    EXPECT_FALSE(eval.failed_);
    ASSERT_EQ(eval.result_.size(), 1);

    auto result = eval.result_.back();
    EXPECT_EQ(result.type_id, rt.string_type());

    nw::StringView str = rt.get_string_view(result.data.hptr);
    EXPECT_EQ(str, "Point(10, 20)");
}

TEST_F(SmallsConstEval, FStringExpressions)
{
    auto& rt = nw::kernel::runtime();
    auto script = make_script(R"###(
const x = 5;
const msg = f"5 + 3 = {x + 3}";
)###"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_msg = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[1]);
    ASSERT_NE(var_msg, nullptr);
    nw::smalls::AstConstEvaluator eval{&script, var_msg->init};
    EXPECT_FALSE(eval.failed_);
    ASSERT_EQ(eval.result_.size(), 1);

    auto result = eval.result_.back();
    EXPECT_EQ(result.type_id, rt.string_type());

    nw::StringView str = rt.get_string_view(result.data.hptr);
    EXPECT_EQ(str, "5 + 3 = 8");
}

TEST_F(SmallsConstEval, FStringMixedTypes)
{
    auto& rt = nw::kernel::runtime();
    auto script = make_script(R"###(
const i = 42;
const f = 3.14;
const b = true;
const s = "world";
const msg = f"int={i}, float={f}, bool={b}, string={s}";
)###"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_msg = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[4]);
    ASSERT_NE(var_msg, nullptr);
    nw::smalls::AstConstEvaluator eval{&script, var_msg->init};
    EXPECT_FALSE(eval.failed_);
    ASSERT_EQ(eval.result_.size(), 1);

    auto result = eval.result_.back();
    EXPECT_EQ(result.type_id, rt.string_type());

    nw::StringView str = rt.get_string_view(result.data.hptr);
    EXPECT_EQ(str, "int=42, float=3.14, bool=true, string=world");
}

TEST_F(SmallsConstEval, FStringEscapeSequences)
{
    auto& rt = nw::kernel::runtime();
    auto script = make_script(R"###(
const x = 5;
const msg = f"Value in {{braces}}: {x}";
)###"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_msg = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[1]);
    ASSERT_NE(var_msg, nullptr);
    nw::smalls::AstConstEvaluator eval{&script, var_msg->init};
    EXPECT_FALSE(eval.failed_);
    ASSERT_EQ(eval.result_.size(), 1);

    auto result = eval.result_.back();
    EXPECT_EQ(result.type_id, rt.string_type());

    nw::StringView str = rt.get_string_view(result.data.hptr);
    EXPECT_EQ(str, "Value in {braces}: 5");
}

TEST_F(SmallsConstEval, CastIntToFloat)
{
    auto& rt = nw::kernel::runtime();
    auto script = make_script(R"(
const x = 42 as float;
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_x = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[0]);
    ASSERT_NE(var_x, nullptr);
    nw::smalls::AstConstEvaluator eval{&script, var_x->init};
    EXPECT_FALSE(eval.failed_);
    ASSERT_EQ(eval.result_.size(), 1);

    auto result = eval.result_.back();
    EXPECT_EQ(result.type_id, rt.float_type());
    EXPECT_FLOAT_EQ(result.data.fval, 42.0f);
}

TEST_F(SmallsConstEval, CastFloatToInt)
{
    auto& rt = nw::kernel::runtime();
    auto script = make_script(R"(
const x = 3.14 as int;
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_x = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[0]);
    ASSERT_NE(var_x, nullptr);
    nw::smalls::AstConstEvaluator eval{&script, var_x->init};
    EXPECT_FALSE(eval.failed_);
    ASSERT_EQ(eval.result_.size(), 1);

    auto result = eval.result_.back();
    EXPECT_EQ(result.type_id, rt.int_type());
    EXPECT_EQ(result.data.ival, 3);
}

TEST_F(SmallsConstEval, CastIntToBool)
{
    auto& rt = nw::kernel::runtime();
    auto script = make_script(R"(
const zero = 0 as bool;
const nonzero = 42 as bool;
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_zero = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[0]);
    ASSERT_NE(var_zero, nullptr);
    nw::smalls::AstConstEvaluator eval_zero{&script, var_zero->init};
    EXPECT_FALSE(eval_zero.failed_);
    ASSERT_EQ(eval_zero.result_.size(), 1);
    auto result_zero = eval_zero.result_.back();
    EXPECT_EQ(result_zero.type_id, rt.bool_type());
    EXPECT_FALSE(result_zero.data.bval);

    auto* var_nonzero = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[1]);
    ASSERT_NE(var_nonzero, nullptr);
    nw::smalls::AstConstEvaluator eval_nonzero{&script, var_nonzero->init};
    EXPECT_FALSE(eval_nonzero.failed_);
    ASSERT_EQ(eval_nonzero.result_.size(), 1);
    auto result_nonzero = eval_nonzero.result_.back();
    EXPECT_EQ(result_nonzero.type_id, rt.bool_type());
    EXPECT_TRUE(result_nonzero.data.bval);
}

TEST_F(SmallsConstEval, CastBoolToInt)
{
    auto& rt = nw::kernel::runtime();
    auto script = make_script(R"(
const t = true as int;
const f = false as int;
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_t = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[0]);
    ASSERT_NE(var_t, nullptr);
    nw::smalls::AstConstEvaluator eval_t{&script, var_t->init};
    EXPECT_FALSE(eval_t.failed_);
    ASSERT_EQ(eval_t.result_.size(), 1);
    auto result_t = eval_t.result_.back();
    EXPECT_EQ(result_t.type_id, rt.int_type());
    EXPECT_EQ(result_t.data.ival, 1);

    auto* var_f = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[1]);
    ASSERT_NE(var_f, nullptr);
    nw::smalls::AstConstEvaluator eval_f{&script, var_f->init};
    EXPECT_FALSE(eval_f.failed_);
    ASSERT_EQ(eval_f.result_.size(), 1);
    auto result_f = eval_f.result_.back();
    EXPECT_EQ(result_f.type_id, rt.int_type());
    EXPECT_EQ(result_f.data.ival, 0);
}

TEST_F(SmallsConstEval, IsTypeCheck)
{
    auto& rt = nw::kernel::runtime();
    auto script = make_script(R"(
const int_val = 42;
const float_val = 3.14;
const a = int_val is int;
const b = int_val is float;
const c = float_val is float;
const d = float_val is int;
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_a = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[2]);
    ASSERT_NE(var_a, nullptr);
    nw::smalls::AstConstEvaluator eval_a{&script, var_a->init};
    EXPECT_FALSE(eval_a.failed_);
    ASSERT_EQ(eval_a.result_.size(), 1);
    auto result_a = eval_a.result_.back();
    EXPECT_EQ(result_a.type_id, rt.bool_type());
    EXPECT_TRUE(result_a.data.bval);

    auto* var_b = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[3]);
    ASSERT_NE(var_b, nullptr);
    nw::smalls::AstConstEvaluator eval_b{&script, var_b->init};
    EXPECT_FALSE(eval_b.failed_);
    ASSERT_EQ(eval_b.result_.size(), 1);
    auto result_b = eval_b.result_.back();
    EXPECT_EQ(result_b.type_id, rt.bool_type());
    EXPECT_FALSE(result_b.data.bval);

    auto* var_c = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[4]);
    ASSERT_NE(var_c, nullptr);
    nw::smalls::AstConstEvaluator eval_c{&script, var_c->init};
    EXPECT_FALSE(eval_c.failed_);
    ASSERT_EQ(eval_c.result_.size(), 1);
    auto result_c = eval_c.result_.back();
    EXPECT_EQ(result_c.type_id, rt.bool_type());
    EXPECT_TRUE(result_c.data.bval);

    auto* var_d = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[5]);
    ASSERT_NE(var_d, nullptr);
    nw::smalls::AstConstEvaluator eval_d{&script, var_d->init};
    EXPECT_FALSE(eval_d.failed_);
    ASSERT_EQ(eval_d.result_.size(), 1);
    auto result_d = eval_d.result_.back();
    EXPECT_EQ(result_d.type_id, rt.bool_type());
    EXPECT_FALSE(result_d.data.bval);
}

TEST_F(SmallsConstEval, IsTypeCheckWithNegation)
{
    auto& rt = nw::kernel::runtime();
    auto script = make_script(R"(
const x = 42;
const not_float = !(x is float);
const not_int = !(x is int);
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_not_float = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[1]);
    ASSERT_NE(var_not_float, nullptr);
    nw::smalls::AstConstEvaluator eval_not_float{&script, var_not_float->init};
    EXPECT_FALSE(eval_not_float.failed_);
    ASSERT_EQ(eval_not_float.result_.size(), 1);
    auto result_not_float = eval_not_float.result_.back();
    EXPECT_EQ(result_not_float.type_id, rt.bool_type());
    EXPECT_TRUE(result_not_float.data.bval);

    auto* var_not_int = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[2]);
    ASSERT_NE(var_not_int, nullptr);
    nw::smalls::AstConstEvaluator eval_not_int{&script, var_not_int->init};
    EXPECT_FALSE(eval_not_int.failed_);
    ASSERT_EQ(eval_not_int.result_.size(), 1);
    auto result_not_int = eval_not_int.result_.back();
    EXPECT_EQ(result_not_int.type_id, rt.bool_type());
    EXPECT_FALSE(result_not_int.data.bval);
}

TEST_F(SmallsConstEval, DivisionByZeroFailsGracefully)
{
    auto script = make_script(R"(
const x = 10 / 0;
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[0]);
    ASSERT_NE(var_decl, nullptr);
    ASSERT_NE(var_decl->init, nullptr);

    nw::smalls::AstConstEvaluator eval{&script, var_decl->init};

    // Division by zero should fail constant evaluation gracefully
    EXPECT_TRUE(eval.failed_);
}

TEST_F(SmallsConstEval, ModuloByZeroFailsGracefully)
{
    auto script = make_script(R"(
const x = 10 % 0;
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[0]);
    ASSERT_NE(var_decl, nullptr);
    ASSERT_NE(var_decl->init, nullptr);

    nw::smalls::AstConstEvaluator eval{&script, var_decl->init};

    // Modulo by zero should fail constant evaluation gracefully
    EXPECT_TRUE(eval.failed_);
}

TEST_F(SmallsConstEval, FloatDivisionByZeroFailsGracefully)
{
    auto script = make_script(R"(
const x = 10.0 / 0.0;
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(script.ast().decls[0]);
    ASSERT_NE(var_decl, nullptr);
    ASSERT_NE(var_decl->init, nullptr);

    nw::smalls::AstConstEvaluator eval{&script, var_decl->init};

    // Float division by zero should fail constant evaluation gracefully
    EXPECT_TRUE(eval.failed_);
}
