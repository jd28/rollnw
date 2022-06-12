#include "Effect.hpp"

namespace nw {

float Effect::get_float(size_t index) const
{
    return index < floats_.size() ? floats_[index] : 0.0f;
}

int Effect::get_int(size_t index) const
{
    return index < integers_.size() ? integers_[index] : 0;
}

std::string_view Effect::get_string(size_t index) const
{
    return index < strings_.size() ? strings_[index] : std::string_view{};
}

EffectHandle Effect::handle() const noexcept
{
    return {type_, this};
}

void Effect::set_float(size_t index, float value)
{
    if (index >= floats_.size()) {
        floats_.resize(index);
    }
    floats_[index] = value;
}

void Effect::set_int(size_t index, int value)
{
    if (index >= integers_.size()) {
        integers_.resize(index);
    }
    integers_[index] = value;
}

void Effect::set_string(size_t index, std::string_view value)
{
    if (index >= strings_.size()) {
        strings_.resize(index);
    }
    strings_[index] = std::string(value);
}

void Effect::set_versus(Versus vs) { versus_ = vs; }

const Versus& Effect::versus() const noexcept { return versus_; }

} // namespace nw
