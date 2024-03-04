#pragma once

#include "../formats/Image.hpp"
#include "../formats/Plt.hpp"
#include "../log.hpp"
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

struct LocatorPayload {
    LocatorPayload(LocatorVariant container_, ResourceType::type restype_)
        : container(std::move(container_))
        , restype(restype_)
    {
    }
    LocatorVariant container;
    ResourceType::type restype = ResourceType::invalid;
};

struct Resources : public Container, public Service {
    Resources(const Resources* parent = nullptr);
    virtual ~Resources() = default;

    using SearchVector = std::vector<LocatorPayload>;

    /// Initializes resources management system
    virtual void initialize() override;
    virtual void clear() override { }

    /// Add a base container
    /// @note This anything that is BELOW the module in priority
    bool add_base_container(const std::filesystem::path& path, const std::string& name,
        ResourceType::type restype = ResourceType::invalid);

    /// Add already created container
    /// @note These containers are above all others in priority
    bool add_custom_container(Container* container, bool take_ownership = true,
        ResourceType::type restype = ResourceType::invalid);

    /// Add override container
    /// @note This anything that is ABOVE the module in priority
    bool add_override_container(const std::filesystem::path& path, const std::string& name,
        ResourceType::type restype = ResourceType::invalid);

    /// Clears any custom loaded containers
    void clear_containers();

    /// Loads container resources for a module
    bool load_module(std::filesystem::path path, std::string_view manifest = {});

    /// Loads module haks
    void load_module_haks(const std::vector<std::string>& haks);

    /// Unloads module
    void unload_module();

    /// Demands a player character file
    ResourceData demand_server_vault(std::string_view cdkey, std::string_view resref);

    /// Attempts to locate first matching resource type by container priority
    ResourceData demand_any(Resref resref, std::initializer_list<ResourceType::type> restypes) const;

    /// Attempts to locate first matching resource by resource type priority
    ResourceData demand_in_order(Resref resref, std::initializer_list<ResourceType::type> restypes) const;

    void load_palette_textures();
    Image* palette_texture(PltLayer layer);

    virtual std::vector<ResourceDescriptor> all() const override
    {
        return {};
    }
    virtual bool contains(Resource res) const override;
    virtual ResourceData demand(Resource res) const override;
    virtual int extract(const std::regex& pattern, const std::filesystem::path& output) const override;
    virtual const std::string& name() const override { return name_; }
    virtual const std::string& path() const override { return name_; }
    virtual size_t size() const override;
    virtual ResourceDescriptor stat(const Resource& res) const override;
    virtual bool valid() const noexcept override { return true; }
    virtual void visit(std::function<void(const Resource&)> callback) const noexcept override;

private:
    void update_container_search();

    const std::string name_{"resman"};
    const Resources* parent_ = nullptr;

    NWSync nwsync_;

    SearchVector search_;

    std::vector<LocatorPayload> custom_;
    std::vector<LocatorPayload> override_;

    unique_container module_;
    std::vector<unique_container> module_haks_;

    std::vector<LocatorPayload> game_;

    NWSyncManifest* nwsync_manifest_ = nullptr;

    // currentgame, savegame, nwsync_savegame - Not dealing with this for now..

    std::filesystem::path module_path_;

    std::array<std::unique_ptr<Image>, plt_layer_size> palette_textures_;
};

inline Resources& resman()
{
    auto res = services().resources.get();
    if (!res) {
        LOG_F(FATAL, "kernel: unable to load resources service");
    }
    return *res;
}

inline Container* resolve_container(const std::filesystem::path& p, const std::string& name)
{
    if (std::filesystem::is_directory(p / name)) {
        return new Directory{p / name};
    } else if (std::filesystem::exists(p / (name + ".hak"))) {
        return new Erf{p / (name + ".hak")};
    } else if (std::filesystem::exists(p / (name + ".erf"))) {
        return new Erf{p / (name + ".erf")};
    } else if (std::filesystem::exists(p / (name + ".zip"))) {
        return new Zip{p / (name + ".zip")};
    } else if (std::filesystem::exists(p / (name + ".key"))) {
        return new Key{p / (name + ".key")};
    }
    return nullptr;
}
} // namespace kernel
} // namespace nw
