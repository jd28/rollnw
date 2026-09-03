#include <nw/formats/StaticTwoDA.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Strings.hpp>
#include <nw/smalls/data_spec.hpp>
#include <nw/smalls/data_transform.hpp>
#include <nw/smalls/runtime.hpp>

#include <fmt/core.h>
#include <nowide/args.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

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

// Make a sanitized name unique without coupling the filename to its row ID.
static std::string unique_filename(
    const std::string& slug, std::set<std::string>& used)
{
    if (used.insert(slug).second) { return slug; }
    for (size_t ordinal = 2;; ++ordinal) {
        std::string candidate = slug + "_" + std::to_string(ordinal);
        if (used.insert(candidate).second) { return candidate; }
    }
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

static bool emit_materialized_value(std::ostream& out,
    const nw::smalls::MaterializedDataValue::Value& value,
    const nw::smalls::DataValueExpression& expression)
{
    if (const auto* integer = std::get_if<int32_t>(&value)) {
        out << *integer;
    } else if (const auto* floating = std::get_if<float>(&value)) {
        auto text = fmt::format("{:.6g}", *floating);
        if (text.find('.') == std::string::npos
            && text.find('e') == std::string::npos) {
            text += ".0";
        }
        out << text;
    } else if (const auto* boolean = std::get_if<bool>(&value)) {
        out << (*boolean ? "true" : "false");
    } else if (const auto* string = std::get_if<nw::String>(&value)) {
        out << '"' << escape_smalls_string(*string) << '"';
    } else if (const auto* resref = std::get_if<nw::Resref>(&value)) {
        out << "resref(\"" << escape_smalls_string(resref->string()) << "\")";
    } else if (const auto* integers = std::get_if<nw::Vector<int32_t>>(&value)) {
        out << '{';
        for (size_t index = 0; index < integers->size(); ++index) {
            if (index != 0) { out << ", "; }
            out << (*integers)[index];
        }
        out << '}';
    } else if (const auto* structs = std::get_if<nw::Vector<
                   nw::smalls::MaterializedDataValue::IntegerStruct>>(&value)) {
        out << '{';
        for (size_t index = 0; index < structs->size(); ++index) {
            if (index != 0) { out << ", "; }
            out << unqualified_type(expression.element_type) << " { ";
            const auto& fields = (*structs)[index].fields;
            for (size_t field_index = 0; field_index < fields.size();
                ++field_index) {
                if (field_index != 0) { out << ", "; }
                out << fields[field_index].first << " = "
                    << fields[field_index].second;
            }
            out << " }";
        }
        out << '}';
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
        if (!emit_materialized_value(out, value->value, field.value)) {
            return false;
        }
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
            if (!emit_materialized_value(out, value->value, field.value)) {
                return false;
            }
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
    const nw::smalls::DataSourceBatch& sources,
    const nw::smalls::MaterializedDataBatch& batch,
    std::vector<std::string>& output)
{
    const nw::smalls::DataTwoDAReferenceSet* source_references = nullptr;
    const nw::StaticTwoDA* filename_table = nullptr;
    size_t filename_column = nw::StaticTwoDA::npos;
    if (spec.source_kind == nw::smalls::DataSourceKind::twoda_references) {
        const auto found = std::ranges::find(sources.references,
            spec.source_reference_column,
            &nw::smalls::DataTwoDAReferenceSet::column);
        if (found == sources.references.end()) {
            fmt::print(stderr,
                "Error: data spec '{}' has no source reference batch\n",
                spec.source_path.string());
            return false;
        }
        source_references = &*found;
    } else {
        if (spec.snapshot_filename_column.empty()) {
            fmt::print(stderr,
                "Error: data spec '{}' does not declare a snapshot filename column\n",
                spec.source_path.string());
            return false;
        }
        filename_column = sources.primary.column_index(
            spec.snapshot_filename_column);
        if (filename_column != nw::StaticTwoDA::npos) {
            filename_table = &sources.primary;
        } else {
            filename_column = sources.primary_base.column_index(
                spec.snapshot_filename_column);
            if (filename_column != nw::StaticTwoDA::npos) {
                filename_table = &sources.primary_base;
            }
        }
        if (!filename_table) {
            fmt::print(stderr,
                "Error: data spec '{}' has no snapshot filename column '{}'\n",
                spec.source_path.string(), spec.snapshot_filename_column);
            return false;
        }
    }

    std::vector<std::string> candidate;
    candidate.reserve(batch.rows.size());
    std::set<std::string> used;
    for (const auto& row : batch.rows) {
        std::string source_name;
        if (source_references) {
            if (row.id < 0
                || static_cast<size_t>(row.id)
                    >= source_references->resources.size()) {
                fmt::print(stderr,
                    "Error: data spec '{}' row {} has no source resource name\n",
                    spec.source_path.string(), row.id);
                return false;
            }
            source_name = source_references
                              ->resources[static_cast<size_t>(row.id)]
                              .name;
        } else if (spec.snapshot_filename_is_strref) {
            int32_t strref = -1;
            if (row.id >= 0
                && filename_table->get_to(static_cast<size_t>(row.id),
                    filename_column, strref)
                && strref >= 0) {
                source_name = nw::kernel::strings().get(
                    static_cast<uint32_t>(strref));
            }
        } else {
            nw::String value;
            if (row.id >= 0
                && filename_table->get_to(static_cast<size_t>(row.id),
                    filename_column, value)) {
                source_name = value;
            }
        }

        if (source_name.empty() || source_name == "****") {
            fmt::print(stderr,
                "Error: data spec '{}' row {} has no snapshot filename in column '{}'\n",
                spec.source_path.string(), row.id,
                spec.snapshot_filename_column);
            return false;
        }

        auto filename = slugify(source_name);
        if (filename.empty()) {
            fmt::print(stderr,
                "Error: data spec '{}' row {} snapshot filename sanitizes to empty\n",
                spec.source_path.string(), row.id);
            return false;
        }
        candidate.push_back(unique_filename(filename, used));
    }
    output = std::move(candidate);
    return true;
}

static int run_structured_spec(
    const nw::smalls::DataSpec& spec,
    const fs::path& out_root,
    bool force)
{
    nw::smalls::DataSourceBatch sources;
    nw::smalls::MaterializedDataBatch batch;
    nw::Vector<nw::smalls::DataDiagnostic> diagnostics;
    const bool loaded = nw::smalls::load_data_sources(
        spec, nw::kernel::resman(), sources, diagnostics);
    const bool materialized = loaded
        && nw::smalls::materialize_data_rows(
            spec, sources, batch, diagnostics);
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
    if (spec.source_kind == nw::smalls::DataSourceKind::twoda
        && spec.snapshot_filename_column.empty()) {
        fmt::print("[{}] materialized={} filtered={} snapshots=disabled\n",
            spec.source_path.stem().string(), batch.rows.size(),
            batch.indexed_size - batch.rows.size());
        return 0;
    }

    std::vector<std::string> filenames;
    if (!compute_structured_snapshot_filenames(
            spec, sources, batch, filenames)) {
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
        batch.indexed_size - batch.rows.size(), batch.indexed_size);
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
    fmt::print("  --data-specs <dir> Structured specs (default: packaged nwn1/data_specs)\n");
    fmt::print("  --entity <name>  Only process this spec (e.g. feats)\n");
    fmt::print("  --no-overwrite   Skip existing files (default: overwrite)\n");
}

int main(int argc, char* argv[])
{
    nowide::args _(argc, argv);
    nw::init_logger(argc, argv);

    std::string nwn_path, out_path, check_path, data_specs_path,
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
    if (data_specs_path.empty())
        data_specs_path = (executable_dir / "stdlib/nwn1/data_specs").string();

    fs::path temporary_output;

    nw::kernel::config().set_paths(nwn_path, "");
    nw::ConfigOptions config_options;
    config_options.profile = "nwn1";
    nw::kernel::config().initialize(std::move(config_options));
    nw::kernel::services().create();
    nw::kernel::runtime().add_module_path(executable_dir / "stdlib/core");
    nw::kernel::runtime().add_module_path(executable_dir / "stdlib/nwn1");
    nw::kernel::services().start();

    std::error_code ec;
    nw::Vector<fs::path> data_spec_files;
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

    if (data_spec_files.empty()) {
        fmt::print(stderr, "No matching specs found{}.\n",
            entity_filter.empty()
                ? ""
                : fmt::format(" for entity '{}'", entity_filter));
        nw::kernel::services().shutdown();
        return 1;
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
    for (const auto& spec : data_specs) {
        if (spec.source_kind == nw::smalls::DataSourceKind::twoda_references
            || !spec.snapshot_filename_column.empty()) {
            output_subdirs.insert(config_output_subdir(spec.config_path));
        }
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
