#pragma once

namespace nw {

/// A dice roll
struct DiceRoll {
    operator bool() { return (dice > 0 && sides > 0) || bonus > 0; }
    int dice = 0;  ///< Number of dice to roll
    int sides = 0; ///< Number of sides on the dice
    int bonus = 0; ///< Additional bonus
};

bool operator==(const DiceRoll& lhs, const DiceRoll& rhs);
bool operator<(const DiceRoll& lhs, const DiceRoll& rhs);

int roll_dice(DiceRoll roll);

} // namespace nw
