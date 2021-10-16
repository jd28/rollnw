#include "Encounter.hpp"

namespace nw {

Encounter::Encounter(const GffStruct gff, SerializationProfile profile)
    : Object{ObjectType::encounter, gff, profile}
{
}

} // namespace nw
