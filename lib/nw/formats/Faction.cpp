#include "Faction.hpp"

#include "../serialization/Gff.hpp"
#include "../serialization/GffBuilder.hpp"

#include <nlohmann/json.hpp>

namespace nw {

Faction::Faction(ResourceData rdata)
{
    if (rdata.bytes.size() <= 8) { return; }
    if (memcmp(rdata.bytes.data(), "FAC V3.2", 8) == 0) {
        Gff gff(std::move(rdata));
        if (!gff.valid()) { return; }
        deserialize(*this, gff.toplevel());
    } else {
        try {
            nlohmann::json j = nlohmann::json::parse(rdata.bytes.string_view());
            deserialize(*this, j);
        } catch (...) {
        }
    }
}

Faction::Faction(const Gff& archive)
{
    deserialize(*this, archive.toplevel());
}

Faction::Faction(const nlohmann::json& archive)
{
    deserialize(*this, archive);
}

// == Faction - Serialization - Gff ==========================================
// ============================================================================

bool deserialize(Faction& obj, const GffStruct& archive)
{
    auto field = archive["FactionList"];
    size_t sz = field.size();
    obj.factions.reserve(sz);

    for (size_t i = 0; i < sz; ++i) {
        FactionInfo fi;
        field[i].get_to("FactionName", fi.name);
        field[i].get_to("FactionParentID", fi.parent);
        field[i].get_to("FactionGlobal", fi.global);
        obj.factions.push_back(fi);
    }

    field = archive["RepList"];
    sz = field.size();
    obj.reputations.reserve(sz);

    for (size_t i = 0; i < sz; ++i) {
        Reputation r;
        field[i].get_to("FactionID1", r.faction_1);
        field[i].get_to("FactionID2", r.faction_2);
        field[i].get_to("FactionRep", r.reputation);
        obj.reputations.push_back(r);
    }

    return true;
}

GffBuilder serialize(const Faction& obj)
{
    GffBuilder out{"FAC"};
    auto& top = out.top;

    uint32_t i = 0;
    auto& fl = top.add_list("FactionList");
    for (const auto& f : obj.factions) {
        auto& st = fl.push_back(i);
        st.add_field("FactionName", f.name);
        st.add_field("FactionParentID", f.parent);
        st.add_field("FactionGlobal", f.global);
        ++i;
    }

    i = 0;
    auto& rl = top.add_list("RepList");
    for (const auto& r : obj.reputations) {
        auto& st = rl.push_back(i);
        st.add_field("FactionID1", r.faction_1);
        st.add_field("FactionID2", r.faction_2);
        st.add_field("FactionRep", r.reputation);
        ++i;
    }

    out.build();
    return out;
}

nlohmann::json Faction::to_json() const
{
    nlohmann::json j;
    j["$type"] = "FAC";
    j["$version"] = json_archive_version;
    auto& fac = j["factions"] = nlohmann::json::array();
    for (const auto& faction : factions) {
        fac.push_back({
            {"name", faction.name},
            {"parent", faction.parent},
            {"global", faction.global},
        });
    }

    auto& rep = j["reputations"] = nlohmann::json::array();
    for (const auto& r : reputations) {
        rep.push_back({
            {"faction_1", r.faction_1},
            {"faction_2", r.faction_2},
            {"reputation", r.reputation},
        });
    }

    return j;
}

// == Faction - Serialization - JSON ==========================================
// ============================================================================

bool deserialize(Faction& obj, const nlohmann::json& archive)
{
    auto& fac = archive.at("factions");
    obj.factions.reserve(fac.size());
    for (const auto& f : fac) {
        FactionInfo fi;
        f.at("name").get_to(fi.name);
        f.at("parent").get_to(fi.parent);
        f.at("global").get_to(fi.global);
        obj.factions.push_back(fi);
    }

    auto& rep = archive.at("reputations");
    obj.reputations.reserve(rep.size());
    for (const auto& r : rep) {
        Reputation fr;
        r.at("faction_1").get_to(fr.faction_1);
        r.at("faction_2").get_to(fr.faction_2);
        r.at("reputation").get_to(fr.reputation);
        obj.reputations.push_back(fr);
    }

    return true;
}

} // namespace nw
