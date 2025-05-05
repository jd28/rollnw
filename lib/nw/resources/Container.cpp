#include "Container.hpp"

#include "StaticDirectory.hpp"
#include "StaticErf.hpp"
#include "StaticKey.hpp"
#include "StaticZip.hpp"

namespace nw {

Container* resolve_container(const std::filesystem::path& p, const String& name)
{
    if (std::filesystem::is_directory(p / name)) {
        return new StaticDirectory{p / name};
    } else if (std::filesystem::exists(p / (name + ".hak"))) {
        return new StaticErf{p / (name + ".hak")};
    } else if (std::filesystem::exists(p / (name + ".erf"))) {
        return new StaticErf{p / (name + ".erf")};
    } else if (std::filesystem::exists(p / (name + ".mod"))) {
        return new StaticErf{p / (name + ".mod")};
    } else if (std::filesystem::exists(p / (name + ".zip"))) {
        return new StaticZip{p / (name + ".zip")};
    } else if (std::filesystem::exists(p / (name + ".key"))) {
        return new StaticKey{p / (name + ".key")};
    }
    return nullptr;
}

} // namespace nw
