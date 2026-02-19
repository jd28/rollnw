#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/rules/RuntimeObject.hpp>
#include <nw/smalls/UnmanagedArray.hpp>
#include <nw/smalls/runtime.hpp>

#include <gtest/gtest.h>

#include <filesystem>

namespace nwk = nw::kernel;
namespace fs = std::filesystem;

class UnmanagedArrayTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Ensure kernel is initialized
        ASSERT_TRUE(nwk::initialized());
    }
};

// == Basic UnmanagedArray Tests =================================================

TEST_F(UnmanagedArrayTest, BasicIntArrayOperations)
{
    using namespace nw::smalls;

    RuntimeObjectPool pool;

    // Allocate an int array
    TypedHandle h = pool.allocate_unmanaged_array(TypeID{1}, 4); // TypeID 1 = int
    EXPECT_TRUE(h.is_valid());
    EXPECT_EQ(h.type, RuntimeObjectPool::TYPE_UNMANAGED_ARRAY);

    // Get the array
    IArray* arr = pool.get_unmanaged_array(h);
    EXPECT_NE(arr, nullptr);

    // Check initial state
    EXPECT_EQ(arr->size(), 0);
    EXPECT_EQ(arr->capacity(), 4);

    // Test append
    Runtime& rt = nwk::runtime();
    Value val = Value::make_int(42);
    arr->append_value(val, rt);
    EXPECT_EQ(arr->size(), 1);

    // Test get
    Value out;
    EXPECT_TRUE(arr->get_value(0, out, rt));
    EXPECT_EQ(out.data.ival, 42);

    // Cleanup
    pool.destroy_unmanaged_array(h);
}

TEST_F(UnmanagedArrayTest, ArrayGrowthBehavior)
{
    using namespace nw::smalls;

    RuntimeObjectPool pool;
    Runtime& rt = nwk::runtime();

    TypedHandle h = pool.allocate_unmanaged_array(TypeID{1}, 0); // Start with capacity 0
    IArray* arr = pool.get_unmanaged_array(h);

    // Append many values to trigger growth
    for (int i = 0; i < 100; ++i) {
        Value val = Value::make_int(i);
        arr->append_value(val, rt);
    }

    EXPECT_EQ(arr->size(), 100);
    EXPECT_GE(arr->capacity(), 100);

    // Verify all values
    for (int i = 0; i < 100; ++i) {
        Value out;
        EXPECT_TRUE(arr->get_value(i, out, rt));
        EXPECT_EQ(out.data.ival, i);
    }

    pool.destroy_unmanaged_array(h);
}

TEST_F(UnmanagedArrayTest, FloatArrayOperations)
{
    using namespace nw::smalls;

    RuntimeObjectPool pool;
    Runtime& rt = nwk::runtime();

    TypedHandle h = pool.allocate_unmanaged_array(TypeID{2}, 4); // TypeID 2 = float
    IArray* arr = pool.get_unmanaged_array(h);

    // Append float values
    for (int i = 0; i < 10; ++i) {
        Value val = Value::make_float(static_cast<float>(i) * 1.5f);
        arr->append_value(val, rt);
    }

    EXPECT_EQ(arr->size(), 10);

    // Verify
    Value out;
    EXPECT_TRUE(arr->get_value(5, out, rt));
    EXPECT_FLOAT_EQ(out.data.fval, 7.5f);

    pool.destroy_unmanaged_array(h);
}

TEST_F(UnmanagedArrayTest, BoolArrayOperations)
{
    using namespace nw::smalls;

    RuntimeObjectPool pool;
    Runtime& rt = nwk::runtime();

    TypedHandle h = pool.allocate_unmanaged_array(TypeID{3}, 4); // TypeID 3 = bool
    IArray* arr = pool.get_unmanaged_array(h);

    // Append alternating true/false
    for (int i = 0; i < 10; ++i) {
        Value val = Value::make_bool(i % 2 == 0);
        arr->append_value(val, rt);
    }

    Value out;
    EXPECT_TRUE(arr->get_value(0, out, rt));
    EXPECT_TRUE(out.data.bval);
    EXPECT_TRUE(arr->get_value(1, out, rt));
    EXPECT_FALSE(out.data.bval);

    pool.destroy_unmanaged_array(h);
}

