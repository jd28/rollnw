#pragma once

#include "../log.hpp"
#include "../util/templates.hpp"
#include "Context.hpp"
#include "Nss.hpp"

#include <immer/map.hpp>

#include <unordered_map>
#include <vector>

namespace nw::script {

struct ScopeDecl {
    bool decl_ready = false;
    bool struct_decl_ready = false;
    Declaration* decl = nullptr;
    StructDecl* struct_decl = nullptr;
};

struct AstResolver : BaseVisitor {
    AstResolver(Nss* parent, Context* ctx, bool command_script = false)
        : parent_{parent}
        , ctx_{ctx}
        , is_command_script_{command_script}
    {
        CHECK_F(!!ctx_, "[script] invalid script context");
        env_stack_.push_back({});
    }

    virtual ~AstResolver() = default;

    using ScopeMap = std::unordered_map<std::string, ScopeDecl>;
    using ScopeStack = std::vector<ScopeMap>;
    using EnvStack = std::vector<immer::map<std::string, Export>>;

    Nss* parent_ = nullptr;
    Context* ctx_ = nullptr;
    ScopeStack scope_stack_;
    EnvStack env_stack_;
    int loop_stack_ = 0;
    int switch_stack_ = 0;
    FunctionDefinition* func_def_stack_ = nullptr;
    bool is_command_script_ = false;

    // == Resolver Helpers ====================================================
    // ========================================================================

    void begin_scope(bool global = false)
    {
        scope_stack_.push_back(ScopeMap{});
        if (!global) {
            env_stack_.push_back(env_stack_.back());
        }
    }

    void declare(NssToken token, Declaration* decl, bool is_type = false)
    {
        auto s = std::string(token.loc.view());

        if (scope_stack_.size() == 1) { // global scope
            bool redecl = false;
            std::string msg;

            for (auto& it : reverse(parent_->ast().includes)) {
                if (!it.script) { continue; }
                if (auto decl = locate(s, it.script, is_type)) {
                    redecl = true;
                    msg = fmt::format("redeclaration of '{}' imported declaration", token.loc.view());
                    break;
                }
            }

            if (!redecl && !is_command_script_) {
                auto sym = ctx_->command_script_->locate_export(s, is_type);
                if (sym.decl) {
                    redecl = true;
                    msg = fmt::format("redeclaration of '{}' command script declaration", token.loc.view());
                }
            }

            if (redecl) {
                ctx_->semantic_diagnostic(parent_, msg, false, token.loc.range);
            }
        }

        auto it = scope_stack_.back().find(s);
        if (it != std::end(scope_stack_.back())) {
            if (is_type) {
                if (it->second.struct_decl) {
                    ctx_->semantic_diagnostic(parent_,
                        fmt::format("declaring '{}' in the same scope twice", token.loc.view()),
                        false,
                        token.loc.range);
                } else {
                    it->second.struct_decl = dynamic_cast<StructDecl*>(decl);
                }
            } else {
                if (!it->second.decl) {
                    it->second.decl = decl;
                } else if (dynamic_cast<FunctionDecl*>(it->second.decl)
                    && dynamic_cast<FunctionDefinition*>(decl)) {
                    it->second.decl = decl;
                } else {
                    ctx_->semantic_diagnostic(parent_,
                        fmt::format("declaring '{}' in the same scope twice", token.loc.view()),
                        false,
                        token.loc.range);
                }
            }
        } else {
            if (is_type) {
                scope_stack_.back().insert({s, {false, false, nullptr, dynamic_cast<StructDecl*>(decl)}});
            } else {
                scope_stack_.back().insert({s, {false, false, decl, nullptr}});
            }
        }
    }

    void define(NssToken token, bool is_type = false)
    {
        auto s = std::string(token.loc.view());
        auto it = scope_stack_.back().find(s);
        if (it == std::end(scope_stack_.back())) {
            ctx_->semantic_diagnostic(parent_,
                fmt::format("defining unknown variable '{}'", token.loc.view()),
                false,
                token.loc.range);
        }
        if (is_type) {
            it->second.struct_decl_ready = true;
        } else {
            it->second.decl_ready = true;
        }
        auto& env = env_stack_.back();

        auto sym = env.find(s);
        Export temp;

        if (sym) { temp = *sym; }
        if (is_type) {
            temp.type = it->second.struct_decl;
        } else {
            temp.decl = it->second.decl;
        }
        env_stack_.back() = env.set(s, temp);
    }

