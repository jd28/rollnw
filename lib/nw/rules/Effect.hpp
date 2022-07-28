#pragma once

#include "system.hpp"
#include "type_traits.hpp"

#include <absl/container/inlined_vector.h>

#include <string>

namespace nw {

enum struct EffectType : int32_t {
    invalid = -1,
};
constexpr EffectType make_effect_type(int32_t index) { return static_cast<EffectType>(index); }

template <>
struct is_rule_type_base<EffectType> : std::true_type {
};

struct EffectHandle;

enum struct EffectCategory {
    magical,
    extraordinary,
    supernatural,
    item,
    innate,
};

struct Effect {
    static uint64_t next_id;

    Effect();

    float get_float(size_t index) const;
    int get_int(size_t index) const;
    std::string_view get_string(size_t index) const;

    EffectHandle handle() const noexcept;
    uint64_t id() const noexcept;

    void set_float(size_t index, float value);
    void set_int(size_t index, int value);
    void set_string(size_t index, std::string_view value);
    void set_versus(Versus vs);

    const Versus& versus() const noexcept;

    int type = -1;
    EffectCategory category = EffectCategory::magical;
    int subtype = -1;
    // flecs::entity creator;
    float duration = 0.0f;
    uint32_t expire_day = 0;
    uint32_t expire_time = 0;

private:
    uint64_t id_;

    absl::InlinedVector<int, 4> integers_;
    absl::InlinedVector<float, 4> floats_;
    absl::InlinedVector<std::string, 4> strings_;
    Versus versus_;
};

struct EffectHandle {
    int type = -1;
    const Effect* effect = nullptr;
};

} // namespace nw
