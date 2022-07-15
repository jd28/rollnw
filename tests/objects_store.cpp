#include <catch2/catch.hpp>

#include <nw/components/Store.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/serialization/Archives.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST_CASE("Loading store blueprint", "[objects]")
{
    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/storethief002.utm"));
    REQUIRE(ent.is_alive());

    auto s = ent.get<nw::Store>();
    auto common = ent.get<nw::Common>();
    auto inv = ent.get<nw::StoreInventory>();

    REQUIRE(common->resref == "storethief002");
    REQUIRE(s->blackmarket);
    REQUIRE(s->blackmarket_markdown == 25);
    REQUIRE(inv->weapons.items.size() > 0);
    REQUIRE(std::get<nw::Resref>(inv->weapons.items[0].item) == "nw_wswdg001");
}

TEST_CASE("store: to_json", "[objects]")
{
    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/storethief002.utm"));
    REQUIRE(ent.is_alive());

    nlohmann::json j;
    nw::kernel::objects().serialize(ent, j, nw::SerializationProfile::blueprint);

    std::ofstream f{"tmp/storethief002.utm.json"};
    f << std::setw(4) << j;
}

TEST_CASE("store: from_json", "[objects]")
{
    auto ent = nw::kernel::objects().make(nw::ObjectType::store);
    REQUIRE(ent.is_alive());

    std::ifstream f{"tmp/storethief002.utm.json"};
    auto j = nlohmann::json::parse(f);
    nw::Store::deserialize(ent, j, nw::SerializationProfile::blueprint);

    auto s = ent.get<nw::Store>();
    auto common = ent.get<nw::Common>();
    auto inv = ent.get<nw::StoreInventory>();

    REQUIRE(common->resref == "storethief002");
    REQUIRE(s->blackmarket);
    REQUIRE(s->blackmarket_markdown == 25);
    REQUIRE(inv->weapons.items.size() > 0);
    REQUIRE(std::get<nw::Resref>(inv->weapons.items[0].item) == "nw_wswdg001");
}

TEST_CASE("store: json to and from", "[objects]")
{
    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/storethief002.utm"));
    REQUIRE(ent.is_alive());

    nlohmann::json j;
    nw::kernel::objects().serialize(ent, j, nw::SerializationProfile::blueprint);

    auto ent2 = nw::kernel::objects().deserialize(nw::ObjectType::store,
        j,
        nw::SerializationProfile::blueprint);
    REQUIRE(ent2.is_alive());

    nlohmann::json j2;
    nw::kernel::objects().serialize(ent2, j2, nw::SerializationProfile::blueprint);

    REQUIRE(j == j2);
}

TEST_CASE("store: gff round trip", "[ojbects]")
{
    nw::GffInputArchive g("test_data/user/development/storethief002.utm");
    REQUIRE(g.valid());

    auto ent = nw::kernel::objects().deserialize(nw::ObjectType::store,
        g.toplevel(),
        nw::SerializationProfile::blueprint);
    REQUIRE(ent.is_alive());

    nw::GffOutputArchive oa = nw::kernel::objects().serialize(ent, nw::SerializationProfile::blueprint);
    oa.write_to("tmp/storethief002_2.utm");

    nw::GffInputArchive g2("tmp/storethief002_2.utm");
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
