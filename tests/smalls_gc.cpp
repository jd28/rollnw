#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/rules/effects.hpp>
#include <nw/smalls/GarbageCollector.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/VirtualMachine.hpp>
#include <nw/smalls/runtime.hpp>

#include <stdexcept>
#include <unordered_set>

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

bool heap_list_is_well_formed(const Runtime& runtime)
{
    HeapPtr current = runtime.heap_.all_objects();
    size_t steps = 0;
    constexpr size_t step_limit = 1'000'000;
    std::unordered_set<uint32_t> visited;

    while (current.value != 0) {
        if (++steps > step_limit) {
            return false;
        }
        if (!visited.insert(current.value).second) {
            return false;
        }

        auto* header = runtime.heap_.try_get_header(current);
        if (!header) {
            return false;
        }

        if (header->alloc_size == 0) {
            return false;
        }

        current = header->next_object;
    }

    return true;
}

bool finish_minor_collection(Runtime& runtime, GarbageCollector& gc)
{
    // One unit advances one root/object work item. Profile-owned config grows
    // this batch, so derive the safety bound from the live allocation volume
    // instead of assuming a fixed startup corpus.
    const size_t step_limit = runtime.heap_.alloc_count() * 8 + 4096;
    for (size_t step = 0; step < step_limit; ++step) {
        if (gc.collect_minor_step(1)) { return true; }
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

TEST_F(SmallsGCTest, ScopedRootsRetainValuesAcrossMinorAndMajorCollections)
{
    auto& runtime = nw::kernel::runtime();
    auto* gc = runtime.gc();
    ASSERT_NE(gc, nullptr);

    HeapPtr value = runtime.alloc_string("scoped root");
    {
        Runtime::ScopedRoots roots{runtime, 1};
        roots.add(Value::make_string(value));

        gc->collect_minor();
        EXPECT_TRUE(heap_contains(runtime, value));

        gc->collect_major();
        EXPECT_TRUE(heap_contains(runtime, value));
        EXPECT_FALSE(gc->runtime_stack_scan_active());
    }

    EXPECT_TRUE(runtime.stack_empty());
    gc->collect_major();
    EXPECT_FALSE(heap_contains(runtime, value));
}

TEST_F(SmallsGCTest, ScopedRootsRestoreNestedMarkers)
{
    auto& runtime = nw::kernel::runtime();
    runtime.push(Value::make_int(7));

    HeapPtr outer_value = runtime.alloc_string("outer");
    HeapPtr inner_value = runtime.alloc_string("inner");
    {
        Runtime::ScopedRoots outer{runtime, 1};
        outer.add(Value::make_string(outer_value));
        EXPECT_EQ(runtime.top().data.hptr.value, outer_value.value);

        {
            Runtime::ScopedRoots inner{runtime, 1};
            inner.add(Value::make_string(inner_value));
            EXPECT_EQ(runtime.top().data.hptr.value, inner_value.value);
        }

        EXPECT_EQ(runtime.top().data.hptr.value, outer_value.value);
    }

    EXPECT_EQ(runtime.top().data.ival, 7);
    runtime.pop();
    EXPECT_TRUE(runtime.stack_empty());
}

TEST_F(SmallsGCTest, ScopedRootsRestoreMarkerDuringExceptionUnwinding)
{
    auto& runtime = nw::kernel::runtime();
    runtime.push(Value::make_int(11));

    try {
        Runtime::ScopedRoots roots{runtime, 1};
        roots.add(Value::make_string(runtime.alloc_string("temporary")));
        throw std::runtime_error{"test"};
    } catch (const std::runtime_error&) {
    }

    ASSERT_FALSE(runtime.stack_empty());
    EXPECT_EQ(runtime.top().data.ival, 11);
    runtime.pop();
    EXPECT_TRUE(runtime.stack_empty());
}

TEST_F(SmallsGCTest, AllocationHelpersRootIntermediateObjectsDuringEmergencyCollection)
{
    auto& runtime = nw::kernel::runtime();
    auto* gc = runtime.gc();
    ASSERT_NE(gc, nullptr);

    auto* script = runtime.load_module_from_source("test.gc_rooted_allocations", R"(
        [[value_type]]
        type RootedContainer {
            text: string;
        };
    )");
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    TypeID struct_type = runtime.type_id("test.gc_rooted_allocations.RootedContainer", false);
    ASSERT_NE(struct_type, invalid_type_id);
    TypeID tuple_type = runtime.register_tuple_type(Vector<TypeID>{runtime.string_type()});
    ASSERT_NE(tuple_type, invalid_type_id);

    HeapPtr normal_object = runtime.alloc_struct(struct_type);
    const StructDef* normal_struct_def = runtime.get_struct_def(struct_type);
    ASSERT_NE(normal_struct_def, nullptr);
    ASSERT_NE(runtime.read_struct_field_by_index(normal_object, normal_struct_def, 0).data.hptr.value, 0);

    gc->collect_major();
    constexpr size_t emergency_threshold = ScriptHeap::max_node_allocs / 2;
    ASSERT_LT(runtime.heap_.alloc_count(), emergency_threshold);

    const size_t filler_count = emergency_threshold + 1 - runtime.heap_.alloc_count();
    Runtime::ScopedRoots roots{runtime, filler_count + 3};
    for (size_t i = 0; i < filler_count; ++i) {
        HeapPtr filler = runtime.heap_.allocate(sizeof(int32_t), alignof(int32_t), runtime.int_type());
        roots.add(Value::make_heap(filler, runtime.int_type()));
    }
    ASSERT_GT(runtime.heap_.alloc_count(), emergency_threshold);

    HeapPtr text = runtime.alloc_string("survives emergency collection");
    ASSERT_TRUE(heap_contains(runtime, text));
    EXPECT_EQ(runtime.get_string_view(text), "survives emergency collection");
    roots.add(Value::make_string(text));

    HeapPtr object = runtime.alloc_struct(struct_type);
    ASSERT_TRUE(heap_contains(runtime, object));
    const StructDef* struct_def = runtime.get_struct_def(struct_type);
    ASSERT_NE(struct_def, nullptr);
    Value struct_text = runtime.read_struct_field_by_index(object, struct_def, 0);
    ASSERT_EQ(struct_text.storage, ValueStorage::heap);
    ASSERT_NE(struct_text.data.hptr.value, 0);
    EXPECT_TRUE(heap_contains(runtime, struct_text.data.hptr));
    roots.add(Value::make_heap(object, struct_type));

    HeapPtr tuple = runtime.alloc_tuple(tuple_type);
    ASSERT_TRUE(heap_contains(runtime, tuple));
    const Type* tuple_type_info = runtime.get_type(tuple_type);
    ASSERT_NE(tuple_type_info, nullptr);
    const TupleDef* tuple_def = runtime.type_table_.get(tuple_type_info->type_params[0].as<TupleID>());
    ASSERT_NE(tuple_def, nullptr);
    Value tuple_text = runtime.read_tuple_element_by_index(tuple, tuple_def, 0);
    ASSERT_EQ(tuple_text.storage, ValueStorage::heap);
    EXPECT_EQ(tuple_text.data.hptr.value, 0);
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

TEST_F(SmallsGCTest, AllocationDuringIncrementalMajorGCSurvives)
{
    auto& runtime = nw::kernel::runtime();
    auto* gc = runtime.gc();
    ASSERT_NE(gc, nullptr);

    gc->start_major_gc();
    ASSERT_EQ(gc->phase(), GCPhase::mark_incremental);

    HeapPtr ptr = runtime.alloc_string("alloc during major");
    ASSERT_NE(ptr.value, 0);
    ASSERT_TRUE(heap_contains(runtime, ptr));

    if (gc->phase() == GCPhase::mark_incremental) {
        while (!gc->mark_step(1024)) {
        }
        gc->finish_major_gc();
    }

    runtime.push(Value::make_string(ptr));
    gc->collect_major();
    EXPECT_TRUE(heap_contains(runtime, ptr));

    runtime.pop();
    gc->collect_major();
    EXPECT_FALSE(heap_contains(runtime, ptr));
}

// Regression for the incremental-major use-after-free: a white object that
// becomes reachable *only* through a root added after start_major_gc()'s
// initial root scan must survive the cycle. The Dijkstra write barrier does
// not cover root slots, so correctness relies on finish_major_gc() re-scanning
// roots before sweeping. Pre-fix this object was swept while still referenced
// from the runtime value stack (a GC root) -> use-after-free.
TEST_F(SmallsGCTest, RootReachableObjectSurvivesIncrementalMajorGC)
{
    auto& runtime = nw::kernel::runtime();
    auto* gc = runtime.gc();
    ASSERT_NE(gc, nullptr);

    // Allocated before the cycle and not reachable from any root, so
    // start_major_gc() colors it white and does not mark it.
    HeapPtr ptr = runtime.alloc_string("root-only during incremental major");
    ASSERT_NE(ptr.value, 0);

    gc->start_major_gc();
    ASSERT_EQ(gc->phase(), GCPhase::mark_incremental);
    ASSERT_EQ(runtime.heap_.get_header(ptr)->mark_color,
        static_cast<uint8_t>(MarkColor::WHITE));

    // Mutator, mid-cycle: the object becomes reachable through the runtime
    // value stack, a root that was already scanned. No barrier fires (it is a
    // root slot), and this is now the object's only reference.
    runtime.push(Value::make_string(ptr));

    // Drain incremental marking and terminate the cycle. mark_step() only
    // drains the gray stack; it never re-scans roots, so `ptr` stays white
    // until finish_major_gc() re-scans.
    while (!gc->mark_step(1024)) {
    }
    gc->finish_major_gc();

    EXPECT_TRUE(heap_contains(runtime, ptr));
    EXPECT_TRUE(heap_list_is_well_formed(runtime));

    // Correctly reclaimed once the root is gone.
    runtime.pop();
    gc->collect_major();
    EXPECT_FALSE(heap_contains(runtime, ptr));
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

TEST_F(SmallsGCTest, NewtypeWrappedStringSurvival)
{
    auto& runtime = nw::kernel::runtime();
    auto* gc = runtime.gc();
    ASSERT_NE(gc, nullptr);

    std::string_view source = R"(
        import core.string as S;

        type Name(string);

        var saved: Name;

        fn main(): int {
            saved = Name("persistent");
            gc_collect();
            gc_collect();

            return S.len(saved as string);
        }
    )";

    auto* script = runtime.load_module_from_source("test/gc/newtype_string", source);
    ASSERT_TRUE(script);
    ASSERT_EQ(script->errors(), 0);

    auto result = runtime.execute_script(script, "main");
    ASSERT_TRUE(result.ok()) << result.error_message;
    EXPECT_EQ(result.value.data.ival, 10);
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

TEST_F(SmallsGCTest, ValueTypeArrayTracesInlineHeapReferences)
{
    auto& runtime = nw::kernel::runtime();
    auto* gc = runtime.gc();
    ASSERT_NE(gc, nullptr);

    constexpr StringView source = R"(
        [[value_type]]
        type Named {
            id: int;
            name: string;
        };
    )";

    auto* script = runtime.load_module_from_source("test/gc/value_type_array", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    TypeID named_type = runtime.type_id("test.gc.value_type_array.Named", false);
    ASSERT_NE(named_type, invalid_type_id);
    EXPECT_FALSE(runtime.type_table_.is_heap_type(named_type));

    HeapPtr element = runtime.alloc_struct(named_type);
    HeapPtr name = runtime.alloc_string("persistent");
    ASSERT_TRUE(runtime.write_struct_field(element, named_type, "id", Value::make_int(0)));
    ASSERT_TRUE(runtime.write_struct_field(element, named_type, "name", Value::make_string(name)));

    HeapPtr array = runtime.create_array_typed(named_type, 1);
    IArray* values = runtime.get_array_typed(array);
    ASSERT_NE(values, nullptr);
    values->resize(1);
    ASSERT_TRUE(values->set_value(0, Value::make_heap(element, named_type), runtime));

    TypeID array_type = runtime.heap_.get_header(array)->type_id;
    runtime.push(Value::make_heap(array, array_type));
    gc->collect_minor();

    EXPECT_TRUE(heap_contains(runtime, name));
    EXPECT_EQ(runtime.get_string_view(name), "persistent");

    runtime.pop();
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
    EXPECT_GE(gc->stats().minor_roots_time_us, 0u);
    EXPECT_GE(gc->stats().minor_trace_time_us, 0u);
    EXPECT_GE(gc->stats().minor_sweep_promote_time_us, 0u);
}

TEST_F(SmallsGCTest, MinorStepCollectsUnreachableYoung)
{
    auto& runtime = nw::kernel::runtime();
    auto* gc = runtime.gc();
    ASSERT_NE(gc, nullptr);

    HeapPtr tmp = runtime.alloc_string("minor-step-temp");
    ASSERT_NE(tmp.value, 0);
    ASSERT_TRUE(heap_contains(runtime, tmp));

    const bool done = finish_minor_collection(runtime, *gc);
    ASSERT_TRUE(done);
    EXPECT_FALSE(heap_contains(runtime, tmp));
}

TEST_F(SmallsGCTest, RememberedOldObjectKeepsYoungChildAlive)
{
    auto& runtime = nw::kernel::runtime();
    auto* gc = runtime.gc();
    ASSERT_NE(gc, nullptr);

    HeapPtr old_arr = runtime.alloc_array(runtime.string_type(), 1);
    ASSERT_NE(old_arr.value, 0);
    auto* old_header = runtime.heap_.get_header(old_arr);
    old_header->generation = 1;

    HeapPtr young_child = runtime.alloc_string("young-child");
    ASSERT_NE(young_child.value, 0);

    TypeID old_arr_type = old_header->type_id;
    runtime.push(Value::make_heap(old_arr, old_arr_type));

    ASSERT_TRUE(runtime.array_set(old_arr, 0, Value::make_string(young_child)));

    const bool done = finish_minor_collection(runtime, *gc);
    ASSERT_TRUE(done);
    EXPECT_TRUE(heap_contains(runtime, young_child));

    runtime.pop();
    gc->collect_major();
}

TEST_F(SmallsGCTest, MinorSweepHandlesInterleavedOldYoungAllObjects)
{
    auto& runtime = nw::kernel::runtime();
    auto* gc = runtime.gc();
    ASSERT_NE(gc, nullptr);

    HeapPtr y1 = runtime.alloc_string("y1");
    HeapPtr o1 = runtime.alloc_string("o1");
    HeapPtr y2 = runtime.alloc_string("y2");
    HeapPtr o2 = runtime.alloc_string("o2");

    ASSERT_NE(y1.value, 0);
    ASSERT_NE(o1.value, 0);
    ASSERT_NE(y2.value, 0);
    ASSERT_NE(o2.value, 0);

    runtime.heap_.get_header(o1)->generation = 1;
    runtime.heap_.get_header(o2)->generation = 1;

    runtime.push(Value::make_heap(y1, runtime.string_type()));

    const bool done = finish_minor_collection(runtime, *gc);
    ASSERT_TRUE(done);

    EXPECT_TRUE(heap_contains(runtime, y1));
    EXPECT_FALSE(heap_contains(runtime, y2));
    EXPECT_TRUE(heap_contains(runtime, o1));
    EXPECT_TRUE(heap_contains(runtime, o2));
    EXPECT_TRUE(heap_list_is_well_formed(runtime));

    runtime.pop();
    gc->collect_major();
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

// Engine handle types are stored as a HeapPtr and must be traced like any other
// heap reference. Before TK_handle existed they were registered with no storage
// layout at all, which both barred them from struct fields and -- had a size
// simply been added -- would have left them untraced inside one.
//
// The checks below pair with VMOwnedEffectHandleFinalizerDestroysEngineEffect
// above: that one proves an unreferenced handle IS collected, these prove a
// handle reachable only through a struct is NOT.
namespace {

struct HandleProbe {
    TypeID tid = invalid_type_id;
    std::shared_ptr<int> destroyed = std::make_shared<int>(0);
    nw::TypedHandle handle{};
    HeapPtr interned{};

    // Uses the real core.effects.Effect rather than a test-local handle type:
    // a [[native]] declaration inside a script module creates a module-scoped
    // type with no registration behind it, which is not what ships.
    void setup(Runtime& runtime)
    {
        tid = runtime.type_id("core.effects.Effect", false);
        ASSERT_NE(tid, invalid_type_id);

        auto counter = destroyed;
        runtime.register_handle_destructor(tid, [counter](nw::TypedHandle h, HeapPtr) {
            auto& es = nw::kernel::effects();
            if (auto* eff = es.get(h)) {
                es.destroy(eff);
                ++*counter;
            }
        });

        auto* eff = nw::kernel::effects().create(nw::EffectType::invalid());
        ASSERT_NE(eff, nullptr);
        handle = eff->handle().to_typed_handle();
        interned = runtime.intern_handle(handle, OwnershipMode::VM_OWNED);
        ASSERT_NE(interned.value, 0);
    }

    bool alive() const { return nw::kernel::effects().get(handle) != nullptr; }
};

} // namespace

TEST_F(SmallsGCTest, HandleTypeHasHeapStorageLayout)
{
    auto& runtime = nw::kernel::runtime();
    const TypeID tid = runtime.type_id("core.effects.Effect", false);
    ASSERT_NE(tid, invalid_type_id);

    const Type* type = runtime.get_type(tid);
    ASSERT_NE(type, nullptr);
    EXPECT_EQ(type->type_kind, TK_handle);
    EXPECT_EQ(type->size, sizeof(HeapPtr));
    EXPECT_EQ(type->alignment, alignof(HeapPtr));
    // The layout validator rejects size 0 && alignment 0, and the collector
    // keys tracing off this predicate.
    EXPECT_TRUE(runtime.type_table_.is_heap_type(tid));
}

TEST_F(SmallsGCTest, HandleReachableOnlyThroughStructFieldSurvivesCollection)
{
    auto& runtime = nw::kernel::runtime();
    auto* gc = runtime.gc();
    ASSERT_NE(gc, nullptr);

    HandleProbe probe;
    ASSERT_NO_FATAL_FAILURE(probe.setup(runtime));

    ASSERT_NE(runtime.load_module_from_source("test.handle_in_struct", R"(
import core.effects as eff;

type Holder {
    effect: eff.Effect;
};

fn hold(e: eff.Effect): Holder {
    return Holder { effect = e };
}

fn still_valid(h: Holder): bool {
    return eff.is_valid(h.effect);
}

fn direct_valid(e: eff.Effect): bool {
    return eff.is_valid(e);
}

fn roundtrip_valid(e: eff.Effect): bool {
    var h = Holder { effect = e };
    return eff.is_valid(h.effect);
}
)"),
        nullptr);

    auto held = runtime.execute_script("test.handle_in_struct", "hold",
        {Value::make_heap(probe.interned, probe.tid)});
    ASSERT_TRUE(held.ok()) << held.error_message;

    {
        // The struct is now the only thing referencing the handle.
        Runtime::ScopedRoots roots{runtime, 1};
        roots.add(held.value);

        gc->collect_minor();
        EXPECT_EQ(*probe.destroyed, 0);
        EXPECT_TRUE(probe.alive());

        gc->collect_major();
        EXPECT_EQ(*probe.destroyed, 0) << "handle in a struct field was collected";
        EXPECT_TRUE(probe.alive());
        EXPECT_TRUE(heap_contains(runtime, probe.interned));

        // Still usable from script, not merely un-freed: read the field back
        // through the VM and ask the engine whether it resolves.
        const Value handle_value = Value::make_heap(probe.interned, probe.tid);

        auto direct = runtime.execute_script("test.handle_in_struct", "direct_valid", {handle_value});
        ASSERT_TRUE(direct.ok()) << direct.error_message;
        EXPECT_TRUE(direct.value.data.bval) << "control: handle does not resolve even directly";

        auto roundtrip = runtime.execute_script("test.handle_in_struct", "roundtrip_valid", {handle_value});
        ASSERT_TRUE(roundtrip.ok()) << roundtrip.error_message;
        EXPECT_TRUE(roundtrip.value.data.bval) << "handle does not survive a struct store/load";

        auto still_valid = runtime.execute_script("test.handle_in_struct", "still_valid", {held.value});
        ASSERT_TRUE(still_valid.ok()) << still_valid.error_message;
        EXPECT_TRUE(still_valid.value.data.bval)
            << "handle survived collection but no longer resolves through the struct";
    }
}

TEST_F(SmallsGCTest, HandleReachableOnlyThroughArrayOfStructsSurvivesCollection)
{
    auto& runtime = nw::kernel::runtime();
    auto* gc = runtime.gc();
    ASSERT_NE(gc, nullptr);

    HandleProbe probe;
    ASSERT_NO_FATAL_FAILURE(probe.setup(runtime));

    ASSERT_NE(runtime.load_module_from_source("test.handle_in_array", R"(
import core.array as A;
import core.effects as eff;

type Row {
    effect: eff.Effect;
};

fn rows(e: eff.Effect): array!(Row) {
    var out: array!(Row) = {};
    A.push(out, Row { effect = e });
    return out;
}
)"),
        nullptr);

    auto held = runtime.execute_script("test.handle_in_array", "rows",
        {Value::make_heap(probe.interned, probe.tid)});
    ASSERT_TRUE(held.ok()) << held.error_message;

    {
        Runtime::ScopedRoots roots{runtime, 1};
        roots.add(held.value);

        gc->collect_major();
        EXPECT_EQ(*probe.destroyed, 0) << "handle nested in array of structs was collected";
        EXPECT_TRUE(probe.alive());
    }
}

// The complement: tracing through structs must not amount to pinning every
// handle forever. Once nothing holds the struct the handle is collected and its
// engine effect destroyed, exactly as a bare handle is.
// The generational hazard the three tests above do not reach: they collect
// immediately after the store, so the handle and its owning struct are both
// young and a missing write barrier would go unnoticed. Here the struct is
// promoted to the old generation first, so a young handle written into it is
// only reachable through the remembered set. Mirrors
// WriteBarriersPreserveYoungValuesInOldContainers, for handle fields.
TEST_F(SmallsGCTest, YoungHandleStoredIntoOldStructSurvivesMinorCollection)
{
    auto& runtime = nw::kernel::runtime();
    auto* gc = runtime.gc();
    ASSERT_NE(gc, nullptr);

    const TypeID tid = runtime.type_id("core.effects.Effect", false);
    ASSERT_NE(tid, invalid_type_id);

    auto destroyed = std::make_shared<int>(0);
    runtime.register_handle_destructor(tid, [destroyed](nw::TypedHandle h, HeapPtr) {
        auto& es = nw::kernel::effects();
        if (auto* eff = es.get(h)) {
            es.destroy(eff);
            ++*destroyed;
        }
    });

    ASSERT_NE(runtime.load_module_from_source("test.handle_write_barrier", R"(
import core.effects as eff;

type Holder {
    effect: eff.Effect;
};

fn make_holder(): Holder {
    return Holder { effect = eff.invalid() };
}

fn set_effect(h: Holder, e: eff.Effect) {
    h.effect = e;
}

fn is_alive(h: Holder): bool {
    return eff.is_valid(h.effect);
}
)"),
        nullptr);

    auto holder = runtime.execute_script("test.handle_write_barrier", "make_holder");
    ASSERT_TRUE(holder.ok()) << holder.error_message;
    ASSERT_EQ(holder.value.storage, ValueStorage::heap);

    Runtime::ScopedRoots roots{runtime, 1};
    roots.add(holder.value);

    // Promote the holder out of the young generation.
    for (int i = 0; i < 4; ++i) {
        gc->collect_minor();
    }
    ASSERT_TRUE(gc->is_old(holder.value.data.hptr)) << "holder never aged; the test would prove nothing";

    // Only now allocate the handle, so its cell is unambiguously young.
    auto* effect = nw::kernel::effects().create(nw::EffectType::invalid());
    ASSERT_NE(effect, nullptr);
    const nw::TypedHandle handle = effect->handle().to_typed_handle();
    const HeapPtr interned = runtime.intern_handle(handle, OwnershipMode::VM_OWNED);
    ASSERT_NE(interned.value, 0u);
    ASSERT_FALSE(gc->is_old(interned)) << "handle cell was not young; the test would prove nothing";

    auto stored = runtime.execute_script("test.handle_write_barrier", "set_effect",
        {holder.value, Value::make_heap(interned, tid)});
    ASSERT_TRUE(stored.ok()) << stored.error_message;

    // A minor collection scans the young generation plus the remembered set. If
    // the field store did not record the old holder, the handle is unreachable
    // from this collection's roots and its engine effect is destroyed.
    gc->collect_minor();

    EXPECT_EQ(*destroyed, 0) << "young handle in an old struct was collected: missing write barrier";
    EXPECT_NE(nw::kernel::effects().get(handle), nullptr);

    auto alive = runtime.execute_script("test.handle_write_barrier", "is_alive", {holder.value});
    ASSERT_TRUE(alive.ok()) << alive.error_message;
    EXPECT_TRUE(alive.value.data.bval) << "handle survived but no longer resolves through the old struct";
}

TEST_F(SmallsGCTest, HandleInUnreachableStructIsStillCollected)
{
    auto& runtime = nw::kernel::runtime();
    auto* gc = runtime.gc();
    ASSERT_NE(gc, nullptr);

    HandleProbe probe;
    ASSERT_NO_FATAL_FAILURE(probe.setup(runtime));

    ASSERT_NE(runtime.load_module_from_source("test.handle_dropped", R"(
import core.effects as eff;

type Holder {
    effect: eff.Effect;
};

fn hold(e: eff.Effect): Holder {
    return Holder { effect = e };
}
)"),
        nullptr);

    {
        auto held = runtime.execute_script("test.handle_dropped", "hold",
            {Value::make_heap(probe.interned, probe.tid)});
        ASSERT_TRUE(held.ok()) << held.error_message;

        Runtime::ScopedRoots roots{runtime, 1};
        roots.add(held.value);
        gc->collect_major();
        EXPECT_EQ(*probe.destroyed, 0);
    }

    ASSERT_TRUE(runtime.stack_empty());
    gc->collect_major();
    EXPECT_EQ(*probe.destroyed, 1) << "struct became unreachable but the handle was retained";
    EXPECT_FALSE(probe.alive());
}

TEST_F(SmallsGCTest, PropsetShadowArrayStorageInvariant)
{
    auto& runtime = nw::kernel::runtime();

    std::string_view source = R"(
        [[propset]]
        type OwnerData {
            nums: array!(int);
        };
    )";

    auto* script = runtime.load_module_from_source("test.gc_propset", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    TypeID propset_tid = runtime.type_id("test.gc_propset.OwnerData", false);
    ASSERT_NE(propset_tid, invalid_type_id);

    const Type* propset_type = runtime.get_type(propset_tid);
    ASSERT_NE(propset_type, nullptr);
    ASSERT_TRUE(propset_type->type_kind == TK_struct && propset_type->type_params[0].is<StructID>());
    const StructDef* def = runtime.type_table_.get(propset_type->type_params[0].as<StructID>());
    ASSERT_NE(def, nullptr);

    uint32_t nums_offset = 0;
    TypeID nums_tid = invalid_type_id;
    for (uint32_t i = 0; i < def->field_count; ++i) {
        if (def->fields[i].name.view() == "nums") {
            nums_offset = def->fields[i].offset;
            nums_tid = def->fields[i].type_id;
            EXPECT_TRUE(def->fields[i].is_object_component_array);
            break;
        }
    }
    ASSERT_NE(nums_tid, invalid_type_id);

    auto* creature = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(creature, nullptr);

    Value propset_ref = runtime.get_or_create_propset_ref(propset_tid, creature->handle());
    ASSERT_EQ(propset_ref.type_id, propset_tid);

    Value nums_val = runtime.read_value_field_at_offset(propset_ref, nums_offset, nums_tid);
    ASSERT_EQ(nums_val.type_id, nums_tid);
    ASSERT_EQ(nums_val.storage, ValueStorage::immediate);

    EXPECT_NE(runtime.resolve_array(nums_val), nullptr);

    nw::kernel::objects().destroy(creature->handle());
    EXPECT_EQ(runtime.resolve_array(nums_val), nullptr);
}

TEST_F(SmallsGCTest, PropsetStringSurvivesMinorAndMajorGC)
{
    auto& runtime = nw::kernel::runtime();
    auto* gc = runtime.gc();
    ASSERT_NE(gc, nullptr);

    std::string_view source = R"(
        [[propset]]
        type NameData {
            label: string;
        };
    )";

    auto* script = runtime.load_module_from_source("test.gc_propset_string", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    TypeID propset_tid = runtime.type_id("test.gc_propset_string.NameData", false);
    ASSERT_NE(propset_tid, invalid_type_id);

    const Type* propset_type = runtime.get_type(propset_tid);
    ASSERT_NE(propset_type, nullptr);
    ASSERT_TRUE(propset_type->type_kind == TK_struct && propset_type->type_params[0].is<StructID>());
    const StructDef* def = runtime.type_table_.get(propset_type->type_params[0].as<StructID>());
    ASSERT_NE(def, nullptr);

    uint32_t label_offset = 0;
    TypeID label_tid = invalid_type_id;
    for (uint32_t i = 0; i < def->field_count; ++i) {
        if (def->fields[i].name.view() == "label") {
            label_offset = def->fields[i].offset;
            label_tid = def->fields[i].type_id;
            break;
        }
    }
    ASSERT_NE(label_tid, invalid_type_id);

    auto* c1 = nw::kernel::objects().make<nw::Creature>();
    auto* c2 = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(c1, nullptr);
    ASSERT_NE(c2, nullptr);

    Value ref1 = runtime.get_or_create_propset_ref(propset_tid, c1->handle());
    Value ref2 = runtime.get_or_create_propset_ref(propset_tid, c2->handle());
    ASSERT_EQ(ref1.type_id, propset_tid);
    ASSERT_EQ(ref2.type_id, propset_tid);

    HeapPtr str_ptr = runtime.alloc_string("tyr_the_evenhanded");
    ASSERT_NE(str_ptr.value, 0u);
    Value str_val = Value::make_heap(str_ptr, runtime.string_type());

    ASSERT_TRUE(runtime.write_value_field_at_offset(ref1, label_offset, label_tid, str_val));
    ASSERT_TRUE(runtime.write_value_field_at_offset(ref2, label_offset, label_tid, str_val));
    str_val = Value{};

    // Both minor and major GC must preserve pinned propset strings without scanning propsets.
    gc->collect_minor();
    gc->collect_major();

    Value read1 = runtime.read_value_field_at_offset(ref1, label_offset, label_tid);
    Value read2 = runtime.read_value_field_at_offset(ref2, label_offset, label_tid);
    ASSERT_EQ(read1.data.hptr, str_ptr);
    ASSERT_EQ(read2.data.hptr, str_ptr);
    EXPECT_EQ(runtime.get_string_view(read1.data.hptr), "tyr_the_evenhanded");
    EXPECT_EQ(runtime.get_string_view(read2.data.hptr), "tyr_the_evenhanded");
    read1 = Value{};
    read2 = Value{};

    // Destroying one owner decrements pin_count (2 -> 1); string stays alive for c2.
    nw::kernel::objects().destroy(c1->handle());
    gc->collect_major();

    Value read2_after = runtime.read_value_field_at_offset(ref2, label_offset, label_tid);
    ASSERT_EQ(read2_after.data.hptr, str_ptr);
    EXPECT_EQ(runtime.get_string_view(read2_after.data.hptr), "tyr_the_evenhanded");
    read2_after = Value{};

    // Destroying the last owner unpins (1 -> 0); next major GC reclaims the string.
    nw::kernel::objects().destroy(c2->handle());
    gc->collect_major();
    EXPECT_EQ(runtime.heap_.try_get_header(str_ptr), nullptr);
}

TEST_F(SmallsGCTest, FreshVmEarlyFailureDoesNotCrash)
{
    auto& runtime = nw::kernel::runtime();
    auto* script = runtime.load_module_from_source("test.gc_fresh_vm", R"(
        fn ok(): int { return 7; }
    )");
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    BytecodeModule* module = runtime.get_or_compile_module(script);
    ASSERT_NE(module, nullptr);

    VirtualMachine fresh_vm;
    Value missing = fresh_vm.execute(module, "does_not_exist", {});
    EXPECT_TRUE(fresh_vm.failed());
    EXPECT_EQ(missing.type_id, invalid_type_id);

    Value result = fresh_vm.execute(module, "ok", {});
    EXPECT_FALSE(fresh_vm.failed());
    EXPECT_EQ(result.data.ival, 7);
}

TEST_F(SmallsGCTest, WriteBarriersPreserveYoungValuesInOldContainers)
{
    auto& runtime = nw::kernel::runtime();
    auto* gc = runtime.gc();
    ASSERT_NE(gc, nullptr);

    auto* script = runtime.load_module_from_source("test.gc_write_barriers", R"(
        import core.array as arr;

        type Box = Empty | Text(string);

        var g_list: array!(string) = {};

        fn push_text(s: string) {
            arr.push(g_list, s);
        }

        fn set_text(i: int, s: string) {
            arr.set(g_list, i, s);
        }

        fn get_text(i: int): string {
            return arr.get(g_list, i);
        }
    )");
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    ASSERT_TRUE(runtime.execute_script(script, "push_text", {Value::make_string(runtime.alloc_string("seed"))}).ok());

    TypeID box_tid = runtime.type_id("test.gc_write_barriers.Box", false);
    ASSERT_NE(box_tid, invalid_type_id);
    const Type* box_type = runtime.get_type(box_tid);
    ASSERT_NE(box_type, nullptr);
    const SumDef* box_def = runtime.type_table_.get(box_type->type_params[0].as<SumID>());
    ASSERT_NE(box_def, nullptr);

    HeapPtr old_sum = runtime.alloc_sum(box_tid);
    ASSERT_NE(old_sum.value, 0u);
    Runtime::ScopedRoots roots{runtime, 1};
    roots.add(Value::make_heap(old_sum, box_tid));

    // Promote g_list and old_sum to old generation.
    for (int i = 0; i < 4; ++i) {
        gc->collect_minor();
    }
    ASSERT_TRUE(gc->is_old(old_sum));

    // Mutate old containers with freshly allocated young strings via array.push, array.set, and write_sum_payload.
    ASSERT_TRUE(runtime.execute_script(script, "push_text", {Value::make_string(runtime.alloc_string("young_pushed"))}).ok());
    ASSERT_TRUE(runtime.execute_script(script, "set_text", {Value::make_int(0), Value::make_string(runtime.alloc_string("young_set"))}).ok());
    HeapPtr young_sum_str = runtime.alloc_string("young_sum");
    runtime.write_sum_tag(old_sum, box_def, 1);
    runtime.write_sum_payload(old_sum, box_def, 1, Value::make_string(young_sum_str));

    gc->collect_minor();

    auto r0 = runtime.execute_script(script, "get_text", {Value::make_int(0)});
    ASSERT_TRUE(r0.ok());
    EXPECT_EQ(runtime.get_string_view(r0.value.data.hptr), "young_set");

    auto r1 = runtime.execute_script(script, "get_text", {Value::make_int(1)});
    ASSERT_TRUE(r1.ok());
    EXPECT_EQ(runtime.get_string_view(r1.value.data.hptr), "young_pushed");

    Value sum_payload = runtime.read_sum_payload(old_sum, box_def, 1);
    ASSERT_EQ(sum_payload.storage, ValueStorage::heap);
    EXPECT_TRUE(heap_contains(runtime, sum_payload.data.hptr));
    EXPECT_EQ(runtime.get_string_view(sum_payload.data.hptr), "young_sum");
}

TEST_F(SmallsGCTest, CardTableFullWordRange)
{
    CardTable table;
    constexpr uint32_t word_span_bytes = static_cast<uint32_t>(CardTable::bits_per_word * CardTable::card_size);
    table.mark_dirty(CardTable::card_size * 7);

    EXPECT_TRUE(table.is_range_dirty(0, word_span_bytes));
    table.clear_range(0, word_span_bytes);
    EXPECT_FALSE(table.is_range_dirty(0, word_span_bytes));
}

TEST_F(SmallsGCTest, IncrementalMinorGCAllocationsSurviveAndMajorResetsMinorPhase)
{
    auto& runtime = nw::kernel::runtime();
    auto* gc = runtime.gc();
    ASSERT_NE(gc, nullptr);

    // Seed unreachable young garbage so minor sweep frees the head of young_objects_.
    for (int i = 0; i < 64; ++i) {
        runtime.alloc_string("garbage");
    }

    // Start an incremental minor cycle without finishing it.
    ASSERT_FALSE(gc->collect_minor_step(1));
    ASSERT_TRUE(gc->is_minor_collecting());

    // Allocate a new rooted young string mid-cycle; it must be staged and not unlinked.
    HeapPtr mid_cycle = runtime.alloc_string("mid_cycle_survivor");
    Runtime::ScopedRoots roots{runtime, 1};
    roots.add(Value::make_string(mid_cycle));

    // Finish the minor cycle and verify the mid-cycle allocation is still live and tracked.
    while (!gc->collect_minor_step(4)) {
    }
    EXPECT_TRUE(heap_contains(runtime, mid_cycle));
    EXPECT_EQ(runtime.get_string_view(mid_cycle), "mid_cycle_survivor");

    // Start another minor cycle, interrupt it with collect_major(), and verify clean state.
    for (int i = 0; i < 64; ++i) {
        runtime.alloc_string("garbage2");
    }
    ASSERT_FALSE(gc->collect_minor_step(1));
    ASSERT_TRUE(gc->is_minor_collecting());
    gc->collect_major();
    EXPECT_FALSE(gc->is_minor_collecting());
    EXPECT_TRUE(heap_contains(runtime, mid_cycle));
}
