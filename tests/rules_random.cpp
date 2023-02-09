#include <gtest/gtest.h>

#include <nw/rules/Dice.hpp>

TEST(Dice, explode)
{
    nw::DiceRoll dr1{1, 6};
    auto roll1 = nw::roll_dice_explode(dr1, 6, 1);
    EXPECT_LE(roll1, 12);

    auto roll2 = nw::roll_dice_explode(dr1, 1, 2);
    EXPECT_LE(roll2, 18);
}
