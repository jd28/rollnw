#include "rml_smalls_bridge.hpp"

#include "script_commands.hpp"
#include "smalls_diagnostics.hpp"
#include "smalls_rmlui.hpp"
#include "smalls_ui_v1.hpp"
#include "ui_contract.hpp"
#include "ui_v1.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/smalls/Ast.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/runtime.hpp>
#include <nw/util/game_install.hpp>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace nw::toolset {

namespace {

std::mutex g_smalls_init_mutex;
bool g_kernel_started = false;
bool g_kernel_failed = false;
bool g_paths_registered = false;
bool g_toolset_module_loaded = false;
bool g_core_commands_module_loaded = false;
bool g_core_rmlui_module_loaded = false;
bool g_toolset_item_editor_module_loaded = false;
uint64_t g_runtime_generation = 0;

std::optional<UiListEventType> to_list_event_type(std::string_view event_name)
{
    if (event_name == ui_contract::list_v1::event_hover) {
        return UiListEventType::hover;
    }
    if (event_name == ui_contract::list_v1::event_select) {
        return UiListEventType::select;
    }
    if (event_name == ui_contract::list_v1::event_activate) {
        return UiListEventType::activate;
    }
    if (event_name == ui_contract::list_v1::event_scroll) {
        return UiListEventType::scroll;
    }
    return std::nullopt;
}

bool validate_list_callback_signature(nw::smalls::Runtime& rt, std::string_view module_path,
    std::string_view fn_name, UiListEventType event_type)
{
    const auto expected_arg_type = (event_type == UiListEventType::scroll)
        ? rt.type_id("core.ui.ListScroll", false)
        : rt.type_id("core.ui.ListSelection", false);
    if (expected_arg_type == nw::smalls::invalid_type_id) {
        return false;
    }

    const std::string qualified = std::string(module_path) + "." + std::string(fn_name);
    const auto external_idx = rt.find_external_function(qualified);
    if (external_idx == UINT32_MAX) {
        return false;
    }

    const auto* function = rt.get_external_function(external_idx);
    if (!function) {
        return false;
    }

    if (function->metadata.params.size() != 1) {
        return false;
    }

    return function->metadata.params[0].type_id == expected_arg_type;
}

bool read_struct_string_field(nw::smalls::Runtime& rt, const nw::smalls::Value& value, std::string_view field, std::string& out)
{
    if (value.storage != nw::smalls::ValueStorage::heap || value.data.hptr.value == 0) {
        return false;
    }

    const nw::smalls::Value field_value = rt.read_struct_field(value.data.hptr, value.type_id, field);
    if (field_value.type_id != rt.string_type()
        || field_value.storage != nw::smalls::ValueStorage::heap
        || field_value.data.hptr.value == 0) {
        return false;
    }

    out = std::string(rt.get_string_view(field_value.data.hptr));
    return true;
}

nw::smalls::Value make_string(nw::smalls::Runtime& rt, std::string_view value)
{
    return nw::smalls::Value::make_string(rt.alloc_string(value));
}

nw::smalls::Value make_selection_value(nw::smalls::Runtime& rt, const UiListSelection& selection)
{
    const auto type_id = rt.type_id("core.ui.ListSelection", false);
    if (type_id == nw::smalls::invalid_type_id) {
        return {};
    }

    const auto ptr = rt.alloc_struct(type_id);
    if (ptr.value == 0) {
        return {};
    }

    if (!rt.write_struct_field(ptr, type_id, "list_id", make_string(rt, selection.list_id))
        || !rt.write_struct_field(ptr, type_id, "key", make_string(rt, selection.key))
        || !rt.write_struct_field(ptr, type_id, "index", nw::smalls::Value::make_int(selection.index))) {
        return {};
    }
    return nw::smalls::Value::make_heap(ptr, type_id);
}

nw::smalls::Value make_scroll_value(nw::smalls::Runtime& rt, const UiListScroll& scroll)
{
    const auto type_id = rt.type_id("core.ui.ListScroll", false);
    if (type_id == nw::smalls::invalid_type_id) {
        return {};
    }

    const auto ptr = rt.alloc_struct(type_id);
    if (ptr.value == 0) {
        return {};
    }

    if (!rt.write_struct_field(ptr, type_id, "list_id", make_string(rt, scroll.list_id))
        || !rt.write_struct_field(ptr, type_id, "top", nw::smalls::Value::make_int(scroll.top))
        || !rt.write_struct_field(ptr, type_id, "start", nw::smalls::Value::make_int(scroll.start))
        || !rt.write_struct_field(ptr, type_id, "end", nw::smalls::Value::make_int(scroll.end))) {
        return {};
    }
    return nw::smalls::Value::make_heap(ptr, type_id);
}

void bind_ui_list_callbacks_from_manifest(nw::smalls::Runtime& rt, std::string_view module_path)
{
    auto bind_callback = [&](std::string_view list_id, std::string_view event_name, std::string_view fn_name) {
        if (list_id.empty() || event_name.empty() || fn_name.empty()) {
            return;
        }

        const auto event_type = to_list_event_type(event_name);
        if (!event_type.has_value()) {
            return;
        }

        if (!validate_list_callback_signature(rt, module_path, fn_name, *event_type)) {
            return;
        }

        ui_v1_host().set_callback(std::string(list_id), *event_type,
            std::string(module_path) + "." + std::string(fn_name));
    };

    const auto result = rt.execute_script(module_path, "__toolset_ui_bindings", {});
    if (result.ok()) {
        const auto& bindings_value = result.value;
        if (bindings_value.storage == nw::smalls::ValueStorage::heap && bindings_value.data.hptr.value != 0) {
            if (auto* array = rt.get_array_typed(bindings_value.data.hptr)) {
                for (size_t i = 0; i < array->size(); ++i) {
                    nw::smalls::Value entry;
                    if (!array->get_value(i, entry, rt)) {
                        continue;
                    }

                    std::string list_id;
                    std::string event_name;
                    std::string fn_name;
                    if (!read_struct_string_field(rt, entry, "list_id", list_id)
                        || !read_struct_string_field(rt, entry, "event", event_name)
                        || !read_struct_string_field(rt, entry, "fn_name", fn_name)) {
                        continue;
                    }

                    bind_callback(list_id, event_name, fn_name);
                }
            }
        }
    }

    auto annotation_arg_string = [](const nw::smalls::AnnotationArg& arg) -> std::string {
        const auto* literal = dynamic_cast<const nw::smalls::LiteralExpression*>(arg.value);
        if (!literal) {
            return {};
        }

        std::string raw = std::string(literal->literal.loc.view());
        if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
            raw = raw.substr(1, raw.size() - 2);
        }
        return raw;
    };

