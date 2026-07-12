#include <gtest/gtest.h>

#include "nwn1_test_builders.hpp"

#include <nw/functions.hpp>
#include <nw/log.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Equips.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Placeable.hpp>
#include <nw/objects/Player.hpp>
#include <nw/objects/Store.hpp>
#include <nw/objects/Trigger.hpp>

#include <nw/kernel/Strings.hpp>
#include <nw/profiles/nwn1/Profile.hpp>
#include <nw/profiles/nwn1/constants.hpp>
#include <nw/profiles/nwn1/scriptbridge.hpp>
#include <nw/serialization/GffBuilder.hpp>
#include <nw/smalls/runtime.hpp>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <filesystem>
#include <limits>
#include <span>

namespace fs = std::filesystem;
namespace nwk = nw::kernel;

using namespace std::literals;

namespace {

int32_t creature_ability_score_from_script(const nw::Creature* creature, nw::Ability ability, bool base = false)
{
    if (!creature || ability == nw::Ability::invalid()) { return 0; }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nwn1::bridge::make_object_arg(creature->handle()));
    args.push_back(nw::smalls::Value::make_int(*ability));
    args.push_back(nw::smalls::Value::make_bool(base));
    return nwn1::bridge::call_nwn1_module_int("nwn1.creature", "get_ability_score", args).value_or(0);
}

nw::smalls::Value script_field(nw::smalls::Runtime& rt, const nw::smalls::Value& value, const char* field)
{
    const nw::smalls::StructDef* def = rt.get_struct_def(value.type_id);
    if (!def) { return {}; }

    const uint32_t index = def->field_index(field);
    if (index == std::numeric_limits<uint32_t>::max()) { return {}; }

    const auto& fd = def->fields[index];
    return rt.read_value_field_at_offset(value, fd.offset, fd.type_id);
}

int32_t script_int_field(nw::smalls::Runtime& rt, const nw::smalls::Value& value, const char* field)
{
    nw::smalls::Value field_value = script_field(rt, value, field);
    return field_value.type_id == rt.int_type() ? field_value.data.ival : 0;
}

nw::smalls::Value find_creature_appearance_propset(nw::smalls::Runtime& rt, const nw::Creature* creature)
{
    if (!creature) { return {}; }

    const auto tid = rt.type_id("core.creature.CreatureAppearance", false);
    if (tid == nw::smalls::invalid_type_id) { return {}; }
    return rt.find_propset_ref(tid, creature->handle());
}

nw::ObjectHandle inventory_destroy_probe_handle;
bool inventory_destroy_probe_seen = false;
bool inventory_destroy_probe_had_inventory = false;

void seed_base_fields(nw::ObjectBase& obj)
{
    obj.resref = nw::Resref{"clear_probe"};
    obj.name.set_strref(42);
    obj.name.add(nw::LanguageID::english, "clear probe");
    obj.tag = nwk::strings().intern("clear_probe");
    obj.comment = "clear probe";
    obj.palette_id = 7;
}

void expect_base_fields_cleared(const nw::ObjectBase& obj)
{
    EXPECT_EQ(obj.resref.length(), 0);
    EXPECT_EQ(obj.name.strref(), std::numeric_limits<uint32_t>::max());
    EXPECT_EQ(obj.name.size(), 0);
    EXPECT_FALSE(obj.tag);
    EXPECT_TRUE(obj.comment.empty());
    EXPECT_EQ(obj.palette_id, 0xFF);
}

void inventory_destroy_probe(nw::ObjectBase* obj)
{
    if (!obj || obj->handle() != inventory_destroy_probe_handle) { return; }

    inventory_destroy_probe_seen = true;
    inventory_destroy_probe_had_inventory = nwk::objects().components().find_inventory(*obj) != nullptr;
}

} // namespace

