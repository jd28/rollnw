#pragma once

#include "../util/game_install.hpp"
#include "Config.hpp"
#include "Objects.hpp"
#include "Resources.hpp"
#include "Rules.hpp"
#include "Strings.hpp"

#include <flecs/flecs.h>

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
    void provide(Objects* objects);
    void provide(Resources* resources);
    void provide(Rules* rules);
    void provide(Strings* strings);

    // Sets game profile
    void set_profile(const GameProfile* profile);

    /// Shutdown all services
    void shutdown();

    /// Start all services, if no services are provided, default services will be used.
    void start(bool fail_hard = true);
    bool started();

    friend Config& config();
    friend Objects& objects();
    friend Resources& resman();
    friend Rules& rules();
    friend Strings& strings();
    friend flecs::world& world();

private:
    std::unique_ptr<Objects> objects_;
    std::unique_ptr<Resources> resources_;
    std::unique_ptr<Rules> rules_;
    std::unique_ptr<Strings> strings_;
    flecs::world world_;
    std::unique_ptr<const GameProfile> profile_;

    bool started_;
};

namespace detail {
static Config s_config;
static Services s_services;
} // namespace detail

Services& services();
Config& config();
Objects& objects();
Resources& resman();
Rules& rules();
Strings& strings();
flecs::world& world();

/**
 * @brief
 *
 * @param profile
 * @param path
 * @param manifest
 * @return `nullptr` on error
 */
Module* load_module(const GameProfile* profile,
    const std::filesystem::path& path,
    std::string_view manifest = {});

void unload_module();

} // namespace kernel
} // namespace nw
