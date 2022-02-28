#pragma once

#include "Resource.hpp"

namespace nw {

struct Container;

struct ResourceDescriptor {
    Resource name;
    size_t size = 0;
    int64_t mtime = 0;
    const Container* parent = nullptr;

    operator bool() { return !!parent; }
};

} // namespace nw
