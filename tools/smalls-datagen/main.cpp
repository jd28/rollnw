#include <nw/formats/StaticTwoDA.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Strings.hpp>
#include <nw/kernel/TwoDACache.hpp>
#include <nw/profiles/nwn1/Profile.hpp>
#include <nw/smalls/data_spec.hpp>
#include <nw/smalls/data_transform.hpp>
#include <nw/smalls/runtime.hpp>

#include <fmt/core.h>
#include <nlohmann/json.hpp>
#include <nowide/args.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr int kInvalidResourceType = 65535;
constexpr int kMdlResourceType = 2002;

struct DatagenProfile final : nwn1::Profile {
    explicit DatagenProfile(fs::path stdlib_root)
        : stdlib_root_{std::move(stdlib_root)}
    {
    }

    void load_custom_services() override
    {
        nw::kernel::runtime().add_module_path(stdlib_root_ / "core");
        nw::kernel::runtime().add_module_path(stdlib_root_ / "nwn1");
    }
    bool load_rules() const override { return true; }

private:
    fs::path stdlib_root_;
};

} // namespace

// ---------------------------------------------------------------------------
// Spec structures
// ---------------------------------------------------------------------------

struct FieldSpec {
    std::string name;
    std::string type;   // "int", "float", "bool", "string", "StrRef", "ResRef", "Resource", "int[N]", "array(int)"
    std::string source; // "@row_index", "@scan_index", "@scan_ref:COL:spec",
                        // "@indirect:COL:SUBCOL:N", "@indirect_grid:COL:PREFIX:N[:LIMIT_COL]",
                        // "@array:COL1,...", "@feat_requirements:COL1,...",
                        // plain column name, or empty for constant default
    std::string default_val;
    std::map<std::string, int> string_enum; // case-insensitive string→int
};

struct FieldGroupSpec {
    std::string name;
    std::string type;
    std::vector<FieldSpec> fields;
};

struct GenSpec {
    std::string spec_name; // filename stem (for --entity filter and @scan_ref lookups)
    std::string smalls_type;
    std::string output_subdir;
    std::optional<std::string> valid_column;
    std::vector<FieldSpec> fields;
    std::vector<FieldGroupSpec> field_groups;

    // Standard mode
    std::string source_2da;

    // Scan mode: scan source_2da for unique values in scan_column;
    // each unique value names a secondary 2da whose rows are the entry data.
    std::string scan_column; // non-empty → scan mode

    // Filename source: how to name the output .smalls file.
    //   ""              → use row index (default)
    //   "@strref:COL"   → read integer from COL, look up in TLK, slugify
    //   "@label:COL"    → read string from COL, slugify
    //   "@scan_name"    → (scan mode default) use secondary 2da name
    std::string filename_source;

    // If set, skip rows where this column's integer StrRef value is <= 0.
    // Filters out placeholder entries (StrRef(0) or missing "****").
    std::optional<std::string> valid_strref_column;
};

// Scan result: sorted map of (secondary_2da_name → integer ID)
// Produced by scan-mode specs, consumed by @scan_ref sources.
using ScanResult = std::map<std::string, int>;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static int array_size_from_type(const std::string& type)
{
    auto lb = type.find('[');
    if (lb == std::string::npos) return 0;
    auto rb = type.find(']', lb);
    if (rb == std::string::npos) return 0;
    return std::stoi(type.substr(lb + 1, rb - lb - 1));
}

static bool is_dynamic_array_type(const std::string& type)
{
    return type == "array(int)" || type == "array!(int)";
}

static std::string escape_smalls_string(const std::string& value)
{
    std::string result;
    result.reserve(value.size());
    for (char c : value) {
        if (c == '\\' || c == '"') {
            result += '\\';
        }
        result += c;
    }
    return result;
}

static std::string read_resref_column(const nw::StaticTwoDA* tda, int row, const std::string& column)
{
    nw::String value;
    if (tda && tda->get_to(row, column, value, false) && !value.empty() && value != "****") {
        return std::string(value);
    }
    return {};
}

// Convert a string to a filename-safe slug: lowercase, spaces→_, strip others.
static std::string slugify(const std::string& s)
{
    std::string result;
    result.reserve(s.size());
    for (unsigned char c : s) {
        if (c == ' ' || c == '-') {
            result += '_';
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') {
            result += static_cast<char>(c);
        } else if (c >= 'A' && c <= 'Z') {
            result += static_cast<char>(c - 'A' + 'a');
        }
        // strip everything else (parentheses, punctuation, etc.)
    }
    // Trim trailing underscores
    while (!result.empty() && result.back() == '_')
        result.pop_back();
    return result;
}

// Compute the output filename stem for a row in standard mode.
// Falls back to std::to_string(fallback_id) if the source can't produce a name.
static std::string compute_filename(const std::string& filename_source,
    const nw::StaticTwoDA* tda, size_t row, int fallback_id)
{
    if (filename_source.empty()) {
        return std::to_string(fallback_id);
    }

    if (filename_source.rfind("@strref:", 0) == 0) {
        std::string col = filename_source.substr(8);
        int32_t strref = -1;
        if (tda && tda->get_to(row, col, strref) && strref >= 0) {
            std::string name = nw::kernel::strings().get(static_cast<uint32_t>(strref));
            std::string slug = slugify(name);
            if (!slug.empty()) return slug;
        }
        return std::to_string(fallback_id);
    }

    if (filename_source.rfind("@label:", 0) == 0) {
        std::string col = filename_source.substr(7);
        nw::String val;
        if (tda && tda->get_to(row, col, val) && !val.empty()) {
            std::string slug = slugify(std::string(val));
            if (!slug.empty()) return slug;
        }
        return std::to_string(fallback_id);
    }

    return std::to_string(fallback_id);
}

