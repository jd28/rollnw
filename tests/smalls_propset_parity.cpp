/// Phase 4 parity gate: must be green before any C++ field hollowing.
///
/// Strategy: for each test, create TWO fresh objects via make+deserialize
/// (no instantiate callbacks, no hp_max recomputation). Creatures compare
/// deserialize-initialized propsets with importer output.
/// Both start from the same deserialized C++ state, so they should produce
/// identical propset field values for all mapped fields.
///
/// Tests:
/// 1. Importer vs legacy/deserialized parity.
/// 2. GFF round-trip parity: export → reload produces value-equivalent propsets.
/// 3. Skip fields stay zero after import.
/// 4. Registry compile-time sanity (no duplicate field names).

#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Strings.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Door.hpp>
#include <nw/objects/Encounter.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Placeable.hpp>
#include <nw/objects/Sound.hpp>
#include <nw/objects/Store.hpp>
#include <nw/objects/Trigger.hpp>
#include <nw/objects/Waypoint.hpp>
#include <nw/profiles/nwn1/propset_gff_exporter.hpp>
#include <nw/profiles/nwn1/propset_gff_importer.hpp>
#include <nw/profiles/nwn1/propset_gff_policy.hpp>
#include <nw/resources/assets.hpp>
#include <nw/serialization/Gff.hpp>
#include <nw/serialization/GffBuilder.hpp>
#include <nw/smalls/Array.hpp>
#include <nw/smalls/runtime.hpp>

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;
namespace nwk = nw::kernel;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static const nw::smalls::StructDef* sdef(nw::smalls::Runtime& rt, nw::smalls::TypeID tid)
{
    return rt.get_struct_def(tid);
}

static int32_t read_int(nw::smalls::Runtime& rt, const nw::smalls::Value& ref,
    const nw::smalls::StructDef* def, const char* field_name)
{
    uint32_t idx = def->field_index(field_name);
    if (idx == UINT32_MAX) { return INT32_MIN; }
    nw::smalls::Value v = rt.read_value_field_at_offset(ref,
        def->fields[idx].offset, rt.int_type());
    return v.data.ival;
}

static float read_float_f(nw::smalls::Runtime& rt, const nw::smalls::Value& ref,
    const nw::smalls::StructDef* def, const char* field_name)
{
    uint32_t idx = def->field_index(field_name);
    if (idx == UINT32_MAX) { return 0.0f; }
    nw::smalls::Value v = rt.read_value_field_at_offset(ref,
        def->fields[idx].offset, rt.float_type());
    return v.data.fval;
}

static nw::Resref read_resref(nw::smalls::Runtime& rt, const nw::smalls::Value& ref,
    const nw::smalls::StructDef* def, const char* field_name)
{
    uint32_t idx = def->field_index(field_name);
    if (idx == UINT32_MAX) { return nw::Resref{}; }
    const nw::smalls::FieldDef& fd = def->fields[idx];
    nw::smalls::Value v = rt.read_value_field_at_offset(ref, fd.offset, fd.type_id);
    auto* ptr = static_cast<nw::Resref*>(rt.get_value_data_ptr(v));
    return ptr ? *ptr : nw::Resref{};
}

static nw::String read_string(nw::smalls::Runtime& rt, const nw::smalls::Value& ref,
    const nw::smalls::StructDef* def, const char* field_name)
{
    uint32_t idx = def->field_index(field_name);
    if (idx == UINT32_MAX) { return {}; }
    const nw::smalls::FieldDef& fd = def->fields[idx];
    nw::smalls::Value v = rt.read_value_field_at_offset(ref, fd.offset, fd.type_id);
    return nw::String{nw::smalls::ScriptString{v.data.hptr}.view(rt)};
}

static nw::TextRef read_text_ref(nw::smalls::Runtime& rt, const nw::smalls::Value& ref,
    const nw::smalls::StructDef* def, const char* field_name)
{
    uint32_t idx = def->field_index(field_name);
    if (idx == UINT32_MAX) { return nw::TextRef{}; }
    const nw::smalls::FieldDef& fd = def->fields[idx];
    nw::smalls::Value v = rt.read_value_field_at_offset(ref, fd.offset, fd.type_id);
    auto* ptr = static_cast<nw::TextRef*>(rt.get_value_data_ptr(v));
    return ptr ? *ptr : nw::TextRef{};
}

static nw::Resref read_resref_elem(nw::smalls::Runtime& rt, const nw::smalls::Value& ref,
    const nw::smalls::StructDef* def, const char* field_name, size_t idx)
{
    uint32_t fi = def->field_index(field_name);
    if (fi == UINT32_MAX) { return {}; }
    const nw::smalls::FieldDef& fd = def->fields[fi];
    if (!fd.is_unmanaged_array) { return {}; }
    nw::smalls::Value arr_val = rt.read_value_field_at_offset(ref, fd.offset, fd.type_id);
    nw::TypedHandle h = nw::TypedHandle::from_ull(arr_val.data.handle);
    nw::smalls::IArray* arr = rt.object_pool().get_unmanaged_array(h);
    if (!arr || idx >= arr->size()) { return {}; }
    nw::smalls::Value v;
    arr->get_value(idx, v, rt);
    auto* ptr = static_cast<nw::Resref*>(rt.get_value_data_ptr(v));
    return ptr ? *ptr : nw::Resref{};
}

static size_t read_array_size(nw::smalls::Runtime& rt, const nw::smalls::Value& ref,
    const nw::smalls::StructDef* def, const char* field_name)
{
    uint32_t fi = def->field_index(field_name);
    if (fi == UINT32_MAX) { return 0; }
    const nw::smalls::FieldDef& fd = def->fields[fi];
    if (!fd.is_unmanaged_array) { return 0; }
    nw::smalls::Value arr_val = rt.read_value_field_at_offset(ref, fd.offset, fd.type_id);
    nw::TypedHandle h = nw::TypedHandle::from_ull(arr_val.data.handle);
    nw::smalls::IArray* arr = rt.object_pool().get_unmanaged_array(h);
    return arr ? arr->size() : 0;
}

static nw::smalls::IArray* read_array(nw::smalls::Runtime& rt, const nw::smalls::Value& ref,
    const nw::smalls::StructDef* def, const char* field_name)
{
    uint32_t fi = def->field_index(field_name);
    if (fi == UINT32_MAX) { return nullptr; }
    const nw::smalls::FieldDef& fd = def->fields[fi];
    if (!fd.is_unmanaged_array) { return nullptr; }
    nw::smalls::Value arr_val = rt.read_value_field_at_offset(ref, fd.offset, fd.type_id);
    nw::TypedHandle h = nw::TypedHandle::from_ull(arr_val.data.handle);
    return rt.object_pool().get_unmanaged_array(h);
}

