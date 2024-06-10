#include <gtest/gtest.h>

#include "nw/formats/Faction.hpp"
#include "nw/serialization/Gff.hpp"
#include "nw/serialization/GffBuilder.hpp"
#include "nw/serialization/Serialization.hpp"

#include <nlohmann/json.hpp>

TEST(Faction, GffDeserialize)
{
    nw::Gff g{"test_data/user/scratch/Repute.fac"};
    EXPECT_TRUE(g.valid());
    nw::Faction f{g};
    EXPECT_TRUE(f.factions.size() >= 4);
    EXPECT_TRUE(f.reputations.size() > 0);
    EXPECT_EQ(f.factions[0].name, "PC");
    EXPECT_EQ(f.factions[1].name, "Hostile");
    EXPECT_EQ(f.factions[2].name, "Commoner");
    EXPECT_EQ(f.factions[3].name, "Merchant");
}

TEST(Faction, GffRoundTrip)
{
    nw::Gff g{"test_data/user/scratch/Repute.fac"};
    EXPECT_TRUE(g.valid());
    nw::Faction f{g};
    nw::GffBuilder out = nw::serialize(f);

    EXPECT_TRUE(out.header.struct_offset == g.head_->struct_offset);
    EXPECT_TRUE(out.header.struct_count == g.head_->struct_count);
    EXPECT_TRUE(out.header.field_offset == g.head_->field_offset);
    EXPECT_TRUE(out.header.field_count == g.head_->field_count);
    EXPECT_TRUE(out.header.label_offset == g.head_->label_offset);
    EXPECT_TRUE(out.header.label_count == g.head_->label_count);
    EXPECT_TRUE(out.header.field_data_offset == g.head_->field_data_offset);
    EXPECT_TRUE(out.header.field_data_count == g.head_->field_data_count);
    EXPECT_TRUE(out.header.field_idx_offset == g.head_->field_idx_offset);
    EXPECT_TRUE(out.header.field_idx_count == g.head_->field_idx_count);
    EXPECT_TRUE(out.header.list_idx_offset == g.head_->list_idx_offset);
    EXPECT_TRUE(out.header.list_idx_count == g.head_->list_idx_count);
}

TEST(Faction, JsonSerialize)
{
    nw::Gff g{"test_data/user/scratch/Repute.fac"};
    EXPECT_TRUE(g.valid());

    nw::Faction faction{g};
    auto json = faction.to_json();

    std::ofstream f{"tmp/repute.fac.json"};
    f << std::setw(4) << json;
}

TEST(Faction, JsonDeserialize)
{
    std::ifstream f("test_data/user/scratch/repute.fac.json");
    nlohmann::json json = nlohmann::json::parse(f);
    nw::Faction faction(json);
    EXPECT_TRUE(faction.factions.size() >= 4);
    EXPECT_TRUE(faction.reputations.size() > 0);
    EXPECT_EQ(faction.factions[0].name, "PC");
    EXPECT_EQ(faction.factions[1].name, "Hostile");
    EXPECT_EQ(faction.factions[2].name, "Commoner");
    EXPECT_EQ(faction.factions[3].name, "Merchant");
}

TEST(Faction, ResDataCtor)
{
    nw::Faction faction(nw::ResourceData::from_file("test_data/user/scratch/Repute.fac"));
    EXPECT_TRUE(faction.factions.size() >= 4);
    EXPECT_TRUE(faction.reputations.size() > 0);
    EXPECT_EQ(faction.factions[0].name, "PC");
    EXPECT_EQ(faction.factions[1].name, "Hostile");
    EXPECT_EQ(faction.factions[2].name, "Commoner");
    EXPECT_EQ(faction.factions[3].name, "Merchant");

    nw::Faction faction2(nw::ResourceData::from_file("test_data/user/scratch/repute.fac.json"));
    EXPECT_TRUE(faction2.factions.size() >= 4);
    EXPECT_TRUE(faction2.reputations.size() > 0);
    EXPECT_EQ(faction2.factions[0].name, "PC");
    EXPECT_EQ(faction2.factions[1].name, "Hostile");
    EXPECT_EQ(faction2.factions[2].name, "Commoner");
    EXPECT_EQ(faction2.factions[3].name, "Merchant");
}