    void end_scope(bool global = false)
    {
        scope_stack_.pop_back();
        if (!global) { env_stack_.pop_back(); }
    }

    immer::map<std::string, Export> symbol_table() const
    {
        return env_stack_[0];
    }

    // Locate decl in script or dependencies
    // Note: does not check command script
    const Declaration* locate(const std::string& token, Nss* script, bool is_type)
    {
        auto symbol = script->locate_export(token, is_type);
        if (symbol.decl) {
            return symbol.decl;
        } else {
            for (auto& it : reverse(script->ast().includes)) {
                if (!it.script) { continue; }
                if (auto decl = locate(token, it.script, is_type)) {
                    return decl;
                }
            }
        }
        return nullptr;
    }

    // Resolve symbol to original decl
    const Declaration* resolve(std::string_view token, SourceRange range, bool is_type)
    {
        auto s = std::string(token);

        // Look first through the scope stack in the current script
        for (const auto& map : reverse(scope_stack_)) {
            auto it = map.find(s);
            if (it == std::end(map)) { continue; }
            if (is_type) {
                if (!it->second.struct_decl_ready) {
                    ctx_->semantic_diagnostic(parent_,
                        fmt::format("recursive use of struct '{}' in declaration", token),
                        false,
                        range);
                }
                return it->second.struct_decl;
            } else {
                if (it->second.decl && !it->second.decl_ready) {
                    ctx_->semantic_diagnostic(parent_,
                        fmt::format("using declared variable '{}' in init", token),
                        false,
                        range);
                }
                return it->second.decl;
            }
        }

        // Next look through all dependencies
        for (auto it : reverse(parent_->ast().includes)) {
            if (!it.script) { continue; }
            if (auto decl = locate(s, it.script, is_type)) { return decl; }
        }

        if (!is_command_script_ && ctx_->command_script_) {
            auto sym = ctx_->command_script_->locate_export(s, is_type);
            return sym.decl;
        }

        return nullptr;
    }

    // == Visitor =============================================================
    // ========================================================================

    virtual void visit(Ast* script) override
    {
        begin_scope(true);
        for (const auto& decl : script->decls) {
            decl->accept(this);
            if (auto d = dynamic_cast<VarDecl*>(decl)) {
                d->is_const_ = true; // All top level var decls are constant.  Probably wrong??
            }
        }
        end_scope(true);
    }

    // Decls

    void match_function_decls(const FunctionDecl* decl, const FunctionDecl* def)
    {
        if (!decl || !def) { return; }

        // If there's a function declaration, try to match
        if (def->type_id_ != decl->type_id_) {
            ctx_->semantic_diagnostic(parent_,
                fmt::format("function declared with return type '{}', defined with return type '{}' ",
                    ctx_->type_name(decl->type_id_),
                    ctx_->type_name(def->type_id_)),
                false,
                def->type.type_specifier.loc.range); // [TODO] expand this
        }

        if (decl->params.size() != def->params.size()) {
            ctx_->semantic_diagnostic(parent_,
                fmt::format("function declared with parameter count '{}', defined with parameter count '{}' ",
                    decl->params.size(),
                    def->params.size()),
                false,
                def->identifier_.loc.range); // [TODO] expand this
        } else {
            std::string reason;

            for (size_t i = 0; i < decl->params.size(); ++i) {
                SourceLocation loc;
                bool mismatch = false;
                bool warning = false;

                if (decl->params[i]->type_id_ != def->params[i]->type_id_) {
                    reason = fmt::format("function parameter declared with type '{}', defined with type '{}'",
                        ctx_->type_name(decl->params[i]->type_id_), ctx_->type_name(def->params[i]->type_id_));
                    mismatch = true;
                    loc = def->params[i]->type.type_specifier.loc;
                } else if (decl->params[i]->identifier_.loc.view() != def->params[i]->identifier_.loc.view()) {
                    reason = fmt::format("function parameter declared with identifier '{}', defined with identifier '{}'",
                        decl->params[i]->identifier_.loc.view(), def->params[i]->identifier_.loc.view());
                    mismatch = true;
                    warning = true;
                    loc = def->params[i]->identifier_.loc;
                } else if (decl->params[i]->is_const_ != def->params[i]->is_const_) {
                    reason = fmt::format("function parameter const mismatch",
                        ctx_->type_name(decl->params[i]->type_id_), ctx_->type_name(def->params[i]->type_id_));
                    mismatch = true;
                    loc = decl->params[i]->identifier_.loc;
                } else if (decl->params[i]->init && def->params[i]->init) {
                    // [TODO] Probably need to have some sort of constant folding or tree walking interpreter
                    // to ensure the values of initializers are the same.
                    auto lit1 = dynamic_cast<LiteralExpression*>(decl->params[i]->init);
                    auto lit2 = dynamic_cast<LiteralExpression*>(def->params[i]->init);
                    if (lit1 && lit2 && lit1->data != lit2->data) {
                        reason = "mismatch parameter initializers";
                        mismatch = true;
                        loc = decl->params[i]->identifier_.loc;
                    } else {
                        auto vlit1 = dynamic_cast<LiteralVectorExpression*>(decl->params[i]->init);
                        auto vlit2 = dynamic_cast<LiteralVectorExpression*>(def->params[i]->init);
                        if (vlit1 && vlit2 && vlit1->data != vlit2->data) {
                            reason = "mismatch parameter initializers";
                            mismatch = true;
                            loc = decl->params[i]->identifier_.loc;
                        }
                    }
                }
                if (mismatch) {
                    ctx_->semantic_diagnostic(parent_, reason, warning, loc.range);
                }
            }
        }
    }

