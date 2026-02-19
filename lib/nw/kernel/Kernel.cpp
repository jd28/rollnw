#include "Kernel.hpp"

#include "../objects/ObjectManager.hpp"
#include "../profiles/nwn1/Profile.hpp"
#include "../resources/ResourceManager.hpp"
#include "../rules/effects.hpp"
#include "../smalls/runtime.hpp"
#include "../util/profile.hpp"
#include "EventSystem.hpp"
#include "FactionSystem.hpp"
#include "ModelCache.hpp"
#include "Rules.hpp"
#include "Strings.hpp"
#include "TilesetRegistry.hpp"
#include "TwoDACache.hpp"

namespace nw::kernel {

namespace {

const char* service_name(const ServiceEntry& entry)
{
    if (entry.index == Strings::type_index) { return "strings"; }
    if (entry.index == ResourceManager::type_index) { return "resources"; }
    if (entry.index == smalls::Runtime::type_index) { return "smalls.runtime"; }
    if (entry.index == TwoDACache::type_index) { return "twoda.cache"; }
    if (entry.index == EffectSystem::type_index) { return "effects"; }
    if (entry.index == Rules::type_index) { return "rules"; }
    if (entry.index == ObjectManager::type_index) { return "objects"; }
    if (entry.index == EventSystem::type_index) { return "events"; }
    if (entry.index == ModelCache::type_index) { return "model_cache"; }
    if (entry.index == TilesetRegistry::type_index) { return "tilesets"; }
    if (entry.index == FactionSystem::type_index) { return "factions"; }
    return "unknown";
}

void profile_service_init(ServiceEntry& entry, ServiceInitTime time)
{
    NW_PROFILE_SCOPE_N("kernel.service.initialize");
    NW_PROFILE_TEXT_CSTR(service_name(entry));
    switch (time) {
    case ServiceInitTime::kernel_start:
        NW_PROFILE_TEXT_CSTR("kernel_start");
        break;
    case ServiceInitTime::module_pre_load:
        NW_PROFILE_TEXT_CSTR("module_pre_load");
        break;
    case ServiceInitTime::module_post_load:
        NW_PROFILE_TEXT_CSTR("module_post_load");
        break;
    case ServiceInitTime::module_post_instantiation:
        NW_PROFILE_TEXT_CSTR("module_post_instantiation");
        break;
    }
    entry.service->initialize(time);
}

} // namespace

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
    : kernel_arena_(MB(512))
    , kernel_scope_(&kernel_arena_)
{
}

void Services::set_module_loading(bool value)
{
    module_loading_ = value;
}

void Services::create()
{
    if (services_created_) { return; }
    if (!profile_) {
        if (config().profile() == "nwn1"
            && (config().version() == GameVersion::vEE
                || config().version() == GameVersion::v1_69)) {
            profile_ = kernel_scope_.alloc_obj<nwn1::Profile>();
        } else {
            throw std::runtime_error("currently selected game profile/version is unsupported");
        }
    }

    // Load all the default services.
    load_services();
    services_created_ = true;
}

void Services::start()
{
    NW_PROFILE_SCOPE_N("kernel.services.start");

    if (serices_started_) { return; }

    LOG_F(INFO, "kernel: starting kernel services");

    {
        NW_PROFILE_SCOPE_N("kernel.services.create");
        create();
    }

    if (!module_loading_) {
        NW_PROFILE_SCOPE_N("kernel.services.init.kernel_start");
        for (auto& entry : services_) {
            if (!entry.service) { break; }
            profile_service_init(entry, ServiceInitTime::kernel_start);
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
    LOG_F(INFO, "kernel: shutting down kernel services");

    services_ = std::array<ServiceEntry, 32>{};
    kernel_scope_.reset();
    if (!user_profile_) { profile_ = nullptr; }
    services_count_ = 0;
    serices_started_ = false;
    services_created_ = false;
    module_loaded_ = false;
    module_loading_ = false;
}

void Services::load_services()
{
    // The ordering here is important.  Pretty much everything depends on strings and resman
    add<Strings>();
    add<ResourceManager>();
    add<smalls::Runtime>();
    add<TwoDACache>();
    add<EffectSystem>();
    add<Rules>();
    add<ObjectManager>();
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

EffectSystem& effects()
{
    auto res = services().get_mut<EffectSystem>();
    if (!res) {
        throw std::runtime_error("kernel: unable to effects service");
    }
    return *res;
}

ObjectManager& objects()
{
    auto res = services().get_mut<ObjectManager>();
    if (!res) {
        throw std::runtime_error("kernel: unable to load object service");
    }
    return *res;
}

ResourceManager& resman()
{
    auto res = services().get_mut<ResourceManager>();
    if (!res) {
        throw std::runtime_error("kernel: unable to load resources service");
    }
    return *res;
}

smalls::Runtime& runtime()
{
    auto res = services().get_mut<smalls::Runtime>();
    if (!res) {
        throw std::runtime_error("kernel: unable to load resources service");
    }
    return *res;
}

Services& services()
{
    static Services s_services;
    return s_services;
}

Module* load_module(const std::filesystem::path& path, bool instantiate)
{
    NW_PROFILE_SCOPE_N("kernel.load_module");
    auto load_path = path.string();
    NW_PROFILE_TEXT(load_path.data(), load_path.size());

    if (services().serices_started_) {
        NW_PROFILE_SCOPE_N("kernel.load_module.pre_start_shutdown");
        services().shutdown();
        services().module_loaded_ = false;
    }

    services().module_loading_ = true;

    auto start = std::chrono::high_resolution_clock::now();
    {
        NW_PROFILE_SCOPE_N("kernel.load_module.start_services");
        services().start();
    }

    {
        NW_PROFILE_SCOPE_N("kernel.load_module.init.module_pre_load");
        for (auto& s : services().services_) {
            if (!s.service) { break; }
            profile_service_init(s, ServiceInitTime::module_pre_load);
        }
    }

    {
        NW_PROFILE_SCOPE_N("kernel.load_module.resman_load_module");
        resman().load_module(path);
    }

    Module* mod = nullptr;
    {
        NW_PROFILE_SCOPE_N("kernel.load_module.make_module");
        mod = objects().make_module();
    }
    if (!mod) { return nullptr; }

    if (mod->haks.size()) {
        NW_PROFILE_SCOPE_N("kernel.load_module.load_haks");
        nw::kernel::resman().load_module_haks(mod->haks);
    }

    if (mod->tlk.size()) {
        NW_PROFILE_SCOPE_N("kernel.load_module.load_custom_tlk");
        auto path = nw::kernel::config().user_path() / "tlk";
        nw::kernel::strings().load_custom_tlk(path / (mod->tlk + ".tlk"));
    }

    {
        NW_PROFILE_SCOPE_N("kernel.load_module.init.module_post_load");
        for (auto& s : services().services_) {
            if (!s.service) { break; }
            profile_service_init(s, ServiceInitTime::module_post_load);
        }
    }

    if (instantiate) {
        NW_PROFILE_SCOPE_N("kernel.load_module.instantiate");
        if (mod) {
            mod->instantiate();
        }

        {
            NW_PROFILE_SCOPE_N("kernel.load_module.init.module_post_instantiation");
            for (auto& s : services().services_) {
                if (!s.service) { break; }
                profile_service_init(s, ServiceInitTime::module_post_instantiation);
            }
        }
    }

    services().module_loaded_ = true;
    services().module_loading_ = false;

    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    auto count = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    LOG_F(INFO, "kernel: loaded module '{}' in {}ms", mod ? strings().get(mod->name) : "???", count);
    return mod;
}

nlohmann::json stats()
{
    nlohmann::json j;
    j["kernel_memory"] = {
        {"kernel_memory_used", services().kernel_arena_.used()},
        {"kernel_memory_allocated", services().kernel_arena_.capacity()},
        {"kernel_services", nlohmann::json::array()},
    };

    for (auto& s : services().services_) {
        if (!s.service) { break; }
        j["kernel_services"].push_back(s.service->stats());
    }
    return j;
}

void set_game_profile(GameProfile* profile)
{
    CHECK_F(!services().serices_started_, "[kernel] attempting set game profile after services have been started.");
    services().profile_ = profile;
    services().user_profile_ = true;
}

void unload_module()
{
    // Since everything is getting nuked, it's not necessary to call Module::destroy
    services().shutdown();
    services().module_loaded_ = false;
    services().start();
}

} // namespace nw::kernel