TEST(ObjectSystem, LoadCreature)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nwk::objects().load<nw::Creature>("nw_chicken");

    EXPECT_TRUE(ent);
    EXPECT_EQ(ent->resref, "nw_chicken");
    EXPECT_EQ(creature_ability_score_from_script(ent, nwn1::ability_dexterity, true), 7);
    auto& rt = nwk::runtime();
    auto appearance = find_creature_appearance_propset(rt, ent);
    ASSERT_NE(appearance.type_id, nw::smalls::invalid_type_id);
    EXPECT_EQ(script_int_field(rt, appearance, "appearance"), 31);

    auto ent2 = nwk::objects().get<nw::Creature>(ent->handle());
    EXPECT_EQ(ent, ent2);

    auto handle = ent->handle();
    nwk::objects().destroy(handle);
    EXPECT_FALSE(nwk::objects().valid(handle));

    auto ent_json_source = nwk::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    ASSERT_TRUE(ent_json_source);
    ASSERT_TRUE(ent_json_source->save("tmp/pl_agent_001.kernel.utc.json", "json"));

    auto ent3 = nwk::objects().load_file<nw::Creature>("tmp/pl_agent_001.kernel.utc.json");

    EXPECT_TRUE(ent3);
    EXPECT_EQ(ent3->resref, "pl_agent_001");
    appearance = find_creature_appearance_propset(rt, ent3);
    ASSERT_NE(appearance.type_id, nw::smalls::invalid_type_id);
    EXPECT_EQ(script_int_field(rt, appearance, "appearance"), 6);
    EXPECT_EQ(script_int_field(rt, appearance, "body_part_shin_left"), 1);
    EXPECT_TRUE(nw::get_equipped_item(ent3, nw::EquipIndex::chest));
}

TEST(ObjectSystem, ObjectByTag)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    std::vector<nw::Creature*> chickens;
    for (size_t i = 0; i < 10; ++i) {
        chickens.push_back(nwk::objects().load<nw::Creature>("nw_chicken"));
    }

    EXPECT_EQ(chickens[0]->tag.view(), "NW_CHICKEN");
    EXPECT_TRUE(nwk::objects().get_by_tag("NW_CHICKEN"));
    EXPECT_TRUE(nwk::objects().get_by_tag("NW_CHICKEN", 5));
    EXPECT_FALSE(nwk::objects().get_by_tag("NW_CHICKEN", 100));

    for (auto chicken : chickens) {
        nwk::objects().destroy(chicken->handle());
    }

    EXPECT_FALSE(nwk::objects().get_by_tag("NW_CHICKEN"));
}

TEST(ObjectSystem, LoadInstanceRejectsLegacyTypedJson)
{
    nlohmann::json legacy_instance = {
        {"$type", "UTI"},
    };

    auto* item = nwk::objects().load_instance<nw::Item>(legacy_instance);
    EXPECT_EQ(item, nullptr);
}

