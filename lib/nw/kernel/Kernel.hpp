#pragma once

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
    void provide(Strings* strings);

    /// Shutdown all services
    void shutdown();

    /// Start all services, if no services are provided, default services will be used.
    void start();

    friend Strings& strings();

private:
    std::unique_ptr<Strings> strings_;
};

namespace detail {
static Services s_services;
} // namespace detail

Services& services();
Strings& strings();

} // namespace nw::kernel
