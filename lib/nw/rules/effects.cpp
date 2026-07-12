#include "effects.hpp"

#include "../kernel/Kernel.hpp"
#include "../kernel/TwoDACache.hpp"
#include "../objects/ObjectBase.hpp"
#include "../smalls/Array.hpp"
#include "../smalls/Bytecode.hpp"
#include "../smalls/runtime.hpp"
#include "../util/profile.hpp"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <chrono>

namespace nw {

namespace {

void dispatch_on_effects(nw::ObjectBase* obj, const nw::Vector<nw::Effect*>& effects, bool is_apply,
    nw::EffectCallbackTimingStats* timing_stats = nullptr)
{
    if (!obj || effects.empty()) { return; }

    auto& rt = nw::kernel::runtime();
    auto* script = rt.get_module(nw::kernel::config().effects_policy_module());
    if (!script) { return; }

    // get_or_compile_module is O(1) when already compiled (hash map lookup).
    auto* bytecode_module = rt.get_or_compile_module(script);
    if (!bytecode_module) { return; }

    auto* fn = bytecode_module->get_function("on_effects");
    if (!fn) { return; }

    const auto marshal_start = timing_stats ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};

    nw::Vector<nw::smalls::Value> args;
    auto object_value = nw::smalls::Value::make_object(obj->handle());
    object_value.type_id = rt.object_subtype_for_tag(obj->handle().type);
    args.push_back(object_value);

    nw::Vector<nw::TypedHandle> pinned_handles;
    pinned_handles.reserve(effects.size());

    nw::Vector<nw::smalls::Value> marshaled_effects;
    marshaled_effects.reserve(effects.size());
    nw::smalls::TypeID effect_value_type = nw::smalls::invalid_type_id;

    for (const auto* effect : effects) {
        if (!effect) { continue; }
        auto effect_handle = effect->handle().to_typed_handle();
        auto effect_value = nw::smalls::detail::make_value(&rt, effect_handle);
        if (effect_value.type_id == nw::smalls::invalid_type_id) {
            if (timing_stats) { ++timing_stats->dropped_invalid_handles; }
            continue;
        }
        if (effect_value_type == nw::smalls::invalid_type_id) {
            effect_value_type = effect_value.type_id;
        }
        // Pin as ENGINE_OWNED to prevent GC from collecting the handle cell
        // during marshaling and VM execution. Released below after execute_compiled.
        rt.set_handle_ownership(effect_handle, nw::smalls::OwnershipMode::ENGINE_OWNED);
        pinned_handles.push_back(effect_handle);
        marshaled_effects.push_back(effect_value);
    }

    auto release_pins = [&]() {
        for (const auto& h : pinned_handles) {
            rt.set_handle_ownership(h, nw::smalls::OwnershipMode::VM_OWNED);
        }
    };

    if (effect_value_type == nw::smalls::invalid_type_id || marshaled_effects.empty()) { return; }

    auto array_ptr = rt.create_array_typed(effect_value_type, marshaled_effects.size());
    if (!array_ptr.value) { release_pins(); return; }

    auto* array = rt.get_array_typed(array_ptr);
    if (!array) { release_pins(); return; }

    for (const auto& v : marshaled_effects) { array->append_value(v, rt); }

    auto* array_header = rt.heap_.get_header(array_ptr);
    if (!array_header) { release_pins(); return; }

    args.push_back(nw::smalls::Value::make_heap(array_ptr, array_header->type_id));
    args.push_back(nw::smalls::Value::make_bool(is_apply));

    if (timing_stats) {
        timing_stats->marshal_ns += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - marshal_start).count());
    }

    const auto dispatch_start = timing_stats ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    auto result = rt.execute_compiled(bytecode_module, fn, args);

    // For apply: keep handles ENGINE_OWNED so the GC cannot collect them while
    // the effects remain on the object — required for the remove dispatch to
    // read effect data (e.g. amount) from a valid handle cell.
    // For remove: release to VM_OWNED; the effects are being torn down so the
    // cells can be collected once the engine destroys the C++ Effect objects.
    if (!is_apply) {
        release_pins();
    }

    if (timing_stats) {
        timing_stats->dispatch_ns += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - dispatch_start).count());
    }

    if (!result.ok()) {
        LOG_F(WARNING, "[effects] on_effects failed: {}", result.error_message);
    }
}

} // namespace

DEFINE_RULE_TYPE(EffectType);

Effect::Effect()
    : Effect(EffectType::invalid())
{
}

Effect::Effect(EffectType type_)
{
    std::fill(ints(), ints() + ints_count, 0);
    std::fill(floats(), floats() + floats_count, 0.0f);
    for (uint16_t i = 0; i < strings_count; ++i) {
        new (strings() + i) String();
    }

    handle_.type = type_;
}

Effect::~Effect()
{
    for (uint16_t i = 0; i < strings_count; ++i) {
        strings()[i].~String();
    }
}