// Read element from a fixed array (stride 4) or unmanaged dynamic array.
static int32_t read_elem(nw::smalls::Runtime& rt, const nw::smalls::Value& ref,
    const nw::smalls::StructDef* def, const char* field_name, size_t idx)
{
    uint32_t fi = def->field_index(field_name);
    if (fi == UINT32_MAX) { return INT32_MIN; }
    const nw::smalls::FieldDef& fd = def->fields[fi];
    if (fd.is_unmanaged_array) {
        nw::smalls::Value arr_val = rt.read_value_field_at_offset(ref, fd.offset, fd.type_id);
        nw::TypedHandle h = nw::TypedHandle::from_ull(arr_val.data.handle);
        nw::smalls::IArray* arr = rt.object_pool().get_unmanaged_array(h);
        if (!arr || idx >= arr->size()) { return INT32_MIN; }
        nw::smalls::Value v;
        arr->get_value(idx, v, rt);
        return v.data.ival;
    }
    // Fixed array
    nw::smalls::Value v = rt.read_value_field_at_offset(ref,
        fd.offset + uint32_t(idx) * 4u, rt.int_type());
    return v.data.ival;
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class SmallsPropsetParity : public ::testing::Test {
protected:
    void SetUp() override
    {
        nw::kernel::services().start();
        nw::kernel::runtime().add_module_path(fs::path("stdlib/core"));
        nw::kernel::runtime().add_module_path(fs::path("stdlib/nwn1"));
        registry_ = std::make_unique<nwn1::PropsetGffPolicyRegistry>(
            nwn1::make_nwn1_propset_policy_registry());
        auto& rt = nw::kernel::runtime();
        importer_ = std::make_unique<nwn1::PropsetGffImporter>(&rt, registry_.get());
        exporter_ = std::make_unique<nwn1::PropsetGffExporter>(&rt, registry_.get());
    }

    void TearDown() override
    {
        importer_.reset();
        exporter_.reset();
        registry_.reset();
        nw::kernel::services().shutdown();
    }

    // Helper: create a creature via make+deserialize, populate via legacy or importer.
    // Does NOT call instantiate (avoids hp_max recomputation side-effects).
    nw::Creature* make_creature(const char* path)
    {
        auto* cre = nwk::objects().make<nw::Creature>();
        if (!cre) { return nullptr; }
        nw::Gff gff(path);
        if (!gff.valid()) { return nullptr; }
        nw::deserialize(cre, gff.toplevel(), nw::SerializationProfile::blueprint);
        return cre;
    }

    nw::Item* make_item(const char* path)
    {
        auto* item = nwk::objects().make<nw::Item>();
        if (!item) { return nullptr; }
        nw::Gff gff(path);
        if (!gff.valid()) { return nullptr; }
        nw::deserialize(item, gff.toplevel(), nw::SerializationProfile::blueprint);
        return item;
    }

    nw::Sound* make_sound(const char* path)
    {
        auto* sound = nwk::objects().make<nw::Sound>();
        if (!sound) { return nullptr; }
        nw::Gff gff(path);
        if (!gff.valid()) { return nullptr; }
        nw::deserialize(sound, gff.toplevel(), nw::SerializationProfile::blueprint);
        return sound;
    }

    nw::Waypoint* make_waypoint(const char* path)
    {
        auto* waypoint = nwk::objects().make<nw::Waypoint>();
        if (!waypoint) { return nullptr; }
        nw::Gff gff(path);
        if (!gff.valid()) { return nullptr; }
        nw::deserialize(waypoint, gff.toplevel(), nw::SerializationProfile::blueprint);
        return waypoint;
    }

    nw::Store* make_store(const char* path)
    {
        auto* store = nwk::objects().make<nw::Store>();
        if (!store) { return nullptr; }
        nw::Gff gff(path);
        if (!gff.valid()) { return nullptr; }
        nw::deserialize(store, gff.toplevel(), nw::SerializationProfile::blueprint);
        return store;
    }

    nw::Trigger* make_trigger(const char* path)
    {
        auto* trigger = nwk::objects().make<nw::Trigger>();
        if (!trigger) { return nullptr; }
        nw::Gff gff(path);
        if (!gff.valid()) { return nullptr; }
        nw::deserialize(trigger, gff.toplevel(), nw::SerializationProfile::blueprint);
        return trigger;
    }

    nw::Door* make_door(const char* path)
    {
        auto* door = nwk::objects().make<nw::Door>();
        if (!door) { return nullptr; }
        nw::Gff gff(path);
        if (!gff.valid()) { return nullptr; }
        nw::deserialize(door, gff.toplevel(), nw::SerializationProfile::blueprint);
        return door;
    }

    nw::Placeable* make_placeable(const char* path)
    {
        auto* placeable = nwk::objects().make<nw::Placeable>();
        if (!placeable) { return nullptr; }
        nw::Gff gff(path);
        if (!gff.valid()) { return nullptr; }
        nw::deserialize(placeable, gff.toplevel(), nw::SerializationProfile::blueprint);
        return placeable;
    }

    nw::Encounter* make_encounter(const char* path)
    {
        auto* encounter = nwk::objects().make<nw::Encounter>();
        if (!encounter) { return nullptr; }
        nw::Gff gff(path);
        if (!gff.valid()) { return nullptr; }
        nw::deserialize(encounter, gff.toplevel(), nw::SerializationProfile::blueprint);
        return encounter;
    }

    std::unique_ptr<nwn1::PropsetGffPolicyRegistry> registry_;
    std::unique_ptr<nwn1::PropsetGffImporter> importer_;
    std::unique_ptr<nwn1::PropsetGffExporter> exporter_;
};

// ---------------------------------------------------------------------------
// Test 1: registry compile-time sanity
// ---------------------------------------------------------------------------

TEST_F(SmallsPropsetParity, RegistryFieldNamesUnique)
{
    for (const auto& policy : registry_->policies()) {
        std::vector<std::string_view> names;
        for (const auto& fp : policy.fields) {
            names.emplace_back(fp.propset_field_name);
        }
        auto sorted = names;
        std::sort(sorted.begin(), sorted.end());
        auto dup = std::adjacent_find(sorted.begin(), sorted.end());
        EXPECT_EQ(dup, sorted.end())
            << "Duplicate field name in " << policy.qualified_name;
    }
}

TEST_F(SmallsPropsetParity, RegistryKnownPropsets)
{
    EXPECT_EQ(registry_->find("core.object.ObjectCommon"), nullptr);
    EXPECT_NE(registry_->find("core.creature.CreatureDescriptor"), nullptr);
    EXPECT_NE(registry_->find("core.creature.CreatureAppearance"), nullptr);
    EXPECT_NE(registry_->find("core.creature.CreatureStats"), nullptr);
    EXPECT_NE(registry_->find("core.creature.CreatureHealth"), nullptr);
    EXPECT_NE(registry_->find("core.creature.CreatureLevels"), nullptr);
    EXPECT_NE(registry_->find("core.creature.CreatureCombat"), nullptr);
    EXPECT_NE(registry_->find("core.item.ItemDescriptor"), nullptr);
    EXPECT_NE(registry_->find("core.item.ItemStats"), nullptr);
    EXPECT_NE(registry_->find("core.item.ItemVisuals"), nullptr);
    EXPECT_NE(registry_->find("core.sound.SoundState"), nullptr);
    EXPECT_NE(registry_->find("core.store.StoreState"), nullptr);
    EXPECT_NE(registry_->find("core.trigger.TriggerState"), nullptr);
    EXPECT_NE(registry_->find("core.encounter.EncounterState"), nullptr);
    EXPECT_NE(registry_->find("core.waypoint.WaypointState"), nullptr);
    EXPECT_NE(registry_->find("core.door.DoorState"), nullptr);
    EXPECT_NE(registry_->find("core.placeable.PlaceableState"), nullptr);
}

TEST_F(SmallsPropsetParity, GffDeserializeInitializesSimpleObjectPropsets)
{
    auto& rt = nw::kernel::runtime();
    ASSERT_NE(rt.load_module("core.sound"), nullptr);
    ASSERT_NE(rt.load_module("core.waypoint"), nullptr);
    ASSERT_NE(rt.load_module("core.store"), nullptr);
    ASSERT_NE(rt.load_module("core.trigger"), nullptr);
    ASSERT_NE(rt.load_module("core.door"), nullptr);
    ASSERT_NE(rt.load_module("core.placeable"), nullptr);
    ASSERT_NE(rt.load_module("core.encounter"), nullptr);

    auto find_ref = [&rt](const nw::ObjectBase* obj, const char* type_name) {
        if (!obj) { return nw::smalls::Value{}; }
        auto tid = rt.type_id(type_name, false);
        EXPECT_NE(tid, nw::smalls::invalid_type_id) << type_name;
        if (tid == nw::smalls::invalid_type_id) { return nw::smalls::Value{}; }
        auto ref = rt.find_propset_ref(tid, obj->handle());
        EXPECT_NE(ref.type_id, nw::smalls::invalid_type_id) << type_name;
        return ref;
    };

    const char* sound_path = "test_data/user/development/blue_bell.uts";
    nw::Gff sound_gff(sound_path);
    ASSERT_TRUE(sound_gff.valid());
    uint8_t sound_volume = 0;
    ASSERT_TRUE(sound_gff.toplevel().get_to("Volume", sound_volume));
    size_t sound_count = sound_gff.toplevel()["Sounds"].size();

    auto* sound = make_sound(sound_path);
    ASSERT_NE(sound, nullptr);
    auto sound_ref = find_ref(sound, "core.sound.SoundState");
    ASSERT_NE(sound_ref.type_id, nw::smalls::invalid_type_id);
    auto sound_def = sdef(rt, sound_ref.type_id);
    ASSERT_NE(sound_def, nullptr);
    EXPECT_EQ(read_array_size(rt, sound_ref, sound_def, "sounds"), sound_count);
    EXPECT_GT(sound_count, size_t{0});
    EXPECT_EQ(read_int(rt, sound_ref, sound_def, "volume"),
        static_cast<int32_t>(sound_volume));

    const char* waypoint_path = "test_data/user/development/wp_behexit001.utw";
    nw::Gff waypoint_gff(waypoint_path);
    ASSERT_TRUE(waypoint_gff.valid());
    uint8_t waypoint_appearance = 0;
    ASSERT_TRUE(waypoint_gff.toplevel().get_to("Appearance", waypoint_appearance));

    auto* waypoint = make_waypoint(waypoint_path);
    ASSERT_NE(waypoint, nullptr);
    auto waypoint_ref = find_ref(waypoint, "core.waypoint.WaypointState");
    ASSERT_NE(waypoint_ref.type_id, nw::smalls::invalid_type_id);
    auto waypoint_def = sdef(rt, waypoint_ref.type_id);
    ASSERT_NE(waypoint_def, nullptr);
    EXPECT_EQ(read_int(rt, waypoint_ref, waypoint_def, "appearance"),
        static_cast<int32_t>(waypoint_appearance));

    const char* store_path = "test_data/user/development/storethief002.utm";
    nw::Gff store_gff(store_path);
    ASSERT_TRUE(store_gff.valid());
    int32_t store_markup = 0;
    ASSERT_TRUE(store_gff.toplevel().get_to("MarkUp", store_markup));

    auto* store = make_store(store_path);
    ASSERT_NE(store, nullptr);
    auto store_ref = find_ref(store, "core.store.StoreState");
    ASSERT_NE(store_ref.type_id, nw::smalls::invalid_type_id);
    auto store_def = sdef(rt, store_ref.type_id);
    ASSERT_NE(store_def, nullptr);
    EXPECT_EQ(read_int(rt, store_ref, store_def, "markup"), store_markup);

    const char* trigger_path = "test_data/user/development/pl_spray_sewage.utt";
    nw::Gff trigger_gff(trigger_path);
    ASSERT_TRUE(trigger_gff.valid());
    int32_t trigger_type = 0;
    ASSERT_TRUE(trigger_gff.toplevel().get_to("Type", trigger_type));

    auto* trigger = make_trigger(trigger_path);
    ASSERT_NE(trigger, nullptr);
    auto trigger_ref = find_ref(trigger, "core.trigger.TriggerState");
    ASSERT_NE(trigger_ref.type_id, nw::smalls::invalid_type_id);
    auto trigger_def = sdef(rt, trigger_ref.type_id);
    ASSERT_NE(trigger_def, nullptr);
    EXPECT_EQ(read_int(rt, trigger_ref, trigger_def, "trigger_type"), trigger_type);

    const char* door_path = "test_data/user/development/door_ttr_002.utd";
    nw::Gff door_gff(door_path);
    ASSERT_TRUE(door_gff.valid());
    uint32_t door_appearance = 0;
    ASSERT_TRUE(door_gff.toplevel().get_to("Appearance", door_appearance));

    auto* door = make_door(door_path);
    ASSERT_NE(door, nullptr);
    auto door_ref = find_ref(door, "core.door.DoorState");
    ASSERT_NE(door_ref.type_id, nw::smalls::invalid_type_id);
    auto door_def = sdef(rt, door_ref.type_id);
    ASSERT_NE(door_def, nullptr);
    EXPECT_EQ(read_int(rt, door_ref, door_def, "appearance"), static_cast<int32_t>(door_appearance));

    auto* placeable = make_placeable("test_data/user/development/arrowcorpse001.utp");
    ASSERT_NE(placeable, nullptr);
    auto placeable_ref = find_ref(placeable, "core.placeable.PlaceableState");
    ASSERT_NE(placeable_ref.type_id, nw::smalls::invalid_type_id);
    auto placeable_def = sdef(rt, placeable_ref.type_id);
    ASSERT_NE(placeable_def, nullptr);
    EXPECT_EQ(read_int(rt, placeable_ref, placeable_def, "appearance"), 109);

    const char* encounter_path = "test_data/user/development/boundelementallo.ute";
    nw::Gff encounter_gff(encounter_path);
    ASSERT_TRUE(encounter_gff.valid());
    const size_t encounter_creature_count = encounter_gff.toplevel()["CreatureList"].size();
    int32_t encounter_creatures_max = 0;
    ASSERT_TRUE(encounter_gff.toplevel().get_to("MaxCreatures", encounter_creatures_max));

    auto* encounter = make_encounter(encounter_path);
    ASSERT_NE(encounter, nullptr);
    auto encounter_ref = find_ref(encounter, "core.encounter.EncounterState");
    ASSERT_NE(encounter_ref.type_id, nw::smalls::invalid_type_id);
    auto encounter_def = sdef(rt, encounter_ref.type_id);
    ASSERT_NE(encounter_def, nullptr);
    EXPECT_EQ(read_array_size(rt, encounter_ref, encounter_def, "creatures"),
        encounter_creature_count);
    EXPECT_GT(encounter_creature_count, size_t{0});
    EXPECT_EQ(read_int(rt, encounter_ref, encounter_def, "creatures_max"),
        encounter_creatures_max);
}

// ---------------------------------------------------------------------------
// Test 2: importer ≡ deserialize-initialized propsets (no instantiation)
// ---------------------------------------------------------------------------

static void check_common_parity(
    nw::smalls::Runtime&,
    const nw::ObjectBase* legacy, const nw::ObjectBase* imported)
{
    ASSERT_NE(legacy, nullptr);
    ASSERT_NE(imported, nullptr);
    EXPECT_EQ(legacy->resref, imported->resref);
    EXPECT_EQ(legacy->name, imported->name);
    EXPECT_EQ(legacy->palette_id, imported->palette_id);
}

static void check_item_descriptor_parity(
    nw::smalls::Runtime& rt,
    nw::Item* item_legacy, nw::Item* item_import)
{
    auto tid = rt.type_id("core.item.ItemDescriptor", false);
    ASSERT_NE(tid, nw::smalls::invalid_type_id);
    auto def = sdef(rt, tid);
    ASSERT_NE(def, nullptr);

    auto ref_l = rt.get_or_create_propset_ref(tid, item_legacy->handle());
    auto ref_i = rt.get_or_create_propset_ref(tid, item_import->handle());
    ASSERT_NE(ref_l.type_id, nw::smalls::invalid_type_id);
    ASSERT_NE(ref_i.type_id, nw::smalls::invalid_type_id);

    const char* text_fields[] = {
        "description", "description_id",
        nullptr};
    for (const char** f = text_fields; *f; ++f) {
        EXPECT_EQ(nwk::strings().to_locstring(read_text_ref(rt, ref_l, def, *f)),
            nwk::strings().to_locstring(read_text_ref(rt, ref_i, def, *f)))
            << "ItemDescriptor." << *f;
    }
}

static void check_item_stats_parity(
    nw::smalls::Runtime& rt,
    nw::Item* item_legacy, nw::Item* item_import)
{
    auto tid = rt.type_id("core.item.ItemStats", false);
    ASSERT_NE(tid, nw::smalls::invalid_type_id);
    auto def = sdef(rt, tid);
    ASSERT_NE(def, nullptr);

    auto ref_l = rt.get_or_create_propset_ref(tid, item_legacy->handle());
    auto ref_i = rt.get_or_create_propset_ref(tid, item_import->handle());

    const char* fields[] = {
        "base_item", "stack_size", "charges",
        "cursed", "identified", "plot", "stolen",
        nullptr};
    for (const char** f = fields; *f; ++f) {
        EXPECT_EQ(read_int(rt, ref_l, def, *f), read_int(rt, ref_i, def, *f))
            << "ItemStats." << *f;
    }
    // Both paths import these from the same GFF data.
    EXPECT_EQ(read_int(rt, ref_l, def, "cost"), read_int(rt, ref_i, def, "cost"));
    EXPECT_EQ(read_int(rt, ref_l, def, "cost_additional"),
        read_int(rt, ref_i, def, "cost_additional"));
}

static void check_creature_descriptor_parity(
    nw::smalls::Runtime& rt,
    nw::Creature* cre_legacy, nw::Creature* cre_import)
{
    auto tid = rt.type_id("core.creature.CreatureDescriptor", false);
    ASSERT_NE(tid, nw::smalls::invalid_type_id);
    auto def = sdef(rt, tid);
    ASSERT_NE(def, nullptr);

    auto ref_l = rt.get_or_create_propset_ref(tid, cre_legacy->handle());
    auto ref_i = rt.get_or_create_propset_ref(tid, cre_import->handle());
    ASSERT_NE(ref_l.type_id, nw::smalls::invalid_type_id);
    ASSERT_NE(ref_i.type_id, nw::smalls::invalid_type_id);

    const char* resref_fields[] = {
        "on_attacked", "on_blocked", "on_conversation", "on_damaged",
        "on_death", "on_disturbed", "on_endround", "on_heartbeat",
        "on_perceived", "on_rested", "on_spawn", "on_spell_cast_at",
        "on_user_defined", "conversation",
        nullptr};
    for (const char** f = resref_fields; *f; ++f) {
        EXPECT_EQ(read_resref(rt, ref_l, def, *f), read_resref(rt, ref_i, def, *f))
            << "CreatureDescriptor." << *f;
    }

    const char* text_fields[] = {
        "description", "name_first", "name_last",
        nullptr};
    for (const char** f = text_fields; *f; ++f) {
        EXPECT_EQ(nwk::strings().to_locstring(read_text_ref(rt, ref_l, def, *f)),
            nwk::strings().to_locstring(read_text_ref(rt, ref_i, def, *f)))
            << "CreatureDescriptor." << *f;
    }

    EXPECT_EQ(read_string(rt, ref_l, def, "deity"),
        read_string(rt, ref_i, def, "deity"));
    EXPECT_EQ(read_string(rt, ref_l, def, "subrace"),
        read_string(rt, ref_i, def, "subrace"));

    const char* int_fields[] = {
        "soundset", "decay_time",
        nullptr};
    for (const char** f = int_fields; *f; ++f) {
        EXPECT_EQ(read_int(rt, ref_l, def, *f), read_int(rt, ref_i, def, *f))
            << "CreatureDescriptor." << *f;
    }
}

static void check_creature_stats_parity(
    nw::smalls::Runtime& rt,
    nw::Creature* cre_legacy, nw::Creature* cre_import)
{
    auto tid = rt.type_id("core.creature.CreatureStats", false);
    ASSERT_NE(tid, nw::smalls::invalid_type_id);
    auto def = sdef(rt, tid);
    ASSERT_NE(def, nullptr);

    auto ref_l = rt.get_or_create_propset_ref(tid, cre_legacy->handle());
    auto ref_i = rt.get_or_create_propset_ref(tid, cre_import->handle());
    ASSERT_NE(ref_l.type_id, nw::smalls::invalid_type_id);
    ASSERT_NE(ref_i.type_id, nw::smalls::invalid_type_id);

    const char* scalar_fields[] = {
        "race", "gender", "good_evil", "lawful_chaotic",
        "ac_natural_bonus", "cr_adjust", "perception_range",
        "disarmable", "immortal", "interruptable", "lootable",
        "pc", "plot", "chunk_death", "bodybag",
        "save_fort", "save_reflex", "save_will",
        nullptr};
    for (const char** f = scalar_fields; *f; ++f) {
        EXPECT_EQ(read_int(rt, ref_l, def, *f), read_int(rt, ref_i, def, *f))
            << "CreatureStats." << *f;
    }
    EXPECT_NEAR(read_float_f(rt, ref_l, def, "cr"),
        read_float_f(rt, ref_i, def, "cr"), 0.001f);
    for (int i = 0; i < 6; ++i) {
        EXPECT_EQ(read_elem(rt, ref_l, def, "abilities", i),
            read_elem(rt, ref_i, def, "abilities", i))
            << "abilities[" << i << "]";
    }
    const char* array_fields[] = {"skills", "feats", nullptr};
    for (const char** f = array_fields; *f; ++f) {
        size_t legacy_size = read_array_size(rt, ref_l, def, *f);
        size_t import_size = read_array_size(rt, ref_i, def, *f);
        ASSERT_EQ(legacy_size, import_size) << "CreatureStats." << *f;
        for (size_t i = 0; i < legacy_size; ++i) {
            EXPECT_EQ(read_elem(rt, ref_l, def, *f, i),
                read_elem(rt, ref_i, def, *f, i))
                << "CreatureStats." << *f << "[" << i << "]";
        }
    }
}

static void check_creature_health_parity(
    nw::smalls::Runtime& rt,
    nw::Creature* cre_legacy, nw::Creature* cre_import)
{
    auto tid = rt.type_id("core.creature.CreatureHealth", false);
    if (tid == nw::smalls::invalid_type_id) { return; }
    auto def = sdef(rt, tid);
    if (!def) { return; }

    auto ref_l = rt.get_or_create_propset_ref(tid, cre_legacy->handle());
    auto ref_i = rt.get_or_create_propset_ref(tid, cre_import->handle());

    const char* fields[] = {"hp", "hp_base_for_max", "hp_current", "hp_max", "faction_id", nullptr};
    for (const char** f = fields; *f; ++f) {
        EXPECT_EQ(read_int(rt, ref_l, def, *f), read_int(rt, ref_i, def, *f))
            << "CreatureHealth." << *f;
    }
}

static void check_creature_levels_parity(
    nw::smalls::Runtime& rt,
    nw::Creature* cre_legacy, nw::Creature* cre_import)
{
    auto tid = rt.type_id("core.creature.CreatureLevels", false);
    if (tid == nw::smalls::invalid_type_id) { return; }
    auto def = sdef(rt, tid);
    if (!def) { return; }

    auto ref_l = rt.get_or_create_propset_ref(tid, cre_legacy->handle());
    auto ref_i = rt.get_or_create_propset_ref(tid, cre_import->handle());

    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(read_elem(rt, ref_l, def, "classes", i),
            read_elem(rt, ref_i, def, "classes", i))
            << "classes[" << i << "]";
        EXPECT_EQ(read_elem(rt, ref_l, def, "class_levels", i),
            read_elem(rt, ref_i, def, "class_levels", i))
            << "class_levels[" << i << "]";
    }
    EXPECT_EQ(read_int(rt, ref_l, def, "walkrate"),
        read_int(rt, ref_i, def, "walkrate"));
}

