#include <catch2/catch_all.hpp>

#include <nw/formats/Ini.hpp>

TEST_CASE("Parse INI", "[formats]")
{
    nw::Ini i{"test_data/user/nwnplayer.ini"};
    REQUIRE(i.valid());
    int server_down_timer = 0;
    REQUIRE(i.get_to("Server Options/ServerDownTimer", server_down_timer));
    REQUIRE(i.get<int>("Server Options/CD Banned Behavior"));

    REQUIRE(server_down_timer == 180);
}
