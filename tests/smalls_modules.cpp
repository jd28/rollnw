#include "smalls_fixtures.hpp"

#include <nw/log.hpp>
#include <nw/rules/RuntimeObject.hpp>
#include <nw/rules/effects.hpp>
#include <nw/smalls/Array.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/runtime.hpp>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>

namespace fs = std::filesystem;

class SmallsModules : public ::testing::Test {
protected:
    void SetUp() override
    {
        nw::kernel::services().start();
        nw::kernel::runtime().add_module_path(fs::path("stdlib/core"));
    }

    void TearDown() override
    {
        nw::kernel::services().shutdown();
    }
};

// == Module Loading from Filesystem ==========================================

TEST_F(SmallsModules, LoadModuleFromFilesystem)
{
    auto* math_module = nw::kernel::runtime().load_module("core.math");
    ASSERT_NE(math_module, nullptr);

    // Verify module has expected exports
    EXPECT_FALSE(math_module->exports().empty());

    // Verify Vector type is exported
    auto vec_it = math_module->exports().find("Vector");
    EXPECT_NE(vec_it, nullptr);
}

TEST_F(SmallsModules, LoadNestedModuleFromFilesystem)
{
    auto* types_module = nw::kernel::runtime().load_module("core.types");
    ASSERT_NE(types_module, nullptr);

    // Verify nested module exports
    auto color_it = types_module->exports().find("Color");
    EXPECT_NE(color_it, nullptr);
}

TEST_F(SmallsModules, LoadCoreEffectsModule)
{
    auto& rt = nw::kernel::runtime();
    auto* effects_module = rt.load_module("core.effects");
    ASSERT_NE(effects_module, nullptr);

    auto effect_it = effects_module->exports().find("Effect");
    EXPECT_NE(effect_it, nullptr);
    auto create_it = effects_module->exports().find("create");
    EXPECT_NE(create_it, nullptr);
    auto apply_it = effects_module->exports().find("apply");
    EXPECT_NE(apply_it, nullptr);
    auto is_valid_it = effects_module->exports().find("is_valid");
    EXPECT_NE(is_valid_it, nullptr);
    auto destroy_it = effects_module->exports().find("destroy");
    EXPECT_NE(destroy_it, nullptr);

    std::string_view source = R"(
        import core.effects as E;
        fn main() {
            var eff = E.create();
            E.apply(eff);
        }
    )";
    auto* script = rt.load_module_from_source("test.core_effects_smoke", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    auto exec_result = rt.execute_script(script, "main");
    EXPECT_TRUE(exec_result.ok());
}

TEST_F(SmallsModules, CoreEffectsConstructorsUseNewtypes)
{
    auto& rt = nw::kernel::runtime();

    std::string_view source = R"(
        from core.constants import { Ability };
        import core.effects as E;

        fn main(): int {
            var eff = E.ability_modifier(Ability(0), 2);
            if (!E.is_valid(eff)) {
                return 0;
            }

            E.apply(eff);
            E.destroy(eff);
            if (!E.is_valid(eff)) {
                return 0;
            }

            var haste = E.haste();
            if (E.is_valid(haste)) {
                return 1;
            }
            return 0;
        }
    )";

    auto* script = rt.load_module_from_source("test.core_effects_newtypes", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    auto result = rt.execute_script(script, "main");
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value.data.ival, 1);
}

TEST_F(SmallsModules, CoreEffectsRejectRawIntWhenNewtypeRequired)
{
    auto& rt = nw::kernel::runtime();

    std::string_view source = R"(
        import core.effects as E;

        fn main() {
            var eff = E.ability_modifier(0, 2);
            E.apply(eff);
        }
    )";

    auto* script = rt.load_module_from_source("test.core_effects_newtype_reject", source);
    ASSERT_NE(script, nullptr);
    EXPECT_GT(script->errors(), 0);
}