    bool all_control_flow_paths_return(const AstNode* node)
    {
        if (!node) { return false; }

        bool has_returned = false;
        if (auto block = dynamic_cast<const BlockStatement*>(node)) {
            for (const auto& decl : block->nodes) {
                if (all_control_flow_paths_return(decl)) {
                    return true;
                }
            }
        } else if (auto jump = dynamic_cast<const JumpStatement*>(node)) {
            if (jump->op.type == NssTokenType::RETURN) {
                return true;
            }
        } else if (auto ifs = dynamic_cast<const IfStatement*>(node)) {
            // Both if and else must be present and both
            if (ifs->if_branch && ifs->else_branch
                && all_control_flow_paths_return(ifs->if_branch)
                && all_control_flow_paths_return(ifs->else_branch)) {
                return true;
            }
        } else if (auto ifs = dynamic_cast<const SwitchStatement*>(node)) {
            // [TODO] - Fix this
            // Every block most return - the game seems really stupid about this
            // So do nothing for now.
        }

        return false;
    }

    virtual void visit(FunctionDecl* decl) override
    {
        decl->env_ = env_stack_.back();
        // Check to see if there's been a function definition, if so got to match.
        auto fd = resolve(decl->identifier_.loc.view(), decl->identifier_.loc.range, false);

        decl->type_id_ = ctx_->type_id(decl->type);
        declare(decl->identifier_, decl);
        define(decl->identifier_);

        // Multiple declarations...
        if (auto d = dynamic_cast<const FunctionDecl*>(fd)) { return; }

        begin_scope();
        for (auto& p : decl->params) {
            p->accept(this);
            if (p->init && !p->init->is_const_) {
                ctx_->semantic_diagnostic(parent_, "initializing parameter a with non-constant expression",
                    false,
                    p->identifier_.loc.range);
            }
        }
        end_scope();

        if (auto d = dynamic_cast<const FunctionDefinition*>(fd)) {
            match_function_decls(decl, d->decl_inline);
        }
    }

    virtual void visit(FunctionDefinition* decl) override
    {
        decl->env_ = env_stack_.back();
        func_def_stack_ = decl;
        // Check to see if there's been a function declaration, if so got to match.
        auto fd = resolve(decl->decl_inline->identifier_.loc.view(), decl->decl_inline->identifier_.loc.range, false);
        decl->decl_external = dynamic_cast<const FunctionDecl*>(fd);

        decl->type_id_ = decl->decl_inline->type_id_ = ctx_->type_id(decl->decl_inline->type);

        declare(decl->decl_inline->identifier_, decl);
        define(decl->decl_inline->identifier_);

        begin_scope();
        for (auto& p : decl->decl_inline->params) {
            p->accept(this);
            if (p->init && !p->init->is_const_) {
                ctx_->semantic_diagnostic(parent_, "initializing parameter a with non-constant expression",
                    false,
                    p->identifier_.loc.range);
            }
        }

        match_function_decls(decl->decl_external, decl->decl_inline);

        decl->block->accept(this);
        if (decl->type_id_ != ctx_->type_id("void") && !all_control_flow_paths_return(decl->block)) {
            ctx_->semantic_diagnostic(parent_, "not all control flow paths return",
                false, decl->decl_inline->identifier_.loc.range);
        }

        end_scope();
        func_def_stack_ = nullptr;
    }