TEST_F(SmallsPropsetParity, ImporterMatchesDeserializedCreature_Chicken)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    const char* path = "test_data/user/development/nw_chicken.utc";
    nw::Gff gff(path);
    ASSERT_TRUE(gff.valid());

    auto& rt = nw::kernel::runtime();

    // Deserializer path: deserialize creature and import propsets from GFF.
    auto* cre_deserialized = make_creature(path);
    ASSERT_NE(cre_deserialized, nullptr);

    // Importer: deserialize C++ struct, then populate propsets from GFF.
    auto* cre_import = make_creature(path);
    ASSERT_NE(cre_import, nullptr);
    rt.init_object_propsets(cre_import->handle());
    importer_->import_creature(cre_import, gff.toplevel(),
        nw::SerializationProfile::blueprint);

    check_common_parity(rt, cre_deserialized, cre_import);
    check_creature_descriptor_parity(rt, cre_deserialized, cre_import);
    check_creature_stats_parity(rt, cre_deserialized, cre_import);
    check_creature_health_parity(rt, cre_deserialized, cre_import);
    check_creature_levels_parity(rt, cre_deserialized, cre_import);
}

TEST_F(SmallsPropsetParity, ImporterMatchesDeserializedCreature_Agent)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    const char* path = "test_data/user/development/pl_agent_001.utc";
    nw::Gff gff(path);
    ASSERT_TRUE(gff.valid());

    auto& rt = nw::kernel::runtime();

    auto* cre_deserialized = make_creature(path);
    ASSERT_NE(cre_deserialized, nullptr);

    auto* cre_import = make_creature(path);
    ASSERT_NE(cre_import, nullptr);
    rt.init_object_propsets(cre_import->handle());
    importer_->import_creature(cre_import, gff.toplevel(),
        nw::SerializationProfile::blueprint);

    check_common_parity(rt, cre_deserialized, cre_import);
    check_creature_descriptor_parity(rt, cre_deserialized, cre_import);
    check_creature_stats_parity(rt, cre_deserialized, cre_import);
    check_creature_health_parity(rt, cre_deserialized, cre_import);
    check_creature_levels_parity(rt, cre_deserialized, cre_import);
}

