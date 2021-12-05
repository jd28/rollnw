#include "Encounter.hpp"

namespace nw {

Encounter::Encounter(const GffInputArchiveStruct gff, SerializationProfile profile)
    : common_{ObjectType::encounter, gff, profile}
{

    size_t sz = gff["CreatureList"].size();
    creatures.resize(sz);
    for (size_t i = 0; i < sz; ++i) {
        auto st = gff["CreatureList"][i];
        st.get_to("Appearance", creatures[i].appearance);
        st.get_to("CR", creatures[i].cr);
        st.get_to("ResRef", creatures[i].resref);
        st.get_to("SingleSpawn", creatures[i].single_spawn);
    }

    gff.get_to("OnEntered", on_entered);
    gff.get_to("OnExhausted", on_exhausted);
    gff.get_to("OnExit", on_exit);
    gff.get_to("OnHeartbeat", on_heartbeat);
    gff.get_to("OnUserDefined", on_userdefined);

    gff.get_to("MaxCreatures", creatures_max);
    gff.get_to("RecCreatures", creatures_recommended);
    gff.get_to("Difficulty", difficulty);
    gff.get_to("DifficultyIndex", difficulty_index);
    gff.get_to("ResetTime", reset_time);
    gff.get_to("Respawns", respawns);
    gff.get_to("SpawnOption", spawn_option);

    gff.get_to("Active", active);
    gff.get_to("PlayerOnly", player_only);
    gff.get_to("Reset", reset);

    if (profile == SerializationProfile::instance) {
        size_t sz = gff["Geometry"].size();
        geometry.reserve(sz);
        for (size_t i = 0; i < sz; ++i) {
            glm::vec3 v;
            gff["Geometry"][i].get_to("X", v.x);
            gff["Geometry"][i].get_to("Y", v.y);
            gff["Geometry"][i].get_to("Z", v.z);
            geometry.push_back(v);
        }

        sz = gff["SpawnPointList"].size();
        spawn_points.reserve(sz);
        for (size_t i = 0; i < sz; ++i) {
            SpawnPoint sp;
            gff["SpawnPointList"][i].get_to("Orientation", sp.orientation);
            gff["SpawnPointList"][i].get_to("X", sp.position.x);
            gff["SpawnPointList"][i].get_to("Y", sp.position.y);
            gff["SpawnPointList"][i].get_to("Z", sp.position.z);
            spawn_points.push_back(sp);
        }
    }
}

} // namespace nw
