#pragma once

namespace nw {
struct ObjectBase;
}

namespace nw::smalls {
struct Runtime;
}

namespace nwn1 {

void populate_creature_propsets(nw::smalls::Runtime* rt, nw::ObjectBase* obj);
void populate_item_propsets(nw::smalls::Runtime* rt, nw::ObjectBase* obj);
void populate_door_propsets(nw::smalls::Runtime* rt, nw::ObjectBase* obj);
void populate_placeable_propsets(nw::smalls::Runtime* rt, nw::ObjectBase* obj);

} // namespace nwn1
