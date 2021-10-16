#include "Door.hpp"

namespace nw {

Door::Door(const GffStruct gff, SerializationProfile profile)
    : SituatedObject{ObjectType::door, gff, profile}
{
    uint8_t u8;
    gff.get_to("AnimationState", u8);
    anim_state = static_cast<AnimationState>(u8);

    gff.get_to("GenericType", generic_type);
    gff.get_to("LinkedTo", linked_to);
    gff.get_to("LinkedToFlags", linked_to_flags);
    gff.get_to("LoadScreenID", loadscreen);
    gff.get_to("OnClick", on_click);
    gff.get_to("OnFailToOpen", on_open_failure);
}

} // namespace nw
