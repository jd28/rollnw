#include "platform.hpp"

#include <cstdlib>
#include <nowide/cstdlib.hpp>

namespace nw {

NWNPaths get_nwn_paths()
{
    // [TODO] PUNT, for now.
    NWNPaths result;

    if (auto p = nowide::getenv("NWN_HOME")) {
        result.user = p;
    }

    if (auto p = nowide::getenv("NWN_ROOT")) {
        result.install = p;
    }

    return result;
}

} // namespace nw
