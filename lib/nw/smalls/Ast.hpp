#pragma once

#include "../objects/Location.hpp"
#include "../util/Variant.hpp"
#include "../util/string.hpp"
#include "Context.hpp"
#include "Token.hpp"
#include "VirtualMachineIntrinsics.hpp"
#include "types.hpp"

#include <fmt/format.h>
#include <glm/glm.hpp>
#include <immer/map.hpp>

#include <stdexcept>
#include <unordered_map>

namespace nw::smalls {

struct ast_limit_error : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct Script;
struct Ast;
struct Context;
struct Export;

struct AnnotationArg;
struct Annotation;
struct Declaration;
struct FunctionDefinition;
struct StructDecl;
struct SumDecl;
struct VariantDecl;
struct VarDecl;
struct TypeAlias;
struct NewtypeDecl;
struct OpaqueTypeDecl;
struct ImportDecl;
struct AliasedImportDecl;
struct SelectiveImportDecl;

struct AssignExpression;
struct BinaryExpression;
struct BraceInitLiteral;
struct CallExpression;
struct CastExpression;
struct ComparisonExpression;
struct ConditionalExpression;
struct PathExpression;
struct EmptyExpression;
struct IndexExpression;
struct GroupingExpression;
struct TupleLiteral;
struct LiteralExpression;
struct FStringExpression;
struct LogicalExpression;
struct TypeExpression;
struct UnaryExpression;
struct IdentifierExpression;
struct LambdaExpression;

struct BlockStatement;
struct DeclList;
struct EmptyStatement;
struct ExprStatement;
struct IfStatement;
struct ForStatement;
struct ForEachStatement;
struct JumpStatement;
struct LabelStatement;
struct SwitchStatement;

struct BaseVisitor {
    virtual ~BaseVisitor() = default;

    virtual void visit(Ast* script) = 0;

    // Decls
    virtual void visit(FunctionDefinition* decl) = 0;
    virtual void visit(StructDecl* decl) = 0;
    virtual void visit(SumDecl* decl) = 0;
    virtual void visit(VariantDecl* decl) = 0;
    virtual void visit(VarDecl* decl) = 0;
    virtual void visit(TypeAlias* decl) = 0;
    virtual void visit(NewtypeDecl* decl) = 0;
    virtual void visit(OpaqueTypeDecl* decl) = 0;
    virtual void visit(AliasedImportDecl* decl) = 0;
    virtual void visit(SelectiveImportDecl* decl) = 0;

    // Expressions
    virtual void visit(AssignExpression* expr) = 0;
    virtual void visit(BinaryExpression* expr) = 0;
    virtual void visit(BraceInitLiteral* expr) = 0;
    virtual void visit(CallExpression* expr) = 0;
    virtual void visit(CastExpression* expr) = 0;
    virtual void visit(ComparisonExpression* expr) = 0;
    virtual void visit(ConditionalExpression* expr) = 0;
    virtual void visit(PathExpression* expr) = 0;
    virtual void visit(EmptyExpression* expr) = 0;
    virtual void visit(GroupingExpression* expr) = 0;
    virtual void visit(TupleLiteral* expr) = 0;
    virtual void visit(IndexExpression* expr) = 0;
    virtual void visit(LiteralExpression* expr) = 0;
    virtual void visit(FStringExpression* expr) = 0;
    virtual void visit(LogicalExpression* expr) = 0;
    virtual void visit(TypeExpression* expr) = 0;
    virtual void visit(UnaryExpression* expr) = 0;
    virtual void visit(IdentifierExpression* expr) = 0;
    virtual void visit(LambdaExpression* expr) = 0;

    // Statements
    virtual void visit(BlockStatement* stmt) = 0;
    virtual void visit(DeclList* stmt) = 0;
    virtual void visit(EmptyStatement* stmt) = 0;
    virtual void visit(ExprStatement* stmt) = 0;
    virtual void visit(IfStatement* stmt) = 0;
    virtual void visit(ForStatement* stmt) = 0;
    virtual void visit(ForEachStatement* stmt) = 0;
    virtual void visit(JumpStatement* stmt) = 0;
    virtual void visit(LabelStatement* stmt) = 0;
    virtual void visit(SwitchStatement* stmt) = 0;
};

struct AstNode {
    virtual ~AstNode() = default;
    virtual void accept(BaseVisitor* visitor) = 0;

