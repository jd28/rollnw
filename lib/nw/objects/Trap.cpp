#include "Trap.hpp"

#include "../formats/TwoDA.hpp"
#include "../serialization/Gff.hpp"
#include "../serialization/GffBuilder.hpp"

#include <nlohmann/json.hpp>

namespace nw {

DEFINE_RULE_TYPE(TrapType);

TrapInfo::TrapInfo(const TwoDARowView& tda)
{
    String temp;

    if (tda.get_to("TrapScript", temp)) {
        script = nw::Resref{temp};
    }
    tda.get_to("TrapName", name);
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

bool deserialize(Trap& self, const GffStruct& archive)
{
    uint8_t temp;
    if (archive.get_to("TrapType", temp)) {
        self.type = TrapType::make(temp);
    }
    archive.get_to("TrapFlag", self.is_trapped);
    archive.get_to("DisarmDC", self.disarm_dc);
    archive.get_to("TrapDetectable", self.detectable);
    archive.get_to("TrapDetectDC", self.detect_dc);
    archive.get_to("TrapDisarmable", self.disarmable);
    archive.get_to("TrapOneShot", self.one_shot);

    return true;
}

bool serialize(const Trap& self, GffBuilderStruct& archive)
{
    archive.add_field("TrapFlag", self.is_trapped)
        .add_field("TrapType", uint8_t(*self.type))
        .add_field("DisarmDC", self.disarm_dc)
        .add_field("TrapDetectable", self.detectable)
        .add_field("TrapDetectDC", self.detect_dc)
        .add_field("TrapDisarmable", self.disarmable)
        .add_field("TrapOneShot", self.one_shot);

    return true;
}

} // namespace nw
