#pragma once

#include "../resources/Resource.hpp"
#include "../rules/rule_type.hpp"
#include "../serialization/Serialization.hpp"

namespace nw {

struct TwoDARowView;

DECLARE_RULE_TYPE(TrapType);

struct TrapInfo {
    TrapInfo(const TwoDARowView& tda);

    nw::Resref script;          // TrapScript
                                // SetDC
                                // DetectDCMod
                                // DisarmDCMod
    uint32_t name = 0xFFFFFFFF; // TrapName
                                // ResRef
                                // IconResRef
                                // Constant

    bool valid() const noexcept { return name != 0xFFFFFFFF; }
};

using TrapArray = RuleTypeArray<TrapType, TrapInfo>;

struct Trap {
    Trap() = default;

    bool from_json(const nlohmann::json& archive);
    nlohmann::json to_json() const;

    bool is_trapped = false;
    TrapType type = TrapType::invalid();
    bool detectable = false;
    uint8_t detect_dc = 0;
    bool disarmable = false;
    uint8_t disarm_dc = 0;
    bool one_shot = false;
};

bool deserialize(Trap& self, const GffStruct& archive);
bool serialize(const Trap& self, GffBuilderStruct& archive);

} // namespace nw
