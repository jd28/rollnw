#include "Container.hpp"

#include "StaticDirectory.hpp"
#include "StaticErf.hpp"
#include "StaticKey.hpp"
#include "StaticZip.hpp"

namespace nw {

Container::Container(nw::MemoryResource* allocator)
    : allocator_(allocator)
{
}

nw::MemoryResource* Container::allocator() const noexcept
{
    return allocator_;
}

Container* resolve_container(const std::filesystem::path& p, const String& name,
    nw::MemoryResource* allocator)
{
    if (std::filesystem::is_directory(p / name)) {
        auto ptr = allocator->allocate(sizeof(StaticDirectory), alignof(StaticDirectory));
        return new (ptr) StaticDirectory{p / name, allocator};
    } else if (std::filesystem::exists(p / (name + ".hak"))) {
        auto ptr = allocator->allocate(sizeof(StaticErf), alignof(StaticErf));
        return new (ptr) StaticErf{p / (name + ".hak"), allocator};
    } else if (std::filesystem::exists(p / (name + ".erf"))) {
        auto ptr = allocator->allocate(sizeof(StaticErf), alignof(StaticErf));
        return new (ptr) StaticErf{p / (name + ".erf"), allocator};
    } else if (std::filesystem::exists(p / (name + ".mod"))) {
        auto ptr = allocator->allocate(sizeof(StaticErf), alignof(StaticErf));
        return new (ptr) StaticErf{p / (name + ".mod"), allocator};
    } else if (std::filesystem::exists(p / (name + ".zip"))) {
        auto ptr = allocator->allocate(sizeof(StaticZip), alignof(StaticZip));
        return new (ptr) StaticZip{p / (name + ".zip"), allocator};
    } else if (std::filesystem::exists(p / (name + ".key"))) {
        auto ptr = allocator->allocate(sizeof(StaticKey), alignof(StaticKey));
        return new (ptr) StaticKey{p / (name + ".key"), allocator};
    }
    return nullptr;
}

} // namespace nw
