#include "Kernel.hpp"

#include "EffectSystem.hpp"
#include "EventSystem.hpp"
#include "FactionSystem.hpp"
#include "ModelCache.hpp"
#include "Objects.hpp"
#include "Resources.hpp"
#include "Rules.hpp"
#include "Strings.hpp"
#include "TilesetRegistry.hpp"
#include "TwoDACache.hpp"

#include "../profiles/nwn1/Profile.hpp"

namespace nw::kernel {

// == Service =================================================================

Service::Service(MemoryResource* memory)
    : memory_{memory}
{
}

MemoryResource* Service::allocator() const noexcept
{
    return memory_;
}

Services::Services()
    : kernel_arena_(MB(16))
    , kernel_scope_(&kernel_arena_)
{
}

void Services::start()
{
    if (serices_started_) { return; }

    if (!profile_) {
        if (config().version() == GameVersion::vEE
            || config().version() == GameVersion::v1_69) {
            profile_ = kernel_scope_.alloc_obj<nwn1::Profile>();
        } else {
            std::runtime_error("currently selected game version is unsupported");
        }
    }

    // Load all the default services.
    load_services();

    if (!module_loading_) {
        for (auto& entry : services_) {
            if (!entry.service) { break; }
            entry.service->initialize(ServiceInitTime::kernel_start);
        }
    }

    serices_started_ = true;
}

GameProfile* Services::profile() const
{
    return profile_;
}

void Services::shutdown()
{
    if (!serices_started_) { return; }
    services_ = std::array<ServiceEntry, 32>{};
    kernel_scope_.reset();
    profile_ = nullptr;
    services_count_ = 0;
    serices_started_ = false;
}

void Services::load_services()
{
    // The ordering here is important.  Pretty much everything depends on strings and resman
    add<Strings>();
    add<Resources>();
    add<TwoDACache>();
    add<EffectSystem>();
    add<Rules>();
    add<ObjectSystem>();
    add<EventSystem>();
    add<ModelCache>();
    add<TilesetRegistry>();
    add<FactionSystem>();

    profile_->load_custom_services();
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

Module* load_module(const std::filesystem::path& path, bool instantiate)
{
    services().module_loading_ = true;
    unload_module(); // Always unload, just in case.

    auto start = std::chrono::high_resolution_clock::now();
    services().start();

    for (auto& s : services().services_) {
        if (!s.service) { break; }
        s.service->initialize(ServiceInitTime::module_pre_load);
    }

    resman().load_module(path, "");

    Module* mod = objects().make_module();
    if (mod->haks.size()) {
        nw::kernel::resman().load_module_haks(mod->haks);
    }

    if (mod->tlk.size()) {
        auto path = nw::kernel::config().user_path() / "tlk";
        nw::kernel::strings().load_custom_tlk(path / (mod->tlk + ".tlk"));
    }

    for (auto& s : services().services_) {
        if (!s.service) { break; }
        s.service->initialize(ServiceInitTime::module_post_load);
    }

    if (instantiate) {
        if (mod) {
            mod->instantiate();
        }

        for (auto& s : services().services_) {
            if (!s.service) { break; }
            s.service->initialize(ServiceInitTime::module_post_instantiation);
        }
    }

    services().module_loaded_ = true;
    services().module_loading_ = false;

    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    auto count = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    LOG_F(INFO, "kernel: loaded module '{}' in {}ms", mod ? strings().get(mod->name) : "???", count);
    return mod;
}

void set_game_profile(GameProfile* profile)
{
    CHECK_F(!services().serices_started_, "[kernel] attempting set game profile after services have been started.");
    services().profile_ = profile;
}

void unload_module()
{
    // Since everything is getting nuked, it's not necessary to call Module::destroy
    services().shutdown();
    services().module_loaded_ = false;
}

} // namespace nw::kernel