    virtual void visit(StructDecl* decl) override
    {
        decl->env_ = env_stack_.back();
        declare(decl->type.struct_id, decl, true);
        decl->type_id_ = ctx_->type_id(decl->type, true);
        begin_scope();
        for (auto& it : decl->decls) {
            it->accept(this);
            if (it->is_const_) {
                ctx_->semantic_diagnostic(parent_,
                    fmt::format("struct member '{}' cannot be declared as 'const'", it->identifier_.loc.view()),
                    false, it->identifier_.loc.range);
            }
        }
        end_scope();
        // Define down here so there's no recursive elements
        define(decl->type.struct_id, true);
    }

    virtual void visit(VarDecl* decl) override
    {
        decl->env_ = env_stack_.back();
        decl->is_const_ = decl->type.type_qualifier.type == NssTokenType::CONST_;
        decl->type_id_ = ctx_->type_id(decl->type);

        if (decl->type_id_ == ctx_->type_id("void")) {
            ctx_->semantic_diagnostic(parent_, "variable declared with void type",
                false,
                decl->identifier_.loc.range);
        }

        if (decl->is_const_ && !decl->init) {
            ctx_->semantic_diagnostic(parent_, "constant variable declaration with no initializer",
                false,
                decl->identifier_.loc.range);
        }

        declare(decl->identifier_, decl);
        if (decl->init) {
            decl->init->accept(this);

            if (decl->type_id_ == ctx_->type_id("float")
                && decl->init->type_id_ == ctx_->type_id("int")) {
                // This is fine
            } else if (decl->type_id_ != decl->init->type_id_) {
                ctx_->semantic_diagnostic(parent_,
                    fmt::format("initializing variable '{}' of type '{}' with value of type '{}' ",
                        decl->identifier_.loc.view(),
                        ctx_->type_name(decl->type_id_),
                        ctx_->type_name(decl->init->type_id_)),
                    false,
                    decl->identifier_.loc.range);
            }
        }
        define(decl->identifier_);
    }

    // Expressions
    virtual void visit(AssignExpression* expr) override
    {
        expr->env_ = env_stack_.back();
        expr->lhs->accept(this);
        expr->rhs->accept(this);

        if (!ctx_->type_check_binary_op(expr->op, expr->lhs->type_id_, expr->rhs->type_id_)) {
            ctx_->semantic_diagnostic(parent_,
                fmt::format("invalid operands of types '{}' and '{}' to binary operator'{}' ",
                    ctx_->type_name(expr->lhs->type_id_),
                    ctx_->type_name(expr->rhs->type_id_),
                    expr->op.loc.view()),
                false,
                expr->op.loc.range);
            return;
        }

        expr->type_id_ = expr->lhs->type_id_;
    }

    virtual void visit(BinaryExpression* expr) override
    {
        expr->env_ = env_stack_.back();
        expr->lhs->accept(this);
        expr->rhs->accept(this);

        expr->is_const_ = expr->lhs->is_const_ && expr->rhs->is_const_;

        if (!ctx_->type_check_binary_op(expr->op, expr->lhs->type_id_, expr->rhs->type_id_)) {
            ctx_->semantic_diagnostic(parent_,
                fmt::format("invalid operands of types '{}' and '{}' to binary operator'{}' ",
                    ctx_->type_name(expr->lhs->type_id_),
                    ctx_->type_name(expr->rhs->type_id_),
                    expr->op.loc.view()),
                false,
                expr->op.loc.range);
            return;
        }
        expr->type_id_ = expr->lhs->type_id_;
    }

