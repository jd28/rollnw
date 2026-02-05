#include "smalls_fuzz_common.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size == 0) { return 0; }

    nw::smalls::fuzz::ensure_kernel_started();

    try {
        std::string src = nw::smalls::fuzz::to_source(data, size);
        nw::smalls::fuzz::FuzzContext ctx;
        auto script = nw::smalls::fuzz::make_script(src, &ctx);
        script.parse();
    } catch (...) {
        // Parse failures are expected.
    }

    return 0;
}
