#include "Resources.hpp"

#include "../formats/Ini.hpp"
#include "../formats/palette_textures.hpp"
#include "../resources/StaticDirectory.hpp"
#include "../util/game_install.hpp"
#include "../util/platform.hpp"
#include "../util/templates.hpp"
#include "GameProfile.hpp"
#include "Kernel.hpp"
#include "Strings.hpp"

#include <chrono>
#include <filesystem>
#include <memory>

namespace fs = std::filesystem;
using namespace std::literals;

namespace nw::kernel {

const std::type_index Resources::type_index{typeid(Resources)};

inline Container* get_container(const LocatorVariant& var)
{
    if (std::holds_alternative<Container*>(var)) {
        return std::get<Container*>(var);
    } else {
        return std::get<unique_container>(var).get();
    }
}

Resources::Resources(MemoryResource* scope, const Resources* parent)
    : Service(scope)
    , parent_(parent)
{
}

void Resources::initialize(ServiceInitTime time)
{
    if (time != ServiceInitTime::kernel_start && time != ServiceInitTime::module_pre_load) { return; }
    LOG_F(INFO, "kernel: resource system initializing...");
    auto start = std::chrono::high_resolution_clock::now();

    if (config().options().include_user
        && config().options().include_nwsync
        && config().version() == GameVersion::vEE) {
        nwsync_ = NWSync{config().user_path() / "nwsync"};
    }

    services().profile()->load_resources();
    update_container_search();
    load_palette_textures();

    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    metrics_.initialization_time = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    LOG_F(INFO, "kernel: resource system initialized ({}ms)", metrics_.initialization_time);
}

bool Resources::load_module(std::filesystem::path path, StringView manifest)
{
    LOG_F(INFO, "resman: loading module container: {}", path);

    if (fs::is_directory(path) && fs::exists(path / "module.ifo")) {
        module_ = std::make_unique<StaticDirectory>(path);
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

void Resources::load_module_haks(const Vector<String>& haks)
{
    for (const auto& h : haks) {
        if (auto c = resolve_container(config().user_path() / "hak", h)) {
            module_haks_.emplace_back(c);
        }
    }
    update_container_search();
}

Container* Resources::module_container() const
{
    return module_.get();
}

Vector<Container*> Resources::module_haks() const
{
    Vector<Container*> result;
    for (const auto& hak : module_haks_) {
        result.push_back(hak.get());
    }
    return result;
}

void Resources::unload_module()
{
    LOG_F(INFO, "resman: unloading module container: {}", module_path_);
    module_path_.clear();
    nwsync_manifest_ = nullptr;
    module_haks_.clear();
    module_.reset();
    update_container_search();
}

bool Resources::add_base_container(const std::filesystem::path& path, const String& name,
    ResourceType::type restype)
{
    auto container = resolve_container(path, name);

    if (!container || !container->valid()) {
        return false;
    }
    for (auto& [cont, _] : search_) {
        auto c = get_container(cont);
        if (!c || c->path() == container->path()) {
            return false;
        }
    }

    game_.push_back(LocatorPayload{unique_container{container}, restype});
    update_container_search();

    return true;
}

bool Resources::add_custom_container(Container* container, bool take_ownership, ResourceType::type restype)
{
    if (!container || !container->valid()) {
        return false;
    }
    for (auto& [cont, _] : search_) {
        auto c = get_container(cont);
        if (!c || c->path() == container->path()) {
            return false;
        }
    }

    if (take_ownership) {
        custom_.push_back(LocatorPayload{unique_container{container}, restype});
    } else {
        custom_.push_back(LocatorPayload{container, restype});
    }

    update_container_search();

    return true;
}

/// Add already created container
bool Resources::add_override_container(const std::filesystem::path& path, const String& name,
    ResourceType::type restype)
{
    auto container = resolve_container(path, name);

    if (!container || !container->valid()) {
        return false;
    }

    for (auto& [cont, _] : search_) {
        auto c = get_container(cont);
        if (!c || c->path() == container->path()) {
            return false;
        }
    }

    override_.push_back(LocatorPayload{unique_container{container}, restype});
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
    for (auto& [cont, type] : search_) {
        auto c = get_container(cont);
        if (!c) { continue; }

        if (type != ResourceType::invalid && !ResourceType::check_category(type, res.type)) {
            continue;
        }

        if (c->contains(res)) { return true; }
    }

    if (parent_ && parent_->contains(res)) {
        return true;
    }

    return false;
}

ResourceData Resources::demand(Resource res) const
{
    ResourceData result;
    for (auto& [cont, type] : search_) {
        auto c = get_container(cont);
        if (!c) { continue; }
        if (type != ResourceType::invalid && !ResourceType::check_category(type, res.type)) {
            continue;
        }
        result = c->demand(res);
        if (result.bytes.size()) {
            break;
        }
    }

    if (result.bytes.size() == 0 && parent_) { result = parent_->demand(res); }

    if (result.bytes.size() == 0) {
        LOG_F(WARNING, "Failed to find '{}'", res.filename());
    }
    return result;
}

ResourceData Resources::demand_any(Resref resref, std::initializer_list<ResourceType::type> restypes) const
{
    ResourceData result;
    for (auto& [cont, type] : search_) {
        auto c = get_container(cont);
        if (!c) { continue; }

        for (auto rt : restypes) {
            Resource res{resref, rt};
            if (type != ResourceType::invalid && !ResourceType::check_category(type, res.type)) {
                continue;
            }
            result = c->demand(res);
            if (result.bytes.size()) {
                return result;
            }
        }
    }
    return parent_ ? parent_->demand_any(resref, restypes) : ResourceData{};
}

ResourceData Resources::demand_in_order(Resref resref, std::initializer_list<ResourceType::type> restypes) const
{
    ResourceData result;
    for (auto rt : restypes) {
        Resource res{resref, rt};
        for (auto& [cont, type] : search_) {
            auto c = get_container(cont);
            if (!c) { continue; }
            if (type != ResourceType::invalid && !ResourceType::check_category(type, res.type)) {
                continue;
            }
            result = c->demand(res);
            if (result.bytes.size()) {
                return result;
            }
        }
    }
    return parent_ ? parent_->demand_in_order(resref, restypes) : ResourceData{};
}

ResourceData Resources::demand_server_vault(StringView cdkey, StringView resref)
{
    ResourceData result;
    auto vault_path = config().user_path() / "servervault";
    if (!fs::exists(vault_path)) { return result; }

    vault_path /= cdkey;
    if (!fs::exists(vault_path)) { return result; }

    Resource res{resref, ResourceType::bic};
    vault_path /= res.filename();
    if (!fs::exists(vault_path)) { return result; }
    result = ResourceData::from_file(vault_path);

    return result;
}

int Resources::extract(const std::regex& pattern, const std::filesystem::path& output) const
{
    int result = 0;
    for (auto& [cont, type] : reverse(search_)) {
        auto c = get_container(cont);
        if (!c) { continue; }
        result += c->extract(pattern, output);
    }

    if (parent_) {
        result += parent_->extract(pattern, output);
    }
    return result;
}

size_t Resources::size() const
{
    size_t result = 0;
    for (auto& [cont, type] : search_) {
        auto c = get_container(cont);
        if (!c) { continue; }
        result += c->size();
    }
    return result;
}

ResourceDescriptor Resources::stat(const Resource& res) const
{
    ResourceDescriptor rd;
    for (auto& [cont, type] : search_) {
        auto c = get_container(cont);
        if (!c) { continue; }

        rd = c->stat(res);
        if (rd) { break; }
    }
    return rd;
}

void Resources::visit(std::function<void(const Resource&)> callback, std::initializer_list<ResourceType::type> types) const noexcept
{
    for (auto& [cont, type] : search_) {
        auto c = get_container(cont);
        if (!c) { continue; }
        c->visit(callback, types);
    }
}

void Resources::load_palette_textures()
{
    // [TODO]: Fix all this with a image/texture cache at some point
    palette_textures_[plt_layer_skin] = std::make_unique<Image>(demand(Resource("pal_skin01"sv, ResourceType::tga)));
    palette_textures_[plt_layer_hair] = std::make_unique<Image>(demand(Resource("pal_hair01"sv, ResourceType::tga)));
    palette_textures_[plt_layer_metal1] = std::make_unique<Image>(demand(Resource("pal_armor01"sv, ResourceType::tga)));
    palette_textures_[plt_layer_metal2] = std::make_unique<Image>(demand(Resource("pal_armor02"sv, ResourceType::tga)));
    palette_textures_[plt_layer_cloth1] = std::make_unique<Image>(demand(Resource("pal_cloth01"sv, ResourceType::tga)));
    palette_textures_[plt_layer_cloth2] = std::make_unique<Image>(demand(Resource("pal_cloth01"sv, ResourceType::tga)));
    palette_textures_[plt_layer_leather1] = std::make_unique<Image>(demand(Resource("pal_leath01"sv, ResourceType::tga)));
    palette_textures_[plt_layer_leather2] = std::make_unique<Image>(demand(Resource("pal_leath01"sv, ResourceType::tga)));
    palette_textures_[plt_layer_tattoo1] = std::make_unique<Image>(demand(Resource("pal_tattoo01"sv, ResourceType::tga)));
    palette_textures_[plt_layer_tattoo2] = std::make_unique<Image>(demand(Resource("pal_tattoo01"sv, ResourceType::tga)));
}

Image* Resources::palette_texture(PltLayer layer)
{
    if (palette_textures_[layer] && palette_textures_[layer]->valid()) {
        return palette_textures_[layer].get();
    }
    ResourceData data;
    data.name = Resource{"<plttex>"sv, ResourceType::tga};
    switch (layer) {
    default:
        return nullptr;
    case plt_layer_skin:
        data.bytes.append(pal_skin01_tga, pal_skin01_tga_len);
        break;
    case plt_layer_hair:
        data.bytes.append(pal_hair01_tga, pal_hair01_tga_len);
        break;
    case plt_layer_metal1:
        data.bytes.append(pal_armor01_tga, pal_armor01_tga_len);
        break;
    case plt_layer_metal2:
        data.bytes.append(pal_armor02_tga, pal_armor02_tga_len);
        break;
    case plt_layer_cloth1:
    case plt_layer_cloth2:
        data.bytes.append(pal_cloth01_tga, pal_cloth01_tga_len);
        break;
    case plt_layer_leather1:
    case plt_layer_leather2:
        data.bytes.append(pal_leath01_tga, pal_leath01_tga_len);
        break;
    case plt_layer_tattoo1:
    case plt_layer_tattoo2:
        data.bytes.append(pal_tattoo01_tga, pal_tattoo01_tga_len);
        break;
    }

    palette_textures_[layer] = std::make_unique<Image>(std::move(data));

    return palette_textures_[layer]->valid() ? palette_textures_[layer].get() : nullptr;
}

Image* Resources::texture(Resref resref) const
{
    auto data = demand_in_order(resref, {ResourceType::dds, ResourceType::tga});
    return data.bytes.size() > 0 ? new Image(std::move(data)) : nullptr;
}

void Resources::update_container_search()
{
    search_.clear();

    auto push_container = [this](Container* c, ResourceType::type cat) {
        if (c && c->valid()) {
            search_.push_back(LocatorPayload(LocatorVariant(c), cat));
        }
    };

    for (auto& c : custom_) {
        push_container(get_container(c.container), c.restype);
    }

    for (auto& c : override_) {
        push_container(get_container(c.container), c.restype);
    }

    // [TODO] Deal with NWSync, somehow

    for (const auto& c : module_haks_) {
        push_container(c.get(), ResourceType::invalid);
    }

    push_container(module_.get(), ResourceType::invalid);

    for (auto& c : game_) {
        push_container(get_container(c.container), c.restype);
    }
}

} // namespace nw::kernel
