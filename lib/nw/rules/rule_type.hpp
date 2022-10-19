#pragma once

#include <nlohmann/json.hpp>

#include <bitset>
#include <compare>
#include <type_traits>

namespace nw {

template <typename T>
struct is_rule_type_base : std::false_type {
};

template <typename T>
struct is_rule_type : is_rule_type_base<std::remove_cv_t<T>> {
};

#define DECLARE_RULE_TYPE(name)                                \
    struct name {                                              \
        bool operator==(const name& rhs) const = default;      \
        auto operator<=>(const name& rhs) const = default;     \
        constexpr int32_t operator*() const noexcept           \
        {                                                      \
            return int32_t(val);                               \
        }                                                      \
        constexpr size_t idx() const noexcept                  \
        {                                                      \
            return size_t(val);                                \
        }                                                      \
        static constexpr name make(int32_t id)                 \
        {                                                      \
            return name{id};                                   \
        }                                                      \
        static constexpr name invalid()                        \
        {                                                      \
            return name{};                                     \
        };                                                     \
                                                               \
        int32_t val = -1;                                      \
    };                                                         \
    template <>                                                \
    struct is_rule_type_base<name> : std::true_type {          \
    };                                                         \
    inline void from_json(const nlohmann::json& j, name& type) \
    {                                                          \
        type = name::make(j.get<int32_t>());                   \
    }                                                          \
    inline void to_json(nlohmann::json& j, const name& type)   \
    {                                                          \
        j = *type;                                             \
    }

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
        if (pos != T::invalid()) {
            Base::flip(static_cast<size_t>(pos));
        }
        return *this;
    }

    RuleFlag& reset(T pos)
    {
        static_assert(is_rule_type<T>::value, "only rule types allowed");
        if (pos != T::invalid()) {
            Base::reset(static_cast<size_t>(pos));
        }
        return *this;
    }

    RuleFlag& set(T pos, bool value = true)
    {
        static_assert(is_rule_type<T>::value, "only rule types allowed");
        if (pos != T::invalid()) {
            Base::set(pos.idx(), value);
        }
        return *this;
    }

    bool test(T pos)
    {
        static_assert(is_rule_type<T>::value, "only rule types allowed");
        return T::invalid() != pos ? Base::test(pos.idx()) : false;
    }
};

} // namespace nw
