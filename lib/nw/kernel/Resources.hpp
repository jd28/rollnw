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

struct ResroucesStats {
    /// Resman Initialization time in Milliseconds
    int64_t initialization_time = 0;
};

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
    const static std::type_index type_index;

    Resources(MemoryResource* memory, const Resources* parent = nullptr);
    virtual ~Resources() = default;

    using SearchVector = Vector<LocatorPayload>;

    /// Initializes resources management system
    virtual void initialize(ServiceInitTime time) override;

    /// Add a base container
    /// @note This anything that is BELOW the module in priority
    bool add_base_container(const std::filesystem::path& path, const String& name,
        ResourceType::type restype = ResourceType::invalid);

    /// Add already created container
    /// @note These containers are above all others in priority
    bool add_custom_container(Container* container, bool take_ownership = true,
        ResourceType::type restype = ResourceType::invalid);

    /// Add override container
    /// @note This anything that is ABOVE the module in priority
    bool add_override_container(const std::filesystem::path& path, const String& name,
        ResourceType::type restype = ResourceType::invalid);

    /// Clears any custom loaded containers
    void clear_containers();

    /// Loads container resources for a module
    bool load_module(std::filesystem::path path, StringView manifest = {});

    /// Loads module haks
    void load_module_haks(const Vector<String>& haks);

    /// Gets module container
    Container* module_container() const;

    /// Gets module haks
    Vector<Container*> module_haks() const;

    /// Unloads module
    void unload_module();

    /// Demands a player character file
    ResourceData demand_server_vault(StringView cdkey, StringView resref);

    /// Attempts to locate first matching resource type by container priority
    ResourceData demand_any(Resref resref, std::initializer_list<ResourceType::type> restypes) const;

    /// Attempts to locate first matching resource by resource type priority
    ResourceData demand_in_order(Resref resref, std::initializer_list<ResourceType::type> restypes) const;

    void load_palette_textures();
    Image* palette_texture(PltLayer layer);

    const ResroucesStats& metrics() const;

    /// Loads a texture from the resource manager
    /// @note This is a wrapper around ``demand_in_order`` with types dds, and tga passed.
    /// plt is not included here or are other image types, png, etc.
    Image* texture(Resref resref) const;

    virtual Vector<ResourceDescriptor> all() const override
    {
        return {};
    }

    virtual bool contains(Resource res) const override;
    virtual ResourceData demand(Resource res) const override;
    virtual int extract(const std::regex& pattern, const std::filesystem::path& output) const override;
    virtual const String& name() const override { return name_; }
    virtual const String& path() const override { return name_; }
    virtual size_t size() const override;
    virtual ResourceDescriptor stat(const Resource& res) const override;
    virtual bool valid() const noexcept override { return true; }
    virtual void visit(std::function<void(const Resource&)> callback, std::initializer_list<ResourceType::type> types = {}) const noexcept override;

private:
    void update_container_search();

    const String name_{"resman"};
    const Resources* parent_ = nullptr;
    ResroucesStats metrics_;

    NWSync nwsync_;

    SearchVector search_;

    Vector<LocatorPayload> custom_;
    Vector<LocatorPayload> override_;

    unique_container module_;
    Vector<unique_container> module_haks_;

    Vector<LocatorPayload> game_;

    NWSyncManifest* nwsync_manifest_ = nullptr;

    // currentgame, savegame, nwsync_savegame - Not dealing with this for now..

    std::filesystem::path module_path_;

    std::array<std::unique_ptr<Image>, plt_layer_size> palette_textures_;
};

inline Resources& resman()
{
    auto res = services().get_mut<Resources>();
    if (!res) {
        throw std::runtime_error("kernel: unable to load resources service");
    }
    return *res;
}

inline Container* resolve_container(const std::filesystem::path& p, const String& name)
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
