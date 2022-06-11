#pragma once

#include <type_traits>
#include <variant>

namespace nw {

/// Empty helper struct for Variant
struct Null {
};

inline bool operator==(const Null&, const Null&) { return true; }
inline bool operator<(const Null&, const Null&) { return false; }

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

    template <typename T>
    bool is() const noexcept
    {
        static_assert(std::disjunction<std::is_same<T, Ts>...>::value, "incompatible type");
        return std::holds_alternative<T>(payload_);
    }

    template <typename T>
    T& as()
    {
        static_assert(std::disjunction<std::is_same<T, Ts>...>::value, "incompatible type");
        return std::get<T>(payload_);
    }

    template <typename T>
    const T& as() const
    {
        static_assert(std::disjunction<std::is_same<T, Ts>...>::value, "incompatible type");
        return std::get<T>(payload_);
    }

    bool operator<(const Variant& rhs) const noexcept
    {
        return payload_ < rhs.payload_;
    }

    bool operator==(const Variant& rhs) const noexcept
    {
        return payload_ == rhs.payload_;
    }

    bool empty() const noexcept { return std::holds_alternative<Null>(payload_); }

private:
    std::variant<Null, Ts...> payload_;
};

} // namespace nw
