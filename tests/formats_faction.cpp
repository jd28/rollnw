#include <gtest/gtest.h>

#include <nw/formats/Faction.hpp>
#include <nw/serialization/Serialization.hpp>

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
    nw::GffBuilder out = f.serialize();

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