// ---------------------------------------------------------------------------
// Test 3: skip fields stay zero after import
// ---------------------------------------------------------------------------

TEST_F(SmallsPropsetParity, SkipFieldsStayZero)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    const char* path = "test_data/user/development/nw_chicken.utc";
    nw::Gff gff(path);
    ASSERT_TRUE(gff.valid());

    auto& rt = nw::kernel::runtime();
    auto* cre = make_creature(path);
    ASSERT_NE(cre, nullptr);
    rt.init_object_propsets(cre->handle());
    importer_->import_creature(cre, gff.toplevel(), nw::SerializationProfile::blueprint);

    // hp_temp should be zero (skip field in CreatureHealth)
    auto tid_h = rt.type_id("core.creature.CreatureHealth", false);
    if (tid_h != nw::smalls::invalid_type_id) {
        auto def_h = sdef(rt, tid_h);
        auto ref_h = rt.get_or_create_propset_ref(tid_h, cre->handle());
        EXPECT_EQ(read_int(rt, ref_h, def_h, "hp_temp"), 0);
    }

    // xp should be zero (skip field in CreatureLevels — not on Creature GFF)
    auto tid_l = rt.type_id("core.creature.CreatureLevels", false);
    if (tid_l != nw::smalls::invalid_type_id) {
        auto def_l = sdef(rt, tid_l);
        auto ref_l = rt.get_or_create_propset_ref(tid_l, cre->handle());
        EXPECT_EQ(read_int(rt, ref_l, def_l, "xp"), 0);
    }

    // CreatureCombat — all fields should stay at zero after import
    auto tid_c = rt.type_id("core.creature.CreatureCombat", false);
    if (tid_c != nw::smalls::invalid_type_id) {
        auto def_c = sdef(rt, tid_c);
        auto ref_c = rt.get_or_create_propset_ref(tid_c, cre->handle());
        const char* combat_fields[] = {
            "attack_current", "attacks_onhand", "attacks_offhand",
            "attacks_extra", "combat_mode",
            nullptr};
        for (const char** f = combat_fields; *f; ++f) {
            EXPECT_EQ(read_int(rt, ref_c, def_c, *f), 0)
                << "CreatureCombat." << *f;
        }
    }
}

