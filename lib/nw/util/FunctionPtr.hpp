#pragma once

#include <utility>

namespace nw {
template <typename T>
struct FunctionPtr;

/// Simple abstraction over a function pointer.
/// @note This will be expanded to support both C function pointers and some script reference i.e. like a
/// lua_Ref.
template <typename Ret, typename... Args>
struct FunctionPtr<Ret(Args...)> {
    FunctionPtr()
        : func_ptr{nullptr}
    {
    }

    FunctionPtr(Ret (*ptr)(Args...))
        : func_ptr{ptr}
    {
    }

    auto operator()(Args... args) -> Ret
    {
        return func_ptr(std::forward<Args>(args)...);
    }

    auto operator()(Args... args) const -> Ret
    {
        return func_ptr(std::forward<Args>(args)...);
    }

    operator bool() const noexcept
    {
        return func_ptr != nullptr;
    }

private:
    Ret (*func_ptr)(Args...) = nullptr;
};

} // namespace nw
