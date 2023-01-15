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

/// Rolls a set of dice
/// @param roll Dice to roll
/// @param multiplier Roll dice n times
int roll_dice(DiceRoll roll, int multiplier = 1);

/// Rolls a set exploding of dice
/// @param dice Dice to roll
/// @param on Value to explode on, default is the sides of the dice
/// @param limit Limit of the number of explosions, default limit is 20
int roll_dice_explode(DiceRoll dice, int on = 0, int limit = 0);

} // namespace nw
