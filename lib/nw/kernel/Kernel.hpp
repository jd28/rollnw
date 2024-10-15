#pragma once

#include "../config.hpp"
#include "../util/memory.hpp"
#include "Config.hpp"

#include <array>
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
    Service(MemoryResource* memory_);
    virtual ~Service() {};

    /// Gets the memory allocator for the servce
    MemoryResource* allocator() const noexcept;

    /// Initializes a service
    /// @note Every service will be called with as many values as exist in ``ServiceInitTime``.
    /// it's up to the service itself to filter/ignore what's not relevant to them.
    virtual void initialize(ServiceInitTime /*time*/){};

private:
    MemoryResource* memory_ = nullptr;
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

    friend GlobalMemory* global_allocator();
    friend void set_game_profile(GameProfile*);
    friend Module* load_module(const std::filesystem::path& path, bool instantiate);
    friend void unload_module();

private:
    std::array<ServiceEntry, 32> services_;
    size_t services_count_ = 0;
    GameProfile* profile_ = nullptr;
    GlobalMemory global_alloc_;
    MemoryArena kernel_arena_;
    MemoryScope kernel_scope_;
    bool serices_started_ = false;
    bool module_loaded_ = false;
    bool module_loading_ = false;

    void load_services();
};

template <typename T>
T* Services::add()
{
    T* service = get_mut<T>();
    if (!service) {
        service = kernel_scope_.alloc_obj<T>(&kernel_scope_);
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
[[nodiscard]] Config& config();

/// Gets services
[[nodiscard]] Services& services();

/// Loads a module
/// If instantiate is false, no areas are loaded and Service::initialize at ``module_post_instantiation``
/// is not called.
[[nodiscard]] Module* load_module(const std::filesystem::path& path, bool instantiate = true);

/// Unloads currently active module
void unload_module();

/// Sets game profile. **Must** be called before nw::kernel::services().start();
/// @note Caller retains ownerserhip of ``profile``.
void set_game_profile(GameProfile* profile);

} // namespace kernel
} // namespace nw
