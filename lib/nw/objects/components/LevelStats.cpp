#include "LevelStats.hpp"

namespace nw {

bool LevelStats::from_gff(const GffInputArchiveStruct& archive)
{
    size_t sz = archive["ClassList"].size();
    classes.reserve(sz);
    for (size_t i = 0; i < sz; ++i) {
        Class c;
        archive["ClassList"][i].get_to("Class", c.id);
        archive["ClassList"][i].get_to("ClassLevel", c.level);
        c.spells.from_gff(archive["ClassList"][i]);
        classes.push_back(std::move(c));
    }

    return true;
}

bool LevelStats::to_gff(GffOutputArchiveStruct& archive) const
{
    auto& list = archive.add_list("ClassList");
    for (const auto& cls : classes) {
        cls.spells.to_gff(list.push_back(3, {
                                                {"Class", cls.id},
                                                {"ClassLevel", cls.level},
                                            }));
    }

    return true;
}

} // namespace nw
