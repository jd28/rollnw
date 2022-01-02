#include "LevelStats.hpp"

#include "../../log.hpp"

#include <nlohmann/json.hpp>

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

bool LevelStats::from_json(const nlohmann::json& archive)
{
    try {
        for (const auto& cls : archive) {
            Class c;
            cls.at("class").get_to(c.id);
            cls.at("level").get_to(c.level);
            c.spells.from_json(cls.at("spellbook"));
            classes.push_back(std::move(c));
        }
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "LevelStats: json exception: {}", e.what());
        return false;
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

nlohmann::json LevelStats::to_json() const
{
    nlohmann::json j = nlohmann::json::array();
    for (const auto& cls : classes) {
        j.push_back({
            {"class", cls.id},
            {"level", cls.level},
            {"spellbook", cls.spells.to_json()},
        });
    }

    return j;
}

} // namespace nw
