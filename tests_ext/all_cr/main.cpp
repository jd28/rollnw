#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Objects.hpp>
#include <nw/kernel/Resources.hpp>
#include <nw/profiles/nwn1/Profile.hpp>
#include <nw/profiles/nwn1/scriptapi.hpp>

#include <mimalloc-new-delete.h>
#include <nowide/cstdlib.hpp>

#include <chrono>

int main(int argc, char* argv[])
{
    nw::init_logger(argc, argv);

    nw::kernel::config().set_paths("", "test_data/user/");
    nw::kernel::config().initialize();
    nw::kernel::services().start();

    int count = 0;
    size_t invalid = 0;
    float mismatch_amount = 0.0f;
    float biggest_mismatch = 0.0f;

    auto callback = [&](const nw::Resource& res) {
        if (res.type != nw::ResourceType::utc) { return; }

        if (res.resref.view() == "x3_halaster") { return; } // Broken

        // Skip all animal companions, familiars, and henchmen.
        if (res.resref.view().find("_ac_") != std::string::npos
            || res.resref.view().find("_fm_") != std::string::npos
            || res.resref.view().find("_hen_") != std::string::npos
            || res.resref.view().find("_s_") != std::string::npos) {
            return;
        }
        auto cre = nw::kernel::objects().load<nw::Creature>(res.resref);
        auto game_cr = cre->cr;
        auto rollnw_cr = nwn1::calculate_challenge_rating(cre);
        if (std::abs(game_cr - rollnw_cr) > 0.1) {
            LOG_F(INFO, "Challenge Rating Mismatch: {} - {} != {}", res.filename(), game_cr, rollnw_cr);
            mismatch_amount += std::abs(game_cr - rollnw_cr);
            biggest_mismatch = std::max(biggest_mismatch, std::abs(game_cr - rollnw_cr));
            if (std::abs(game_cr - rollnw_cr) > 5) {
                LOG_F(INFO, "^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^");
            }
            ++invalid;
        }
        ++count;
    };

    nw::kernel::resman().visit(callback);

    LOG_F(INFO, "processing all creatures {}, {} invalid, average mismatch: {}, biggent mismatch: {}",
        count, invalid, mismatch_amount / invalid, biggest_mismatch);

    return 0;
}