TEST(ObjectSystem, ObjectComponentsTrackSpatialState)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    auto& components = nwk::objects().components();
    const size_t initial_count = components.spatial_count();
    const size_t initial_local_data_count = components.local_data_count();
    const size_t initial_geometry_count = components.geometry_count();

    auto* item = nwk::objects().make<nw::Item>();
    ASSERT_NE(item, nullptr);
    auto* locals = components.get_or_create_locals(item->handle());
    ASSERT_NE(locals, nullptr);
    locals->set_int("test", 1);
    const std::array<glm::vec3, 3> geometry_points{
        glm::vec3{0.0f, 0.0f, 0.0f},
        glm::vec3{1.0f, 0.0f, 0.0f},
        glm::vec3{0.0f, 1.0f, 0.0f},
    };
    ASSERT_TRUE(components.set_geometry(item->handle(), std::span<const glm::vec3>{geometry_points}));
    const std::array<nw::ObjectSpawnPoint, 1> spawn_points{
        nw::ObjectSpawnPoint{.position = {2.0f, 3.0f, 4.0f}, .orientation = 0.75f},
    };
    ASSERT_TRUE(components.set_spawn_points(item->handle(), std::span<const nw::ObjectSpawnPoint>{spawn_points}));
    nw::Location location;
    location.area = static_cast<nw::ObjectID>(42);
    location.position = {1.0f, 2.0f, 3.0f};
    location.orientation = {0.0f, 1.0f, 0.0f};
    ASSERT_TRUE(components.set_location(item->handle(), location));
    ASSERT_TRUE(item->instantiate());

    auto handle = item->handle();
    auto* row = components.find_spatial(handle);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(components.spatial_count(), initial_count + 1);
    EXPECT_EQ(components.local_data_count(), initial_local_data_count + 1);
    EXPECT_EQ(components.geometry_count(), initial_geometry_count + 1);
    locals = components.find_locals(handle);
    ASSERT_NE(locals, nullptr);
    EXPECT_EQ(locals->get_int("test"), 1);
    auto* geometry = components.find_geometry(handle);
    ASSERT_NE(geometry, nullptr);
    ASSERT_EQ(geometry->points.size(), geometry_points.size());
    EXPECT_FLOAT_EQ(geometry->points[1].x, 1.0f);
    ASSERT_EQ(geometry->spawn_points.size(), spawn_points.size());
    EXPECT_FLOAT_EQ(geometry->spawn_points[0].position.z, 4.0f);
    EXPECT_FLOAT_EQ(geometry->spawn_points[0].orientation, 0.75f);
    EXPECT_EQ(row->area, static_cast<nw::ObjectID>(42));
    EXPECT_FLOAT_EQ(row->position.x, 1.0f);
    EXPECT_FLOAT_EQ(row->position.y, 2.0f);
    EXPECT_FLOAT_EQ(row->position.z, 3.0f);
    EXPECT_FLOAT_EQ(row->orientation.y, 1.0f);
    EXPECT_FLOAT_EQ(row->scale.x, 1.0f);
    EXPECT_FLOAT_EQ(row->scale.y, 1.0f);
    EXPECT_FLOAT_EQ(row->scale.z, 1.0f);

    EXPECT_TRUE(components.set_position(handle, {4.0f, 5.0f, 6.0f}));
    EXPECT_TRUE(components.set_scale(handle, {2.0f, 3.0f, 4.0f}));
    EXPECT_FALSE(components.set_scale(handle, {0.0f, 1.0f, 1.0f}));
    EXPECT_TRUE(components.set_velocity(handle, {7.0f, 8.0f, 9.0f}));
    const std::array<glm::vec3, 1> invalid_geometry{
        glm::vec3{std::numeric_limits<float>::infinity(), 0.0f, 0.0f},
    };
    EXPECT_FALSE(components.set_geometry(handle, std::span<const glm::vec3>{invalid_geometry}));
    geometry = components.find_geometry(handle);
    ASSERT_NE(geometry, nullptr);
    EXPECT_EQ(geometry->points.size(), geometry_points.size());
    const std::array<nw::ObjectSpawnPoint, 1> invalid_spawn_points{
        nw::ObjectSpawnPoint{.position = {std::numeric_limits<float>::infinity(), 0.0f, 0.0f}, .orientation = 0.0f},
    };
    EXPECT_FALSE(components.set_spawn_points(handle, std::span<const nw::ObjectSpawnPoint>{invalid_spawn_points}));
    geometry = components.find_geometry(handle);
    ASSERT_NE(geometry, nullptr);
    EXPECT_EQ(geometry->spawn_points.size(), spawn_points.size());
    EXPECT_TRUE(components.set_geometry(handle, std::span<const glm::vec3>{}));
    geometry = components.find_geometry(handle);
    ASSERT_NE(geometry, nullptr);
    EXPECT_TRUE(geometry->points.empty());
    ASSERT_EQ(geometry->spawn_points.size(), spawn_points.size());
    row = components.find_spatial(handle);
    ASSERT_NE(row, nullptr);
    EXPECT_FLOAT_EQ(row->position.x, 4.0f);
    EXPECT_FLOAT_EQ(row->position.y, 5.0f);
    EXPECT_FLOAT_EQ(row->position.z, 6.0f);
    EXPECT_FLOAT_EQ(row->scale.x, 2.0f);
    EXPECT_FLOAT_EQ(row->scale.y, 3.0f);
    EXPECT_FLOAT_EQ(row->scale.z, 4.0f);
    EXPECT_FLOAT_EQ(row->velocity.x, 7.0f);
    EXPECT_FLOAT_EQ(row->velocity.y, 8.0f);
    EXPECT_FLOAT_EQ(row->velocity.z, 9.0f);
    location = components.location(handle);
    EXPECT_FLOAT_EQ(location.position.x, 4.0f);
    EXPECT_FLOAT_EQ(location.position.y, 5.0f);
    EXPECT_FLOAT_EQ(location.position.z, 6.0f);

    auto* item2 = nwk::objects().make<nw::Item>();
    ASSERT_NE(item2, nullptr);
    ASSERT_TRUE(item2->instantiate());
    const auto handle2 = item2->handle();
    EXPECT_TRUE(components.set_position(handle2, {8.0f, 9.0f, 10.0f}));
    ASSERT_TRUE(components.set_geometry(handle2, std::span<const glm::vec3>{geometry_points}));
    EXPECT_EQ(components.spatial_count(), initial_count + 2);
    EXPECT_EQ(components.geometry_count(), initial_geometry_count + 2);

    nwk::objects().destroy(handle);
    EXPECT_EQ(components.find_spatial(handle), nullptr);
    EXPECT_EQ(components.find_locals(handle), nullptr);
    EXPECT_EQ(components.find_geometry(handle), nullptr);
    EXPECT_FALSE(components.set_velocity(handle, {1.0f, 1.0f, 1.0f}));

    row = components.find_spatial(handle2);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->owner, handle2);
    geometry = components.find_geometry(handle2);
    ASSERT_NE(geometry, nullptr);
    EXPECT_EQ(geometry->owner, handle2);

    EXPECT_TRUE(components.set_geometry(handle2, std::span<const glm::vec3>{}));
    EXPECT_EQ(components.find_geometry(handle2), nullptr);

    nwk::objects().destroy(handle2);
    EXPECT_EQ(components.spatial_count(), initial_count);
    EXPECT_EQ(components.local_data_count(), initial_local_data_count);
    EXPECT_EQ(components.geometry_count(), initial_geometry_count);
}