TEST_F(SmallsModules, CoreEffectsUseExpandedConstants)
{
    auto& rt = nw::kernel::runtime();

    auto* constants_module = rt.load_module("core.constants");
    ASSERT_NE(constants_module, nullptr);

    EXPECT_NE(constants_module->exports().find("attack_type_unarmed"), nullptr);
    EXPECT_NE(constants_module->exports().find("class_type_barbarian"), nullptr);
    EXPECT_NE(constants_module->exports().find("damage_category_critical"), nullptr);
    EXPECT_NE(constants_module->exports().find("damage_type_sonic"), nullptr);
    EXPECT_NE(constants_module->exports().find("saving_throw_fort"), nullptr);
    EXPECT_NE(constants_module->exports().find("saving_throw_vs_poison"), nullptr);
    EXPECT_NE(constants_module->exports().find("skill_ride"), nullptr);
    EXPECT_NE(constants_module->exports().find("equip_index_righthand"), nullptr);
}

TEST_F(SmallsModules, LoadCoreCreatureModule)
{
    auto& rt = nw::kernel::runtime();
    auto* creature_module = rt.load_module("core.creature");
    ASSERT_NE(creature_module, nullptr);

    EXPECT_NE(creature_module->exports().find("get_ability_score"), nullptr);
    EXPECT_NE(creature_module->exports().find("get_ability_modifier"), nullptr);
    EXPECT_NE(creature_module->exports().find("can_equip_item"), nullptr);
    EXPECT_NE(creature_module->exports().find("equip_item"), nullptr);
    EXPECT_NE(creature_module->exports().find("get_equipped_item"), nullptr);
    EXPECT_NE(creature_module->exports().find("unequip_item"), nullptr);
}

TEST_F(SmallsModules, LoadCoreItemModule)
{
    auto& rt = nw::kernel::runtime();
    auto* item_module = rt.load_module("core.item");
    ASSERT_NE(item_module, nullptr);

    EXPECT_NE(item_module->exports().find("property_count"), nullptr);
    EXPECT_NE(item_module->exports().find("clear_properties"), nullptr);
}

TEST_F(SmallsModules, LoadObjectTypeStubModules)
{
    auto& rt = nw::kernel::runtime();

    std::array<std::string_view, 10> modules = {
        "core.area",
        "core.door",
        "core.encounter",
        "core.module",
        "core.placeable",
        "core.player",
        "core.sound",
        "core.store",
        "core.trigger",
        "core.waypoint",
    };

    for (auto module : modules) {
        auto* loaded = rt.load_module(module);
        ASSERT_NE(loaded, nullptr) << module;
        EXPECT_NE(loaded->exports().find("is_valid"), nullptr) << module;
        EXPECT_NE(loaded->exports().find("type_id"), nullptr) << module;

        if (module == "core.area") {
            EXPECT_NE(loaded->exports().find("is_area"), nullptr);
            EXPECT_NE(loaded->exports().find("as_area"), nullptr);
            EXPECT_NE(loaded->exports().find("get_width"), nullptr);
            EXPECT_NE(loaded->exports().find("get_height"), nullptr);
            EXPECT_NE(loaded->exports().find("get_size"), nullptr);
        } else if (module == "core.door") {
            EXPECT_NE(loaded->exports().find("is_door"), nullptr);
            EXPECT_NE(loaded->exports().find("as_door"), nullptr);
            EXPECT_NE(loaded->exports().find("get_hp_current"), nullptr);
            EXPECT_NE(loaded->exports().find("is_open"), nullptr);
        } else if (module == "core.encounter") {
            EXPECT_NE(loaded->exports().find("is_encounter"), nullptr);
            EXPECT_NE(loaded->exports().find("as_encounter"), nullptr);
            EXPECT_NE(loaded->exports().find("is_active"), nullptr);
            EXPECT_NE(loaded->exports().find("get_spawn_point_count"), nullptr);
        } else if (module == "core.module") {
            EXPECT_NE(loaded->exports().find("is_module"), nullptr);
            EXPECT_NE(loaded->exports().find("as_module"), nullptr);
            EXPECT_NE(loaded->exports().find("get_area_count"), nullptr);
            EXPECT_NE(loaded->exports().find("is_save_game"), nullptr);
        } else if (module == "core.placeable") {
            EXPECT_NE(loaded->exports().find("is_placeable"), nullptr);
            EXPECT_NE(loaded->exports().find("as_placeable"), nullptr);
            EXPECT_NE(loaded->exports().find("get_hp_current"), nullptr);
            EXPECT_NE(loaded->exports().find("has_inventory"), nullptr);
        } else if (module == "core.player") {
            EXPECT_NE(loaded->exports().find("is_player"), nullptr);
            EXPECT_NE(loaded->exports().find("as_player"), nullptr);
            EXPECT_NE(loaded->exports().find("get_hp_current"), nullptr);
            EXPECT_NE(loaded->exports().find("is_pc"), nullptr);
        } else if (module == "core.sound") {
            EXPECT_NE(loaded->exports().find("is_sound"), nullptr);
            EXPECT_NE(loaded->exports().find("as_sound"), nullptr);
            EXPECT_NE(loaded->exports().find("get_sound_count"), nullptr);
            EXPECT_NE(loaded->exports().find("is_active"), nullptr);
        } else if (module == "core.store") {
            EXPECT_NE(loaded->exports().find("is_store"), nullptr);
            EXPECT_NE(loaded->exports().find("as_store"), nullptr);
            EXPECT_NE(loaded->exports().find("get_gold"), nullptr);
            EXPECT_NE(loaded->exports().find("is_blackmarket"), nullptr);
        } else if (module == "core.trigger") {
            EXPECT_NE(loaded->exports().find("is_trigger"), nullptr);
            EXPECT_NE(loaded->exports().find("as_trigger"), nullptr);
            EXPECT_NE(loaded->exports().find("get_geometry_point_count"), nullptr);
            EXPECT_NE(loaded->exports().find("get_trigger_type"), nullptr);
        } else if (module == "core.waypoint") {
            EXPECT_NE(loaded->exports().find("is_waypoint"), nullptr);
            EXPECT_NE(loaded->exports().find("as_waypoint"), nullptr);
            EXPECT_NE(loaded->exports().find("get_appearance"), nullptr);
            EXPECT_NE(loaded->exports().find("has_map_note"), nullptr);
        }
    }
}

