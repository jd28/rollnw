#include "Trap.hpp"

#include <nlohmann/json.hpp>

namespace nw {

bool Trap::from_gff(const GffInputArchiveStruct& archive)
{
    archive.get_to("TrapFlag", is_trapped);
    archive.get_to("TrapType", type);
    archive.get_to("DisarmDC", disarm_dc);
    archive.get_to("TrapDetectable", detectable);
    archive.get_to("TrapDetectDC", detect_dc);
    archive.get_to("TrapDisarmable", disarmable);
    archive.get_to("TrapOneShot", one_shot);

    return true;
}

bool Trap::from_json(const nlohmann::json& archive)
{
    try {
        archive.at("is_trapped").get_to(is_trapped);
        archive.at("type").get_to(type);
        archive.at("detectable").get_to(detectable);
        archive.at("detect_dc").get_to(detect_dc);
        archive.at("disarmable").get_to(disarmable);
        archive.at("disarm_dc").get_to(disarm_dc);
        archive.at("one_shot").get_to(one_shot);
    } catch (nlohmann::json::exception& e) {
        LOG_F(ERROR, "Trap::to_json exception: {}", e.what());
        return false;
    }

    return true;
}

bool Trap::to_gff(GffOutputArchiveStruct& archive) const
{
    archive.add_fields({
        {"TrapFlag", is_trapped},
        {"TrapType", type},
        {"DisarmDC", disarm_dc},
        {"TrapDetectable", detectable},
        {"TrapDetectDC", detect_dc},
        {"TrapDisarmable", disarmable},
        {"TrapOneShot", one_shot},
    });
    return true;
}

nlohmann::json Trap::to_json() const
{
    nlohmann::json j;

    j["is_trapped"] = is_trapped;
    j["type"] = type;
    j["detectable"] = detectable;
    j["detect_dc"] = detect_dc;
    j["disarmable"] = disarmable;
    j["disarm_dc"] = disarm_dc;
    j["one_shot"] = one_shot;

    return j;
}

} // namespace nw
