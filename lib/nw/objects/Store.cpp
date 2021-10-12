#include "Store.hpp"

namespace nw {

Store::Store(const GffStruct gff, SerializationProfile profile)
    : Object{gff, profile}
{
}

} // namespace nw