// Make a filename unique within a set of already-used names.
// If "slug" is taken, tries "slug_<id>".
static std::string unique_filename(const std::string& slug, int fallback_id,
    std::set<std::string>& used)
{
    if (used.find(slug) == used.end()) {
        used.insert(slug);
        return slug;
    }
    std::string candidate = slug + "_" + std::to_string(fallback_id);
    used.insert(candidate);
    return candidate;
}

// ---------------------------------------------------------------------------
// emit_field
// ---------------------------------------------------------------------------

static void emit_field(std::ofstream& out, const FieldSpec& f,
    int row_index,
    const nw::StaticTwoDA* tda,                                            // primary 2da (scan mode: secondary 2da for this entry)
    const std::map<std::string, const nw::StaticTwoDA*>& cached_secondary, // @indirect cache
    const std::map<std::string, ScanResult>& scan_results,                 // global scan results
    int scan_index = -1,                                                   // ID in scan results (scan mode only)
    int indent = 4)
{
    out << std::string(static_cast<size_t>(indent), ' ') << f.name << " = ";

    if (f.source == "@row_index") {
        out << row_index;
        return;
    }

    if (f.source == "@scan_index") {
        out << scan_index;
        return;
    }

    // @scan_ref:COL:spec_name — look up column value in named scan's results
    if (f.source.rfind("@scan_ref:", 0) == 0) {
        std::string rest = f.source.substr(10);
        auto colon = rest.find(':');
        std::string col = rest.substr(0, colon);
        std::string spec = rest.substr(colon + 1);

        int val = 0;
        if (!f.default_val.empty()) val = std::stoi(f.default_val);

        nw::String col_val;
        if (tda && tda->get_to(row_index, col, col_val)) {
            auto sr_it = scan_results.find(spec);
            if (sr_it != scan_results.end()) {
                auto id_it = sr_it->second.find(col_val);
                if (id_it != sr_it->second.end()) {
                    val = id_it->second;
                }
            }
        }
        out << val;
        return;
    }

    // Constant default (empty source = null in JSON)
    if (f.source.empty()) {
        out << f.default_val;
        return;
    }

    int arr_size = array_size_from_type(f.type);

    // @indirect:COL:SUBCOL:N — secondary 2da fixed array
    if (f.source.rfind("@indirect:", 0) == 0) {
        auto it = cached_secondary.find(f.name);
        if (it == cached_secondary.end() || !it->second) {
            out << "{";
            for (int i = 0; i < arr_size; ++i) {
                if (i > 0) out << ", ";
                out << f.default_val;
            }
            out << "}";
            return;
        }
        const nw::StaticTwoDA* sec = it->second;
        std::string src = f.source.substr(10);
        auto colon1 = src.find(':');
        auto colon2 = src.find(':', colon1 + 1);
        std::string subcol = src.substr(colon1 + 1, colon2 - colon1 - 1);

        out << "{";
        size_t nrows = sec->rows();
        for (int i = 0; i < arr_size; ++i) {
            if (i > 0) out << ", ";
            int val = 0;
            if (static_cast<size_t>(i) < nrows) sec->get_to(i, subcol, val);
            out << val;
        }
        out << "}";
        return;
    }

    // @indirect_grid:COL:PREFIX:N[:LIMIT_COL] — secondary 2da row-major fixed array
    if (f.source.rfind("@indirect_grid:", 0) == 0) {
        int default_val = 0;
        if (!f.default_val.empty()) {
            default_val = std::stoi(f.default_val);
        }

        auto write_defaults = [&]() {
            out << "{";
            for (int i = 0; i < arr_size; ++i) {
                if (i > 0) out << ", ";
                out << default_val;
            }
            out << "}";
        };

        auto it = cached_secondary.find(f.name);
        if (it == cached_secondary.end() || !it->second) {
            write_defaults();
            return;
        }

        std::string src = f.source.substr(15);
        std::vector<std::string> parts;
        size_t pos = 0;
        while (pos <= src.size()) {
            auto colon = src.find(':', pos);
            if (colon == std::string::npos) {
                parts.push_back(src.substr(pos));
                break;
            }
            parts.push_back(src.substr(pos, colon - pos));
            pos = colon + 1;
        }

        if (parts.size() < 3) {
            write_defaults();
            return;
        }

        const nw::StaticTwoDA* sec = it->second;
        const std::string& prefix = parts[1];
        int columns = std::stoi(parts[2]);
        if (columns <= 0) {
            write_defaults();
            return;
        }
        const std::string limit_column = parts.size() >= 4 ? parts[3] : "";

        out << "{";
        for (int i = 0; i < arr_size; ++i) {
            if (i > 0) out << ", ";
            int val = default_val;
            int row = i / columns;
            int col = i % columns;
            int column_limit = columns;
            if (!limit_column.empty() && !sec->get_to(static_cast<size_t>(row), limit_column, column_limit, false)) {
                column_limit = 0;
            }
            if (column_limit < 0) {
                column_limit = 0;
            } else if (column_limit > columns) {
                column_limit = columns;
            }
            if (row >= 0 && static_cast<size_t>(row) < sec->rows() && col < column_limit) {
                sec->get_to(static_cast<size_t>(row), fmt::format("{}{}", prefix, col), val, false);
            }
            out << val;
        }
        out << "}";
        return;
    }

    // @array:COL1,COL2,... — inline array from multiple columns of the primary 2da
    if (f.source.rfind("@array:", 0) == 0) {
        std::string cols_str = f.source.substr(7);
        std::vector<std::string> cols;
        size_t pos = 0;
        while (pos < cols_str.size()) {
            auto comma = cols_str.find(',', pos);
            if (comma == std::string::npos) {
                cols.push_back(cols_str.substr(pos));
                break;
            }
            cols.push_back(cols_str.substr(pos, comma - pos));
            pos = comma + 1;
        }
        int default_val = 0;
        if (!f.default_val.empty()) {
            default_val = std::stoi(f.default_val);
        }

        out << "{";
        for (size_t i = 0; i < cols.size(); ++i) {
            if (i > 0) out << ", ";
            int val = default_val;
            tda->get_to(row_index, cols[i], val);
            out << val;
        }
        out << "}";
        return;
    }

    // @feat_requirements:COL1,COL2,... — convert legacy feat columns into
    // semantic requirement qualifiers, omitting negative sentinel values.
    if (f.source.rfind("@feat_requirements:", 0) == 0) {
        std::string cols_str = f.source.substr(19);
        std::vector<std::string> cols;
        size_t pos = 0;
        while (pos < cols_str.size()) {
            auto comma = cols_str.find(',', pos);
            if (comma == std::string::npos) {
                cols.push_back(cols_str.substr(pos));
                break;
            }
            cols.push_back(cols_str.substr(pos, comma - pos));
            pos = comma + 1;
        }

        out << "{";
        bool first = true;
        for (const auto& column : cols) {
            int feat = -1;
            if (!tda || !tda->get_to(row_index, column, feat) || feat < 0) {
                continue;
            }
            if (!first) {
                out << ", ";
            }
            out << "feat_requirement(" << feat << ")";
            first = false;
        }
        out << "}";
        return;
    }

    // Dynamic array: read all rows of tda under given column
    // Used in scan mode where tda IS the secondary 2da (one per progression)
    if (is_dynamic_array_type(f.type)) {
        out << "{";
        size_t nrows = tda ? tda->rows() : 0;
        for (size_t i = 0; i < nrows; ++i) {
            if (i > 0) out << ", ";
            int val = 0;
            tda->get_to(i, f.source, val);
            out << val;
        }
        out << "}";
        return;
    }

    // String-enum: read column as string, map to int
    if (!f.string_enum.empty()) {
        nw::String sval;
        if (tda && tda->get_to(row_index, f.source, sval)) {
            std::string lower;
            lower.resize(sval.size());
            for (size_t i = 0; i < sval.size(); ++i)
                lower[i] = static_cast<char>(tolower(static_cast<unsigned char>(sval[i])));
            auto it = f.string_enum.find(lower);
            if (it != f.string_enum.end()) {
                out << it->second;
                return;
            }
        }
        out << f.default_val;
        return;
    }

    // Direct column reads
    if (f.type == "int") {
        int val = 0;
        if (!f.default_val.empty()) val = std::stoi(f.default_val);
        if (tda) tda->get_to(row_index, f.source, val);
        out << val;
    } else if (f.type == "float") {
        float val = 0.0f;
        if (!f.default_val.empty()) val = std::stof(f.default_val);
        if (tda) tda->get_to(row_index, f.source, val);
        std::string fstr = fmt::format("{:.6g}", val);
        if (fstr.find('.') == std::string::npos && fstr.find('e') == std::string::npos)
            fstr += ".0";
        out << fstr;
    } else if (f.type == "bool") {
        int val = 0;
        if (tda) tda->get_to(row_index, f.source, val);
        out << (val ? "true" : "false");
    } else if (f.type == "string") {
        nw::String value;
        if (tda) tda->get_to(row_index, f.source, value);
        out << "\"" << escape_smalls_string(value) << "\"";
    } else if (f.type == "StrRef") {
        int32_t raw = -1;
        if (tda) tda->get_to(row_index, f.source, raw);
        // Treat unset values correctly: -1 stays as StrRef(-1)
        uint32_t val = static_cast<uint32_t>(raw);
        out << "StrRef(" << static_cast<int32_t>(val) << ")";
    } else if (f.type == "ResRef") {
        std::string value = read_resref_column(tda, row_index, f.source);
        out << "resref(\"" << escape_smalls_string(value) << "\")";
    } else if (f.type == "Resource") {
        std::string value = read_resref_column(tda, row_index, f.source);
        if (value.empty()) {
            out << "resource(resref(\"\"), " << kInvalidResourceType << ")";
        } else {
            out << "resource(resref(\"" << escape_smalls_string(value) << "\"), " << kMdlResourceType << ")";
        }
    } else {
        out << f.default_val;
    }
}

