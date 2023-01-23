#include "LocalData.hpp"

#include <nlohmann/json.hpp>

namespace nw {

void LocalData::delete_float(std::string_view var)
{
    absl::string_view v{var.data(), var.size()};
    auto it = vars_.find(v);
    if (it == std::end(vars_)) {
        return;
    }
    it->second.float_ = 0.0;
    it->second.flags.reset(LocalVarType::float_);
}

void LocalData::delete_int(std::string_view var)
{
    absl::string_view v{var.data(), var.size()};
    auto it = vars_.find(v);
    if (it == std::end(vars_)) {
        return;
    }
    it->second.integer = 0;
    it->second.flags.reset(LocalVarType::integer);
}

void LocalData::delete_object(std::string_view var)
{
    absl::string_view v{var.data(), var.size()};
    auto it = vars_.find(v);
    if (it == std::end(vars_)) {
        return;
    }
    it->second.object = object_invalid;
    it->second.flags.reset(LocalVarType::object);
}

void LocalData::delete_string(std::string_view var)
{
    absl::string_view v{var.data(), var.size()};
    auto it = vars_.find(v);
    if (it == std::end(vars_)) {
        return;
    }
    it->second.string = {};
    it->second.flags.reset(LocalVarType::string);
}

void LocalData::delete_location(std::string_view var)
{
    absl::string_view v{var.data(), var.size()};
    auto it = vars_.find(v);
    if (it == std::end(vars_)) {
        return;
    }
    it->second.loc = nw::Location();
    it->second.flags.reset(LocalVarType::location);
}

float LocalData::get_float(std::string_view var) const
{
    absl::string_view v(var.data(), var.size());
    auto it = vars_.find(v);
    return it != vars_.end() ? it->second.float_ : 0.0f;
}

int32_t LocalData::get_int(std::string_view var) const
{
    absl::string_view v(var.data(), var.size());
    auto it = vars_.find(v);
    return it != vars_.end() ? it->second.integer : 0;
}

ObjectID LocalData::get_object(std::string_view var) const
{
    absl::string_view v(var.data(), var.size());
    auto it = vars_.find(v);
    return it != vars_.end() ? it->second.object : object_invalid;
}

std::string LocalData::get_string(std::string_view var) const
{
    absl::string_view v(var.data(), var.size());
    auto it = vars_.find(v);
    return it != vars_.end() ? it->second.string : std::string{};
}

Location LocalData::get_location(std::string_view var) const
{
    absl::string_view v(var.data(), var.size());
    auto it = vars_.find(v);
    return it != vars_.end() ? it->second.loc : Location{};
}

void LocalData::set_float(std::string_view var, float value)
{
    absl::string_view v(var.data(), var.size());
    auto& payload = vars_[v];
    payload.float_ = value;
    payload.flags.set(LocalVarType::float_);
}

void LocalData::set_int(std::string_view var, int32_t value)
{
    absl::string_view v(var.data(), var.size());
    auto& payload = vars_[v];
    payload.integer = value;
    payload.flags.set(LocalVarType::integer);
}

void LocalData::set_object(std::string_view var, ObjectID value)
{
    absl::string_view v(var.data(), var.size());
    auto& payload = vars_[v];
    payload.object = value;
    payload.flags.set(LocalVarType::object);
}

void LocalData::set_string(std::string_view var, std::string_view value)
{
    absl::string_view v(var.data(), var.size());
    auto& payload = vars_[v];
    payload.string = std::string(value);
    payload.flags.set(LocalVarType::string);
}

void LocalData::set_location(std::string_view var, Location value)
{
    absl::string_view v(var.data(), var.size());
    auto& payload = vars_[v];
    payload.loc = value;
    payload.flags.set(LocalVarType::location);
}

bool LocalData::from_json(const nlohmann::json& archive)
{
    try {
        for (const auto& [key, value] : archive.items()) {
            auto& payload = vars_[key];

            auto it = value.find("float");
            if (it != std::end(value)) {
                payload.float_ = it->get<float>();
                payload.flags.set(LocalVarType::float_);
            }
            it = value.find("integer");
            if (it != std::end(value)) {
                payload.integer = it->get<int>();
                payload.flags.set(LocalVarType::integer);
            }
            it = value.find("object");
            if (it != std::end(value)) {
                payload.object = static_cast<ObjectID>(it->get<uint32_t>());
                payload.flags.set(LocalVarType::object);
            }
            it = value.find("string");
            if (it != std::end(value)) {
                payload.string = it->get<std::string>();
                payload.flags.set(LocalVarType::string);
            }
            it = value.find("location");
            if (it != std::end(value)) {
                payload.loc = it->get<Location>();
                payload.flags.set(LocalVarType::location);
            }
        }
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "LocalData::from_json exception: {}", e.what());
        return false;
    }

