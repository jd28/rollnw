#pragma once

#include "type_traits.hpp"

#include <bitset>

namespace nw {

template <typename T, size_t N = 64>
struct RuleFlag : private std::bitset<N> {
    using Base = std::bitset<N>;

    using Base::all;
    using Base::any;
    using Base::count;
    using Base::none;
    using Base::size;
    using Base::to_string;
    using Base::to_ullong;
    using Base::to_ulong;

    /// Test
    using Base::operator&=;
    using Base::operator|=;
    using Base::operator^=;
    using Base::operator~;
    using Base::operator<<=;
    using Base::operator>>=;
    using Base::operator<<;
    using Base::operator>>;

    constexpr RuleFlag() = default;
    constexpr RuleFlag(unsigned long long val) noexcept
        : Base{val}
    {
    }

    RuleFlag(T val) noexcept
    {
        set(val);
    }

    explicit RuleFlag(std::string_view str)
        : Base(str.data())
    {
    }

    bool operator[](T pos) const
    {
        static_assert(is_rule_type<T>::value, "only rule types allowed");
        return test(pos);
    }

    RuleFlag& flip(T pos)
    {
        static_assert(is_rule_type<T>::value, "only rule types allowed");
        if (pos != T::invalid) {
            Base::flip(static_cast<size_t>(pos));
        }
        return *this;
    }

    RuleFlag& reset(T pos)
    {
        static_assert(is_rule_type<T>::value, "only rule types allowed");
        if (pos != T::invalid) {
            Base::reset(static_cast<size_t>(pos));
        }
        return *this;
    }

    RuleFlag& set(T pos, bool value = true)
    {
        static_assert(is_rule_type<T>::value, "only rule types allowed");
        if (pos != T::invalid) {
            Base::set(static_cast<size_t>(pos), value);
        }
        return *this;
    }

    bool test(T pos)
    {
        static_assert(is_rule_type<T>::value, "only rule types allowed");
        return T::invalid != pos ? Base::test(static_cast<size_t>(pos)) : false;
    }
};
} // namespace nw
