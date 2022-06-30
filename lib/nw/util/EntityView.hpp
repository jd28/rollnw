#pragma once

#include <flecs/flecs.h>

#include <tuple>

namespace nw {

template <typename... Ts>
struct EntityView {
    EntityView(const flecs::entity entity)
        : ptrs{entity.get<Ts>()...}
        , ent{entity}
    {
    }

    EntityView(const flecs::entity entity, Ts*... components)
        : ptrs{components...}
        , ent{entity}
    {
    }

    operator bool() const noexcept
    {
        bool result = true;
        std::apply([&result](auto&&... args) { ((result = result && args), ...); }, ptrs);
        return result;
    }

    template <typename T>
    const T* get() const noexcept { return std::get<const T*>(ptrs); }

    std::tuple<const Ts*...> ptrs;
    flecs::entity const ent;
};

} // namespace nw
