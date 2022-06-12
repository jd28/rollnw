#pragma once

#include "system.hpp"

#include <absl/container/inlined_vector.h>
#include <flecs/flecs.h>

#include <string>

namespace nw {

struct EffectHandle;

struct Effect {
    // static uint64_t id = 0;

    float get_float(size_t index) const;
    int get_int(size_t index) const;
    std::string_view get_string(size_t index) const;

    EffectHandle handle() const noexcept;

    void set_float(size_t index, float value);
    void set_int(size_t index, int value);
    void set_string(size_t index, std::string_view value);
    void set_versus(Versus vs);

    const Versus& versus() const noexcept;

private:
    uint64_t id_;
    int type_ = -1;
    int subtype_ = -1;
    flecs::entity creator;
    float duration = 0.0f;
    uint32_t expire_day = 0;
    uint32_t expire_time = 0;

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
