#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/rules/effects.hpp>
#include <nw/smalls/GarbageCollector.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/runtime.hpp>

using namespace nw;
using namespace nw::smalls;

namespace fs = std::filesystem;

namespace {

bool heap_contains(const Runtime& runtime, HeapPtr ptr)
{
    if (ptr.value == 0) {
        return false;
    }

    HeapPtr current = runtime.heap_.all_objects();
    while (current.value != 0) {
        if (current.value == ptr.value) {
            return true;
        }
        auto* header = runtime.heap_.get_header(current);
        current = header->next_object;
    }
    return false;
}

} // namespace

struct SmallsGCTest : public testing::Test {
    SmallsGCTest()
    {
        nw::kernel::services().start();
        auto& rt = nw::kernel::runtime();
        rt.add_module_path(fs::path("stdlib/core"));
    }

    ~SmallsGCTest() override
    {
        nw::kernel::services().shutdown();
    }
};

TEST_F(SmallsGCTest, ObjectHeaderBitPacking)
{
    auto& runtime = nw::kernel::runtime();

    HeapPtr ptr = runtime.alloc_string("test");
    auto* header = runtime.heap_.get_header(ptr);

    EXPECT_EQ(header->mark_color, 0);
    EXPECT_EQ(header->generation, 0);
    EXPECT_EQ(header->age, 0);

    header->mark_color = 2;
    header->generation = 1;
    header->age = 15;

    EXPECT_EQ(header->mark_color, 2);
    EXPECT_EQ(header->generation, 1);
    EXPECT_EQ(header->age, 15);

    header->mark_color = 0;
    header->generation = 0;
    header->age = 0;

    EXPECT_EQ(header->mark_color, 0);
    EXPECT_EQ(header->generation, 0);
    EXPECT_EQ(header->age, 0);
}

TEST_F(SmallsGCTest, CardTableOperations)
{
    CardTable table;

    EXPECT_FALSE(table.is_dirty(0));
    EXPECT_FALSE(table.is_dirty(512));
    EXPECT_FALSE(table.is_dirty(1024));

    table.mark_dirty(0);
    EXPECT_TRUE(table.is_dirty(0));
    EXPECT_TRUE(table.is_dirty(100));
    EXPECT_TRUE(table.is_dirty(511));
    EXPECT_FALSE(table.is_dirty(512));

    table.mark_dirty(1024);
    EXPECT_TRUE(table.is_dirty(1024));
    EXPECT_TRUE(table.is_dirty(1500));
    EXPECT_FALSE(table.is_dirty(512));

    table.clear_card(0);
    EXPECT_FALSE(table.is_dirty(0));
    EXPECT_TRUE(table.is_dirty(1024));

    table.clear_all();
    EXPECT_FALSE(table.is_dirty(0));
    EXPECT_FALSE(table.is_dirty(1024));
}

TEST_F(SmallsGCTest, NewObjectsAreYoung)
{
    auto& runtime = nw::kernel::runtime();
    auto* gc = runtime.gc();
    ASSERT_NE(gc, nullptr);

    HeapPtr str1 = runtime.alloc_string("test1");
    HeapPtr str2 = runtime.alloc_string("test2");

    EXPECT_TRUE(gc->is_young(str1));
    EXPECT_TRUE(gc->is_young(str2));
    EXPECT_FALSE(gc->is_old(str1));
    EXPECT_FALSE(gc->is_old(str2));
}

TEST_F(SmallsGCTest, ObjectListTracking)
{
    auto& runtime = nw::kernel::runtime();

    size_t count_before = 0;
    HeapPtr current = runtime.heap_.all_objects();
    while (current.value != 0) {
        count_before++;
        auto* header = runtime.heap_.get_header(current);
        current = header->next_object;
    }

    HeapPtr str1 = runtime.alloc_string("new1");
    HeapPtr str2 = runtime.alloc_string("new2");
    HeapPtr str3 = runtime.alloc_string("new3");
    (void)str1;
    (void)str2;
    (void)str3;

    size_t count_after = 0;
    current = runtime.heap_.all_objects();
    while (current.value != 0) {
        count_after++;
        auto* header = runtime.heap_.get_header(current);
        current = header->next_object;
    }

    // Each string creates 2 allocations: StringRepr + backing data
    EXPECT_EQ(count_after - count_before, 6);
}

