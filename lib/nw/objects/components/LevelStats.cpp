#include "LevelStats.hpp"

namespace nw {

LevelStats::LevelStats(const GffStruct gff)
{
    size_t sz = gff["ClassList"].size();
    classes.reserve(sz);
    for (size_t i = 0; i < sz; ++i) {
        Class c;
        gff["ClassList"][i].get_to("Class", c.id);
        gff["ClassList"][i].get_to("ClassLevel", c.level);
        c.spells = SpellBook(gff["ClassList"][i]);
        classes.push_back(std::move(c));
    }
}

} // namespace nw
