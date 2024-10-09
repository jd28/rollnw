#pragma once

#include "../config.hpp"
#include "../util/templates.hpp"
#include "Config.hpp"

#include <functional>
#include <memory>
#include <typeindex>

namespace nw {

// Forward decls
struct GameProfile;
struct Module;

namespace kernel {

/// Guides services on load order.
enum struct ServiceInitTime {
    /// Immediately after the kernel is started with ``nw::kernel::start()``.
    /// @note example: The resource manager loading base game resources.
    kernel_start,

    /// Before module is constructed and assets, i.e. haks/tlks, have been loaded.
    /// @note example: Initializing the object system, since the module itself, must be
    /// constructed via that system.
    module_pre_load,

    /// After module assets have been loaded, i.e. haks, tlk. but before any areas have been loaded
    /// @note example: The rules system needs to be initialized here that will effect all other objects,
    /// creatures, etc, when constructed during module instantiation
    module_post_load,

    /// After the module has been instantiated, i.e. all areas have been constructed.
    /// @note example:
    module_post_instantiation,
};

/// Abstract base class of all services
struct Service {
    virtual ~Service() = default;

    /// Initializes a service
    /// @note Every service will be called with as many values as exist in ``ServiceInitTime``.
    /// it's up to the service itself to filter/ignore what's not relevant to them.
    virtual void initialize(ServiceInitTime /*time*/){};

    /// Clears a service
    virtual void clear() { };
};

struct ServiceEntry {
    std::type_index index;
    std::unique_ptr<Service> service;
};

struct Services {
    Services();

    /// Initializes kernel services
    void start();

    /// Shutsdown kernel services
    void shutdown();

    /// Gets current game profile
    GameProfile* profile() const;

    /// Adds a service
    template <typename T>
    T* add();

    /// Gets a service
    template <typename T>
    const T* get() const;

    /// Gets a service as non-const
    template <typename T>
    T* get_mut();

    friend Module* load_module(const std::filesystem::path& path, bool instantiate);
    friend void unload_module();

private:
    Vector<ServiceEntry> services_;
    std::unique_ptr<GameProfile> profile_;
};

template <typename T>
T* Services::add()
{
    T* service = get_mut<T>();
    if (!service) {
        service = new T;
        services_.push_back({std::type_index(typeid(T)), std::unique_ptr<Service>(service)});
    }
    return service;
}

template <typename T>
const T* Services::get() const
{
    auto ti = std::type_index(typeid(T));
    for (auto& s : services_) {
        if (s.index == ti) {
            return static_cast<const T*>(s.service.get());
        }
    }
    return nullptr;
}

template <typename T>
T* Services::get_mut()
{
    auto ti = std::type_index(typeid(T));
    for (auto& s : services_) {
        if (s.index == ti) {
            return static_cast<T*>(s.service.get());
        }
    }
    return nullptr;
}

/// Gets configuration options
Config& config();

/// Gets services
Services& services();

/// Loads a module
/// If instantiate is false, no areas are loaded and service init at `` module_post_instantiation``
/// is not called.
Module* load_module(const std::filesystem::path& path, bool instantiate = true);

/// Unloads currently active module
void unload_module();

} // namespace kernel
} // namespace nw
