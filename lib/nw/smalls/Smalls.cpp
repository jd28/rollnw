#include "Smalls.hpp"

#include "AstHinter.hpp"
#include "AstLocator.hpp"
#include "AstResolver.hpp"
#include "runtime.hpp"

#include <fmt/format.h>
#include <new>

extern "C" {
#include <fzy/match.h>
}

#include <string_view>

namespace nw::smalls {

bool Script::semantic_tooling_enabled() const noexcept
{
    return ctx_ && ctx_->config.debug_level == DebugLevel::full;
}

bool Script::emit_tooling_disabled_diagnostic(StringView api_name) const
{
    if (semantic_tooling_enabled()) { return false; }
    if (ctx_ && !tooling_disabled_diagnostic_emitted_) {
        String msg = fmt::format(
            "semantic tooling requires DebugLevel::full (called '{}')",
            api_name);
        ctx_->semantic_diagnostic(const_cast<Script*>(this), msg, true, {});
        tooling_disabled_diagnostic_emitted_ = true;
    }
    return true;
}

namespace {

bool has_native_annotation(const Declaration* decl)
{
    if (!decl) { return false; }
    for (const auto& ann : decl->annotations_) {
        if (ann.name.loc.view() == "native") { return true; }
    }
    return false;
}

std::optional<size_t> generic_param_index(const Vector<String>& params, StringView name)
{
    for (size_t i = 0; i < params.size(); ++i) {
        if (params[i] == name) {
            return i;
        }
    }
    return std::nullopt;
}

String type_name_expr(Expression* expr)
{
    if (!expr) {
        return {};
    }

    if (auto* ident = dynamic_cast<IdentifierExpression*>(expr)) {
        return String(ident->ident.loc.view());
    }

    if (auto* path = dynamic_cast<PathExpression*>(expr)) {
        String out;
        for (size_t i = 0; i < path->parts.size(); ++i) {
            auto* part_ident = dynamic_cast<IdentifierExpression*>(path->parts[i]);
            if (!part_ident) {
                return {};
            }
            if (i != 0) {
                out += ".";
            }
            out += String(part_ident->ident.loc.view());
        }
        return out;
    }

    return {};
}

NormalizedTypeExpr normalize_type_expr(TypeExpression* expr, const Vector<String>& generic_params)
{
    NormalizedTypeExpr out;
    if (!expr) {
        return out;
    }

    if (!expr->params.empty()) {
        out.kind = NormalizedTypeExpr::Kind::applied;
        out.applied_name = type_name_expr(expr->name);
        out.concrete_type = expr->type_id_;
        out.applied_args.reserve(expr->params.size());
        bool has_generic_arg = false;
        for (auto* arg : expr->params) {
            auto* arg_type = dynamic_cast<TypeExpression*>(arg);
            auto arg_norm = normalize_type_expr(arg_type, generic_params);
            if (arg_norm.kind == NormalizedTypeExpr::Kind::generic_param
                || arg_norm.kind == NormalizedTypeExpr::Kind::applied) {
                has_generic_arg = true;
            }
            out.applied_args.push_back(std::move(arg_norm));
        }

        if (!has_generic_arg && expr->type_id_ != invalid_type_id) {
            out.kind = NormalizedTypeExpr::Kind::concrete;
            out.applied_name.clear();
            out.applied_args.clear();
        }
        return out;
    }

    if (expr->name) {
        String base_name = type_name_expr(expr->name);
        if (!base_name.empty()) {
            if (auto idx = generic_param_index(generic_params, base_name)) {
                out.kind = NormalizedTypeExpr::Kind::generic_param;
                out.generic_param_index = static_cast<uint16_t>(*idx);
                return out;
            }
        }
    }

    if (expr->type_id_ != invalid_type_id) {
        out.kind = NormalizedTypeExpr::Kind::concrete;
        out.concrete_type = expr->type_id_;
        return out;
    }

    out.kind = NormalizedTypeExpr::Kind::applied;
    out.applied_name = type_name_expr(expr->name);
    out.concrete_type = expr->type_id_;
    return out;
}

std::optional<FunctionExportAbi> build_function_abi(const FunctionDefinition* fn, StringView provider_module)
{
    if (!fn) {
        return std::nullopt;
    }

    FunctionExportAbi abi;
    abi.max_arity = static_cast<uint16_t>(fn->params.size());
    abi.min_arity = 0;
    abi.generic_arity = static_cast<uint16_t>(fn->type_params.size());
    abi.param_types.reserve(fn->params.size());
    abi.param_has_default.reserve(fn->params.size());

    for (auto* param : fn->params) {
        bool has_default = param && param->init;
        if (!has_default) {
            ++abi.min_arity;
        }
        if (param && param->type) {
            abi.param_types.push_back(normalize_type_expr(param->type, fn->type_params));
        } else {
            NormalizedTypeExpr unknown;
            unknown.kind = NormalizedTypeExpr::Kind::unknown;
            abi.param_types.push_back(std::move(unknown));
        }
        abi.param_has_default.push_back(has_default ? 1 : 0);
    }

    if (fn->return_type) {
        abi.return_type = normalize_type_expr(fn->return_type, fn->type_params);
    } else {
        abi.return_type.kind = NormalizedTypeExpr::Kind::concrete;
        abi.return_type.concrete_type = fn->type_id_;
    }

    abi.call_target = fmt::format("{}.{}", provider_module, fn->identifier());
    return abi;
}

} // namespace

Script::Script(const std::filesystem::path& filename, Context* ctx)
    : ctx_{ctx}
    , data_{ResourceData::from_file(filename)}
    , text_{data_.bytes.string_view()}
    , ast_(ctx_)
{
    CHECK_F(!!ctx_, "[script] invalid script context");
    data_.name = Resource::from_path(filename, true);
}

Script::Script(StringView name, StringView script, Context* ctx)
    : ctx_{ctx}
    , text_{script}
    , ast_(ctx_)
{
    CHECK_F(!!ctx_, "[script] invalid script context");
    CHECK_F(!name.empty(), "[script] script name cannot be empty");

    String module_name(name);
    for (char& c : module_name) {
        if (c == '/' || c == '\\') {
            c = '.';
        }
    }
    data_.name = Resource(std::move(module_name), ResourceType::smalls);
}

Script::Script(ResourceData data, Context* ctx)
    : ctx_{ctx}
    , data_{std::move(data)}
    , text_{data_.bytes.string_view()}
    , ast_(ctx_)
{
    CHECK_F(!!ctx_, "[script] invalid script context");

    String module_name(data_.name.resref.view());
    for (char& c : module_name) {
        if (c == '/' || c == '\\') {
            c = '.';
        }
    }
    data_.name.resref = Resref(module_name);
}

void Script::add_diagnostic(Diagnostic diagnostic)
{
    diagnostics_.push_back(std::move(diagnostic));
}

void Script::complete(const String& needle, CompletionContext& out, bool no_filter) const
{
    if (emit_tooling_disabled_diagnostic("complete")) {
        return;
    }
    for (const auto& [name, exp] : symbol_table_) {
        if (no_filter || has_match(needle.c_str(), name.c_str())) {
            out.add(export_to_symbol(name, exp));
        }
    }
}

void Script::complete_at(const String& needle, size_t line, size_t character, CompletionContext& out, bool no_filter)
{
    if (emit_tooling_disabled_diagnostic("complete_at")) {
        return;
    }
    AstLocator locator{this, needle, line, character};
    locator.visit(&ast_);
    auto node = locator.last_seen_decl;

    if (locator.path && locator.path->parts.size() >= 2) {
        auto* base_expr = locator.path->parts[locator.path->parts.size() - 2];
        String struct_name(nw::kernel::runtime().type_name(base_expr->type_id_));
        const StructDecl* struct_decl = nullptr;
        const Script* provider = this;
        auto exp = locator.path->env_.find(struct_name);
        if (exp && exp->decl) {
            struct_decl = dynamic_cast<const StructDecl*>(exp->decl);
        } else {
            auto symbol = locate_export(struct_name, true);
            provider = symbol.provider;
            struct_decl = dynamic_cast<const StructDecl*>(symbol.decl);
        }

        if (struct_decl) {
            for (auto decl : struct_decl->decls) {
                if (auto vdl = dynamic_cast<const DeclList*>(decl)) {
                    for (auto vd : vdl->decls) {
                        out.add(provider->declaration_to_symbol(vd));
                    }
                } else {
                    out.add(provider->declaration_to_symbol(decl));
                }
            }
        }
        return;
    } else if (locator.brace_init) {
        auto& rt = nw::kernel::runtime();
        const Type* type = rt.get_type(locator.brace_init->type_id_);
        if (type && type->type_kind == TK_struct) {
            auto struct_id = type->type_params[0].as<StructID>();
            const StructDef* struct_def = rt.type_table_.get(struct_id);
            if (struct_def && struct_def->decl) {
                for (auto decl : struct_def->decl->decls) {
                    if (auto vdl = dynamic_cast<const DeclList*>(decl)) {
                        for (auto vd : vdl->decls) {
                            auto sym = declaration_to_symbol(vd);
                            sym.kind = SymbolKind::field;
                            out.add(sym);
                        }
                    } else {
                        auto sym = declaration_to_symbol(decl);
                        sym.kind = SymbolKind::field;
                        out.add(sym);
                    }
                }
            }
        }
    }

    if (node) {
        Vector<const Declaration*> decls;
        node->complete(needle, decls, no_filter);
        for (auto decl : decls) {
            out.add(declaration_to_symbol(decl));
        }

        // If the last seen declaration is a selective import, it's necessary to grab the imported symbols,
        // because they otherwise wouldn't be available.
        if (auto selective_import = dynamic_cast<const SelectiveImportDecl*>(node)) {
            if (selective_import->loaded_module) {
                auto module_exports = selective_import->loaded_module->exports();
                for (const auto& symbol_token : selective_import->imported_symbols) {
                    auto symbol_name = String(symbol_token.loc.view());
                    if (no_filter || has_match(needle.c_str(), symbol_name.c_str())) {
                        auto export_ptr = module_exports.find(symbol_name);
                        if (export_ptr && export_ptr->decl) {
                            out.add(selective_import->loaded_module->declaration_to_symbol(export_ptr->decl));
                        }
                    }
                }
            }
        }

        if (node->range_.start.line < line) { // Skip if on the same line
            if (auto d = dynamic_cast<const DeclList*>(node)) {
                for (const auto decl : d->decls) {
                    if (no_filter || has_match(needle.c_str(), decl->identifier().data())) {
                        out.add(declaration_to_symbol(decl));
                    }
                }
            } else {
                if (no_filter || has_match(needle.c_str(), node->identifier().data())) {
                    out.add(declaration_to_symbol(node));
                }
            }
        }
    }
}

void Script::complete_dot(const String& needle, size_t line, size_t character, Vector<Symbol>& out,
    bool)
{
    if (emit_tooling_disabled_diagnostic("complete_dot")) {
        return;
    }
    AstLocator locator{this, needle, line, character};
    locator.visit(&ast_);

    auto node = locator.last_seen_decl;
    if (!node) {
        LOG_F(INFO, "Failed to find node");
        return;
    }

    const Declaration* decl;
    if (node->identifier() == needle) {
        decl = node;
    } else {
        auto exp = node->env_.find(needle);
        if (!exp || !exp->decl) {
            LOG_F(INFO, "Failed to find needle declaration: {}", needle);
            return;
        }
        decl = exp->decl;
    }

    // Check if this is a module import - if so, return all module exports
    if (auto import_decl = dynamic_cast<const AliasedImportDecl*>(decl)) {
        if (import_decl->loaded_module) {
            for (const auto& [name, exp] : import_decl->loaded_module->exports()) {
                if (exp.decl) {
                    out.push_back(import_decl->loaded_module->declaration_to_symbol(exp.decl));
                }
            }
        }
        return;
    }

    // Otherwise, handle struct field completions
    if (!decl->type) { return; }
    String struct_id(decl->type->str());
    const StructDecl* struct_decl = nullptr;
    const Script* provider = this;

    auto exp2 = node->env_.find(struct_id);
    if (exp2 && exp2->decl) {
        struct_decl = dynamic_cast<const StructDecl*>(exp2->decl);
    }

    if (!struct_decl) {
        auto symbol = locate_export(struct_id, true);
        provider = symbol.provider;
        struct_decl = dynamic_cast<const StructDecl*>(symbol.decl);
    }

    if (struct_decl) {
        for (auto decl : struct_decl->decls) {
            if (auto vdl = dynamic_cast<const DeclList*>(decl)) {
                for (auto vd : vdl->decls) {
                    out.push_back(provider->declaration_to_symbol(vd));
                }
            } else {
                out.push_back(provider->declaration_to_symbol(decl));
            }
        }
    }
}

Context* Script::ctx() const
{
    return ctx_;
}

Symbol Script::declaration_to_symbol(const Declaration* decl) const
{
    Symbol result;
    result.decl = decl;
    result.comment = ast().find_comment(decl->range_.start.line);
    result.type = nw::kernel::runtime().type_name(decl->type_id_);
    result.provider = this;
    result.view = view_from_range(decl->range_);

    if (dynamic_cast<const StructDecl*>(decl)
        || dynamic_cast<const TypeAlias*>(decl)
        || dynamic_cast<const NewtypeDecl*>(decl)) {
        result.kind = SymbolKind::type;
    } else if (auto fd = dynamic_cast<const FunctionDefinition*>(decl)) {
        result.kind = SymbolKind::function;
        result.view = view_from_range(fd->range_selection_);
    } else {
        result.kind = SymbolKind::variable;
    }

    return result;
}

Symbol Script::export_to_symbol(StringView name, const Export& exp) const
{
    if (exp.decl) {
        return declaration_to_symbol(exp.decl);
    }

    Symbol result;
    result.decl = nullptr;
    result.provider = this;
    result.view = name;
    if (exp.type_id != invalid_type_id) {
        result.type = nw::kernel::runtime().type_name(exp.type_id);
    }

    switch (exp.kind) {
    case Export::Kind::function:
        result.kind = SymbolKind::function;
        if (result.type.empty()) {
            result.type = "<function>";
        }
        break;
    case Export::Kind::type:
        result.kind = SymbolKind::type;
        if (result.type.empty()) {
            result.type = "<type>";
        }
        break;
    case Export::Kind::module_alias:
        result.kind = SymbolKind::variable;
        result.type = "module";
        break;
    case Export::Kind::variable:
    case Export::Kind::unknown:
        result.kind = SymbolKind::variable;
        result.type = "<unknown>";
        break;
    }

    return result;
}

Vector<String> Script::dependencies() const
{
    return dependency_paths_;
}

const Vector<Diagnostic>& Script::diagnostics() const noexcept
{
    return diagnostics_;
}

Vector<InlayHint> Script::inlay_hints(SourceRange range)
{
    if (emit_tooling_disabled_diagnostic("inlay_hints")) {
        return {};
    }
    AstHinter hinter{this, range};
    hinter.visit(&ast_);
    return hinter.result_;
}

Symbol Script::locate_export(const String& symbol, bool search_dependencies) const
{
    auto sym = symbol_table_.find(symbol);

    Symbol result;

    if (sym) {
        result = export_to_symbol(symbol, *sym);
    }

    return result;
}

Symbol Script::locate_symbol(const String& symbol, size_t line, size_t character)
{
    if (emit_tooling_disabled_diagnostic("locate_symbol")) {
        return {};
    }
    AstLocator locator{this, symbol, line, character};
    locator.visit(&ast_);
    return locator.result_;
}

StringView Script::name() const noexcept
{
    return data_.name.resref.view();
}

void Script::parse()
{
    if (parsed_) { return; }
    try {
        Parser parser{text_, ctx_, this};
        ast_ = parser.parse_program();
        dependency_paths_.clear();
        dependency_paths_.reserve(ast_.imports.size());
        for (const auto* import : ast_.imports) {
            if (import) {
                dependency_paths_.push_back(import->module_path);
            }
        }
        ast_discarded_ = false;
    } catch (const std::bad_alloc&) {
        if (ctx_) {
            ctx_->parse_diagnostic(this, "out of memory while parsing", false, {});
        }
    }
    parsed_ = true;
}

void Script::resolve()
{
    if (resolved_) { return; }
    if (ast_discarded_ && text_.empty()) {
        return;
    }
    try {
        parse();

        AstResolver resolver{this, ctx_};
        resolver.resolve(&ast_);
        symbol_table_ = resolver.symbol_table();

        immer::map<String, Export> enriched;
        for (const auto& [name, exp] : symbol_table_) {
            Export updated = exp;
            if (updated.provider_module.empty()) {
                updated.provider_module = String(this->name());
            }
            if (updated.decl) {
                updated.type_id = updated.decl->type_id_;
                updated.is_native = has_native_annotation(updated.decl);
            }

            if (auto* fn = dynamic_cast<const FunctionDefinition*>(updated.decl)) {
                updated.kind = Export::Kind::function;
                updated.is_generic = fn->is_generic();
                updated.has_defaults = false;
                for (auto* param : fn->params) {
                    if (param && param->init) {
                        updated.has_defaults = true;
                        break;
                    }
                }
                updated.function_abi = build_function_abi(fn, updated.provider_module);
            }

            enriched = enriched.set(name, std::move(updated));
        }
        symbol_table_ = std::move(enriched);
    } catch (const std::bad_alloc&) {
        if (ctx_) {
            ctx_->semantic_diagnostic(this, "out of memory while resolving", false, {});
        }
    }
    resolved_ = true;
}

Ast& Script::ast() { return ast_; }
const Ast& Script::ast() const { return ast_; }

void Script::set_name(const String& new_name)
{
    data_.name = Resource(new_name, ResourceType::smalls);
}

SignatureHelp Script::signature_help(size_t line, size_t character)
{
    SignatureHelp result;
    if (emit_tooling_disabled_diagnostic("signature_help")) {
        return result;
    }
    AstLocator locator{this, "", line, character};
    locator.visit(&ast_);
    if (!locator.call) { return result; }

    if (auto path = dynamic_cast<PathExpression*>(locator.call->expr)) {
        if (path->parts.size() == 1) {
            if (auto ve = dynamic_cast<IdentifierExpression*>(path->parts.front())) {
                String name(ve->ident.loc.view());
                result.expr = locator.call;
                result.active_param = locator.active_param;
                auto exp = result.expr->env_.find(name);
                if (exp && exp->decl) {
                    result.decl = exp->decl;
                } else {
                    auto sym = locate_export(name, true);
                    result.decl = sym.decl;
                }
            }
        }
    }

    return result;
}

StringView Script::text() const noexcept { return text_; }

void Script::discard_source()
{
    data_.bytes.clear();
    text_ = {};
}

void Script::discard_ast()
{
    immer::map<String, Export> stripped_symbols;
    for (const auto& [name, exp] : symbol_table_) {
        Export stripped = exp;
        stripped.decl = nullptr;
        stripped_symbols = stripped_symbols.set(name, stripped);
    }
    symbol_table_ = std::move(stripped_symbols);
    ast_ = Ast(ctx_);
    parsed_ = false;
    resolved_ = false;
    includes_processed_ = false;
    ast_discarded_ = true;
}

bool Script::can_discard_ast() const
{
    return !ast_discarded_;
}

bool Script::has_generic_templates() const
{
    for (const auto* node : ast_.decls) {
        if (const auto* fn = dynamic_cast<const FunctionDefinition*>(node)) {
            if (fn->is_generic()) {
                return true;
            }
            continue;
        }
        if (const auto* sd = dynamic_cast<const StructDecl*>(node)) {
            if (!sd->type_params.empty()) {
                return true;
            }
            continue;
        }
        if (const auto* sum = dynamic_cast<const SumDecl*>(node)) {
            if (!sum->type_params.empty()) {
                return true;
            }
            continue;
        }
        if (const auto* alias = dynamic_cast<const TypeAlias*>(node)) {
            if (!alias->type_params.empty()) {
                return true;
            }
        }
    }
    return false;
}

StringView Script::view_from_range(SourceRange range) const noexcept
{
    if (range.start.line == 0 || range.end.line == 0) {
        return {};
    }

    const auto& lm = ast().line_map;
    if (range.start.line > lm.size() || range.end.line > lm.size()) {
        return {};
    }

    size_t start = lm[range.start.line - 1] + range.start.column;
    size_t end = lm[range.end.line - 1] + range.end.column;
    if (start > text_.size() || end > text_.size() || end < start) {
        return {};
    }
    return StringView(text_.data() + start, end - start);
}

} // namespace nw::smalls