// ---------------------------------------------------------------------------
// Test 4: GFF round-trip via PropsetGffExporter → PropsetGffImporter
// ---------------------------------------------------------------------------

TEST_F(SmallsPropsetParity, GffRoundTripCreatureStats)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    auto& rt = nw::kernel::runtime();
    const char* path = "test_data/user/development/pl_agent_001.utc";

    // Reference: deserialize path imports propsets directly from GFF.
    auto* cre_ref = make_creature(path);
    ASSERT_NE(cre_ref, nullptr);

    // Export propsets to GFF using PropsetGffExporter (in-memory, no disk I/O).
    nw::GffBuilder builder(nw::Creature::serial_id);
    exporter_->export_creature(cre_ref, builder.top, nw::SerializationProfile::blueprint);
    builder.build();
    nw::ResourceData rd;
    rd.bytes = builder.to_byte_array();
    nw::Gff exported_gff(std::move(rd));
    ASSERT_TRUE(exported_gff.valid());

    // Import from exporter output into a fresh creature.
    auto* cre_rt = make_creature(path);
    ASSERT_NE(cre_rt, nullptr);
    rt.init_object_propsets(cre_rt->handle());
    importer_->import_creature(cre_rt, exported_gff.toplevel(),
        nw::SerializationProfile::blueprint);

    // Propsets must survive the exporter → importer round-trip.
    check_common_parity(rt, cre_ref, cre_rt);
    check_creature_descriptor_parity(rt, cre_ref, cre_rt);
    check_creature_stats_parity(rt, cre_ref, cre_rt);
    check_creature_health_parity(rt, cre_ref, cre_rt);
    check_creature_levels_parity(rt, cre_ref, cre_rt);
}

// ---------------------------------------------------------------------------
// Test 5: item importer ≡ deserialize-initialized propsets
// ---------------------------------------------------------------------------

TEST_F(SmallsPropsetParity, ImporterMatchesDeserializedItemStats)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    const char* path = "test_data/user/development/cloth028.uti";
    nw::Gff gff(path);
    ASSERT_TRUE(gff.valid());

    auto& rt = nw::kernel::runtime();

    // Deserializer path: deserialize item and import propsets from GFF.
    auto* item_deserialized = make_item(path);
    ASSERT_NE(item_deserialized, nullptr);

    // Importer path: deserialize item, populate propsets from GFF.
    auto* item_import = make_item(path);
    ASSERT_NE(item_import, nullptr);
    rt.init_object_propsets(item_import->handle());
    importer_->import_item(item_import, gff.toplevel(), nw::SerializationProfile::blueprint);

    check_common_parity(rt, item_deserialized, item_import);
    check_item_descriptor_parity(rt, item_deserialized, item_import);
    check_item_stats_parity(rt, item_deserialized, item_import);
}

static void check_sound_state_parity(
    nw::smalls::Runtime& rt,
    nw::Sound* sound_legacy, nw::Sound* sound_import)
{
    auto tid = rt.type_id("core.sound.SoundState", false);
    ASSERT_NE(tid, nw::smalls::invalid_type_id);
    auto def = sdef(rt, tid);
    ASSERT_NE(def, nullptr);

    auto ref_l = rt.get_or_create_propset_ref(tid, sound_legacy->handle());
    auto ref_i = rt.get_or_create_propset_ref(tid, sound_import->handle());
    ASSERT_NE(ref_l.type_id, nw::smalls::invalid_type_id);
    ASSERT_NE(ref_i.type_id, nw::smalls::invalid_type_id);

    EXPECT_EQ(read_array_size(rt, ref_l, def, "sounds"),
        read_array_size(rt, ref_i, def, "sounds"));
    for (size_t i = 0; i < read_array_size(rt, ref_l, def, "sounds"); ++i) {
        EXPECT_EQ(read_resref_elem(rt, ref_l, def, "sounds", i),
            read_resref_elem(rt, ref_i, def, "sounds", i))
            << "sounds[" << i << "]";
    }

    const char* int_fields[] = {
        "generated_type", "hours", "interval", "interval_variation",
        "active", "continuous", "looping", "positional", "priority",
        "random", "random_position", "times", "volume", "volume_variation",
        nullptr};
    for (const char** f = int_fields; *f; ++f) {
        EXPECT_EQ(read_int(rt, ref_l, def, *f), read_int(rt, ref_i, def, *f))
            << "SoundState." << *f;
    }

    const char* float_fields[] = {
        "distance_min", "distance_max", "elevation",
        "pitch_variation", "random_x", "random_y",
        nullptr};
    for (const char** f = float_fields; *f; ++f) {
        EXPECT_NEAR(read_float_f(rt, ref_l, def, *f),
            read_float_f(rt, ref_i, def, *f), 0.001f)
            << "SoundState." << *f;
    }
}

static void check_waypoint_state_parity(
    nw::smalls::Runtime& rt,
    nw::Waypoint* waypoint_legacy, nw::Waypoint* waypoint_import)
{
    auto tid = rt.type_id("core.waypoint.WaypointState", false);
    ASSERT_NE(tid, nw::smalls::invalid_type_id);
    auto def = sdef(rt, tid);
    ASSERT_NE(def, nullptr);

    auto ref_l = rt.get_or_create_propset_ref(tid, waypoint_legacy->handle());
    auto ref_i = rt.get_or_create_propset_ref(tid, waypoint_import->handle());
    ASSERT_NE(ref_l.type_id, nw::smalls::invalid_type_id);
    ASSERT_NE(ref_i.type_id, nw::smalls::invalid_type_id);

    EXPECT_EQ(nwk::strings().to_locstring(read_text_ref(rt, ref_l, def, "description")),
        nwk::strings().to_locstring(read_text_ref(rt, ref_i, def, "description")));
    EXPECT_EQ(read_string(rt, ref_l, def, "linked_to"),
        read_string(rt, ref_i, def, "linked_to"));
    EXPECT_EQ(nwk::strings().to_locstring(read_text_ref(rt, ref_l, def, "map_note")),
        nwk::strings().to_locstring(read_text_ref(rt, ref_i, def, "map_note")));

    const char* int_fields[] = {
        "appearance", "has_map_note", "map_note_enabled",
        nullptr};
    for (const char** f = int_fields; *f; ++f) {
        EXPECT_EQ(read_int(rt, ref_l, def, *f), read_int(rt, ref_i, def, *f))
            << "WaypointState." << *f;
    }
}

static void check_store_state_parity(
    nw::smalls::Runtime& rt,
    nw::Store* store_legacy, nw::Store* store_import)
{
    auto tid = rt.type_id("core.store.StoreState", false);
    ASSERT_NE(tid, nw::smalls::invalid_type_id);
    auto def = sdef(rt, tid);
    ASSERT_NE(def, nullptr);

    auto ref_l = rt.get_or_create_propset_ref(tid, store_legacy->handle());
    auto ref_i = rt.get_or_create_propset_ref(tid, store_import->handle());
    ASSERT_NE(ref_l.type_id, nw::smalls::invalid_type_id);
    ASSERT_NE(ref_i.type_id, nw::smalls::invalid_type_id);

    const char* resref_fields[] = {
        "on_closed", "on_opened",
        nullptr};
    for (const char** f = resref_fields; *f; ++f) {
        EXPECT_EQ(read_resref(rt, ref_l, def, *f), read_resref(rt, ref_i, def, *f))
            << "StoreState." << *f;
    }

    const char* int_fields[] = {
        "blackmarket_markdown", "identify_price", "markdown",
        "markup", "max_price", "gold", "blackmarket",
        nullptr};
    for (const char** f = int_fields; *f; ++f) {
        EXPECT_EQ(read_int(rt, ref_l, def, *f), read_int(rt, ref_i, def, *f))
            << "StoreState." << *f;
    }
}

