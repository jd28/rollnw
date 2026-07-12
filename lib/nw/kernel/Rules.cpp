#include "Rules.hpp"

#include "../util/profile.hpp"
#include "../util/string.hpp"
#include "GameProfile.hpp"
#include "TwoDACache.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>

namespace nw::kernel {

const std::type_index Rules::type_index{typeid(Rules)};

Rules::Rules(MemoryResource* scope)
    : Service(scope)
    , feats{allocator()}
    , races{allocator()}
    , spells{allocator()}
    , spellschools{allocator()}
    , skills{allocator()}
    , appearances{allocator()}
{
}

void Rules::initialize(ServiceInitTime time)
{
    NW_PROFILE_SCOPE_N("rules.initialize");

    if (time != ServiceInitTime::kernel_start && time != ServiceInitTime::module_post_load) {
        return;
    }

    auto start = std::chrono::high_resolution_clock::now();

    LOG_F(INFO, "kernel: rules system initializing...");
    {
        NW_PROFILE_SCOPE_N("rules.initialize.load_rules");
        services().profile()->load_rules();
    }

    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    LOG_F(INFO, "kernel: rules system initialized. ({}ms)",
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

bool Rules::match(const Qualifier& qual, const ObjectBase* obj) const
{
    return qualifier_matcher_ ? qualifier_matcher_(qual, obj) : true;
}

bool Rules::meets_requirement(const Requirement& req, const ObjectBase* obj) const
{
    for (const auto& q : req.qualifiers) {
        if (req.conjunction) {
            if (!match(q, obj)) {
                return false;
            }
        } else if (match(q, obj)) {
            return true;
        }
    }
    return true;
}

void Rules::set_qualifier_matcher(QualifierMatcher matcher) noexcept
{
    qualifier_matcher_ = matcher;
}

nlohmann::json Rules::stats() const
{
    nlohmann::json j;
    j["rule system"] = {};
    return j;
}

} // namespace nw::kernel
