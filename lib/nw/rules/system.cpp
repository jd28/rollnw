#include "system.hpp"

#include "../kernel/Kernel.hpp"
#include "Class.hpp"
#include "feats.hpp"

namespace nw {

// == Qualifier ===============================================================
// ============================================================================

Qualifier qualifier_ability(nw::Ability id, int min, int max)
{
    Qualifier q;
    q.type = nw::req_type_ability;
    q.subtype = *id;
    q.params.push_back(min);
    q.params.push_back(max);
    return q;
}

Qualifier qualifier_alignment(nw::AlignmentAxis axis, nw::AlignmentFlags flags)
{
    Qualifier q;
    q.type = req_type_alignment;
    q.subtype = static_cast<int>(axis);
    q.params.push_back(static_cast<int32_t>(flags));
    return q;
}

Qualifier qualifier_base_attack_bonus(int min, int max)
{
    Qualifier q;
    q.type = req_type_bab;
    q.params.push_back(min);
    q.params.push_back(max);
    return q;
}

Qualifier qualifier_class_level(nw::Class id, int min, int max)
{
    Qualifier q;
    q.type = req_type_class_level;
    q.subtype = *id;
    q.params.push_back(min);
    q.params.push_back(max);
    return q;
}

Qualifier qualifier_feat(nw::Feat id)
{
    Qualifier q;
    q.type = req_type_feat;
    q.subtype = *id;
    return q;
}

Qualifier qualifier_race(nw::Race id)
{
    Qualifier q;
    q.type = req_type_race;
    q.params.push_back(*id);
    return q;
}

Qualifier qualifier_skill(nw::Skill id, int min, int max)
{
    Qualifier q;
    q.type = req_type_skill;
    q.subtype = *id;
    q.params.push_back(min);
    q.params.push_back(max);
    return q;
}

Qualifier qualifier_level(int min, int max)
{
    Qualifier q;
    q.type = req_type_level;
    q.params.push_back(min);
    q.params.push_back(max);
    return q;
}

// == Requirement =============================================================
// ============================================================================

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

ModifierResult::ModifierResult(int value)
    : integer{value}
    , type_(type::int_)
{
}

ModifierResult::ModifierResult(float value)
    : number{value}
    , type_(type::float_)
{
}

ModifierResult::ModifierResult(DamageRoll value)
    : dmg_roll{value}
    , type_(type::dmg_roll)
{
}

nw::Modifier make_modifier(nw::ModifierType type, nw::ModifierVariant value,
    StringView tag, nw::ModifierSource source, nw::Requirement req, int32_t subtype)
{
    return nw::Modifier{
        type,
        {value},
        tag.size() ? nw::kernel::strings().intern(tag) : nw::InternedString{},
        source,
        std::move(req),
        subtype,
    };
}

ModifierRegistry::ModifierRegistry(MemoryResource* allocator)
    : allocator_(allocator)
{
}

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

int ModifierRegistry::remove(StringView tag)
{
    StringView prefix = tag;
    bool starts = false;
    int result = 0;

    if (prefix.empty()) {
        return result;
    }
    if (prefix.back() == '*') {
        prefix = StringView{prefix.data(), prefix.size() - 1};
        if (prefix.empty()) {
            return result;
        }
        starts = true;
    }

    auto it = std::remove_if(std::begin(entries_), std::end(entries_),
        [&](const auto& mod) {
            if (starts) {
                if (string::startswith(mod.tagged.view(), prefix)) {
                    ++result;
                    return true;
                }
            } else if (mod.tagged.view() == prefix) {
                ++result;
                return true;
            }
            return false;
        });

    entries_.erase(it, std::end(entries_));

    return result;
}

int ModifierRegistry::replace(StringView tag, ModifierVariant value)
{
    StringView prefix = tag;
    bool starts = false;
    int result = 0;

    if (prefix.empty()) {
        return result;
    }

    if (prefix.back() == '*') {
        prefix = StringView{prefix.data(), prefix.size() - 1};
        if (prefix.empty()) {
            return result;
        }
        starts = true;
    }

    for (auto& mod : entries_) {
        if (starts) {
            if (string::startswith(mod.tagged.view(), prefix)) {
                mod.input = std::move(value);
                ++result;
            }
        } else if (mod.tagged.view() == prefix) {
            mod.input = std::move(value);
            ++result;
        }
    }
    return result;
}

int ModifierRegistry::replace(StringView tag, const Requirement& req)
{
    StringView prefix = tag;
    bool starts = false;
    int result = 0;

    if (prefix.empty()) {
        return result;
    }

    if (prefix.back() == '*') {
        prefix = StringView{prefix.data(), prefix.size() - 1};
        if (prefix.empty()) {
            return result;
        }
        starts = true;
    }

    for (auto& mod : entries_) {
        if (starts) {
            if (string::startswith(mod.tagged.view(), prefix)) {
                mod.requirement = req;
                ++result;
            }
        } else if (mod.tagged.view() == prefix) {
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