TEST_F(SmallsGCTest, MinorCollectionFreesUnreachable)
{
    auto& runtime = nw::kernel::runtime();
    auto* gc = runtime.gc();
    ASSERT_NE(gc, nullptr);

    uint64_t freed_before = gc->stats().objects_freed;

    {
        HeapPtr temp1 = runtime.alloc_string("temporary1");
        HeapPtr temp2 = runtime.alloc_string("temporary2");
        (void)temp1;
        (void)temp2;
    }

    gc->collect_minor();

    uint64_t freed_after = gc->stats().objects_freed;
    EXPECT_GE(freed_after, freed_before);
}

TEST_F(SmallsGCTest, MajorCollectionStats)
{
    auto& runtime = nw::kernel::runtime();
    auto* gc = runtime.gc();
    ASSERT_NE(gc, nullptr);

    uint64_t major_before = gc->stats().major_collections;

    gc->collect_major();

    uint64_t major_after = gc->stats().major_collections;
    EXPECT_EQ(major_after, major_before + 1);
}

TEST_F(SmallsGCTest, WriteBarrierDirtiesCard)
{
    auto& runtime = nw::kernel::runtime();
    auto* gc = runtime.gc();
    ASSERT_NE(gc, nullptr);

    gc->card_table().clear_all();

    HeapPtr old_obj = runtime.alloc_string("old object");
    auto* header = runtime.heap_.get_header(old_obj);
    header->generation = 1;

    HeapPtr young_obj = runtime.alloc_string("young object");

    gc->write_barrier(old_obj, young_obj);

    EXPECT_TRUE(gc->card_table().is_dirty(old_obj.value));
}

TEST_F(SmallsGCTest, WriteBarrierNoOpForYoungToYoung)
{
    auto& runtime = nw::kernel::runtime();
    auto* gc = runtime.gc();
    ASSERT_NE(gc, nullptr);

    gc->card_table().clear_all();

    HeapPtr young1 = runtime.alloc_string("young1");
    HeapPtr young2 = runtime.alloc_string("young2");

    gc->write_barrier(young1, young2);

    EXPECT_FALSE(gc->card_table().is_dirty(young1.value));
}

TEST_F(SmallsGCTest, PromotionAfterSurvival)
{
    auto& runtime = nw::kernel::runtime();
    auto* gc = runtime.gc();
    ASSERT_NE(gc, nullptr);

    GCConfig config = gc->config();
    config.promotion_threshold = 1;
    gc->set_config(config);

    HeapPtr survivor = runtime.alloc_string("survivor");
    runtime.push(Value::make_string(survivor));

    EXPECT_TRUE(gc->is_young(survivor));

    gc->collect_minor();

    auto* header = runtime.heap_.get_header(survivor);
    EXPECT_GE(header->age, 1);

    gc->collect_minor();

    EXPECT_TRUE(gc->is_old(survivor));

    runtime.pop();
}

TEST_F(SmallsGCTest, MinorCollectionKeepsLiveYoungSurvivorWithThresholdGt1)
{
    auto& runtime = nw::kernel::runtime();
    auto* gc = runtime.gc();
    ASSERT_NE(gc, nullptr);

    GCConfig config = gc->config();
    config.promotion_threshold = 2;
    gc->set_config(config);

    HeapPtr survivor = runtime.alloc_string("survivor");
    runtime.push(Value::make_string(survivor));

    ASSERT_TRUE(heap_contains(runtime, survivor));
    ASSERT_TRUE(gc->is_young(survivor));

    gc->collect_minor();

    EXPECT_TRUE(heap_contains(runtime, survivor));
    EXPECT_TRUE(gc->is_young(survivor));

    runtime.pop();
}

TEST_F(SmallsGCTest, MajorCollectionMarksOldRoots)
{
    auto& runtime = nw::kernel::runtime();
    auto* gc = runtime.gc();
    ASSERT_NE(gc, nullptr);

    HeapPtr old_root = runtime.alloc_string("old root");
    auto* header = runtime.heap_.get_header(old_root);
    header->generation = 1;

    runtime.push(Value::make_string(old_root));

    ASSERT_TRUE(heap_contains(runtime, old_root));
    ASSERT_TRUE(gc->is_old(old_root));

    gc->collect_major();

    EXPECT_TRUE(heap_contains(runtime, old_root));
    EXPECT_TRUE(gc->is_old(old_root));

    runtime.pop();
}