TEST_F(SmallsModules, SelectiveImportFromFilesystemModule)
{
    auto script = make_script(R"(
from core.types import { Color };
const c: Color = { 255, 128, 64, 255 };
)");

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsModules, AliasedImportFromFilesystemModule)
{
    auto script = make_script(R"(
 import core.types as types;
 const c: types.Color = { 255, 128, 64, 255 };
 )");

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

// == Native Module Registration ==============================================

// Example opaque type for testing
struct TestObject {
    int id;
    float value;
};

// Example functions for testing
int32_t test_get_value(TestObject* obj) { return static_cast<int32_t>(obj->value); }
void test_set_value(TestObject* obj, float val) { obj->value = val; }

TEST_F(SmallsModules, NativeModuleRegistration)
{
    auto& rt = nw::kernel::runtime();

    rt.module("test.native")
        .add_opaque_type<TestObject>("TestObject")
        .function("get_value", &test_get_value)
        .function("set_value", &test_set_value)
        .finalize();

    // Verify module is registered
    EXPECT_NE(rt.get_native_module("test.native"), nullptr);

    // Verify we have interface metadata
    const auto* iface = rt.get_native_module("test.native");
    ASSERT_NE(iface, nullptr);
    EXPECT_EQ(iface->module_path, "test.native");
    EXPECT_EQ(iface->opaque_types.size(), 1);
    EXPECT_EQ(iface->functions.size(), 2);
}

TEST_F(SmallsModules, NativeModuleFunctionMetadata)
{
    auto& rt = nw::kernel::runtime();

    rt.module("test.metadata")
        .add_opaque_type<TestObject>("TestObject")
        .function("get_value", &test_get_value)
        .finalize();

    const auto& interfaces = rt.native_interfaces();
    ASSERT_FALSE(interfaces.empty());

    const auto& func = interfaces.back().functions[0];
    EXPECT_EQ(func.name, "get_value");
    EXPECT_EQ(func.return_type, rt.int_type());
}

// == Layout Validation =======================================================

// Test struct with known layout
struct TestVec3 {
    float x, y, z;
};

TEST_F(SmallsModules, ValueTypeLayoutValidation)
{
    auto& rt = nw::kernel::runtime();

    // First, register the smalls type definition
    nw::smalls::Type vec3_type{
        .name = nw::kernel::strings().intern("TestVec3"),
        .type_params = {},
        .type_kind = nw::smalls::TK_struct,
        .size = sizeof(TestVec3),
        .alignment = alignof(TestVec3),
    };
    rt.register_type("TestVec3", vec3_type);

    // Now validate the C++ struct matches
    rt.module("test.layout")
        .value_type<TestVec3>("TestVec3") // Should pass validation
        .finalize();

    // If we get here without error logs, validation passed
    SUCCEED();
}

TEST_F(SmallsModules, ValueTypeLayoutMismatch)
{
    auto& rt = nw::kernel::runtime();

    // Register a smalls type with wrong size
    nw::smalls::Type wrong_type{
        .name = nw::kernel::strings().intern("WrongSize"),
        .type_params = {},
        .type_kind = nw::smalls::TK_struct,
        .size = sizeof(TestVec3) + 4, // Wrong size!
        .alignment = alignof(TestVec3),
    };
    rt.register_type("WrongSize", wrong_type);

    // This should log an error about size mismatch
    rt.module("test.mismatch")
        .value_type<TestVec3>("WrongSize") // Should fail validation
        .finalize();

    // Test passes if we don't crash - validation logs error but continues
    SUCCEED();
}

// == Native Struct Validation ================================================

struct TestEntity {
    int32_t id;
    float health;
    nw::ObjectHandle owner;
};

TEST_F(SmallsModules, NativeStructValidationCorrect)
{
    auto& rt = nw::kernel::runtime();

    // Register C++ struct layout via ModuleBuilder
    nw::smalls::ModuleBuilder mb(&rt, "test.entity");
    auto ns = mb.native_struct<TestEntity>("TestEntity");
    ns.field("id", &TestEntity::id);
    ns.field("health", &TestEntity::health);
    ns.field("owner", &TestEntity::owner);
    mb.finalize();

    // Load smalls script with [[native]] annotation (unqualified type name)
    const char* source = R"(
        [[native]]
        type TestEntity {
            id: int;
            health: float;
            owner: object;
        };
    )";

    auto* script = rt.load_module_from_source("test.entity", source);
    ASSERT_NE(script, nullptr);

    // Validation happens during resolve - check no errors
    EXPECT_EQ(script->errors(), 0);
}