    return true;
}

nlohmann::json LocalData::to_json(SerializationProfile profile) const
{
    nlohmann::json j = nlohmann::json::object();
    for (const auto& [key, value] : vars_) {
        if (!value.flags.any()) {
            continue;
        }
        auto& payload = j[key] = nlohmann::json::object();

        if (value.flags.test(LocalVarType::float_)) {
            payload["float"] = value.float_;
        } else if (value.flags.test(LocalVarType::integer)) {
            payload["integer"] = value.integer;
        } else if (value.flags.test(LocalVarType::string)) {
            payload["string"] = value.string;
        }
        if (profile != SerializationProfile::blueprint) {

            if (value.flags.test(LocalVarType::object)) {
                payload["object"] = value.object;
            } else if (value.flags.test(LocalVarType::location)) {
                payload["location"] = value.loc;
            }
        }
    }
    return j;
}

#ifdef ROLLNW_ENABLE_LEGACY
bool deserialize(LocalData& self, const GffStruct& archive)
{
    auto st = archive["VarTable"];
    if (!st.valid()) {
        return false;
    }
    size_t sz = st.size();
    std::string name;
    uint32_t type, obj;

    for (size_t i = 0; i < sz; ++i) {
        if (!st[i].get_to("Name", name)
            || !st[i].get_to("Type", type)) {
            LOG_F(ERROR, "local data invalid local var at index {}", i);
            break;
        }

        auto& payload = self.vars_[name];

        switch (type) {
        default:
            LOG_F(ERROR, "local data invalid local var type at index {}", i);
            self.vars_.clear();
            return false;
        case 1: // int
            st[i].get_to("Value", payload.integer);
            payload.flags.set(LocalVarType::integer);
            break;
        case 2: // float
            st[i].get_to("Value", payload.float_);
            payload.flags.set(LocalVarType::float_);
            break;
        case 3: // string
            st[i].get_to("Value", payload.string);
            payload.flags.set(LocalVarType::string);
            break;
        case 4: // object
            st[i].get_to("Value", obj);
            payload.object = static_cast<ObjectID>(obj);
            payload.flags.set(LocalVarType::object);
            break;
        case 5: { // location
            if (auto s = st[i].get<GffStruct>("Value")) {
                deserialize(payload.loc, *s, SerializationProfile::any);
                payload.flags.set(LocalVarType::location);
            } else {
                LOG_F(ERROR, "failed to read location struct");
                self.vars_.clear();
                return false;
            }
        } break;
        }
    }

    return true;
}

bool serialize(const LocalData& self, GffBuilderStruct& archive, SerializationProfile profile)
{
    if (self.vars_.empty()) {
        return true;
    }

    auto& list = archive.add_list("VarTable");

    for (const auto& [key, value] : self.vars_) {
        if (!value.flags.any()) {
            continue;
        }
        auto& payload = list.push_back(0).add_field("Name", key);

        if (value.flags.test(LocalVarType::float_)) {
            payload.add_field("Type", LocalVarType::float_);
            payload.add_field("Value", value.float_);
        } else if (value.flags.test(LocalVarType::integer)) {
            payload.add_field("Type", LocalVarType::integer);
            payload.add_field("Value", value.integer);
        } else if (value.flags.test(LocalVarType::string)) {
            payload.add_field("Type", LocalVarType::string);
            payload.add_field("Value", value.string);
        }
        if (profile != SerializationProfile::blueprint) {
            if (value.flags.test(LocalVarType::object)) {
                payload.add_field("Type", LocalVarType::object);
                payload.add_field("Value", static_cast<uint32_t>(value.object));
            } else if (value.flags.test(LocalVarType::location)) {
                payload.add_field("Type", LocalVarType::location);
                payload.add_struct("Value", 1)
                    .add_field("Area", static_cast<uint32_t>(value.loc.area))
                    .add_field("PositionX", value.loc.position.x)
                    .add_field("PositionY", value.loc.position.y)
                    .add_field("PositionZ", value.loc.position.z)
                    .add_field("OrientationX", value.loc.orientation.x)
                    .add_field("OrientationY", value.loc.orientation.y)
                    .add_field("OrientationZ", value.loc.orientation.z);
            }
        }
    }

    return true;
}

#endif

} // namespace nw
