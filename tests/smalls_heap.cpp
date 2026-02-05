#include "nw/smalls/Smalls.hpp"
#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/smalls/runtime.hpp>

using namespace nw;
using namespace nw::smalls;

struct SmallsHeapTest : public testing::Test {
    SmallsHeapTest()
    {
        nw::kernel::services().start();
    }

    ~SmallsHeapTest() override
    {
        nw::kernel::services().shutdown();
    }
};

TEST_F(SmallsHeapTest, CppLayoutCompatibility)
{
    // C++ struct with known layout
    struct TestStruct {
        int32_t a; // offset 0
        bool b;    // offset 4
        // 3 bytes padding
        int32_t c; // offset 8
    }; // sizeof = 12, alignof = 4

    // Verify C++ layout assumptions
    EXPECT_EQ(sizeof(TestStruct), 12);
    EXPECT_EQ(alignof(TestStruct), 4);
    EXPECT_EQ(offsetof(TestStruct, a), 0);
    EXPECT_EQ(offsetof(TestStruct, b), 4);
    EXPECT_EQ(offsetof(TestStruct, c), 8);

    // Create script with matching struct definition
    std::string_view source = R"(
        type TestStruct {
            a: int;
            b: bool;
            c: int;
        };
    )";

    auto& runtime = nw::kernel::runtime();
    auto* script = runtime.load_module_from_source("test/heap/layout", source);
    ASSERT_TRUE(script);
    ASSERT_EQ(script->errors(), 0);

    // Debug: print all registered type names
    LOG_F(INFO, "Registered types:");
    for (size_t i = 0; i < runtime.type_table_.types_.size(); ++i) {
        const auto& t = runtime.type_table_.types_[i];
        LOG_F(INFO, "  [{}] {} (kind={})", i, t.name.view().data(), int(t.type_kind));
    }

    // Get the struct type (fully qualified with module path)
    const Type* type = runtime.get_type("test.heap.layout.TestStruct");
    ASSERT_TRUE(type);
    ASSERT_EQ(int(type->type_kind), int(TK_struct));

    // Verify size and alignment match C++
    EXPECT_EQ(type->size, sizeof(TestStruct));
    EXPECT_EQ(type->alignment, alignof(TestStruct));

    // Get struct def and verify field offsets
    auto struct_id = type->type_params[0].as<StructID>();
    const StructDef* def = runtime.type_table_.get(struct_id);
    ASSERT_TRUE(def);
    ASSERT_EQ(def->field_count, 3);

    // Verify each field offset matches C++
    EXPECT_EQ(def->fields[0].offset, offsetof(TestStruct, a));
    EXPECT_EQ(def->fields[1].offset, offsetof(TestStruct, b));
    EXPECT_EQ(def->fields[2].offset, offsetof(TestStruct, c));
}

TEST_F(SmallsHeapTest, CppLayoutWithHeapPtr)
{
    // C++ struct using HeapPtr for reference types (strings use view-based layout)
    struct ConfigData {
        int32_t width;     // offset 0, size 4
        int32_t height;    // offset 4, size 4
        HeapPtr title;     // offset 8, size 4 (points to StringRepr)
        bool fullscreen;   // offset 12, size 1
        // 3 bytes padding
    }; // sizeof = 16, alignof = 4

    // Verify C++ layout assumptions
    EXPECT_EQ(sizeof(HeapPtr), 4);
    EXPECT_EQ(sizeof(ConfigData), 16);
    EXPECT_EQ(alignof(ConfigData), 4);
    EXPECT_EQ(offsetof(ConfigData, width), 0);
    EXPECT_EQ(offsetof(ConfigData, height), 4);
    EXPECT_EQ(offsetof(ConfigData, title), 8);
    EXPECT_EQ(offsetof(ConfigData, fullscreen), 12);

    auto& runtime = nw::kernel::runtime();

    // Allocate and populate a ConfigData struct
    HeapPtr title_ptr = runtime.alloc_string("My Game");
    HeapPtr struct_ptr = runtime.heap_.allocate(sizeof(ConfigData), alignof(ConfigData));

    ConfigData* config = static_cast<ConfigData*>(runtime.heap_.get_ptr(struct_ptr));
    config->width = 1920;
    config->height = 1080;
    config->title = title_ptr;
    config->fullscreen = true;

    // Verify we can read back the data
    EXPECT_EQ(config->width, 1920);
    EXPECT_EQ(config->height, 1080);
    EXPECT_TRUE(config->fullscreen);

    // Verify string can be accessed via the accessor
    StringView title_sv = runtime.get_string_view(config->title);
    EXPECT_EQ(title_sv, "My Game");
}

TEST_F(SmallsHeapTest, AlignedAllocation)
{
    auto& runtime = nw::kernel::runtime();

    // Allocate with various alignments
    HeapPtr ptr8 = runtime.heap_.allocate(16, 8);
    void* raw8 = runtime.heap_.get_ptr(ptr8);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(raw8) % 8, 0);

    HeapPtr ptr16 = runtime.heap_.allocate(16, 16);
    void* raw16 = runtime.heap_.get_ptr(ptr16);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(raw16) % 16, 0);

    HeapPtr ptr32 = runtime.heap_.allocate(16, 32);
    void* raw32 = runtime.heap_.get_ptr(ptr32);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(raw32) % 32, 0);
}

TEST_F(SmallsHeapTest, StringAllocation)
{
    auto& runtime = nw::kernel::runtime();

    HeapPtr ptr = runtime.alloc_string("Hello, World!");
    StringView sv = runtime.get_string_view(ptr);

    EXPECT_EQ(sv, "Hello, World!");
}

TEST_F(SmallsHeapTest, StringViewAccess)
{
    auto& runtime = nw::kernel::runtime();

    HeapPtr str_ptr = runtime.alloc_string("Test String");

    // Verify string can be accessed via accessor
    StringView sv = runtime.get_string_view(str_ptr);
    EXPECT_EQ(sv, "Test String");

    // Verify length accessor
    EXPECT_EQ(runtime.get_string_length(str_ptr), 11);

    // Verify data accessor
    const char* data = runtime.get_string_data(str_ptr);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(StringView(data, 11), "Test String");
}