    if (auto* script = rt.get_module(module_path)) {
        for (const auto& [_, exp] : script->exports()) {
            if (exp.kind != nw::smalls::Export::Kind::function || !exp.decl) {
                continue;
            }

            const auto* fn = dynamic_cast<const nw::smalls::FunctionDefinition*>(exp.decl);
            if (!fn) {
                continue;
            }

            for (const auto& ann : fn->annotations_) {
                if (ann.name.loc.view() != std::string_view{"ui_list"} || ann.args.size() < 2) {
                    continue;
                }

                const std::string list_id = annotation_arg_string(ann.args[0]);
                const std::string event_name = annotation_arg_string(ann.args[1]);
                bind_callback(list_id, event_name, fn->identifier_.loc.view());
            }
        }
    }
}

std::filesystem::path executable_dir()
{
    namespace fs = std::filesystem;

#if defined(_WIN32)
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD size = 0;
    while (true) {
        size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (size == 0) {
            return {};
        }
        if (size < buffer.size() - 1) {
            buffer.resize(size);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    return fs::path(buffer).parent_path();
#elif defined(__linux__)
    std::string buffer(4096, '\0');
    const ssize_t size = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (size <= 0) {
        return {};
    }
    buffer.resize(static_cast<size_t>(size));
    return fs::path(buffer).parent_path();
#else
    return {};
#endif
}

std::filesystem::path find_source_toolset_scripts_from(std::filesystem::path base)
{
    namespace fs = std::filesystem;
    std::error_code ec;

    for (int i = 0; i < 8; ++i) {
        const fs::path candidate = base / "tools" / "ui" / "scripts" / "toolset";
        if (fs::exists(candidate / "package.json", ec) && fs::is_directory(candidate, ec)) {
            return fs::weakly_canonical(candidate, ec);
        }
        if (!base.has_parent_path()) {
            break;
        }
        base = base.parent_path();
    }

    return {};
}

std::filesystem::path find_source_toolset_scripts_path()
{
    namespace fs = std::filesystem;
    std::error_code ec;

    if (const fs::path from_cwd = find_source_toolset_scripts_from(fs::current_path(ec)); !from_cwd.empty()) {
        return from_cwd;
    }

    if (const fs::path from_exe = find_source_toolset_scripts_from(executable_dir()); !from_exe.empty()) {
        return from_exe;
    }

    // Dev-only fallback: __FILE__ may be just the basename, depending on compiler flags.
    const fs::path source_path(__FILE__);
    if (const fs::path from_file = find_source_toolset_scripts_from(source_path.parent_path()); !from_file.empty()) {
        return from_file;
    }

    return {};
}

std::filesystem::path resolve_toolset_scripts_path(const std::filesystem::path& smalls_scripts)
{
    namespace fs = std::filesystem;
    std::error_code ec;

    const fs::path packaged_toolset = smalls_scripts / "toolset";
    if (fs::exists(packaged_toolset / "package.json", ec) && fs::is_directory(packaged_toolset, ec)) {
        return fs::weakly_canonical(packaged_toolset, ec);
    }

    return find_source_toolset_scripts_path();
}

std::filesystem::path toolset_module_file(std::string_view module_path)
{
    namespace fs = std::filesystem;
    constexpr std::string_view prefix = "toolset.";
    if (!module_path.starts_with(prefix)) {
        return {};
    }

    std::string relative{module_path.substr(prefix.size())};
    for (char& ch : relative) {
        if (ch == '.') {
            ch = fs::path::preferred_separator;
        }
    }
    relative += ".smalls";
    return fs::path{relative};
}

std::optional<std::string> read_text_file(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }

    return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

nw::smalls::Script* load_toolset_module(nw::smalls::Runtime& rt,
    std::string_view module_path,
    const std::filesystem::path& toolset_scripts)
{
    const std::filesystem::path module_file = toolset_module_file(module_path);
    if (!module_file.empty() && !toolset_scripts.empty()) {
        const std::filesystem::path source_path = toolset_scripts / module_file;
        if (auto source = read_text_file(source_path)) {
            auto* script = rt.load_module_from_source(module_path, *source);
            return (script && script->errors() == 0) ? script : nullptr;
        }
        LOG_F(WARNING, "Smalls bridge could not read direct module '{}' from '{}'", module_path, source_path.string());
    }

    auto* script = rt.load_module(module_path);
    return (script && script->errors() == 0) ? script : nullptr;
}

void add_smalls_paths(nw::smalls::Runtime& rt, const std::filesystem::path& smalls_scripts)
{
    rt.add_module_path(smalls_scripts / "core");
    rt.add_module_path(smalls_scripts / "nwn1");

    if (const std::filesystem::path toolset_scripts = resolve_toolset_scripts_path(smalls_scripts); !toolset_scripts.empty()) {
        rt.add_module_path(toolset_scripts);
        return;
    }

    LOG_F(WARNING, "Smalls bridge did not find toolset scripts for stdlib root '{}'", smalls_scripts.string());
}

std::filesystem::path validate_smalls_scripts_root(const std::filesystem::path& root)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (root.empty()) {
        return {};
    }

    const fs::path core = root / "core";
    const fs::path nwn1 = root / "nwn1";
    if (fs::exists(core / "package.json", ec) && fs::is_directory(core, ec)
        && fs::exists(nwn1 / "package.json", ec) && fs::is_directory(nwn1, ec)) {
        return fs::weakly_canonical(root, ec);
    }
    return {};
}

std::filesystem::path find_installed_smalls_scripts_from(std::filesystem::path base)
{
    namespace fs = std::filesystem;

    for (int i = 0; i < 8; ++i) {
        if (const fs::path candidate = validate_smalls_scripts_root(base / "stdlib"); !candidate.empty()) {
            return candidate;
        }
        if (!base.has_parent_path()) {
            break;
        }
        base = base.parent_path();
    }

    return {};
}

std::filesystem::path find_smalls_scripts_from(std::filesystem::path base)
{
    namespace fs = std::filesystem;
    std::error_code ec;

    for (int i = 0; i < 8; ++i) {
        const fs::path candidate = base / "lib" / "nw" / "smalls" / "scripts";
        if (fs::exists(candidate, ec) && fs::is_directory(candidate, ec)) {
            return fs::weakly_canonical(candidate, ec);
        }
        if (!base.has_parent_path()) {
            break;
        }
        base = base.parent_path();
    }

    return {};
}

std::filesystem::path resolve_smalls_scripts_path()
{
    namespace fs = std::filesystem;
    std::error_code ec;

    if (const char* env = std::getenv("ROLLNW_SMALLS_STDLIB")) {
        if (const fs::path from_env = validate_smalls_scripts_root(env); !from_env.empty()) {
            return from_env;
        }
    }

    if (const fs::path from_cwd = find_smalls_scripts_from(fs::current_path(ec)); !from_cwd.empty()) {
        return from_cwd;
    }

    if (const fs::path from_cwd = find_installed_smalls_scripts_from(fs::current_path(ec)); !from_cwd.empty()) {
        return from_cwd;
    }

    if (const fs::path from_exe = find_installed_smalls_scripts_from(executable_dir()); !from_exe.empty()) {
        return from_exe;
    }

    // Dev-only fallback: __FILE__ may not exist in installed/package builds.
    const fs::path source_path(__FILE__);
    if (const fs::path from_file = find_smalls_scripts_from(source_path.parent_path()); !from_file.empty()) {
        return from_file;
    }

    return {};
}

} // namespace

