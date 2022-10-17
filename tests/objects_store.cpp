#include <catch2/catch_all.hpp>

#include <nw/components/Store.hpp>
#include <nw/kernel/Objects.hpp>
#include <nw/serialization/Archives.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST_CASE("Loading store blueprint", "[objects]")
{
    auto ent = nw::kernel::objects().load<nw::Store>(fs::path("test_data/user/development/storethief002.utm"));
    REQUIRE(ent);

    REQUIRE(ent->common.resref == "storethief002");
    REQUIRE(ent->blackmarket);
    REQUIRE(ent->blackmarket_markdown == 25);
    REQUIRE(ent->inventory.weapons.items.size() > 0);
    REQUIRE(std::get<nw::Item*>(ent->inventory.weapons.items[0].item)->common.resref == "nw_wswdg001");
}

TEST_CASE("store: to_json", "[objects]")
{
    auto ent = nw::kernel::objects().load<nw::Store>(fs::path("test_data/user/development/storethief002.utm"));
    REQUIRE(ent);

    nlohmann::json j;
    nw::Store::serialize(ent, j, nw::SerializationProfile::blueprint);

    std::ofstream f{"tmp/storethief002.utm.json"};
    f << std::setw(4) << j;
}

TEST_CASE("store: from_json", "[objects]")
{
    auto ent = nw::kernel::objects().make<nw::Store>();
    REQUIRE(ent);

    std::ifstream f{"tmp/storethief002.utm.json"};
    auto j = nlohmann::json::parse(f);
    nw::Store::deserialize(ent, j, nw::SerializationProfile::blueprint);

    REQUIRE(ent->common.resref == "storethief002");
    REQUIRE(ent->blackmarket);
    REQUIRE(ent->blackmarket_markdown == 25);
    REQUIRE(ent->inventory.weapons.items.size() > 0);
    REQUIRE(std::get<nw::Resref>(ent->inventory.weapons.items[0].item) == "nw_wswdg001");
}

TEST_CASE("store: json to and from", "[objects]")
{
    auto ent = nw::kernel::objects().load<nw::Store>(fs::path("test_data/user/development/storethief002.utm"));
    REQUIRE(ent);

    nlohmann::json j;
    nw::Store::serialize(ent, j, nw::SerializationProfile::blueprint);

    auto ent2 = nw::kernel::objects().make<nw::Store>();
    nw::Store::deserialize(ent2, j, nw::SerializationProfile::blueprint);
    REQUIRE(ent2);

    nlohmann::json j2;
    nw::Store::serialize(ent2, j2, nw::SerializationProfile::blueprint);

    REQUIRE(j == j2);
}

TEST_CASE("store: gff round trip", "[ojbects]")
{
    nw::Gff g("test_data/user/development/storethief002.utm");
    REQUIRE(g.valid());

    auto ent = nw::kernel::objects().make<nw::Store>();
    REQUIRE(ent);
    nw::Store::deserialize(ent, g.toplevel(), nw::SerializationProfile::blueprint);

    nw::GffBuilder oa = nw::Store::serialize(ent, nw::SerializationProfile::blueprint);
    oa.write_to("tmp/storethief002_2.utm");

    nw::Gff g2("tmp/storethief002_2.utm");
    REQUIRE(g2.valid());

    // Problem: store pages aren't saved in the same order as toolset
    // REQUIRE(nw::gff_to_gffjson(g) == nw::gff_to_gffjson(g2));

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
