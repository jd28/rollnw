#pragma once

#include "../formats/Nss.hpp"
#include "../resources/Resource.hpp"
#include "Kernel.hpp"

#include <absl/container/flat_hash_map.h>

#include <memory>

namespace nw {
namespace script {
struct Script;
} // namespace script

namespace kernel {

struct ScriptCache : public kernel::Service {
    script::Script* get(Resref resref);

    virtual void clear() override;
    virtual void initialize() override;

private:
    absl::flat_hash_map<Resource, std::unique_ptr<script::Script>> cache_;
};

} // namespace kernel

} // namespace nw
