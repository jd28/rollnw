#include "platform.hpp"

#include <cstdlib>

namespace nw {

NWNPaths get_nwn_paths()
{
    // [TODO] PUNT, for now.
    NWNPaths result;
    result.user = std::getenv("NWN_HOME");
    result.install = std::getenv("NWN_ROOT");
    return result;
}

} // namespace nw
