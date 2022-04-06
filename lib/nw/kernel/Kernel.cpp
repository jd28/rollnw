#include "Kernel.hpp"

#include "../log.hpp"

namespace nw::kernel {

void Services::provide(Objects* objects)
{
    if (started_) {
        LOG_F(WARNING, "Services are unable to be provided after start");
    }

    if (objects_) {
        LOG_F(WARNING, "Objects service already loaded, overriding...");
    }
    objects_ = std::unique_ptr<Objects>(objects);
}

void Services::provide(Resources* resources)
{
    if (resources_) {
        LOG_F(WARNING, "Objects service already loaded, overriding...");
    }
    resources_ = std::unique_ptr<Resources>(resources);
}

void Services::provide(Strings* strings)
{
    if (strings_) {
        LOG_F(WARNING, "Strings service already loaded, overriding...");
    }
    strings_ = std::unique_ptr<Strings>(strings);
}

void Services::shutdown()
{
    // Opposite start order
    objects_.reset();
    resources_.reset();
    strings_.reset();
    started_ = false;
}

void Services::start()
{
    if (!objects_) {
        objects_ = std::make_unique<Objects>();
    }

    if (!resources_) {
        resources_ = std::make_unique<Resources>();
    }

    if (!strings_) {
        strings_ = std::make_unique<Strings>();
    }

    strings_->initialize();
    resources_->initialize();
    objects_->initialize();

    started_ = true;
}

bool Services::started()
{
    return started_;
}

Services& services() { return detail::s_services; }
Config& config() { return detail::s_config; }
Objects& objects() { return *detail::s_services.objects_.get(); }
Resources& resman() { return *detail::s_services.resources_.get(); }
Strings& strings() { return *detail::s_services.strings_.get(); }

} // namespace nw::kernel