TEST_F(SmallsModules, NativeStructValidationFieldCountMismatch)
{
    auto& rt = nw::kernel::runtime();

    // Register C++ struct with 3 fields
    nw::smalls::ModuleBuilder mb(&rt, "test.entity2");
    auto ns = mb.native_struct<TestEntity>("TestEntity2");
    ns.field("id", &TestEntity::id);
    ns.field("health", &TestEntity::health);
    ns.field("owner", &TestEntity::owner);
    mb.finalize();

    // Smalls script has only 2 fields - should fail
    const char* source = R"(
        [[native]]
        type TestEntity2 {
            id: int;
            health: float;
        };
    )";

    auto* script = rt.load_module_from_source("test.entity2", source);
    ASSERT_NE(script, nullptr);

    // Should have errors due to field count mismatch
    EXPECT_GT(script->errors(), 0);
}

TEST_F(SmallsModules, NativeStructValidationFieldTypeMismatch)
{
    auto& rt = nw::kernel::runtime();

    // Register C++ struct
    nw::smalls::ModuleBuilder mb(&rt, "test.entity3");
    auto ns = mb.native_struct<TestEntity>("TestEntity3");
    ns.field("id", &TestEntity::id);
    ns.field("health", &TestEntity::health);
    ns.field("owner", &TestEntity::owner);
    mb.finalize();

    // Smalls has wrong type for 'health' field
    const char* source = R"(
        [[native]]
        type TestEntity3 {
            id: int;
            health: int;
            owner: object;
        };
    )";

    auto* script = rt.load_module_from_source("test.entity3", source);
    ASSERT_NE(script, nullptr);

    // Should have errors due to type mismatch
    EXPECT_GT(script->errors(), 0);
}

// == Native Module Linking ===================================================

TEST_F(SmallsModules, NativeLinkingValid)
{
    auto& rt = nw::kernel::runtime();

    // Register a native module
    rt.module("test.linking")
        .add_opaque_type<TestObject>("GameObject")
        .function("create_object", +[]() -> int32_t { return 42; })
        .finalize();

    // Now load a script module with matching [[native]] declarations
    auto* script = rt.load_module_from_source("test.linking", R"(
[[native]] type GameObject;
[[native]] fn create_object(): int;
)");

    ASSERT_NE(script, nullptr);
    EXPECT_EQ(script->errors(), 0);
}

