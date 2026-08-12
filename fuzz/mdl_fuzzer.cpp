#include <nw/log.hpp>
#include <nw/model/Mdl.hpp>
#include <nw/resources/assets.hpp>

#include <cstddef>
#include <cstdint>
#include <utility>

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
    loguru::g_stderr_verbosity = loguru::Verbosity_OFF;
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    constexpr size_t max_input_size = 1u << 20;
    if (!data || size == 0 || size > max_input_size) { return 0; }

    try {
        nw::ResourceData resource;
        resource.bytes.append(data, size);
        resource.bytes[0] = 0;
        nw::model::Mdl mdl{std::move(resource)};
        (void)mdl.valid();
    } catch (...) {
        // Invalid model bytes are expected; memory safety is the fuzz target.
    }

    return 0;
}
