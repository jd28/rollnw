#include <gtest/gtest.h>

#include <nw/rules/creature_body_parts.hpp>

#include <array>

TEST(CreatureBodyPartCatalog, PublishesBoundedVariableLengthSpans)
{
    nw::CreatureBodyPartCatalog catalog;
    const std::array sets{
        nw::CreatureBodyPartSet{.assembly_id = 7, .part_offset = 0, .part_count = 2},
        nw::CreatureBodyPartSet{.assembly_id = 8, .part_offset = 2, .part_count = 1},
    };
    const std::array parts{
        nw::CreatureBodyPartInfo{
            .part_id = 10,
            .mirror_part_id = 11,
            .option_offset = 0,
            .option_count = 2,
            .label = "Left",
        },
        nw::CreatureBodyPartInfo{
            .part_id = 11,
            .mirror_part_id = 10,
            .option_offset = 2,
            .option_count = 1,
            .label = "Right",
        },
        nw::CreatureBodyPartInfo{
            .part_id = 99,
            .option_offset = 3,
            .option_count = 1,
            .label = "Only",
        },
    };
    const std::array options{
        nw::CreatureBodyPartOption{.part_id = 10, .option_id = 0},
        nw::CreatureBodyPartOption{
            .part_id = 10, .option_id = 3, .model = nw::Resref{"left003"}},
        nw::CreatureBodyPartOption{
            .part_id = 11, .option_id = 3, .model = nw::Resref{"right003"}},
        nw::CreatureBodyPartOption{
            .part_id = 99, .option_id = 4, .model = nw::Resref{"only004"}},
    };

    nw::String diagnostic;
    ASSERT_TRUE(catalog.publish(sets, parts, options, diagnostic)) << diagnostic;
    ASSERT_EQ(catalog.parts(7).size(), 2);
    ASSERT_EQ(catalog.parts(8).size(), 1);
    EXPECT_TRUE(catalog.parts(9).empty());
    ASSERT_EQ(catalog.options(7, 10).size(), 2);
    EXPECT_TRUE(catalog.options(7, 12).empty());
    ASSERT_NE(catalog.option(7, 10, 3), nullptr);
    EXPECT_EQ(catalog.option(7, 10, 3)->model, nw::Resref{"left003"});
    EXPECT_EQ(catalog.option(7, 11, 3)->model, nw::Resref{"right003"});
}

TEST(CreatureBodyPartCatalog, RejectsCompleteInvalidBatchWithoutPublication)
{
    nw::CreatureBodyPartCatalog catalog;
    const std::array sets{
        nw::CreatureBodyPartSet{.assembly_id = 1, .part_offset = 0, .part_count = 1},
    };
    const std::array parts{
        nw::CreatureBodyPartInfo{
            .part_id = 5, .option_offset = 0, .option_count = 1, .label = "Head"},
    };
    const std::array options{
        nw::CreatureBodyPartOption{.part_id = 5, .option_id = 1},
    };
    nw::String diagnostic;
    ASSERT_TRUE(catalog.publish(sets, parts, options, diagnostic)) << diagnostic;

    auto malformed_parts = parts;
    malformed_parts[0].option_count = 2;
    EXPECT_FALSE(catalog.publish(sets, malformed_parts, options, diagnostic));
    EXPECT_FALSE(diagnostic.empty());
    ASSERT_EQ(catalog.options(1, 5).size(), 1);
    EXPECT_EQ(catalog.options(1, 5)[0].option_id, 1);
}
