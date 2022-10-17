#include <catch2/catch_all.hpp>

#include <nw/rules/Dice.hpp>

namespace fs = std::filesystem;

TEST_CASE("dice explode", "[rules]")
{
    nw::DiceRoll dr1{1, 6};
    auto roll1 = nw::roll_dice_explode(dr1, 6, 1);
    REQUIRE(roll1 <= 12);

    auto roll2 = nw::roll_dice_explode(dr1, 1, 2);
    REQUIRE(roll2 <= 18);
}