TEST_F(SmallsGCTest, ClosureUpvalueSurvival)
{
    auto& runtime = nw::kernel::runtime();
    auto* gc = runtime.gc();
    ASSERT_NE(gc, nullptr);

    std::string_view source = R"(
        fn make_counter(): fn(): int {
            var x = 0;
            var inner = fn(): int {
                x = x + 1;
                return x;
            };
            return inner;
        }

        fn main(): int {
            var counter = make_counter();
            var a = counter();
            var b = counter();
            return b;
        }
    )";

    auto* script = runtime.load_module_from_source("test/gc/closure", source);
    ASSERT_TRUE(script);
    ASSERT_EQ(script->errors(), 0);

    auto result = runtime.execute_script(script, "main");
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value.data.ival, 2);
}

TEST_F(SmallsGCTest, StructFieldSurvival)
{
    auto& runtime = nw::kernel::runtime();
    auto* gc = runtime.gc();
    ASSERT_NE(gc, nullptr);

    std::string_view source = R"(
        type Container {
            name: string;
            value: int;
        };

        fn main(): int {
            var c = Container { name = "test", value = 42 };
            return c.value;
        }
    )";

    auto* script = runtime.load_module_from_source("test/gc/struct_field", source);
    ASSERT_TRUE(script);
    ASSERT_EQ(script->errors(), 0);

    auto result = runtime.execute_script(script, "main");
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value.data.ival, 42);
}

TEST_F(SmallsGCTest, ArrayElementSurvival)
{
    auto& runtime = nw::kernel::runtime();
    auto* gc = runtime.gc();
    ASSERT_NE(gc, nullptr);

    std::string_view source = R"(
        fn main(): int {
            var arr: string[2];
            arr[0] = "hello";
            arr[1] = "world";
            return 2;
        }
    )";

    auto* script = runtime.load_module_from_source("test/gc/array", source);
    ASSERT_TRUE(script);
    ASSERT_EQ(script->errors(), 0);

    auto result = runtime.execute_script(script, "main");
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value.data.ival, 2);
}

TEST_F(SmallsGCTest, FixedArrayRootsSurviveMinorGC)
{
    auto& runtime = nw::kernel::runtime();
    auto* gc = runtime.gc();
    ASSERT_NE(gc, nullptr);

    std::string_view source = R"(
        import core.string as S;

        fn main(): int {
            var arr: string[2];
            arr[0] = "hello";
            arr[1] = "world";

            // Trigger GC while arr is still live on the stack.
            gc_collect();

            println(arr[0]);
            println(arr[1]);
            return S.len(arr[0]) + S.len(arr[1]);
        }
    )";

    auto* script = runtime.load_module_from_source("test/gc/fixed_array_roots", source);
    ASSERT_TRUE(script);
    ASSERT_EQ(script->errors(), 0);

    auto result = runtime.execute_script(script, "main");
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value.data.ival, 10);
}

TEST_F(SmallsGCTest, AllocationChurn)
{
    auto& runtime = nw::kernel::runtime();
    auto* gc = runtime.gc();
    ASSERT_NE(gc, nullptr);

    std::string_view source = R"(
        import core.string as S;
        fn main(): int {
            var sum = 0;
            var i = 0;
            for (i < 100) {
                var s = "temp string {i}";
                sum = sum + S.len(s);
                i = i + 1;
            }
            return sum;
        }
    )";

    auto* script = runtime.load_module_from_source("test/gc/churn", source);
    ASSERT_TRUE(script);
    ASSERT_EQ(script->errors(), 0);

    auto result = runtime.execute_script(script, "main");
    ASSERT_TRUE(result.ok());
    EXPECT_GT(result.value.data.ival, 0);
}

TEST_F(SmallsGCTest, GCStatsTracking)
{
    auto& runtime = nw::kernel::runtime();
    auto* gc = runtime.gc();
    ASSERT_NE(gc, nullptr);

    uint64_t minor_before = gc->stats().minor_collections;
    uint64_t major_before = gc->stats().major_collections;

    gc->collect_minor();
    gc->collect_minor();
    gc->collect_major();

    EXPECT_EQ(gc->stats().minor_collections, minor_before + 2);
    EXPECT_EQ(gc->stats().major_collections, major_before + 1);
}