static void check_trigger_state_parity(
    nw::smalls::Runtime& rt,
    nw::Trigger* trigger_legacy, nw::Trigger* trigger_import)
{
    auto tid = rt.type_id("core.trigger.TriggerState", false);
    ASSERT_NE(tid, nw::smalls::invalid_type_id);
    auto def = sdef(rt, tid);
    ASSERT_NE(def, nullptr);

    auto ref_l = rt.get_or_create_propset_ref(tid, trigger_legacy->handle());
    auto ref_i = rt.get_or_create_propset_ref(tid, trigger_import->handle());
    ASSERT_NE(ref_l.type_id, nw::smalls::invalid_type_id);
    ASSERT_NE(ref_i.type_id, nw::smalls::invalid_type_id);

    const char* resref_fields[] = {
        "on_click", "on_disarm", "on_enter", "on_exit",
        "on_heartbeat", "on_trap_triggered", "on_user_defined",
        nullptr};
    for (const char** f = resref_fields; *f; ++f) {
        EXPECT_EQ(read_resref(rt, ref_l, def, *f), read_resref(rt, ref_i, def, *f))
            << "TriggerState." << *f;
    }

    EXPECT_EQ(read_string(rt, ref_l, def, "linked_to"),
        read_string(rt, ref_i, def, "linked_to"));

    const char* int_fields[] = {
        "trap_type", "is_trapped", "trap_detectable", "trap_detect_dc",
        "trap_disarmable", "trap_disarm_dc", "trap_one_shot",
        "faction", "trigger_type", "loadscreen", "portrait", "cursor",
        "linked_to_flags",
        nullptr};
    for (const char** f = int_fields; *f; ++f) {
        EXPECT_EQ(read_int(rt, ref_l, def, *f), read_int(rt, ref_i, def, *f))
            << "TriggerState." << *f;
    }

    EXPECT_NEAR(read_float_f(rt, ref_l, def, "highlight_height"),
        read_float_f(rt, ref_i, def, "highlight_height"), 0.001f);
}

static void check_door_state_parity(
    nw::smalls::Runtime& rt,
    nw::Door* door_legacy, nw::Door* door_import)
{
    auto tid = rt.type_id("core.door.DoorState", false);
    ASSERT_NE(tid, nw::smalls::invalid_type_id);
    auto def = sdef(rt, tid);
    ASSERT_NE(def, nullptr);

    auto ref_l = rt.get_or_create_propset_ref(tid, door_legacy->handle());
    auto ref_i = rt.get_or_create_propset_ref(tid, door_import->handle());
    ASSERT_NE(ref_l.type_id, nw::smalls::invalid_type_id);
    ASSERT_NE(ref_i.type_id, nw::smalls::invalid_type_id);

    const char* resref_fields[] = {
        "on_click", "on_closed", "on_damaged", "on_death",
        "on_disarm", "on_heartbeat", "on_lock", "on_melee_attacked",
        "on_open", "on_open_failure", "on_spell_cast_at",
        "on_trap_triggered", "on_unlock", "on_user_defined",
        "conversation",
        nullptr};
    for (const char** f = resref_fields; *f; ++f) {
        EXPECT_EQ(read_resref(rt, ref_l, def, *f), read_resref(rt, ref_i, def, *f))
            << "DoorState." << *f;
    }

    EXPECT_EQ(read_string(rt, ref_l, def, "key_name"),
        read_string(rt, ref_i, def, "key_name"));
    EXPECT_EQ(read_string(rt, ref_l, def, "linked_to"),
        read_string(rt, ref_i, def, "linked_to"));
    EXPECT_EQ(nwk::strings().to_locstring(read_text_ref(rt, ref_l, def, "description")),
        nwk::strings().to_locstring(read_text_ref(rt, ref_i, def, "description")));

    const char* int_fields[] = {
        "key_required", "lockable", "locked", "lock_dc", "unlock_dc",
        "remove_key", "trap_type", "is_trapped", "trap_detectable",
        "trap_detect_dc", "trap_disarmable", "trap_disarm_dc",
        "trap_one_shot", "hp", "hp_current", "save_fort", "save_reflex",
        "save_will", "appearance", "faction", "generic_type",
        "loadscreen", "portrait_id", "hardness", "bash_dc", "open_state",
        "plot", "interruptable", "linked_to_flags",
        nullptr};
    for (const char** f = int_fields; *f; ++f) {
        EXPECT_EQ(read_int(rt, ref_l, def, *f), read_int(rt, ref_i, def, *f))
            << "DoorState." << *f;
    }
}

static void check_placeable_state_parity(
    nw::smalls::Runtime& rt,
    nw::Placeable* placeable_legacy, nw::Placeable* placeable_import)
{
    auto tid = rt.type_id("core.placeable.PlaceableState", false);
    ASSERT_NE(tid, nw::smalls::invalid_type_id);
    auto def = sdef(rt, tid);
    ASSERT_NE(def, nullptr);

    auto ref_l = rt.get_or_create_propset_ref(tid, placeable_legacy->handle());
    auto ref_i = rt.get_or_create_propset_ref(tid, placeable_import->handle());
    ASSERT_NE(ref_l.type_id, nw::smalls::invalid_type_id);
    ASSERT_NE(ref_i.type_id, nw::smalls::invalid_type_id);

    const char* resref_fields[] = {
        "on_click", "on_closed", "on_damaged", "on_death",
        "on_disarm", "on_heartbeat", "on_inventory_disturbed",
        "on_lock", "on_melee_attacked", "on_open", "on_spell_cast_at",
        "on_trap_triggered", "on_unlock", "on_used", "on_user_defined",
        "conversation",
        nullptr};
    for (const char** f = resref_fields; *f; ++f) {
        EXPECT_EQ(read_resref(rt, ref_l, def, *f), read_resref(rt, ref_i, def, *f))
            << "PlaceableState." << *f;
    }

    EXPECT_EQ(read_string(rt, ref_l, def, "key_name"),
        read_string(rt, ref_i, def, "key_name"));
    EXPECT_EQ(nwk::strings().to_locstring(read_text_ref(rt, ref_l, def, "description")),
        nwk::strings().to_locstring(read_text_ref(rt, ref_i, def, "description")));

    const char* int_fields[] = {
        "key_required", "lockable", "locked", "lock_dc", "unlock_dc",
        "remove_key", "trap_type", "is_trapped", "trap_detectable",
        "trap_detect_dc", "trap_disarmable", "trap_disarm_dc",
        "trap_one_shot", "hp", "hp_current", "save_fort", "save_reflex",
        "save_will", "appearance", "faction", "portrait_id", "legacy_type",
        "animation_state", "bodybag", "hardness", "interruptable",
        "plot", "useable", "has_inventory", "static", "light_color",
        nullptr};
    for (const char** f = int_fields; *f; ++f) {
        EXPECT_EQ(read_int(rt, ref_l, def, *f), read_int(rt, ref_i, def, *f))
            << "PlaceableState." << *f;
    }
}

static void check_encounter_state_parity(
    nw::smalls::Runtime& rt,
    nw::Encounter* encounter_legacy, nw::Encounter* encounter_import)
{
    auto tid = rt.type_id("core.encounter.EncounterState", false);
    ASSERT_NE(tid, nw::smalls::invalid_type_id);
    auto def = sdef(rt, tid);
    ASSERT_NE(def, nullptr);

    auto ref_l = rt.get_or_create_propset_ref(tid, encounter_legacy->handle());
    auto ref_i = rt.get_or_create_propset_ref(tid, encounter_import->handle());
    ASSERT_NE(ref_l.type_id, nw::smalls::invalid_type_id);
    ASSERT_NE(ref_i.type_id, nw::smalls::invalid_type_id);

    const char* resref_fields[] = {
        "on_entered", "on_exhausted", "on_exit", "on_heartbeat", "on_user_defined",
        nullptr};
    for (const char** f = resref_fields; *f; ++f) {
        EXPECT_EQ(read_resref(rt, ref_l, def, *f), read_resref(rt, ref_i, def, *f))
            << "EncounterState." << *f;
    }

    const char* int_fields[] = {
        "creatures_max", "creatures_recommended", "difficulty",
        "difficulty_index", "faction", "reset_time", "respawns",
        "spawn_option", "active", "player_only", "reset",
        nullptr};
    for (const char** f = int_fields; *f; ++f) {
        EXPECT_EQ(read_int(rt, ref_l, def, *f), read_int(rt, ref_i, def, *f))
            << "EncounterState." << *f;
    }

    nw::smalls::IArray* creatures_l = read_array(rt, ref_l, def, "creatures");
    nw::smalls::IArray* creatures_i = read_array(rt, ref_i, def, "creatures");
    ASSERT_NE(creatures_l, nullptr);
    ASSERT_NE(creatures_i, nullptr);
    ASSERT_EQ(creatures_l->size(), creatures_i->size());

    auto spawn_tid = rt.type_id("core.encounter.EncounterSpawn", false);
    ASSERT_NE(spawn_tid, nw::smalls::invalid_type_id);
    const auto* spawn_def = sdef(rt, spawn_tid);
    ASSERT_NE(spawn_def, nullptr);

    for (size_t i = 0; i < creatures_l->size(); ++i) {
        nw::smalls::Value spawn_l;
        nw::smalls::Value spawn_i;
        ASSERT_TRUE(creatures_l->get_value(i, spawn_l, rt));
        ASSERT_TRUE(creatures_i->get_value(i, spawn_i, rt));
        EXPECT_EQ(read_int(rt, spawn_l, spawn_def, "appearance"),
            read_int(rt, spawn_i, spawn_def, "appearance"))
            << "EncounterState.creatures[" << i << "].appearance";
        EXPECT_NEAR(read_float_f(rt, spawn_l, spawn_def, "cr"),
            read_float_f(rt, spawn_i, spawn_def, "cr"), 0.001f)
            << "EncounterState.creatures[" << i << "].cr";
        EXPECT_EQ(read_resref(rt, spawn_l, spawn_def, "resref"),
            read_resref(rt, spawn_i, spawn_def, "resref"))
            << "EncounterState.creatures[" << i << "].resref";
        EXPECT_EQ(read_int(rt, spawn_l, spawn_def, "single_spawn"),
            read_int(rt, spawn_i, spawn_def, "single_spawn"))
            << "EncounterState.creatures[" << i << "].single_spawn";
    }
}

