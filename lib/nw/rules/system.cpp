#include "system.hpp"

namespace nw {

// == Constant ================================================================

size_t Constant::as_index() const
{
    return static_cast<size_t>(std::get<int>(value));
}

int Constant::as_int() const
{
    return std::get<int>(value);
}

float Constant::as_float() const
{
    return std::get<float>(value);
}

const std::string& Constant::as_string() const
{
    return std::get<std::string>(value);
}

bool Constant::is_int() const noexcept
{
    return std::holds_alternative<int>(value);
}

bool Constant::is_float() const noexcept
{
    return std::holds_alternative<float>(value);
}

bool Constant::is_string() const noexcept
{
    return std::holds_alternative<std::string>(value);
}

bool operator==(const Constant& lhs, const Constant& rhs)
{
    return std::tie(lhs.name, lhs.value) == std::tie(rhs.name, rhs.value);
}

} // namespace nw
