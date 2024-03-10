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

    strings->initialize();
    resources->initialize();
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
    resman().load_module(path, manifest);

    Module* mod = objects().make_module();
    if (mod) {
        mod->instantiate();
    }

    return mod;
}

void unload_module()
{
    objects().clear();
    resman().unload_module();
    strings().unload_custom_tlk();
}

} // namespace nw::kernel
