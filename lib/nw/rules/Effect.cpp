#include "Effect.hpp"

namespace nw {

Effect::Effect()
{
}

Effect::Effect(EffectType type_)
    : type{type_}
{
}

void Effect::clear()
{
    type = EffectType::invalid();
    category = EffectCategory::magical;
    subtype = -1;
    creator = ObjectHandle{};
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

std::string_view Effect::get_string(size_t index) const noexcept
{
    return index < strings_.size() ? strings_[index] : std::string_view{};
}

EffectHandle Effect::handle() noexcept
{
    return {type, subtype, creator, spell_id, category, this};
}

EffectID Effect::id() const noexcept
{
    return id_;
}

void Effect::set_float(size_t index, float value)
{
    if (index >= floats_.size()) {
        floats_.resize(index + 1);
    }
    floats_[index] = value;
}

void Effect::set_id(EffectID id)
{
    id_ = id;
}

void Effect::set_int(size_t index, int value)
{
    if (index >= integers_.size()) {
        integers_.resize(index + 1);
    }
    integers_[index] = value;
}

void Effect::set_string(size_t index, std::string value)
{
    if (index >= strings_.size()) {
        strings_.resize(index + 1);
    }
    strings_[index] = std::move(value);
}

void Effect::set_versus(Versus vs) { versus_ = vs; }

const Versus& Effect::versus() const noexcept { return versus_; }

} // namespace nw
