#include <catch2/catch.hpp>

#include <nw/components/Trigger.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/serialization/Archives.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST_CASE("trigger: from_gff", "[objects]")
{
    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/pl_spray_sewage.utt"));
    REQUIRE(ent.is_alive());

    auto t = ent.get<nw::Trigger>();
    auto common = ent.get<nw::Common>();
    auto scripts = ent.get<nw::TriggerScripts>();

    REQUIRE(common->resref == "pl_spray_sewage");
    REQUIRE(t->portrait == 0);
    REQUIRE(t->loadscreen == 0);
    REQUIRE(scripts->on_enter == "pl_trig_sewage");
}

TEST_CASE("trigger: json round trip", "[objects]")
{
    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/pl_spray_sewage.utt"));
    REQUIRE(ent.is_alive());

    nlohmann::json j;
    nw::kernel::objects().serialize(ent, j, nw::SerializationProfile::blueprint);

    auto ent2 = nw::kernel::objects().deserialize(nw::ObjectType::trigger,
        j,
        nw::SerializationProfile::blueprint);
    REQUIRE(ent2.is_alive());

    nlohmann::json j2;
    nw::kernel::objects().serialize(ent2, j2, nw::SerializationProfile::blueprint);

    REQUIRE(j == j2);

    std::ofstream f{"tmp/pl_spray_sewage.utt.json"};
    f << std::setw(4) << j;
}

TEST_CASE("trigger: gff round trip", "[ojbects]")
{
    nw::GffInputArchive g("test_data/user/development/pl_spray_sewage.utt");
    REQUIRE(g.valid());

    auto ent = nw::kernel::objects().deserialize(nw::ObjectType::trigger,
        g.toplevel(),
        nw::SerializationProfile::blueprint);
    REQUIRE(ent.is_alive());

    nw::GffOutputArchive oa = nw::kernel::objects().serialize(ent, nw::SerializationProfile::blueprint);
    oa.write_to("tmp/pl_spray_sewage_2.utt");

    nw::GffInputArchive g2{"tmp/pl_spray_sewage_2.utt"};
    REQUIRE(g2.valid());
    REQUIRE(nw::gff_to_gffjson(g) == nw::gff_to_gffjson(g2));

    REQUIRE(oa.header.struct_offset == g.head_->struct_offset);
    REQUIRE(oa.header.struct_count == g.head_->struct_count);
    REQUIRE(oa.header.field_offset == g.head_->field_offset);
    REQUIRE(oa.header.field_count == g.head_->field_count);
    REQUIRE(oa.header.label_offset == g.head_->label_offset);
    REQUIRE(oa.header.label_count == g.head_->label_count);
    REQUIRE(oa.header.field_data_offset == g.head_->field_data_offset);
    REQUIRE(oa.header.field_data_count == g.head_->field_data_count);
    REQUIRE(oa.header.field_idx_offset == g.head_->field_idx_offset);
    REQUIRE(oa.header.field_idx_count == g.head_->field_idx_count);
    REQUIRE(oa.header.list_idx_offset == g.head_->list_idx_offset);
    REQUIRE(oa.header.list_idx_count == g.head_->list_idx_count);
}
