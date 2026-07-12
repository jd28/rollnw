#include "legacy_gff_compat.hpp"

#include "../../serialization/GffBuilder.hpp"

#include <cstdint>

namespace nwn1 {

void export_creature_legacy_gff_fields(nw::GffBuilderStruct& archive)
{
    archive.add_list("TemplateList");
}

void export_module_legacy_gff_fields(nw::GffBuilderStruct& archive)
{
    archive.add_list("Mod_CutSceneList");
    archive.add_list("Mod_Expan_List");
    archive.add_list("Mod_GVar_List");
}

void export_trigger_legacy_gff_fields(nw::GffBuilderStruct& archive)
{
    uint8_t zero = 0;
    nw::String empty;
    archive.add_field("AutoRemoveKey", zero)
        .add_field("KeyName", empty);
}

} // namespace nwn1
