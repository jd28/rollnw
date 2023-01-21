#include "system.hpp"

#include "../kernel/Kernel.hpp"

namespace nw {

// == Requirement =============================================================

Requirement::Requirement(bool conjunction_)
    : conjunction{conjunction_}
{
}

Requirement::Requirement(std::initializer_list<Qualifier> quals, bool conjunction_)
    : conjunction{conjunction_}
{
    for (const auto& q : quals) {
        qualifiers.push_back(q);
    }
}

void Requirement::add(Qualifier qualifier)
{
    qualifiers.push_back(std::move(qualifier));
}

size_t Requirement::size() const noexcept
{
    return qualifiers.size();
}

// == Modifier ================================================================
// ============================================================================

void ModifierRegistry::add(Modifier mod)
{
    auto it = std::lower_bound(std::begin(entries_), std::end(entries_), mod);
    if (it == std::end(entries_)) {
        entries_.push_back(std::move(mod));
    } else {
        entries_.insert(it, std::move(mod));
    }
}

ModifierRegistry::iterator ModifierRegistry::begin() { return entries_.begin(); }
ModifierRegistry::const_iterator ModifierRegistry::begin() const { return entries_.begin(); }
ModifierRegistry::const_iterator ModifierRegistry::cbegin() const { return entries_.begin(); }
ModifierRegistry::const_iterator ModifierRegistry::cend() const { return entries_.end(); }

void ModifierRegistry::clear()
{
    entries_.clear();
}

ModifierRegistry::iterator ModifierRegistry::end() { return entries_.end(); }
ModifierRegistry::const_iterator ModifierRegistry::end() const { return entries_.end(); }

int ModifierRegistry::remove(std::string_view tag)
{
    std::string_view prefix = tag;
    bool starts = false;
    int result = 0;

    if (prefix.empty()) {
        return result;
    }
    if (prefix.back() == '*') {
        prefix = std::string_view{prefix.data(), prefix.size() - 1};
        if (prefix.empty()) {
            return result;
        }
        starts = true;
    }

    auto it = std::remove_if(std::begin(entries_), std::end(entries_),
        [&](const auto& mod) {
            if (starts) {
                if (string::startswith(mod.tagged, prefix)) {
                    ++result;
                    return true;
                }
            } else if (mod.tagged == prefix) {
                ++result;
                return true;
            }
            return false;
        });

    entries_.erase(it, std::end(entries_));

    return result;
}

int ModifierRegistry::replace(std::string_view tag, ModifierVariant value)
{
    std::string_view prefix = tag;
    bool starts = false;
    int result = 0;

    if (prefix.empty()) {
        return result;
    }

    if (prefix.back() == '*') {
        prefix = std::string_view{prefix.data(), prefix.size() - 1};
        if (prefix.empty()) {
            return result;
        }
        starts = true;
    }

    for (auto& mod : entries_) {
        if (starts) {
            if (string::startswith(mod.tagged, prefix)) {
                mod.input = std::move(value);
                ++result;
            }
        } else if (mod.tagged == prefix) {
            mod.input = std::move(value);
            ++result;
        }
    }
    return result;
}

int ModifierRegistry::replace(std::string_view tag, const Requirement& req)
{
    std::string_view prefix = tag;
    bool starts = false;
    int result = 0;

    if (prefix.empty()) {
        return result;
    }

    if (prefix.back() == '*') {
        prefix = std::string_view{prefix.data(), prefix.size() - 1};
        if (prefix.empty()) {
            return result;
        }
        starts = true;
    }

    for (auto& mod : entries_) {
        if (starts) {
            if (string::startswith(mod.tagged, prefix)) {
                mod.requirement = req;
                ++result;
            }
        } else if (mod.tagged == prefix) {
            mod.requirement = req;
            ++result;
        }
    }
    return result;
}

size_t ModifierRegistry::size() const
{
    return entries_.size();
}

} // namespace nw
