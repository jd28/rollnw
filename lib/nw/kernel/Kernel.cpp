#include "Kernel.hpp"

#include "../log.hpp"

namespace nw::kernel {

void Services::provide(Strings* strings)
{
    if (strings_) {
        LOG_F(WARNING, "Strings service already loaded, overriding...");
    }
    strings_ = std::unique_ptr<Strings>(strings);
}

void Services::shutdown()
{
    // Nothing for now...
    started_ = false;
}

void Services::start()
{
    if (!resources_) {
        resources_ = std::make_unique<Resources>();
    }
    if (!strings_) {
        strings_ = std::make_unique<Strings>();
    }

    strings_->initialize();
    resources_->initialize();
    started_ = true;
}

bool Services::started()
{
    return started_;
}

Services& services() { return detail::s_services; }
Config& config() { return detail::s_config; }
Resources& resman() { return *detail::s_services.resources_.get(); }
Strings& strings() { return *detail::s_services.strings_.get(); }

} // namespace nw::kernel
