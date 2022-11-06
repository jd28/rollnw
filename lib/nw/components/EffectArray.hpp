#pragma once

#include "../rules/Effect.hpp"
#include "ObjectHandle.hpp"

namespace nw {

struct EffectArray {
    /// Adds an effect
    bool add(Effect* effect);

    /// Removes an effect
    bool remove(Effect* effect);

    /// Removes effects by creator
    int remove(ObjectHandle obj);

    /// Gets the number of applied effects
    size_t size() const noexcept;

private:
    std::vector<EffectHandle> effects_;
};

} // namespace nw
