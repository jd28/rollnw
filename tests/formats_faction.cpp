#include <catch2/catch_all.hpp>

#include <nw/formats/Faction.hpp>
#include <nw/serialization/Serialization.hpp>

TEST_CASE("faction: from_gff", "[objects]")
{
    nw::Gff g{"test_data/user/scratch/Repute.fac"};
    REQUIRE(g.valid());
    nw::Faction f{g};
    REQUIRE(f.factions.size() >= 4);
    REQUIRE(f.reputations.size() > 0);
    REQUIRE(f.factions[0].name == "PC");
    REQUIRE(f.factions[1].name == "Hostile");
    REQUIRE(f.factions[2].name == "Commoner");
    REQUIRE(f.factions[3].name == "Merchant");
}

TEST_CASE("faction: gff roundtrip", "[objects]")
{
    nw::Gff g{"test_data/user/scratch/Repute.fac"};
    REQUIRE(g.valid());
    nw::Faction f{g};
    nw::GffBuilder out = f.to_gff();

    REQUIRE(out.header.struct_offset == g.head_->struct_offset);
    REQUIRE(out.header.struct_count == g.head_->struct_count);
    REQUIRE(out.header.field_offset == g.head_->field_offset);
    REQUIRE(out.header.field_count == g.head_->field_count);
    REQUIRE(out.header.label_offset == g.head_->label_offset);
    REQUIRE(out.header.label_count == g.head_->label_count);
    REQUIRE(out.header.field_data_offset == g.head_->field_data_offset);
    REQUIRE(out.header.field_data_count == g.head_->field_data_count);
    REQUIRE(out.header.field_idx_offset == g.head_->field_idx_offset);
    REQUIRE(out.header.field_idx_count == g.head_->field_idx_count);
    REQUIRE(out.header.list_idx_offset == g.head_->list_idx_offset);
    REQUIRE(out.header.list_idx_count == g.head_->list_idx_count);
}
