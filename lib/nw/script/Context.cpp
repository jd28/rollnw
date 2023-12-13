#include "Context.hpp"

#include "../kernel/Resources.hpp"
#include "Nss.hpp"

#include <fmt/format.h>

namespace nw::script {

Context::Context(std::string command_script)
    : dependencies_{}
    , command_script_name_{std::move(command_script)}
    , type_map_{}
{
    register_default_types();

    command_script_ = get(Resref{command_script_name_}, true);
    CHECK_F(!!command_script_, "[script] unable to load command script '{}'", command_script_name_);
    register_engine_types(); // Must come before resolving command script!
    command_script_->resolve();
}

Nss* Context::get(Resref resref, bool command_script)
{
    Resource res{resref, ResourceType::nss};
    auto it = dependencies_.find(res);
    if (it != std::end(dependencies_)) {
        return it->second.get();
    }

    auto data = nw::kernel::resman().demand(res);
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

size_t Context::type_id(std::string_view type_name, bool define)
{
    absl::string_view tn{type_name.data(), type_name.size()};
    auto it = type_map_.find(tn);
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

std::string_view Context::type_name(size_t type_id)
{
    if (type_id >= type_array_.size()) { return "<unknown>"; }
    return type_array_[type_id];
}

bool Context::type_check_binary_op(NssToken op, size_t lhs, size_t rhs)
{
    size_t int_ = type_id("int");
    size_t float_ = type_id("float");
    size_t string_ = type_id("string");
    size_t vector_ = type_id("vector");

    bool is_good = false;
    bool is_eq = false;

    switch (op.type) {
    default:
        return false;

    case NssTokenType::EQ:
        return is_type_convertible(lhs, rhs);
    case NssTokenType::MODEQ:
    case NssTokenType::MOD:
        return lhs == int_ && rhs == int_;
    case NssTokenType::PLUS:
    case NssTokenType::PLUSEQ:
        is_eq = op.type == NssTokenType::PLUSEQ;
        if ((lhs == int_ || lhs == float_) && (rhs == int_ || rhs == float_)) {
            is_good = true;
        } else if (lhs == vector_ && rhs == vector_) {
            is_good = true;
        } else if (lhs == string_ && rhs == string_) {
            is_good = true;
        } else if ((lhs == vector_ || lhs == float_) && (rhs == vector_ || rhs == float_)) {
            // x3_inc_horse
            is_good = true;
        }
        break;
    case NssTokenType::MINUS:
    case NssTokenType::MINUSEQ:
        is_eq = op.type == NssTokenType::MINUSEQ;
        if ((lhs == int_ || lhs == float_) && (rhs == int_ || rhs == float_)) {
            is_good = true;
        } else if (lhs == vector_ && rhs == vector_) {
            is_good = true;
        }
        break;
    case NssTokenType::TIMES:
    case NssTokenType::TIMESEQ:
        is_eq = op.type == NssTokenType::TIMESEQ;
        if ((lhs == int_ || lhs == float_) && (rhs == int_ || rhs == float_)) {
            is_good = true;
        } else if ((lhs == vector_ || lhs == float_) && (rhs == vector_ || rhs == float_)) {
            is_good = true;
        }
        break;
    case NssTokenType::DIV:
    case NssTokenType::DIVEQ:
        is_eq = op.type == NssTokenType::DIVEQ;
        if ((lhs == int_ || lhs == float_) && (rhs == int_ || rhs == float_)) {
            is_good = true;
        } else if (lhs == vector_ && rhs == float_) {
            is_good = true;
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
        return lhs == int_ && rhs == int_;
    }

    return is_eq ? (is_good && is_type_convertible(lhs, rhs)) : is_good;
}

bool Context::is_type_convertible(size_t lhs, size_t rhs)
{
    size_t int_ = type_id("int");
    size_t float_ = type_id("float");

    return lhs == rhs || (lhs == float_ && rhs == int_);
}

void Context::lexical_diagnostic(Nss* script, std::string_view msg, bool is_warning, SourceRange range)
{
    if (script) {
        Diagnostic result;
        result.type = DiagnosticType::lexical;
        result.level = is_warning ? DiagnosticLevel::warning : DiagnosticLevel::error;
        result.script = script ? script->name() : "<source>";
        result.message = std::string(msg);
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

void Context::parse_diagnostic(Nss* script, std::string_view msg, bool is_warning, SourceRange range)
{
    if (script) {
        Diagnostic result;
        result.type = DiagnosticType::parse;
        result.level = is_warning ? DiagnosticLevel::warning : DiagnosticLevel::error;
        result.script = script ? script->name() : "<source>";
        result.message = std::string(msg);
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

void Context::semantic_diagnostic(Nss* script, std::string_view msg, bool is_warning, SourceRange range)
{
    if (script) {
        Diagnostic result;
        result.type = DiagnosticType::semantic;
        result.level = is_warning ? DiagnosticLevel::warning : DiagnosticLevel::error;
        result.script = script ? script->name() : "<source>";
        result.message = std::string(msg);
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
