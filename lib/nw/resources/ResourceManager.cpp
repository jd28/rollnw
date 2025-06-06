#include "ResourceManager.hpp"

#include "StaticDirectory.hpp"
#include "StaticErf.hpp"
#include "StaticKey.hpp"
#include "StaticZip.hpp"

#include "../formats/palette_textures.hpp"
#include "../kernel/GameProfile.hpp"
#include "../util/macros.hpp"
#include "../util/platform.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <memory>

namespace fs = std::filesystem;
using namespace std::literals;

namespace nw {

const std::type_index ResourceManager::type_index{typeid(ResourceManager)};

inline Container* get_container(const LocatorVariant& var)
{
    if (std::holds_alternative<Container*>(var)) {
        return std::get<Container*>(var);
    } else {
        return std::get<unique_container>(var).get();
    }
}

inline unique_container make_unique_container(Container* c)
{
    ContainerDeleter del{c->allocator()};
    return unique_container{c, del};
}

ResourceManager::ResourceManager(MemoryResource* scope, const ResourceManager* parent)
    : Service(scope)
    , parent_(parent)
    , module_{nullptr, ContainerDeleter{allocator()}}
{
}

void ResourceManager::initialize(kernel::ServiceInitTime time)
{
    if (time == kernel::ServiceInitTime::kernel_start || time == kernel::ServiceInitTime::module_pre_load) {
        LOG_F(INFO, "kernel: resource system initializing...");
        auto start = std::chrono::high_resolution_clock::now();

        kernel::services().profile()->load_resources();
        update_container_search();
        load_palette_textures();

        auto elapsed = std::chrono::high_resolution_clock::now() - start;
        LOG_F(INFO, "kernel: resource system initialized ({}ms)",
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
    }

    if (time == kernel::ServiceInitTime::kernel_start || time == kernel::ServiceInitTime::module_post_load) {
        LOG_F(INFO, "kernel: resource system building resource registry...");
        auto start = std::chrono::high_resolution_clock::now();
        build_registry();
        auto elapsed = std::chrono::high_resolution_clock::now() - start;
        LOG_F(INFO, "kernel: resource system resource registry built ({}ms)", std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
    }
}

bool ResourceManager::load_module(std::filesystem::path path)
{
    LOG_F(INFO, "resman: loading module container: {}", path);

    if (fs::is_directory(path) && fs::exists(path / "module.ifo")) {
        auto ptr = allocator()->allocate(sizeof(StaticDirectory), alignof(StaticDirectory));
        module_ = make_unique_container(new (ptr) StaticDirectory(path, allocator()));
    } else if (fs::exists(path) && string::icmp(path_to_string(path.extension()), ".mod")) {
        auto ptr = allocator()->allocate(sizeof(StaticErf), alignof(StaticErf));
        module_ = make_unique_container(new (ptr) StaticErf(path, allocator()));
    } else if (fs::exists(path) && string::icmp(path_to_string(path.extension()), ".zip")) {
        auto ptr = allocator()->allocate(sizeof(StaticZip), alignof(StaticZip));
        module_ = make_unique_container(new (ptr) StaticZip(path, allocator()));
    }

    if (!module_ || !module_->valid()) {
        LOG_F(ERROR, "Failed to load module at '{}'", path);
        return false;
    }

    module_path_ = std::move(path);
    update_container_search();
    return true;
}

void ResourceManager::load_module_haks(const Vector<String>& haks)
{

    auto start = std::chrono::high_resolution_clock::now();

    for (const auto& h : haks) {
        if (auto c = resolve_container(kernel::config().user_path() / "hak", h, allocator())) {
            module_haks_.push_back(make_unique_container(c));
        }
    }
    update_container_search();

    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    LOG_F(INFO, "    ... loaded {} module haks ({}ms)", haks.size(),
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

nlohmann::json ResourceManager::stats() const
{
    nlohmann::json j;
    j["name"] = "resources service";
    j["total_static_assets"] = registry_.size();
    return j;
}

Container* ResourceManager::module_container() const
{
    return module_.get();
}

void ResourceManager::unload_module()
{
    LOG_F(INFO, "resman: unloading module container: {}", module_path_);
    module_path_.clear();
    module_haks_.clear();
    module_.reset();
    registry_.clear();
    update_container_search();
    build_registry();
}

bool ResourceManager::add_base_container(const std::filesystem::path& path, const String& name,
    ResourceType::type restype)
{
    ENSURE_OR_RETURN_FALSE(!frozen_, "[resman] asset registry is already frozen: unable to add container '{}/{}'", path, name);

    auto container = resolve_container(path, name, allocator());
    ENSURE_OR_RETURN_FALSE(container && container->valid(), "[resman] attempting to add invalid base container '{}/{}'", path, name);
    ENSURE_OR_RETURN_FALSE(container->size() > 0, "[resman] attempting to add empty container '{}/{}'", path, name);

    for (auto& [cont, _] : search_) {
        auto c = get_container(cont);
        if (!c || c->path() == container->path()) {
            allocator()->deallocate(container);
            return false;
        }
    }

    game_.push_back(LocatorPayload{make_unique_container(container), restype});
    update_container_search();

    return true;
}

bool ResourceManager::add_custom_container(Container* container, bool take_ownership, ResourceType::type restype)
{
    ENSURE_OR_RETURN_FALSE(container && container->valid(), "[resman] attempting to add invalid custom container");
    ENSURE_OR_RETURN_FALSE(!frozen_, "[resman] asset registry is already frozen: unable to add container '{}'", container->path());
    ENSURE_OR_RETURN_FALSE(container->size() > 0, "[resman] attempting to add empty container '{}'", container->path());

    for (auto& [cont, _] : search_) {
        auto c = get_container(cont);
        if (!c || c->path() == container->path()) {
            return false;
        }
    }

    if (take_ownership) {
        custom_.push_back(LocatorPayload{make_unique_container(container), restype});
    } else {
        custom_.push_back(LocatorPayload{container, restype});
    }

    update_container_search();

    return true;
}

bool ResourceManager::add_override_container(const std::filesystem::path& path, const String& name,
    ResourceType::type restype)
{
    ENSURE_OR_RETURN_FALSE(!frozen_, "[resman] asset registry is already frozen: unable to add container '{}/{}'", path, name);

    auto container = resolve_container(path, name, allocator());
    ENSURE_OR_RETURN_FALSE(container && container->valid(), "[resman] attempting to add invalid override container: '{}/{}'", path, name);
    ENSURE_OR_RETURN_FALSE(container->size() > 0, "[resman] attempting to add empty container '{}/{}'", path, name);

    for (auto& [cont, _] : search_) {
        auto c = get_container(cont);
        if (!c || c->path() == container->path()) {
            return false;
        }
    }

    override_.push_back(LocatorPayload{make_unique_container(container), restype});
    update_container_search();

    return true;
}

void ResourceManager::build_registry()
{
    ENSURE_OR_RETURN(!frozen_, "[resman] asset registry is already frozen");
    Container* current = nullptr;

    size_t sz = 0;
    for (auto& [cont, _] : search_) {
        current = get_container(cont);
        sz += current->size();
    }

    registry_.reserve(sz);

    // The registry will determine how assets stack..
    auto cb = [this, &current](Resource uri, const ContainerKey* key) {
        registry_.insert(uri, current, key);
    };

    for (auto& [cont, _] : search_) {
        current = get_container(cont);
        current->visit(cb);
    }

    frozen_ = true;
}

bool ResourceManager::contains(Resource uri) const noexcept
{
    return registry_.contains(uri);
}

ResourceData ResourceManager::demand(Resource uri) const
{
    // [TODO] Add dynamic registry lookup.
    auto result = registry_.demand(uri);
    if (parent_ && result.bytes.size() == 0) {
        result = parent_->demand(uri);
    }
    return result;
}

ResourceData ResourceManager::demand_in_order(Resref resref, std::initializer_list<ResourceType::type> restypes) const
{
    ResourceData result;
    for (auto rt : restypes) {
        result = demand({resref, rt});
        if (result.bytes.size() > 0) { return result; }
    }
    return parent_ ? parent_->demand_in_order(resref, restypes) : ResourceData{};
}

ResourceData ResourceManager::demand_server_vault(StringView cdkey, StringView resref)
{
    ResourceData result;
    auto vault_path = kernel::config().user_path() / "servervault";
    if (!fs::exists(vault_path)) { return result; }

    vault_path /= cdkey;
    if (!fs::exists(vault_path)) { return result; }

    Resource res{resref, ResourceType::bic};
    vault_path /= res.filename();
    if (!fs::exists(vault_path)) { return result; }
    result = ResourceData::from_file(vault_path);

    return result;
}

void ResourceManager::visit(std::function<void(Resource)> visitor) const
{
    registry_.visit(std::move(visitor));
}

size_t ResourceManager::size() const
{
    return registry_.size();
}

void ResourceManager::load_palette_textures()
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

Image* ResourceManager::palette_texture(PltLayer layer)
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

Image* ResourceManager::texture(Resref resref) const
{
    auto data = demand_in_order(resref, {ResourceType::dds, ResourceType::tga});
    return data.bytes.size() > 0 ? new Image(std::move(data)) : nullptr;
}

void ResourceManager::update_container_search()
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

    for (const auto& c : module_haks_) {
        push_container(c.get(), ResourceType::invalid);
    }

    push_container(module_.get(), ResourceType::invalid);

    for (auto& c : game_) {
        push_container(get_container(c.container), c.restype);
    }
}

} // namespace nw
