#pragma once

namespace nw {
struct Effect;
struct ObjectBase;
} // namespace nw

namespace nwn1 {

bool effect_haste_apply(nw::ObjectBase* obj, const nw::Effect*);
bool effect_haste_remove(nw::ObjectBase* obj, const nw::Effect*);

} // namespace nwn1