TEST_F(SmallsPropsetParity, ImporterMatchesDeserializedSoundState)
{
    const char* path = "test_data/user/development/blue_bell.uts";
    nw::Gff gff(path);
    ASSERT_TRUE(gff.valid());

    auto& rt = nw::kernel::runtime();
    ASSERT_NE(rt.load_module("core.sound"), nullptr);

    auto* sound_legacy = make_sound(path);
    ASSERT_NE(sound_legacy, nullptr);
    rt.init_object_propsets(sound_legacy->handle());

    auto* sound_import = make_sound(path);
    ASSERT_NE(sound_import, nullptr);
    rt.init_object_propsets(sound_import->handle());
    importer_->import_sound(sound_import, gff.toplevel(), nw::SerializationProfile::blueprint);

    check_common_parity(rt, sound_legacy, sound_import);
    check_sound_state_parity(rt, sound_legacy, sound_import);
}

TEST_F(SmallsPropsetParity, ImporterMatchesDeserializedWaypointState)
{
    const char* path = "test_data/user/development/wp_behexit001.utw";
    nw::Gff gff(path);
    ASSERT_TRUE(gff.valid());

    auto& rt = nw::kernel::runtime();
    ASSERT_NE(rt.load_module("core.waypoint"), nullptr);

    auto* waypoint_legacy = make_waypoint(path);
    ASSERT_NE(waypoint_legacy, nullptr);
    rt.init_object_propsets(waypoint_legacy->handle());

    auto* waypoint_import = make_waypoint(path);
    ASSERT_NE(waypoint_import, nullptr);
    rt.init_object_propsets(waypoint_import->handle());
    importer_->import_waypoint(waypoint_import, gff.toplevel(), nw::SerializationProfile::blueprint);

    check_common_parity(rt, waypoint_legacy, waypoint_import);
    check_waypoint_state_parity(rt, waypoint_legacy, waypoint_import);
}

TEST_F(SmallsPropsetParity, ImporterMatchesDeserializedStoreState)
{
    const char* path = "test_data/user/development/storethief002.utm";
    nw::Gff gff(path);
    ASSERT_TRUE(gff.valid());

    auto& rt = nw::kernel::runtime();
    ASSERT_NE(rt.load_module("core.store"), nullptr);

    auto* store_legacy = make_store(path);
    ASSERT_NE(store_legacy, nullptr);
    rt.init_object_propsets(store_legacy->handle());

    auto* store_import = make_store(path);
    ASSERT_NE(store_import, nullptr);
    rt.init_object_propsets(store_import->handle());
    importer_->import_store(store_import, gff.toplevel(), nw::SerializationProfile::blueprint);

    check_common_parity(rt, store_legacy, store_import);
    check_store_state_parity(rt, store_legacy, store_import);
}

TEST_F(SmallsPropsetParity, ImporterMatchesDeserializedTriggerState)
{
    const char* path = "test_data/user/development/pl_spray_sewage.utt";
    nw::Gff gff(path);
    ASSERT_TRUE(gff.valid());

    auto& rt = nw::kernel::runtime();
    ASSERT_NE(rt.load_module("core.trigger"), nullptr);

    auto* trigger_legacy = make_trigger(path);
    ASSERT_NE(trigger_legacy, nullptr);
    rt.init_object_propsets(trigger_legacy->handle());

    auto* trigger_import = make_trigger(path);
    ASSERT_NE(trigger_import, nullptr);
    rt.init_object_propsets(trigger_import->handle());
    importer_->import_trigger(trigger_import, gff.toplevel(), nw::SerializationProfile::blueprint);

    check_common_parity(rt, trigger_legacy, trigger_import);
    check_trigger_state_parity(rt, trigger_legacy, trigger_import);
}

TEST_F(SmallsPropsetParity, ImporterMatchesDeserializedDoorState)
{
    const char* path = "test_data/user/development/door_ttr_002.utd";
    nw::Gff gff(path);
    ASSERT_TRUE(gff.valid());

    auto& rt = nw::kernel::runtime();
    ASSERT_NE(rt.load_module("core.door"), nullptr);

    auto* door_legacy = make_door(path);
    ASSERT_NE(door_legacy, nullptr);
    rt.init_object_propsets(door_legacy->handle());

    auto* door_import = make_door(path);
    ASSERT_NE(door_import, nullptr);
    rt.init_object_propsets(door_import->handle());
    importer_->import_door(door_import, gff.toplevel(), nw::SerializationProfile::blueprint);

    check_common_parity(rt, door_legacy, door_import);
    check_door_state_parity(rt, door_legacy, door_import);
}

TEST_F(SmallsPropsetParity, ImporterMatchesDeserializedPlaceableState)
{
    const char* path = "test_data/user/development/arrowcorpse001.utp";
    nw::Gff gff(path);
    ASSERT_TRUE(gff.valid());

    auto& rt = nw::kernel::runtime();
    ASSERT_NE(rt.load_module("core.placeable"), nullptr);

    auto* placeable_legacy = make_placeable(path);
    ASSERT_NE(placeable_legacy, nullptr);
    rt.init_object_propsets(placeable_legacy->handle());

    auto* placeable_import = make_placeable(path);
    ASSERT_NE(placeable_import, nullptr);
    rt.init_object_propsets(placeable_import->handle());
    importer_->import_placeable(placeable_import, gff.toplevel(), nw::SerializationProfile::blueprint);

    check_common_parity(rt, placeable_legacy, placeable_import);
    check_placeable_state_parity(rt, placeable_legacy, placeable_import);
}

TEST_F(SmallsPropsetParity, ImporterMatchesDeserializedEncounterState)
{
    const char* path = "test_data/user/development/boundelementallo.ute";
    nw::Gff gff(path);
    ASSERT_TRUE(gff.valid());

    auto& rt = nw::kernel::runtime();
    ASSERT_NE(rt.load_module("core.encounter"), nullptr);

    auto* encounter_legacy = make_encounter(path);
    ASSERT_NE(encounter_legacy, nullptr);
    rt.init_object_propsets(encounter_legacy->handle());

    auto* encounter_import = make_encounter(path);
    ASSERT_NE(encounter_import, nullptr);
    rt.init_object_propsets(encounter_import->handle());
    importer_->import_encounter(encounter_import, gff.toplevel(), nw::SerializationProfile::blueprint);

    check_common_parity(rt, encounter_legacy, encounter_import);
    check_encounter_state_parity(rt, encounter_legacy, encounter_import);
}

TEST_F(SmallsPropsetParity, GffRoundTripItemPropsets)
{
    auto& rt = nw::kernel::runtime();
    const char* path = "test_data/user/development/cloth028.uti";
    ASSERT_NE(rt.load_module("core.item"), nullptr);

    auto* item_ref = make_item(path);
    ASSERT_NE(item_ref, nullptr);

    nw::GffBuilder builder(nw::Item::serial_id);
    exporter_->export_item(item_ref, builder.top, nw::SerializationProfile::blueprint);
    builder.build();
    nw::ResourceData rd;
    rd.bytes = builder.to_byte_array();
    nw::Gff exported_gff(std::move(rd));
    ASSERT_TRUE(exported_gff.valid());

    auto* item_rt = make_item(path);
    ASSERT_NE(item_rt, nullptr);
    rt.init_object_propsets(item_rt->handle());
    importer_->import_item(item_rt, exported_gff.toplevel(),
        nw::SerializationProfile::blueprint);

    check_common_parity(rt, item_ref, item_rt);
    check_item_descriptor_parity(rt, item_ref, item_rt);
    check_item_stats_parity(rt, item_ref, item_rt);
}