    /// Find completions for this Ast Node
    /// @note This function does not traverse dependencies
    virtual void complete(const String& needle, Vector<const Declaration*>& out, bool no_filter = false) const;

    TypeID type_id_{}; // Default constructs to invalid TypeID
    bool is_const_ = false;
    immer::map<String, Export> env_;
    SourceRange range_;
};

#define DEFINE_ACCEPT_VISITOR                          \
    virtual void accept(BaseVisitor* visitor) override \
    {                                                  \
        visitor->visit(this);                          \
    }

// ---- Expression ------------------------------------------------------------

struct Expression : public AstNode {
    virtual ~Expression() = default;
};

struct AssignExpression : Expression {
    AssignExpression(Expression* lhs_, Token token, Expression* rhs_);

    Expression* lhs = nullptr;
    Token op;
    Expression* rhs = nullptr;

    DEFINE_ACCEPT_VISITOR
};

struct BinaryExpression : Expression {
    BinaryExpression(Expression* lhs_, Token token, Expression* rhs_);

    Expression* lhs = nullptr;
    Token op;
    Expression* rhs = nullptr;

    DEFINE_ACCEPT_VISITOR
};

struct ComparisonExpression : Expression {
    ComparisonExpression(Expression* lhs_, Token token, Expression* rhs_);

    Expression* lhs = nullptr;
    Token op;
    Expression* rhs = nullptr;

    DEFINE_ACCEPT_VISITOR
};

struct ConditionalExpression : Expression {
    ConditionalExpression(Expression* expr_, Expression* true_branch_, Expression* false_branch_);

    Expression* test = nullptr;
    Expression* true_branch = nullptr;
    Expression* false_branch = nullptr;

    DEFINE_ACCEPT_VISITOR
};

struct PathExpression : Expression {
    explicit PathExpression(MemoryResource* allocator);

    PVector<Expression*> parts;

    bool is_simple_identifier() const;
    IdentifierExpression* single_identifier() const;
    IdentifierExpression* last_identifier() const;

    DEFINE_ACCEPT_VISITOR
};

struct GroupingExpression : Expression {
    explicit GroupingExpression(Expression* expr_);

    Expression* expr = nullptr;

    DEFINE_ACCEPT_VISITOR
};

struct TupleLiteral : Expression {
    explicit TupleLiteral(MemoryResource* allocator);

    PVector<Expression*> elements;

    DEFINE_ACCEPT_VISITOR
};

struct IndexExpression : Expression {
    IndexExpression(Expression* target_, Expression* index_);

    Expression* target = nullptr;
    Expression* index = nullptr;

    DEFINE_ACCEPT_VISITOR
};

struct LiteralExpression : Expression {
    explicit LiteralExpression(Token token);

    Token literal;
    Variant<int32_t, float, PString, Location, ObjectID, bool> data;

    DEFINE_ACCEPT_VISITOR
};

struct FStringExpression : Expression {
    explicit FStringExpression(MemoryResource* allocator);

    Token fstring_token;
    PVector<PString> parts;
    PVector<Expression*> expressions;

    DEFINE_ACCEPT_VISITOR
};

struct LogicalExpression : Expression {
    LogicalExpression(Expression* lhs_, Token token, Expression* rhs_);

    Expression* lhs = nullptr;
    Token op;
    Expression* rhs = nullptr;

    DEFINE_ACCEPT_VISITOR
};

enum class BraceInitType {
    none,  // No brace initialization allowed
    field, // { name = value, ... } for structs
    kv,    // { key: value, ... } for maps
    list   // { value1, value2, ... } for arrays/vectors
};

struct BraceInitItem {
    Expression* key = nullptr;
    Expression* value = nullptr;
};

struct BraceInitLiteral : Expression {
    explicit BraceInitLiteral(MemoryResource* allocator);

