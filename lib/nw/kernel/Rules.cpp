#include "Rules.hpp"

#include "../smalls/runtime.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <stdexcept>

namespace nw::kernel {

namespace {

bool match_profile_qualifier(const Qualifier& qualifier, const ObjectBase* object)
{
    return runtime().profile_match_qualifier(
        object ? object->handle() : ObjectHandle{},
        *qualifier.type,
        qualifier.subtype,
        static_cast<int32_t>(qualifier.match),
        qualifier.value);
}

} // namespace

const std::type_index Rules::type_index{typeid(Rules)};

Rules::Rules(MemoryResource* scope)
    : Service(scope)
    , appearances{allocator()}
    , wingmodels{allocator()}
    , tailmodels{allocator()}
    , creature_body_parts{allocator()}
    , baseitems{allocator()}
    , placeables{allocator()}
    , doortypes{allocator()}
    , genericdoors{allocator()}
{
}

bool Rules::publish_skill_count(int32_t count) noexcept
{
    if (count < 0) { return false; }
    skill_count_ = static_cast<size_t>(count);
    return true;
}

void Rules::initialize(ServiceInitTime time)
{
    if (time != ServiceInitTime::kernel_start && time != ServiceInitTime::module_post_load) {
        return;
    }
    set_qualifier_matcher(match_profile_qualifier);
}

bool Rules::match(const Qualifier& qual, const ObjectBase* obj) const
{
    if (!qualifier_matcher_) {
        throw std::logic_error("rules: selected package qualifier matcher is unavailable");
    }
    return qualifier_matcher_(qual, obj);
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
