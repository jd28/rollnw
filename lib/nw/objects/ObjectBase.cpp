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

void ObjectBase::add_visual_transform(VisualTransform value)
{
    // The game is using a std::map, I think. I have no clue why.. yet.
    if (value == VisualTransform{}
        || std::find(visual_transform_.begin(), visual_transform_.end(), value) != visual_transform_.end()) {
        return;
    }
    visual_transform_.push_back(value);
}

EffectArray& ObjectBase::effects() { return effects_; }
const EffectArray& ObjectBase::effects() const { return effects_; }

InternedString ObjectBase::tag() const { return {}; }

} // namespace nw
