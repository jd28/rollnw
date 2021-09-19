#include <catch2/catch.hpp>

#include <nw/formats/Ini.hpp>

TEST_CASE("Parse INI", "[formats]")
{
    nw::Ini i{"test_data/nwnplayer.ini"};
    REQUIRE(i.is_valid());
    int server_down_timer = 0;
    REQUIRE(i.get_to("Server Options/ServerDownTimer", server_down_timer));
    REQUIRE(i.get<int>("Server Options/CD Banned Behavior"));

    REQUIRE(server_down_timer == 180);
}
