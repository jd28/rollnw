/// Phase 4 parity gate: must be green before any C++ field hollowing.
///
/// Strategy: for each test, create TWO fresh creatures/items via make+deserialize
/// (no instantiate callbacks, no hp_max recomputation).  Then populate one via
/// the legacy populate_*_propsets() path and the other via the importer.  Both
/// start from the same deserialized C++ state, so they should produce identical
/// propset field values for all mapped fields.
///
/// Tests:
/// 1. Importer vs populate parity (creature and item).
/// 2. GFF round-trip parity: export → reload produces value-equivalent propsets.
/// 3. Skip fields stay zero after import.
/// 4. Registry compile-time sanity (no duplicate field names).

#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Door.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Placeable.hpp>
#include <nw/profiles/nwn1/object_json.hpp>
#include <nw/profiles/nwn1/propset_gff_exporter.hpp>
#include <nw/profiles/nwn1/propset_gff_importer.hpp>
#include <nw/profiles/nwn1/propset_gff_policy.hpp>
#include <nw/profiles/nwn1/propset_populate.hpp>
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
    const nw::smalls::Type* type = rt.get_type(tid);
    if (!type || type->type_kind != nw::smalls::TK_struct) { return nullptr; }
    auto sid = type->type_params[0].as<nw::smalls::StructID>();
    return rt.type_table_.get(sid);
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
    nw::Creature* make_creature(const char* path) {
        auto* cre = nwk::objects().make<nw::Creature>();
        if (!cre) { return nullptr; }
        nw::Gff gff(path);
        if (!gff.valid()) { return nullptr; }
        nw::deserialize(cre, gff.toplevel(), nw::SerializationProfile::blueprint);
        return cre;
    }

    nw::Item* make_item(const char* path) {
        auto* item = nwk::objects().make<nw::Item>();
        if (!item) { return nullptr; }
        nw::Gff gff(path);
        if (!gff.valid()) { return nullptr; }
        nw::deserialize(item, gff.toplevel(), nw::SerializationProfile::blueprint);
        return item;
    }

    std::unique_ptr<nwn1::PropsetGffPolicyRegistry> registry_;
    std::unique_ptr<nwn1::PropsetGffImporter>        importer_;
    std::unique_ptr<nwn1::PropsetGffExporter>         exporter_;
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
    EXPECT_NE(registry_->find("core.creature.CreatureStats"),   nullptr);
    EXPECT_NE(registry_->find("core.creature.CreatureHealth"),  nullptr);
    EXPECT_NE(registry_->find("core.creature.CreatureLevels"),  nullptr);
    EXPECT_NE(registry_->find("core.creature.CreatureCombat"),  nullptr);
    EXPECT_NE(registry_->find("core.item.ItemStats"),           nullptr);
    EXPECT_NE(registry_->find("core.door.DoorState"),           nullptr);
    EXPECT_NE(registry_->find("core.placeable.PlaceableState"), nullptr);
}

// ---------------------------------------------------------------------------
// Test 2: importer ≡ populate_creature_propsets (no instantiation)
// ---------------------------------------------------------------------------

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
        nullptr
    };
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

    const char* fields[] = { "hp", "hp_current", "hp_max", "faction_id", nullptr };
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

TEST_F(SmallsPropsetParity, ImporterMatchesPopulateCreature_Chicken)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    const char* path = "test_data/user/development/nw_chicken.utc";
    nw::Gff gff(path);
    ASSERT_TRUE(gff.valid());

    auto& rt = nw::kernel::runtime();

    // Legacy: deserialize C++ struct, then populate propsets from C++ struct.
    auto* cre_legacy = make_creature(path);
    ASSERT_NE(cre_legacy, nullptr);
    rt.init_object_propsets(cre_legacy->handle());
    nwn1::populate_creature_propsets(&rt, cre_legacy);

    // Importer: deserialize C++ struct, then populate propsets from GFF.
    auto* cre_import = make_creature(path);
    ASSERT_NE(cre_import, nullptr);
    rt.init_object_propsets(cre_import->handle());
    importer_->import_creature(cre_import, gff.toplevel(),
        nw::SerializationProfile::blueprint);

    check_creature_stats_parity(rt, cre_legacy, cre_import);
    check_creature_health_parity(rt, cre_legacy, cre_import);
    check_creature_levels_parity(rt, cre_legacy, cre_import);
}

