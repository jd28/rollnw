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
}

void Services::start()
{
    if (!strings_) {
        strings_ = std::make_unique<Strings>();
    }
}

Services& services() { return detail::s_services; }
Config& config() { return detail::s_config; }
Strings& strings() { return *detail::s_services.strings_.get(); }

} // namespace nw::kernel
