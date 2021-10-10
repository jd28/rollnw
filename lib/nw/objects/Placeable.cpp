#include "Placeable.hpp"

namespace nw {

Placeable::Placeable(const GffStruct gff, SerializationProfile profile)
    : SituatedObject{gff, profile}
{
}

} // namespace nw