TEST_F(UnmanagedArrayTest, ClearAndResizeOperations)
{
    using namespace nw::smalls;

    RuntimeObjectPool pool;
    Runtime& rt = nwk::runtime();

    TypedHandle h = pool.allocate_unmanaged_array(TypeID{1}, 4);
    IArray* arr = pool.get_unmanaged_array(h);

    // Add values
    for (int i = 0; i < 10; ++i) {
        Value val = Value::make_int(i);
        arr->append_value(val, rt);
    }

    // Clear
    arr->clear();
    EXPECT_EQ(arr->size(), 0);
    EXPECT_GE(arr->capacity(), 10); // Capacity shouldn't shrink

    // Resize
    arr->resize(20);
    EXPECT_EQ(arr->size(), 20);

    // Check that new elements are zeroed
    Value out;
    EXPECT_TRUE(arr->get_value(15, out, rt));
    EXPECT_EQ(out.data.ival, 0);

    pool.destroy_unmanaged_array(h);
}

TEST_F(UnmanagedArrayTest, SetValueOperations)
{
    using namespace nw::smalls;

    RuntimeObjectPool pool;
    Runtime& rt = nwk::runtime();

    TypedHandle h = pool.allocate_unmanaged_array(TypeID{1}, 4);
    IArray* arr = pool.get_unmanaged_array(h);

    // Resize to make room
    arr->resize(10);

    // Set specific values
    for (int i = 0; i < 10; ++i) {
        Value val = Value::make_int(i * 10);
        EXPECT_TRUE(arr->set_value(i, val, rt));
    }

    // Verify
    for (int i = 0; i < 10; ++i) {
        Value out;
        EXPECT_TRUE(arr->get_value(i, out, rt));
        EXPECT_EQ(out.data.ival, i * 10);
    }

    // Out of bounds should fail
    Value val = Value::make_int(999);
    EXPECT_FALSE(arr->set_value(20, val, rt));

    pool.destroy_unmanaged_array(h);
}

// == Handle Validation Tests ===================================================

TEST_F(UnmanagedArrayTest, HandleValidation)
{
    using namespace nw::smalls;

    RuntimeObjectPool pool;

    // Allocate and destroy
    TypedHandle h = pool.allocate_unmanaged_array(TypeID{1}, 4);
    EXPECT_TRUE(pool.valid_unmanaged_array(h));

    pool.destroy_unmanaged_array(h);
    EXPECT_FALSE(pool.valid_unmanaged_array(h));

    // Getting destroyed handle should return nullptr
    EXPECT_EQ(pool.get_unmanaged_array(h), nullptr);
}

TEST_F(UnmanagedArrayTest, MultipleArraysIndependent)
{
    using namespace nw::smalls;

    RuntimeObjectPool pool;
    Runtime& rt = nwk::runtime();

    // Create multiple arrays
    TypedHandle h1 = pool.allocate_unmanaged_array(TypeID{1}, 4);
    TypedHandle h2 = pool.allocate_unmanaged_array(TypeID{1}, 4);
    TypedHandle h3 = pool.allocate_unmanaged_array(TypeID{1}, 4);

    IArray* arr1 = pool.get_unmanaged_array(h1);
    IArray* arr2 = pool.get_unmanaged_array(h2);
    IArray* arr3 = pool.get_unmanaged_array(h3);

    // Populate independently
    for (int i = 0; i < 5; ++i) {
        arr1->append_value(Value::make_int(i), rt);
        arr2->append_value(Value::make_int(i * 10), rt);
        arr3->append_value(Value::make_int(i * 100), rt);
    }

    // Verify independence
    for (int i = 0; i < 5; ++i) {
        Value v1, v2, v3;
        arr1->get_value(i, v1, rt);
        arr2->get_value(i, v2, rt);
        arr3->get_value(i, v3, rt);

        EXPECT_EQ(v1.data.ival, i);
        EXPECT_EQ(v2.data.ival, i * 10);
        EXPECT_EQ(v3.data.ival, i * 100);
    }

    pool.destroy_unmanaged_array(h1);
    pool.destroy_unmanaged_array(h2);
    pool.destroy_unmanaged_array(h3);
}

