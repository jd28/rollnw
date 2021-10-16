#include "Creature.hpp"

namespace nw {

Creature::Creature(const GffStruct gff, SerializationProfile profile)
    : Object{ObjectType::creature, gff, profile}
{
}

} // namespace nw
