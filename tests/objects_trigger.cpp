#include <gtest/gtest.h>

#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Trigger.hpp>
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

int32_t trigger_state_int(nw::Trigger* trigger, const char* field)
{
    auto& rt = nw::kernel::runtime();
    auto tid = rt.type_id("core.trigger.TriggerState", false);
    if (!trigger || tid == nw::smalls::invalid_type_id) { return 0; }

    auto ref = rt.find_propset_ref(tid, trigger->handle());
    if (ref.type_id == nw::smalls::invalid_type_id) { return 0; }

    const auto* def = rt.get_struct_def(tid);
    if (!def) { return 0; }

    uint32_t idx = def->field_index(field);
    if (idx == UINT32_MAX) { return 0; }

    auto value = rt.read_value_field_at_offset(ref, def->fields[idx].offset, rt.int_type());
    return value.type_id == rt.int_type() ? value.data.ival : 0;
}

nw::Resref trigger_state_resref(nw::Trigger* trigger, const char* field)
{
    auto& rt = nw::kernel::runtime();
    auto tid = rt.type_id("core.trigger.TriggerState", false);
    if (!trigger || tid == nw::smalls::invalid_type_id) { return {}; }

    auto ref = rt.find_propset_ref(tid, trigger->handle());
    if (ref.type_id == nw::smalls::invalid_type_id) { return {}; }

    const auto* def = rt.get_struct_def(tid);
    if (!def) { return {}; }

    uint32_t idx = def->field_index(field);
    if (idx == UINT32_MAX) { return {}; }

    const auto& fd = def->fields[idx];
    auto value = rt.read_value_field_at_offset(ref, fd.offset, fd.type_id);
    auto* result = static_cast<nw::Resref*>(rt.get_value_data_ptr(value));
    return result ? *result : nw::Resref{};
}

} // namespace

TEST(Trigger, GffDeserialize)
{
    auto name = nw::Trigger::get_name_from_file(fs::path("test_data/user/development/pl_spray_sewage.utt"));
    EXPECT_EQ(name, "pl_spray_sewage");

    auto ent = nw::kernel::objects().load_file<nw::Trigger>("test_data/user/development/pl_spray_sewage.utt");
    EXPECT_TRUE(ent);

    EXPECT_EQ(ent->resref, "pl_spray_sewage");
    EXPECT_EQ(trigger_state_int(ent, "portrait"), 0);
    EXPECT_EQ(trigger_state_int(ent, "loadscreen"), 0);
    EXPECT_EQ(trigger_state_resref(ent, "on_enter"), "pl_trig_sewage");
}

TEST(Trigger, JsonRoundTrip)
{
    auto ent = nw::kernel::objects().load_file<nw::Trigger>("test_data/user/development/pl_spray_sewage.utt");
    EXPECT_TRUE(ent);

    nlohmann::json j;
    nw::serialize(ent, j, nw::SerializationProfile::blueprint);

    auto ent2 = nw::kernel::objects().make<nw::Trigger>();
    EXPECT_TRUE(ent2);
    EXPECT_TRUE(nw::deserialize(ent2, j, nw::SerializationProfile::blueprint));

    nlohmann::json j2;
    EXPECT_TRUE(nw::serialize(ent2, j2, nw::SerializationProfile::blueprint));

    EXPECT_EQ(j, j2);

    std::ofstream f{"tmp/pl_spray_sewage.utt.json"};
    f << std::setw(4) << j;
    f.close();

    auto name = nw::Trigger::get_name_from_file(fs::path("tmp/pl_spray_sewage.utt.json"));
    EXPECT_EQ(name, "pl_spray_sewage");

    auto ent3 = nw::kernel::objects().load_file<nw::Trigger>("tmp/pl_spray_sewage.utt.json");
    EXPECT_TRUE(ent3);
}

TEST(Trigger, InstanceJsonRoundTripStoresGeometryComponent)
{
    auto* ent = nw::kernel::objects().make<nw::Trigger>();
    ASSERT_NE(ent, nullptr);
    ASSERT_NE(nw::kernel::runtime().load_module("core.trigger"), nullptr);
    nw::kernel::runtime().init_object_propsets(ent->handle());

    const std::array<glm::vec3, 3> points{
        glm::vec3{0.0f, 0.0f, 0.0f},
        glm::vec3{2.0f, 0.0f, 0.0f},
        glm::vec3{0.0f, 2.0f, 0.0f},
    };
    ASSERT_TRUE(nw::kernel::objects().components().set_geometry(ent->handle(), std::span<const glm::vec3>{points}));

    nlohmann::json j;
    ASSERT_TRUE(nw::serialize(ent, j, nw::SerializationProfile::instance));
    ASSERT_TRUE(j.contains("components"));
    ASSERT_TRUE(j["components"].contains("geometry"));
    ASSERT_EQ(j["components"]["geometry"].size(), points.size());

    auto* ent2 = nw::kernel::objects().make<nw::Trigger>();
    ASSERT_NE(ent2, nullptr);
    ASSERT_TRUE(nw::deserialize(ent2, j, nw::SerializationProfile::instance));

    const auto* geometry = nw::kernel::objects().components().find_geometry(ent2->handle());
    ASSERT_NE(geometry, nullptr);
    ASSERT_EQ(geometry->points.size(), points.size());
    EXPECT_FLOAT_EQ(geometry->points[1].x, 2.0f);
}

TEST(Trigger, GffRoundTrip)
{
    nw::Gff g("test_data/user/development/pl_spray_sewage.utt");
    EXPECT_TRUE(g.valid());

    auto ent = nw::kernel::objects().make<nw::Trigger>();
    EXPECT_TRUE(ent);
    EXPECT_TRUE(deserialize(ent, g.toplevel(), nw::SerializationProfile::blueprint));

    nw::GffBuilder oa = serialize(ent, nw::SerializationProfile::blueprint);
    oa.write_to("tmp/pl_spray_sewage_2.utt");

    nw::Gff g2{"tmp/pl_spray_sewage_2.utt"};
    EXPECT_TRUE(g2.valid());
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
