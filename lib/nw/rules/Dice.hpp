#pragma once

namespace nw {

struct DiceRoll {
    int dice = 0;
    int sides = 0;
    int bonus = 0;

    operator bool() { return (dice > 0 && sides > 0) || bonus > 0; }
};

} // namespace nw