void Effect::reset()
{
    handle_.type = EffectType::invalid();
    handle_.category = EffectCategory::magical;
    handle_.subtype = -1;
    handle_.creator = ObjectHandle{};
    handle_.effect = nullptr;
    handle_.runtime_handle = TypedHandle{};
    duration = 0.0f;
    expire_day = 0;
    expire_time = 0;

    versus_ = Versus{};
    std::fill(ints(), ints() + ints_count, 0);
    std::fill(floats(), floats() + floats_count, 0.0f);
    for (uint16_t i = 0; i < strings_count; ++i) {
        strings()[i].clear();
    }
}

float Effect::get_float(size_t index) const noexcept
{
    return index < floats_count ? floats()[index] : 0.0f;
}

int Effect::get_int(size_t index) const noexcept
{
    return index < ints_count ? ints()[index] : 0;
}

StringView Effect::get_string(size_t index) const noexcept
{
    return index < strings_count ? strings()[index] : StringView{};
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
    if (index < floats_count) { floats()[index] = value; }
}

void Effect::set_int(size_t index, int value)
{
    if (index < ints_count) { ints()[index] = value; }
}

void Effect::set_string(size_t index, String value)
{
    if (index < strings_count) { strings()[index] = std::move(value); }
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

    auto comp = [](const EffectHandle& lhs, const EffectHandle& rhs) {
        return std::tie(lhs.type, lhs.subtype) < std::tie(rhs.type, rhs.subtype);
    };
    auto it = std::lower_bound(std::begin(effects_), std::end(effects_), needle, comp);
    effects_.insert(it, needle);
    ++effect_version;

    return true;
}

void EffectArray::clear()
{
    if (effects_.empty()) { return; }

    effects_.clear();
    ++effect_version;
}

