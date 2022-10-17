#include <catch2/catch_all.hpp>

#include <nw/log.hpp>
#include <nw/serialization/Archives.hpp>

#include <nlohmann/json.hpp>

#include <fstream>

using namespace std::literals;

TEST_CASE("Gff Validation", "[serialization]")
{
    nw::Gff g("test_data/user/development/nw_chicken.utc");
    REQUIRE(g.valid());
    auto top = g.toplevel();
    REQUIRE(top.size() > 0);

    REQUIRE(g.toplevel().has_field("TemplateResRef"));

    auto field = g.toplevel()["TemplateResRef"];
    REQUIRE(field.valid());
    REQUIRE(field.name() == "TemplateResRef");
    REQUIRE(field.type() == nw::SerializationType::resref);

    nw::Resref r;
    REQUIRE(field.get_to(r));
    REQUIRE(r.view() == "nw_chicken");

    REQUIRE(top[0].valid());
    REQUIRE(top[0].name() == "Deity");
}

TEST_CASE("Gff Lists", "[serialization]")
{
    nw::Gff g("test_data/user/development/nw_chicken.utc");
    REQUIRE(g.valid());
    auto top = g.toplevel();
    auto skills = top["SkillList"];
    REQUIRE(skills.valid());
    REQUIRE(skills.size() > 0);
    auto skillrank = skills[0];
    uint8_t val;
    REQUIRE(skillrank.valid());
    REQUIRE(skillrank["Rank"].get_to(val));
    REQUIRE(val == 0);
    REQUIRE(skillrank["Rank"].get<uint8_t>());
    REQUIRE(*skillrank["Rank"].get<uint8_t>() == 0);
    REQUIRE(top["ClassList"].valid());
    REQUIRE(top["ClassList"].name() == "ClassList");
    REQUIRE(top["ClassList"].size() == 1);
    REQUIRE(top["ClassList"][0].valid());
    REQUIRE(top["ClassList"][0]["Class"].valid());
    REQUIRE(top["ClassList"][0]["Class"].name() == "Class");
    int32_t i32;
    REQUIRE(top["ClassList"][0]["Class"].get_to(i32));
    REQUIRE(i32 == 12);
    REQUIRE(*top["ClassList"][0]["Class"].get<int32_t>() == 12);
}

TEST_CASE("Gff Conversion", "[serialization]")
{
    nw::Gff g("test_data/user/development/nw_chicken.utc");
    REQUIRE(g.valid());
    auto j = nw::gff_to_gffjson(g);
    std::ofstream f{"tmp/nw_chicken.utc.json", std::ios_base::binary};
    f << std::setw(4) << j;
}

TEST_CASE("Gff Object Poisoning", "[serialization]")
{
    nw::Gff g("test_data/user/development/nw_chicken.utc");
    REQUIRE(g.valid());
    auto top = g.toplevel();
    int32_t nonextant;
    REQUIRE_FALSE(top["thisdoesn't exist"][2000]["Or this"][1000000000]["Maybe this?"].get_to(nonextant));
}

TEST_CASE("GffBuilder construction", "[serialization]")
{
    nw::GffBuilder oa("UTC");
    REQUIRE(std::memcmp(oa.header.type, "UTC ", 4) == 0);
    REQUIRE(std::memcmp(oa.header.version, "V3.2", 4) == 0);
}

TEST_CASE("GffBuilder add_labels", "[serialization]")
{
    nw::GffBuilder oa("UTC");
    auto idx1 = oa.add_label("ThisIsTest1");
    oa.add_label("ThisIsTest2");
    oa.add_label("ThisIsTest3");
    auto idx4 = oa.add_label("ThisIsTest4");
    REQUIRE(idx4 == 3);
    auto idx1_2 = oa.add_label("ThisIsTest1");
    REQUIRE(oa.labels.size() == 4);
    REQUIRE(idx1 == idx1_2);

    std::string s{"hello world"};
    int32_t i = 0;
    oa.top.add_field("temp", s)
        .add_field("another", i);

    auto& skills = oa.top.add_list("SkillList");
    for (int j = 0; j < 10; ++j) {
        skills.push_back(0x3432).add_field("Rank", j);
    }

    REQUIRE(skills.size() == 10);
}

TEST_CASE("gff: nested lists")
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
    REQUIRE(out.struct_entries[0].field_count == 2);
    REQUIRE(out.struct_entries.size() == 4);

    out.write_to("tmp/test.gff");

    nw::Gff in{"tmp/test.gff"};
    auto j = nw::gff_to_gffjson(in);
    std::ofstream f{"tmp/test.gffjson"};
    f << std::setw(4) << j;
}
