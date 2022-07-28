#pragma once

#include "../util/game_install.hpp"
#include "Config.hpp"
#include "Objects.hpp"
#include "Resources.hpp"
#include "Rules.hpp"
#include "Strings.hpp"
#include "fwd.hpp"

#include <functional>
#include <memory>

namespace nw {

struct GameProfile;

namespace kernel {

struct Services {
    Services();
    Services(const Services&) = delete;
    Services(Services&&) = delete;
    Services& operator=(const Services&) = delete;
    Services& operator=(Services&&) = delete;
    ~Services();

    /// Provide global service
    void provide(ObjectSystem* objects);
    void provide(Resources* resources);
    void provide(Rules* rules);
    void provide(Strings* strings);

    // Sets game profile
    bool set_profile(const GameProfile* profile);

    /// Shutdown all services
    void shutdown();

    /// Start all services, if no services are provided, default services will be used.
    void start(bool fail_hard = true);
    bool started();

    friend Config& config();
    friend ObjectSystem& objects();
    friend Resources& resman();
    friend Rules& rules();
    friend Strings& strings();

private:
    std::unique_ptr<ObjectSystem> objects_;
    std::unique_ptr<Resources> resources_;
    std::unique_ptr<Rules> rules_;
    std::unique_ptr<Strings> strings_;
    std::unique_ptr<const GameProfile> profile_;

    bool started_ = false;
};

namespace detail {
static Config s_config;
static Services s_services;
} // namespace detail

/// Loads game profile
bool load_profile(const GameProfile* profile);

/// Loads a module
Module* load_module(const std::filesystem::path& path, std::string_view manifest = {});

/// Unloads currently active module
void unload_module();

} // namespace kernel
} // namespace nw
