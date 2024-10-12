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
MemoryScope* Service::scope() const noexcept
{
    return scope_;
}

void Service::set_scope(MemoryScope* scope) noexcept
{
    scope_ = scope;
}

Services::Services()
    : kernel_arena_(MB(16))
    , kernel_scope_(&kernel_arena_)
    , service_scope_(nullptr)
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

    for (auto& entry : services_) {
        if (!entry.service) { break; }
        entry.service->initialize(ServiceInitTime::kernel_start);
    }
}

GameProfile* Services::profile() const
{
    return profile_;
}

void Services::shutdown()
{
    for (size_t i = services().services_count_; i > 0; --i) {
        services().services_[i - 1].service->clear();
    }
    // Wipe everything.
    kernel_scope_.reset();
    serices_started_ = false;
}

void Services::load_services()
{
    // The ordering here is important.  Pretty much everything depends on strings and resman
    add<Strings>();
    add<Resources>();
    add<TwoDACache>();
    add<Rules>();
    add<EffectSystem>();
    add<ObjectSystem>();
    add<EventSystem>();
    add<ModelCache>();
    add<TilesetRegistry>();
    add<FactionSystem>();

    profile_->load_custom_services();

    // After this point all
    service_scope_ = kernel_scope_.alloc_obj<MemoryScope>(&kernel_arena_);

    for (auto& entry : services_) {
        if (!entry.service) { break; }
        entry.service->set_scope(service_scope_);
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

Module* load_module(const std::filesystem::path& path, bool instantiate)
{
    // Always unload, just in case.
    unload_module();

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

    return mod;
}

void set_game_profile(GameProfile* profile)
{
    CHECK_F(!services().serices_started_, "[kernel] attempting set game profile after services have been started.");
    services().profile_ = profile;
}

void unload_module()
{
    for (size_t i = services().services_count_; i > 0; --i) {
        services().services_[i - 1].service->clear();
    }

    resman().unload_module();
    strings().unload_custom_tlk();
}

MemoryScope* tls_scratch()
{
    static thread_local MemoryArena tls_arena_(MB(1));
    static thread_local MemoryScope tls_scope_(&tls_arena_);
    return &tls_scope_;
}
} // namespace nw::kernel