    virtual void visit(CallExpression* expr) override
    {
        expr->env_ = env_stack_.back();

        auto ve = dynamic_cast<VariableExpression*>(expr->expr);
        if (!ve) {
            ctx_->semantic_diagnostic(
                parent_,
                "only variable expressions are permitted in function call expression",
                false,
                expr->expr->range_);
            return;
        }

        ve->env_ = env_stack_.back();

        const FunctionDecl* func_decl = nullptr;
        const FunctionDecl* orig_decl = nullptr;
        auto decl = resolve(ve->var.loc.view(), ve->var.loc.range, false);
        if (auto fd = dynamic_cast<const FunctionDecl*>(decl)) {
            func_decl = fd;
        } else if (auto fd = dynamic_cast<const FunctionDefinition*>(decl)) {
            func_decl = fd->decl_inline;
            orig_decl = fd->decl_external;
        } else {
            ctx_->semantic_diagnostic(parent_,
                fmt::format("unable to resolve identifier '{}'", ve->var.loc.view()),
                false,
                ve->range_);
            return;
        }

        expr->type_id_ = func_decl->type_id_;

        size_t req = 0;
        for (size_t i = 0; i < func_decl->params.size(); ++i) {
            if (func_decl->params[i]->init || (orig_decl && orig_decl->params[i]->init)) { break; }
            ++req;
        }

        if (expr->args.size() < req || expr->args.size() > func_decl->params.size()) {
            ctx_->semantic_diagnostic(parent_,
                fmt::format("no matching function call '{}' expected {} parameters",
                    parent_->view_from_range(expr->range_), req),
                false,
                expr->range_);
            return;
        }

        for (size_t i = 0; i < expr->args.size(); ++i) {
            expr->args[i]->accept(this);

            if (dynamic_cast<EmptyExpression*>(expr->args[i])) {
                ctx_->semantic_diagnostic(parent_,
                    "argument cannot be a null expression",
                    false,
                    expr->args[i]->range_);
                continue;
            }

            if (func_decl->params[i]->type_id_ == ctx_->type_id("float")
                && expr->args[i]->type_id_ == ctx_->type_id("int")) {
                // This is fine
            } else if (func_decl->params[i]->type_id_ == ctx_->type_id("action")
                && dynamic_cast<CallExpression*>(expr->args[i])) {
                // This is fine
            } else if (func_decl->params[i]->type_id_ != expr->args[i]->type_id_) {
                ctx_->semantic_diagnostic(parent_,
                    fmt::format("no matching function call '{}' expected parameter type '{}' ",
                        parent_->view_from_range(expr->range_),
                        ctx_->type_name(func_decl->params[i]->type_id_)),
                    false,
                    expr->range_);
            }
        }
    }

    virtual void visit(ComparisonExpression* expr) override
    {
        expr->env_ = env_stack_.back();
        expr->lhs->accept(this);
        expr->rhs->accept(this);

        expr->is_const_ = expr->lhs->is_const_ && expr->rhs->is_const_;

        if (expr->lhs->type_id_ != expr->rhs->type_id_
            && !ctx_->is_type_convertible(expr->lhs->type_id_, expr->rhs->type_id_)
            && !ctx_->is_type_convertible(expr->rhs->type_id_, expr->lhs->type_id_)) {
            ctx_->semantic_diagnostic(parent_,
                fmt::format(
                    "mismatched types in binary-expression '{}' != '{}', {} ",
                    ctx_->type_name(expr->lhs->type_id_),
                    ctx_->type_name(expr->rhs->type_id_),
                    expr->op.loc.view()),
                false,
                expr->range_);
        }
        expr->type_id_ = ctx_->type_id("int");
    }

    virtual void visit(ConditionalExpression* expr) override
    {
        expr->env_ = env_stack_.back();
        expr->test->accept(this);
        if (expr->test->type_id_ != ctx_->type_id("int")) {
            ctx_->semantic_diagnostic(parent_,
                fmt::format("could not convert value of type '{}' to integer bool ",
                    ctx_->type_name(expr->test->type_id_)),
                false,
                expr->test->range_);
        }

        expr->true_branch->accept(this);
        expr->false_branch->accept(this);

        if (expr->true_branch->type_id_ != expr->false_branch->type_id_) {
            ctx_->semantic_diagnostic(parent_,
                fmt::format("operands of operator ?: have different types '{}' and'{}' ",
                    ctx_->type_name(expr->true_branch->type_id_),
                    ctx_->type_name(expr->false_branch->type_id_)),
                false,
                expr->range_);
        }

        expr->type_id_ = expr->true_branch->type_id_;
    }

