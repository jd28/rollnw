#include "Archives.hpp"

#include "../util/base64.hpp"
#include "Serialization.hpp"

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

bool gff_to_gffjson(const GffInputArchiveStruct str, nlohmann::json& cursor);

inline bool gff_to_gffjson(const GffInputArchiveField field, nlohmann::json& cursor)
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
    case SerializationType::UINT8:
        add_field_kv(cursor, field_name, *field.get<uint8_t>());
        break;
    case SerializationType::INT8: {
        add_field_kv(cursor, field_name, *field.get<int8_t>());
    } break;
    case SerializationType::UINT16: {
        add_field_kv(cursor, field_name, *field.get<uint16_t>());
    } break;
    case SerializationType::INT16: {
        add_field_kv(cursor, field_name, *field.get<int16_t>());
    } break;
    case SerializationType::UINT32: {
        add_field_kv(cursor, field_name, *field.get<uint32_t>());
    } break;
    case SerializationType::INT32: {
        add_field_kv(cursor, field_name, *field.get<int32_t>());
    } break;
    case SerializationType::UINT64: {
        add_field_kv(cursor, field_name, *field.get<uint64_t>());
    } break;
    case SerializationType::INT64: {
        add_field_kv(cursor, field_name, *field.get<uint32_t>());
    } break;
    case SerializationType::FLOAT: {
        add_field_kv(cursor, field_name, *field.get<float>());
    } break;
    case SerializationType::DOUBLE: {
        add_field_kv(cursor, field_name, *field.get<double>());
    } break;
    case SerializationType::STRING: {
        add_field_kv(cursor, field_name, *field.get<std::string>());
    } break;
    case SerializationType::RESREF: {
        auto& o = cursor[field_name] = json::object();
        o["value"] = (*field.get<Resref>()).string();
        o["type"] = SerializationType::to_string(SerializationType::RESREF);
    } break;
    case SerializationType::LOCSTRING: {
        auto& o = cursor[field_name] = json::object();
        o["type"] = SerializationType::to_string(SerializationType::LOCSTRING);
        LocString ls = *field.get<LocString>();
        if (ls.strref() != std::numeric_limits<uint32_t>::max()) {
            o["strref"] = ls.strref();
        }
        auto& a = o["value"] = json::object();
        for (const auto& [lang, str] : ls) {
            a[std::to_string(lang)] = str;
        }
    } break;
    case SerializationType::VOID: {
        auto& o = cursor[field_name] = json::object();
        o["value"] = to_base64((*field.get<ByteArray>()).span());
        o["type"] = SerializationType::to_string(SerializationType::VOID);
    } break;
    case SerializationType::STRUCT: {
        auto& o = cursor[field_name] = json::object();
        o["type"] = SerializationType::to_string(SerializationType::STRUCT);
        auto& v = o["value"] = json::object();
        check = check && gff_to_gffjson(*field.get<GffInputArchiveStruct>(), v);
    } break;
    case SerializationType::LIST: {
        auto& v = cursor[field_name] = json::array();
        for (size_t i = 0; i < field.size(); ++i) {
            v.push_back(json::object());
            check = check && gff_to_gffjson(field[i], v[i]);
            if (!check) { break; }
        }
    } break;
    }
    return check;
}

bool gff_to_gffjson(const GffInputArchiveStruct str, nlohmann::json& cursor)
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

nlohmann::json gff_to_gffjson(const GffInputArchive& gff)
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
