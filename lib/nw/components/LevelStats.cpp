#include "LevelStats.hpp"

#include "../log.hpp"

#include <nlohmann/json.hpp>

namespace nw {

bool LevelStats::from_gff(const GffStruct& archive)
{
    size_t sz = archive["ClassList"].size();
    entries.reserve(sz);
    for (size_t i = 0; i < sz; ++i) {
        ClassEntry c;
        int32_t temp;
        if (archive["ClassList"][i].get_to("Class", temp)) {
            c.id = Class::make(temp);
        }
        archive["ClassList"][i].get_to("ClassLevel", c.level);
        c.spells.from_gff(archive["ClassList"][i]);
        entries.push_back(std::move(c));
    }

    return true;
}

bool LevelStats::from_json(const nlohmann::json& archive)
{
    try {
        for (const auto& cls : archive) {
            ClassEntry c;
            cls.at("class").get_to(c.id);
            cls.at("level").get_to(c.level);
            c.spells.from_json(cls.at("spellbook"));
            entries.push_back(std::move(c));
        }
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "LevelStats: json exception: {}", e.what());
        return false;
    }

    return true;
}

bool LevelStats::to_gff(GffBuilderStruct& archive) const
{
    auto& list = archive.add_list("ClassList");
    for (const auto& cls : entries) {
        cls.spells.to_gff(list.push_back(3)
                              .add_field("Class", *cls.id)
                              .add_field("ClassLevel", cls.level));
    }

    return true;
}

nlohmann::json LevelStats::to_json() const
{
    nlohmann::json j = nlohmann::json::array();
    for (const auto& cls : entries) {
        j.push_back({
            {"class", cls.id},
            {"level", cls.level},
            {"spellbook", cls.spells.to_json()},
        });
    }

    return j;
}

int LevelStats::level() const noexcept
{
    int result = 0;
    for (const auto& cl : entries) {
        result += cl.level;
    }
    return result;
}

int LevelStats::level_by_class(Class id) const noexcept
{
    for (const auto& cl : entries) {
        if (id == cl.id) {
            return cl.level;
        }
    }
    return 0;
}

} // namespace nw
