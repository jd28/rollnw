#include <catch2/catch.hpp>

#include <nw/formats/Gff.hpp>
#include <nw/formats/gff_conversions.hpp>
#include <nw/log.hpp>

#include <fstream>

TEST_CASE("Gff Validation", "[formats]")
{
    nw::Gff g("test_data/nw_chicken.utc");
    REQUIRE(g.valid());
    REQUIRE(g.toplevel().size() > 0);

    auto field = g.toplevel()["TemplateResRef"];
    REQUIRE(field.valid());
    REQUIRE(field.name() == "TemplateResRef");
    REQUIRE(field.type() == nw::GffType::RESREF);

    nw::Resref r;
    REQUIRE(field.get_to(r));
    REQUIRE(r.view() == "nw_chicken");

    REQUIRE(g[0].valid());
    REQUIRE(g[0].name() == "Deity");
}

TEST_CASE("Gff Lists", "[formats]")
{
    nw::Gff g("test_data/nw_chicken.utc");
    REQUIRE(g.valid());
    auto skills = g["SkillList"];
    REQUIRE(skills.valid());
    REQUIRE(skills.size() > 0);
    auto skillrank = skills[0];
    uint8_t val;
    REQUIRE(skillrank.valid());
    REQUIRE(skillrank["Rank"].get_to(val));
    REQUIRE(val == 0);
    REQUIRE(skillrank["Rank"].get<uint8_t>());
    REQUIRE(*skillrank["Rank"].get<uint8_t>() == 0);
    REQUIRE(g["ClassList"].valid());
    REQUIRE(g["ClassList"].name() == "ClassList");
    REQUIRE(g["ClassList"].size() == 1);
    REQUIRE(g["ClassList"][0].valid());
    REQUIRE(g["ClassList"][0]["Class"].valid());
    REQUIRE(g["ClassList"][0]["Class"].name() == "Class");
    int32_t i32;
    REQUIRE(g["ClassList"][0]["Class"].get_to(i32));
    REQUIRE(i32 == 12);
    REQUIRE(*g["ClassList"][0]["Class"].get<int32_t>() == 12);
}

TEST_CASE("Gff Conversion", "[formats]")
{
    nw::Gff g("test_data/nw_chicken.utc");
    REQUIRE(g.valid());
    auto j = nw::gff_to_json(g);
    std::ofstream f{"tmp/nw_chicken.utc.json", std::ios_base::binary};
    f << std::setw(4) << j;
}