TEST_F(SmallsPropsetParity, GffRoundTripSoundState)
{
    auto& rt = nw::kernel::runtime();
    const char* path = "test_data/user/development/blue_bell.uts";
    ASSERT_NE(rt.load_module("core.sound"), nullptr);

    auto* sound_ref = make_sound(path);
    ASSERT_NE(sound_ref, nullptr);
    rt.init_object_propsets(sound_ref->handle());

    nw::GffBuilder builder(nw::Sound::serial_id);
    exporter_->export_sound(sound_ref, builder.top, nw::SerializationProfile::blueprint);
    builder.build();
    nw::ResourceData rd;
    rd.bytes = builder.to_byte_array();
    nw::Gff exported_gff(std::move(rd));
    ASSERT_TRUE(exported_gff.valid());

    auto* sound_rt = make_sound(path);
    ASSERT_NE(sound_rt, nullptr);
    rt.init_object_propsets(sound_rt->handle());
    importer_->import_sound(sound_rt, exported_gff.toplevel(),
        nw::SerializationProfile::blueprint);

    check_common_parity(rt, sound_ref, sound_rt);
    check_sound_state_parity(rt, sound_ref, sound_rt);
}

TEST_F(SmallsPropsetParity, GffRoundTripWaypointState)
{
    auto& rt = nw::kernel::runtime();
    const char* path = "test_data/user/development/wp_behexit001.utw";
    ASSERT_NE(rt.load_module("core.waypoint"), nullptr);

    auto* waypoint_ref = make_waypoint(path);
    ASSERT_NE(waypoint_ref, nullptr);
    rt.init_object_propsets(waypoint_ref->handle());

    nw::GffBuilder builder(nw::Waypoint::serial_id);
    exporter_->export_waypoint(waypoint_ref, builder.top, nw::SerializationProfile::blueprint);
    builder.build();
    nw::ResourceData rd;
    rd.bytes = builder.to_byte_array();
    nw::Gff exported_gff(std::move(rd));
    ASSERT_TRUE(exported_gff.valid());

    auto* waypoint_rt = make_waypoint(path);
    ASSERT_NE(waypoint_rt, nullptr);
    rt.init_object_propsets(waypoint_rt->handle());
    importer_->import_waypoint(waypoint_rt, exported_gff.toplevel(),
        nw::SerializationProfile::blueprint);

    check_common_parity(rt, waypoint_ref, waypoint_rt);
    check_waypoint_state_parity(rt, waypoint_ref, waypoint_rt);
}

TEST_F(SmallsPropsetParity, GffRoundTripStoreState)
{
    auto& rt = nw::kernel::runtime();
    const char* path = "test_data/user/development/storethief002.utm";
    ASSERT_NE(rt.load_module("core.store"), nullptr);

    auto* store_ref = make_store(path);
    ASSERT_NE(store_ref, nullptr);
    rt.init_object_propsets(store_ref->handle());

    nw::GffBuilder builder(nw::Store::serial_id);
    exporter_->export_store(store_ref, builder.top, nw::SerializationProfile::blueprint);
    builder.build();
    nw::ResourceData rd;
    rd.bytes = builder.to_byte_array();
    nw::Gff exported_gff(std::move(rd));
    ASSERT_TRUE(exported_gff.valid());

    auto* store_rt = make_store(path);
    ASSERT_NE(store_rt, nullptr);
    rt.init_object_propsets(store_rt->handle());
    importer_->import_store(store_rt, exported_gff.toplevel(),
        nw::SerializationProfile::blueprint);

    check_common_parity(rt, store_ref, store_rt);
    check_store_state_parity(rt, store_ref, store_rt);
}

TEST_F(SmallsPropsetParity, GffRoundTripTriggerState)
{
    auto& rt = nw::kernel::runtime();
    const char* path = "test_data/user/development/pl_spray_sewage.utt";
    ASSERT_NE(rt.load_module("core.trigger"), nullptr);

    auto* trigger_ref = make_trigger(path);
    ASSERT_NE(trigger_ref, nullptr);
    rt.init_object_propsets(trigger_ref->handle());

    nw::GffBuilder builder(nw::Trigger::serial_id);
    exporter_->export_trigger(trigger_ref, builder.top, nw::SerializationProfile::blueprint);
    builder.build();
    nw::ResourceData rd;
    rd.bytes = builder.to_byte_array();
    nw::Gff exported_gff(std::move(rd));
    ASSERT_TRUE(exported_gff.valid());

    auto* trigger_rt = make_trigger(path);
    ASSERT_NE(trigger_rt, nullptr);
    rt.init_object_propsets(trigger_rt->handle());
    importer_->import_trigger(trigger_rt, exported_gff.toplevel(),
        nw::SerializationProfile::blueprint);

    check_common_parity(rt, trigger_ref, trigger_rt);
    check_trigger_state_parity(rt, trigger_ref, trigger_rt);
}

TEST_F(SmallsPropsetParity, GffRoundTripDoorState)
{
    auto& rt = nw::kernel::runtime();
    const char* path = "test_data/user/development/door_ttr_002.utd";
    ASSERT_NE(rt.load_module("core.door"), nullptr);

    auto* door_ref = make_door(path);
    ASSERT_NE(door_ref, nullptr);
    rt.init_object_propsets(door_ref->handle());

    nw::GffBuilder builder(nw::Door::serial_id);
    exporter_->export_door(door_ref, builder.top, nw::SerializationProfile::blueprint);
    builder.build();
    nw::ResourceData rd;
    rd.bytes = builder.to_byte_array();
    nw::Gff exported_gff(std::move(rd));
    ASSERT_TRUE(exported_gff.valid());

    auto* door_rt = make_door(path);
    ASSERT_NE(door_rt, nullptr);
    rt.init_object_propsets(door_rt->handle());
    importer_->import_door(door_rt, exported_gff.toplevel(),
        nw::SerializationProfile::blueprint);

    check_common_parity(rt, door_ref, door_rt);
    check_door_state_parity(rt, door_ref, door_rt);
}

TEST_F(SmallsPropsetParity, GffRoundTripPlaceableState)
{
    auto& rt = nw::kernel::runtime();
    const char* path = "test_data/user/development/arrowcorpse001.utp";
    ASSERT_NE(rt.load_module("core.placeable"), nullptr);

    auto* placeable_ref = make_placeable(path);
    ASSERT_NE(placeable_ref, nullptr);
    rt.init_object_propsets(placeable_ref->handle());

    nw::GffBuilder builder(nw::Placeable::serial_id);
    exporter_->export_placeable(placeable_ref, builder.top, nw::SerializationProfile::blueprint);
    builder.build();
    nw::ResourceData rd;
    rd.bytes = builder.to_byte_array();
    nw::Gff exported_gff(std::move(rd));
    ASSERT_TRUE(exported_gff.valid());

    auto* placeable_rt = make_placeable(path);
    ASSERT_NE(placeable_rt, nullptr);
    rt.init_object_propsets(placeable_rt->handle());
    importer_->import_placeable(placeable_rt, exported_gff.toplevel(),
        nw::SerializationProfile::blueprint);

    check_common_parity(rt, placeable_ref, placeable_rt);
    check_placeable_state_parity(rt, placeable_ref, placeable_rt);
}

TEST_F(SmallsPropsetParity, GffRoundTripEncounterState)
{
    auto& rt = nw::kernel::runtime();
    const char* path = "test_data/user/development/boundelementallo.ute";
    ASSERT_NE(rt.load_module("core.encounter"), nullptr);

    auto* encounter_ref = make_encounter(path);
    ASSERT_NE(encounter_ref, nullptr);
    rt.init_object_propsets(encounter_ref->handle());

    nw::GffBuilder builder(nw::Encounter::serial_id);
    exporter_->export_encounter(encounter_ref, builder.top, nw::SerializationProfile::blueprint);
    builder.build();
    nw::ResourceData rd;
    rd.bytes = builder.to_byte_array();
    nw::Gff exported_gff(std::move(rd));
    ASSERT_TRUE(exported_gff.valid());

    auto* encounter_rt = make_encounter(path);
    ASSERT_NE(encounter_rt, nullptr);
    rt.init_object_propsets(encounter_rt->handle());
    importer_->import_encounter(encounter_rt, exported_gff.toplevel(),
        nw::SerializationProfile::blueprint);

    check_common_parity(rt, encounter_ref, encounter_rt);
    check_encounter_state_parity(rt, encounter_ref, encounter_rt);
}

TEST_F(SmallsPropsetParity, ObjectBaseIdentityUsesBaseFields)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    auto* item = make_item("test_data/user/development/cloth028.uti");
    ASSERT_NE(item, nullptr);

    nw::Resref override_resref{"propset_probe"};
    item->resref = override_resref;
    EXPECT_EQ(item->resref, override_resref);
}