    Token type;
    BraceInitType init_type;
    PVector<BraceInitItem> items;

    DEFINE_ACCEPT_VISITOR
};

struct UnaryExpression : Expression {
    UnaryExpression(Token token, Expression* rhs_);

    Token op;
    Expression* rhs = nullptr;

    DEFINE_ACCEPT_VISITOR
};

struct IdentifierExpression : Expression {
    explicit IdentifierExpression(Token token, MemoryResource* allocator);

    Token ident;
    PVector<Expression*> type_params;
    const Declaration* decl = nullptr;

    DEFINE_ACCEPT_VISITOR
};

inline bool PathExpression::is_simple_identifier() const
{
    return parts.size() == 1 && dynamic_cast<IdentifierExpression*>(parts[0]);
}

inline IdentifierExpression* PathExpression::single_identifier() const
{
    if (parts.size() != 1) { return nullptr; }
    return dynamic_cast<IdentifierExpression*>(parts[0]);
}

inline IdentifierExpression* PathExpression::last_identifier() const
{
    if (parts.empty()) { return nullptr; }
    return dynamic_cast<IdentifierExpression*>(parts.back());
}

inline PathExpression* as_path(Expression* expr)
{
    return dynamic_cast<PathExpression*>(expr);
}

inline IdentifierExpression* as_identifier(Expression* expr)
{
    if (auto ident = dynamic_cast<IdentifierExpression*>(expr)) {
        return ident;
    }
    if (auto path = dynamic_cast<PathExpression*>(expr)) {
        return path->single_identifier();
    }
    return nullptr;
}

bool is_mutable_lvalue(const Expression* expr);

struct CallExpression : Expression {
    explicit CallExpression(Expression* expr_, MemoryResource* allocator);

    Expression* expr = nullptr;
    PVector<Expression*> args;
    SourceRange arg_range;
    PVector<SourceRange> comma_ranges; // This is probably stupid
    Vector<TypeID> inferred_type_args; // If calling generic
    std::optional<IntrinsicId> intrinsic_id;
    const FunctionDefinition* resolved_func = nullptr;
    Script* resolved_provider = nullptr;

    DEFINE_ACCEPT_VISITOR
};

struct CastExpression : Expression {
    explicit CastExpression(MemoryResource* allocator);

    Expression* expr = nullptr;            // Expression to cast/check
    Token op;                              // TokenType::AS or TokenType::IS
    TypeExpression* target_type = nullptr; // Target type

    DEFINE_ACCEPT_VISITOR
};

struct EmptyExpression : Expression {
    DEFINE_ACCEPT_VISITOR
};

// ---- Statements ------------------------------------------------------------

struct Statement : public AstNode {
    virtual ~Statement() = default;
};

struct BlockStatement : public Statement {
    BlockStatement(MemoryResource* allocator);

    BlockStatement(BlockStatement&) = delete;
    BlockStatement& operator=(const BlockStatement&) = delete;

    PVector<Statement*> nodes;

    DEFINE_ACCEPT_VISITOR
};

struct EmptyStatement : public Statement {
    DEFINE_ACCEPT_VISITOR
};

struct ExprStatement : public Statement {
    Expression* expr = nullptr;

    DEFINE_ACCEPT_VISITOR
};

struct IfStatement : public Statement {
    Expression* expr = nullptr;
    BlockStatement* if_branch = nullptr;
    BlockStatement* else_branch = nullptr;

    DEFINE_ACCEPT_VISITOR
};

struct ForStatement : public Statement {
    AstNode* init = nullptr;
    Expression* check = nullptr;
    Expression* inc = nullptr;
    BlockStatement* block = nullptr;

    DEFINE_ACCEPT_VISITOR
};

struct ForEachStatement : public Statement {
    VarDecl* var = nullptr;
    VarDecl* key_var = nullptr;
    VarDecl* value_var = nullptr;
    Expression* collection = nullptr;
    BlockStatement* block = nullptr;

