#pragma once

#include "functions.hpp"

#include <nw/objects/ObjectBase.hpp>
#include <nw/rules/system.hpp>

namespace nwn1 {

bool match(const nw::Qualifier& qual, const nw::ObjectBase* obj);
nw::RuleValue selector(const nw::Selector& selector, const nw::ObjectBase* obj);

} // namespace nwn1
