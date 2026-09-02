#pragma once

#include "Container.hpp"

#include "../formats/Image.hpp"
#include "../formats/Plt.hpp"
#include "../kernel/Kernel.hpp"

#include <cstdint>
#include <variant>

namespace nw {

struct Module;

enum class ModuleResourceFormat : uint8_t {
    invalid,
    legacy_gff,
    native_json,
};

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

    /// Demand the highest-priority 2DA below module and hak resources. This
    /// supports column-level compatibility when an overriding 2DA predates a
    /// column present in the installed game data.
    ResourceData demand_base_twoda(Resref resref) const;

    /// Demand some resource by resource priority
    ResourceData demand_in_order(Resref resref, std::initializer_list<ResourceType::type> restypes) const;

    /// Loads container resources for a module
    bool load_module(std::filesystem::path path);

    /// Loads module haks from the configured user hak directory.
    size_t load_module_haks(const Vector<String>& haks);

    /// Loads module haks from the given search roots in order.
    size_t load_module_haks(const Vector<String>& haks, const Vector<std::filesystem::path>& roots);

    /// Logs service metrics
    nlohmann::json stats() const override;

    /// Gets module container
    Container* module_container() const;

    /// Gets the number of module hak containers opened for the active module.
    size_t module_hak_count() const noexcept { return module_haks_.size(); }

    /// Gets the format selected when the module container was opened.
    ModuleResourceFormat module_format() const noexcept { return module_format_; }

    /// Unloads module
    void unload_module();

    /// Returns true if the registry has been built and frozen.
    bool is_frozen() const noexcept { return frozen_; }

    /// Monotonic generation of the visible resource registry. Consumers that
    /// cache demanded resource data use this to detect registry replacement.
    [[nodiscard]] uint64_t generation() const noexcept { return generation_; }

    /// Clears frozen state and registry so new containers can be added and the registry rebuilt.
    void unfreeze();

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
    void advance_generation();
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

    /// Winning resources from `game_`, restricted to 2DAs so compatibility
    /// lookup does not duplicate the complete visible-resource registry.
    ResourceRegistry base_twoda_registry_;

    // currentgame, savegame, nwsync_savegame - Not dealing with this for now..

    std::filesystem::path module_path_;
    ModuleResourceFormat module_format_ = ModuleResourceFormat::invalid;

    std::array<std::unique_ptr<Image>, plt_layer_size> palette_textures_;

    ResourceRegistry registry_;
    uint64_t generation_ = 1;
    bool frozen_ = false;
};

} // namespace nw