    bool is_map_iteration = false;
    TypeID element_type{};
    TypeID key_type{};
    TypeID value_type{};

    DEFINE_ACCEPT_VISITOR
};

struct JumpStatement : public Statement {
    JumpStatement(MemoryResource* allocator);

    Token op;
    PVector<Expression*> exprs;

    DEFINE_ACCEPT_VISITOR
};

struct LabelStatement : public Statement {
    LabelStatement(MemoryResource* allocator);

    Token type; // CASE or DEFAULT
    Expression* expr = nullptr;

    // Pattern matching fields
    PVector<VarDecl*> bindings;    // case Ok(x): or case Rect{w,h}:
    Expression* guard = nullptr;   // case Ok(x) if x > 0:
    bool is_pattern_match = false; // true for sum type patterns

    DEFINE_ACCEPT_VISITOR
};

struct SwitchStatement : public Statement {
    Expression* target;
    BlockStatement* block = nullptr;

    DEFINE_ACCEPT_VISITOR
};

/// Parsed type name - everything represented as expressions
struct TypeExpression : Expression {
    Expression* name = nullptr;               ///< Base type (IdentifierExpression for `int`, PathExpression for `vector.Vector`, etc.)
    std::vector<Expression*> params;          ///< Type parameters (any expression: types, integers, etc.)
    String qualified_name;                    ///< Fully qualified canonical name (set during resolution, e.g., "core.math.Vector", "int[10]")
    bool is_fixed_array = false;              ///< true for T[N] syntax (fixed-size inline array)
    bool is_function_type = false;            ///< true for fn(T1, T2): R syntax (function type)
    TypeExpression* fn_return_type = nullptr; ///< Return type for function types (params holds parameter types)

    SourcePosition range_start() const noexcept
    {
        return name ? name->range_.start : SourcePosition{};
    }

    String str() const;

    DEFINE_ACCEPT_VISITOR
};

/// Captured variable info for closures
struct CapturedVar {
    String name;
    uint32_t declaring_depth; ///< Function nesting depth where declared
    uint8_t source_register;  ///< Register in declaring function (or upvalue index if from parent closure)
    TypeID type;
    bool is_upvalue_in_parent; ///< True if parent also captures this (transitive capture)
};

/// Lambda expression: fn(x: int, y: int): int { body }
struct LambdaExpression : Expression {
    LambdaExpression(MemoryResource* allocator);

    PVector<VarDecl*> params;              ///< Parameter declarations with explicit types
    TypeExpression* return_type = nullptr; ///< Return type annotation
    BlockStatement* body = nullptr;        ///< Lambda body

    Vector<CapturedVar> captures;     ///< Captured variables (populated by resolver)
    uint32_t compiled_func_index = 0; ///< Index into module's function table (set by compiler)

    DEFINE_ACCEPT_VISITOR
};

// ---- Annotations -----------------------------------------------------------

struct AnnotationArg {
    Token name;
    Expression* value = nullptr;
    bool is_named = false;
};

struct Annotation {
    Token name;
    Vector<AnnotationArg> args;
    SourceRange range_;
};

// ---- Declaration -----------------------------------------------------------

struct Declaration : public Statement {
    TypeExpression* type;
    SourceRange range_selection_;
    StringView view;
    Vector<Annotation> annotations_;

    virtual String identifier() const = 0;
    virtual SourceRange range() const noexcept;
    virtual SourceRange selection_range() const noexcept;
};

struct FunctionDefinition : public Declaration {
    FunctionDefinition(MemoryResource* allocator);
    FunctionDefinition(FunctionDefinition&) = delete;
    FunctionDefinition& operator=(const FunctionDefinition&) = delete;

    Token identifier_;
    PVector<VarDecl*> params;
    TypeExpression* return_type = nullptr;
    BlockStatement* block = nullptr;
    Vector<String> type_params;

    virtual String identifier() const override { return String(identifier_.loc.view()); };
    bool is_generic() const noexcept { return !type_params.empty(); }

