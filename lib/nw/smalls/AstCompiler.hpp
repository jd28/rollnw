#pragma once

#include "Ast.hpp"
#include "Bytecode.hpp"
#include "runtime.hpp"

#include <unordered_map>

namespace nw::smalls {

// Forward Decls
struct Script;
struct Context;
struct Export;
struct ExportConstValue;

// == Register Allocator ======================================================
// ============================================================================

struct RegisterAllocator {
    static constexpr uint16_t max_registers = 256;

    uint16_t next_reg = 0;
    uint16_t max_reg = 0;

    // Fixed-size stack for free registers (no heap allocation)
    uint8_t free_stack[max_registers];
    uint16_t free_count = 0;

    uint8_t allocate();
    uint8_t allocate_contiguous(uint8_t count);
    void free(uint8_t reg);
    void mark_used(uint8_t reg);
    uint8_t high_water_mark() const { return static_cast<uint8_t>(max_reg); }
    void reset();
};

// == Control Scope (for break/continue/switch) ===============================
// ============================================================================

struct ControlScope {
    bool is_loop = false;
    uint32_t start_pc = 0; // Loop start (for continue)

    Vector<uint32_t> break_jumps;
    Vector<uint32_t> continue_jumps; // Only for loops

    // For Switch
    Vector<std::pair<Expression*, uint32_t>> cases; // Case expression and PC
    uint32_t default_pc = UINT32_MAX;

    // For Sum Type Pattern Matching
    bool is_sum_switch = false;
    uint8_t sum_target_reg = 0;
    TypeID sum_type_id{};
    Vector<std::pair<LabelStatement*, uint32_t>> pattern_cases; // LabelStatement and PC
};

// == Variable Mapping ========================================================
// ============================================================================

struct VariableInfo {
    uint8_t register_index;
    bool is_parameter;
};

// == AST Compiler ============================================================
// ============================================================================

struct AstCompiler : public BaseVisitor {
    Script* script_ = nullptr;
    BytecodeModule* module_ = nullptr;
    Runtime* runtime_ = nullptr;
    Context* ctx_ = nullptr;

    CompiledFunction* current_func_ = nullptr;
    RegisterAllocator registers_;
    Vector<ControlScope> control_stack_;

    // Maps variable names to register indices for current function
    std::unordered_map<String, VariableInfo> local_vars_;

    struct ModuleGlobal {
        uint16_t slot;
        bool is_const;
    };
    std::unordered_map<String, ModuleGlobal> module_globals_;

    // Closure support: maps captured variable names to upvalue indices
    std::unordered_map<String, uint8_t> upvalue_indices_;
    LambdaExpression* current_lambda_ = nullptr;
    uint32_t lambda_counter_ = 0;

    uint32_t generic_instantiation_count_ = 0;

    bool failed_ = false;
    String error_message_;

    // Set true by JumpStatement (return/break/continue) to signal that subsequent
    // statements in the same block are unreachable (dead code elimination).
    bool block_terminated_ = false;

    // Result of compiling an expression (which register holds the value)
    uint8_t result_reg_ = 0;

    // Current source location for debug info
    SourceRange current_source_range_;

    explicit AstCompiler(Script* script, BytecodeModule* module, Runtime* runtime, Context* ctx);

    // Main compilation entry point
    bool compile();

    // Compile single function
    bool compile_function(FunctionDefinition* func);

    // Compile an instantiated generic function (function body only)
    bool compile_instantiated(CompiledFunction* compiled, FunctionDefinition* func);

    // Compile a lambda expression as a separate function, returns function index in module
    uint16_t compile_lambda(LambdaExpression* lambda);

    // Check if a variable is captured (accessed via upvalue)
    bool is_captured_variable(StringView name) const;
    uint8_t get_upvalue_index(StringView name) const;

    // == Instruction Emission ================================================

    void emit(Instruction instr);
    void emit_abc(Opcode op, uint8_t a, uint8_t b, uint8_t c);
    void emit_abx(Opcode op, uint8_t a, uint16_t bx);
    void emit_asbx(Opcode op, uint8_t a, int16_t sbx);
    uint32_t emit_jump(Opcode op, int32_t offset = 0); // Returns instruction index
    void patch_jump(uint32_t idx, int32_t target_pc);

