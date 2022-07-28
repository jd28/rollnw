#include "Objects.hpp"

#include "../components/Area.hpp"
#include "../components/Creature.hpp"
#include "../components/Door.hpp"
#include "../components/Encounter.hpp"
#include "../components/Item.hpp"
#include "../components/Module.hpp"
#include "../components/Trigger.hpp"
#include "../log.hpp"
#include "../util/platform.hpp"
#include "../util/templates.hpp"
#include "Kernel.hpp"

namespace nw::kernel {

void ObjectSystem::clear()
{
    free_list_ = std::stack<ObjectID>();
    objects_.clear();
}

void ObjectSystem::destroy(ObjectHandle obj)
{
    if (valid(obj)) {
        size_t idx = static_cast<size_t>(obj.id);
        auto& o = std::get<std::unique_ptr<ObjectBase>>(objects_[idx]);
        auto new_handle = o->handle();
        ++new_handle.version;
        free_list_.push(new_handle.id);
        objects_[idx] = new_handle;
    }
}

Area* ObjectSystem::make_area(Resref area)
{
    GffInputArchive are{resman().demand({area, ResourceType::are})};
    GffInputArchive git{resman().demand({area, ResourceType::git})};
    GffInputArchive gic{resman().demand({area, ResourceType::gic})};
    Area* obj = make<Area>();
    Area::deserialize(obj, are.toplevel(), git.toplevel(), gic.toplevel());
    return obj;
}

Module* ObjectSystem::make_module()
{
    Module* obj = make<Module>();
    auto ba = nw::kernel::resman().demand({"module"sv, ResourceType::ifo});

    if (!ba.size()) {
        LOG_F(ERROR, "Unable to load module.ifo from resman");
        delete obj;
        return nullptr;
    }

    if (ba.size() > 8 && memcmp(ba.data(), "IFO V3.2", 8) == 0) {
        GffInputArchive in{std::move(ba)};
        if (in.valid()) {
            Module::deserialize(obj, in.toplevel());
        } else {
            delete obj;
            return nullptr;
        }
    } else {
        auto sv = ba.string_view();
        try {
            nlohmann::json j = nlohmann::json::parse(sv.begin(), sv.end());
            Module::deserialize(obj, j);
        } catch (std::exception& e) {
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