void EffectArray::erase(EffectArray::iterator first, EffectArray::iterator last)
{
    if (first != last) { ++effect_version; }
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
    if (count > 0) { ++effect_version; }
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
    , pool_()
{
}

bool EffectSystem::add(EffectType type, EffectFunc apply, EffectFunc remove)
{
    return add(type, effect_object_mask_all, effect_event_none, std::move(apply), std::move(remove), false);
}

bool EffectSystem::add(EffectType type, uint32_t object_mask, EffectFunc apply, EffectFunc remove)
{
    return add(type, object_mask, effect_event_none, std::move(apply), std::move(remove), false);
}

bool EffectSystem::add(EffectType type, uint32_t object_mask, uint32_t event_mask, EffectFunc apply, EffectFunc remove,
    bool has_versus_component)
{
    auto [it, added] = registry_.try_emplace(*type);
    it->second.object_mask = object_mask;
    it->second.event_mask = event_mask;
    it->second.has_versus_component = has_versus_component;
    it->second.apply = std::move(apply);
    it->second.remove = std::move(remove);
    return added;
}

bool EffectSystem::apply(ObjectBase* obj, Effect* effect)
{
    if (!effect) { return false; }
    auto it = registry_.find(*effect->handle().type);
    if (it == std::end(registry_)) { return false; }

    if (!obj) { return false; }
    uint32_t obj_mask = effect_object_mask(obj->handle().type);
    if ((it->second.object_mask & obj_mask) == 0) {
        return false;
    }

    if (it->second.apply && it->second.apply(obj, effect)) {
        return true;
    }
    if (!it->second.apply) {
        return true;
    }
    return false;
}

bool EffectSystem::apply_to(ObjectBase* obj, Effect* effect)
{
    if (!obj || !effect) {
        return false;
    }

    if (!apply(obj, effect)) {
        return false;
    }

    if (!obj->effects().add(effect)) {
        return false;
    }

    dispatch_on_effects(obj, {effect}, true);

    if (event_callback_) {
        event_callback_(obj, effect, true);
    }

    return true;
}

size_t EffectSystem::apply_to(ObjectBase* obj, const Vector<Effect*>& effects, Vector<Effect*>* failed)
{
    if (!obj) {
        if (failed) {
            failed->insert(std::end(*failed), std::begin(effects), std::end(effects));
        }
        return 0;
    }

    Vector<Effect*> applied_effects;
    applied_effects.reserve(effects.size());
    size_t applied = 0;
    for (auto* effect : effects) {
        if (!effect) { continue; }
        if (!apply(obj, effect) || !obj->effects().add(effect)) {
            if (failed) { failed->push_back(effect); }
            continue;
        }
        ++applied;
        applied_effects.push_back(effect);
    }

    if (!applied_effects.empty()) {
        auto* timing = callback_timing_enabled_ ? &callback_timing_stats_ : nullptr;
        if (timing) {
            timing->effects_scanned += effects.size();
            timing->effects_dispatched += applied_effects.size();
        }
        dispatch_on_effects(obj, applied_effects, true, timing);
        if (timing) { ++timing->batches; }
        if (event_batch_callback_) { event_batch_callback_(obj, applied_effects, true); }
    }

    return applied;
}

Effect* EffectSystem::create(EffectType type)
{
    TypedHandle handle = pool_.allocate_effect();
    if (!handle.is_valid()) { return nullptr; }

    auto* effect = static_cast<Effect*>(pool_.get(handle));
    if (!effect) { return nullptr; }

    effect->reset();
    effect->handle().type = type;
    effect->handle().effect = effect;
    effect->handle().runtime_handle = handle;

    return effect;
}

Effect* EffectSystem::get(TypedHandle handle)
{
    if (!handle.is_valid() || handle.type != RuntimeObjectPool::TYPE_EFFECT) {
        return nullptr;
    }
    return static_cast<Effect*>(pool_.get(handle));
}

const Effect* EffectSystem::get(TypedHandle handle) const
{
    if (!handle.is_valid() || handle.type != RuntimeObjectPool::TYPE_EFFECT) {
        return nullptr;
    }
    return static_cast<const Effect*>(pool_.get(handle));
}

void EffectSystem::destroy(Effect* effect)
{
    if (!effect) { return; }
    pool_.destroy(effect->handle().to_typed_handle());
}

void EffectSystem::initialize(kernel::ServiceInitTime time)
{
    NW_PROFILE_SCOPE_N("effects.initialize");

    if (time != kernel::ServiceInitTime::kernel_start && time != kernel::ServiceInitTime::module_post_load) {
        return;
    }

    auto start = std::chrono::high_resolution_clock::now();

    LOG_F(INFO, "kernel: effect system initializing...");

    {
        NW_PROFILE_SCOPE_N("effects.initialize.cost_tables");
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
    }

    {
        NW_PROFILE_SCOPE_N("effects.initialize.param_tables");
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
    }

    {
        NW_PROFILE_SCOPE_N("effects.initialize.definitions");
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
    }

    {
        NW_PROFILE_SCOPE_N("effects.initialize.baseitem_table");
        LOG_F(INFO, "  ... loading item property baseitem table");
        itemprop_table_ = kernel::twodas().get("itemprops");
    }

    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    LOG_F(INFO, "kernel: effect system initialized. ({}ms)",
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
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

    if (!obj) { return false; }
    uint32_t obj_mask = effect_object_mask(obj->handle().type);
    if ((it->second.object_mask & obj_mask) == 0) {
        return false;
    }

    if (it->second.remove && it->second.remove(obj, effect)) {
        return true;
    }
    if (!it->second.remove) {
        return true;
    }
    return false;
}

bool EffectSystem::remove_from(ObjectBase* obj, Effect* effect)
{
    if (!obj || !effect) {
        return false;
    }

    if (!remove(obj, effect)) {
        return false;
    }

    if (!obj->effects().remove(effect)) {
        return false;
    }

    dispatch_on_effects(obj, {effect}, false);

    if (event_callback_) {
        event_callback_(obj, effect, false);
    }

    return true;
}

size_t EffectSystem::remove_from(ObjectBase* obj, const Vector<Effect*>& effects,
    bool destroy, Vector<Effect*>* failed)
{
    if (!obj) {
        if (failed) {
            failed->insert(std::end(*failed), std::begin(effects), std::end(effects));
        }
        return 0;
    }

    Vector<Effect*> removed_effects;
    removed_effects.reserve(effects.size());
    size_t removed = 0;
    for (auto* effect : effects) {
        if (!effect) { continue; }
        if (!remove(obj, effect) || !obj->effects().remove(effect)) {
            if (failed) { failed->push_back(effect); }
            continue;
        }
        ++removed;
        removed_effects.push_back(effect);
    }

    if (!removed_effects.empty()) {
        auto* timing = callback_timing_enabled_ ? &callback_timing_stats_ : nullptr;
        if (timing) {
            timing->effects_scanned += effects.size();
            timing->effects_dispatched += removed_effects.size();
        }
        dispatch_on_effects(obj, removed_effects, false, timing);
        if (timing) { ++timing->batches; }
        if (event_batch_callback_) { event_batch_callback_(obj, removed_effects, false); }
    }

    if (destroy) {
        for (auto* effect : removed_effects) { this->destroy(effect); }
    }

    return removed;
}

void EffectSystem::set_event_callback(EffectEventFunc callback)
{
    event_callback_ = callback;
}

void EffectSystem::set_event_batch_callback(EffectBatchEventFunc callback)
{
    event_batch_callback_ = callback;
}

void EffectSystem::set_callback_timing_enabled(bool enabled)
{
    callback_timing_enabled_ = enabled;
}

void EffectSystem::reset_callback_timing()
{
    callback_timing_stats_ = {};
}

EffectCallbackTimingStats EffectSystem::callback_timing_stats() const
{
    return callback_timing_stats_;
}

nlohmann::json EffectSystem::stats() const
{
    nlohmann::json j;
    j["effect system"] = {
        {"callback_timing_enabled", callback_timing_enabled_},
        {"callback_timing", {
            {"filter_ns", callback_timing_stats_.filter_ns},
            {"marshal_ns", callback_timing_stats_.marshal_ns},
            {"dispatch_ns", callback_timing_stats_.dispatch_ns},
            {"batches", callback_timing_stats_.batches},
            {"effects_scanned", callback_timing_stats_.effects_scanned},
            {"effects_dispatched", callback_timing_stats_.effects_dispatched},
            {"dropped_invalid_handles", callback_timing_stats_.dropped_invalid_handles},
        }},
    };
    return j;
}

} // namespace nw