bool RmlSmallsBridge::initialize()
{
    std::lock_guard<std::mutex> lock(g_smalls_init_mutex);

    auto& services = nw::kernel::services();
    const uint64_t service_generation = services.generation();
    if (initialized_ && runtime_generation_ == service_generation
        && services.get<nw::smalls::Runtime>()) {
        return true;
    }
    if (g_kernel_failed && g_runtime_generation == service_generation) {
        initialized_ = false;
        return false;
    }

    try {
        const std::filesystem::path smalls_scripts = resolve_smalls_scripts_path();
        if (smalls_scripts.empty()) {
            initialized_ = false;
            return false;
        }
        const std::filesystem::path toolset_scripts = resolve_toolset_scripts_path(smalls_scripts);

        if (!g_kernel_started) {
            const auto install = nw::probe_nwn_install(nw::GameVersion::vEE);
            if (install.install.empty()) {
                g_kernel_failed = true;
                initialized_ = false;
                return false;
            }

            nw::kernel::config().set_paths(install.install, install.user);
            nw::ConfigOptions config_options;
            config_options.profile = "nwn1";
            config_options.init_module = "";
            nw::kernel::config().initialize(std::move(config_options));
            nw::kernel::config().set_init_module("");
            services.start();

            auto& rt = nw::kernel::runtime();
            add_smalls_paths(rt, smalls_scripts);
            g_paths_registered = true;
            g_kernel_started = true;
        }

        auto& rt = nw::kernel::runtime();
        const uint64_t runtime_generation = services.generation();
        if (g_runtime_generation != runtime_generation) {
            ui_v1_host().reset();
            smalls_rmlui_host().clear_active_object();
            active_area_ = nw::ObjectHandle{};
            g_kernel_failed = false;
            g_paths_registered = false;
            g_toolset_module_loaded = false;
            g_core_commands_module_loaded = false;
            g_core_rmlui_module_loaded = false;
            g_toolset_item_editor_module_loaded = false;
            g_runtime_generation = runtime_generation;
        }
        if (g_kernel_failed) {
            initialized_ = false;
            return false;
        }

        if (!g_paths_registered) {
            add_smalls_paths(rt, smalls_scripts);
            g_paths_registered = true;
        }

        if (!g_toolset_module_loaded) {
            auto* ui_script = rt.load_module("core.ui");
            if (!ui_script || ui_script->errors() != 0) {
                g_kernel_failed = true;
                initialized_ = false;
                return false;
            }
            register_smalls_ui_v1(rt);
            auto* native_script = rt.load_module("core.ui.v1");
            if (!native_script || native_script->errors() != 0) {
                g_kernel_failed = true;
                initialized_ = false;
                return false;
            }
            auto* script = load_toolset_module(rt, module_path_, toolset_scripts);
            if (!script) {
                g_kernel_failed = true;
                initialized_ = false;
                return false;
            }
            g_toolset_module_loaded = true;
        }

        if (!g_core_commands_module_loaded) {
            auto* commands_script = rt.load_module("core.commands");
            if (!commands_script || commands_script->errors() != 0) {
                g_kernel_failed = true;
                initialized_ = false;
                return false;
            }
            register_smalls_commands_v1(rt);
            auto* commands_v1_script = rt.load_module("core.commands.v1");
            if (!commands_v1_script || commands_v1_script->errors() != 0) {
                g_kernel_failed = true;
                initialized_ = false;
                return false;
            }
            g_core_commands_module_loaded = true;
        }

        if (!g_core_rmlui_module_loaded) {
            register_smalls_rmlui(rt);
            auto* rmlui_script = rt.load_module("core.rmlui");
            if (!rmlui_script || rmlui_script->errors() != 0) {
                g_kernel_failed = true;
                initialized_ = false;
                return false;
            }
            if (!rt.get_or_compile_module(rmlui_script)) {
                g_kernel_failed = true;
                initialized_ = false;
                return false;
            }
            g_core_rmlui_module_loaded = true;
        }

        if (!g_toolset_item_editor_module_loaded) {
            auto* item_editor_script = load_toolset_module(
                rt, "toolset.item_editor", toolset_scripts);
            if (!item_editor_script || !rt.get_or_compile_module(item_editor_script)) {
                g_kernel_failed = true;
                initialized_ = false;
                return false;
            }
            const auto initialized = rt.execute_script(
                "toolset.item_editor", "initialize", {});
            if (!initialized.ok() || initialized.value.type_id != rt.bool_type()
                || !initialized.value.data.bval) {
                g_kernel_failed = true;
                initialized_ = false;
                return false;
            }
            bind_ui_list_callbacks_from_manifest(rt, "toolset.item_editor");
            g_toolset_item_editor_module_loaded = true;
        }

        bind_ui_list_callbacks_from_manifest(rt, module_path_);

        initialized_ = true;
        runtime_generation_ = runtime_generation;
    } catch (...) {
        g_kernel_failed = true;
        initialized_ = false;
    }

    return initialized_;
}

