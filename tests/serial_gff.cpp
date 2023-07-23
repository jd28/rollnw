#include <gtest/gtest.h>

#include <nw/log.hpp>
#include <nw/serialization/Archives.hpp>

#include <nlohmann/json.hpp>

#include <fstream>

using namespace std::literals;

TEST(Gff, Validation)
{
    nw::Gff g("test_data/user/development/nw_chicken.utc");
    EXPECT_TRUE(g.valid());
    auto top = g.toplevel();
    EXPECT_TRUE(top.size() > 0);

    EXPECT_TRUE(g.toplevel().has_field("TemplateResRef"));

    auto field = g.toplevel()["TemplateResRef"];
    EXPECT_TRUE(field.valid());
    EXPECT_EQ(field.name(), "TemplateResRef");
    EXPECT_EQ(field.type(), nw::SerializationType::resref);

    nw::Resref r;
    EXPECT_TRUE(field.get_to(r));
    EXPECT_EQ(r.view(), "nw_chicken");

    EXPECT_TRUE(top[0].valid());
    EXPECT_EQ(top[0].name(), "Deity");
}

TEST(Gff, Lists)
{
    nw::Gff g("test_data/user/development/nw_chicken.utc");
    EXPECT_TRUE(g.valid());
    auto top = g.toplevel();
    auto skills = top["SkillList"];
    EXPECT_TRUE(skills.valid());
    EXPECT_TRUE(skills.size() > 0);
    auto skillrank = skills[0];
    uint8_t val;
    EXPECT_TRUE(skillrank.valid());
    EXPECT_TRUE(skillrank["Rank"].get_to(val));
    EXPECT_EQ(val, 0);
    EXPECT_TRUE(skillrank["Rank"].get<uint8_t>());
    EXPECT_EQ(*skillrank["Rank"].get<uint8_t>(), 0);
    EXPECT_TRUE(top["ClassList"].valid());
    EXPECT_EQ(top["ClassList"].name(), "ClassList");
    EXPECT_EQ(top["ClassList"].size(), 1);
    EXPECT_TRUE(top["ClassList"][0].valid());
    EXPECT_TRUE(top["ClassList"][0]["Class"].valid());
    EXPECT_EQ(top["ClassList"][0]["Class"].name(), "Class");
    int32_t i32;
    EXPECT_TRUE(top["ClassList"][0]["Class"].get_to(i32));
    EXPECT_EQ(i32, 12);
    EXPECT_EQ(*top["ClassList"][0]["Class"].get<int32_t>(), 12);
    EXPECT_EQ(*top["ClassList"][0]["Class"].get<int64_t>(), 12);
}

TEST(Gff, IntegerPromotion)
{
    nw::Gff g("test_data/user/development/nw_chicken.utc");
    EXPECT_TRUE(g.valid());
    auto top = g.toplevel();
    EXPECT_EQ(*top["Int"].get<uint8_t>(), 3);
    EXPECT_FALSE(top["Int"].get<int8_t>());
    EXPECT_FALSE(top["Int"].get<int32_t>());
    EXPECT_EQ(*top["Int"].get<uint64_t>(), 3);
    EXPECT_EQ(*top["Int"].get<uint16_t>(), 3);
    EXPECT_TRUE(*top["Int"].get<bool>());
    EXPECT_FALSE(top["ClassList"][0]["Class"].get<uint8_t>());
    EXPECT_FALSE(top["ClassList"][0]["Class"].get<uint32_t>());
    EXPECT_FALSE(top["ClassList"][0]["Class"].get<int16_t>());
    EXPECT_TRUE(top["ClassList"][0]["Class"].get<int32_t>());
    EXPECT_TRUE(top["ClassList"][0]["Class"].get<int64_t>());
}

TEST(Gff, GffJsonConversion)
{
    nw::Gff g("test_data/user/development/nw_chicken.utc");
    EXPECT_TRUE(g.valid());
    auto j = nw::gff_to_gffjson(g);
    std::ofstream f{"tmp/nw_chicken.utc.json", std::ios_base::binary};
    f << std::setw(4) << j;
}

TEST(Gff, ObjectPoisoning)
{
    nw::Gff g("test_data/user/development/nw_chicken.utc");
    EXPECT_TRUE(g.valid());
    auto top = g.toplevel();
    int32_t nonextant;
    EXPECT_FALSE(top["thisdoesn't exist"][2000]["Or this"][1000000000]["Maybe this?"].get_to(nonextant));
}

TEST(GffBuilder, Construction)
{
    nw::GffBuilder oa("UTC");
    EXPECT_EQ(std::memcmp(oa.header.type, "UTC ", 4), 0);
    EXPECT_EQ(std::memcmp(oa.header.version, "V3.2", 4), 0);
}

TEST(GffBuilder, Labels)
{
    nw::GffBuilder oa("UTC");
    auto idx1 = oa.add_label("ThisIsTest1");
    oa.add_label("ThisIsTest2");
    oa.add_label("ThisIsTest3");
    auto idx4 = oa.add_label("ThisIsTest4");
    EXPECT_EQ(idx4, 3);
    auto idx1_2 = oa.add_label("ThisIsTest1");
    EXPECT_EQ(oa.labels.size(), 4);
    EXPECT_EQ(idx1, idx1_2);

    std::string s{"hello world"};
    int32_t i = 0;
    oa.top.add_field("temp", s)
        .add_field("another", i);

    auto& skills = oa.top.add_list("SkillList");
    for (int j = 0; j < 10; ++j) {
        skills.push_back(0x3432).add_field("Rank", j);
    }

    EXPECT_EQ(skills.size(), 10);
}

TEST(Gff, ListsNested)
{
    uint32_t temp = 0;
    nw::GffBuilder out{"GFF"};
    auto& list1 = out.top.add_list("list1");
    auto& str1 = list1.push_back(1).add_field("test1", temp);
    str1.add_field("hellow", 1);
    str1.add_field("this", "dfs"s);

    auto& list2 = str1.add_list("list2");
    list2.push_back(2).add_field("test2", temp);
    auto& list3 = out.top.add_list("list3");
    list3.push_back(3).add_field("test3", temp);

    out.build();
    EXPECT_EQ(out.struct_entries[0].field_count, 2u);
    EXPECT_EQ(out.struct_entries.size(), 4u);

    out.write_to("tmp/test.gff");

    nw::Gff in{"tmp/test.gff"};
    auto j = nw::gff_to_gffjson(in);
    std::ofstream f{"tmp/test.gffjson"};
    f << std::setw(4) << j;
}
