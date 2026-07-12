#include <gtest/gtest.h>

#include <nw/objects/Encounter.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/serialization/GffBuilder.hpp>
#include <nw/serialization/gff_conversion.hpp>
#include <nw/smalls/runtime.hpp>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <span>

namespace fs = std::filesystem;

namespace {

nw::smalls::Value encounter_state_ref(nw::Encounter* encounter, const nw::smalls::StructDef*& def)
{
    def = nullptr;
    auto& rt = nw::kernel::runtime();
    auto tid = rt.type_id("core.encounter.EncounterState", false);
    if (!encounter || tid == nw::smalls::invalid_type_id) { return {}; }

    auto ref = rt.find_propset_ref(tid, encounter->handle());
    if (ref.type_id == nw::smalls::invalid_type_id) { return {}; }

    def = rt.get_struct_def(tid);
    return def ? ref : nw::smalls::Value{};
}

int32_t encounter_state_int(nw::Encounter* encounter, const char* field)
{
    const nw::smalls::StructDef* def = nullptr;
    auto ref = encounter_state_ref(encounter, def);
    if (ref.type_id == nw::smalls::invalid_type_id || !def) { return 0; }

    auto& rt = nw::kernel::runtime();
    uint32_t idx = def->field_index(field);
    if (idx == UINT32_MAX) { return 0; }

    auto value = rt.read_value_field_at_offset(ref, def->fields[idx].offset, rt.int_type());
    return value.type_id == rt.int_type() ? value.data.ival : 0;
}

nw::Resref encounter_state_resref(nw::Encounter* encounter, const char* field)
{
    const nw::smalls::StructDef* def = nullptr;
    auto ref = encounter_state_ref(encounter, def);
    if (ref.type_id == nw::smalls::invalid_type_id || !def) { return {}; }

    auto& rt = nw::kernel::runtime();
    uint32_t idx = def->field_index(field);
    if (idx == UINT32_MAX) { return {}; }

    const auto& fd = def->fields[idx];
    auto value = rt.read_value_field_at_offset(ref, fd.offset, fd.type_id);
    auto* result = static_cast<nw::Resref*>(rt.get_value_data_ptr(value));
    return result ? *result : nw::Resref{};
}

nw::smalls::IArray* encounter_state_array(nw::Encounter* encounter, const char* field)
{
    const nw::smalls::StructDef* def = nullptr;
    auto ref = encounter_state_ref(encounter, def);
    if (ref.type_id == nw::smalls::invalid_type_id || !def) { return nullptr; }

    auto& rt = nw::kernel::runtime();
    uint32_t idx = def->field_index(field);
    if (idx == UINT32_MAX) { return nullptr; }

    const auto& fd = def->fields[idx];
    auto value = rt.read_value_field_at_offset(ref, fd.offset, fd.type_id);
    return rt.object_pool().get_unmanaged_array(nw::TypedHandle::from_ull(value.data.handle));
}

nw::Resref encounter_spawn_resref(nw::Encounter* encounter, size_t index)
{
    auto* array = encounter_state_array(encounter, "creatures");
    if (!array || index >= array->size()) { return {}; }

    auto& rt = nw::kernel::runtime();
    auto tid = rt.type_id("core.encounter.EncounterSpawn", false);
    const auto* def = rt.get_struct_def(tid);
    if (tid == nw::smalls::invalid_type_id || !def) { return {}; }

    nw::smalls::Value spawn;
    if (!array->get_value(index, spawn, rt)) { return {}; }

    uint32_t idx = def->field_index("resref");
    if (idx == UINT32_MAX) { return {}; }

    const auto& fd = def->fields[idx];
    auto value = rt.read_value_field_at_offset(spawn, fd.offset, fd.type_id);
    auto* result = static_cast<nw::Resref*>(rt.get_value_data_ptr(value));
    return result ? *result : nw::Resref{};
}

} // namespace

TEST(Encounter, GffDeserialize)
{
    auto name = nw::Encounter::get_name_from_file(fs::path("test_data/user/development/boundelementallo.ute"));
    EXPECT_EQ(name, "Bound Elemental Lord");

    auto enc = nw::kernel::objects().load_file<nw::Encounter>("test_data/user/development/boundelementallo.ute");
    EXPECT_TRUE(enc);

    EXPECT_EQ(enc->resref, "boundelementallo");
    EXPECT_TRUE(!!encounter_state_int(enc, "player_only"));
    EXPECT_EQ(encounter_state_resref(enc, "on_entered"), "");
    auto* creatures = encounter_state_array(enc, "creatures");
    ASSERT_NE(creatures, nullptr);
    EXPECT_GT(creatures->size(), size_t{0});
    EXPECT_EQ(encounter_spawn_resref(enc, 0), "tyn_bound_acid_l");
}

TEST(Encounter, JsonSerialize)
{
    auto enc = nw::kernel::objects().load_file<nw::Encounter>("test_data/user/development/boundelementallo.ute");
    EXPECT_TRUE(enc);

    nlohmann::json j;
    nw::serialize(enc, j, nw::SerializationProfile::blueprint);

    std::ofstream f{"tmp/boundelementallo.ute.json"};
    f << std::setw(4) << j;
    f.close();

    auto name = nw::Encounter::get_name_from_file(fs::path("tmp/boundelementallo.ute.json"));
    EXPECT_EQ(name, "Bound Elemental Lord");
}