TEST_F(SmallsPropsetParity, ImporterMatchesPopulateCreature_Agent)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    const char* path = "test_data/user/development/pl_agent_001.utc";
    nw::Gff gff(path);
    ASSERT_TRUE(gff.valid());

    auto& rt = nw::kernel::runtime();

    auto* cre_legacy = make_creature(path);
    ASSERT_NE(cre_legacy, nullptr);
    rt.init_object_propsets(cre_legacy->handle());
    nwn1::populate_creature_propsets(&rt, cre_legacy);

    auto* cre_import = make_creature(path);
    ASSERT_NE(cre_import, nullptr);
    rt.init_object_propsets(cre_import->handle());
    importer_->import_creature(cre_import, gff.toplevel(),
        nw::SerializationProfile::blueprint);

    check_creature_stats_parity(rt, cre_legacy, cre_import);
    check_creature_health_parity(rt, cre_legacy, cre_import);
    check_creature_levels_parity(rt, cre_legacy, cre_import);
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
            nullptr
        };
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

    // Reference: deserialize + populate via legacy path.
    auto* cre_ref = make_creature(path);
    ASSERT_NE(cre_ref, nullptr);
    rt.init_object_propsets(cre_ref->handle());
    nwn1::populate_creature_propsets(&rt, cre_ref);

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
    check_creature_stats_parity(rt, cre_ref, cre_rt);
    check_creature_health_parity(rt, cre_ref, cre_rt);
    check_creature_levels_parity(rt, cre_ref, cre_rt);
}

// ---------------------------------------------------------------------------
// Test 5: item importer ≡ populate_item_propsets
// ---------------------------------------------------------------------------

TEST_F(SmallsPropsetParity, ImporterMatchesPopulateItemStats)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    const char* path = "test_data/user/development/cloth028.uti";
    nw::Gff gff(path);
    ASSERT_TRUE(gff.valid());

    auto& rt = nw::kernel::runtime();

    // Legacy path: deserialize item, populate propsets from C++ struct.
    auto* item_legacy = make_item(path);
    ASSERT_NE(item_legacy, nullptr);
    rt.init_object_propsets(item_legacy->handle());
    nwn1::populate_item_propsets(&rt, item_legacy);

    // Importer path: deserialize item, populate propsets from GFF.
    auto* item_import = make_item(path);
    ASSERT_NE(item_import, nullptr);
    rt.init_object_propsets(item_import->handle());
    importer_->import_item(item_import, gff.toplevel(), nw::SerializationProfile::blueprint);

    auto tid = rt.type_id("core.item.ItemStats", false);
    ASSERT_NE(tid, nw::smalls::invalid_type_id);
    auto def = sdef(rt, tid);
    ASSERT_NE(def, nullptr);

    auto ref_l = rt.get_or_create_propset_ref(tid, item_legacy->handle());
    auto ref_i = rt.get_or_create_propset_ref(tid, item_import->handle());

    const char* fields[] = {
        "base_item", "stack_size", "charges",
        "cursed", "identified", "plot", "stolen",
        nullptr
    };
    for (const char** f = fields; *f; ++f) {
        EXPECT_EQ(read_int(rt, ref_l, def, *f), read_int(rt, ref_i, def, *f))
            << "ItemStats." << *f;
    }
    // cost/cost_additional: legacy uses int32_t cast of uint32 C++ field;
    // importer reads uint32 GFF. Both should match since they come from same data.
    EXPECT_EQ(read_int(rt, ref_l, def, "cost"), read_int(rt, ref_i, def, "cost"));
    EXPECT_EQ(read_int(rt, ref_l, def, "cost_additional"),
              read_int(rt, ref_i, def, "cost_additional"));
}

// ---------------------------------------------------------------------------
// Test 6: object JSON round-trip (serialize_object_json → deserialize_object_json)
// ---------------------------------------------------------------------------

TEST_F(SmallsPropsetParity, ObjectJsonRoundTrip)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    const char* path = "test_data/user/development/pl_agent_001.utc";
    auto& rt = nw::kernel::runtime();

    // Reference: load + populate propsets via legacy path.
    auto* cre = make_creature(path);
    ASSERT_NE(cre, nullptr);
    rt.init_object_propsets(cre->handle());
    nwn1::populate_creature_propsets(&rt, cre);

    // Serialize to unified object JSON.
    nlohmann::json j = nwn1::serialize_object_json(
        cre, &rt, registry_.get(), nw::SerializationProfile::blueprint);

    // Verify propset sections are present (keyed by qualified name).
    EXPECT_TRUE(j.contains("core.creature.CreatureStats"));
    EXPECT_TRUE(j.contains("core.creature.CreatureHealth"));
    EXPECT_TRUE(j.contains("core.creature.CreatureLevels"));
    // CreatureCombat is instance_only — skipped for blueprint profile.
    EXPECT_FALSE(j.contains("core.creature.CreatureCombat"));
    // C++ holdover section should be present.
    EXPECT_TRUE(j.contains("common"));

    // Fresh creature — deserialize from JSON.
    auto* cre2 = make_creature(path);
    ASSERT_NE(cre2, nullptr);
    rt.init_object_propsets(cre2->handle());
    bool ok = nwn1::deserialize_object_json(
        j, cre2, &rt, registry_.get(), nw::SerializationProfile::blueprint);
    EXPECT_TRUE(ok);

    // Propset fields must match.
    check_creature_stats_parity(rt, cre, cre2);
    check_creature_health_parity(rt, cre, cre2);
    check_creature_levels_parity(rt, cre, cre2);

    // C++ holdover field must round-trip.
    EXPECT_EQ(cre2->common.tag, cre->common.tag);
}
