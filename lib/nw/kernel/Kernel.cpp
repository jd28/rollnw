#include "Kernel.hpp"

#include "EffectSystem.hpp"
#include "EventSystem.hpp"
#include "Objects.hpp"
#include "Resources.hpp"
#include "Rules.hpp"
#include "Strings.hpp"
#include "TwoDACache.hpp"

#include "../profiles/nwn1/Profile.hpp"

namespace nw::kernel {

Services::Services()
    : strings{new Strings}
    , resources{new Resources}
    , twoda_cache{new TwoDACache}
    , rules{new Rules}
    , effects{new EffectSystem}
    , objects{new ObjectSystem}
    , events{new EventSystem}
{
    // LOG_F(INFO, "kernel: initializing default services");
}

void Services::start()
{
    if (config().version() == GameVersion::vEE
        || config().version() == GameVersion::v1_69) {
        profile_ = std::make_unique<nwn1::Profile>();
    } else {
        std::runtime_error("currently selected game version is unsupported");
    }

    resources->initialize();

    strings->initialize();
    twoda_cache->initialize();
    rules->initialize();
    effects->initialize();
    objects->initialize();
    events->initialize();

    for (auto& s : services_) {
        s.service->initialize();
    }
}

GameProfile* Services::profile() const
{
    return profile_.get();
}

void Services::shutdown()
{
    profile_.reset();

    events->clear();
    objects->clear();
    effects->clear();
    rules->clear();
    twoda_cache->clear();

    for (auto& s : reverse(services_)) {
        s.service->clear();
    }
}

Config& config()
{
    static Config s_config;
    return s_config;
}

Services& services()
{
    static Services s_services;
    return s_services;
}

Module* load_module(const std::filesystem::path& path, std::string_view manifest)
{
    services().events->clear();
    services().objects->clear();
    services().effects->clear();
    services().rules->clear();
    services().twoda_cache->clear();

    for (auto& s : reverse(services().services_)) {
        s.service->clear();
    }

    services().strings->initialize();
    services().twoda_cache->initialize();
    services().objects->initialize();
    services().events->initialize();
    for (auto& s : services().services_) {
        s.service->initialize();
    }

    resman().load_module(path, manifest);

    Module* mod = objects().make_module();
    if (mod->haks.size()) {
        nw::kernel::resman().load_module_haks(mod->haks);
    }

    if (mod->tlk.size()) {
        auto path = nw::kernel::config().user_path() / "tlk";
        nw::kernel::strings().load_custom_tlk(path / (mod->tlk + ".tlk"));
    }

    services().rules->initialize();
    services().effects->initialize();

    if (mod) {
        mod->instantiate();
    }

    return mod;
}

void unload_module()
{
    services().events->clear();
    services().objects->clear();
    services().effects->clear();
    services().rules->clear();
    services().twoda_cache->clear();

    for (auto& s : reverse(services().services_)) {
        s.service->clear();
    }

    resman().unload_module();
    strings().unload_custom_tlk();
}

} // namespace nw::kernel