// == Value Storage Tests ======================================================

TEST_F(UnmanagedArrayTest, ValueMakeUnmanagedArray)
{
    using namespace nw::smalls;

    RuntimeObjectPool pool;

    TypedHandle h = pool.allocate_unmanaged_array(TypeID{1}, 4);

    // Create value using the factory
    TypeID array_type = nwk::runtime().array_type(TypeID{1});
    Value val = Value::make_unmanaged_array(h, array_type);

    EXPECT_EQ(val.type_id, array_type);
    EXPECT_EQ(val.data.handle, h.to_ull());

    // Verify we can extract the handle
    TypedHandle extracted = TypedHandle::from_ull(val.data.handle);
    EXPECT_EQ(extracted.id, h.id);
    EXPECT_EQ(extracted.type, h.type);
    EXPECT_EQ(extracted.generation, h.generation);

    pool.destroy_unmanaged_array(h);
}

// == Type Flags Tests =========================================================

TEST_F(UnmanagedArrayTest, TypeFlagsForUnmanagedArrays)
{
    using namespace nw::smalls;

    RuntimeObjectPool pool;

    TypedHandle h = pool.allocate_unmanaged_array(TypeID{1}, 4);
    EXPECT_EQ(h.type, RuntimeObjectPool::TYPE_UNMANAGED_ARRAY);

    pool.destroy_unmanaged_array(h);
}

// == Stress Tests =============================================================

TEST_F(UnmanagedArrayTest, StressManyArrays)
{
    using namespace nw::smalls;

    RuntimeObjectPool pool;
    Runtime& rt = nwk::runtime();

    constexpr int k_num_arrays = 1000;
    constexpr int k_elements_per_array = 100;

    std::vector<TypedHandle> handles;
    handles.reserve(k_num_arrays);

    // Allocate many arrays
    for (int i = 0; i < k_num_arrays; ++i) {
        TypedHandle h = pool.allocate_unmanaged_array(TypeID{1}, 4);
        handles.push_back(h);
    }

    // Populate each array
    for (int i = 0; i < k_num_arrays; ++i) {
        IArray* arr = pool.get_unmanaged_array(handles[i]);
        for (int j = 0; j < k_elements_per_array; ++j) {
            arr->append_value(Value::make_int(i * k_elements_per_array + j), rt);
        }
    }

    // Verify all values
    for (int i = 0; i < k_num_arrays; ++i) {
        IArray* arr = pool.get_unmanaged_array(handles[i]);
        EXPECT_EQ(arr->size(), k_elements_per_array);

        for (int j = 0; j < k_elements_per_array; ++j) {
            Value out;
            EXPECT_TRUE(arr->get_value(j, out, rt));
            EXPECT_EQ(out.data.ival, i * k_elements_per_array + j);
        }
    }

    // Destroy all
    for (auto h : handles) {
        pool.destroy_unmanaged_array(h);
    }
}

// == Integration with Runtime Tests ===========================================

TEST_F(UnmanagedArrayTest, RuntimeObjectPoolIntegration)
{
    using namespace nw::smalls;

    // Access pool through runtime
    nw::RuntimeObjectPool& pool = nwk::runtime().object_pool();

    TypedHandle h = pool.allocate_unmanaged_array(TypeID{1}, 4);
    EXPECT_TRUE(h.is_valid());

    IArray* arr = pool.get_unmanaged_array(h);
    EXPECT_NE(arr, nullptr);

    pool.destroy_unmanaged_array(h);
}

// == Main Test Runner =========================================================

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
