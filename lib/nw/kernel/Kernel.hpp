#pragma once

#include "../util/templates.hpp"
#include "Config.hpp"

#include <functional>
#include <memory>
#include <typeindex>
#include <vector>

namespace nw {

// Forward decls
struct GameProfile;
struct Module;

namespace kernel {

struct EffectSystem;
struct EventSystem;
struct ObjectSystem;
struct Resources;
struct Rules;
struct ScriptSystem;
struct Strings;
struct TwoDACache;

struct Service {
    virtual ~Service() { }

    /// Initializes a service
    virtual void initialize(){};

    /// Clears a service
    virtual void clear(){};
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

    std::unique_ptr<Strings> strings;
    std::unique_ptr<Resources> resources;
    std::unique_ptr<TwoDACache> twoda_cache;
    std::unique_ptr<Rules> rules;
    std::unique_ptr<EffectSystem> effects;
    std::unique_ptr<ObjectSystem> objects;
    std::unique_ptr<EventSystem> events;

private:
    std::vector<ServiceEntry> services_;
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
Module* load_module(const std::filesystem::path& path, std::string_view manifest = {});

/// Unloads currently active module
void unload_module();

} // namespace kernel
} // namespace nw
