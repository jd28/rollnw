#include <nw/kernel/Config.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/model/Mdl.hpp>
#include <nw/resources/assets.hpp>

#include <nowide/cstdlib.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <utility>

namespace {

constexpr size_t max_input_size = 256u * 1024u;

void configure_kernel()
{
    namespace fs = std::filesystem;
    static bool configured = false;
    if (configured) { return; }

    const fs::path repo_root = fs::path(__FILE__).parent_path().parent_path();
    const fs::path user_root = repo_root / "tests" / "test_data" / "user";

    fs::path install_root;
    if (auto* p = nowide::getenv("NWN_ROOT")) {
        install_root = fs::path(p);
    } else {
        install_root = repo_root / "nwn";
    }

    nw::kernel::config().set_paths(install_root, user_root);
    nw::kernel::config().initialize();
    configured = true;
}

struct KernelServicesScope {
    KernelServicesScope()
    {
        configure_kernel();
        nw::kernel::services().start();
    }

    ~KernelServicesScope()
    {
        nw::kernel::services().shutdown();
    }

    KernelServicesScope(const KernelServicesScope&) = delete;
    KernelServicesScope& operator=(const KernelServicesScope&) = delete;
};

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (!data || size == 0 || size > max_input_size) { return 0; }

    try {
        KernelServicesScope kernel;

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
