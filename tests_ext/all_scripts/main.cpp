#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Resources.hpp>
#include <nw/script/Nss.hpp>
#include <nwn1/Profile.hpp>

#include <nowide/cstdlib.hpp>

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

    int thrown = 0;
    int errors = 0;
    int warnings = 0;

    auto callback = [&](const nw::Resource& res) {
        if (res.type != nw::ResourceType::nss) { return; }

        try {
            nw::script::Nss nss{nw::kernel::resman().demand(res)};
            nss.parse();
            nss.process_includes();
            nss.resolve();

            errors += nss.errors();
            warnings += nss.warnings();
        } catch (const std::runtime_error&) {
            ++thrown;
        }
    };

    nw::kernel::resman().visit(callback);

    LOG_F(INFO, "processing all default nwscripts: throw: {}, errors: {}, warnings:{}", thrown, errors, warnings);

    return thrown;
}