    virtual void visit(DotExpression* expr) override
    {
        expr->env_ = env_stack_.back();

        auto resolve_struct_member = [this](VariableExpression* var, const StructDecl* str) {
            for (const auto& it : str->decls) {
                if (it->identifier_.loc.view() == var->var.loc.view()) {
                    var->type_id_ = it->type_id_;
                    var->is_const_ = it->is_const_;
                    return true;
                }
            }
            return false;
        };

        auto ex_rhs = dynamic_cast<VariableExpression*>(expr->rhs);
        if (!ex_rhs) {
            ctx_->semantic_diagnostic(parent_,
                "struct member must be a variable expression",
                false,
                expr->dot.loc.range);
            return;
        }

        const StructDecl* struct_decl = nullptr;
        std::string_view struct_type;
        if (auto de = dynamic_cast<DotExpression*>(expr->lhs)) {
            expr->lhs->accept(this);
            struct_type = ctx_->type_name(expr->lhs->type_id_);
            struct_decl = struct_decl = dynamic_cast<const StructDecl*>(resolve(struct_type, expr->dot.loc.range, true));
        } else if (auto ve = dynamic_cast<VariableExpression*>(expr->lhs)) {
            ve->accept(this);

            // special case vector lookup here for now
            if (ve->type_id_ == ctx_->type_id("vector")
                && (ex_rhs->var.loc.view() == "x"
                    || ex_rhs->var.loc.view() == "y"
                    || ex_rhs->var.loc.view() == "z")) {
                expr->type_id_ = ctx_->type_id("float");
                return;
            }

            struct_type = ctx_->type_name(ve->type_id_);
            struct_decl = dynamic_cast<const StructDecl*>(resolve(ctx_->type_name(ve->type_id_), ve->var.loc.range, true));
        }

        if (!struct_decl) {
            ctx_->semantic_diagnostic(parent_,
                fmt::format("request for member '{}' in '{}', which is of non - struct type ",
                    ex_rhs->var.loc.view(),
                    struct_type),
                false,
                expr->dot.loc.range);
        } else if (!resolve_struct_member(ex_rhs, struct_decl)) {
            ctx_->semantic_diagnostic(parent_,
                fmt::format("request for member '{}', which is not in struct of type '{}' ",
                    ex_rhs->var.loc.view(),
                    struct_type),
                false,
                expr->dot.loc.range);
        }

        expr->type_id_ = expr->rhs->type_id_;
    }

    virtual void visit(EmptyExpression* expr) override
    {
        // No op
    }

    virtual void visit(GroupingExpression* expr) override
    {
        expr->env_ = env_stack_.back();
        if (expr->expr) {
            expr->expr->accept(this);
        } else {
            ctx_->semantic_diagnostic(parent_, "empty groupling expression", true, expr->range_);
        }
        expr->type_id_ = expr->expr->type_id_;
        expr->is_const_ = expr->expr->is_const_;
    }

    virtual void visit(LiteralExpression* expr) override
    {
        expr->env_ = env_stack_.back();
        expr->is_const_ = true;
        if (expr->literal.type == NssTokenType::FLOAT_CONST) {
            expr->type_id_ = ctx_->type_id("float");
        } else if (expr->literal.type == NssTokenType::INTEGER_CONST) {
            expr->type_id_ = ctx_->type_id("int");
        } else if (expr->literal.type == NssTokenType::STRING_CONST) {
            expr->type_id_ = ctx_->type_id("string");
        } else if (expr->literal.type == NssTokenType::OBJECT_SELF_CONST
            || expr->literal.type == NssTokenType::OBJECT_INVALID_CONST) {
            expr->type_id_ = ctx_->type_id("object");
        } else if (expr->literal.type == NssTokenType::LOCATION_INVALID) {
            expr->type_id_ = ctx_->type_id("location");
        } else if (expr->literal.type == NssTokenType::JSON_CONST) {
            expr->type_id_ = ctx_->type_id("json");
        }
    }