    // == Constant Pool Management ============================================

    uint32_t add_constant_int(int32_t val);
    uint32_t add_constant_float(float val);
    uint32_t add_constant_string(StringView str);

    // == Helper Methods ======================================================

    void fail(StringView message);
    uint8_t allocate_local(StringView name, bool is_parameter);
    uint8_t get_local_register(StringView name);

    void emit_field_get(uint8_t dest, uint8_t struct_reg, uint32_t offset, TypeID type_id);
    void emit_field_set(uint8_t struct_reg, uint32_t offset, TypeID type_id, uint8_t val_reg);

    // Stack value type helpers
    bool is_value_type(TypeID tid) const;
    void emit_stack_field_get(uint8_t dest, uint8_t base_reg, uint32_t offset, TypeID type_id);
    void emit_stack_field_set(uint8_t base_reg, uint32_t offset, TypeID type_id, uint8_t val_reg);

    // Converts TokenType to Opcode for binary operations
    Opcode token_to_binary_opcode(TokenType type);
    Opcode token_to_comparison_opcode(TokenType type);

    // Constant folding helper: if expr is a compile-time constant, emit a
    // single load instruction and set result_reg_.  Returns true on success.
    bool try_emit_const(Expression* expr);
    bool try_emit_export_const(const ExportConstValue& exported);
    bool try_emit_imported_const_from_provider(StringView name, const Export& imported);
    bool try_materialize_export_const(Script* provider, const Export& exp, ExportConstValue& out) const;
    bool try_value_to_export_const(const Value& value, TypeID type_id, ExportConstValue& out) const;
    TypeID unwrap_newtype_base_type(TypeID type_id) const;

    // Emits CALLEXT for a script-defined operator function
    // Returns the result register in out_result
    bool emit_script_operator_call(const ScriptFunctionRef& ref,
        const Vector<uint8_t>& arg_regs, uint8_t& out_result);

    // == Visitor Methods (BaseVisitor interface) ============================

    virtual void visit(Ast* ast) override;

    // Declarations
    virtual void visit(FunctionDefinition* decl) override;
    virtual void visit(VarDecl* decl) override;
    virtual void visit(StructDecl* decl) override;
    virtual void visit(SumDecl* decl) override;
    virtual void visit(VariantDecl* decl) override;
    virtual void visit(TypeAlias* decl) override;
    virtual void visit(NewtypeDecl* decl) override;
    virtual void visit(OpaqueTypeDecl* decl) override;
    virtual void visit(AliasedImportDecl* decl) override;
    virtual void visit(SelectiveImportDecl* decl) override;

    // Statements
    virtual void visit(ExprStatement* stmt) override;
    virtual void visit(BlockStatement* stmt) override;
    virtual void visit(IfStatement* stmt) override;
    virtual void visit(ForStatement* stmt) override;
    virtual void visit(ForEachStatement* stmt) override;
    virtual void visit(SwitchStatement* stmt) override;
    virtual void visit(LabelStatement* stmt) override;
    virtual void visit(JumpStatement* stmt) override;
    virtual void visit(DeclList* stmt) override;
    virtual void visit(EmptyStatement* stmt) override;

    // Expressions
    virtual void visit(BinaryExpression* expr) override;
    virtual void visit(ComparisonExpression* expr) override;
    virtual void visit(LogicalExpression* expr) override;
    virtual void visit(UnaryExpression* expr) override;
    virtual void visit(AssignExpression* expr) override;
    virtual void visit(ConditionalExpression* expr) override;
    virtual void visit(IdentifierExpression* expr) override;
    virtual void visit(PathExpression* expr) override;
    virtual void visit(IndexExpression* expr) override;
    virtual void visit(CallExpression* expr) override;
    virtual void visit(CastExpression* expr) override;
    virtual void visit(LiteralExpression* expr) override;
    virtual void visit(TupleLiteral* expr) override;
    virtual void visit(BraceInitLiteral* expr) override;
    virtual void visit(FStringExpression* expr) override;
    virtual void visit(GroupingExpression* expr) override;
    virtual void visit(TypeExpression* expr) override;
    virtual void visit(EmptyExpression* expr) override;
    virtual void visit(LambdaExpression* expr) override;
};

} // namespace nw::smalls
