#include "Creature.hpp"

namespace nw {

Creature::Creature(const GffStruct gff, SerializationProfile profile)
    : Object(gff, profile)
    , loc{gff, profile}
{
}

} // namespace nw
