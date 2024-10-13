#include "FactionSystem.hpp"

#include "Resources.hpp"

#include <limits>

using namespace std::literals;

namespace nw::kernel {

const std::type_index FactionSystem::type_index{typeid(FactionSystem)};

FactionSystem::FactionSystem(MemoryResource* scope)
    : Service(scope)
{
}

void FactionSystem::initialize(ServiceInitTime time)
{
    if (time != ServiceInitTime::module_post_load) { return; }
    auto rd = nw::kernel::resman().demand({"repute"sv, nw::ResourceType::fac});
    if (rd.bytes.size() == 0) {
        throw std::runtime_error("[factions] unable to load 'repute.fac'");
    }
    factions_ = std::make_unique<Faction>(std::move(rd));
    std::sort(std::begin(factions_->reputations), std::end(factions_->reputations));
    name_id_map_.reserve(factions_->factions.size());
    for (uint32_t i = 0; i < factions_->factions.size(); ++i) {
        name_id_map_.emplace(factions_->factions[i].name, i);
    }
}

Vector<String> FactionSystem::all() const
{
    Vector<String> result;
    if (!factions_) { return result; }

    for (auto& fac : factions_->factions) {
        result.push_back(fac.name);
    }
    return result;
}

size_t FactionSystem::count() const
{
    return factions_ ? factions_->factions.size() : 0;
}

uint32_t FactionSystem::faction_id(StringView name) const
{
    if (!factions_) { return std::numeric_limits<uint32_t>::max(); }

    auto it = name_id_map_.find(name);
    if (it == std::end(name_id_map_)) {
        return std::numeric_limits<uint32_t>::max();
    }
    return it->second;
}

Reputation FactionSystem::locate(uint32_t faction1, uint32_t faction2) const
{
    Reputation result;
    if (!factions_) { return result; }

    Reputation needle{faction1, faction2, 0};
    auto it = std::lower_bound(std::begin(factions_->reputations), std::end(factions_->reputations), needle);
    if (it == std::end(factions_->reputations)) {
        return result;
    }
    if (it->faction_1 == faction1 && it->faction_2 == faction2) {
        return *it;
    }
    return result;
}

StringView FactionSystem::name(uint32_t id) const
{
    if (!factions_ || id >= factions_->factions.size()) { return {}; }
    return factions_->factions[id].name;
}

uint32_t FactionSystem::reputation(uint32_t faction1, uint32_t faction2) const
{
    if (!factions_) { return 0; }
    auto rep = locate(faction1, faction2);
    return rep.reputation;
}

} // nw::kernel
