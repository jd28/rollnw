#include "FactionSystem.hpp"

#include "Resources.hpp"

#include <limits>

using namespace std::literals;

namespace nw::kernel {

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

void FactionSystem::clear()
{
    factions_.reset();
    name_id_map_.clear();
}

std::vector<std::string> FactionSystem::all() const
{
    std::vector<std::string> result;
    for (auto& fac : factions_->factions) {
        result.push_back(fac.name);
    }
    return result;
}

size_t FactionSystem::count() const
{
    return factions_ ? factions_->factions.size() : 0;
}

uint32_t FactionSystem::faction_id(std::string_view name) const
{
    absl::string_view sv{name.data(), name.size()};
    auto it = name_id_map_.find(sv);
    if (it == std::end(name_id_map_)) {
        return std::numeric_limits<uint32_t>::max();
    }
    return it->second;
}

Reputation FactionSystem::locate(uint32_t faction1, uint32_t faction2) const
{
    Reputation result;
    Reputation needle{faction1, faction2, 0};
    auto it = std::lower_bound(std::begin(factions_->reputations), std::end(factions_->reputations), needle);
    if (it == std::end(factions_->reputations)) {
        LOG_F(INFO, "not found");
        return result;
    }
    if (it->faction_1 == faction1 && it->faction_2 == faction2) {
        LOG_F(INFO, "found");
        return *it;
    } else {
        LOG_F(INFO, "not found: {} {}", it->faction_1, it->faction_2);
    }
    return result;
}

std::string_view FactionSystem::name(uint32_t id) const
{
    if (id >= factions_->factions.size()) { return {}; }
    return factions_->factions[id].name;
}

uint32_t FactionSystem::reputation(uint32_t faction1, uint32_t faction2) const
{
    auto rep = locate(faction1, faction2);
    return rep.reputation;
}

} // nw::kernel
