#include "Context.hpp"

#include "../kernel/Resources.hpp"
#include "Nss.hpp"

#include <fmt/format.h>

namespace fs = std::filesystem;

namespace nw::script {

Context::Context(Vector<String> include_paths, String command_script)
    : include_paths_{std::move(include_paths)}
    , dependencies_{}
    , command_script_name_{std::move(command_script)}
    , resman_{nw::kernel::global_allocator(), &kernel::resman()}
    , type_map_{}
{
    register_default_types();

    for (const auto& path : include_paths_) {
        if (!fs::exists(path) || !fs::is_directory(path)) { continue; }
        resman_.add_custom_container(new Directory{path});
    }

    command_script_ = get(Resref{command_script_name_}, true);
    CHECK_F(!!command_script_, "[script] unable to load command script '{}'", command_script_name_);
    register_engine_types(); // Must come before resolving command script!
    command_script_->resolve();
}

void Context::add_include_path(const std::filesystem::path& path)
{
    if (!fs::exists(path) || !fs::is_directory(path)) { return; }
    resman_.add_custom_container(new Directory{path});
}

Nss* Context::get(Resref resref, bool command_script)
{
    Resource res{resref, ResourceType::nss};
    auto it = dependencies_.find(res);
    if (it != std::end(dependencies_)) {
        return it->second.get();
    }

    auto data = resman_.demand(res);
    if (data.bytes.size()) {
        auto nss = std::make_unique<Nss>(std::move(data), this, command_script);
        nss->parse();
        auto it = dependencies_.insert({res, std::move(nss)});
        return it.first->second.get();
    }

    return nullptr;
}

void Context::register_default_types()
{
    // Basic Types
    type_id("action", true);
    type_id("float", true);
    type_id("int", true);
    type_id("object", true);
    type_id("string", true);
    type_id("vector", true);
    type_id("void", true);
}

void Context::register_engine_types()
{
    auto it = command_script_->ast().defines.find("ENGINE_NUM_STRUCTURES");
    if (it == std::end(command_script_->ast().defines)) { return; }

    auto num = string::from<size_t>(it->second);
    if (!num) { return; }

    size_t count = *num;

    for (size_t i = 0; i < count; ++i) {
        auto lookup = fmt::format("ENGINE_STRUCTURE_{}", i);
        auto it = command_script_->ast().defines.find(lookup);
        if (it != std::end(command_script_->ast().defines)) {
            type_id(it->second, true);
        }
    }
}

size_t Context::type_id(StringView type_name, bool define)
{
    auto it = type_map_.find(type_name);
    if (it == std::end(type_map_)) {
        if (!define) { return invalid_type_id; }
        size_t new_id = type_array_.size();
        type_array_.emplace_back(type_name);
        type_map_.emplace(type_name, new_id);
        return new_id;
    } else {
        return it->second;
    }
}

size_t Context::type_id(Type type_name, bool define)
{
    if (type_name.type_specifier.type == NssTokenType::STRUCT) {
        return type_id(type_name.struct_id.loc.view(), define);
    } else {
        return type_id(type_name.type_specifier.loc.view(), define);
    }
}

StringView Context::type_name(size_t type_id)
{
    if (type_id >= type_array_.size()) { return "<unknown>"; }
    return type_array_[type_id];
}

size_t Context::type_check_binary_op(NssToken op, size_t lhs, size_t rhs)
{
    size_t int_ = type_id("int");
    size_t float_ = type_id("float");
    size_t string_ = type_id("string");
    size_t vector_ = type_id("vector");

    size_t is_good = invalid_type_id;
    bool is_eq = false;

    switch (op.type) {
    default:
        return invalid_type_id;

    case NssTokenType::EQ:
        if (is_type_convertible(lhs, rhs)) {
            return lhs;
        }
    case NssTokenType::MODEQ:
    case NssTokenType::MOD:
        if (lhs == int_ && rhs == int_) {
            return int_;
        }
    case NssTokenType::PLUS:
    case NssTokenType::PLUSEQ:
        is_eq = op.type == NssTokenType::PLUSEQ;
        if ((lhs == int_ || lhs == float_) && (rhs == int_ || rhs == float_)) {
            if (lhs == float_ || rhs == float_) {
                is_good = float_;
            } else {
                is_good = int_;
            }
        } else if (lhs == vector_ && rhs == vector_) {
            is_good = vector_;
        } else if (lhs == string_ && rhs == string_) {
            is_good = string_;
        }
        break;
    case NssTokenType::MINUS:
    case NssTokenType::MINUSEQ:
        is_eq = op.type == NssTokenType::MINUSEQ;
        if ((lhs == int_ || lhs == float_) && (rhs == int_ || rhs == float_)) {
            if (lhs == float_ || rhs == float_) {
                is_good = float_;
            } else {
                is_good = int_;
            }
        } else if (lhs == vector_ && rhs == vector_) {
            is_good = vector_;
        }
        break;
    case NssTokenType::TIMES:
    case NssTokenType::TIMESEQ:
        is_eq = op.type == NssTokenType::TIMESEQ;
        if ((lhs == int_ || lhs == float_) && (rhs == int_ || rhs == float_)) {
            if (lhs == float_ || rhs == float_) {
                is_good = float_;
            } else {
                is_good = int_;
            }
        } else if ((lhs == vector_ || lhs == float_) && (rhs == vector_ || rhs == float_)) {
            if (lhs == vector_ || rhs == vector_) {
                is_good = vector_;
            } else {
                is_good = float_;
            }
        }
        break;
    case NssTokenType::DIV:
    case NssTokenType::DIVEQ:
        is_eq = op.type == NssTokenType::DIVEQ;
        if ((lhs == int_ || lhs == float_) && (rhs == int_ || rhs == float_)) {
            if (lhs == float_ || rhs == float_) {
                is_good = float_;
            } else {
                is_good = int_;
            }
        } else if (lhs == vector_ && rhs == float_) {
            is_good = vector_;
        }
        break;
    case NssTokenType::SL:
    case NssTokenType::SR:
    case NssTokenType::USR:
    case NssTokenType::SLEQ:
    case NssTokenType::SREQ:
    case NssTokenType::USREQ:
    case NssTokenType::OR:
    case NssTokenType::OREQ:
    case NssTokenType::AND:
    case NssTokenType::ANDEQ:
    case NssTokenType::XOR:
    case NssTokenType::XOREQ:
        if (lhs == int_ && rhs == int_) {
            return int_;
        }
    }

    if (is_eq) {
        if (is_type_convertible(lhs, rhs) && is_good != invalid_type_id) {
            return lhs;
        } else {
            return invalid_type_id;
        }
    } else {
        return is_good;
    }
}

bool Context::is_type_convertible(size_t lhs, size_t rhs)
{
    size_t int_ = type_id("int");
    size_t float_ = type_id("float");

    return lhs == rhs || (lhs == float_ && rhs == int_);
}

void Context::lexical_diagnostic(Nss* script, StringView msg, bool is_warning, SourceRange range)
{
    if (script) {
        Diagnostic result;
        result.type = DiagnosticType::lexical;
        result.severity = is_warning ? DiagnosticSeverity::warning : DiagnosticSeverity::error;
        result.script = script ? script->name() : "<source>";
        result.message = String(msg);
        result.location = range;
        script->add_diagnostic(std::move(result));

        if (is_warning) {
            script->increment_warnings();
        } else {
            script->increment_errors();
        }
    }

    auto out = fmt::format("{}:{}:{} {}: {}", script ? script->name() : "<source>",
        range.start.line, range.start.column, is_warning ? "warning" : "error", msg);
    if (is_warning) {
        LOG_F(WARNING, "{}", out);
    } else {
        LOG_F(ERROR, "{}", out);
    }
}

void Context::parse_diagnostic(Nss* script, StringView msg, bool is_warning, SourceRange range)
{
    if (script) {
        Diagnostic result;
        result.type = DiagnosticType::parse;
        result.severity = is_warning ? DiagnosticSeverity::warning : DiagnosticSeverity::error;
        result.script = script ? script->name() : "<source>";
        result.message = String(msg);
        result.location = range;
        script->add_diagnostic(std::move(result));

        if (is_warning) {
            script->increment_warnings();
        } else {
            script->increment_errors();
        }
    }

    auto out = fmt::format("{}:{}:{} {}: {}", script ? script->name() : "<source>",
        range.start.line, range.start.column, is_warning ? "warning" : "error", msg);
    if (is_warning) {
        LOG_F(WARNING, "{}", out);
    } else {
        LOG_F(ERROR, "{}", out);
    }
}

void Context::semantic_diagnostic(Nss* script, StringView msg, bool is_warning, SourceRange range)
{
    if (script) {
        Diagnostic result;
        result.type = DiagnosticType::semantic;
        result.severity = is_warning ? DiagnosticSeverity::warning : DiagnosticSeverity::error;
        result.script = script ? script->name() : "<source>";
        result.message = String(msg);
        result.location = range;
        script->add_diagnostic(std::move(result));

        if (is_warning) {
            script->increment_warnings();
        } else {
            script->increment_errors();
        }
    }

    auto out = fmt::format("{}:{}:{} {}: {}", script ? script->name() : "<source>",
        range.start.line, range.start.column, is_warning ? "warning" : "error", msg);
    if (is_warning) {
        LOG_F(WARNING, "{}", out);
    } else {
        LOG_F(ERROR, "{}", out);
    }
}

} // nw::script