    DEFINE_ACCEPT_VISITOR
};

struct StructDecl : public Declaration {
    StructDecl(MemoryResource* allocator);

    PVector<Declaration*> decls;
    Vector<String> type_params;

    virtual String identifier() const override
    {
        if (auto ident = dynamic_cast<IdentifierExpression*>(type->name)) {
            return String(ident->ident.loc.view());
        }
        if (auto path = dynamic_cast<PathExpression*>(type->name)) {
            if (path->parts.size() == 1) {
                if (auto path_ident = dynamic_cast<IdentifierExpression*>(path->parts[0])) {
                    return String(path_ident->ident.loc.view());
                }
            }
        }
        return type->str();
    };

    bool is_generic() const noexcept { return !type_params.empty(); }
    const VarDecl* locate_member_decl(StringView name) const;

    DEFINE_ACCEPT_VISITOR
};

struct VarDecl : public Declaration {
    Token identifier_;
    Expression* init = nullptr;

    virtual String identifier() const override { return String(identifier_.loc.view()); };

    DEFINE_ACCEPT_VISITOR
};

/// Type alias: type Gold = int;
struct TypeAlias : public Declaration {
    TypeExpression* aliased_type;
    Vector<String> type_params;

    virtual String identifier() const override
    {
        if (auto ident = dynamic_cast<IdentifierExpression*>(type->name)) {
            return String(ident->ident.loc.view());
        }
        if (auto path = dynamic_cast<PathExpression*>(type->name)) {
            if (path->parts.size() == 1) {
                if (auto path_ident = dynamic_cast<IdentifierExpression*>(path->parts[0])) {
                    return String(path_ident->ident.loc.view());
                }
            }
        }
        return type->str();
    };

    bool is_generic() const { return !type_params.empty(); }

    DEFINE_ACCEPT_VISITOR
};

/// Newtype: type Feat(int);
struct NewtypeDecl : public Declaration {
    TypeExpression* wrapped_type;

    virtual String identifier() const override
    {
        if (auto ident = dynamic_cast<IdentifierExpression*>(type->name)) {
            return String(ident->ident.loc.view());
        }
        if (auto path = dynamic_cast<PathExpression*>(type->name)) {
            if (path->parts.size() == 1) {
                if (auto path_ident = dynamic_cast<IdentifierExpression*>(path->parts[0])) {
                    return String(path_ident->ident.loc.view());
                }
            }
        }
        return type->str();
    };

    DEFINE_ACCEPT_VISITOR
};

/// Variant in a sum type: Ok(int) or Err(string) or Point
struct VariantDecl : public AstNode {
    Token identifier_;
    Expression* payload = nullptr; // TypeExpression, StructDecl, or nullptr for unit variants

    String identifier() const { return String(identifier_.loc.view()); }
    bool is_unit() const { return payload == nullptr; }

    DEFINE_ACCEPT_VISITOR
};

/// Sum type: type Result = Ok(int) | Err(string);
struct SumDecl : public Declaration {
    SumDecl(MemoryResource* allocator);

    PVector<VariantDecl*> variants;
    Vector<String> type_params;

    virtual String identifier() const override
    {
        if (auto ident = dynamic_cast<IdentifierExpression*>(type->name)) {
            return String(ident->ident.loc.view());
        }
        if (auto path = dynamic_cast<PathExpression*>(type->name)) {
            if (path->parts.size() == 1) {
                if (auto path_ident = dynamic_cast<IdentifierExpression*>(path->parts[0])) {
                    return String(path_ident->ident.loc.view());
                }
            }
        }
        return type->str();
    };

    bool is_generic() const noexcept { return !type_params.empty(); }

    const VariantDecl* locate_variant(StringView name) const;

