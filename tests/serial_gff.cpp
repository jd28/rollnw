#include <catch2/catch.hpp>

#include <nw/log.hpp>
#include <nw/serialization/conversions.hpp>

#include <fstream>

TEST_CASE("GffInputArchive Validation", "[serialization]")
{
    nw::GffInputArchive g("test_data/nw_chicken.utc");
    REQUIRE(g.valid());
    auto top = g.toplevel();
    REQUIRE(top.size() > 0);

    REQUIRE(g.toplevel().has_field("TemplateResRef"));

    auto field = g.toplevel()["TemplateResRef"];
    REQUIRE(field.valid());
    REQUIRE(field.name() == "TemplateResRef");
    REQUIRE(field.type() == nw::SerializationType::RESREF);

    nw::Resref r;
    REQUIRE(field.get_to(r));
    REQUIRE(r.view() == "nw_chicken");

    REQUIRE(top[0].valid());
    REQUIRE(top[0].name() == "Deity");
}

TEST_CASE("GffInputArchive Lists", "[serialization]")
{
    nw::GffInputArchive g("test_data/nw_chicken.utc");
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

TEST_CASE("GffInputArchive Conversion", "[serialization]")
{
    nw::GffInputArchive g("test_data/nw_chicken.utc");
    REQUIRE(g.valid());
    auto j = nw::gff_to_json(g);
    std::ofstream f{"tmp/nw_chicken.utc.json", std::ios_base::binary};
    f << std::setw(4) << j;
}

TEST_CASE("GffInputArchive Object Poisoning", "[serialization]")
{
    nw::GffInputArchive g("test_data/nw_chicken.utc");
    REQUIRE(g.valid());
    auto top = g.toplevel();
    int32_t nonextant;
    REQUIRE_FALSE(top["thisdoesn't exist"][2000]["Or this"][1000000000]["Maybe this?"].get_to(nonextant));
}
