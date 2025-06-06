#pragma once

#include "Container.hpp"

#include "../formats/Image.hpp"
#include "../formats/Plt.hpp"
#include "../kernel/Kernel.hpp"

#include <variant>

namespace nw {

struct Module;

using LocatorVariant = std::variant<Container*, unique_container>;

struct LocatorPayload {
    LocatorPayload(LocatorVariant container_, ResourceType::type restype_)
        : container(std::move(container_))
        , restype(restype_)
    {
    }

    LocatorPayload(LocatorPayload&&) = default;
    LocatorPayload(const LocatorPayload&) = delete;

    LocatorVariant container;
    ResourceType::type restype = ResourceType::invalid;
};

struct ResourceManager final : public kernel::Service {
    const static std::type_index type_index;

    ResourceManager(MemoryResource* memory, const ResourceManager* parent = nullptr);
    virtual ~ResourceManager() = default;

    using SearchVector = Vector<LocatorPayload>;

    /// Initializes resources management system
    virtual void initialize(kernel::ServiceInitTime time) override;

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

    /// Builds resource main registry
    void build_registry();

    /// Determines if a resource is in the resource manager
    bool contains(Resource uri) const noexcept;

    /// Demand some resource
    ResourceData demand(Resource uri) const;

    /// Demand some resource by resource priority
    ResourceData demand_in_order(Resref resref, std::initializer_list<ResourceType::type> restypes) const;

    /// Loads container resources for a module
    bool load_module(std::filesystem::path path);

    /// Loads module haks
    void load_module_haks(const Vector<String>& haks);

    /// Logs service metrics
    nlohmann::json stats() const override;

    /// Gets module container
    Container* module_container() const;

    /// Unloads module
    void unload_module();

    /// Demands a player character file
    ResourceData demand_server_vault(StringView cdkey, StringView resref);

    /// Gets cached palette texture
    Image* palette_texture(PltLayer layer);

    /// Loads a texture from the resource manager
    /// @note This is a wrapper around ``demand_in_order`` with types dds, and tga passed.
    /// plt is not included here or are other image types, png, etc.
    Image* texture(Resref resref) const;

    const String& name() const { return name_; }
    size_t size() const;
    bool valid() const noexcept { return true; }

    /// Executes callback on all assets in the resource registry
    void visit(std::function<void(Resource)> visitor) const;

private:
    void load_palette_textures();
    void update_container_search();

    const String name_{"resman"};
    const ResourceManager* parent_ = nullptr;

    SearchVector search_;

    Vector<LocatorPayload> custom_;
    Vector<LocatorPayload> override_;

    unique_container module_;
    Vector<unique_container> module_haks_;

    Vector<LocatorPayload> game_;

    // currentgame, savegame, nwsync_savegame - Not dealing with this for now..

    std::filesystem::path module_path_;

    std::array<std::unique_ptr<Image>, plt_layer_size> palette_textures_;

    ResourceRegistry registry_;
    bool frozen_ = false;
};

} // namespace nw
