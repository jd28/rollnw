#pragma once

#include "../rules/Effect.hpp"
#include "ObjectHandle.hpp"

namespace nw {

struct EffectArray {
    using storage = std::vector<EffectHandle>;

    using iterator = storage::iterator;
    using const_iterator = storage::const_iterator;

    iterator begin();
    const_iterator begin() const;

    /// Adds an effect
    bool add(Effect* effect);

    iterator end();
    const_iterator end() const;

    /// Removes an effect
    bool remove(Effect* effect);

    /// Removes effects by creator
    int remove(ObjectHandle obj);

    /// Gets the number of applied effects
    size_t size() const noexcept;

private:
    storage effects_;
};

} // namespace nw
