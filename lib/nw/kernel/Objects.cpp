#include "Objects.hpp"

#include "../log.hpp"
#include "../objects/Area.hpp"
#include "../objects/Creature.hpp"
#include "../objects/Door.hpp"
#include "../objects/Encounter.hpp"
#include "../objects/Item.hpp"
#include "../objects/Module.hpp"
#include "../objects/Player.hpp"
#include "../objects/Trigger.hpp"
#include "../util/platform.hpp"
#include "../util/templates.hpp"
#include "Kernel.hpp"
#include "Resources.hpp"

namespace nw::kernel {

void ObjectSystem::clear()
{
    free_list_ = std::stack<ObjectID, std::vector<ObjectID>>();
    objects_.clear();
}

void ObjectSystem::destroy(ObjectHandle obj)
{
    if (valid(obj)) {
        size_t idx = static_cast<size_t>(obj.id);
        auto& o = std::get<std::unique_ptr<ObjectBase>>(objects_[idx]);
        auto new_handle = o->handle();
        // If version is at max don't add to free list.  Still clobber object.
        if (new_handle.version < std::numeric_limits<uint16_t>::max()) {
            ++new_handle.version;
            free_list_.push(new_handle.id);
        }
        objects_[idx] = new_handle;
    }
}

ObjectBase* ObjectSystem::get_object_base(ObjectHandle obj)
{
    if (!valid(obj)) { return nullptr; }
    auto idx = static_cast<size_t>(obj.id);
    return std::get<std::unique_ptr<ObjectBase>>(objects_[idx]).get();
}

nw::Player* ObjectSystem::load_player(std::string_view cdkey, std::string_view resref)
{
    auto data = resman().demand_server_vault(cdkey, resref);
    if (data.bytes.size() == 0) { return nullptr; }

    auto obj = make<nw::Player>();
    Gff in{std::move(data)};
    deserialize(obj, in.toplevel());

    return obj;
}

Area* ObjectSystem::make_area(Resref area)
{
    Gff are{resman().demand({area, ResourceType::are})};
    Gff git{resman().demand({area, ResourceType::git})};
    Gff gic{resman().demand({area, ResourceType::gic})};
    Area* obj = make<Area>();
    deserialize(obj, are.toplevel(), git.toplevel(), gic.toplevel());
    return obj;
}

Module* ObjectSystem::make_module()
{
    Module* obj = make<Module>();
    auto data = nw::kernel::resman().demand({"module"sv, ResourceType::ifo});

    if (!data.bytes.size()) {
        LOG_F(ERROR, "Unable to load module.ifo from resman");
        delete obj;
        return nullptr;
    }

    if (data.bytes.size() > 8 && memcmp(data.bytes.data(), "IFO V3.2", 8) == 0) {
        Gff in{std::move(data)};
        if (in.valid()) {
            deserialize(obj, in.toplevel());
        } else {
            delete obj;
            return nullptr;
        }
    } else {
        auto sv = data.bytes.string_view();
        try {
            nlohmann::json j = nlohmann::json::parse(sv.begin(), sv.end());
            Module::deserialize(obj, j);
        } catch (std::exception& e) {
            LOG_F(ERROR, "[kernel] failed to load module from json: {}", e.what());
            delete obj;
            return nullptr;
        }
    }

    return obj;
}

bool ObjectSystem::valid(ObjectHandle handle) const
{
    auto idx = static_cast<size_t>(handle.id);
    if (idx >= objects_.size() || std::holds_alternative<ObjectHandle>(objects_[idx])) {
        return false;
    }

    if (auto& obj = std::get<std::unique_ptr<ObjectBase>>(objects_[idx])) {
        return obj->handle() == handle;
    }

    return false;
}

} // namespace nw::kernel
