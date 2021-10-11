#include "Store.hpp"

namespace nw {

Store::Store(const GffStruct gff, SerializationProfile profile)
    : Object{gff, profile}
{
    gff.get_to("ResRef", resref); // Store blueprints do their own thing for some reason.
}

} // namespace nw