TEST(Encounter, JsonRoundTrip)
{
    auto enc = nw::kernel::objects().load_file<nw::Encounter>("test_data/user/development/boundelementallo.ute");
    EXPECT_TRUE(enc);

    nlohmann::json j;
    nw::serialize(enc, j, nw::SerializationProfile::blueprint);

    auto* enc2 = nw::kernel::objects().make<nw::Encounter>();
    ASSERT_NE(enc2, nullptr);
    EXPECT_TRUE(nw::deserialize(enc2, j, nw::SerializationProfile::blueprint));

    nlohmann::json j2;
    nw::serialize(enc2, j2, nw::SerializationProfile::blueprint);
    EXPECT_EQ(j, j2);
}

TEST(Encounter, InstanceJsonRoundTripStoresGeometryComponent)
{
    auto* enc = nw::kernel::objects().make<nw::Encounter>();
    ASSERT_NE(enc, nullptr);
    ASSERT_NE(nw::kernel::runtime().load_module("core.encounter"), nullptr);
    nw::kernel::runtime().init_object_propsets(enc->handle());

    const std::array<glm::vec3, 3> points{
        glm::vec3{0.0f, 0.0f, 0.0f},
        glm::vec3{3.0f, 0.0f, 0.0f},
        glm::vec3{0.0f, 3.0f, 0.0f},
    };
    ASSERT_TRUE(nw::kernel::objects().components().set_geometry(enc->handle(), std::span<const glm::vec3>{points}));
    const std::array<nw::ObjectSpawnPoint, 2> spawn_points{
        nw::ObjectSpawnPoint{.position = {1.0f, 2.0f, 3.0f}, .orientation = 0.5f},
        nw::ObjectSpawnPoint{.position = {4.0f, 5.0f, 6.0f}, .orientation = 1.5f},
    };
    ASSERT_TRUE(nw::kernel::objects().components().set_spawn_points(
        enc->handle(), std::span<const nw::ObjectSpawnPoint>{spawn_points}));

    nlohmann::json j;
    ASSERT_TRUE(nw::serialize(enc, j, nw::SerializationProfile::instance));
    ASSERT_TRUE(j.contains("components"));
    ASSERT_TRUE(j["components"].contains("geometry"));
    ASSERT_EQ(j["components"]["geometry"].size(), points.size());
    ASSERT_TRUE(j["components"].contains("spawn_points"));
    ASSERT_EQ(j["components"]["spawn_points"].size(), spawn_points.size());

    auto* enc2 = nw::kernel::objects().make<nw::Encounter>();
    ASSERT_NE(enc2, nullptr);
    ASSERT_TRUE(nw::deserialize(enc2, j, nw::SerializationProfile::instance));

    const auto* geometry = nw::kernel::objects().components().find_geometry(enc2->handle());
    ASSERT_NE(geometry, nullptr);
    ASSERT_EQ(geometry->points.size(), points.size());
    EXPECT_FLOAT_EQ(geometry->points[1].x, 3.0f);
    ASSERT_EQ(geometry->spawn_points.size(), spawn_points.size());
    EXPECT_FLOAT_EQ(geometry->spawn_points[1].position.y, 5.0f);
    EXPECT_FLOAT_EQ(geometry->spawn_points[1].orientation, 1.5f);
}

TEST(Encounter, GffRoundTrip)
{
    nw::Gff g("test_data/user/development/boundelementallo.ute");
    EXPECT_TRUE(g.valid());

    auto* enc = nw::kernel::objects().make<nw::Encounter>();
    ASSERT_NE(enc, nullptr);
    EXPECT_TRUE(deserialize(enc, g.toplevel(),
        nw::SerializationProfile::blueprint));

    nw::GffBuilder oa = serialize(enc, nw::SerializationProfile::blueprint);
    oa.write_to("tmp/boundelementallo_2.ute");
    nw::Gff g2{"tmp/boundelementallo_2.ute"};

    EXPECT_EQ(nw::gff_to_gffjson(g), nw::gff_to_gffjson(g2));

    EXPECT_EQ(oa.header.struct_offset, g.head_->struct_offset);
    EXPECT_EQ(oa.header.struct_count, g.head_->struct_count);
    EXPECT_EQ(oa.header.field_offset, g.head_->field_offset);
    EXPECT_EQ(oa.header.field_count, g.head_->field_count);
    EXPECT_EQ(oa.header.label_offset, g.head_->label_offset);
    EXPECT_EQ(oa.header.label_count, g.head_->label_count);
    EXPECT_EQ(oa.header.field_data_offset, g.head_->field_data_offset);
    EXPECT_EQ(oa.header.field_data_count, g.head_->field_data_count);
    EXPECT_EQ(oa.header.field_idx_offset, g.head_->field_idx_offset);
    EXPECT_EQ(oa.header.field_idx_count, g.head_->field_idx_count);
    EXPECT_EQ(oa.header.list_idx_offset, g.head_->list_idx_offset);
    EXPECT_EQ(oa.header.list_idx_count, g.head_->list_idx_count);
}
