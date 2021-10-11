#include "Faction.hpp"

namespace nw {

Faction::Faction(const GffStruct gff)
{

    GffField field = gff["FactionList"];
    size_t sz = field.size();
    factions.reserve(sz);

    for (size_t i = 0; i < sz; ++i) {
        FactionInfo fi;
        field[i].get_to("FactionName", fi.name);
        field[i].get_to("FactionParentID", fi.parent);
        field[i].get_to("FactionGlobal", fi.global);
        factions.push_back(fi);
    }

    field = gff["RepList"];
    sz = field.size();
    reputations.reserve(sz);

    for (size_t i = 0; i < sz; ++i) {
        Reputation r;
        field[i].get_to("FactionID1", r.faction_1);
        field[i].get_to("FactionID2", r.faction_2);
        field[i].get_to("FactionRep", r.reputation);
        reputations.push_back(r);
    }
}

} // namespace nw
