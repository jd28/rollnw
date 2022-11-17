#pragma once

#include "../components/Creature.hpp"
#include "Feat.hpp"
#include "Modifier.hpp"
#include "system.hpp"

#include <cstdint>
#include <limits>
#include <vector>

namespace nw {

DECLARE_RULE_TYPE(MasterFeat);

/// Entry in Master Feat registry
struct MasterFeatEntry {
    MasterFeat mfeat = MasterFeat::invalid();
    int32_t type = -1;
    Feat feat = Feat::invalid();
};

inline bool operator<(const MasterFeatEntry& lhs, const MasterFeatEntry& rhs)
{
    return std::tie(lhs.mfeat, lhs.type, lhs.feat) < std::tie(rhs.mfeat, rhs.type, rhs.feat);
}

inline bool operator==(const MasterFeatEntry& lhs, const MasterFeatEntry& rhs)
{
    return std::tie(lhs.mfeat, lhs.type, lhs.feat) == std::tie(rhs.mfeat, rhs.type, rhs.feat);
}

struct MasterFeatRegistry {
    template <typename T>
    void add(T type, MasterFeat mfeat, Feat feat);
    void clear() noexcept;
    const std::vector<MasterFeatEntry>& entries() const noexcept { return entries_; }
    const ModifierVariant& get_bonus(MasterFeat mfeat) const;

    template <typename T>
    void remove(T type, MasterFeat mfeat);

    /**
     * @brief Resolves a master feat bonus
     *
     * @tparam T Return type
     * @tparam U Rule type
     * @param obj Creature object
     * @param type Rule value
     * @param mfeat Master feat
     */
    template <typename T, typename U>
    T resolve(const Creature* obj, U type, MasterFeat mfeat)
    {
        T result{};
        resolve_n<T>(
            obj, type,
            [&result](T val) { result = val; },
            mfeat);
        return result;
    }

    /**
     * @brief Resolves an arbitrary number of master feats
     *
     * @tparam T Return type
     * @tparam U Rule type
     * @tparam Callback Callback type should be ``void(T)``
     * @tparam Args MasterFeat...
     * @param obj Creature object
     * @param type Rule value
     * @param cb This parameter will be called with any valid master feat bonus as a parameter.
     * @param mfeats As many master feats as needed
     */
    template <typename T, typename U, typename Callback, typename... Args>
    void resolve_n(const Creature* obj, U type, Callback cb, Args... mfeats) const;

    void set_bonus(MasterFeat mfeat, ModifierVariant bonus);

private:
    std::vector<MasterFeatEntry> entries_;
    std::vector<ModifierVariant> bonuses_;
};

template <typename T>
void MasterFeatRegistry::add(T type, MasterFeat mfeat, Feat feat)
{
    static_assert(is_rule_type<T>::value, "only rule types allowed");
    MasterFeatEntry mfe{mfeat, static_cast<int32_t>(*type), feat};

    auto it = std::lower_bound(std::begin(entries_), std::end(entries_), mfe);
    if (it == std::end(entries_)) {
        entries_.push_back(mfe);
    } else if (*it == mfe) {
        return;
    } else {
        entries_.insert(it, mfe);
    }
}

template <typename T>
void MasterFeatRegistry::remove(T type, MasterFeat mfeat)
{
    static_assert(is_rule_type<T>::value, "only rule types allowed");
    entries_.erase(std::remove_if(std::begin(entries_), std::end(entries_),
                       [type, mfeat](const auto& entry) {
                           return entry.mfeat == mfeat && entry.type == *type;
                       }),
        std::end(entries_));
}

template <typename T, typename U, typename Callback, typename... Args>
void MasterFeatRegistry::resolve_n(const Creature* obj,
    U type, Callback cb, Args... mfeats) const
{
    static_assert(is_rule_type<U>::value, "only rule types allowed");
    static_assert(std::is_same_v<T, int> || std::is_same_v<T, float>,
        "result type can only be int or float");

    if (!obj) { return; }

    std::array<T, sizeof...(Args)> result{};
    std::array<MasterFeat, sizeof...(Args)> mfs{mfeats...};
    std::sort(std::begin(mfs), std::end(mfs));

    auto it = std::begin(entries());
    size_t i = 0;
    for (auto mf : mfs) {
        MasterFeatEntry mfe{mf, static_cast<int32_t>(*type), Feat::invalid()};
        const auto& mf_bonus = get_bonus(mf);
        if (mf_bonus.empty()) { continue; }

        it = std::lower_bound(it, std::end(entries()), mfe);
        if (it == std::end(entries())) {
            break;
        }

        while (it != std::end(entries()) && it->type == *type) {
            if (obj->stats.has_feat(it->feat)) {
                if (mf_bonus.template is<T>()) {
                    result[i] = mf_bonus.template as<T>();
                } else if (mf_bonus.template is<ModifierFunction>()) {
                    auto res = mf_bonus.template as<ModifierFunction>()(obj);
                    if (res.template is<T>()) {
                        result[i] = res.template as<T>();
                    }
                }
                break;
            }
            ++it;
        }
        ++i;
    }
    for (const auto& value : result) {
        cb(value);
    }
}

} // namespace nw