bool RmlSmallsBridge::ensure_current_runtime()
{
    auto& services = nw::kernel::services();
    if (initialized_ && runtime_generation_ == services.generation()
        && services.get<nw::smalls::Runtime>()) {
        return true;
    }
    return initialize();
}

void RmlSmallsBridge::publish_active_object(nw::ObjectHandle object)
{
    if (!ensure_current_runtime()) {
        return;
    }
    const auto previous = smalls_rmlui_host().active_object();
    smalls_rmlui_host().publish_active_object(object);
    if (previous != smalls_rmlui_host().active_object()) {
        refresh_ui_lists();
    }
}

void RmlSmallsBridge::clear_active_object()
{
    const auto previous = smalls_rmlui_host().active_object();
    smalls_rmlui_host().clear_active_object();
    if (previous.type != nw::ObjectType::invalid) {
        refresh_ui_lists();
    }
}

void RmlSmallsBridge::clear_active_object(nw::ObjectHandle object)
{
    const auto previous = smalls_rmlui_host().active_object();
    smalls_rmlui_host().clear_active_object(object);
    if (previous != smalls_rmlui_host().active_object()) {
        refresh_ui_lists();
    }
}

nw::ObjectHandle RmlSmallsBridge::active_object() const noexcept
{
    return smalls_rmlui_host().active_object();
}

