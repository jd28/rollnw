#include "Kernel.hpp"

#include "../log.hpp"
#include "../objects/Module.hpp"

namespace nw::kernel {

void Services::provide(Objects* objects)
{
    if (started_) {
        LOG_F(ERROR, "Services are unable to be provided after start");
        return;
    } else if (objects_) {
        LOG_F(WARNING, "Objects service already loaded, overriding...");
    }
    objects_ = std::unique_ptr<Objects>(objects);
}

void Services::provide(Resources* resources)
{
    if (started_) {
        LOG_F(ERROR, "Services are unable to be provided after start");
        return;
    } else if (resources_) {
        LOG_F(WARNING, "Objects service already loaded, overriding...");
    }
    resources_ = std::unique_ptr<Resources>(resources);
}

void Services::provide(Rules* rules)
{
    if (started_) {
        LOG_F(ERROR, "Services are unable to be provided after start");
        return;
    } else if (rules_) {
        LOG_F(WARNING, "Rules service already loaded, overriding...");
    }

    rules_ = std::unique_ptr<Rules>(rules);
}

void Services::provide(Strings* strings)
{
    if (started_) {
        LOG_F(ERROR, "Services are unable to be provided after start");
        return;
    }

    if (strings_) {
        LOG_F(WARNING, "Strings service already loaded, overriding...");
    }
    strings_ = std::unique_ptr<Strings>(strings);
}

void Services::shutdown()
{
    // Opposite start order
    rules_.reset();
    objects_.reset();
    resources_.reset();
    strings_.reset();
    started_ = false;
}

void Services::start(bool fail_hard)
{
    LIBNW_UNUSED(fail_hard);

    if (started_) {
        LOG_F(WARNING, "Services have already been started...");
        return;
    }

    if (!objects_) {
        objects_ = std::make_unique<Objects>();
    }

    if (!resources_) {
        resources_ = std::make_unique<Resources>();
    }

    if (!rules_) {
        rules_ = std::make_unique<Rules>();
    }

    if (!strings_) {
        strings_ = std::make_unique<Strings>();
    }

    strings_->initialize();
    resources_->initialize();
    objects_->initialize();
    rules_->initialize();

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
Rules& rules() { return *detail::s_services.rules_.get(); }
Strings& strings() { return *detail::s_services.strings_.get(); }
flecs::world& world() { return detail::s_services.world_; }

Module* load_module(const RuleProfile* profile,
    const std::filesystem::path& path,
    std::string_view manifest)
{
    world().add<ConstantRegistry>();

    if (!services().started()) { services().start(); }

    if (!profile) { return nullptr; }

    if (!profile->load_constants()) return nullptr;
    profile->load_compontents();

    resman().load_module(path, manifest);
    auto mod = objects().initialize_module();
    if (mod) {
        mod->instantiate();
    }
    rules().load(profile);

    return mod;
}

void unload_module()
{
    rules().clear();
    objects().clear();
    resman().unload_module();
    strings().unload_custom_tlk();

    world().get_mut<ConstantRegistry>()->clear();
}

} // namespace nw::kernel
