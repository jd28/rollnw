#pragma once

#include "../objects/ObjectHandle.hpp"
#include "Spell.hpp"
#include "Versus.hpp"
#include "rule_type.hpp"

#include <absl/container/inlined_vector.h>

#include <compare>
#include <string>

namespace nw {

// == Effect ==================================================================
// ============================================================================

DECLARE_RULE_TYPE(EffectType);

struct EffectHandle;

enum struct EffectCategory {
    magical,
    extraordinary,
    supernatural,
    item,
    innate,
};

struct EffectID {
    uint32_t version = 0;
    uint32_t index = 0;
};

struct Effect {
    Effect();
    Effect(EffectType type_);

    /// Clears the effect such that it's as if default constructed
    void clear();

    /// Gets a floating point value
    float get_float(size_t index) const noexcept;

    /// Gets an integer point value
    int get_int(size_t index) const noexcept;

    /// Gets a string value
    std::string_view get_string(size_t index) const noexcept;

    /// Gets the effect's handle
    EffectHandle handle() noexcept;

    /// Gets the effect's ID
    EffectID id() const noexcept;

    /// Sets a floating point value
    void set_float(size_t index, float value);

    /// Sets effect's ID
    void set_id(EffectID id);

    /// Sets an integer point value
    void set_int(size_t index, int value);

    /// Sets a string value
    void set_string(size_t index, std::string value);

    /// Sets the versus value
    void set_versus(Versus vs);

    /// Gets the versus value
    const Versus& versus() const noexcept;

    EffectType type = EffectType::invalid();
    EffectCategory category = EffectCategory::magical;
    int subtype = -1;
    ObjectHandle creator;
    Spell spell_id = Spell::invalid();
    float duration = 0.0f;
    uint32_t expire_day = 0;
    uint32_t expire_time = 0;

private:
    EffectID id_;

    absl::InlinedVector<int, 4> integers_;
    absl::InlinedVector<float, 4> floats_;
    absl::InlinedVector<std::string, 4> strings_;
    Versus versus_;
};

struct EffectHandle {
    EffectType type = EffectType::invalid();
    int subtype = -1;
    ObjectHandle creator;
    Spell spell_id = Spell::invalid();
    EffectCategory category = EffectCategory::magical;
    Effect* effect = nullptr;

    bool operator==(const EffectHandle&) const = default;
    auto operator<=>(const EffectHandle&) const = default;
};

} // namespace nw
