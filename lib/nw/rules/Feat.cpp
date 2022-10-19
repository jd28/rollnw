#include "Feat.hpp"

#include "../components/CreatureStats.hpp"
#include "../formats/TwoDA.hpp"
#include "../kernel/Kernel.hpp"

namespace nw {

FeatInfo::FeatInfo(const TwoDARowView& tda)
{
    std::string temp_string;
    int temp_int;
    if (!tda.get_to("label", temp_string)) {
        return;
    }

    tda.get_to("FEAT", name);
    tda.get_to("DESCRIPTION", description);
    if (tda.get_to("ICON", temp_string)) {
        icon = {temp_string, nw::ResourceType::texture};
    }
    tda.get_to("ALLCLASSESCANUSE", all_can_use);
    tda.get_to("CATEGORY", category);
    tda.get_to("MAXCR", max_cr);
    tda.get_to("SPELLID", spell);
    if (tda.get_to("SUCCESSOR", temp_int)) {
        successor = Feat::make(temp_int);
    }
    tda.get_to("CRValue", cr_value);
    tda.get_to("USESPERDAY", uses);
    tda.get_to("MASTERFEAT", master);
    tda.get_to("TARGETSELF", target_self);

    if (tda.get_to("Constant", temp_string)) {
        constant = nw::kernel::strings().intern(temp_string);
    }

    tda.get_to("TOOLSCATEGORIES", tools_categories);
    tda.get_to("HostileFeat", hostile);
    tda.get_to("PreReqEpic", epic);
    tda.get_to("ReqAction", requires_action);
}

const FeatInfo* FeatArray::get(Feat feat) const noexcept
{
    if (feat.idx() < entries.size() && entries[feat.idx()].valid()) {
        return &entries[feat.idx()];
    } else {
        return nullptr;
    }
}

bool FeatArray::is_valid(Feat feat) const noexcept
{
    return feat.idx() < entries.size() && entries[feat.idx()].valid();
}

Feat FeatArray::from_constant(std::string_view constant) const
{
    absl::string_view v{constant.data(), constant.size()};
    auto it = constant_to_index.find(v);
    if (it == constant_to_index.end()) {
        return Feat::invalid();
    } else {
        return it->second;
    }
}

} // namespace nw
