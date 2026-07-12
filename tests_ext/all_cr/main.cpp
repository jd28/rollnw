#include <nw/kernel/Kernel.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/profiles/nwn1/Profile.hpp>
#include <nw/profiles/nwn1/scriptbridge.hpp>
#include <nw/resources/ResourceManager.hpp>

#include <nowide/cstdlib.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>

namespace {

std::optional<float> imported_creature_cr(const nw::Creature* creature)
{
    if (!creature) { return std::nullopt; }

    auto& rt = nw::kernel::runtime();
    auto tid = rt.type_id("core.creature.CreatureStats", false);
    if (tid == nw::smalls::invalid_type_id) { return std::nullopt; }

    auto stats = rt.find_propset_ref(tid, creature->handle());
    if (stats.type_id == nw::smalls::invalid_type_id) { return std::nullopt; }

    const nw::smalls::StructDef* def = rt.get_struct_def(stats.type_id);
    if (!def) { return std::nullopt; }

    uint32_t index = def->field_index("cr");
    if (index == UINT32_MAX) { return std::nullopt; }

    const auto& fd = def->fields[index];
    auto value = rt.read_value_field_at_offset(stats, fd.offset, rt.float_type());
    if (value.type_id != rt.float_type()) { return std::nullopt; }
    return value.data.fval;
}

std::optional<float> calculated_creature_cr(const nw::Creature* creature)
{
    if (!creature) { return std::nullopt; }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nwn1::bridge::make_object_arg(creature->handle()));
    return nwn1::bridge::call_nwn1_module_float("nwn1.creature", "calculate_challenge_rating", args);
}

} // namespace

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
        auto game_cr = imported_creature_cr(cre);
        auto rollnw_cr = calculated_creature_cr(cre);
        if (!game_cr || !rollnw_cr) { return; }
        const float mismatch = std::abs(*game_cr - *rollnw_cr);
        if (mismatch > 0.1f) {
            LOG_F(INFO, "Challenge Rating Mismatch: {} - {} != {}", res.filename(), *game_cr, *rollnw_cr);
            mismatch_amount += mismatch;
            biggest_mismatch = std::max(biggest_mismatch, mismatch);
            if (mismatch > 5) {
                LOG_F(INFO, "^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^");
            }
            ++invalid;
        }
        ++count;
    };

    nw::kernel::resman().visit(callback);

    const float average_mismatch = invalid == 0 ? 0.0f : mismatch_amount / static_cast<float>(invalid);
    LOG_F(INFO, "processing all creatures {}, {} invalid, average mismatch: {}, biggent mismatch: {}",
        count, invalid, average_mismatch, biggest_mismatch);

    return 0;
}
