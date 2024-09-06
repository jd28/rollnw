#include "Rules.hpp"

#include "../util/string.hpp"
#include "GameProfile.hpp"
#include "TwoDACache.hpp"

#include <cstddef>

namespace nw::kernel {

Rules::~Rules()
{
    clear();
}

void Rules::clear()
{
    modifiers.clear();
    baseitems.clear();
    classes.clear();
    feats.clear();
    races.clear();
    spells.clear();
    skills.clear();
    master_feats.clear();
    appearances.clear();
    phenotypes.clear();
    traps.clear();
    placeables.clear();
}

void Rules::initialize(ServiceInitTime time)
{
    if (time != ServiceInitTime::kernel_start && time != ServiceInitTime::module_post_load) {
        return;
    }

    LOG_F(INFO, "kernel: rules system initializing...");
    if (auto profile = services().profile()) {
        profile->load_rules();
    }
}

bool Rules::match(const Qualifier& qual, const ObjectBase* obj) const
{
    if (qual.type.idx() >= qualifiers_.size()) {
        return true;
    }
    auto fd = qualifiers_[qual.type.idx()];
    return fd(qual, obj);
}

bool Rules::meets_requirement(const Requirement& req, const ObjectBase* obj) const
{
    for (const auto& q : req.qualifiers) {
        if (req.conjunction) {
            if (!match(q, obj)) {
                return false;
            }
        } else if (match(q, obj)) {
            return true;
        }
    }
    return true;
}

void Rules::set_qualifier(ReqType type, bool (*qualifier)(const Qualifier&, const ObjectBase*))
{
    if (type.idx() >= qualifiers_.size()) {
        qualifiers_.resize(type.idx() + 1);
    }
    if (qualifier) { qualifiers_[type.idx()] = qualifier; };
}

} // namespace nw::kernel
