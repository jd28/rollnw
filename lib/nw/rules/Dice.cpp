#include "Dice.hpp"

#include <random>
#include <tuple>

namespace nw {

static thread_local std::mt19937 prng(std::random_device{}());

bool operator==(const DiceRoll& lhs, const DiceRoll& rhs)
{
    return std::tie(lhs.dice, lhs.sides, lhs.bonus) == std::tie(rhs.dice, rhs.sides, rhs.bonus);
}

bool operator<(const DiceRoll& lhs, const DiceRoll& rhs)
{
    return std::tie(lhs.dice, lhs.sides, lhs.bonus) < std::tie(rhs.dice, rhs.sides, rhs.bonus);
}

int roll_dice(DiceRoll roll, int multiplier)
{
    if (!roll) { return 0; }
    if (multiplier <= 0) { multiplier = 1; }
    int result = 0;
    for (int i = 0; i < multiplier; ++i) {
        result += roll.bonus;
        if (roll.sides > 0) {
            std::uniform_int_distribution<int> dX(1, roll.sides);
            for (int j = 0; j < roll.dice; ++j) {
                result += dX(prng);
            }
        }
    }
    return result;
}

int roll_dice_explode(DiceRoll dice, int on, int limit)
{
    int result = dice.bonus;
    int explode_on = on == 0 ? dice.sides : on;
    int stop_at = limit <= 0 ? 20 : limit;
    if (dice.sides) {
        std::uniform_int_distribution<int> dX(1, dice.sides);
        for (int i = 0; i < dice.dice; ++i) {
            int roll, j = 0;
            do {
                roll = dX(prng);
                result += roll;
                ++j;
            } while (roll >= explode_on && j <= stop_at); // <= cause first run is 1
        }
    }

    return result;
}

} // namespace nw
