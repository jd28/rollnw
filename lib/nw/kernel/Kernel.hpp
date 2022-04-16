#pragma once

#include "../util/game_install.hpp"
#include "Config.hpp"
#include "Objects.hpp"
#include "Resources.hpp"
#include "Strings.hpp"

#include <memory>

namespace nw::kernel {

struct Services {
    Services() = default;
    Services(const Services&) = delete;
    Services(Services&&) = delete;
    Services& operator=(const Services&) = delete;
    Services& operator=(Services&&) = delete;

    /// Provide global service
    void provide(Objects* objects);
    void provide(Resources* resources);
    void provide(Strings* strings);

    /// Shutdown all services
    void shutdown();

    /// Start all services, if no services are provided, default services will be used.
    void start(bool fail_hard = true);
    bool started();

    friend Config& config();
    friend Objects& objects();
    friend Resources& resman();
    friend Strings& strings();

private:
    std::unique_ptr<Objects> objects_;
    std::unique_ptr<Resources> resources_;
    std::unique_ptr<Strings> strings_;

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
Strings& strings();

Module* load_module(const std::filesystem::path& path, std::string_view manifest = {});
void unload_module();

} // namespace nw::kernel
