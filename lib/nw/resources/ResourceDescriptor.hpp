#pragma once

#include "Resource.hpp"

namespace nw {

struct Container;

struct ResourceDescriptor {
    Resource name;
    int64_t size = 0;
    int64_t mtime = 0;
    Container* parent = nullptr;

    operator bool() { return !!parent; }
};

} // namespace nw
