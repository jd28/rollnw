#include "gff_conversions.hpp"

#include "../util/base64.hpp"

#include <limits>

using json = nlohmann::json;

namespace nw {

inline const char* type_to_string(GffType::type type)
{
    switch (type) {
    default:
        return "(invalid)";
    case GffType::UINT8:
        return "byte";
    case GffType::INT8:
        return "char";
    case GffType::UINT16:
        return "word";
    case GffType::INT16:
        return "short";
    case GffType::UINT32:
        return "dword";
    case GffType::INT32:
        return "int";
    case GffType::UINT64:
        return "dword64";
    case GffType::INT64:
        return "int64";
    case GffType::FLOAT:
        return "float";
    case GffType::DOUBLE:
        return "double";
    case GffType::STRING:
        return "cexostring";
    case GffType::RESREF:
        return "resref";
    case GffType::LOCSTRING:
        return "cexolocstring";
    case GffType::VOID:
        return "void";
    case GffType::STRUCT:
        return "struct";
    case GffType::LIST:
        return "list";
    }
}

template <typename T>
inline void add_field_kv(json& j, const std::string& name, const T& value)
{
    auto& o = j[name] = json::object();
    o["value"] = value;
    o["type"] = type_to_string(GffType::id<T>());
}

bool gff_to_json(const GffStruct str, nlohmann::json& cursor);

inline bool gff_to_json(const GffField field, nlohmann::json& cursor)
{
    bool check = true;
    if (!field.valid()) {
        LOG_F(ERROR, "attempted to convert invalid field");
        return false;
    }

    // nholmann_json doesn't seem to support hetero lookup, bummer.
    std::string field_name = std::string(field.name());

    switch (field.type()) {
    case GffType::UINT8:
        add_field_kv(cursor, field_name, *field.get<uint8_t>());
        break;
    case GffType::INT8: {
        add_field_kv(cursor, field_name, *field.get<int8_t>());
    } break;
    case GffType::UINT16: {
        add_field_kv(cursor, field_name, *field.get<uint16_t>());
    } break;
    case GffType::INT16: {
        add_field_kv(cursor, field_name, *field.get<int16_t>());
    } break;
    case GffType::UINT32: {
        add_field_kv(cursor, field_name, *field.get<uint32_t>());
    } break;
    case GffType::INT32: {
        add_field_kv(cursor, field_name, *field.get<int32_t>());
    } break;
    case GffType::UINT64: {
        add_field_kv(cursor, field_name, *field.get<uint64_t>());
    } break;
    case GffType::INT64: {
        add_field_kv(cursor, field_name, *field.get<uint32_t>());
    } break;
    case GffType::FLOAT: {
        add_field_kv(cursor, field_name, *field.get<float>());
    } break;
    case GffType::DOUBLE: {
        add_field_kv(cursor, field_name, *field.get<double>());
    } break;
    case GffType::STRING: {
        add_field_kv(cursor, field_name, *field.get<std::string>());
    } break;
    case GffType::RESREF: {
        auto& o = cursor[field_name] = json::object();
        o["value"] = (*field.get<Resref>()).string();
        o["type"] = type_to_string(GffType::RESREF);
    } break;
    case GffType::LOCSTRING: {
        auto& o = cursor[field_name] = json::object();
        o["type"] = type_to_string(GffType::LOCSTRING);
        LocString ls = *field.get<LocString>();
        if (ls.strref() != std::numeric_limits<uint32_t>::max()) {
            o["strref"] = ls.strref();
        }
        auto& a = o["value"] = json::object();
        for (const auto& [lang, str] : ls) {
            a[std::to_string(lang)] = str;
        }
    } break;
    case GffType::VOID: {
        auto& o = cursor[field_name] = json::object();
        o["value"] = to_base64((*field.get<ByteArray>()).span());
        o["type"] = type_to_string(GffType::VOID);
    } break;
    case GffType::STRUCT: {
        auto& o = cursor[field_name] = json::object();
        o["type"] = type_to_string(GffType::STRUCT);
        auto& v = o["value"] = json::object();
        check = check && gff_to_json(*field.get<GffStruct>(), v);
    } break;
    case GffType::LIST: {
        auto& v = cursor[field_name] = json::array();
        for (size_t i = 0; i < field.size(); ++i) {
            v.push_back(json::object());
            check = check && gff_to_json(field[i], v[i]);
            if (!check) { break; }
        }
    } break;
    }
    return check;
}

bool gff_to_json(const GffStruct str, nlohmann::json& cursor)
{
    bool check = true;
    if (!str.valid()) {
        LOG_F(ERROR, "attempted to convert invalid struct");
        return false;
    }
    cursor["__struct_id"] = str.id();
    for (size_t i = 0; i < str.size(); ++i) {
        check = check && gff_to_json(str[i], cursor);
        if (!check) {
            LOG_F(ERROR, "Failed to conver struct");
            break;
        }
    }
    return check;
}

nlohmann::json gff_to_json(const Gff& gff)
{
    nlohmann::json j;
    if (!gff.valid()) {
        LOG_F(ERROR, "attempting to convert invalid gff to json");
    } else {
        try {
            if (!gff_to_json(gff.toplevel(), j)) {
                j.clear();
                LOG_F(ERROR, "gff_to_json failed conversion");
            }
        } catch (const std::exception& e) {
            LOG_F(ERROR, "gff_to_json exception thrown: {}", e.what());
            j.clear();
        }
    }
    return j;
}

} // namespace nw
