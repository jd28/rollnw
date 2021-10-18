#include "Appearance.hpp"

namespace nw {

Appearance::Appearance(const GffStruct gff)
{

    if (!gff.get_to("Tail_New", tail, false)) {
        uint8_t old = 0;
        gff.get_to("Tail", old, false);
        tail = old;
    }
    if (!gff.get_to("Wings_New", wings, false)) {
        uint8_t old = 0;
        gff.get_to("Wings", old, false);
        wings = old;
    }

    gff.get_to("Appearance_Type", id);

    gff.get_to("Color_Hair", hair, false);
    gff.get_to("Color_Skin", skin, false);
    gff.get_to("Color_Tattoo1", tattoo1, false);
    gff.get_to("Color_Tattoo2", tattoo2, false);
    gff.get_to("Phenotype", phenotype);
}

} // namespace nw
