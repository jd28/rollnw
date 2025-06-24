#include <nw/kernel/Kernel.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/script/Nss.hpp>

#include <nowide/cstdlib.hpp>

#include <chrono>

int main(int argc, char* argv[])
{
    nw::init_logger(argc, argv);

    nw::kernel::config().set_paths("", "test_data/user/");
    nw::kernel::config().initialize({true, false});
    nw::kernel::services().start();

    std::string biggest_name;
    int64_t biggest = 0;
    int64_t sum = 0;
    int count = 0;
    int invalid = 0;

    auto callback = [&](const nw::Resource& res) {
        if (!nw::ResourceType::check_category(nw::ResourceType::object, res.type)) { return; }

        auto start = std::chrono::high_resolution_clock::now();
        nw::ObjectBase* obj = nullptr;
        switch (res.type) {
        default:
            return;
        case nw::ResourceType::utc:
            obj = nw::kernel::objects().load<nw::Creature>(res.resref);
            break;
        case nw::ResourceType::utd:
            obj = nw::kernel::objects().load<nw::Door>(res.resref);
            break;
        case nw::ResourceType::ute:
            obj = nw::kernel::objects().load<nw::Encounter>(res.resref);
            break;
        case nw::ResourceType::uti:
            obj = nw::kernel::objects().load<nw::Item>(res.resref);
            break;
        case nw::ResourceType::utm:
            obj = nw::kernel::objects().load<nw::Store>(res.resref);
            break;
        case nw::ResourceType::utp:
            obj = nw::kernel::objects().load<nw::Placeable>(res.resref);
            break;
        case nw::ResourceType::uts:
            obj = nw::kernel::objects().load<nw::Sound>(res.resref);
            break;
        case nw::ResourceType::utt:
            obj = nw::kernel::objects().load<nw::Trigger>(res.resref);
            break;
        case nw::ResourceType::utw:
            obj = nw::kernel::objects().load<nw::Waypoint>(res.resref);
            break;
        }
        auto stop = std::chrono::high_resolution_clock::now();
        auto total = std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count();
        if (total > biggest) {
            biggest = total;
            biggest_name = res.filename();
        }
        sum += total;
        ++count;
        if (!obj) { ++invalid; }
    };

    nw::kernel::resman().visit(callback);

    LOG_F(INFO, "processing all default object blueprints: total: {}, invalid: {}", count, invalid);
    LOG_F(INFO, "  avg processing time: {}us, longest: {}us, longest name: {}", sum / count, biggest, biggest_name);

    return invalid;
}
