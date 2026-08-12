#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/model/Mdl.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/resources/assets.hpp>

#include <nowide/cstdlib.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace {

constexpr size_t max_input_size = 256u * 1024u;

void ensure_model_resources()
{
    namespace fs = std::filesystem;
    static bool initialized = false;
    if (initialized) { return; }

    loguru::g_stderr_verbosity = loguru::Verbosity_OFF;

    const fs::path repo_root = fs::path(__FILE__).parent_path().parent_path();

    fs::path install_root;
    if (auto* p = nowide::getenv("NWN_ROOT")) {
        install_root = fs::path(p);
    } else {
        install_root = repo_root / "nwn";
    }

    // Text models can reference a supermodel. Build only the resource index
    // required to resolve those references; no game services are needed.
    nw::kernel::services().create(nw::kernel::ServiceMode::language);
    auto& resources = nw::kernel::resman();
    if (!resources.add_base_container(install_root / "data", "nwn_base")) {
        throw std::runtime_error("fuzz: unable to load NWN base resources");
    }
    resources.build_registry();
    initialized = true;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (!data || size == 0 || size > max_input_size) { return 0; }

    ensure_model_resources();

    try {
        nw::ResourceData resource;
        resource.name.type = nw::ResourceType::mdl;
        resource.bytes.append(data, size);
        if (resource.bytes[0] == 0) {
            resource.bytes[0] = '#';
        }

        nw::model::Mdl mdl{std::move(resource)};
        (void)mdl.valid();
    } catch (...) {
        // Invalid text model inputs are expected; memory safety is the fuzz target.
    }

    return 0;
}
