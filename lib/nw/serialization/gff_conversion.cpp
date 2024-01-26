#include "gff_conversion.hpp"

#include "../serialization/Serialization.hpp"
#include "../util/base64.hpp"
#include "Gff.hpp"

#include <nlohmann/json.hpp>

#include <limits>

using json = nlohmann::json;

namespace nw {

template <typename T>
inline void add_field_kv(json& j, const std::string& name, const T& value)
{
    auto& o = j[name] = json::object();
    o["value"] = value;
    o["type"] = SerializationType::to_string(SerializationType::id<T>());
}

bool gff_to_gffjson(const GffStruct str, nlohmann::json& cursor);

inline bool gff_to_gffjson(const GffField field, nlohmann::json& cursor)
{
    bool check = true;
    if (!field.valid()) {
        LOG_F(ERROR, "attempted to convert invalid field");
        return false;
    }

    // nholmann_json doesn't seem to support hetero lookup, bummer.
    std::string field_name = std::string(field.name());

    switch (field.type()) {
    default:
        LOG_F(ERROR, "attempted to convert invalid field type");
        return false;
    case SerializationType::uint8:
        add_field_kv(cursor, field_name, *field.get<uint8_t>());
        break;
    case SerializationType::int8: {
        add_field_kv(cursor, field_name, *field.get<int8_t>());
    } break;
    case SerializationType::uint16: {
        add_field_kv(cursor, field_name, *field.get<uint16_t>());
    } break;
    case SerializationType::int16: {
        add_field_kv(cursor, field_name, *field.get<int16_t>());
    } break;
    case SerializationType::uint32: {
        add_field_kv(cursor, field_name, *field.get<uint32_t>());
    } break;
    case SerializationType::int32: {
        add_field_kv(cursor, field_name, *field.get<int32_t>());
    } break;
    case SerializationType::uint64: {
        add_field_kv(cursor, field_name, *field.get<uint64_t>());
    } break;
    case SerializationType::int64: {
        add_field_kv(cursor, field_name, *field.get<uint32_t>());
    } break;
    case SerializationType::float_: {
        add_field_kv(cursor, field_name, *field.get<float>());
    } break;
    case SerializationType::double_: {
        add_field_kv(cursor, field_name, *field.get<double>());
    } break;
    case SerializationType::string: {
        add_field_kv(cursor, field_name, *field.get<std::string>());
    } break;
    case SerializationType::resref: {
        auto& o = cursor[field_name] = json::object();
        o["value"] = (*field.get<Resref>()).string();
        o["type"] = SerializationType::to_string(SerializationType::resref);
    } break;
    case SerializationType::locstring: {
        auto& o = cursor[field_name] = json::object();
        o["type"] = SerializationType::to_string(SerializationType::locstring);
        LocString ls = *field.get<LocString>();
        if (ls.strref() != std::numeric_limits<uint32_t>::max()) {
            o["strref"] = ls.strref();
        }
        auto& a = o["value"] = json::object();
        for (const auto& [lang, str] : ls) {
            a[std::to_string(lang)] = str;
        }
    } break;
    case SerializationType::void_: {
        auto& o = cursor[field_name] = json::object();
        o["value"] = to_base64((*field.get<ByteArray>()).span());
        o["type"] = SerializationType::to_string(SerializationType::void_);
    } break;
    case SerializationType::struct_: {
        auto& o = cursor[field_name] = json::object();
        o["type"] = SerializationType::to_string(SerializationType::struct_);
        auto& v = o["value"] = json::object();
        check = check && gff_to_gffjson(*field.get<GffStruct>(), v);
    } break;
    case SerializationType::list: {
        auto& v = cursor[field_name] = json::array();
        for (size_t i = 0; i < field.size(); ++i) {
            v.push_back(json::object());
            check = check && gff_to_gffjson(field[i], v[i]);
            if (!check) {
                break;
            }
        }
    } break;
    }
    return check;
}

bool gff_to_gffjson(const GffStruct str, nlohmann::json& cursor)
{
    bool check = true;
    if (!str.valid()) {
        LOG_F(ERROR, "attempted to convert invalid struct");
        return false;
    }
    cursor["__struct_id"] = str.id();
    for (size_t i = 0; i < str.size(); ++i) {
        check = check && gff_to_gffjson(str[i], cursor);
        if (!check) {
            LOG_F(ERROR, "Failed to conver struct");
            break;
        }
    }
    return check;
}

nlohmann::json gff_to_gffjson(const Gff& gff)
{
    nlohmann::json j;
    if (!gff.valid()) {
        LOG_F(ERROR, "attempting to convert invalid gff to json");
    } else {
        try {
            if (!gff_to_gffjson(gff.toplevel(), j)) {
                j.clear();
                LOG_F(ERROR, "gff_to_gffjson failed conversion");
            }
        } catch (const std::exception& e) {
            LOG_F(ERROR, "gff_to_gffjson exception thrown: {}", e.what());
            j.clear();
        }
    }
    return j;
}

} // namespace nw
