#include <catch2/catch.hpp>

#include <nw/objects/Store.hpp>
#include <nw/serialization/Archives.hpp>

#include <nlohmann/json.hpp>

#include <fstream>

TEST_CASE("Loading store blueprint", "[objects]")
{
    nw::GffInputArchive g{"test_data/user/development/storethief002.utm"};
    REQUIRE(g.valid());
    nw::Store s{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(s.common()->resref == "storethief002");
    REQUIRE(s.blackmarket);
    REQUIRE(s.blackmarket_markdown == 25);
    REQUIRE(s.weapons.items.size() > 0);
    REQUIRE(std::get<nw::Resref>(s.weapons.items[0].item) == "nw_wswdg001");
}

TEST_CASE("store: to_json", "[objects]")
{
    nw::GffInputArchive g{"test_data/user/development/storethief002.utm"};
    REQUIRE(g.valid());
    nw::Store s{g.toplevel(), nw::SerializationProfile::blueprint};

    nlohmann::json j = s.to_json(nw::SerializationProfile::blueprint);

    std::ofstream f{"tmp/storethief002.utm.json"};
    f << std::setw(4) << j;
}

TEST_CASE("store: from_json", "[objects]")
{
    std::ifstream f{"tmp/storethief002.utm.json"};
    auto j = nlohmann::json::parse(f);
    nw::Store s;
    s.from_json(j, nw::SerializationProfile::blueprint);
    REQUIRE(s.common()->resref == "storethief002");
    REQUIRE(s.blackmarket);
    REQUIRE(s.blackmarket_markdown == 25);
}

TEST_CASE("store: json to and from", "[objects]")
{
    nw::GffInputArchive g{"test_data/user/development/storethief002.utm"};
    REQUIRE(g.valid());
    nw::Store s{g.toplevel(), nw::SerializationProfile::blueprint};

    nlohmann::json j = s.to_json(nw::SerializationProfile::blueprint);
    nw::Store s2{j, nw::SerializationProfile::blueprint};
    nlohmann::json j2 = s2.to_json(nw::SerializationProfile::blueprint);
    REQUIRE(j == j2);
}

TEST_CASE("store: gff round trip", "[ojbects]")
{
    nw::GffInputArchive g("test_data/user/development/storethief002.utm");
    REQUIRE(g.valid());

    nw::Store store{g.toplevel(), nw::SerializationProfile::blueprint};
    nw::GffOutputArchive oa = store.to_gff(nw::SerializationProfile::blueprint);
    oa.write_to("tmp/storethief002_2.utm");

    nw::GffInputArchive g2("tmp/storethief002_2.utm");
    REQUIRE(g2.valid());

    std::ofstream f1{"tmp/storethief002.utm.gffjson", std::ios_base::binary};
    f1 << std::setw(4) << nw::gff_to_gffjson(g);
    std::ofstream f2{"tmp/storethief002_2.utm.gffjson", std::ios_base::binary};
    f2 << std::setw(4) << nw::gff_to_gffjson(g2);

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
