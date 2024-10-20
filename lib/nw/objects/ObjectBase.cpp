#include "ObjectBase.hpp"

#include <nlohmann/json.hpp>

namespace nw {

// == ObjectBase ==============================================================
// ============================================================================

ObjectBase::ObjectBase(nw::MemoryResource* allocator)
    : allocator_(allocator)
    , effects_(allocator)
{
}

void ObjectBase::clear()
{
}

EffectArray& ObjectBase::effects() { return effects_; }
const EffectArray& ObjectBase::effects() const { return effects_; }

InternedString ObjectBase::tag() const { return {}; }

} // namespace nw
