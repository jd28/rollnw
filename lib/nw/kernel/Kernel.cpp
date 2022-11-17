#include "Kernel.hpp"

#include "../components/Module.hpp"
#include "../log.hpp"
#include "EffectSystem.hpp"
#include "Objects.hpp"
#include "ParsedScriptCache.hpp"
#include "Resources.hpp"
#include "Rules.hpp"
#include "Strings.hpp"
#include "TwoDACache.hpp"

namespace nw::kernel {

namespace detail {
Config s_config;
Services s_services;
} // namespace detail

Services::Services()
{
    LOG_F(INFO, "kernel: initializing default services");
    services().add<Strings>();
    services().add<Resources>();
    services().add<ObjectSystem>();
    services().add<Rules>();
    services().add<EffectSystem>();
    services().add<ParsedScriptCache>();
    services().add<TwoDACache>();
}

Config& config() { return detail::s_config; }
Services& services() { return detail::s_services; }

void load_profile(const GameProfile* profile)
{
    detail::s_services.set_profile(profile);
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
