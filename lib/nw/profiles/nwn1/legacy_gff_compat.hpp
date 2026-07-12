#pragma once

namespace nw {
struct GffBuilderStruct;
} // namespace nw

namespace nwn1 {

void export_creature_legacy_gff_fields(nw::GffBuilderStruct& archive);
void export_module_legacy_gff_fields(nw::GffBuilderStruct& archive);
void export_trigger_legacy_gff_fields(nw::GffBuilderStruct& archive);

} // namespace nwn1
