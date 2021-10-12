#include "Waypoint.hpp"

namespace nw {
Waypoint::Waypoint(const GffStruct gff, SerializationProfile profile)
    : Object{gff, profile}
{
    gff.get_to("Appearance", appearance);
    gff.get_to("Description", description);
    gff.get_to("HasMapNote", has_map_note);
    gff.get_to("LinkedTo", linked_to);
    gff.get_to("MapNote", map_note);
    gff.get_to("MapNoteEnabled", map_note_enabled);
}

} // namespace nw
