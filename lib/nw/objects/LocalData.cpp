#include "LocalData.hpp"

#include "../serialization/Gff.hpp"
#include "../serialization/GffBuilder.hpp"

#include <nlohmann/json.hpp>

namespace nw {

void LocalData::clear(StringView var, uint32_t type)
{
    auto it = vars_.find(var);
    if (it == std::end(vars_)) {
        return;
    }

    switch (type) {
    default:
        LOG_F(ERROR, "local data invalid local var type: {}", type);
    case 1: // int
        it->second.integer = 0;
        it->second.flags.reset(LocalVarType::integer);
        break;
    case 2: // float
        it->second.float_ = 0.0;
        it->second.flags.reset(LocalVarType::float_);
        break;
    case 3: // string
        it->second.string = "";
        it->second.flags.reset(LocalVarType::string);
        break;
    case 4: // object
        it->second.object = object_invalid;
        it->second.flags.reset(LocalVarType::object);
        break;
    case 5: // location
        it->second.loc = Location{};
        it->second.flags.reset(LocalVarType::location);
        break;
    }
    if (it->second.flags.none()) { vars_.erase(it); }
}

void LocalData::clear_all(uint32_t type)
{
    static Vector<StringView> eraser;

    for (auto& [k, v] : vars_) {
        switch (type) {
        default:
            LOG_F(ERROR, "local data invalid local var type: {}", type);
        case 1: // int
            v.integer = 0;
            v.flags.reset(LocalVarType::integer);
            break;
        case 2: // float
            v.float_ = 0.0;
            v.flags.reset(LocalVarType::float_);
            break;
        case 3: // string
            v.string = "";
            v.flags.reset(LocalVarType::string);
            break;
        case 4: // object
            v.object = object_invalid;
            v.flags.reset(LocalVarType::object);
            break;
        case 5: // location
            v.loc = Location{};
            v.flags.reset(LocalVarType::location);
            break;
        }
        if (v.flags.none()) { eraser.push_back(k); }
    }

    for (const auto& e : eraser) {
        vars_.erase(e);
    }
    eraser.clear();
}

void LocalData::delete_float(StringView var)
{
    auto it = vars_.find(var);
    if (it == std::end(vars_)) {
        return;
    }
    it->second.float_ = 0.0;
    it->second.flags.reset(LocalVarType::float_);
    if (it->second.flags.none()) { vars_.erase(it); }
}

void LocalData::delete_int(StringView var)
{
    auto it = vars_.find(var);
    if (it == std::end(vars_)) {
        return;
    }
    it->second.integer = 0;
    it->second.flags.reset(LocalVarType::integer);
    if (it->second.flags.none()) { vars_.erase(it); }
}

void LocalData::delete_object(StringView var)
{
    auto it = vars_.find(var);
    if (it == std::end(vars_)) {
        return;
    }
    it->second.object = object_invalid;
    it->second.flags.reset(LocalVarType::object);
    if (it->second.flags.none()) { vars_.erase(it); }
}

void LocalData::delete_string(StringView var)
{
    auto it = vars_.find(var);
    if (it == std::end(vars_)) {
        return;
    }
    it->second.string = {};
    it->second.flags.reset(LocalVarType::string);
    if (it->second.flags.none()) { vars_.erase(it); }
}

void LocalData::delete_location(StringView var)
{
    auto it = vars_.find(var);
    if (it == std::end(vars_)) {
        return;
    }
    it->second.loc = nw::Location();
    it->second.flags.reset(LocalVarType::location);
    if (it->second.flags.none()) { vars_.erase(it); }
}

float LocalData::get_float(StringView var) const
{
    auto it = vars_.find(var);
    return it != vars_.end() ? it->second.float_ : 0.0f;
}

int32_t LocalData::get_int(StringView var) const
{
    auto it = vars_.find(var);
    return it != vars_.end() ? it->second.integer : 0;
}

ObjectID LocalData::get_object(StringView var) const
{
    auto it = vars_.find(var);
    return it != vars_.end() ? it->second.object : object_invalid;
}

String LocalData::get_string(StringView var) const
{
    auto it = vars_.find(var);
    return it != vars_.end() ? it->second.string : String{};
}

Location LocalData::get_location(StringView var) const
{
    auto it = vars_.find(var);
    return it != vars_.end() ? it->second.loc : Location{};
}

void LocalData::set_float(StringView var, float value)
{
    auto& payload = vars_[var];
    payload.float_ = value;
    payload.flags.set(LocalVarType::float_);
}

void LocalData::set_int(StringView var, int32_t value)
{
    auto& payload = vars_[var];
    payload.integer = value;
    payload.flags.set(LocalVarType::integer);
}

void LocalData::set_object(StringView var, ObjectID value)
{
    auto& payload = vars_[var];
    payload.object = value;
    payload.flags.set(LocalVarType::object);
}

void LocalData::set_string(StringView var, StringView value)
{
    auto& payload = vars_[var];
    payload.string = String(value);
    payload.flags.set(LocalVarType::string);
}

void LocalData::set_location(StringView var, Location value)
{
    auto& payload = vars_[var];
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
                payload.string = it->get<String>();
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

bool deserialize(LocalData& self, const GffStruct& archive)
{
    auto st = archive["VarTable"];
    if (!st.valid()) {
        return false;
    }
    size_t sz = st.size();
    String name;
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

} // namespace nw