    virtual void visit(LiteralVectorExpression* expr) override
    {
        expr->env_ = env_stack_.back();
        expr->is_const_ = true;
        expr->type_id_ = ctx_->type_id("vector");
    }

    virtual void visit(LogicalExpression* expr) override
    {
        expr->env_ = env_stack_.back();
        expr->lhs->accept(this);
        expr->rhs->accept(this);

        if (expr->lhs->type_id_ != expr->rhs->type_id_) {
            ctx_->semantic_diagnostic(parent_, "mismatched types", false, expr->op.loc.range);
        }

        expr->type_id_ = ctx_->type_id("int");
        expr->is_const_ = expr->lhs->is_const_ && expr->rhs->is_const_;
    }

    virtual void visit(PostfixExpression* expr) override
    {
        expr->env_ = env_stack_.back();
        expr->lhs->accept(this);
        expr->type_id_ = expr->lhs->type_id_;
        expr->is_const_ = expr->lhs->is_const_;
    }

    virtual void visit(UnaryExpression* expr) override
    {
        expr->env_ = env_stack_.back();
        expr->rhs->accept(this);
        expr->type_id_ = expr->rhs->type_id_;
        expr->is_const_ = expr->rhs->is_const_;
    }

    virtual void visit(VariableExpression* expr) override
    {
        expr->env_ = env_stack_.back();
        auto decl = resolve(expr->var.loc.view(), expr->var.loc.range, false);
        if (decl) {
            expr->type_id_ = decl->type_id_;
            expr->is_const_ = decl->is_const_;
        } else {
            ctx_->semantic_diagnostic(parent_,
                fmt::format("unable to resolve identifier '{}'", expr->var.loc.view()),
                false,
                expr->range_);
        }
    }

    // Statements
    virtual void visit(BlockStatement* stmt) override
    {
        stmt->env_ = env_stack_.back();
        stmt->type_id_ = ctx_->type_id("void");
        for (auto& s : stmt->nodes) {
            s->accept(this);
        }
    }

    virtual void visit(DeclList* stmt) override
    {
        stmt->env_ = env_stack_.back();
        size_t ti = invalid_type_id;
        for (auto& s : stmt->decls) {
            // types of all must be the same;
            s->accept(this);
            if (ti == invalid_type_id) {
                ti = s->type_id_;
            } else {
                if (ti != s->type_id_) {
                }
            }
        }
    }

    virtual void visit(DoStatement* stmt) override
    {
        stmt->env_ = env_stack_.back();
        ++loop_stack_;
        begin_scope();
        stmt->block->accept(this);
        end_scope();

        stmt->expr->accept(this);
        if (stmt->expr->type_id_ != ctx_->type_id("int")) {
            ctx_->semantic_diagnostic(parent_,
                fmt::format("could not convert value of type '{}' to integer bool",
                    ctx_->type_name(stmt->expr->type_id_)),
                false,
                stmt->expr->range_);
        }

        --loop_stack_;
    }

    virtual void visit(EmptyStatement* stmt) override
    {
        stmt->env_ = env_stack_.back();
        stmt->type_id_ = ctx_->type_id("void");
    }

    virtual void visit(ExprStatement* stmt) override
    {
        stmt->env_ = env_stack_.back();
        stmt->expr->accept(this);
    }

    virtual void visit(IfStatement* stmt) override
    {
        stmt->env_ = env_stack_.back();
        stmt->type_id_ = ctx_->type_id("void");
        stmt->expr->accept(this);

        if (stmt->expr->type_id_ != ctx_->type_id("int")) {
            ctx_->semantic_diagnostic(parent_,
                fmt::format("could not convert value of type '{}' to integer bool",
                    ctx_->type_name(stmt->expr->type_id_)),
                false,
                stmt->expr->range_);
        }

        begin_scope();
        stmt->if_branch->accept(this);
        end_scope();
        if (stmt->else_branch) {
            begin_scope();
            stmt->else_branch->accept(this);
            end_scope();
        }
    }

