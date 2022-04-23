#include "Rules.hpp"

#include "../formats/TwoDA.hpp"
#include "../util/string.hpp"
#include "Kernel.hpp"

namespace nw::kernel {

Rules::~Rules()
{
    clear();
}

void Rules::clear()
{
    skill_info_.clear();
    constants_.clear();
}

bool Rules::initialize()
{
    // Stub
    return true;
}

bool Rules::load(bool fail_hard)
{
#define TDA_GET_STRREF(name, row, column)           \
    do {                                            \
        if (tda.get_to(row, column, temp_int)) {    \
            name = static_cast<uint32_t>(temp_int); \
        }                                           \
    } while (0)

#define TDA_GET_FLOAT(name, row, column)                                \
    do {                                                                \
        if (tda.get_to(row, column, temp_float)) { name = temp_float; } \
    } while (0)

#define TDA_GET_INT(name, row, column)                              \
    do {                                                            \
        if (tda.get_to(row, column, temp_int)) { name = temp_int; } \
    } while (0)

#define TDA_GET_BOOL(name, row, column)                               \
    do {                                                              \
        if (tda.get_to(row, column, temp_int)) { name = !!temp_int; } \
    } while (0)

#define TDA_GET_RES(name, row, column, type)                                              \
    do {                                                                                  \
        if (tda.get_to(row, column, temp_string)) { name = Resource{temp_string, type}; } \
    } while (0)

#define TDA_GET_CONSTANT(name, row, column)                                                                         \
    do {                                                                                                            \
        if (tda.get_to(row, column, temp_string)) { name = register_constant(temp_string, static_cast<int>(row)); } \
    } while (0)

    LOG_F(INFO, "kernel: initializing rules system");

    TwoDA tda;
    std::string temp_string;
    temp_string.reserve(100);

    // Abilities - Going to have to cheat on this for now..
    register_constant("ABILITY_STRENGTH", 0);
    register_constant("ABILITY_DEXTERITY", 1);
    register_constant("ABILITY_CONSTITUTION", 2);
    register_constant("ABILITY_INTELLIGENCE", 3);
    register_constant("ABILITY_WISDOM", 4);
    register_constant("ABILITY_CHARISMA", 5);

    // Skills
    tda = TwoDA{resman().demand({"skills"sv, ResourceType::twoda})};
    if (tda.is_valid()) {
        skill_info_.reserve(tda.rows());
        for (size_t i = 0; i < tda.rows(); ++i) {
            Skill info;
            int temp_int = 0;
            // float temp_float = 0.0f;

            if (tda.get_to(0, "label", temp_string)) {
                TDA_GET_STRREF(info.name, i, "Name");
                TDA_GET_STRREF(info.description, i, "Description");
                TDA_GET_RES(info.icon, i, "Icon", ResourceType::texture);
                TDA_GET_BOOL(info.untrained, i, "Untrained");

                if (tda.get_to(i, "KeyAbility", temp_string)) {
                    if (string::icmp("str", temp_string)) {
                        info.ability = get_constant("ABILITY_STRENGTH");
                    } else if (string::icmp("dex", temp_string)) {
                        info.ability = get_constant("ABILITY_DEXTERITY");
                    } else if (string::icmp("con", temp_string)) {
                        info.ability = get_constant("ABILITY_CONSTITUTION");
                    } else if (string::icmp("int", temp_string)) {
                        info.ability = get_constant("ABILITY_INTELLIGENCE");
                    } else if (string::icmp("wis", temp_string)) {
                        info.ability = get_constant("ABILITY_WISDOM");
                    } else if (string::icmp("cha", temp_string)) {
                        info.ability = get_constant("ABILITY_CHARISMA");
                    } else {
                        // Try to get whatever is there..
                        info.ability = get_constant(temp_string);
                    }
                }
                // KeyAbility
                TDA_GET_BOOL(info.armor_check_penalty, i, "ArmorCheckPenalty");
                TDA_GET_BOOL(info.all_can_use, i, "AllClassesCanUse");
                TDA_GET_CONSTANT(info.constant, i, "Constant");
                TDA_GET_BOOL(info.hostile, i, "HostileSkill");
            }

            skill_info_.push_back(info);
        }
    } else if (fail_hard) {
        throw std::runtime_error("rules: failed to load 'skills.2da'");
    } else {
        LOG_F(WARNING, "rules: failed to load 'skills.2da'");
    }

    return true;

#undef TDA_GET_STRREF
#undef TDA_GET_FLOAT
#undef TDA_GET_INT
#undef TDA_GET_BOOL
#undef TDA_GET_RES
}

bool Rules::reload(bool fail_hard)
{
    clear();
    return load(fail_hard);
}

Constant Rules::get_constant(std::string_view name) const
{
    absl::string_view v{name.data(), name.size()};
    auto it = constants_.find(v);
    if (it != std::end(constants_)) {
        return {it->first, it->second};
    }

    return {};
}

Constant Rules::register_constant(std::string_view name, int value)
{
    absl::string_view v{name.data(), name.size()};
    auto it = constants_.find(v);
    if (it != std::end(constants_)) {
        return {it->first, it->second};
    }

    auto intstr = kernel::strings().intern(name);
    constants_.emplace(intstr, value);

    return {intstr, value};
}

const Skill& Rules::skill(size_t index)
{
    return skill_info_.at(index);
}

size_t Rules::skill_count() const noexcept
{
    return skill_info_.size();
}

} // namespace nw::kernel
