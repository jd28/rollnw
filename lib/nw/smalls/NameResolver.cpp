#include "NameResolver.hpp"

namespace nw::smalls {

NameResolver::NameResolver(AstResolver& ctx)
    : ctx(ctx)
{
}

void NameResolver::visit(Ast* script)
{
    for (const auto& decl : script->decls) {
        if (auto import_decl = dynamic_cast<AliasedImportDecl*>(decl)) {
            import_decl->accept(this);
        } else if (auto import_decl = dynamic_cast<SelectiveImportDecl*>(decl)) {
            import_decl->accept(this);
        } else if (auto struct_decl = dynamic_cast<StructDecl*>(decl)) {
            String qualified = String(ctx.parent_->name()) + "." + String(struct_decl->identifier());
            ctx.declare_global(struct_decl->identifier(), struct_decl);
            struct_decl->type_id_ = nw::kernel::runtime().type_table_.reserve(qualified);
        } else if (auto type_alias = dynamic_cast<TypeAlias*>(decl)) {
            String qualified = String(ctx.parent_->name()) + "." + String(type_alias->identifier());
            ctx.declare_global(type_alias->identifier(), type_alias);
            type_alias->type_id_ = nw::kernel::runtime().type_table_.reserve(qualified);
        } else if (auto newtype = dynamic_cast<NewtypeDecl*>(decl)) {
            String qualified = String(ctx.parent_->name()) + "." + String(newtype->identifier());
            ctx.declare_global(newtype->identifier(), newtype);
            newtype->type_id_ = nw::kernel::runtime().type_table_.reserve(qualified);
        } else if (auto opaque = dynamic_cast<OpaqueTypeDecl*>(decl)) {
            String qualified = String(ctx.parent_->name()) + "." + String(opaque->identifier());
            ctx.declare_global(opaque->identifier(), opaque);
            opaque->type_id_ = nw::kernel::runtime().type_table_.reserve(qualified);
        } else if (auto sum_decl = dynamic_cast<SumDecl*>(decl)) {
            String qualified = String(ctx.parent_->name()) + "." + String(sum_decl->identifier());
            ctx.declare_global(sum_decl->identifier(), sum_decl);
            sum_decl->type_id_ = nw::kernel::runtime().type_table_.reserve(qualified);
        } else if (auto func = dynamic_cast<FunctionDefinition*>(decl)) {
            ctx.declare_global(func->identifier_.loc.view(), func);
        } else if (auto var = dynamic_cast<VarDecl*>(decl)) {
            ctx.declare_global(var->identifier_.loc.view(), var);
        } else if (auto decl_list = dynamic_cast<DeclList*>(decl)) {
            for (auto& d : decl_list->decls) {
                ctx.declare_global(d->identifier_.loc.view(), d);
            }
        }
    }
}

void NameResolver::visit(AliasedImportDecl* decl)
{
    decl->env_ = ctx.env_stack_.back();

    String resource_path = module_path_to_resource_path(decl->module_path);
    decl->loaded_module = nw::kernel::runtime().get_module(resource_path);
    if (!decl->loaded_module) {
        ctx.errorf(decl->alias.loc.range, "failed to load module '{}'", decl->module_path);
        return;
    }

    ctx.declare_global(decl->alias.loc.view(), decl);
    decl->type_id_ = nw::kernel::runtime().type_id("module");

    auto alias_name = String(decl->alias.loc.view());
    auto alias_export = ctx.env_stack_.back().find(alias_name);
    if (alias_export) {
        Export updated = *alias_export;
        updated.kind = Export::Kind::module_alias;
        if (decl->loaded_module) {
            updated.provider_module = String(decl->loaded_module->name());
        }
        updated.type_id = decl->type_id_;
        ctx.env_stack_.back() = ctx.env_stack_.back().set(alias_name, std::move(updated));
    }
}

void NameResolver::visit(SelectiveImportDecl* decl)
{
    decl->env_ = ctx.env_stack_.back();

    String resource_path = module_path_to_resource_path(decl->module_path);
    decl->loaded_module = nw::kernel::runtime().get_module(resource_path);
    if (!decl->loaded_module) {
        ctx.errorf(decl->imported_symbols.empty() ? SourceRange{} : decl->imported_symbols[0].loc.range,
            "failed to load module '{}'", decl->module_path);
        return;
    }

    auto module_exports = decl->loaded_module->exports();

    for (const auto& symbol_token : decl->imported_symbols) {
        auto symbol_name = String(symbol_token.loc.view());
        auto export_ptr = module_exports.find(symbol_name);

        if (!export_ptr) {
            auto suggestions = format_suggestions(symbol_name, ctx.collect_module_exports(decl->loaded_module));
            ctx.errorf(symbol_token.loc.range, "'{}' not found in module '{}'{}", symbol_name, decl->module_path, suggestions);
            continue;
        }

        if (!export_ptr->decl) {
            bool importable = (export_ptr->kind == Export::Kind::function && export_ptr->function_abi)
                || (export_ptr->type_id != invalid_type_id);
            if (!importable) {
                ctx.errorf(symbol_token.loc.range,
                    "'{}' exists in module '{}' but semantic declaration data is unavailable in current debug level",
                    symbol_name,
                    decl->module_path);
                continue;
            }
        }

        auto git = ctx.global_decls_.find(symbol_name);
        auto existing_env = ctx.env_stack_.back().find(symbol_name);
        if (git != ctx.global_decls_.end() || existing_env) {
            ctx.errorf(symbol_token.loc.range, "importing '{}' would redeclare existing symbol", symbol_name);
            continue;
        }

        if (export_ptr->decl) {
            ctx.global_decls_.insert({symbol_name, export_ptr->decl});
            ctx.record_decl_provider(export_ptr->decl, decl->loaded_module);
        }
        ctx.env_stack_.back() = ctx.env_stack_.back().set(symbol_name, *export_ptr);
    }
}

} // namespace nw::smalls
