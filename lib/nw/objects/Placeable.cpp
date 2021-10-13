#include "Placeable.hpp"

namespace nw {

Placeable::Placeable(const GffStruct gff, SerializationProfile profile)
    : SituatedObject{gff, profile}
{
    uint8_t u8;
    gff.get_to("AnimationState", u8);
    anim_state = static_cast<AnimationState>(u8);

    gff.get_to("BodyBag", bodybag);
    gff.get_to("HasInventory", has_inventory);
    gff.get_to("OnInvDisturbed", on_inv_disturbed);
    gff.get_to("OnUsed", on_used);
    gff.get_to("Static", static_);
    gff.get_to("Useable", useable);

    if (has_inventory) {
        inventory = Inventory{gff, profile};
    }
}

} // namespace nw
