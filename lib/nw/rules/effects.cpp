#include "effects.hpp"

#include "../kernel/TwoDACache.hpp"

#include "nlohmann/json.hpp"

namespace nw {

DEFINE_RULE_TYPE(EffectType);

Effect::Effect(nw::MemoryResource* allocator)
    : Effect(EffectType::invalid(), allocator)
{
}

Effect::Effect(EffectType type_, nw::MemoryResource* allocator)
    : integers_(20, allocator)
    , floats_(4, allocator)
    , strings_(4, allocator)
{
    handle_.type = type_;
}

void Effect::reset()
{
    handle_.type = EffectType::invalid();
    handle_.category = EffectCategory::magical;
    handle_.subtype = -1;
    handle_.creator = ObjectHandle{};
    duration = 0.0f;
    expire_day = 0;
    expire_time = 0;

    integers_.clear();
    floats_.clear();
    strings_.clear();
}

float Effect::get_float(size_t index) const noexcept
{
    return index < floats_.size() ? floats_[index] : 0.0f;
}

int Effect::get_int(size_t index) const noexcept
{
    return index < integers_.size() ? integers_[index] : 0;
}

StringView Effect::get_string(size_t index) const noexcept
{
    return index < strings_.size() ? strings_[index] : StringView{};
}

EffectHandle& Effect::handle() noexcept
{
    return handle_;
}

const EffectHandle& Effect::handle() const noexcept
{
    return handle_;
}

void Effect::set_float(size_t index, float value)
{
    if (index >= floats_.size()) {
        floats_.resize(index + 1);
    }
    floats_[index] = value;
}

void Effect::set_int(size_t index, int value)
{
    if (index >= integers_.size()) {
        integers_.resize(index + 1);
    }
    integers_[index] = value;
}

void Effect::set_handle(EffectHandle hndl)
{
    handle_.index = hndl.index;
    handle_.generation = hndl.generation;
    handle_.effect = this;
}

void Effect::set_string(size_t index, String value)
{
    if (index >= strings_.size()) {
        strings_.resize(index + 1);
    }
    strings_[index] = std::move(value);
}

void Effect::set_versus(Versus vs) { versus_ = vs; }

const Versus& Effect::versus() const noexcept { return versus_; }

// == EffectArray =============================================================
// ============================================================================

EffectArray::EffectArray(nw::MemoryResource* allocator)
    : allocator_(allocator)
{
}

EffectArray::iterator EffectArray::begin() { return effects_.begin(); }
EffectArray::const_iterator EffectArray::begin() const { return effects_.begin(); }

EffectArray::iterator EffectArray::end() { return effects_.end(); }
EffectArray::const_iterator EffectArray::end() const { return effects_.end(); }

bool EffectArray::add(Effect* effect)
{
    if (!effect) { return false; }
    auto needle = effect->handle();

    auto it = std::lower_bound(std::begin(effects_), std::end(effects_), needle);
    effects_.insert(it, needle);

    return true;
}

void EffectArray::erase(EffectArray::iterator first, EffectArray::iterator last)
{
    effects_.erase(first, last);
}

bool EffectArray::remove(Effect* effect)
{
    if (!effect) { return false; }
    auto needle = effect->handle();
    int count = 0;

    auto it = std::remove_if(std::begin(effects_), std::end(effects_), [&count, needle](const EffectHandle& handle) {
        if (needle == handle) {
            ++count;
            return true;
        }
        return false;
    });
    effects_.erase(it, std::end(effects_));
    return count > 0;
}

size_t EffectArray::size() const noexcept
{
    return effects_.size();
}

// == EffectSystem ============================================================
// ============================================================================

const std::type_index EffectSystem::type_index{typeid(EffectSystem)};

EffectSystem::EffectSystem(MemoryResource* allocator)
    : Service(allocator)
    , pool_(1024, allocator)
{
}

bool EffectSystem::add(EffectType type, EffectFunc apply, EffectFunc remove)
{
    auto [it, added] = registry_.emplace(*type, std::make_pair(std::move(apply), std::move(remove)));
    return added;
}

bool EffectSystem::add(ItemPropertyType type, ItemPropFunc generator)
{
    auto [it, added] = itemprops_.emplace(*type, std::move(generator));
    return added;
}

bool EffectSystem::apply(ObjectBase* obj, Effect* effect)
{
    if (!effect) { return false; }
    auto it = registry_.find(*effect->handle().type);
    if (it == std::end(registry_)) { return false; }
    if (it->second.first && it->second.first(obj, effect)) {
        return true;
    }
    return false;
}

Effect* EffectSystem::create(EffectType type)
{
    auto effect = pool_.create();
    if (effect == nullptr) { return nullptr; }
    effect->handle().type = type;
    return effect;
}

void EffectSystem::destroy(Effect* effect)
{
    if (!effect) { return; }
    pool_.destroy(effect->handle());
}

void EffectSystem::initialize(kernel::ServiceInitTime time)
{
    if (time != kernel::ServiceInitTime::kernel_start && time != kernel::ServiceInitTime::module_post_load) {
        return;
    }

    auto start = std::chrono::high_resolution_clock::now();

    LOG_F(INFO, "kernel: effect system initializing...");
    LOG_F(INFO, "  ... loading item property cost tables");
    auto costtable = kernel::twodas().get("iprp_costtable");
    if (costtable) {
        ip_cost_table_.resize(costtable->rows());
        std::optional<StringView> resref;
        int count = 0;
        for (size_t i = 0; i < costtable->rows(); ++i) {
            if ((resref = costtable->get<StringView>(i, "Name"))) {
                auto tda = kernel::twodas().get(*resref);
                if (!tda) {
                    LOG_F(WARNING, "  ... failed to load cost table {}", *resref);
                    ip_cost_table_[i] = nullptr;
                } else {
                    ip_cost_table_[i] = tda;
                    ++count;
                }
            }
        }
        LOG_F(INFO, "  ... loaded {} item property cost tables", count);
    } else {
        LOG_F(ERROR, "  ... failed to load item property cost tables");
    }

    LOG_F(INFO, "  ... loading item property param tables");
    auto paramtable = kernel::twodas().get("iprp_paramtable");
    if (paramtable) {
        ip_param_table_.resize(paramtable->rows());
        std::optional<StringView> resref;
        int count = 0;
        for (size_t i = 0; i < paramtable->rows(); ++i) {
            if ((resref = paramtable->get<StringView>(i, "TableResRef"))) {
                auto tda = kernel::twodas().get(*resref);
                if (!tda) {
                    LOG_F(WARNING, "  ... failed to load param table {}", *resref);
                    ip_param_table_[i] = nullptr;
                } else {
                    ip_param_table_[i] = tda;
                    ++count;
                }
            }
        }
        LOG_F(INFO, "  ... loaded {} item property param tables", count);
    } else {
        LOG_F(ERROR, "Failed to load item property param tables");
    }

    LOG_F(INFO, "  ... loading item property definitions");
    auto ipdef = kernel::twodas().get("itempropdef");
    if (ipdef) {
        int count = 0;
        std::optional<StringView> temp;
        for (size_t i = 0; i < ipdef->rows(); ++i) {
            ItemPropertyDefinition def;
            if (ipdef->get_to(i, "Name", def.name)) {
                if ((temp = ipdef->get<StringView>(i, "SubTypeResRef"))) {
                    def.subtype = kernel::twodas().get(*temp);
                }
                if (auto cost = ipdef->get<int>(i, "CostTableResRef")) {
                    def.cost_table = ip_cost_table_[size_t(*cost)];
                }
                if (auto param = ipdef->get<int>(i, "Param1ResRef")) {
                    def.param_table = ip_param_table_[size_t(*param)];
                }
                ipdef->get_to(i, "Cost", def.cost);
                ipdef->get_to(i, "GameStrRef", def.game_string);
                ipdef->get_to(i, "Description", def.description);
                ++count;
            }
            ip_definitions_.push_back(def);
        }
        LOG_F(INFO, "  ... loaded {} item property definitions", count);
    } else {
        LOG_F(ERROR, "Failed to load item property definitions");
    }

    LOG_F(INFO, "  ... loading item property baseitem table");
    itemprop_table_ = kernel::twodas().get("itemprops");

    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    LOG_F(INFO, "kernel: effect system initialized. ({}ms)",
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

Effect* EffectSystem::generate(const ItemProperty& property, EquipIndex index, BaseItem baseitem) const
{
    auto it = itemprops_.find(property.type);
    if (it == std::end(itemprops_)) { return nullptr; }
    if (it->second) {
        return it->second(property, index, baseitem);
    }
    return nullptr;
}

const StaticTwoDA* EffectSystem::ip_cost_table(size_t table) const
{
    if (table >= ip_cost_table_.size()) { return nullptr; }
    return ip_cost_table_[table];
}

const ItemPropertyDefinition* EffectSystem::ip_definition(ItemPropertyType type) const
{
    if (type.idx() >= ip_definitions_.size()) { return nullptr; }
    return &ip_definitions_[type.idx()];
}

const Vector<ItemPropertyDefinition>& EffectSystem::ip_definitions() const noexcept
{
    return ip_definitions_;
}

const StaticTwoDA* EffectSystem::ip_param_table(size_t table) const
{
    if (table >= ip_param_table_.size()) { return nullptr; }
    return ip_param_table_[table];
}

const StaticTwoDA* EffectSystem::itemprops() const noexcept
{
    return itemprop_table_;
}

bool EffectSystem::remove(ObjectBase* obj, Effect* effect)
{
    if (!effect) { return false; }
    auto it = registry_.find(*effect->handle().type);
    if (it == std::end(registry_)) { return false; }
    if (it->second.second && it->second.second(obj, effect)) {
        return true;
    }
    return false;
}

nlohmann::json EffectSystem::stats() const
{
    nlohmann::json j;
    j["effect system"] = {};
    return j;
}

} // namespace nw
