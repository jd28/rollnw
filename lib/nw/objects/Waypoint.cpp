#include "Waypoint.hpp"

namespace nw {
Waypoint::Waypoint(const GffStruct gff, SerializationProfile profile)
    : Object{gff, profile}
{
}

} // namespace nw
