#include "Trigger.hpp"

namespace nw {

Trigger::Trigger(const GffStruct gff, SerializationProfile profile)
    : Object{gff, profile}
    , trap{gff, profile}
{
}

} // namespace nw
