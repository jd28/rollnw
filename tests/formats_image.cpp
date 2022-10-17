#include <catch2/catch_all.hpp>

#include <nw/formats/Image.hpp>
#include <nw/log.hpp>

#include <filesystem>

TEST_CASE("Parse Bioware DDS", "[formats]")
{
    nw::Image dds{"test_data/user/development/bioRGBA.dds"};
    REQUIRE(dds.valid());
#ifndef _WIN32
    CHECK(dds.write_to("tmp/bioRGBA.dds"));
    CHECK(dds.write_to("tmp/bioRGBA.png"));
    CHECK(dds.write_to("tmp/bioRGBA.tga"));
#endif
}

TEST_CASE("Parse Standard DDS", "[formats]")
{
    nw::Image dds{"test_data/user/development/dxtRBG.dds"};
    REQUIRE(dds.valid());
#ifndef _WIN32
    CHECK(dds.write_to("tmp/dxtRBG.dds"));
    CHECK(dds.write_to("tmp/dxtRBG.png"));
    CHECK(dds.write_to("tmp/dxtRBG.tga"));
#endif
}

TEST_CASE("Parse TGA", "[formats]")
{
    nw::Image tga{"test_data/user/development/qfpp_001_L.tga"};
    REQUIRE(tga.valid());
#ifndef _WIN32
    CHECK(tga.write_to("tmp/qfpp_001_L.dds"));
    CHECK(tga.write_to("tmp/qfpp_001_L.png"));
    CHECK(tga.write_to("tmp/qfpp_001_L.tga"));
#endif
}