// ---------------------------------------------------------------------------
// Parse spec from JSON
// ---------------------------------------------------------------------------

static FieldSpec parse_field_spec(const json& source)
{
    FieldSpec result;
    result.name = source.value("name", "");
    result.type = source.value("type", "int");
    result.default_val = source.value("default", "0");

    if (source.contains("source") && !source["source"].is_null()) {
        result.source = source["source"].get<std::string>();
    }

    if (source.contains("enum")) {
        for (auto& [key, value] : source["enum"].items()) {
            std::string lower = key;
            for (char& c : lower) {
                c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
            }
            result.string_enum[lower] = value.get<int>();
        }
    }

    return result;
}

static bool parse_spec(const fs::path& spec_path, GenSpec& out_spec)
{
    std::ifstream ifs(spec_path);
    if (!ifs.is_open()) {
        fmt::print(stderr, "Error: cannot open spec '{}'\n", spec_path.string());
        return false;
    }

    json j;
    try {
        ifs >> j;
    } catch (const json::exception& e) {
        fmt::print(stderr, "Error: JSON parse '{}': {}\n", spec_path.string(), e.what());
        return false;
    }

    out_spec.spec_name = spec_path.stem().string();
    out_spec.smalls_type = j.value("smalls_type", "");
    out_spec.output_subdir = j.value("output_subdir", "");
    out_spec.source_2da = j.value("source_2da", "");
    out_spec.scan_column = j.value("scan_column", "");
    out_spec.filename_source = j.value("filename_source", "");

    if (j.contains("valid_column") && !j["valid_column"].is_null())
        out_spec.valid_column = j["valid_column"].get<std::string>();

    if (j.contains("valid_strref_column") && !j["valid_strref_column"].is_null())
        out_spec.valid_strref_column = j["valid_strref_column"].get<std::string>();

    for (const auto& field : j.value("fields", json::array())) {
        out_spec.fields.push_back(parse_field_spec(field));
    }

    for (const auto& group_source : j.value("field_groups", json::array())) {
        FieldGroupSpec group;
        group.name = group_source.value("name", "");
        group.type = group_source.value("type", "");
        for (const auto& field : group_source.value("fields", json::array())) {
            group.fields.push_back(parse_field_spec(field));
        }
        out_spec.field_groups.push_back(std::move(group));
    }

    return true;
}