void RmlSmallsBridge::publish_active_area(nw::ObjectHandle area)
{
    if (!ensure_current_runtime()) {
        return;
    }
    active_area_ = area;
}

void RmlSmallsBridge::clear_active_area() noexcept
{
    active_area_ = nw::ObjectHandle{};
}

nw::ObjectHandle RmlSmallsBridge::active_area() const noexcept
{
    return nw::kernel::objects().valid(active_area_) ? active_area_ : nw::ObjectHandle{};
}

void RmlSmallsBridge::register_event_handler(std::string event_key, std::string function_name)
{
    event_handlers_[std::move(event_key)] = std::move(function_name);
}

std::string RmlSmallsBridge::dispatch_event(std::string_view event_key, const std::vector<std::string_view>& args)
{
    auto it = event_handlers_.find(std::string(event_key));
    if (it == event_handlers_.end()) {
        std::vector<std::string_view> fallback_args;
        fallback_args.reserve(args.size() + 1);
        fallback_args.push_back(event_key);
        fallback_args.insert(fallback_args.end(), args.begin(), args.end());
        return execute_handler(module_path_, ui_contract::event::fallback_ui_event, fallback_args).message;
    }

    return execute_handler(module_path_, it->second, args).message;
}

std::string RmlSmallsBridge::call(std::string_view module_path, std::string_view function_name, const std::vector<std::string_view>& args)
{
    return execute_handler(module_path, function_name, args).message;
}

