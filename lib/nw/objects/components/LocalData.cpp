#include "LocalData.hpp"

namespace nw {

LocalData::LocalData(const GffInputArchiveStruct gff)
{
    auto st = gff["VarTable"];
    if (!st.valid()) { return; }
    size_t sz = st.size();
    std::string name;
    uint32_t type, obj;

    for (size_t i = 0; i < sz; ++i) {
        if (!st[i].get_to("Name", name)
            || !st[i].get_to("Type", type)) {
            LOG_F(ERROR, "local data invalid local var at index {}", i);
            break;
        }

        auto& payload = vars_[name];

        switch (type) {
        default:
            LOG_F(ERROR, "local data invalid local var type at index {}", i);
            return;
        case 1: // int
            st[i].get_to("Value", payload.integer);
            break;
        case 2: // float
            st[i].get_to("Value", payload.float_);
            break;
        case 3: // string
            st[i].get_to("Value", payload.string);
            break;
        case 4: // object
            st[i].get_to("Value", obj);
            payload.object = static_cast<ObjectID>(obj);
            break;
        case 5: { // location
            if (auto s = st[i].get<GffInputArchiveStruct>("Value")) {
                payload.loc = Location{*s, SerializationProfile::any};
            } else {
                LOG_F(ERROR, "failed to read location struct");
            }
        } break;
        }
    }
}

float LocalData::get_local_float(absl::string_view var) const
{
    auto it = vars_.find(var);
    return it != vars_.end() ? it->second.float_ : 0.0f;
}

int32_t LocalData::get_local_int(absl::string_view var) const
{
    auto it = vars_.find(var);
    return it != vars_.end() ? it->second.integer : 0;
}

ObjectID LocalData::get_local_object(absl::string_view var) const
{
    auto it = vars_.find(var);
    return it != vars_.end() ? it->second.object : object_invalid;
}

std::string LocalData::get_local_string(absl::string_view var) const
{
    auto it = vars_.find(var);
    return it != vars_.end() ? it->second.string : std::string{};
}

Location LocalData::get_local_location(absl::string_view var) const
{
    auto it = vars_.find(var);
    return it != vars_.end() ? it->second.loc : Location{};
}

} // namespace nw
