#include "Rules.hpp"

#include "../formats/TwoDA.hpp"
#include "Kernel.hpp"

namespace nw::kernel {

Rules::~Rules()
{
    clear();
}

void Rules::clear()
{
}

bool Rules::initialize()
{
    // Stub
    return true;
}

} // namespace nw::kernel
