#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Resources.hpp>
#include <nw/script/Nss.hpp>
#include <nwn1/Profile.hpp>

#include <nowide/cstdlib.hpp>

#include <chrono>

int main(int argc, char* argv[])
{
    nw::init_logger(argc, argv);

    auto info = nw::probe_nwn_install();
    nw::kernel::config().initialize({
        info.version,
        info.install,
        "test_data/user/",
    });

    nw::kernel::services().start();
    nw::kernel::load_profile(new nwn1::Profile);

    std::string biggest_name;
    int64_t biggest = 0;
    int64_t sum = 0;
    int count = 0;
    int thrown = 0;
    size_t errors = 0;
    size_t warnings = 0;

    auto callback = [&](const nw::Resource& res) {
        if (res.type != nw::ResourceType::nss) { return; }

        try {
            auto start = std::chrono::high_resolution_clock::now();
            auto ctx = std::make_unique<nw::script::Context>();
            nw::script::Nss nss{nw::kernel::resman().demand(res), ctx.get()};
            nss.parse();
            nss.process_includes();
            nss.resolve();

            auto stop = std::chrono::high_resolution_clock::now();
            auto total = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count();
            if (total > biggest) {
                biggest = total;
                biggest_name = res.filename();
            }
            sum += total;
            ++count;

            errors += nss.errors();
            warnings += nss.warnings();
        } catch (const std::runtime_error&) {
            ++thrown;
        }
    };

    nw::kernel::resman().visit(callback);

    LOG_F(INFO, "processing all default nwscripts: throw: {}, errors: {}, warnings:{}", thrown, errors, warnings);
    LOG_F(INFO, "  avg processing time: {}, longest: {}, longest name: {}", sum / count, biggest, biggest_name);

    return thrown;
}