    virtual void visit(ForStatement* stmt) override
    {
        stmt->env_ = env_stack_.back();
        ++loop_stack_;
        begin_scope();

        if (stmt->init) {
            stmt->init->accept(this);
        }

        if (stmt->check) {
            stmt->check->accept(this);
            if (stmt->check->type_id_ != ctx_->type_id("int")) {
                ctx_->semantic_diagnostic(parent_,
                    fmt::format("could not convert value of type '{}' to integer bool",
                        ctx_->type_name(stmt->check->type_id_)),
                    false,
                    stmt->check->range_);
            }
        }

        if (stmt->inc) {
            stmt->inc->accept(this);
        }

        stmt->block->accept(this);
        end_scope();
        --loop_stack_;
    }

    virtual void visit(JumpStatement* stmt) override
    {
        stmt->env_ = env_stack_.back();

        if (stmt->expr) {
            stmt->expr->accept(this);
            stmt->type_id_ = stmt->expr->type_id_;
        } else {
            stmt->type_id_ = ctx_->type_id("void");
        }

        if (stmt->op.type == NssTokenType::CONTINUE
            && loop_stack_ == 0) {
            ctx_->semantic_diagnostic(parent_, "continue statement not within a loop", false, stmt->op.loc.range);

        } else if (stmt->op.type == NssTokenType::BREAK
            && (loop_stack_ == 0 && switch_stack_ == 0)) {
            ctx_->semantic_diagnostic(parent_, "break statement not within loop or switch", false, stmt->op.loc.range);

        } else if (stmt->op.type == NssTokenType::RETURN) {
            if (!func_def_stack_) {
                // This shouldn't even be possible and would be a parser error
                ctx_->semantic_diagnostic(parent_, "return statement not within function definition", false, stmt->op.loc.range);
            } else if (func_def_stack_->type_id_ != stmt->type_id_
                && !ctx_->is_type_convertible(func_def_stack_->type_id_, stmt->type_id_)) {
                ctx_->semantic_diagnostic(parent_,
                    fmt::format("returning type '{}' expected '{}'", ctx_->type_name(stmt->type_id_),
                        ctx_->type_name(func_def_stack_->type_id_)),
                    false,
                    stmt->expr->range_);
            }
        }
    }

    virtual void visit(LabelStatement* stmt) override
    {
        stmt->env_ = env_stack_.back();

        if ((stmt->type.type == NssTokenType::CASE
                || stmt->type.type == NssTokenType::DEFAULT)
            && switch_stack_ == 0) {
            ctx_->semantic_diagnostic(parent_, "label statement not within switch", false, stmt->type.loc.range);
        }

        if (stmt->type.type == NssTokenType::DEFAULT) {
            return; // No expr..
        }

        stmt->expr->accept(this);
        if (stmt->expr->type_id_ != ctx_->type_id("int")
            && stmt->expr->type_id_ != ctx_->type_id("string")) {
            ctx_->semantic_diagnostic(parent_,
                fmt::format("could not convert value to integer or string"),
                false, stmt->expr->range_);
        } else if (!stmt->expr->is_const_) {
            ctx_->semantic_diagnostic(parent_, "case expression must be constant expression",
                false, stmt->expr->range_);
        }
    }

    virtual void visit(SwitchStatement* stmt) override
    {
        stmt->env_ = env_stack_.back();

        stmt->type_id_ = ctx_->type_id("void");
        ++switch_stack_;
        stmt->target->accept(this);

        // this could have string type also, but when the string case statements
        // were added to NWscript, the NWN:EE team half-assed this from what
        // I could tell.
        if (stmt->target->type_id_ != ctx_->type_id("int")) {
            ctx_->semantic_diagnostic(parent_,
                fmt::format("switch quantity not an integer"),
                false,
                stmt->target->range_);
        }

        begin_scope();
        stmt->block->accept(this);
        end_scope();
        --switch_stack_;
    }

    virtual void visit(WhileStatement* stmt) override
    {
        stmt->env_ = env_stack_.back();

        stmt->type_id_ = ctx_->type_id("void");
        ++loop_stack_;

        stmt->check->accept(this);
        if (stmt->check->type_id_ != ctx_->type_id("int")) {
            ctx_->semantic_diagnostic(parent_,
                fmt::format("could not convert value of type '{}' to integer bool",
                    ctx_->type_name(stmt->check->type_id_)),
                false,
                stmt->check->range_);
        }

        begin_scope();
        stmt->block->accept(this);
        end_scope();
        --loop_stack_;
    }
};

} // namespace nw::script