// ---------------------------------------------------------------------------
// Emit one smalls file for a set of fields
// ---------------------------------------------------------------------------

static bool emit_smalls_file(const fs::path& out_file, const GenSpec& spec,
    int row_index,
    const nw::StaticTwoDA* tda,
    const std::map<std::string, const nw::StaticTwoDA*>& cached_secondary,
    const std::map<std::string, ScanResult>& scan_results,
    int scan_index,
    bool force)
{
    if (!force && fs::exists(out_file)) return false; // skip existing

    std::ofstream ofs(out_file);
    if (!ofs.is_open()) {
        fmt::print(stderr, "Error: cannot write '{}'\n", out_file.string());
        return false;
    }

    ofs << spec.smalls_type << " {\n";
    size_t component_index = 0;
    const size_t component_count = spec.fields.size() + spec.field_groups.size();
    for (size_t fi = 0; fi < spec.fields.size(); ++fi) {
        emit_field(ofs, spec.fields[fi], row_index, tda,
            cached_secondary, scan_results, scan_index);
        if (++component_index < component_count) ofs << ",";
        ofs << "\n";
    }
    for (const auto& group : spec.field_groups) {
        ofs << "    " << group.name << " = " << group.type << " {\n";
        for (size_t fi = 0; fi < group.fields.size(); ++fi) {
            emit_field(ofs, group.fields[fi], row_index, tda,
                cached_secondary, scan_results, scan_index, 8);
            if (fi + 1 < group.fields.size()) ofs << ",";
            ofs << "\n";
        }
        ofs << "    }";
        if (++component_index < component_count) ofs << ",";
        ofs << "\n";
    }
    ofs << "}\n";
    return true;
}

// ---------------------------------------------------------------------------
// Build @indirect cache for a row
// ---------------------------------------------------------------------------

static std::map<std::string, const nw::StaticTwoDA*> build_indirect_cache(
    const GenSpec& spec, const nw::StaticTwoDA* tda, size_t row)
{
    std::map<std::string, const nw::StaticTwoDA*> cached_secondary;
    std::map<std::string, const nw::StaticTwoDA*> resolved;

    auto cache_field = [&](const FieldSpec& f) {
        size_t prefix_len = 0;
        if (f.source.rfind("@indirect:", 0) == 0) {
            prefix_len = 10;
        } else if (f.source.rfind("@indirect_grid:", 0) == 0) {
            prefix_len = 15;
        } else {
            return;
        }
        std::string src = f.source.substr(prefix_len);
        auto colon1 = src.find(':');
        std::string col = src.substr(0, colon1);

        auto it = resolved.find(col);
        if (it != resolved.end()) {
            cached_secondary[f.name] = it->second;
            return;
        }

        nw::StringView table_name;
        const nw::StaticTwoDA* sec = nullptr;
        if (tda->get_to(row, col, table_name) && !table_name.empty())
            sec = nw::kernel::twodas().get(nw::String(table_name));

        resolved[col] = sec;
        cached_secondary[f.name] = sec;
    };

    for (const auto& field : spec.fields) {
        cache_field(field);
    }
    for (const auto& group : spec.field_groups) {
        for (const auto& field : group.fields) {
            cache_field(field);
        }
    }
    return cached_secondary;
}

// ---------------------------------------------------------------------------
// Run standard spec (one file per 2da row)
// ---------------------------------------------------------------------------

