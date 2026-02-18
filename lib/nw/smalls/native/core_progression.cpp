#include "../runtime.hpp"

#include "../../kernel/Rules.hpp"

namespace nw::smalls {

void register_core_progression(Runtime& rt)
{
    if (rt.get_native_module("nwn1.progression")) {
        return;
    }

    rt.module("nwn1.progression")
        .function("get_base_attack_bonus", +[](int32_t class_type, int32_t levels) -> int32_t {
            if (levels <= 0) {
                return 0;
            }

            auto cl = nw::Class::make(class_type);
            if (cl == nw::Class::invalid()) {
                return 0;
            }

            return nw::kernel::rules().classes.get_base_attack_bonus(cl, levels); })
        .finalize();
}

} // namespace nw::smalls
