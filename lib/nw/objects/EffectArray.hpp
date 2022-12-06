#pragma once

#include "../rules/Effect.hpp"
#include "ObjectHandle.hpp"

namespace nw {

struct EffectArray {
    using storage = std::vector<EffectHandle>;

    using iterator = storage::iterator;
    using const_iterator = storage::const_iterator;

    /// Adds an effect
    bool add(Effect* effect);

    iterator begin();
    const_iterator begin() const;

    iterator end();
    const_iterator end() const;

    /// Removes a range of effects
    void erase(iterator first, iterator last);

    /// Removes an effect
    bool remove(Effect* effect);

    /// Gets the number of applied effects
    size_t size() const noexcept;

private:
    storage effects_;
};

} // namespace nw