static int run_standard_spec(const GenSpec& spec, const fs::path& out_root,
    bool force, const std::map<std::string, ScanResult>& scan_results)
{
    const nw::StaticTwoDA* tda = nw::kernel::twodas().get(spec.source_2da);
    if (!tda || !tda->is_valid()) {
        fmt::print(stderr, "Error: 2da '{}' not found or invalid\n", spec.source_2da);
        return 1;
    }

    fs::path out_dir = out_root / spec.output_subdir;
    std::error_code ec;
    fs::create_directories(out_dir, ec);
    if (ec) {
        fmt::print(stderr, "Error: create dir '{}': {}\n", out_dir.string(), ec.message());
        return 1;
    }

    int emitted = 0, skipped = 0, filtered = 0;
    std::set<std::string> used_names;

    for (size_t row = 0; row < tda->rows(); ++row) {
        if (spec.valid_column) {
            nw::String dummy;
            if (!tda->get_to(row, *spec.valid_column, dummy, false)) {
                ++filtered;
                continue;
            }
        }

        if (spec.valid_strref_column) {
            int32_t strref = 0;
            if (!tda->get_to(row, *spec.valid_strref_column, strref, false) || strref <= 0) {
                ++filtered;
                continue;
            }
        }

        std::string stem = compute_filename(spec.filename_source, tda, row, static_cast<int>(row));
        stem = unique_filename(stem, static_cast<int>(row), used_names);

        auto cached_secondary = build_indirect_cache(spec, tda, row);
        fs::path out_file = out_dir / (stem + ".smalls");
        if (emit_smalls_file(out_file, spec, static_cast<int>(row), tda,
                cached_secondary, scan_results, -1, force))
            ++emitted;
        else
            ++skipped;
    }

    fmt::print("[{}] emitted={} skipped={} filtered={} (rows: {})\n",
        spec.spec_name, emitted, skipped, filtered, tda->rows());
    return 0;
}

// ---------------------------------------------------------------------------
// Run scan spec: scan source_2da for unique scan_column values;
// each unique value is a secondary 2da. One file per unique table.
// Returns the ScanResult (name → id mapping) for use by @scan_ref.
// ---------------------------------------------------------------------------

static int run_scan_spec(const GenSpec& spec, const fs::path& out_root,
    bool force, ScanResult& out_result)
{
    const nw::StaticTwoDA* primary = nw::kernel::twodas().get(spec.source_2da);
    if (!primary || !primary->is_valid()) {
        fmt::print(stderr, "Error: scan 2da '{}' not found\n", spec.source_2da);
        return 1;
    }

    // Collect unique secondary 2da names in sorted order (for stable IDs)
    std::map<std::string, int> name_to_id; // std::map → sorted by key
    for (size_t row = 0; row < primary->rows(); ++row) {
        nw::String val;
        if (primary->get_to(row, spec.scan_column, val) && !val.empty())
            name_to_id.emplace(val, 0); // value irrelevant until IDs assigned
    }

    int id = 0;
    for (auto& [name, _] : name_to_id) {
        _ = id++;
    }
    out_result = std::map<std::string, int>(name_to_id.begin(), name_to_id.end());

    fs::path out_dir = out_root / spec.output_subdir;
    std::error_code ec;
    fs::create_directories(out_dir, ec);
    if (ec) {
        fmt::print(stderr, "Error: create dir '{}': {}\n", out_dir.string(), ec.message());
        return 1;
    }

    int emitted = 0, skipped = 0;
    for (auto& [table_name, table_id] : out_result) {
        const nw::StaticTwoDA* sec = nw::kernel::twodas().get(table_name);
        if (!sec || !sec->is_valid()) {
            fmt::print(stderr, "Warning: scan secondary 2da '{}' not found, skipping\n", table_name);
            continue;
        }
        // Use the secondary 2da name as the filename (natural slug for progression tables)
        std::string stem = slugify(table_name);
        if (stem.empty()) stem = std::to_string(table_id);

        std::map<std::string, const nw::StaticTwoDA*> no_secondary;
        std::map<std::string, ScanResult> no_scans;
        fs::path out_file = out_dir / (stem + ".smalls");
        if (emit_smalls_file(out_file, spec, 0, sec,
                no_secondary, no_scans, table_id, force))
            ++emitted;
        else
            ++skipped;
    }

    fmt::print("[{}] emitted={} skipped={} (unique tables: {})\n",
        spec.spec_name, emitted, skipped, out_result.size());
    return 0;
}

// ---------------------------------------------------------------------------
// Shared structured specs
// ---------------------------------------------------------------------------

static std::string unqualified_type(nw::StringView type)
{
    const auto separator = type.rfind('.');
    return std::string{separator == nw::StringView::npos
            ? type
            : type.substr(separator + 1)};
}

static fs::path config_output_subdir(nw::StringView config_path)
{
    std::string relative{config_path};
    std::replace(relative.begin(), relative.end(), '.', '/');
    return relative;
}

static bool valid_output_subdir(const fs::path& path)
{
    if (path.empty() || path.is_absolute()) { return false; }
    return std::ranges::none_of(path,
        [](const auto& component) {
            return component == "." || component == "..";
        });
}

