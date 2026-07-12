#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Strings.hpp>
#include <nw/kernel/TwoDACache.hpp>

#include <fmt/core.h>
#include <nlohmann/json.hpp>
#include <nowide/args.hpp>

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

} // namespace

// ---------------------------------------------------------------------------
// Spec structures
// ---------------------------------------------------------------------------

struct FieldSpec {
    std::string name;
    std::string type;   // "int", "float", "bool", "StrRef", "ResRef", "Resource", "int[N]", "array(int)"
    std::string source; // "@row_index", "@scan_index", "@scan_ref:COL:spec",
                        // "@indirect:COL:SUBCOL:N", "@indirect_grid:COL:PREFIX:N[:LIMIT_COL]",
                        // "@array:COL1,...",
                        // plain column name, or empty for constant default
    std::string default_val;
    std::map<std::string, int> string_enum; // case-insensitive string→int
};

struct GenSpec {
    std::string spec_name; // filename stem (for --entity filter and @scan_ref lookups)
    std::string smalls_type;
    std::string output_subdir;
    std::optional<std::string> valid_column;
    std::vector<FieldSpec> fields;

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
    int scan_index = -1)                                                   // ID in scan results (scan mode only)
{
    out << "    " << f.name << " = ";

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

    for (const auto& jf : j.value("fields", json::array())) {
        FieldSpec f;
        f.name = jf.value("name", "");
        f.type = jf.value("type", "int");
        f.default_val = jf.value("default", "0");

        if (jf.contains("source") && !jf["source"].is_null())
            f.source = jf["source"].get<std::string>();

        if (jf.contains("enum"))
            for (auto& [k, v] : jf["enum"].items()) {
                std::string lower = k;
                for (char& c : lower)
                    c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
                f.string_enum[lower] = v.get<int>();
            }

        out_spec.fields.push_back(std::move(f));
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
    for (size_t fi = 0; fi < spec.fields.size(); ++fi) {
        emit_field(ofs, spec.fields[fi], row_index, tda,
            cached_secondary, scan_results, scan_index);
        if (fi + 1 < spec.fields.size()) ofs << ",";
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

    for (const auto& f : spec.fields) {
        size_t prefix_len = 0;
        if (f.source.rfind("@indirect:", 0) == 0) {
            prefix_len = 10;
        } else if (f.source.rfind("@indirect_grid:", 0) == 0) {
            prefix_len = 15;
        } else {
            continue;
        }
        std::string src = f.source.substr(prefix_len);
        auto colon1 = src.find(':');
        std::string col = src.substr(0, colon1);

        auto it = resolved.find(col);
        if (it != resolved.end()) {
            cached_secondary[f.name] = it->second;
            continue;
        }

        nw::StringView table_name;
        const nw::StaticTwoDA* sec = nullptr;
        if (tda->get_to(row, col, table_name) && !table_name.empty())
            sec = nw::kernel::twodas().get(nw::String(table_name));

        resolved[col] = sec;
        cached_secondary[f.name] = sec;
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
// Main
// ---------------------------------------------------------------------------

static void print_usage(const char* prog)
{
    fmt::print("Usage: {} --nwn <path> --out <dir> [options]\n\n", prog);
    fmt::print("Required:\n");
    fmt::print("  --nwn <path>     Path to NWN installation directory\n");
    fmt::print("  --out <dir>      Output root (e.g. lib/nw/smalls/scripts/nwn1)\n\n");
    fmt::print("Optional:\n");
    fmt::print("  --specs <dir>    JSON spec directory (default: adjacent to binary)\n");
    fmt::print("  --entity <name>  Only process this spec (e.g. feats)\n");
    fmt::print("  --no-overwrite   Skip existing files (default: overwrite)\n");
}

int main(int argc, char* argv[])
{
    nowide::args _(argc, argv);
    nw::init_logger(argc, argv);

    std::string nwn_path, out_path, specs_path, entity_filter;
    bool force = true;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--nwn" && i + 1 < argc)
            nwn_path = argv[++i];
        else if (arg == "--out" && i + 1 < argc)
            out_path = argv[++i];
        else if (arg == "--specs" && i + 1 < argc)
            specs_path = argv[++i];
        else if (arg == "--entity" && i + 1 < argc)
            entity_filter = argv[++i];
        else if (arg == "--no-overwrite")
            force = false;
        else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
    }

    if (nwn_path.empty() || out_path.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    if (specs_path.empty())
        specs_path = (fs::path(argv[0]).parent_path() / "specs").string();

    nw::kernel::config().set_paths(nwn_path, "");
    nw::kernel::config().initialize();
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

    if (spec_files.empty()) {
        fmt::print(stderr, "No spec files found in '{}'{}\n", specs_path,
            entity_filter.empty() ? "" : fmt::format(" (filter: '{}')", entity_filter));
        nw::kernel::services().shutdown();
        return 1;
    }

    std::sort(spec_files.begin(), spec_files.end());

    // Parse all specs
    std::vector<GenSpec> specs;
    for (const auto& sp : spec_files) {
        GenSpec s;
        if (parse_spec(sp, s)) specs.push_back(std::move(s));
    }

    int ret = 0;
    fs::path out_root = out_path;

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

    nw::kernel::services().shutdown();
    return ret;
}