    DEFINE_ACCEPT_VISITOR
};

/// Opaque type: type Name;
struct OpaqueTypeDecl : public Declaration {
    virtual String identifier() const override
    {
        if (auto ident = dynamic_cast<IdentifierExpression*>(type->name)) {
            return String(ident->ident.loc.view());
        }
        if (auto path = dynamic_cast<PathExpression*>(type->name)) {
            if (path->parts.size() == 1) {
                if (auto path_ident = dynamic_cast<IdentifierExpression*>(path->parts[0])) {
                    return String(path_ident->ident.loc.view());
                }
            }
        }
        return type->str();
    };

    DEFINE_ACCEPT_VISITOR
};

/// List of comma separated declarations
struct DeclList : public Declaration {
    DeclList(MemoryResource* allocator);

    PVector<VarDecl*> decls;

    virtual String identifier() const override
    {
        Vector<String> identifiers;
        for (const auto decl : decls) {
            identifiers.push_back(decl->identifier());
        }
        return string::join(identifiers);
    };

    const VarDecl* locate_decl(StringView name) const;

    DEFINE_ACCEPT_VISITOR
};

// ---- Import Declarations ---------------------------------------------------

/// Base class for all import declarations
/// Imports are regular declarations that participate in normal scoping
struct ImportDecl : public Declaration {
    String module_path;              ///< Full module path (e.g., "core.math.vector")
    Script* loaded_module = nullptr; ///< Resolved during semantic analysis

    virtual String identifier() const override = 0;
};

/// Aliased import: import core.math.vector as vec;
/// Access via: vec.length(v)
struct AliasedImportDecl : public ImportDecl {
    Token alias; ///< Alias token (e.g., "vec")

    virtual String identifier() const override { return String(alias.loc.view()); }

    DEFINE_ACCEPT_VISITOR
};

/// Selective import: from core.math.vector import { Vector, Point };
/// Access directly: Vector, Point
/// This doesn't add a single symbol but multiple, so identifier() returns empty
struct SelectiveImportDecl : public ImportDecl {
    Vector<Token> imported_symbols; ///< List of imported symbol names

    virtual String identifier() const override { return ""; }

    DEFINE_ACCEPT_VISITOR
};

/// Abstracts a comment
struct Comment {

    void append(StringView comment, SourceLocation range)
    {
        if (comment_.empty()) {
            comment_ = String(comment);
            range_ = merge_source_location(range_, range);
        } else {
            comment_ = fmt::format("{}\n{}", comment_, comment);
            range_ = merge_source_location(range_, range);
        }
    }

    SourceLocation range_;
    String comment_;
};

struct Ast {
    Ast(Context* ctx);
    Ast(const Ast&) = delete;
    Ast(Ast&&) = default;
    Ast& operator=(const Ast&) = delete;
    Ast& operator=(Ast&&) = default;

    Context* ctx_;
    Vector<Statement*> decls;
    Vector<ImportDecl*> imports; ///< Import declarations (QualifiedImportDecl, AliasedImportDecl, SelectiveImportDecl)
    std::unordered_map<String, String> defines;
    Vector<Comment> comments;
    Vector<size_t> line_map;

    size_t node_count_ = 0;

    template <typename T, typename... Args>
    T* create_node(Args&&... args)
    {
        if (ctx_ && ctx_->limits.max_ast_nodes != 0) {
            if (node_count_ + 1 > ctx_->limits.max_ast_nodes) {
                throw ast_limit_error("Smalls AST node limit exceeded");
            }
        }
        ++node_count_;
        T* node = static_cast<T*>(ctx_->scope->alloc_obj<T>(std::forward<Args>(args)...));
        return node;
    }

    void accept(BaseVisitor* visitor)
    {
        visitor->visit(this);
    }

    /// Finds first comment that the source range of which ends on ``line`` or ``line`` - 1
    StringView find_comment(size_t line) const noexcept;
};

// == Utilities ===============================================================

/// Converts module path syntax to resource path
/// @param module_path Dot-separated module path (e.g., "core.math.vector")
/// @return Resource path with slashes (e.g., "core/math/vector")
inline String module_path_to_resource_path(String module_path)
{
    for (char& c : module_path) {
        if (c == '.') { c = '/'; }
    }
    return module_path;
}

} // namespace nw::smalls