TEST_F(SmallsGCTest, VMOwnedHandleFinalizationFiresDestructorOnceAndDeregisters)
{
    auto& runtime = nw::kernel::runtime();
    auto* gc = runtime.gc();
    ASSERT_NE(gc, nullptr);

    runtime.add_handle_type("TestHandle", nw::RuntimeObjectPool::TYPE_EFFECT);
    TypeID tid = runtime.type_id("TestHandle", false);
    ASSERT_NE(tid, invalid_type_id);

    auto called = std::make_shared<int>(0);
    runtime.register_handle_destructor(tid, [called](nw::TypedHandle, HeapPtr) {
        ++*called;
    });

    nw::TypedHandle h;
    h.id = 42;
    h.type = nw::RuntimeObjectPool::TYPE_EFFECT;
    h.generation = 1;

    HeapPtr ptr = runtime.intern_handle(h, OwnershipMode::VM_OWNED);
    ASSERT_NE(ptr.value, 0);
    ASSERT_EQ(runtime.lookup_handle(h).value, ptr.value);
    ASSERT_TRUE(heap_contains(runtime, ptr));

    gc->collect_minor();

    EXPECT_EQ(*called, 1);
    EXPECT_EQ(runtime.lookup_handle(h).value, 0);
    EXPECT_FALSE(heap_contains(runtime, ptr));

    gc->collect_minor();
    EXPECT_EQ(*called, 1);
}

TEST_F(SmallsGCTest, EngineOwnedAndBorrowedHandlesSurviveGCAndNeverFinalize)
{
    auto& runtime = nw::kernel::runtime();
    auto* gc = runtime.gc();
    ASSERT_NE(gc, nullptr);

    runtime.add_handle_type("TestHandle2", nw::RuntimeObjectPool::TYPE_EFFECT);
    TypeID tid = runtime.type_id("TestHandle2", false);
    ASSERT_NE(tid, invalid_type_id);

    auto called = std::make_shared<int>(0);
    runtime.register_handle_destructor(tid, [called](nw::TypedHandle, HeapPtr) {
        ++*called;
    });

    auto make_handle = []() {
        nw::TypedHandle h;
        h.id = 7;
        h.type = nw::RuntimeObjectPool::TYPE_EFFECT;
        h.generation = 1;
        return h;
    };

    // ENGINE_OWNED
    {
        nw::TypedHandle h = make_handle();
        HeapPtr ptr = runtime.intern_handle(h, OwnershipMode::ENGINE_OWNED);
        ASSERT_NE(ptr.value, 0);

        gc->collect_minor();
        gc->collect_major();

        EXPECT_EQ(*called, 0);
        EXPECT_EQ(runtime.lookup_handle(h).value, ptr.value);
        EXPECT_TRUE(heap_contains(runtime, ptr));
    }

    // BORROWED
    {
        nw::TypedHandle h = make_handle();
        h.id = 8;
        HeapPtr ptr = runtime.intern_handle(h, OwnershipMode::BORROWED);
        ASSERT_NE(ptr.value, 0);

        gc->collect_minor();
        gc->collect_major();

        EXPECT_EQ(*called, 0);
        EXPECT_EQ(runtime.lookup_handle(h).value, ptr.value);
        EXPECT_TRUE(heap_contains(runtime, ptr));
    }
}

TEST_F(SmallsGCTest, VMOwnedEffectHandleFinalizerDestroysEngineEffect)
{
    auto& runtime = nw::kernel::runtime();
    auto* gc = runtime.gc();
    ASSERT_NE(gc, nullptr);

    runtime.add_handle_type("TestEffect", nw::RuntimeObjectPool::TYPE_EFFECT);
    TypeID tid = runtime.type_id("TestEffect", false);
    ASSERT_NE(tid, invalid_type_id);

    auto destroyed = std::make_shared<int>(0);
    runtime.register_handle_destructor(tid, [destroyed](nw::TypedHandle h, HeapPtr) {
        auto& es = nw::kernel::effects();
        if (auto* eff = es.get(h)) {
            es.destroy(eff);
            ++*destroyed;
        }
    });

    auto* eff = nw::kernel::effects().create(nw::EffectType::invalid());
    ASSERT_NE(eff, nullptr);
    nw::TypedHandle h = eff->handle().to_typed_handle();

    HeapPtr ptr = runtime.intern_handle(h, OwnershipMode::VM_OWNED);
    ASSERT_NE(ptr.value, 0);

    gc->collect_minor();

    EXPECT_EQ(*destroyed, 1);
    EXPECT_EQ(runtime.lookup_handle(h).value, 0);
    EXPECT_EQ(nw::kernel::effects().get(h), nullptr);
}
