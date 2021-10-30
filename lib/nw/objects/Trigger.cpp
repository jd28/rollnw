#include "Trigger.hpp"

namespace nw {

Trigger::Trigger(const GffStruct gff, SerializationProfile profile)
    : Object{ObjectType::trigger, gff, profile}
    , trap{gff, profile}
{
    gff.get_to("Cursor", cursor);
    gff.get_to("HighlightHeight", highlight_height);
    gff.get_to("LinkedTo", linked_to);
    gff.get_to("LinkedToFlags", linked_to_flags);
    gff.get_to("LoadScreenID", loadscreen);
    gff.get_to("OnClick", on_click);
    gff.get_to("OnDisarm", on_disarm);
    gff.get_to("OnTrapTriggered", on_trap_triggered);
    gff.get_to("PortraitId", portrait);
    gff.get_to("ScriptHeartbeat", on_heartbeat);
    gff.get_to("ScriptOnEnter", on_enter);
    gff.get_to("ScriptOnExit", on_exit);
    gff.get_to("ScriptUserDefine", on_user_defined);

    if (profile == SerializationProfile::instance) {
        size_t sz = gff["Geometry"].size();
        geometery.reserve(sz);
        for (size_t i = 0; i < sz; ++i) {
            glm::vec3 v;
            gff["Geometry"][i].get_to("PointX", v[0]);
            gff["Geometry"][i].get_to("PointY", v[1]);
            gff["Geometry"][i].get_to("PointZ", v[2]);
            geometery.push_back(v);
        }
    }
}

} // namespace nw
