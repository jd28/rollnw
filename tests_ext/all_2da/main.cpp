#include <nw/formats/StaticTwoDA.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Resources.hpp>
#include <nw/profiles/nwn1/Profile.hpp>

#include <mimalloc-new-delete.h>
#include <nowide/cstdlib.hpp>

#include <chrono>

int main(int argc, char* argv[])
{
    nw::init_logger(argc, argv);

    nw::kernel::config().set_paths("", "test_data/user/");
    nw::kernel::config().initialize();
    nw::kernel::services().start();

    std::string biggest_name;
    int64_t biggest = 0;
    int64_t sum = 0;
    int count = 0;
    size_t invalid = 0;

    auto callback = [&](const nw::Resource& res) {
        if (res.type != nw::ResourceType::twoda) { return; }
        auto start = std::chrono::high_resolution_clock::now();
        nw::StaticTwoDA tda{nw::kernel::resman().demand(res)};
        if (!tda.is_valid()) {
            ++invalid;
        }
        auto stop = std::chrono::high_resolution_clock::now();
        auto total = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count();
        if (total > biggest) {
            biggest = total;
            biggest_name = res.filename();
        }
        sum += total;
        ++count;
    };

    nw::kernel::resman().visit(callback);

    LOG_F(INFO, "processing all default {} 2da, {} invalid", count, invalid);
    LOG_F(INFO, "  total: {}ns, avg processing time: {}ns, longest: {}ns, longest name: {}", sum, sum / count, biggest, biggest_name);

    return 0;
}