TEST_F(SmallsModules, NativeLinkingNoModuleRegistered)
{
    auto& rt = nw::kernel::runtime();

    // Load a script with [[native]] declarations but no C++ module
    auto* script = rt.load_module_from_source("test.unregistered", R"(
[[native]] type GameObject;
)");

    ASSERT_NE(script, nullptr);
    EXPECT_GT(script->errors(), 0);
}

TEST_F(SmallsModules, NativeLinkingTypeNotRegistered)
{
    auto& rt = nw::kernel::runtime();

    // Register a native module with one type
    rt.module("test.missing_type")
        .add_opaque_type<TestObject>("GameObject")
        .finalize();

    // Try to link a script that declares a different type
    auto* script = rt.load_module_from_source("test.missing_type", R"(
[[native]] type Effect;
)");

    ASSERT_NE(script, nullptr);
    EXPECT_GT(script->errors(), 0);
}

TEST_F(SmallsModules, NativeLinkingFunctionNotRegistered)
{
    auto& rt = nw::kernel::runtime();

    // Register a native module with one function
    rt.module("test.missing_func")
        .function("foo", +[]() -> int32_t { return 0; })
        .finalize();

    // Try to link a script that declares a different function
    auto* script = rt.load_module_from_source("test.missing_func", R"(
[[native]] fn bar(): int;
)");

    ASSERT_NE(script, nullptr);
    EXPECT_GT(script->errors(), 0);
}

TEST_F(SmallsModules, NativeLinkingReturnTypeMismatch)
{
    auto& rt = nw::kernel::runtime();

    // Register a native function returning int
    rt.module("test.return_mismatch")
        .function("get_value", +[]() -> int32_t { return 42; })
        .finalize();

    // Try to link a script declaring it as returning float
    auto* script = rt.load_module_from_source("test.return_mismatch", R"(
[[native]] fn get_value(): float;
)");

    ASSERT_NE(script, nullptr);
    EXPECT_GT(script->errors(), 0);
}

TEST_F(SmallsModules, NativeLinkingParamCountMismatch)
{
    auto& rt = nw::kernel::runtime();

    // Register a native function with 2 parameters
    rt.module("test.param_count")
        .function("add", +[](int32_t a, int32_t b) -> int32_t { return a + b; })
        .finalize();

    // Try to link a script declaring it with 1 parameter
    auto* script = rt.load_module_from_source("test.param_count", R"(
[[native]] fn add(x: int): int;
)");

    ASSERT_NE(script, nullptr);
    EXPECT_GT(script->errors(), 0);
}

TEST_F(SmallsModules, NativeLinkingParamTypeMismatch)
{
    auto& rt = nw::kernel::runtime();

    // Register a native function with int parameter
    rt.module("test.param_type")
        .function("square", +[](int32_t x) -> int32_t { return x * x; })
        .finalize();

    // Try to link a script declaring it with float parameter
    auto* script = rt.load_module_from_source("test.param_type", R"(
[[native]] fn square(x: float): int;
)");

    ASSERT_NE(script, nullptr);
    EXPECT_GT(script->errors(), 0);
}

TEST_F(SmallsModules, NativeLinkingMixedDeclarations)
{
    auto& rt = nw::kernel::runtime();

    // Register a native module
    rt.module("test.mixed")
        .add_opaque_type<TestObject>("GameObject")
        .function("spawn", +[]() -> int32_t { return 1; })
        .finalize();

    // Load a script mixing native and regular declarations
    auto* script = rt.load_module_from_source("test.mixed", R"(
[[native]] type GameObject;
[[native]] fn spawn(): int;

type Position { x, y: float; };

fn local_helper(pos: Position): float {
    return pos.x + pos.y;
}
)");

    ASSERT_NE(script, nullptr);
    EXPECT_EQ(script->errors(), 0);
}

// == Handle Registry Tests ===================================================

TEST_F(SmallsModules, HandleRegistration)
{
    auto& rt = nw::kernel::runtime();
    rt.add_handle_type("Effect", nw::RuntimeObjectPool::TYPE_EFFECT);

    auto effect_tid = rt.type_id("Effect", false);
    EXPECT_NE(effect_tid, nw::smalls::invalid_type_id);

    nw::TypedHandle h;
    h.id = 1;
    h.type = nw::RuntimeObjectPool::TYPE_EFFECT;
    h.generation = 1;
    EXPECT_EQ(rt.lookup_handle(h).value, 0);
}

