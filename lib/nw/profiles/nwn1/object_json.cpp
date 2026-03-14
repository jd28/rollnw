#include "object_json.hpp"

#include "propset_gff_policy.hpp"
#include "../../objects/Creature.hpp"
#include "../../serialization/Gff.hpp"
#include "../../serialization/GffBuilder.hpp"
#include "../../smalls/propset_json.hpp"
#include "../../smalls/runtime.hpp"

#include <nlohmann/json.hpp>

namespace nwn1 {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static const nw::smalls::StructDef* struct_def_from_tid(nw::smalls::Runtime* rt,
    nw::smalls::TypeID tid)
{
    const nw::smalls::Type* type = rt->get_type(tid);
    if (!type || type->type_kind != nw::smalls::TK_struct) { return nullptr; }
    auto struct_id = type->type_params[0].as<nw::smalls::StructID>();
    return rt->type_table_.get(struct_id);
}

// ---------------------------------------------------------------------------
// serialize_object_json
// ---------------------------------------------------------------------------

nlohmann::json serialize_object_json(const nw::Creature* cre,
    nw::smalls::Runtime* rt, const PropsetGffPolicyRegistry* registry,
    nw::SerializationProfile profile)
{
    nlohmann::json j;
    if (!cre || !rt || !registry) { return j; }

    // Emit all C++ holdover sections via existing serialization.
    nw::serialize(cre, j, profile);

    // Emit propset sections for "core.creature.*", keyed by qualified name.
    nw::smalls::PropsetJsonSerializer ser(rt);
    for (const auto& policy : registry->policies()) {
        std::string_view qname = policy.qualified_name;
        if (!qname.starts_with("core.creature.")) { continue; }

        if (policy.instance_only && profile == nw::SerializationProfile::blueprint) {
            continue;
        }

        nw::smalls::TypeID tid = rt->type_id(qname, false);
        if (tid == nw::smalls::invalid_type_id) { continue; }

        nw::smalls::Value ref = rt->get_or_create_propset_ref(tid, cre->handle());
        if (ref.type_id == nw::smalls::invalid_type_id) { continue; }

        const nw::smalls::StructDef* def = struct_def_from_tid(rt, tid);
        if (!def) { continue; }

        j[std::string{qname}] = ser.serialize(ref, def);
    }

    return j;
}

// ---------------------------------------------------------------------------
// deserialize_object_json
// ---------------------------------------------------------------------------

bool deserialize_object_json(const nlohmann::json& j, nw::Creature* cre,
    nw::smalls::Runtime* rt, const PropsetGffPolicyRegistry* registry,
    nw::SerializationProfile profile)
{
    if (!cre || !rt || !registry) { return false; }

    // Restore C++ holdovers via existing deserialization path.
    bool ok = nw::deserialize(cre, j, profile);

    // Restore propset sections for "core.creature.*".
    nw::smalls::PropsetJsonSerializer ser(rt);
    for (const auto& policy : registry->policies()) {
        std::string_view qname = policy.qualified_name;
        if (!qname.starts_with("core.creature.")) { continue; }

        std::string key{qname};
        if (!j.contains(key)) { continue; }

        nw::smalls::TypeID tid = rt->type_id(qname, false);
        if (tid == nw::smalls::invalid_type_id) { continue; }

        nw::smalls::Value ref = rt->get_or_create_propset_ref(tid, cre->handle());
        if (ref.type_id == nw::smalls::invalid_type_id) { ok = false; continue; }

        const nw::smalls::StructDef* def = struct_def_from_tid(rt, tid);
        if (!def) { ok = false; continue; }

        if (!ser.deserialize(j.at(key), ref, def)) { ok = false; }
    }

    return ok;
}

} // namespace nwn1
