#include "Kernel.hpp"

#include "../log.hpp"
#include "../objects/Module.hpp"
#include "../profiles/GameProfile.hpp"
#include "components/IndexRegistry.hpp"
#include "components/TwoDACache.hpp"

namespace nw::kernel {

Services::Services()
    : world_{}
{
}
Services::~Services() { }

void Services::provide(ObjectSystem* objects)
{
    if (started_) {
        LOG_F(ERROR, "Services are unable to be provided after start");
        return;
    } else if (objects_) {
        LOG_F(WARNING, "Objects service already loaded, overriding...");
    }
    objects_ = std::unique_ptr<ObjectSystem>(objects);
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

bool Services::set_profile(const GameProfile* profile)
{
    profile_.reset(profile);
    if (!profile->load_constants()) return false;
    profile->load_components();
    profile->load_rules();
    return true;
}

void Services::shutdown()
{
    // Opposite start order
    rules_.reset();
    objects_.reset();
    resources_.reset();
    strings_.reset();

    world().get_mut<IndexRegistry>()->clear();
    world().get_mut<TwoDACache>()->clear();

    started_ = false;
}

void Services::start(bool fail_hard)
{
    ROLLNW_UNUSED(fail_hard);

    if (started_) {
        LOG_F(WARNING, "Services have already been started...");
        return;
    }

    world().add<IndexRegistry>();
    world().add<TwoDACache>();

    if (!objects_) {
        objects_ = std::make_unique<ObjectSystem>();
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
    // objects_->initialize();
    rules_->initialize();

    started_ = true;
}

bool Services::started()
{
    return started_;
}

Services& services() { return detail::s_services; }
Config& config() { return detail::s_config; }
ObjectSystem& objects() { return *detail::s_services.objects_.get(); }
Resources& resman() { return *detail::s_services.resources_.get(); }
Rules& rules() { return *detail::s_services.rules_.get(); }
Strings& strings() { return *detail::s_services.strings_.get(); }
flecs::world& world() { return detail::s_services.world_; }

bool load_profile(const GameProfile* profile)
{
    return services().set_profile(profile);
}

flecs::entity load_module(const std::filesystem::path& path, std::string_view manifest)
{
    flecs::entity mod;

    resman().load_module(path, manifest);
    mod = objects().make_module();
    if (objects().valid(mod)) {
        Module::instantiate(mod);
    }

    return mod;
}

void unload_module()
{
    rules().clear();
    objects().clear();
    resman().unload_module();
    strings().unload_custom_tlk();

    world() = flecs::world();
}

} // namespace nw::kernel