static bool emit_materialized_scalar(
    std::ostream& out, const nw::smalls::DataScalar& scalar)
{
    if (const auto* integer = std::get_if<int32_t>(&scalar)) {
        out << *integer;
    } else if (const auto* floating = std::get_if<float>(&scalar)) {
        auto text = fmt::format("{:.6g}", *floating);
        if (text.find('.') == std::string::npos
            && text.find('e') == std::string::npos) {
            text += ".0";
        }
        out << text;
    } else if (const auto* boolean = std::get_if<bool>(&scalar)) {
        out << (*boolean ? "true" : "false");
    } else if (const auto* string = std::get_if<nw::String>(&scalar)) {
        out << '"' << escape_smalls_string(*string) << '"';
    } else if (const auto* resref = std::get_if<nw::Resref>(&scalar)) {
        out << "resref(\"" << escape_smalls_string(resref->string()) << "\")";
    } else {
        return false;
    }
    return true;
}

static const nw::smalls::MaterializedDataValue* materialized_value(
    const nw::smalls::MaterializedDataBatch& batch,
    const nw::smalls::MaterializedDataRow& row,
    nw::StringView target)
{
    const auto values = batch.row_values(row);
    const auto found = std::ranges::find(
        values, target, &nw::smalls::MaterializedDataValue::target);
    return found == values.end() ? nullptr : &*found;
}

static bool emit_structured_snapshot(
    const fs::path& output,
    const nw::smalls::DataSpec& spec,
    const nw::smalls::MaterializedDataBatch& batch,
    const nw::smalls::MaterializedDataRow& row)
{
    std::ofstream out{output};
    if (!out) {
        fmt::print(stderr, "Error: cannot write '{}'\n", output.string());
        return false;
    }

    out << unqualified_type(spec.entry_type) << " {\n";
    size_t component = 0;
    const size_t component_count = spec.fields.size() + spec.field_groups.size();
    for (const auto& field : spec.fields) {
        const auto* value = materialized_value(batch, row, field.target);
        if (!value) {
            fmt::print(stderr, "Error: materialized row {} has no target '{}'\n",
                row.id, field.target);
            return false;
        }
        out << "    " << field.target << " = ";
        if (!emit_materialized_scalar(out, value->value)) { return false; }
        if (++component < component_count) { out << ','; }
        out << '\n';
    }
    for (const auto& group : spec.field_groups) {
        out << "    " << group.target << " = "
            << unqualified_type(group.type) << " {\n";
        for (size_t index = 0; index < group.fields.size(); ++index) {
            const auto& field = group.fields[index];
            const auto target = fmt::format("{}.{}", group.target, field.target);
            const auto* value = materialized_value(batch, row, target);
            if (!value) {
                fmt::print(stderr, "Error: materialized row {} has no target '{}'\n",
                    row.id, target);
                return false;
            }
            out << "        " << field.target << " = ";
            if (!emit_materialized_scalar(out, value->value)) { return false; }
            if (index + 1 < group.fields.size()) { out << ','; }
            out << '\n';
        }
        out << "    }";
        if (++component < component_count) { out << ','; }
        out << '\n';
    }
    out << "}\n";
    return static_cast<bool>(out);
}

static bool compute_structured_snapshot_filenames(
    const nw::smalls::DataSpec& spec,
    const nw::StaticTwoDA& source,
    const nw::smalls::MaterializedDataBatch& batch,
    std::vector<std::string>& output)
{
    if (spec.snapshot_filename_column.empty()) {
        fmt::print(stderr,
            "Error: data spec '{}' does not declare a snapshot filename column\n",
            spec.source_path.string());
        return false;
    }

    std::vector<std::string> candidate;
    candidate.reserve(batch.rows.size());
    std::set<std::string> used;
    for (const auto& row : batch.rows) {
        nw::String source_name;
        if (!source.get_to(static_cast<size_t>(row.id),
                spec.snapshot_filename_column, source_name, false)
            || source_name.empty() || source_name == "****") {
            fmt::print(stderr,
                "Error: data spec '{}' row {} has no snapshot filename in column '{}'\n",
                spec.source_path.string(), row.id,
                spec.snapshot_filename_column);
            return false;
        }

        auto filename = slugify(std::string{source_name});
        if (filename.empty()) {
            fmt::print(stderr,
                "Error: data spec '{}' row {} snapshot filename sanitizes to empty\n",
                spec.source_path.string(), row.id);
            return false;
        }
        if (!used.insert(filename).second) {
            fmt::print(stderr,
                "Error: data spec '{}' row {} has duplicate sanitized snapshot filename '{}'\n",
                spec.source_path.string(), row.id, filename);
            return false;
        }
        candidate.push_back(std::move(filename));
    }
    output = std::move(candidate);
    return true;
}

