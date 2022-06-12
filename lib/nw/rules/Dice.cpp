#include "Dice.hpp"

#include <random>

namespace nw {

bool operator==(const DiceRoll& lhs, const DiceRoll& rhs)
{
    return std::tie(lhs.dice, lhs.sides, lhs.bonus) == std::tie(rhs.dice, rhs.sides, rhs.bonus);
}

bool operator<(const DiceRoll& lhs, const DiceRoll& rhs)
{
    return std::tie(lhs.dice, lhs.sides, lhs.bonus) < std::tie(rhs.dice, rhs.sides, rhs.bonus);
}

int roll_dice(DiceRoll roll)
{
    static thread_local std::mt19937 prng(std::random_device{}());

    int result = roll.bonus;
    if (roll.sides > 0) {
        std::uniform_int_distribution<int> dX(1, roll.sides);
        for (int i = 0; i < roll.dice; ++i) {
            result += dX(prng);
        }
    }
    return result;
}

} // namespace nw
