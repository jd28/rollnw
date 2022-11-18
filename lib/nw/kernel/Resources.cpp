#include "Resources.hpp"

#include "../components/Module.hpp"
#include "../util/game_install.hpp"
#include "../util/platform.hpp"
#include "../util/templates.hpp"
#include "Kernel.hpp"
#include "Strings.hpp"

#include <filesystem>
#include <memory>

namespace nw::kernel {

inline const Container* get_container(const LocatorVariant& var)
{
    if (std::holds_alternative<Container*>(var)) {
        return std::get<Container*>(var);
    } else {
        return std::get<unique_container>(var).get();
    }
}

inline Container* resolve_container(const std::filesystem::path& p, const std::string& name)
{
    if (fs::is_directory(p / name)) {
        return new Directory{p / name};
    } else if (fs::exists(p / (name + ".hak"))) {
        return new Erf{p / (name + ".hak")};
    } else if (fs::exists(p / (name + ".erf"))) {
        return new Erf{p / (name + ".erf")};
    } else if (fs::exists(p / (name + ".zip"))) {
        return new Zip{p / (name + ".zip")};
    }
    return nullptr;
}

void Resources::initialize()
{
    LOG_F(INFO, "kernel: initializing resource system");
    LOG_F(INFO, "kernel: root directory: {}", config().options().install);
    LOG_F(INFO, "kernel: user directory: {}", config().options().user);

    ambient_user_ = Directory{config().alias_path(PathAlias::ambient)};
    dmvault_user_ = Directory{config().alias_path(PathAlias::dmvault)};
    localvault_user_ = Directory{config().alias_path(PathAlias::localvault)};
    music_user_ = Directory{config().alias_path(PathAlias::music)};
    override_user_ = Directory{config().alias_path(PathAlias::override)};
    portraits_user_ = Directory{config().alias_path(PathAlias::portraits)};
    servervault_user_ = Directory{config().alias_path(PathAlias::servervault)};

    if (config().options().version == GameVersion::vEE) {
        if (config().options().include_nwsync) {
            nwsync_ = NWSync{config().alias_path(PathAlias::nwsync)};
        }
        development_ = Directory{config().alias_path(PathAlias::development)};
    }

    if (config().userpatch_ini().valid()) {
        int i = 0;
        std::string file;
        while (config().userpatch_ini().get_to(fmt::format("Patch/PatchFile{:03d}", i++), file)) {
            auto c = resolve_container(config().alias_path(PathAlias::patch), file);
            if (c) {
                patches_.emplace_back(c);
            }
        }
    }

    if (config().options().include_install) {
        texture_packs_.reserve(4);
        if (config().options().version == GameVersion::vEE) {
            ambient_install_ = Directory{config().options().install / "data/amb/"};
            dmvault_install_ = Directory{config().options().install / "data/dmv/"};
            localvault_install_ = Directory{config().options().install / "data/lcv/"};
            music_install_ = Directory{config().options().install / "data/mus"};
            override_install_ = Directory{config().options().install / "data/ovr"};
            portraits_install_ = Directory{config().options().install / "data/prt/"};

            texture_packs_.emplace_back(config().options().install / "data/txpk/xp2_tex_tpa.erf");
            texture_packs_.emplace_back(config().options().install / "data/txpk/xp1_tex_tpa.erf");
            texture_packs_.emplace_back(config().options().install / "data/txpk/textures_tpa.erf");
            texture_packs_.emplace_back(config().options().install / "data/txpk/tiles_tpa.erf");

            keys_.reserve(2);
            auto lang = strings().global_language();
            if (lang != LanguageID::english) {
                auto shortcode = Language::to_string(lang);
                keys_.emplace_back(config().options().install / "lang" / shortcode / "data" / "nwn_base_loc.key");
            }
            keys_.emplace_back(config().options().install / "data/nwn_base.key");
        } else {
            texture_packs_.emplace_back(config().options().install / "texturepacks/xp2_tex_tpa.erf");
            texture_packs_.emplace_back(config().options().install / "texturepacks/xp1_tex_tpa.erf");
            texture_packs_.emplace_back(config().options().install / "texturepacks/textures_tpa.erf");
            texture_packs_.emplace_back(config().options().install / "texturepacks/tiles_tpa.erf");

            keys_.reserve(8);
            keys_.emplace_back(config().options().install / "xp3patch.key");
            keys_.emplace_back(config().options().install / "xp3.key");
            keys_.emplace_back(config().options().install / "xp2patch.key");
            keys_.emplace_back(config().options().install / "xp2.key");
            keys_.emplace_back(config().options().install / "xp1patch.key");
            keys_.emplace_back(config().options().install / "xp1.key");
            keys_.emplace_back(config().options().install / "patch.key");
            keys_.emplace_back(config().options().install / "chitin.key");
        }
    }
    update_container_search();
}

bool Resources::load_module(std::filesystem::path path, std::string_view manifest)
{
    LOG_F(INFO, "resman: loading module container: {}", path);

    if (fs::is_directory(path) && fs::exists(path / "module.ifo")) {
        module_ = std::make_unique<Directory>(path);
    } else if (fs::exists(path) && string::icmp(path_to_string(path.extension()), ".mod")) {
        module_ = std::make_unique<Erf>(path);
    } else if (fs::exists(path) && string::icmp(path_to_string(path.extension()), ".zip")) {
        module_ = std::make_unique<Zip>(path);
    }

    if (!module_ || !module_->valid()) {
        LOG_F(ERROR, "Failed to load module at '{}'", path);
        return false;
    }

    module_path_ = std::move(path);
    nwsync_manifest_ = nwsync_.get(manifest);
    update_container_search();
    return true;
}

void Resources::load_module_haks(const std::vector<std::string>& haks)
{
    // [TODO]
    for (const auto& h : haks) {
        haks_.emplace_back(config().alias_path(PathAlias::hak) / (h + ".hak"));
    }
}

void Resources::unload_module()
{
    LOG_F(INFO, "resman: unloading module container: {}", module_path_);
    module_path_.clear();
    nwsync_manifest_ = nullptr;
    haks_.clear();
    module_.reset();
    update_container_search();
}

bool Resources::add_container(Container* container, bool take_ownership)
{
    if (!container || !container->valid()) {
        return false;
    }
    for (auto& [cont, type, user] : search_) {
        if (cont->path() == container->path()) {
            return false;
        }
    }

    if (take_ownership) {
        custom_.emplace_back(unique_container{container});
    } else {
        custom_.push_back(container);
    }

    update_container_search();

    return true;
}

void Resources::clear_containers()
{
    custom_.clear();
    update_container_search();
}

bool Resources::contains(Resource res) const
{
    for (auto [cont, type, user] : search_) {
        if (type != ResourceType::invalid && !ResourceType::check_category(type, res.type)) {
            continue;
        }
        if (cont->contains(res)) {
            return true;
        }
    }
    return false;
}

ByteArray Resources::demand(Resource res) const
{
    ByteArray result;
    for (auto [cont, type, user] : search_) {
        if (type != ResourceType::invalid && !ResourceType::check_category(type, res.type)) {
            continue;
        }
        result = cont->demand(res);
        if (result.size()) {
            break;
        }
    }
    if (result.size() == 0) {
        LOG_F(WARNING, "Failed to find '{}'", res.filename());
    }
    return result;
}

std::pair<ByteArray, ResourceType::type>
Resources::demand_any(Resref resref, std::initializer_list<ResourceType::type> restypes) const
{
    ByteArray result;
    for (auto [cont, type, user] : search_) {
        for (auto rt : restypes) {
            Resource res{resref, rt};
            if (type != ResourceType::invalid && !ResourceType::check_category(type, res.type)) {
                continue;
            }
            result = cont->demand(res);
            if (result.size()) {
                return {result, rt};
            }
        }
    }
    return {result, nw::ResourceType::invalid};
}

std::pair<ByteArray, ResourceType::type>
Resources::demand_in_order(Resref resref, std::initializer_list<ResourceType::type> restypes) const
{
    ByteArray result;
    for (auto rt : restypes) {
        Resource res{resref, rt};
        for (auto [cont, type, user] : search_) {
            if (type != ResourceType::invalid && !ResourceType::check_category(type, res.type)) {
                continue;
            }
            result = cont->demand(res);
            if (result.size()) {
                return {result, rt};
            }
        }
    }
    return {result, nw::ResourceType::invalid};
}

ByteArray Resources::demand_server_vault(std::string_view cdkey, std::string_view resref)
{
    ByteArray result;
    auto vault_path = config().alias_path(PathAlias::servervault);
    if (!fs::exists(vault_path)) { return result; }

    vault_path /= cdkey;
    if (!fs::exists(vault_path)) { return result; }

    Resource res{resref, ResourceType::bic};
    vault_path /= res.filename();
    if (!fs::exists(vault_path)) { return result; }
    result = ByteArray::from_file(vault_path);

    return result;
}

int Resources::extract(const std::regex& pattern, const std::filesystem::path& output) const
{
    int result = 0;
    for (auto [cont, type, user] : reverse(search_)) {
        result += cont->extract(pattern, output);
    }
    return result;
}

size_t Resources::size() const
{
    size_t result = 0;
    for (auto [cont, type, user] : search_) {
        result += cont->size();
    }
    return result;
}

ResourceDescriptor Resources::stat(const Resource& res) const
{
    ResourceDescriptor rd;
    for (auto [cont, type, user] : search_) {
        rd = cont->stat(res);
        if (rd) {
            break;
        }
    }
    return rd;
}

void Resources::update_container_search()
{
    if (search_.size()) {
        search_.clear();
    }

    auto push_container = [this](const Container* c, ResourceType::type cat, bool user) {
        if (c && c->valid()) {
            search_.emplace_back(c, cat, user);
        }
    };

    for (auto& c : custom_) {
        push_container(get_container(c), ResourceType::invalid, true);
    }

    push_container(&portraits_user_, ResourceType::texture, true);
    push_container(&portraits_install_, ResourceType::texture, false);

    // Vault
    // push_container(vault_user_, ResourceType::player, true);

    push_container(&development_, ResourceType::invalid, true);

    push_container(nwsync_manifest_, ResourceType::invalid, true);

    for (const auto& c : haks_) {
        push_container(&c, ResourceType::invalid, false);
    }

    push_container(module_.get(), ResourceType::invalid, true);

    push_container(&override_user_, ResourceType::invalid, true);
    push_container(&override_install_, ResourceType::invalid, false);

    push_container(&ambient_user_, ResourceType::sound, true);
    push_container(&music_user_, ResourceType::sound, true);
    push_container(&ambient_install_, ResourceType::sound, false);
    push_container(&music_install_, ResourceType::sound, false);

    for (auto& c : patches_) {
        push_container(c.get(), ResourceType::invalid, false);
    }

    for (auto& c : texture_packs_) {
        push_container(&c, ResourceType::texture, false);
    }

    for (auto& c : keys_) {
        push_container(&c, ResourceType::invalid, false);
    }
}

} // namespace nw::kernel
