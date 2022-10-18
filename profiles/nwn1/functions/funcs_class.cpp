#include "funcs_class.hpp"

#include "../constants.hpp"

#include <nw/components/Creature.hpp>
#include <nw/rules/Class.hpp>

namespace nwn1 {

std::pair<bool, int> can_use_monk_abilities(const nw::Creature* obj)
{
    if (class_type_monk == nw::Class::invalid()) {
        return {false, 0};
    }
    auto level = obj->levels.level_by_class(class_type_monk);
    if (level == 0) {
        return {false, 0};
    }
    return {true, level};
}

} // namespace nwn1
