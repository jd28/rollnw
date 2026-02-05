#pragma once

#include <absl/hash/hash.h>

#include <optional>
#include <type_traits>
#include <variant>

namespace nw {

/// Wrapper around std::variant
template <typename... Ts>
struct Variant {
    Variant() = default;
    Variant(const Variant&) = default;
    Variant(Variant&&) = default;
    Variant& operator=(const Variant&) = default;
    Variant& operator=(Variant&&) = default;

    template <typename T>
    Variant(T value)
        : payload_{std::move(value)}
    {
    }

    /// Checks variant value is `T`
    template <typename T>
    bool is() const noexcept
    {
        static_assert(std::disjunction<std::is_same<T, Ts>...>::value, "incompatible type");
        return std::holds_alternative<T>(payload_);
    }

    /// Gets variant value as `T`
    template <typename T>
    T& as()
    {
        static_assert(std::disjunction<std::is_same<T, Ts>...>::value, "incompatible type");
        return std::get<T>(payload_);
    }

    /// Gets variant value as `T`
    template <typename T>
    const T& as() const
    {
        static_assert(std::disjunction<std::is_same<T, Ts>...>::value, "incompatible type");
        return std::get<T>(payload_);
    }

    /// Checks variant value is `std::optional<T>`
    /// @note This does entail a copy
    template <typename T>
    std::optional<T> get() const
    {
        static_assert(std::disjunction<std::is_same<T, Ts>...>::value, "incompatible type");
        return is<T>() ? std::get<T>(payload_) : std::optional<T>();
    }

    bool operator<(const Variant& rhs) const noexcept
    {
        return payload_ < rhs.payload_;
    }

    bool operator==(const Variant& rhs) const noexcept
    {
        return payload_ == rhs.payload_;
    }

    bool empty() const noexcept { return payload_.index() == 0; }

    std::variant<std::monostate, Ts...> payload_;
};

template <typename H, typename... Ts>
H AbslHashValue(H h, const Variant<Ts...>& var)
{
    return H::combine(std::move(h), var.payload_);
}

} // namespace nw
