#pragma once

#include "../util/InternedString.hpp"

#include "../kernel/Memory.hpp"
#include <absl/container/flat_hash_map.h>
#include <nlohmann/json_fwd.hpp>

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

#define DECLARE_RULE_TYPE(name)                            \
    struct name {                                          \
        /** Defaulted equality operator */                 \
        bool operator==(const name& rhs) const = default;  \
        /** Defaulted spaceship operator */                \
        auto operator<=>(const name& rhs) const = default; \
        /** Returns rule type as value */                  \
        constexpr int32_t operator*() const noexcept       \
        {                                                  \
            return int32_t(val);                           \
        }                                                  \
        /** Returns rule type as index */                  \
        constexpr size_t idx() const noexcept              \
        {                                                  \
            return size_t(val);                            \
        }                                                  \
        /** Makes a rule type */                           \
        static constexpr name make(int32_t id)             \
        {                                                  \
            return name{id};                               \
        }                                                  \
        /** Returns an invalid rule type */                \
        static constexpr name invalid()                    \
        {                                                  \
            return name{};                                 \
        };                                                 \
                                                           \
        int32_t val = -1;                                  \
    };                                                     \
    void from_json(const nlohmann::json& j, name& type);   \
    void to_json(nlohmann::json& j, const name& type);     \
    template <>                                            \
    struct is_rule_type_base<name> : std::true_type {      \
    }

#define DEFINE_RULE_TYPE(name)                          \
    void from_json(const nlohmann::json& j, name& type) \
    {                                                   \
        type = name::make(j.get<int32_t>());            \
    }                                                   \
    void to_json(nlohmann::json& j, const name& type)   \
    {                                                   \
        j = *type;                                      \
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

    explicit RuleFlag(StringView str)
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
            Base::flip(pos.idx());
        }
        return *this;
    }

    RuleFlag& reset(T pos)
    {
        static_assert(is_rule_type<T>::value, "only rule types allowed");
        if (pos != T::invalid()) {
            Base::reset(pos.idx());
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

    bool test(T pos) const
    {
        static_assert(is_rule_type<T>::value, "only rule types allowed");
        return T::invalid() != pos ? Base::test(pos.idx()) : false;
    }
};

/**
 * @brief Base template for rule type arrays
 *
 * @tparam RuleType
 * @tparam RuleTypeInfo
 */
template <typename RuleType, typename RuleTypeInfo>
struct RuleTypeArray {
    using map_type = absl::flat_hash_map<
        InternedString,
        RuleType,
        InternedStringHash,
        InternedStringEq,
        Allocator<std::pair<const InternedString, const RuleType>>>;

    RuleTypeArray(MemoryResource* allocator = kernel::global_allocator())
        : entries(allocator)
        , constant_to_index(allocator)
    {
    }

    const RuleTypeInfo* get(RuleType type) const noexcept
    {
        if (type.idx() < entries.size() && entries[type.idx()].valid()) {
            return &entries[type.idx()];
        } else {
            return nullptr;
        }
    }

    void clear()
    {
        entries.clear();
        constant_to_index.clear();
    }

    bool is_valid(RuleType type) const noexcept
    {
        return type.idx() < entries.size() && entries[type.idx()].valid();
    }

    RuleType from_constant(StringView constant) const
    {
        auto it = constant_to_index.find(constant);
        if (it == constant_to_index.end()) {
            return RuleType::invalid();
        } else {
            return it->second;
        }
    }

    PVector<RuleTypeInfo> entries;
    map_type constant_to_index;
};

} // namespace nw