static int run_structured_spec(
    const nw::smalls::DataSpec& spec,
    const fs::path& out_root,
    bool force)
{
    const auto* source = nw::kernel::twodas().get(spec.source_resource);
    if (!source) {
        fmt::print(stderr, "Error: 2da '{}' not found\n", spec.source_resource);
        return 1;
    }

    nw::smalls::MaterializedDataBatch batch;
    nw::Vector<nw::smalls::DataDiagnostic> diagnostics;
    const bool materialized = nw::smalls::materialize_data_rows(
        spec, *source, batch, diagnostics);
    for (const auto& diagnostic : diagnostics) {
        const std::string_view label = diagnostic.severity
                == nw::smalls::DiagnosticSeverity::warning
            ? "Warning"
            : "Error";
        fmt::print(stderr, "{}: {} row {} target '{}': {}\n",
            label, diagnostic.source.string(), diagnostic.row,
            diagnostic.target, diagnostic.message);
    }
    if (!materialized) { return 1; }

    std::vector<std::string> filenames;
    if (!compute_structured_snapshot_filenames(
            spec, *source, batch, filenames)) {
        return 1;
    }

    const auto relative = config_output_subdir(spec.config_path);
    if (!valid_output_subdir(relative)) {
        fmt::print(stderr, "Error: data spec '{}' has unsafe output path '{}'\n",
            spec.source_path.string(), relative.string());
        return 1;
    }
    const auto out_dir = out_root / relative;
    std::error_code ec;
    fs::create_directories(out_dir, ec);
    if (ec) {
        fmt::print(stderr, "Error: create dir '{}': {}\n",
            out_dir.string(), ec.message());
        return 1;
    }

    size_t emitted = 0;
    size_t skipped = 0;
    for (size_t index = 0; index < batch.rows.size(); ++index) {
        const auto& row = batch.rows[index];
        const auto output = out_dir / (filenames[index] + ".smalls");
        if (!force && fs::exists(output)) {
            ++skipped;
            continue;
        }
        if (!emit_structured_snapshot(output, spec, batch, row)) { return 1; }
        ++emitted;
    }
    fmt::print("[{}] emitted={} skipped={} filtered={} (rows: {})\n",
        spec.source_path.stem().string(), emitted, skipped,
        source->rows() - batch.rows.size(), source->rows());
    return 0;
}

static bool same_file_contents(const fs::path& lhs, const fs::path& rhs)
{
    std::error_code ec;
    const auto lhs_size = fs::file_size(lhs, ec);
    if (ec) { return false; }
    const auto rhs_size = fs::file_size(rhs, ec);
    if (ec || lhs_size != rhs_size) { return false; }

    std::ifstream left{lhs, std::ios::binary};
    std::ifstream right{rhs, std::ios::binary};
    return std::equal(std::istreambuf_iterator<char>{left},
        std::istreambuf_iterator<char>{},
        std::istreambuf_iterator<char>{right},
        std::istreambuf_iterator<char>{});
}

static std::set<fs::path> relative_files(const fs::path& root)
{
    std::set<fs::path> result;
    std::error_code ec;
    if (!fs::is_directory(root, ec)) { return result; }
    for (fs::recursive_directory_iterator it{root, ec}, end;
        !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file()) {
            result.insert(fs::relative(it->path(), root));
        }
    }
    return result;
}

static size_t report_snapshot_diff(
    const fs::path& generated,
    const fs::path& checked_in,
    const fs::path& display_root)
{
    const auto generated_files = relative_files(generated);
    const auto checked_files = relative_files(checked_in);
    std::set<fs::path> all;
    std::ranges::set_union(generated_files, checked_files,
        std::inserter(all, all.end()));

    size_t changes = 0;
    for (const auto& relative : all) {
        const bool has_generated = generated_files.contains(relative);
        const bool has_checked = checked_files.contains(relative);
        char status = 0;
        if (has_generated && !has_checked) {
            status = 'A';
        } else if (!has_generated && has_checked) {
            status = 'D';
        } else if (!same_file_contents(
                       generated / relative, checked_in / relative)) {
            status = 'M';
        }
        if (status) {
            fmt::print("{} {}\n", status,
                (display_root / relative).generic_string());
            ++changes;
        }
    }
    return changes;
}

static fs::path make_temporary_output()
{
    const auto seed = std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count();
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto path = fs::temp_directory_path()
            / fmt::format("smalls-datagen-{}-{}", seed, attempt);
        std::error_code ec;
        if (fs::create_directory(path, ec)) { return path; }
    }
    return {};
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

static void print_usage(const char* prog)
{
    fmt::print("Usage: {} --nwn <path> (--out <dir> | --check <dir>) [options]\n\n", prog);
    fmt::print("Required:\n");
    fmt::print("  --nwn <path>     Path to NWN installation directory\n");
    fmt::print("  --out <dir>      Snapshot root (e.g. lib/nw/smalls/scripts)\n");
    fmt::print("  --check <dir>    Generate in a temporary tree and report snapshot diff\n\n");
    fmt::print("Optional:\n");
    fmt::print("  --specs <dir>    JSON spec directory (default: adjacent to binary)\n");
    fmt::print("  --data-specs <dir> Structured specs (default: packaged nwn1/data_specs)\n");
    fmt::print("  --entity <name>  Only process this spec (e.g. feats)\n");
    fmt::print("  --no-overwrite   Skip existing files (default: overwrite)\n");
}