TEST_F(SmallsModules, HandleConversion)
{
    nw::EffectHandle eff;
    eff.runtime_handle.generation = 42;
    eff.runtime_handle.id = 123;
    eff.runtime_handle.type = nw::RuntimeObjectPool::TYPE_EFFECT;
    eff.type = nw::EffectType::make(5);
    eff.subtype = 7;

    // Convert to generic handle
    nw::TypedHandle h = eff.to_typed_handle();

    // Verify handle fields copied
    EXPECT_EQ(h.generation, 42);
    EXPECT_EQ(h.id, 123);
    EXPECT_EQ(h.type, nw::RuntimeObjectPool::TYPE_EFFECT);
}

TEST_F(SmallsModules, HandlePoolWithEffects)
{
    nw::RuntimeObjectPool pool;

    // Create an effect
    nw::TypedHandle h = pool.allocate_effect();
    nw::Effect* eff = static_cast<nw::Effect*>(pool.get(h));

    ASSERT_NE(eff, nullptr);
    EXPECT_EQ(h.generation, 1);
    EXPECT_EQ(h.id, 0);

    // Verify the effect got the runtime handle set
    EXPECT_EQ(eff->handle().runtime_handle.generation, h.generation);
    EXPECT_EQ(eff->handle().runtime_handle.id, h.id);
    EXPECT_EQ(eff->handle().runtime_handle.type, h.type);

    // Set some metadata on the effect
    eff->handle().type = nw::EffectType::make(10);
    eff->handle().subtype = 5;

    // Convert to typed handle and verify
    nw::TypedHandle typed = eff->handle().to_typed_handle();
    EXPECT_EQ(typed.generation, h.generation);
    EXPECT_EQ(typed.id, h.id);
    EXPECT_EQ(typed.type, h.type);

    // Verify we can retrieve via typed handle
    nw::Effect* eff2 = static_cast<nw::Effect*>(pool.get(typed));
    EXPECT_EQ(eff, eff2);
    EXPECT_EQ(*eff2->handle().type, 10);
    EXPECT_EQ(eff2->handle().subtype, 5);
}

TEST_F(SmallsModules, HandleDestructorCallback)
{
    auto& rt = nw::kernel::runtime();
    rt.add_handle_type("TestEffect", nw::RuntimeObjectPool::TYPE_EFFECT);

    // Create a fake HeapPtr and Handle for testing
    nw::TypedHandle h;
    h.id = 1;
    h.type = nw::RuntimeObjectPool::TYPE_EFFECT;
    h.generation = 1;
    nw::smalls::HandleEntry entry;
    entry.vm_value = nw::smalls::HeapPtr{0}; // Fake heap pointer
    entry.mode = nw::smalls::OwnershipMode::VM_OWNED;

    // Add to registry
    rt.register_handle(h, entry.vm_value, entry.mode);

    // Verify it was registered
    EXPECT_EQ(rt.lookup_handle(h).value, entry.vm_value.value);

    // Note: We can't fully test destroy_func_ without proper heap allocation
    // This test just verifies the registration and structure is correct
}

TEST_F(SmallsModules, HandleAllocationAndLookup)
{
    auto& rt = nw::kernel::runtime();

    // Register TypedHandle type
    rt.add_handle_type("TestEffect2", nw::RuntimeObjectPool::TYPE_EFFECT);

    // Create and register a handle
    nw::TypedHandle h1;
    h1.id = 42;
    h1.type = nw::RuntimeObjectPool::TYPE_EFFECT;
    h1.generation = 1;

    nw::smalls::HeapPtr ptr1 = rt.intern_handle(h1, nw::smalls::OwnershipMode::VM_OWNED);
    ASSERT_NE(ptr1.value, 0);

    // Look up the handle - should get same HeapPtr back
    nw::smalls::HeapPtr lookup1 = rt.lookup_handle(h1);
    EXPECT_EQ(lookup1.value, ptr1.value);

    // Allocate a second handle
    nw::TypedHandle h2;
    h2.id = 99;
    h2.type = nw::RuntimeObjectPool::TYPE_EFFECT;
    h2.generation = 1;

    nw::smalls::HeapPtr ptr2 = rt.intern_handle(h2, nw::smalls::OwnershipMode::VM_OWNED);
    ASSERT_NE(ptr2.value, 0);
    EXPECT_NE(ptr2.value, ptr1.value); // Different allocation

    // Look up second handle - should get different HeapPtr
    nw::smalls::HeapPtr lookup2 = rt.lookup_handle(h2);
    EXPECT_EQ(lookup2.value, ptr2.value);
    EXPECT_NE(lookup2.value, lookup1.value);

    // Looking up first handle should still work
    nw::smalls::HeapPtr lookup1_again = rt.lookup_handle(h1);
    EXPECT_EQ(lookup1_again.value, ptr1.value);
}

