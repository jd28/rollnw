#include <catch2/catch.hpp>

#include <nw/objects/Item.hpp>
#include <nw/serialization/Serialization.hpp>

#include <nlohmann/json.hpp>

#include <fstream>

TEST_CASE("item: load armor item", "[objects]")
{
    nw::GffInputArchive g{"test_data/user/development/cloth028.uti"};
    nw::Item i{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(i.common()->resref == "cloth028");
    REQUIRE(i.properties.size() > 0);
    REQUIRE(i.model_type == nw::ItemModelType::armor);
    REQUIRE(i.common()->locals.size() > 0);
}

TEST_CASE("item: load layered item", "[objects]")
{
    nw::GffInputArchive g{"test_data/user/development/wduersc004.uti"};
    nw::Item i{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(i.common()->resref == "wduersc004");
    REQUIRE(i.properties.size() > 0);
    REQUIRE(i.model_type == nw::ItemModelType::composite);
}

TEST_CASE("item: load simple item", "[objects]")
{
    nw::GffInputArchive g{"test_data/user/development/pl_aleu_shuriken.uti"};
    nw::Item i{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(i.common()->resref == "pl_aleu_shuriken");
    REQUIRE(i.properties.size() > 0);
    REQUIRE(i.model_type == nw::ItemModelType::simple);
}

TEST_CASE("item: to_json", "[objects]")
{
    nw::GffInputArchive g{"test_data/user/development/cloth028.uti"};
    nw::Item i{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(i.common()->resref == "cloth028");
    REQUIRE(i.properties.size() > 0);
    REQUIRE(i.model_type == nw::ItemModelType::armor);
    REQUIRE(i.common()->locals.size() > 0);

    nlohmann::json j = i.to_json(nw::SerializationProfile::blueprint);

    nw::Item it2;
    it2.from_json(j, nw::SerializationProfile::blueprint);
    REQUIRE(it2.common()->locals.get_string("stringtest") == "0");
    it2.common()->locals.set_int("inttest", 42);
    REQUIRE(it2.common()->locals.get_int("inttest") == 42);

    std::ofstream f{"tmp/cloth028.uti.json"};
    f << std::setw(4) << j;
}

TEST_CASE("item: json to and from", "[objects]")
{
    nw::GffInputArchive g{"test_data/user/development/cloth028.uti"};
    nw::Item i{g.toplevel(), nw::SerializationProfile::blueprint};
    nlohmann::json j = i.to_json(nw::SerializationProfile::blueprint);
    nw::Item it2{j, nw::SerializationProfile::blueprint};
    REQUIRE(it2.common()->locals.get_string("stringtest") == "0");
    nlohmann::json j2 = it2.to_json(nw::SerializationProfile::blueprint);
    REQUIRE(j == j2);
}

TEST_CASE("item: armor to_gff", "[objects]")
{
    nw::GffInputArchive g{"test_data/user/development/cloth028.uti"};
    nw::Item i{g.toplevel(), nw::SerializationProfile::blueprint};
    nw::GffOutputArchive oa{"UTI"};
    REQUIRE(i.to_gff(oa.top, nw::SerializationProfile::blueprint));
    oa.build();
    oa.write_to("tmp/cloth028.uti");

    nw::GffInputArchive g2{"tmp/cloth028.uti"};
    REQUIRE(g2.valid());
    nw::Item i2{g2.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(i.to_json(nw::SerializationProfile::blueprint) == i2.to_json(nw::SerializationProfile::blueprint));
}

TEST_CASE("item: gff round trip", "[ojbects]")
{
    nw::GffInputArchive g("test_data/user/development/cloth028.uti");
    REQUIRE(g.valid());

    nw::Item item{g.toplevel(), nw::SerializationProfile::blueprint};
    nw::GffOutputArchive oa = item.to_gff(nw::SerializationProfile::blueprint);

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