int main(int argc, char* argv[])
{
    nowide::args _(argc, argv);
    nw::init_logger(argc, argv);

    std::string nwn_path, out_path, check_path, specs_path, data_specs_path,
        entity_filter;
    bool force = true;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--nwn" && i + 1 < argc)
            nwn_path = argv[++i];
        else if (arg == "--out" && i + 1 < argc)
            out_path = argv[++i];
        else if (arg == "--check" && i + 1 < argc)
            check_path = argv[++i];
        else if (arg == "--specs" && i + 1 < argc)
            specs_path = argv[++i];
        else if (arg == "--data-specs" && i + 1 < argc)
            data_specs_path = argv[++i];
        else if (arg == "--entity" && i + 1 < argc)
            entity_filter = argv[++i];
        else if (arg == "--no-overwrite")
            force = false;
        else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
    }

    if (nwn_path.empty() || (out_path.empty() == check_path.empty())) {
        print_usage(argv[0]);
        return 1;
    }

    const fs::path executable_dir = fs::absolute(fs::path{argv[0]}).parent_path();
    if (specs_path.empty())
        specs_path = (executable_dir / "specs").string();
    if (data_specs_path.empty())
        data_specs_path = (executable_dir / "stdlib/nwn1/data_specs").string();

    fs::path temporary_output;

    nw::kernel::config().set_paths(nwn_path, "");
    nw::kernel::config().initialize();
    DatagenProfile profile{executable_dir / "stdlib"};
    nw::kernel::set_game_profile(&profile);
    nw::kernel::services().start();

    // Collect spec files
    std::vector<fs::path> spec_files;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(specs_path, ec)) {
        if (entry.path().extension() != ".json") continue;
        if (!entity_filter.empty() && entry.path().stem() != entity_filter) continue;
        spec_files.push_back(entry.path());
    }
    if (ec) {
        fmt::print(stderr, "Error: iterate specs '{}': {}\n", specs_path, ec.message());
        nw::kernel::services().shutdown();
        return 1;
    }

    std::ranges::sort(spec_files);

    nw::Vector<fs::path> data_spec_files;
    ec.clear();
    for (const auto& entry : fs::directory_iterator(data_specs_path, ec)) {
        if (entry.path().extension() != ".json") { continue; }
        if (!entity_filter.empty() && entry.path().stem() != entity_filter) {
            continue;
        }
        data_spec_files.push_back(entry.path());
    }
    if (ec) {
        fmt::print(stderr, "Error: iterate data specs '{}': {}\n",
            data_specs_path, ec.message());
        nw::kernel::services().shutdown();
        return 1;
    }
    std::ranges::sort(data_spec_files);

    if (spec_files.empty() && data_spec_files.empty()) {
        fmt::print(stderr, "No matching specs found{}.\n",
            entity_filter.empty()
                ? ""
                : fmt::format(" for entity '{}'", entity_filter));
        nw::kernel::services().shutdown();
        return 1;
    }

    // Parse all specs
    std::vector<GenSpec> specs;
    for (const auto& sp : spec_files) {
        GenSpec s;
        if (parse_spec(sp, s)) specs.push_back(std::move(s));
    }

    nw::Vector<nw::smalls::DataSpec> data_specs;
    nw::Vector<nw::smalls::DataDiagnostic> data_diagnostics;
    if (!data_spec_files.empty()
        && !nw::smalls::parse_data_specs(
            data_spec_files, data_specs, data_diagnostics)) {
        for (const auto& diagnostic : data_diagnostics) {
            fmt::print(stderr, "Error: {} target '{}': {}\n",
                diagnostic.source.string(), diagnostic.target,
                diagnostic.message);
        }
        nw::kernel::services().shutdown();
        return 1;
    }

    if (!check_path.empty()) {
        temporary_output = make_temporary_output();
        if (temporary_output.empty()) {
            fmt::print(stderr, "Error: unable to create temporary output directory\n");
            nw::kernel::services().shutdown();
            return 1;
        }
        out_path = temporary_output.string();
        force = true;
    }

    int ret = 0;
    fs::path out_root = out_path;

    std::set<fs::path> output_subdirs;
    for (const auto& spec : specs) {
        output_subdirs.insert(spec.output_subdir);
    }
    for (const auto& spec : data_specs) {
        output_subdirs.insert(config_output_subdir(spec.config_path));
    }
    for (const auto& relative : output_subdirs) {
        if (!valid_output_subdir(relative)) {
            fmt::print(stderr, "Error: unsafe generated output path '{}'\n",
                relative.string());
            ret = 1;
            continue;
        }
        if (force) {
            std::error_code remove_error;
            fs::remove_all(out_root / relative, remove_error);
            if (remove_error) {
                fmt::print(stderr, "Error: remove generated directory '{}': {}\n",
                    (out_root / relative).string(), remove_error.message());
                ret = 1;
            }
        }
    }

    // Global scan results: spec_name → (2da_name → integer ID)
    std::map<std::string, ScanResult> scan_results;

    // Pass 1: run scan-based specs first (their results feed @scan_ref in pass 2)
    for (const auto& spec : specs) {
        if (spec.scan_column.empty()) continue;
        ScanResult result;
        if (run_scan_spec(spec, out_root, force, result) != 0) ret = 1;
        scan_results[spec.spec_name] = std::move(result);
    }

    // Pass 2: run standard specs
    for (const auto& spec : specs) {
        if (!spec.scan_column.empty()) continue;
        if (run_standard_spec(spec, out_root, force, scan_results) != 0) ret = 1;
    }

    for (const auto& spec : data_specs) {
        if (run_structured_spec(spec, out_root, force) != 0) { ret = 1; }
    }

    if (!check_path.empty() && ret == 0) {
        size_t changes = 0;
        for (const auto& relative : output_subdirs) {
            changes += report_snapshot_diff(out_root / relative,
                fs::path{check_path} / relative, relative);
        }
        fmt::print("snapshot changes={}\n", changes);
        if (changes != 0) { ret = 1; }
    }

    nw::kernel::services().shutdown();
    if (!temporary_output.empty()) {
        std::error_code cleanup_error;
        fs::remove_all(temporary_output, cleanup_error);
        if (cleanup_error) {
            fmt::print(stderr, "Warning: cleanup '{}': {}\n",
                temporary_output.string(), cleanup_error.message());
        }
    }
    return ret;
}