TEST_F(SmallsModules, ModuleBuilderHandleType)
{
    auto& rt = nw::kernel::runtime();

    // Register a native module with handle type
    rt.module("test.handles")
        .add_opaque_type<TestObject>("GameObject")
        .handle_type("Effect", nw::RuntimeObjectPool::TYPE_EFFECT)
        .function("create_effect", +[]() -> int32_t { return 0; })
        .finalize();

    // Verify handle type is registered with qualified name
    auto effect_tid = rt.type_id("test.handles.Effect", false);
    EXPECT_NE(effect_tid, nw::smalls::invalid_type_id);

    // Verify lookup works (registry check)
    nw::TypedHandle h;
    h.id = 1;
    h.type = nw::RuntimeObjectPool::TYPE_EFFECT;
    h.generation = 1;
    EXPECT_EQ(rt.lookup_handle(h).value, 0);

    // Verify the interface metadata is correct
    const auto* iface = rt.get_native_module("test.handles");
    ASSERT_NE(iface, nullptr);
    EXPECT_EQ(iface->module_path, "test.handles");
    EXPECT_EQ(iface->opaque_types.size(), 2);
    EXPECT_EQ(iface->opaque_types[0], "GameObject");
    EXPECT_EQ(iface->opaque_types[1], "Effect");
    EXPECT_EQ(iface->functions.size(), 1);
}

// == Generic Native Functions ================================================

TEST_F(SmallsModules, GenericNativeArrayParam)
{
    auto& rt = nw::kernel::runtime();

    // Register a native function taking IArray* (matches any array<T>)
    rt.module("test.generic_array")
        .function("array_length", +[](nw::smalls::IArray* arr) -> int32_t { return arr ? static_cast<int32_t>(arr->size()) : 0; })
        .finalize();

    // Script declares with array!($T) - should validate successfully
    auto* script = rt.load_module_from_source("test.generic_array", R"(
[[native]] fn array_length(xs: array!($T)): int;
)");

    ASSERT_NE(script, nullptr);
    EXPECT_EQ(script->errors(), 0);
}

TEST_F(SmallsModules, GenericNativeArrayParamConcreteType)
{
    auto& rt = nw::kernel::runtime();

    // Register a native function taking IArray*
    rt.module("test.generic_array_concrete")
        .function("sum_ints", +[](nw::smalls::IArray* /*arr*/) -> int32_t {
            return 0; // Dummy implementation
        })
        .finalize();

    // Script declares with concrete array!(int) - should also validate
    // (any_array matches any array type including concrete ones)
    auto* script = rt.load_module_from_source("test.generic_array_concrete", R"(
[[native]] fn sum_ints(xs: array!(int)): int;
)");

    ASSERT_NE(script, nullptr);
    EXPECT_EQ(script->errors(), 0);
}

TEST_F(SmallsModules, GenericNativeValueReturn)
{
    auto& rt = nw::kernel::runtime();

    // Register a native function returning Value (matches any return type)
    rt.module("test.generic_return")
        .function("array_get", +[](nw::smalls::IArray* arr, int32_t idx) -> nw::smalls::Value {
            nw::smalls::Value result{};
            if (arr) {
                arr->get_value(static_cast<size_t>(idx), result, nw::kernel::runtime());
            }
            return result; })
        .finalize();

    // Script declares with $T return type - should validate successfully
    auto* script = rt.load_module_from_source("test.generic_return", R"(
[[native]] fn array_get(xs: array!($T), idx: int): $T;
)");

    ASSERT_NE(script, nullptr);
    EXPECT_EQ(script->errors(), 0);
}

