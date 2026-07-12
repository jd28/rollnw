#include "rules.hpp"

#include "scriptbridge.hpp"

#include "../../kernel/Rules.hpp"
#include "../../objects/Creature.hpp"

namespace nwn1 {

namespace {

bool is_supported_qualifier_type(nw::ReqType type) noexcept
{
    return type == nw::req_type_ability
        || type == nw::req_type_alignment
        || type == nw::req_type_bab
        || type == nw::req_type_class_level
        || type == nw::req_type_feat
        || type == nw::req_type_level
        || type == nw::req_type_race
        || type == nw::req_type_skill;
}

bool match_qualifier(const nw::Qualifier& qual, const nw::ObjectBase* obj)
{
    if (!is_supported_qualifier_type(qual.type)) { return true; }

    auto* cre = obj ? obj->as_creature() : nullptr;
    if (!cre) { return false; }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(bridge::make_object_arg(cre->handle()));
    args.push_back(nw::smalls::Value::make_int(*qual.type));
    args.push_back(nw::smalls::Value::make_int(qual.subtype));
    args.push_back(nw::smalls::Value::make_int(static_cast<int32_t>(qual.match)));
    args.push_back(nw::smalls::Value::make_int(qual.value));
    return bridge::call_nwn1_module_bool("nwn1.requirements", "match_qualifier", args).value_or(false);
}

} // namespace

void load_qualifier_matcher()
{
    nw::kernel::rules().set_qualifier_matcher(match_qualifier);
}

} // namespace nwn1
