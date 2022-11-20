#include "LevelStats.hpp"

#include "../log.hpp"

#include <nlohmann/json.hpp>

namespace nw {

bool LevelStats::from_gff(const GffStruct& archive)
{
    size_t sz = archive["ClassList"].size();
    if (sz >= max_classes) {
        LOG_F(ERROR, "level stats: attempting a creature with more than {} classes", max_classes);
        return false;
    }

    for (size_t i = 0; i < sz; ++i) {
        int32_t temp;
        if (archive["ClassList"][i].get_to("Class", temp)) {
            entries[i].id = Class::make(temp);
        }
        archive["ClassList"][i].get_to("ClassLevel", entries[i].level);
        entries[i].spells.from_gff(archive["ClassList"][i]);
    }

    return true;
}

bool LevelStats::from_json(const nlohmann::json& archive)
{
    try {
        if (archive.size() >= max_classes) {
            LOG_F(ERROR, "level stats: attempting a creature with more than {} classes", max_classes);
            return false;
        }

        size_t i = 0;
        for (const auto& cls : archive) {
            cls.at("class").get_to(entries[i].id);
            cls.at("level").get_to(entries[i].level);
            entries[i].spells.from_json(cls.at("spellbook"));
            ++i;
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
        if (cls.id == nw::Class::invalid()) { continue; }
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
        if (cls.id == nw::Class::invalid()) { continue; }
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

size_t LevelStats::position(Class id) const noexcept
{
    if (id == nw::Class::invalid()) { return npos; }

    for (size_t i = 0; i < max_classes; ++i) {
        if (entries[i].id == id) {
            return i;
        } else if (entries[i].id == nw::Class::invalid()) {
            break;
        }
    }

    return npos;
}

} // namespace nw