TEST(ObjectSystem, ObjectComponentsTrackInventoryState)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    auto& components = nwk::objects().components();
    const size_t initial_count = components.inventory_count();
    const size_t initial_store_count = components.store_inventory_count();

    auto* blank_item = nwk::objects().make<nw::Item>();
    ASSERT_NE(blank_item, nullptr);
    ASSERT_TRUE(blank_item->instantiate());
    EXPECT_EQ(components.find_inventory(*blank_item), nullptr);
    EXPECT_EQ(components.inventory_count(), initial_count);
    seed_base_fields(*blank_item);
    ASSERT_TRUE(components.set_item_layout(blank_item->handle(), 1, 1));
    ASSERT_TRUE(components.add_visual_model(blank_item->handle(), nw::ObjectVisualModel{.model = nw::Resref{"clear_probe"}}));
    blank_item->clear();
    expect_base_fields_cleared(*blank_item);
    EXPECT_EQ(components.find_inventory(*blank_item), nullptr);
    EXPECT_EQ(components.find_item_layout(blank_item->handle()), nullptr);
    EXPECT_EQ(components.find_visual(blank_item->handle()), nullptr);
    EXPECT_EQ(components.inventory_count(), initial_count);
    nwk::objects().destroy(blank_item->handle());

    auto* blank_creature = nwk::objects().make<nw::Creature>();
    ASSERT_NE(blank_creature, nullptr);
    ASSERT_TRUE(blank_creature->instantiate());
    EXPECT_EQ(components.find_inventory(*blank_creature), nullptr);
    EXPECT_EQ(components.inventory_count(), initial_count);
    seed_base_fields(*blank_creature);
    auto* clear_effect = nwn1::effect_haste();
    ASSERT_NE(clear_effect, nullptr);
    const auto clear_effect_handle = clear_effect->handle().to_typed_handle();
    ASSERT_TRUE(blank_creature->effects().add(clear_effect));
    const auto clear_effect_version = blank_creature->effects().effect_version;
    blank_creature->clear();
    expect_base_fields_cleared(*blank_creature);
    EXPECT_EQ(blank_creature->effects().size(), 0);
    EXPECT_GT(blank_creature->effects().effect_version, clear_effect_version);
    EXPECT_EQ(nwk::effects().get(clear_effect_handle), nullptr);
    EXPECT_EQ(components.find_inventory(*blank_creature), nullptr);
    EXPECT_EQ(components.inventory_count(), initial_count);
    nwk::objects().destroy(blank_creature->handle());

    auto* blank_placeable = nwk::objects().make<nw::Placeable>();
    ASSERT_NE(blank_placeable, nullptr);
    ASSERT_TRUE(blank_placeable->instantiate());
    EXPECT_EQ(components.find_inventory(*blank_placeable), nullptr);
    EXPECT_EQ(components.inventory_count(), initial_count);
    seed_base_fields(*blank_placeable);
    blank_placeable->clear();
    expect_base_fields_cleared(*blank_placeable);
    EXPECT_EQ(components.find_inventory(*blank_placeable), nullptr);
    EXPECT_EQ(components.inventory_count(), initial_count);
    nwk::objects().destroy(blank_placeable->handle());

    auto* blank_store = nwk::objects().make<nw::Store>();
    ASSERT_NE(blank_store, nullptr);
    ASSERT_TRUE(blank_store->instantiate());
    EXPECT_EQ(components.find_store_inventory(*blank_store), nullptr);
    EXPECT_EQ(components.store_inventory_count(), initial_store_count);
    nw::GffBuilder blank_store_gff{nw::Store::serial_id};
    ASSERT_TRUE(nw::serialize(blank_store, blank_store_gff.top, nw::SerializationProfile::blueprint));
    EXPECT_EQ(components.find_store_inventory(*blank_store), nullptr);
    EXPECT_EQ(components.store_inventory_count(), initial_store_count);
    seed_base_fields(*blank_store);
    blank_store->clear();
    expect_base_fields_cleared(*blank_store);
    EXPECT_EQ(components.find_store_inventory(*blank_store), nullptr);
    EXPECT_EQ(components.store_inventory_count(), initial_store_count);
    nwk::objects().destroy(blank_store->handle());

    auto* blank_trigger = nwk::objects().make<nw::Trigger>();
    ASSERT_NE(blank_trigger, nullptr);
    std::array trigger_geometry{
        glm::vec3{0.0f, 0.0f, 0.0f},
        glm::vec3{1.0f, 0.0f, 0.0f},
        glm::vec3{0.0f, 1.0f, 0.0f},
    };
    seed_base_fields(*blank_trigger);
    ASSERT_TRUE(components.set_position(blank_trigger->handle(), {1.0f, 2.0f, 3.0f}));
    ASSERT_TRUE(components.set_geometry(blank_trigger->handle(), std::span<const glm::vec3>{trigger_geometry}));
    ASSERT_TRUE(components.add_visual_model(blank_trigger->handle(), nw::ObjectVisualModel{.model = nw::Resref{"clear_probe"}}));
    blank_trigger->clear();
    expect_base_fields_cleared(*blank_trigger);
    EXPECT_EQ(components.find_spatial(blank_trigger->handle()), nullptr);
    EXPECT_EQ(components.find_geometry(blank_trigger->handle()), nullptr);
    EXPECT_EQ(components.find_visual(blank_trigger->handle()), nullptr);
    nwk::objects().destroy(blank_trigger->handle());

    auto* item = nwk::objects().make<nw::Item>();
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(components.find_inventory(*item), nullptr);
    EXPECT_EQ(components.inventory_count(), initial_count);

    auto& inventory = item->inventory();
    EXPECT_EQ(&inventory, components.find_inventory(*item));
    EXPECT_EQ(components.inventory_count(), initial_count + 1);

    const auto handle = item->handle();
    nwk::objects().destroy(handle);
    EXPECT_EQ(components.inventory_count(), initial_count);

    auto* container = nwk::objects().make<nw::Item>();
    ASSERT_NE(container, nullptr);
    auto* child = nwk::objects().make<nw::Item>();
    ASSERT_NE(child, nullptr);
    ASSERT_TRUE(components.set_item_layout(child->handle(), 1, 1));
    ASSERT_TRUE(container->inventory().add_item(child));
    EXPECT_EQ(components.inventory_count(), initial_count + 1);

    inventory_destroy_probe_handle = child->handle();
    inventory_destroy_probe_seen = false;
    inventory_destroy_probe_had_inventory = false;
    nwk::objects().set_destroy_callback(inventory_destroy_probe);
    container->clear();
    nwk::objects().set_destroy_callback(nullptr);

    EXPECT_TRUE(inventory_destroy_probe_seen);
    EXPECT_FALSE(inventory_destroy_probe_had_inventory);
    EXPECT_FALSE(nwk::objects().valid(inventory_destroy_probe_handle));
    EXPECT_EQ(components.find_inventory(*container), nullptr);
    EXPECT_EQ(components.inventory_count(), initial_count);
    nwk::objects().destroy(container->handle());

    {
        nw::Item transient;
        auto& transient_inventory = transient.inventory();
        EXPECT_EQ(&transient_inventory, components.find_inventory(transient));
        EXPECT_EQ(components.inventory_count(), initial_count + 1);

        transient.clear();

        EXPECT_EQ(components.find_inventory(transient), nullptr);
        EXPECT_EQ(components.inventory_count(), initial_count);
    }

    {
        nw::Item transient;
        static_cast<void>(transient.inventory());
        EXPECT_EQ(components.inventory_count(), initial_count + 1);

        nw::ObjectHandle fake_handle;
        fake_handle.id = static_cast<nw::ObjectID>(4242);
        fake_handle.type = nw::ObjectType::item;
        fake_handle.version = 0;
        transient.set_handle(fake_handle);

        transient.clear();

        EXPECT_EQ(components.find_inventory(transient), nullptr);
        EXPECT_EQ(components.inventory_count(), initial_count);
    }

    {
        nw::Item transient;
        auto& transient_inventory = transient.inventory();
        EXPECT_EQ(components.inventory_count(), initial_count + 1);

        nw::ObjectHandle fake_handle;
        fake_handle.id = static_cast<nw::ObjectID>(4242);
        fake_handle.type = nw::ObjectType::item;
        fake_handle.version = 0;
        transient.set_handle(fake_handle);

        EXPECT_EQ(&transient_inventory, components.find_inventory(transient));
        EXPECT_EQ(components.inventory_count(), initial_count + 1);
    }

    EXPECT_EQ(components.inventory_count(), initial_count);
}

TEST(ObjectSystem, ObjectDestroyClearsAppliedEffects)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    auto* creature = nwk::objects().load_file<nw::Creature>("test_data/user/development/test_creature.utc");
    ASSERT_NE(creature, nullptr);

    auto* effect = nwn1::effect_haste();
    ASSERT_NE(effect, nullptr);
    const auto effect_handle = effect->handle().to_typed_handle();
    ASSERT_TRUE(nwk::effects().apply_to(creature, effect));
    ASSERT_EQ(creature->effects().size(), 1);

    const auto creature_handle = creature->handle();
    nwk::objects().destroy(creature_handle);

    EXPECT_FALSE(nwk::objects().valid(creature_handle));
    EXPECT_EQ(nwk::effects().get(effect_handle), nullptr);
}

TEST(ObjectSystem, LoadPlayer)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto pl = nwk::objects().load_player("CDKEY", "testmonkpc");
    EXPECT_TRUE(pl);

    auto pl2 = nwk::objects().load_player("WRONG", "testmonkpc");
    EXPECT_FALSE(pl2);

    auto pl3 = nwk::objects().load_file<nw::Player>(fs::path("test_data/user/servervault/CDKEY/testmonkpc.bic"));
    EXPECT_TRUE(pl3);
}
