#include "../formats/Faction.hpp"
#include "../log.hpp"
#include "Kernel.hpp"

#include <absl/container/flat_hash_map.h>

#include <string>

namespace nw::kernel {

struct FactionSystem : public Service {
    const static std::type_index type_index;

    FactionSystem(MemoryResource* scope);
    virtual void initialize(ServiceInitTime time) override;

    /// Gets all factions by name
    Vector<String> all() const;

    /// Gets number of factions
    size_t count() const;

    /// Gets faction ID by name
    uint32_t faction_id(StringView name) const;

    /// Gets the reputation between 2 factions
    Reputation locate(uint32_t faction1, uint32_t faction2) const;

    /// Gets faction name by ID
    StringView name(uint32_t) const;

    /// Gets the reputation value between 2 factions
    uint32_t reputation(uint32_t faction1, uint32_t faction2) const;

private:
    std::unique_ptr<Faction> factions_;
    absl::flat_hash_map<String, uint32_t> name_id_map_;
};

inline FactionSystem& factions()
{
    auto res = services().get_mut<FactionSystem>();
    if (!res) {
        throw std::runtime_error("kernel: unable to load faction service");
    }
    return *res;
}
} // namespace nw::kernel
