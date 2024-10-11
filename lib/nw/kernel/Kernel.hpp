#pragma once

#include "../config.hpp"
#include "../util/memory.hpp"
#include "../util/templates.hpp"
#include "Config.hpp"

#include <array>
#include <functional>
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
    virtual void clear() {};

    /// Gets the memory scope for the servce
    MemoryScope* scope() const noexcept;

    /// Gets the memory scope for the servce
    void set_scope(MemoryScope* scope) noexcept;

private:
    MemoryScope* scope_ = nullptr;
};

struct ServiceEntry {
    std::type_index index = typeid(int);
    Service* service = nullptr;
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
    std::array<ServiceEntry, 32> services_;
    size_t services_count_ = 0;
    GameProfile* profile_ = nullptr;
    MemoryArena kernel_arena_;
    MemoryScope kernel_scope_;
    MemoryScope* service_scope_ = nullptr;
};

template <typename T>
T* Services::add()
{
    T* service = get_mut<T>();
    if (!service) {
        service = kernel_scope_.alloc_obj<T>();
        CHECK_F(services_count_ < 32, "Only 32 total services are allowed");
        services_[services_count_] = ServiceEntry{T::type_index, service};
        ++services_count_;
    }
    return service;
}

template <typename T>
const T* Services::get() const
{
    for (auto& s : services_) {
        if (!s.service) { break; }
        if (s.index == T::type_index) {
            return static_cast<const T*>(s.service);
        }
    }
    return nullptr;
}

template <typename T>
T* Services::get_mut()
{
    for (auto& s : services_) {
        if (!s.service) { break; }
        if (s.index == T::type_index) {
            return static_cast<T*>(s.service);
        }
    }
    return nullptr;
}

/// Gets configuration options
Config& config();

/// Gets services
Services& services();

/// Loads a module
/// If instantiate is false, no areas are loaded and Service::initialize at ``module_post_instantiation``
/// is not called.
Module* load_module(const std::filesystem::path& path, bool instantiate = true);

/// Unloads currently active module
void unload_module();

static thread_local MemoryArena tsl_arena_(MB(1));
static thread_local MemoryScope tsl_scope(&tsl_arena_);

} // namespace kernel
} // namespace nw
