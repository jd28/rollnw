#include <gtest/gtest.h>

#include <nw/formats/Ini.hpp>

TEST(Ini, Parse)
{
    nw::Ini i{"test_data/user/nwnplayer.ini"};
    EXPECT_TRUE(i.valid());
    int server_down_timer = 0;
    EXPECT_TRUE(i.get_to("Server Options/ServerDownTimer", server_down_timer));
    EXPECT_TRUE(i.get<int>("Server Options/CD Banned Behavior"));

    EXPECT_EQ(server_down_timer, 180);
}
