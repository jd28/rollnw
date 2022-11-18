#pragma once

#include "../resources/Directory.hpp"
#include "../resources/Erf.hpp"
#include "../resources/Key.hpp"
#include "../resources/NWSync.hpp"
#include "../resources/Zip.hpp"
#include "Kernel.hpp"

#include <string_view>
#include <variant>

namespace nw {

struct Module;

namespace kernel {

using LocatorVariant = std::variant<Container*, unique_container>;

struct Resources : public Container, public Service {
    Resources() = default;
    virtual ~Resources() = default;

    using SearchVector = std::vector<std::tuple<const Container*, ResourceType::type, bool>>;

    /// Initializes resources management system
    virtual void initialize() override;
    virtual void clear() override { }

    /// Add already created container
    bool add_container(Container* container, bool take_ownership = true);

    /// Clears any custom loaded containers
    void clear_containers();

    /// Loads container resources for a module
    bool load_module(std::filesystem::path path, std::string_view manifest = {});

    /// Loads module haks
    void load_module_haks(const std::vector<std::string>& haks);

    /// Unloads module
    void unload_module();

    /// Demands a player character file
    ByteArray demand_server_vault(std::string_view cdkey, std::string_view resref);

    /// Attempts to locate first matching resource type by container priority
    std::pair<ByteArray, ResourceType::type>
    demand_any(Resref resref, std::initializer_list<ResourceType::type> restypes) const;

    /// Attempts to locate first matching resource by resource type priority
    std::pair<ByteArray, ResourceType::type>
    demand_in_order(Resref resref, std::initializer_list<ResourceType::type> restypes) const;

    virtual std::vector<ResourceDescriptor> all() const override { return {}; }
    virtual bool contains(Resource res) const override;
    virtual ByteArray demand(Resource res) const override;
    virtual int extract(const std::regex& pattern, const std::filesystem::path& output) const override;
    virtual const std::string& name() const override { return name_; }
    virtual const std::string& path() const override { return name_; }
    virtual size_t size() const override;
    virtual ResourceDescriptor stat(const Resource& res) const override;
    virtual bool valid() const noexcept override { return true; }

private:
    void update_container_search();

    const std::string name_{"resman"};

    NWSync nwsync_;

    SearchVector search_;

    std::vector<LocatorVariant> custom_;

    Directory ambient_user_, ambient_install_;
    Directory development_;
    Directory dmvault_user_, dmvault_install_;
    Directory localvault_user_, localvault_install_;
    Directory music_user_, music_install_;
    Directory override_user_, override_install_;
    Directory portraits_user_, portraits_install_;
    Directory servervault_user_, servervault_install_;

    std::vector<Erf> haks_;
    NWSyncManifest* nwsync_manifest_ = nullptr;

    // currentgame, savegame, nwsync_savegame - Not dealing with this for now..

    unique_container module_;
    std::filesystem::path module_path_;

    std::vector<unique_container> patches_;

    std::vector<Erf> texture_packs_;
    std::vector<Key> keys_;
};

inline Resources& resman()
{
    auto res = detail::s_services.get_mut<Resources>();
    return *res;
}

} // namespace kernel
} // namespace nw
