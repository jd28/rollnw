#include <catch2/catch_all.hpp>

#include <nw/components/Trigger.hpp>
#include <nw/kernel/Objects.hpp>
#include <nw/serialization/Archives.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST_CASE("trigger: from_gff", "[objects]")
{
    auto ent = nw::kernel::objects().load<nw::Trigger>(fs::path("test_data/user/development/pl_spray_sewage.utt"));
    REQUIRE(ent);

    REQUIRE(ent->common.resref == "pl_spray_sewage");
    REQUIRE(ent->portrait == 0);
    REQUIRE(ent->loadscreen == 0);
    REQUIRE(ent->scripts.on_enter == "pl_trig_sewage");
}

TEST_CASE("trigger: json round trip", "[objects]")
{
    auto ent = nw::kernel::objects().load<nw::Trigger>(fs::path("test_data/user/development/pl_spray_sewage.utt"));
    REQUIRE(ent);

    nlohmann::json j;
    nw::Trigger::serialize(ent, j, nw::SerializationProfile::blueprint);

    auto ent2 = nw::kernel::objects().make<nw::Trigger>();
    REQUIRE(ent2);
    REQUIRE(nw::Trigger::deserialize(ent2, j, nw::SerializationProfile::blueprint));

    nlohmann::json j2;
    REQUIRE(nw::Trigger::serialize(ent2, j2, nw::SerializationProfile::blueprint));

    REQUIRE(j == j2);

    std::ofstream f{"tmp/pl_spray_sewage.utt.json"};
    f << std::setw(4) << j;
}

TEST_CASE("trigger: gff round trip", "[objects]")
{
    nw::Gff g("test_data/user/development/pl_spray_sewage.utt");
    REQUIRE(g.valid());

    auto ent = nw::kernel::objects().make<nw::Trigger>();
    REQUIRE(ent);
    REQUIRE(nw::Trigger::deserialize(ent, g.toplevel(), nw::SerializationProfile::blueprint));

    nw::GffBuilder oa = nw::Trigger::serialize(ent, nw::SerializationProfile::blueprint);
    oa.write_to("tmp/pl_spray_sewage_2.utt");

    nw::Gff g2{"tmp/pl_spray_sewage_2.utt"};
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