TEST_F(SmallsModules, GenericNativeTypesCompatible)
{
    auto& rt = nw::kernel::runtime();

    // Test the native_types_compatible function directly
    auto any_array = rt.any_array_type();
    auto any = rt.any_type();
    auto int_type = rt.int_type();

    // Create a concrete array type
    auto array_int = rt.type_id("array!(int)", true);
    auto* array_int_type = const_cast<nw::smalls::Type*>(rt.get_type(array_int));
    if (array_int_type) {
        array_int_type->type_kind = nw::smalls::TK_array;
    }

    // any_array should match any array<T>
    EXPECT_TRUE(rt.native_types_compatible(any_array, array_int));

    // any_array should not match non-array types
    EXPECT_FALSE(rt.native_types_compatible(any_array, int_type));

    // any_type should match anything
    EXPECT_TRUE(rt.native_types_compatible(any, int_type));
    EXPECT_TRUE(rt.native_types_compatible(any, array_int));

    // exact match should work
    EXPECT_TRUE(rt.native_types_compatible(int_type, int_type));

    // mismatched concrete types should fail
    EXPECT_FALSE(rt.native_types_compatible(int_type, rt.float_type()));
}

TEST_F(SmallsModules, GenericSumTypeParsingAndResolution)
{
    auto script = make_script(R"(
type Option!($T) = Some($T) | None;

fn main(): int {
    return 0;
}
)");

    EXPECT_NO_THROW(script.parse());
    ASSERT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    // Verify that the generic type was parsed correctly
    auto& decls = script.ast().decls;
    ASSERT_GE(decls.size(), 1u);

    auto* sum = dynamic_cast<nw::smalls::SumDecl*>(decls[0]);
    ASSERT_NE(sum, nullptr);
    EXPECT_TRUE(sum->is_generic());
    ASSERT_EQ(sum->type_params.size(), 1u);
    EXPECT_EQ(sum->type_params[0], "$T");
}

TEST_F(SmallsModules, GenericStructTypeInstantiation)
{
    auto script = make_script(R"(
type Pair!($A, $B) {
    first: $A;
    second: $B;
};

fn main(): int {
    var p: Pair!(int, int) = { 10, 20 };
    return p.first + p.second;
}
)");

    EXPECT_NO_THROW(script.parse());
    ASSERT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto& rt = nw::kernel::runtime();
    auto result = rt.execute_script(&script, "main");
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.value.data.ival, 30);
}

TEST_F(SmallsModules, GenericStructFloatFloat)
{
    auto script = make_script(R"(
type Pair!($A, $B) {
    first: $A;
    second: $B;
};

fn main(): float {
    var p: Pair!(float, float) = { 1.5, 2.5 };
    return p.first + p.second;
}
)");

    EXPECT_NO_THROW(script.parse());
    ASSERT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto& rt = nw::kernel::runtime();
    auto result = rt.execute_script(&script, "main");
    EXPECT_TRUE(result.ok()) << "Error: " << result.error_message;
}

TEST_F(SmallsModules, GenericStructIntBool)
{
    auto script = make_script(R"(
type Pair!($A, $B) {
    first: $A;
    second: $B;
};

fn main(): int {
    var p: Pair!(int, bool) = { 42, true };
    return p.first;
}
)");

    EXPECT_NO_THROW(script.parse());
    ASSERT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto& rt = nw::kernel::runtime();
    auto result = rt.execute_script(&script, "main");
    EXPECT_TRUE(result.ok()) << "Error: " << result.error_message;
    EXPECT_EQ(result.value.data.ival, 42);
}

TEST_F(SmallsModules, GenericStructFloatBool)
{
    auto script = make_script(R"(
type Pair!($A, $B) {
    first: $A;
    second: $B;
};

fn main(): int {
    var p: Pair!(float, bool) = { 3.14, true };
    return 1;
}
)");

    EXPECT_NO_THROW(script.parse());
    ASSERT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto& rt = nw::kernel::runtime();
    auto result = rt.execute_script(&script, "main");
    EXPECT_TRUE(result.ok()) << "Error: " << result.error_message;
}
