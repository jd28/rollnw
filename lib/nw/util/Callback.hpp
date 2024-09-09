#pragma once

#include <utility>

namespace nw {
template <typename T>
struct Callback;

/// Simple abstraction over a callback.
/// This will be expanded to support both C function pointers and some
/// script reference.
template <typename Ret, typename... Args>
struct Callback<Ret(Args...)> {
    Ret (*func_ptr)(Args...) = nullptr;

    auto operator()(Args... args) -> Ret
    {
        return func_ptr(std::forward<Args>(args)...);
    }
};

} // namespace nw