SmallsInvocationResult RmlSmallsBridge::invoke(
    std::string_view module_path, std::string_view function_name, const std::vector<std::string_view>& args)
{
    return execute_handler(module_path, function_name, args);
}

SmallsInvocationResult RmlSmallsBridge::call_ui_list_callback(
    std::string_view qualified_function, const UiListEvent& event)
{
    if (!ensure_current_runtime()) {
        return {false, "Smalls bridge not initialized"};
    }

    auto& rt = nw::kernel::runtime();
    nw::Vector<nw::smalls::Value> args;
    args.reserve(1);

    if (event.type == UiListEventType::scroll) {
        args.push_back(make_scroll_value(rt, event.scroll));
    } else {
        args.push_back(make_selection_value(rt, event.selection));
    }

    const size_t separator = qualified_function.rfind('.');
    if (separator == std::string_view::npos || separator == 0
        || separator + 1 >= qualified_function.size()) {
        return {false, "Invalid qualified Smalls list callback"};
    }
    const auto result = rt.execute_script(
        qualified_function.substr(0, separator),
        qualified_function.substr(separator + 1), args);
    if (!result.ok()) {
        return {false, format_smalls_execution_error(result)};
    }

    if (result.value.type_id == rt.string_type() && result.value.data.hptr.value != 0) {
        return {true, std::string(rt.get_string_view(result.value.data.hptr))};
    }
    return {true, {}};
}

void RmlSmallsBridge::refresh_ui_lists()
{
    if (!ensure_current_runtime()) {
        return;
    }
    const std::vector<std::string> callbacks{
        ui_v1_host().refresh_callbacks().begin(),
        ui_v1_host().refresh_callbacks().end()};
    for (const auto& qualified : callbacks) {
        const size_t separator = qualified.rfind('.');
        if (separator == std::string::npos || separator == 0
            || separator + 1 >= qualified.size()) {
            LOG_F(ERROR, "[rml-smalls] invalid managed-list refresh callback '{}'", qualified);
            continue;
        }
        const auto result = nw::kernel::runtime().execute_script(
            std::string_view{qualified}.substr(0, separator),
            std::string_view{qualified}.substr(separator + 1), {});
        if (!result.ok()) {
            LOG_F(ERROR, "[rml-smalls] managed-list refresh '{}' failed: {}",
                qualified, format_smalls_execution_error(result));
        }
    }
}

SmallsInvocationResult RmlSmallsBridge::execute_handler(
    std::string_view module_path,
    std::string_view function_name,
    const std::vector<std::string_view>& script_args)
{
    if (!ensure_current_runtime()) {
        return {false, "Smalls bridge not initialized"};
    }

    auto& rt = nw::kernel::runtime();
    nw::Vector<nw::smalls::Value> args;
    args.reserve(static_cast<int64_t>(script_args.size()));
    for (const auto value : script_args) {
        args.push_back(nw::smalls::Value::make_string(rt.alloc_string(value)));
    }

    const auto result = rt.execute_script(module_path, function_name, args);
    if (!result.ok()) {
        return {false, format_smalls_execution_error(result)};
    }

    if (result.value.type_id == rt.string_type() && result.value.data.hptr.value != 0) {
        return {true, std::string(rt.get_string_view(result.value.data.hptr))};
    }

    return {true, "Smalls handler completed"};
}

} // namespace nw::toolset
