#include "AstCompiler.hpp"

#include "Ast.hpp"
#include "AstConstEvaluator.hpp"
#include "Context.hpp"
#include "Smalls.hpp"

#include <fmt/format.h>
#include <limits>

namespace nw::smalls {

namespace {

TypeID resolve_newtype_type_id(Runtime* runtime, const Export* exp)
{
    if (!runtime || !exp) {
        return invalid_type_id;
    }

    if (exp->kind == Export::Kind::type) {
        TypeID tid = exp->type_id;
        if (tid == invalid_type_id && exp->decl) {
            tid = exp->decl->type_id_;
        }
        if (tid != invalid_type_id) {
            const Type* t = runtime->get_type(tid);
            if (t && t->type_kind == TK_newtype) {
                return tid;
            }
        }
    }

    return invalid_type_id;
}

TypeID resolve_function_value_type(Runtime* runtime, const FunctionDefinition* func)
{
    if (!runtime || !func) {
        return invalid_type_id;
    }

    Vector<TypeID> param_types;
    param_types.reserve(func->params.size());
    for (const auto* param : func->params) {
        if (!param || param->type_id_ == invalid_type_id) {
            return invalid_type_id;
        }
        param_types.push_back(param->type_id_);
    }

    TypeID return_type = func->return_type ? func->return_type->type_id_ : runtime->void_type();
    if (return_type == invalid_type_id) {
        return invalid_type_id;
    }

    return runtime->register_function_type(param_types, return_type);
}

} // namespace

// == RegisterAllocator =======================================================

uint8_t RegisterAllocator::allocate()
{
    uint8_t reg;

    if (free_count > 0) {
        reg = free_stack[--free_count];
    } else {
        if (next_reg >= RegisterAllocator::max_registers) {
            throw std::runtime_error("Register overflow: exceeded 256 registers");
        }
        reg = static_cast<uint8_t>(next_reg++);
    }

    if (reg >= max_reg) {
        max_reg = static_cast<uint16_t>(reg) + 1;
    }

    return reg;
}

void RegisterAllocator::free(uint8_t reg)
{
    // If freeing the highest allocated register, shrink next_reg instead
    // This allows contiguous allocations to reuse the space
    if (next_reg > 0 && reg == next_reg - 1) {
        --next_reg;
        // Also pop any free_stack entries that are now at the top
        while (next_reg > 0 && free_count > 0 && free_stack[free_count - 1] == next_reg - 1) {
            --free_count;
            --next_reg;
        }
    } else if (free_count < RegisterAllocator::max_registers) {
        free_stack[free_count++] = reg;
    }
}

void RegisterAllocator::mark_used(uint8_t reg)
{
    if (reg >= next_reg) {
        next_reg = static_cast<uint16_t>(reg) + 1;
    }
    if (reg >= max_reg) {
        max_reg = static_cast<uint16_t>(reg) + 1;
    }
}

void RegisterAllocator::reset()
{
    next_reg = 0;
    max_reg = 0;
    free_count = 0;
}

uint8_t RegisterAllocator::allocate_contiguous(uint8_t count)
{
    if (next_reg + count > RegisterAllocator::max_registers) {
        throw std::runtime_error("Register overflow: exceeded 256 registers");
    }
    uint8_t start = static_cast<uint8_t>(next_reg);
    next_reg = static_cast<uint16_t>(next_reg + count);
    if (next_reg > max_reg) { max_reg = next_reg; }
    return start;
}

// == AstCompiler =============================================================

AstCompiler::AstCompiler(Script* script, BytecodeModule* module, Runtime* runtime, Context* ctx)
    : script_(script)
    , module_(module)
    , runtime_(runtime)
    , ctx_(ctx)
{
}

// ---------------------------------------------------------------------------
// Constant folding helpers
// ---------------------------------------------------------------------------

static bool const_is_truthy(const Value& val, Runtime* rt)
{
    if (val.type_id == rt->bool_type()) { return val.data.bval; }
    if (val.type_id == rt->int_type()) { return val.data.ival != 0; }
    if (val.type_id == rt->float_type()) { return val.data.fval != 0.0f; }
    return false;
}

/// If expr is a compile-time constant, emit a single load instruction and set
/// result_reg_.  Returns true on success; false means fall through to normal
/// compilation.
bool AstCompiler::try_emit_const(Expression* expr)
{
    if (!expr->is_const_) { return false; }

    AstConstEvaluator eval(script_, expr);
    if (eval.failed_ || eval.result_.empty()) { return false; }

    Value result = eval.result_.back();

    uint8_t reg = registers_.allocate();

    if (result.type_id == runtime_->int_type()) {
        int32_t val = result.data.ival;
        if (val >= -32768 && val <= 32767) {
            emit_asbx(Opcode::LOADI, reg, static_cast<int16_t>(val));
        } else {
            uint32_t k_idx = add_constant_int(val);
            if (k_idx > 65535) {
                fail("Constant pool overflow");
                return false;
            }
            emit_abx(Opcode::LOADK, reg, static_cast<uint16_t>(k_idx));
        }
    } else if (result.type_id == runtime_->float_type()) {
        uint32_t k_idx = add_constant_float(result.data.fval);
        if (k_idx > 65535) {
            fail("Constant pool overflow");
            return false;
        }
        emit_abx(Opcode::LOADK, reg, static_cast<uint16_t>(k_idx));
    } else if (result.type_id == runtime_->bool_type()) {
        emit_abc(Opcode::LOADB, reg, result.data.bval ? 1 : 0, 0);
    } else if (result.type_id == runtime_->string_type()) {
        StringView sv = runtime_->get_string_view(result.data.hptr);
        uint32_t k_idx = add_constant_string(sv);
        if (k_idx > 65535) {
            fail("Constant pool overflow");
            return false;
        }
        emit_abx(Opcode::LOADK, reg, static_cast<uint16_t>(k_idx));
    } else {
        registers_.free(reg);
        return false;
    }

    result_reg_ = reg;
    return true;
}

TypeID AstCompiler::unwrap_newtype_base_type(TypeID type_id) const
{
    const Type* type = runtime_->get_type(type_id);
    while (type && type->type_kind == TK_newtype
        && !type->type_params.empty() && type->type_params[0].is<TypeID>()) {
        type_id = type->type_params[0].as<TypeID>();
        type = runtime_->get_type(type_id);
    }
    return type_id;
}

bool AstCompiler::try_value_to_export_const(const Value& value, TypeID type_id, ExportConstValue& out) const
{
    TypeID base_type = unwrap_newtype_base_type(type_id);

    if (base_type == runtime_->int_type()) {
        out.value = value.data.ival;
        return true;
    }
    if (base_type == runtime_->float_type()) {
        out.value = value.data.fval;
        return true;
    }
    if (base_type == runtime_->bool_type()) {
        out.value = value.data.bval;
        return true;
    }
    if (base_type == runtime_->string_type()) {
        out.value = String(runtime_->get_string_view(value.data.hptr));
        return true;
    }

    return false;
}

bool AstCompiler::try_materialize_export_const(Script* provider, const Export& exp, ExportConstValue& out) const
{
    auto* var_decl = dynamic_cast<const VarDecl*>(exp.decl);
    if (!provider || !var_decl || !var_decl->init) {
        return false;
    }

    AstConstEvaluator eval(provider, var_decl->init);
    if (eval.failed_ || eval.result_.empty()) {
        return false;
    }

    const Value value = eval.result_.back();
    return try_value_to_export_const(value, value.type_id, out);
}

bool AstCompiler::try_emit_export_const(const ExportConstValue& exported)
{
    uint8_t result = registers_.allocate();

    if (const auto* v = std::get_if<int32_t>(&exported.value)) {
        if (*v >= -32768 && *v <= 32767) {
            emit_asbx(Opcode::LOADI, result, static_cast<int16_t>(*v));
        } else {
            uint32_t k_idx = add_constant_int(*v);
            if (k_idx > 65535) {
                registers_.free(result);
                fail("Constant pool overflow");
                return false;
            }
            emit_abx(Opcode::LOADK, result, static_cast<uint16_t>(k_idx));
        }
        result_reg_ = result;
        return true;
    }

    if (const auto* v = std::get_if<float>(&exported.value)) {
        uint32_t k_idx = add_constant_float(*v);
        if (k_idx > 65535) {
            registers_.free(result);
            fail("Constant pool overflow");
            return false;
        }
        emit_abx(Opcode::LOADK, result, static_cast<uint16_t>(k_idx));
        result_reg_ = result;
        return true;
    }

    if (const auto* v = std::get_if<bool>(&exported.value)) {
        emit_abc(Opcode::LOADB, result, *v ? 1 : 0, 0);
        result_reg_ = result;
        return true;
    }

    if (const auto* v = std::get_if<String>(&exported.value)) {
        uint32_t k_idx = add_constant_string(*v);
        if (k_idx > 65535) {
            registers_.free(result);
            fail("Constant pool overflow");
            return false;
        }
        emit_abx(Opcode::LOADK, result, static_cast<uint16_t>(k_idx));
        result_reg_ = result;
        return true;
    }

    registers_.free(result);
    return false;
}

bool AstCompiler::try_emit_imported_const_from_provider(StringView name, const Export& imported)
{
    if (imported.provider_module.empty()) {
        return false;
    }

    Script* provider = runtime_->get_module(imported.provider_module);
    if (!provider) {
        return false;
    }

    auto provider_export = provider->exports().find(String(name));
    if (!provider_export) {
        return false;
    }

    if (try_emit_export_const(provider_export->const_value)) {
        return true;
    }

    ExportConstValue materialized;
    if (!try_materialize_export_const(provider, *provider_export, materialized)) {
        return false;
    }

    return try_emit_export_const(materialized);
}

bool AstCompiler::compile()
{
    if (!script_ || !module_ || !runtime_) {
        fail("Invalid compiler state");
        return false;
    }

    // Pass 0: Assign global slots for top-level var/const declarations
    uint16_t global_slot = 0;
    for (auto* decl : script_->ast().decls) {
        if (auto* var = dynamic_cast<VarDecl*>(decl)) {
            module_globals_[String(var->identifier())] = {global_slot++, var->is_const_};
        } else if (auto* decl_list = dynamic_cast<DeclList*>(decl)) {
            for (auto& d : decl_list->decls) {
                module_globals_[String(d->identifier())] = {global_slot++, d->is_const_};
            }
        }
    }
    module_->global_count = global_slot;

    // Pass 1: Create function skeletons and add to module
    for (auto* decl : script_->ast().decls) {
        if (auto* func = dynamic_cast<FunctionDefinition*>(decl)) {
            if (func->is_generic()) { continue; }
            auto* compiled = new CompiledFunction(func->identifier());
            compiled->param_count = static_cast<uint8_t>(func->params.size());
            compiled->register_count = compiled->param_count;
            compiled->return_type = func->return_type ? func->return_type->type_id_ : runtime_->void_type();
            compiled->function_type = resolve_function_value_type(runtime_, func);
            compiled->source_ast = func;
            module_->add_function(compiled);
        }
    }

    // Also create __init skeleton if there are globals
    if (module_->global_count > 0) {
        auto* init_func = new CompiledFunction(String("__init"));
        init_func->param_count = 0;
        init_func->return_type = runtime_->void_type();
        module_->add_function(init_func);
    }

    // Pass 2: Compile __init body from top-level var/const initializers
    if (module_->global_count > 0) {
        CompiledFunction* init_func = const_cast<CompiledFunction*>(module_->get_function("__init"));
        current_func_ = init_func;
        registers_.reset();
        local_vars_.clear();

        for (auto* decl : script_->ast().decls) {
            if (auto* var = dynamic_cast<VarDecl*>(decl)) {
                auto git = module_globals_.find(String(var->identifier()));
                if (git == module_globals_.end()) { continue; }
                uint16_t slot = git->second.slot;

                if (var->init) {
                    var->init->accept(this);
                    uint8_t src = result_reg_;
                    emit_abx(Opcode::SETGLOBAL, src, slot);
                    registers_.free(src);
                } else {
                    uint8_t tmp = registers_.allocate();
                    emit_abc(Opcode::LOADNIL, tmp, 0, 0);
                    emit_abx(Opcode::SETGLOBAL, tmp, slot);
                    registers_.free(tmp);
                }
            }
        }

        emit(Instruction::make_abc(Opcode::CLOSEUPVALS, 0, 0, 0));
        emit(Instruction::make_abc(Opcode::RETVOID, 0, 0, 0));
        init_func->register_count = registers_.high_water_mark();
        current_func_ = nullptr;
    }

    // Pass 3: Compile function bodies
    for (auto* decl : script_->ast().decls) {
        if (auto* func = dynamic_cast<FunctionDefinition*>(decl)) {
            if (func->is_generic()) { continue; }
            if (!compile_function(func)) { return false; }
        }
    }

    return !failed_;
}

bool AstCompiler::compile_function(FunctionDefinition* func)
{
    // Retrieve the pre-created function
    const CompiledFunction* const_cf = module_->get_function(func->identifier());
    if (!const_cf) {
        fail("Function skeleton missing");
        return false;
    }

    // Check for [[native]] annotation
    for (const auto& anno : func->annotations_) {
        if (anno.name.loc.view() == "native" || anno.name.loc.view() == "intrinsic") {
            return !failed_;
        }
    }

    CompiledFunction* compiled = const_cast<CompiledFunction*>(const_cf);
    compiled->function_type = resolve_function_value_type(runtime_, func);

    current_func_ = compiled;
    registers_.reset();
    local_vars_.clear();

    // Allocate registers for parameters (r0, r1, r2, ...)
    for (size_t i = 0; i < func->params.size(); ++i) {
        auto* param = func->params[i];
        String param_name(param->identifier());
        uint8_t reg = static_cast<uint8_t>(i);
        registers_.mark_used(reg);
        local_vars_[param_name] = VariableInfo{reg, true};
    }

    // Compile function body
    if (func->block) {
        func->block->accept(this);
    }

    // If function didn't end with return, emit RETVOID
    if (current_func_->instructions.empty() || current_func_->instructions.back().opcode() != Opcode::RET) {
        emit(Instruction::make_abc(Opcode::CLOSEUPVALS, 0, 0, 0));
        emit(Instruction::make_abc(Opcode::RETVOID, 0, 0, 0));
    }

    // Set register count
    compiled->register_count = registers_.high_water_mark();

    // Function is already in module

    current_func_ = nullptr;
    return !failed_;
}

bool AstCompiler::compile_instantiated(CompiledFunction* compiled, FunctionDefinition* func)
{
    if (compiled) {
        compiled->function_type = resolve_function_value_type(runtime_, func);
    }

    current_func_ = compiled;
    registers_.reset();
    local_vars_.clear();

    for (size_t i = 0; i < func->params.size(); ++i) {
        auto* param = func->params[i];
        String param_name(param->identifier());
        uint8_t reg = static_cast<uint8_t>(i);
        registers_.mark_used(reg);
        local_vars_[param_name] = VariableInfo{reg, true};
    }

    if (func->block) {
        func->block->accept(this);
    }

    if (current_func_->instructions.empty() || current_func_->instructions.back().opcode() != Opcode::RET) {
        emit(Instruction::make_abc(Opcode::CLOSEUPVALS, 0, 0, 0));
        emit(Instruction::make_abc(Opcode::RETVOID, 0, 0, 0));
    }

    compiled->register_count = registers_.high_water_mark();
    current_func_ = nullptr;
    return !failed_;
}

// == Source Location Tracking ================================================

struct SourceTracker {
    AstCompiler* compiler;
    SourceRange prev_range;

    SourceTracker(AstCompiler* c, const SourceRange& new_range)
        : compiler(c)
        , prev_range(c->current_source_range_)
    {
        compiler->current_source_range_ = new_range;
    }

    ~SourceTracker()
    {
        compiler->current_source_range_ = prev_range;
    }
};

#define TRACK_SOURCE(node) SourceTracker __tracker(this, (node)->range_)

// == Instruction Emission ====================================================

void AstCompiler::emit(Instruction instr)
{
    if (!current_func_) {
        fail("No current function");
        return;
    }
    current_func_->instructions.push_back(instr);

    if (runtime_->diagnostic_config().debug_level != DebugLevel::none) {
        SourceLocation loc;
        loc.range = current_source_range_;
        current_func_->debug_locations.push_back(loc);
    }
}

void AstCompiler::emit_abc(Opcode op, uint8_t a, uint8_t b, uint8_t c)
{
    emit(Instruction::make_abc(op, a, b, c));
}

void AstCompiler::emit_abx(Opcode op, uint8_t a, uint16_t bx)
{
    emit(Instruction::make_abx(op, a, bx));
}

void AstCompiler::emit_asbx(Opcode op, uint8_t a, int16_t sbx)
{
    emit(Instruction::make_asbx(op, a, sbx));
}

uint32_t AstCompiler::emit_jump(Opcode op, int32_t offset)
{
    uint32_t idx = static_cast<uint32_t>(current_func_->instructions.size());
    emit(Instruction::make_jump(op, offset));
    return idx;
}

void AstCompiler::patch_jump(uint32_t instr_idx, int32_t target_pc)
{
    if (!current_func_ || instr_idx >= current_func_->instructions.size()) {
        fail("Invalid jump patch");
        return;
    }

    // Calculate relative offset from instruction after the jump
    int32_t offset = target_pc - static_cast<int32_t>(instr_idx + 1);

    // Patch the instruction
    Instruction& instr = current_func_->instructions[instr_idx];
    Opcode op = instr.opcode();

    if (op == Opcode::JMP) {
        instr = Instruction::make_jump(op, offset);
    } else if (op == Opcode::JMPF || op == Opcode::JMPT) {
        // For conditional jumps, use sBx format
        if (offset < -32768 || offset > 32767) {
            fail(fmt::format("Jump offset {} out of range for conditional jump", offset));
            return;
        }
        uint8_t cond_reg = instr.arg_a(); // Preserve condition register
        instr = Instruction::make_asbx(op, cond_reg, static_cast<int16_t>(offset));
    } else {
        fail("Attempting to patch non-jump instruction");
    }
}

// == Constant Pool Management ================================================

uint32_t AstCompiler::add_constant_int(int32_t val)
{
    Constant c = Constant::make_int(val);
    c.type_id = runtime_->int_type();
    return module_->add_constant(c);
}

uint32_t AstCompiler::add_constant_float(float val)
{
    Constant c = Constant::make_float(val);
    c.type_id = runtime_->float_type();
    return module_->add_constant(c);
}

uint32_t AstCompiler::add_constant_string(StringView str)
{
    uint32_t string_idx = module_->add_string(str);
    Constant c = Constant::make_string(string_idx);
    c.type_id = runtime_->string_type();
    return module_->add_constant(c);
}

// == Helper Methods ==========================================================

void AstCompiler::fail(StringView message)
{
    if (!failed_) {
        failed_ = true;
        error_message_ = String(message);
    }
}

uint8_t AstCompiler::allocate_local(StringView name, bool is_parameter)
{
    auto it = local_vars_.find(String(name));
    if (it != local_vars_.end()) {
        return it->second.register_index;
    }

    uint8_t reg = registers_.allocate();
    local_vars_[String(name)] = VariableInfo{reg, is_parameter};
    return reg;
}

uint8_t AstCompiler::get_local_register(StringView name)
{
    auto it = local_vars_.find(String(name));
    if (it == local_vars_.end()) {
        fail(fmt::format("Undefined variable: {}", name));
        return 0;
    }
    return it->second.register_index;
}

void AstCompiler::emit_field_get(uint8_t dest, uint8_t struct_reg, uint32_t offset, TypeID type_id)
{
    auto& rt = *runtime_;
    bool is_heap = (type_id != rt.int_type() && type_id != rt.float_type() && type_id != rt.bool_type() && type_id != rt.string_type() && type_id != rt.object_type());
    uint32_t ref_idx = module_->add_field_ref(offset, type_id);

    if (is_heap) {
        if (ref_idx <= 255) {
            emit_abc(Opcode::FIELDGETH, dest, struct_reg, static_cast<uint8_t>(ref_idx));
        } else {
            // Fallback to register-based RefIdx
            uint8_t idx_reg = registers_.allocate();
            if (ref_idx <= 32767) {
                emit_asbx(Opcode::LOADI, idx_reg, static_cast<int16_t>(ref_idx));
            } else {
                uint32_t k_idx = add_constant_int(static_cast<int32_t>(ref_idx));
                emit_abx(Opcode::LOADK, idx_reg, static_cast<uint16_t>(k_idx));
            }
            emit_abc(Opcode::FIELDGETH_R, dest, struct_reg, idx_reg);
            registers_.free(idx_reg);
        }
    } else {
        // Primitives use direct Offset
        Opcode op;
        Opcode op_r;
        if (type_id == rt.int_type()) {
            op = Opcode::FIELDGETI;
            op_r = Opcode::FIELDGETI_R;
        } else if (type_id == rt.float_type()) {
            op = Opcode::FIELDGETF;
            op_r = Opcode::FIELDGETF_R;
        } else if (type_id == rt.bool_type()) {
            op = Opcode::FIELDGETB;
            op_r = Opcode::FIELDGETB_R;
        } else if (type_id == rt.string_type()) {
            op = Opcode::FIELDGETS;
            op_r = Opcode::FIELDGETS_R;
        } else {
            op = Opcode::FIELDGETO;
            op_r = Opcode::FIELDGETO_R;
        } // Object

        if (ref_idx <= 255) {
            emit_abc(op, dest, struct_reg, static_cast<uint8_t>(ref_idx));
        } else {
            // Fallback to register-based ref_idx
            uint8_t off_reg = registers_.allocate();
            if (ref_idx <= 32767) {
                emit_asbx(Opcode::LOADI, off_reg, static_cast<int16_t>(ref_idx));
            } else {
                uint32_t k_idx = add_constant_int(static_cast<int32_t>(ref_idx));
                emit_abx(Opcode::LOADK, off_reg, static_cast<uint16_t>(k_idx));
            }
            emit_abc(op_r, dest, struct_reg, off_reg);
            registers_.free(off_reg);
        }
    }
}

void AstCompiler::emit_field_set(uint8_t struct_reg, uint32_t offset, TypeID type_id, uint8_t val_reg)
{
    auto& rt = *runtime_;
    bool is_heap = (type_id != rt.int_type() && type_id != rt.float_type() && type_id != rt.bool_type() && type_id != rt.string_type() && type_id != rt.object_type());
    uint32_t ref_idx = module_->add_field_ref(offset, type_id);

    if (is_heap) {
        if (ref_idx <= 255) {
            emit_abc(Opcode::FIELDSETH, struct_reg, static_cast<uint8_t>(ref_idx), val_reg);
        } else {
            uint8_t idx_reg = registers_.allocate();
            if (ref_idx <= 32767) {
                emit_asbx(Opcode::LOADI, idx_reg, static_cast<int16_t>(ref_idx));
            } else {
                uint32_t k_idx = add_constant_int(static_cast<int32_t>(ref_idx));
                emit_abx(Opcode::LOADK, idx_reg, static_cast<uint16_t>(k_idx));
            }
            emit_abc(Opcode::FIELDSETH_R, struct_reg, idx_reg, val_reg);
            registers_.free(idx_reg);
        }
    } else {
        Opcode op;
        Opcode op_r;
        if (type_id == rt.int_type()) {
            op = Opcode::FIELDSETI;
            op_r = Opcode::FIELDSETI_R;
        } else if (type_id == rt.float_type()) {
            op = Opcode::FIELDSETF;
            op_r = Opcode::FIELDSETF_R;
        } else if (type_id == rt.bool_type()) {
            op = Opcode::FIELDSETB;
            op_r = Opcode::FIELDSETB_R;
        } else if (type_id == rt.string_type()) {
            op = Opcode::FIELDSETS;
            op_r = Opcode::FIELDSETS_R;
        } else {
            op = Opcode::FIELDSETO;
            op_r = Opcode::FIELDSETO_R;
        }

        if (ref_idx <= 255) {
            emit_abc(op, struct_reg, static_cast<uint8_t>(ref_idx), val_reg);
        } else {
            uint8_t off_reg = registers_.allocate();
            if (ref_idx <= 32767) {
                emit_asbx(Opcode::LOADI, off_reg, static_cast<int16_t>(ref_idx));
            } else {
                uint32_t k_idx = add_constant_int(static_cast<int32_t>(ref_idx));
                emit_abx(Opcode::LOADK, off_reg, static_cast<uint16_t>(k_idx));
            }
            emit_abc(op_r, struct_reg, off_reg, val_reg);
            registers_.free(off_reg);
        }
    }
}

// == Stack Value Type Helpers ================================================

bool AstCompiler::is_value_type(TypeID tid) const
{
    const Type* type = runtime_->get_type(tid);
    if (!type) return false;

    // T[N] fixed arrays are always value types
    if (type->type_kind == TK_fixed_array) {
        return true;
    }

    // Check for [[value_type]] annotation on struct
    if (type->type_kind == TK_struct) {
        auto struct_id = type->type_params[0].as<StructID>();
        const StructDef* def = runtime_->type_table_.get(struct_id);
        if (def && def->decl) {
            for (const auto& ann : def->decl->annotations_) {
                if (ann.name.loc.view() == "value_type") {
                    return true;
                }
            }
        }
    }

    // Check for [[value_type]] annotation on sum type
    if (type->type_kind == TK_sum) {
        auto sum_id = type->type_params[0].as<SumID>();
        const SumDef* def = runtime_->type_table_.get(sum_id);
        if (def && def->decl) {
            for (const auto& ann : def->decl->annotations_) {
                if (ann.name.loc.view() == "value_type") {
                    return true;
                }
            }
        }
    }

    return false;
}

void AstCompiler::emit_stack_field_get(uint8_t dest, uint8_t base_reg, uint32_t offset, TypeID type_id)
{
    uint32_t ref_idx = module_->add_field_ref(offset, type_id);

    if (ref_idx <= 255) {
        emit_abc(Opcode::STACK_FIELDGET, dest, base_reg, static_cast<uint8_t>(ref_idx));
    } else {
        // Load index into register and use _R variant
        uint8_t idx_reg = registers_.allocate();
        if (ref_idx <= 32767) {
            emit_asbx(Opcode::LOADI, idx_reg, static_cast<int16_t>(ref_idx));
        } else {
            uint32_t k_idx = add_constant_int(static_cast<int32_t>(ref_idx));
            emit_abx(Opcode::LOADK, idx_reg, static_cast<uint16_t>(k_idx));
        }
        emit_abc(Opcode::STACK_FIELDGET_R, dest, base_reg, idx_reg);
        registers_.free(idx_reg);
    }
}

void AstCompiler::emit_stack_field_set(uint8_t base_reg, uint32_t offset, TypeID type_id, uint8_t val_reg)
{
    uint32_t ref_idx = module_->add_field_ref(offset, type_id);

    if (ref_idx <= 255) {
        emit_abc(Opcode::STACK_FIELDSET, base_reg, static_cast<uint8_t>(ref_idx), val_reg);
    } else {
        // Load index into register and use _R variant
        uint8_t idx_reg = registers_.allocate();
        if (ref_idx <= 32767) {
            emit_asbx(Opcode::LOADI, idx_reg, static_cast<int16_t>(ref_idx));
        } else {
            uint32_t k_idx = add_constant_int(static_cast<int32_t>(ref_idx));
            emit_abx(Opcode::LOADK, idx_reg, static_cast<uint16_t>(k_idx));
        }
        emit_abc(Opcode::STACK_FIELDSET_R, base_reg, idx_reg, val_reg);
        registers_.free(idx_reg);
    }
}

std::pair<bool, int32_t> AstCompiler::try_eval_const_int(Expression* expr) const
{
    if (auto lit = dynamic_cast<LiteralExpression*>(expr)) {
        if (lit->data.is<int32_t>()) {
            return {true, lit->data.as<int32_t>()};
        }
    }

    AstConstEvaluator eval(script_, expr);
    if (!eval.failed_ && !eval.result_.empty() && eval.result_.back().type_id == runtime_->int_type()) {
        return {true, eval.result_.back().data.ival};
    }

    return {false, -1};
}

uint8_t AstCompiler::emit_fixed_array_element_offset(uint8_t idx_reg, uint32_t base_offset, uint32_t elem_size)
{
    uint8_t offset_reg = registers_.allocate();
    uint8_t base_reg = registers_.allocate();

    if (base_offset <= 32767) {
        emit_asbx(Opcode::LOADI, base_reg, static_cast<int16_t>(base_offset));
    } else {
        uint32_t k_idx = add_constant_int(static_cast<int32_t>(base_offset));
        emit_abx(Opcode::LOADK, base_reg, static_cast<uint16_t>(k_idx));
    }

    if (elem_size == 1) {
        emit_abc(Opcode::ADD, offset_reg, idx_reg, base_reg);
    } else {
        uint8_t size_reg = registers_.allocate();
        if (elem_size <= 32767) {
            emit_asbx(Opcode::LOADI, size_reg, static_cast<int16_t>(elem_size));
        } else {
            uint32_t k_idx = add_constant_int(static_cast<int32_t>(elem_size));
            emit_abx(Opcode::LOADK, size_reg, static_cast<uint16_t>(k_idx));
        }
        uint8_t scaled_idx = registers_.allocate();
        emit_abc(Opcode::MUL, scaled_idx, idx_reg, size_reg);
        emit_abc(Opcode::ADD, offset_reg, scaled_idx, base_reg);
        registers_.free(scaled_idx);
        registers_.free(size_reg);
    }

    registers_.free(base_reg);
    return offset_reg;
}

Opcode AstCompiler::field_offset_get_opcode(TypeID elem_type_id) const
{
    if (elem_type_id == runtime_->int_type()) {
        return Opcode::FIELDGETI_OFF_R;
    }
    if (elem_type_id == runtime_->float_type()) {
        return Opcode::FIELDGETF_OFF_R;
    }
    if (elem_type_id == runtime_->bool_type()) {
        return Opcode::FIELDGETB_OFF_R;
    }
    if (elem_type_id == runtime_->string_type()) {
        return Opcode::FIELDGETS_OFF_R;
    }
    if (runtime_->is_object_like_type(elem_type_id)) {
        return Opcode::FIELDGETO_OFF_R;
    }
    return Opcode::FIELDGETH_OFF_R;
}

Opcode AstCompiler::field_offset_set_opcode(TypeID elem_type_id) const
{
    if (elem_type_id == runtime_->int_type()) {
        return Opcode::FIELDSETI_OFF_R;
    }
    if (elem_type_id == runtime_->float_type()) {
        return Opcode::FIELDSETF_OFF_R;
    }
    if (elem_type_id == runtime_->bool_type()) {
        return Opcode::FIELDSETB_OFF_R;
    }
    if (elem_type_id == runtime_->string_type()) {
        return Opcode::FIELDSETS_OFF_R;
    }
    if (runtime_->is_object_like_type(elem_type_id)) {
        return Opcode::FIELDSETO_OFF_R;
    }
    return Opcode::FIELDSETH_OFF_R;
}

Opcode AstCompiler::token_to_binary_opcode(TokenType type)
{
    switch (type) {
    case TokenType::PLUS:
    case TokenType::PLUSEQ:
        return Opcode::ADD;
    case TokenType::MINUS:
    case TokenType::MINUSEQ:
        return Opcode::SUB;
    case TokenType::TIMES:
    case TokenType::TIMESEQ:
        return Opcode::MUL;
    case TokenType::DIV:
    case TokenType::DIVEQ:
        return Opcode::DIV;
    case TokenType::MOD:
    case TokenType::MODEQ:
        return Opcode::MOD;
    default:
        fail(fmt::format("Unknown binary operator: {}", static_cast<int>(type)));
        return Opcode::ADD;
    }
}

Opcode AstCompiler::token_to_comparison_opcode(TokenType type)
{
    switch (type) {
    case TokenType::EQEQ:
        return Opcode::EQ;
    case TokenType::NOTEQ:
        return Opcode::NE;
    case TokenType::LT:
        return Opcode::LT;
    case TokenType::LTEQ:
        return Opcode::LE;
    case TokenType::GT:
        return Opcode::GT;
    case TokenType::GTEQ:
        return Opcode::GE;
    default:
        fail(fmt::format("Unknown comparison operator: {}", static_cast<int>(type)));
        return Opcode::EQ;
    }
}

bool AstCompiler::emit_script_operator_call(const ScriptFunctionRef& ref,
    const Vector<uint8_t>& arg_regs, uint8_t& out_result)
{
    String qualified_name = ref.module_path + "." + ref.function_name;
    InternedString interned = nw::kernel::strings().intern(qualified_name);
    uint32_t ext_idx = module_->add_external_ref(interned);
    if (ext_idx > 255) {
        fail("External ref index > 255 for script operator");
        return false;
    }

    uint8_t argc = static_cast<uint8_t>(arg_regs.size());
    uint8_t count = 1 + argc;
    uint8_t base_reg = registers_.allocate_contiguous(count);

    for (uint8_t i = 0; i < argc; ++i) {
        emit_abc(Opcode::MOVE, base_reg + 1 + i, arg_regs[i], 0);
    }

    emit_abc(Opcode::CALLEXT, base_reg, static_cast<uint8_t>(ext_idx), argc);

    for (uint8_t i = 0; i < argc; ++i) {
        registers_.free(base_reg + 1 + i);
    }

    out_result = base_reg;
    return true;
}

// == Visitor Methods (Declarations) ==========================================

void AstCompiler::visit(Ast* ast)
{
    // Top-level - should not be visited directly
}

void AstCompiler::visit(FunctionDefinition* decl)
{
    // Handled by compile_function, should not be visited recursively
}

void AstCompiler::visit(VarDecl* decl)
{
    uint8_t var_reg = allocate_local(decl->identifier_.loc.view(), false);

    if (decl->init) {
        bool is_val_type = is_value_type(decl->type_id_);
        bool is_brace_init = dynamic_cast<BraceInitLiteral*>(decl->init) != nullptr;

        if (is_val_type && !is_brace_init) {
            // Value type initialized from another value - need to allocate and copy
            // First allocate stack space for this variable
            uint16_t type_idx = static_cast<uint16_t>(module_->type_refs.size());
            module_->type_refs.push_back(decl->type_id_);
            emit_abx(Opcode::STACK_ALLOC, var_reg, type_idx);

            // Compile source expression
            decl->init->accept(this);
            uint8_t src_reg = result_reg_;

            // Copy from source to destination
            emit_abc(Opcode::STACK_COPY, var_reg, src_reg, 0);
            registers_.free(src_reg);
        } else {
            // Regular initialization (primitives, heap types, or brace init which handles allocation)
            decl->init->accept(this);
            if (result_reg_ != var_reg) {
                emit_abc(Opcode::MOVE, var_reg, result_reg_, 0);
                registers_.free(result_reg_);
            }
        }
    } else {
        // No initializer
        if (is_value_type(decl->type_id_)) {
            // Value type - allocate zeroed stack space
            uint16_t type_idx = static_cast<uint16_t>(module_->type_refs.size());
            module_->type_refs.push_back(decl->type_id_);
            emit_abx(Opcode::STACK_ALLOC, var_reg, type_idx);
        } else {
            const Type* decl_type = runtime_->get_type(decl->type_id_);

            if (decl_type && decl_type->primitive_kind == PK_string) {
                uint32_t k_idx = add_constant_string("");
                emit_abx(Opcode::LOADK, var_reg, static_cast<uint16_t>(k_idx));
            } else if (decl_type && decl_type->type_kind == TK_array) {
                emit_asbx(Opcode::LOADI, var_reg, 0);
                uint16_t type_idx = static_cast<uint16_t>(module_->type_refs.size());
                module_->type_refs.push_back(decl->type_id_);
                emit_abx(Opcode::NEWARRAY, var_reg, type_idx);
            } else if (decl_type && decl_type->type_kind == TK_map) {
                uint16_t type_idx = static_cast<uint16_t>(module_->type_refs.size());
                module_->type_refs.push_back(decl->type_id_);
                emit_abx(Opcode::NEWMAP, var_reg, type_idx);
            } else {
                emit_abc(Opcode::LOADNIL, var_reg, 0, 0);

                if (decl_type && decl_type->type_kind == TK_function) {
                    uint16_t type_idx = static_cast<uint16_t>(module_->type_refs.size());
                    module_->type_refs.push_back(decl->type_id_);
                    emit_abx(Opcode::CAST, var_reg, type_idx);
                }
            }
        }
    }
}

void AstCompiler::visit(StructDecl* decl) { }
void AstCompiler::visit(SumDecl* decl) { }
void AstCompiler::visit(VariantDecl* decl) { }
void AstCompiler::visit(TypeAlias* decl) { }
void AstCompiler::visit(NewtypeDecl* decl) { }
void AstCompiler::visit(OpaqueTypeDecl* decl) { }
void AstCompiler::visit(AliasedImportDecl* decl) { }
void AstCompiler::visit(SelectiveImportDecl* decl) { }

// == Visitor Methods (Statements) ============================================

void AstCompiler::visit(ExprStatement* stmt)
{
    if (stmt->expr) {
        stmt->expr->accept(this);
        // Free the result register (expression value is discarded)
        registers_.free(result_reg_);
    }
}

void AstCompiler::visit(BlockStatement* stmt)
{
    // Save the terminated flag from any outer block so we don't clobber it.
    bool outer_terminated = block_terminated_;
    block_terminated_ = false;

    for (auto* node : stmt->nodes) {
        if (failed_) break;
        if (block_terminated_) break; // DCE: skip unreachable code
        node->accept(this);
    }

    // The terminated state is only relevant within this block; restore the outer.
    block_terminated_ = outer_terminated;
}

void AstCompiler::visit(IfStatement* stmt)
{
    // DCE: constant condition — compile only the taken branch.
    if (stmt->expr->is_const_) {
        AstConstEvaluator cond_eval(script_, stmt->expr);
        if (!cond_eval.failed_ && !cond_eval.result_.empty()) {
            bool cond = const_is_truthy(cond_eval.result_.back(), runtime_);
            if (cond) {
                stmt->if_branch->accept(this);
            } else if (stmt->else_branch) {
                stmt->else_branch->accept(this);
            }
            return;
        }
    }

    // 1. Compile condition
    stmt->expr->accept(this);
    uint8_t cond_reg = result_reg_;

    // 2. Emit Jump-if-False to skip 'then' block
    // We don't know the offset yet, so we emit with 0 and patch later.
    // JMPF expects (rA, sBx).
    emit_asbx(Opcode::JMPF, cond_reg, 0);
    uint32_t jmp_to_else_idx = static_cast<uint32_t>(current_func_->instructions.size() - 1);

    registers_.free(cond_reg);

    // 3. Compile 'then' block
    stmt->if_branch->accept(this);

    if (stmt->else_branch) {
        // 4. Emit Jump to skip 'else' block (unconditional JMP)
        uint32_t jmp_to_end_idx = emit_jump(Opcode::JMP, 0);

        // 5. Patch the JMPF to jump here (start of else block)
        // Target PC is the next instruction index (which is current size)
        patch_jump(jmp_to_else_idx, static_cast<int32_t>(current_func_->instructions.size()));

        // 6. Compile 'else' block
        stmt->else_branch->accept(this);

        // 7. Patch the JMP to jump here (end of else block)
        patch_jump(jmp_to_end_idx, static_cast<int32_t>(current_func_->instructions.size()));
    } else {
        // No else block, so JMPF jumps to here (end of then block)
        patch_jump(jmp_to_else_idx, static_cast<int32_t>(current_func_->instructions.size()));
    }
}

void AstCompiler::visit(ForStatement* stmt)
{
    // DCE: constant-false condition → skip entire loop body.
    if (stmt->check && stmt->check->is_const_) {
        AstConstEvaluator chk_eval(script_, stmt->check);
        if (!chk_eval.failed_ && !chk_eval.result_.empty()) {
            if (!const_is_truthy(chk_eval.result_.back(), runtime_)) {
                // The loop will never execute.  Still compile the init clause
                // because it may have side-effects (e.g. variable declarations).
                if (stmt->init) stmt->init->accept(this);
                return;
            }
            // constant-true: fall through to normal compilation (infinite loop)
        }
    }

    // 1. Compile Init
    if (stmt->init) {
        stmt->init->accept(this);
        // Note: Registers allocated here persist (current allocator limitation)
    }

    uint32_t loop_start = static_cast<uint32_t>(current_func_->instructions.size());

    // Push loop context
    ControlScope scope;
    scope.is_loop = true;
    scope.start_pc = loop_start;
    control_stack_.push_back(scope);

    // 2. Compile Check
    uint32_t exit_jump_idx = UINT32_MAX;
    if (stmt->check) {
        stmt->check->accept(this);
        uint8_t cond_reg = result_reg_;

        // Emit JMPF to end
        emit_asbx(Opcode::JMPF, cond_reg, 0);
        exit_jump_idx = static_cast<uint32_t>(current_func_->instructions.size() - 1);
        registers_.free(cond_reg);
    }

    // 3. Compile Body
    if (stmt->block) {
        stmt->block->accept(this);
    }

    // 4. Continue Target
    uint32_t continue_target = static_cast<uint32_t>(current_func_->instructions.size());

    // Patch continues to here
    for (uint32_t idx : control_stack_.back().continue_jumps) {
        patch_jump(idx, static_cast<int32_t>(continue_target));
    }

    // 5. Compile Increment
    if (stmt->inc) {
        stmt->inc->accept(this);
        registers_.free(result_reg_);
    }

    // 6. Jump back to start
    uint32_t back_jump_idx = emit_jump(Opcode::JMP, 0);
    patch_jump(back_jump_idx, static_cast<int32_t>(loop_start));

    // 7. Loop End
    uint32_t loop_end = static_cast<uint32_t>(current_func_->instructions.size());

    // Patch exit jump
    if (exit_jump_idx != UINT32_MAX) {
        patch_jump(exit_jump_idx, static_cast<int32_t>(loop_end));
    }

    // Patch breaks
    for (uint32_t idx : control_stack_.back().break_jumps) {
        patch_jump(idx, static_cast<int32_t>(loop_end));
    }

    control_stack_.pop_back();
}

void AstCompiler::visit(ForEachStatement* stmt)
{
    // Compile collection expression
    stmt->collection->accept(this);
    uint8_t collection_reg = result_reg_;

    if (stmt->is_map_iteration) {
        // Map iteration: use iterator intrinsics
        // iter = map_iter_begin(collection)
        uint8_t iter_reg = registers_.allocate();
        emit_abc(Opcode::MOVE, iter_reg + 1, collection_reg, 0);
        emit_abc(Opcode::CALLINTR, iter_reg, static_cast<uint8_t>(IntrinsicId::MapIterBegin), 1);

        uint32_t loop_start = static_cast<uint32_t>(current_func_->instructions.size());

        // Push loop context
        ControlScope scope;
        scope.is_loop = true;
        scope.start_pc = loop_start;
        control_stack_.push_back(scope);

        // Allocate locals for key and value
        uint8_t key_reg = allocate_local(stmt->key_var->identifier_.loc.view(), false);
        uint8_t val_reg = allocate_local(stmt->value_var->identifier_.loc.view(), false);

        // (valid, key, value) = map_iter_next(iter)
        // Results: valid in key_reg, key in key_reg+1, value in key_reg+2
        emit_abc(Opcode::MOVE, key_reg + 1, iter_reg, 0);
        emit_abc(Opcode::CALLINTR, key_reg, static_cast<uint8_t>(IntrinsicId::MapIterNext), 1);

        // Check if valid (result is in key_reg)
        emit_asbx(Opcode::JMPF, key_reg, 0);
        uint32_t exit_jump_idx = static_cast<uint32_t>(current_func_->instructions.size() - 1);

        // Move key and value to correct registers
        // key is in key_reg+1, value is in key_reg+2
        // We need to move them after checking valid
        emit_abc(Opcode::MOVE, key_reg, key_reg + 1, 0);
        emit_abc(Opcode::MOVE, val_reg, key_reg + 2, 0);

        // Body
        if (stmt->block) {
            stmt->block->accept(this);
        }

        // Continue target
        uint32_t continue_target = static_cast<uint32_t>(current_func_->instructions.size());
        for (uint32_t idx : control_stack_.back().continue_jumps) {
            patch_jump(idx, static_cast<int32_t>(continue_target));
        }

        // Jump back to loop start
        uint32_t back_jump_idx = emit_jump(Opcode::JMP, 0);
        patch_jump(back_jump_idx, static_cast<int32_t>(loop_start));

        // Cleanup label (for breaks and exit)
        uint32_t cleanup_pc = static_cast<uint32_t>(current_func_->instructions.size());

        // map_iter_end(collection, iter)
        uint8_t cleanup_reg = registers_.allocate();
        emit_abc(Opcode::MOVE, cleanup_reg + 1, collection_reg, 0);
        emit_abc(Opcode::MOVE, cleanup_reg + 2, iter_reg, 0);
        emit_abc(Opcode::CALLINTR, cleanup_reg, static_cast<uint8_t>(IntrinsicId::MapIterEnd), 2);
        registers_.free(cleanup_reg);

        uint32_t loop_end = static_cast<uint32_t>(current_func_->instructions.size());

        // Patch exit jump and breaks to cleanup
        patch_jump(exit_jump_idx, static_cast<int32_t>(cleanup_pc));
        for (uint32_t idx : control_stack_.back().break_jumps) {
            patch_jump(idx, static_cast<int32_t>(cleanup_pc));
        }

        registers_.free(iter_reg);
        control_stack_.pop_back();

    } else {
        // Array iteration: index-based loop
        // index = 0
        uint8_t index_reg = registers_.allocate();
        uint32_t zero_const = add_constant_int(0);
        emit_abx(Opcode::LOADK, index_reg, static_cast<uint16_t>(zero_const));

        uint32_t loop_start = static_cast<uint32_t>(current_func_->instructions.size());

        // Push loop context
        ControlScope scope;
        scope.is_loop = true;
        scope.start_pc = loop_start;
        control_stack_.push_back(scope);

        // len = array_len(collection)
        uint8_t len_reg = registers_.allocate();
        emit_abc(Opcode::MOVE, len_reg + 1, collection_reg, 0);
        emit_abc(Opcode::CALLINTR, len_reg, static_cast<uint8_t>(IntrinsicId::ArrayLen), 1);

        // if index >= len goto end
        uint8_t cmp_reg = registers_.allocate();
        emit_abc(Opcode::LT, cmp_reg, index_reg, len_reg); // cmp_reg = index < len
        emit_asbx(Opcode::JMPF, cmp_reg, 0);
        uint32_t exit_jump_idx = static_cast<uint32_t>(current_func_->instructions.size() - 1);
        registers_.free(cmp_reg);
        registers_.free(len_reg);

        // elem = collection[index]
        uint8_t elem_reg = allocate_local(stmt->var->identifier_.loc.view(), false);
        emit_abc(Opcode::GETARRAY, elem_reg, collection_reg, index_reg);

        // Body
        if (stmt->block) {
            stmt->block->accept(this);
        }

        // Continue target
        uint32_t continue_target = static_cast<uint32_t>(current_func_->instructions.size());
        for (uint32_t idx : control_stack_.back().continue_jumps) {
            patch_jump(idx, static_cast<int32_t>(continue_target));
        }

        // index++
        uint8_t one_reg = registers_.allocate();
        uint32_t one_const = add_constant_int(1);
        emit_abx(Opcode::LOADK, one_reg, static_cast<uint16_t>(one_const));
        emit_abc(Opcode::ADD, index_reg, index_reg, one_reg);
        registers_.free(one_reg);

        // Jump back to loop start
        uint32_t back_jump_idx = emit_jump(Opcode::JMP, 0);
        patch_jump(back_jump_idx, static_cast<int32_t>(loop_start));

        // Loop end
        uint32_t loop_end = static_cast<uint32_t>(current_func_->instructions.size());
        patch_jump(exit_jump_idx, static_cast<int32_t>(loop_end));

        // Patch breaks
        for (uint32_t idx : control_stack_.back().break_jumps) {
            patch_jump(idx, static_cast<int32_t>(loop_end));
        }

        registers_.free(index_reg);
        control_stack_.pop_back();
    }

    registers_.free(collection_reg);
}

void AstCompiler::visit(SwitchStatement* stmt)
{
    // 1. Compile target
    stmt->target->accept(this);
    uint8_t target_reg = result_reg_;

    // Check if this is a sum type switch
    const Type* target_type = runtime_->get_type(stmt->target->type_id_);
    bool is_sum_switch = target_type && target_type->type_kind == TK_sum;
    bool is_object_switch = runtime_->is_object_like_type(stmt->target->type_id_);

    // 2. Jump to dispatch logic (at end)
    uint32_t jump_to_dispatch = emit_jump(Opcode::JMP, 0);

    // 3. Push Switch Control Scope
    ControlScope scope;
    scope.is_loop = false;
    scope.is_sum_switch = is_sum_switch;
    scope.is_object_switch = is_object_switch;
    scope.sum_target_reg = target_reg;
    scope.object_target_reg = target_reg;
    scope.sum_type_id = stmt->target->type_id_;
    control_stack_.push_back(scope);

    // 4. Compile Body (no implicit fallthrough - each case gets implicit break)
    if (stmt->block) {
        auto* block = dynamic_cast<BlockStatement*>(stmt->block);
        if (block) {
            bool last_was_label = false;
            for (size_t i = 0; i < block->nodes.size(); ++i) {
                if (failed_) break;
                auto* node = block->nodes[i];
                auto* label = dynamic_cast<LabelStatement*>(node);

                if (label) {
                    last_was_label = true;
                    node->accept(this);
                } else {
                    node->accept(this);
                    if (last_was_label) {
                        uint32_t break_idx = emit_jump(Opcode::JMP, 0);
                        control_stack_.back().break_jumps.push_back(break_idx);
                        last_was_label = false;
                    }
                }
            }
        } else {
            stmt->block->accept(this);
        }
    }

    // 5. Jump to End (fallthrough from last case falls off end of body)
    uint32_t jump_to_end = emit_jump(Opcode::JMP, 0);

    // 6. Generate Dispatch Logic
    uint32_t dispatch_start_pc = static_cast<uint32_t>(current_func_->instructions.size());
    patch_jump(jump_to_dispatch, static_cast<int32_t>(dispatch_start_pc));

    uint32_t default_pc = control_stack_.back().default_pc;

    if (is_sum_switch) {
        // Sum type pattern matching dispatch
        auto sum_id = target_type->type_params[0].as<SumID>();
        const SumDef* sum_def = runtime_->type_table_.get(sum_id);

        // Extract tag from sum value
        uint8_t tag_reg = registers_.allocate();
        emit_abc(Opcode::SUMGETTAG, tag_reg, target_reg, 0);

        auto& pattern_cases = control_stack_.back().pattern_cases;
        for (const auto& [label_stmt, label_pc] : pattern_cases) {
            StringView variant_name;
            if (auto path_expr = dynamic_cast<PathExpression*>(label_stmt->expr)) {
                if (!path_expr->parts.empty()) {
                    if (auto rhs_var = dynamic_cast<IdentifierExpression*>(path_expr->parts.back())) {
                        variant_name = rhs_var->ident.loc.view();
                    }
                }
            }
            if (variant_name.empty()) {
                continue;
            }
            const VariantDef* variant = sum_def->find_variant(variant_name);
            if (!variant) continue;

            // Load expected tag value
            uint8_t expected_tag_reg = registers_.allocate();
            emit_asbx(Opcode::LOADI, expected_tag_reg, static_cast<int16_t>(variant->tag_value));

            // Compare tags
            emit_abc(Opcode::ISEQ, tag_reg, expected_tag_reg, 0);
            registers_.free(expected_tag_reg);

            // JMP to next check (skipped if equal)
            uint32_t skip_jump = emit_jump(Opcode::JMP, 0);

            // JMP to case label (executed if equal)
            int32_t offset = static_cast<int32_t>(label_pc) - static_cast<int32_t>(current_func_->instructions.size() + 1);
            emit_jump(Opcode::JMP, offset);

            // Patch skip_jump to here
            patch_jump(skip_jump, static_cast<int32_t>(current_func_->instructions.size()));
        }

        registers_.free(tag_reg);
    } else if (is_object_switch) {
        auto& pattern_cases = control_stack_.back().object_pattern_cases;
        for (const auto& [label_stmt, label_pc] : pattern_cases) {
            TypeID case_type = label_stmt && label_stmt->expr ? label_stmt->expr->type_id_ : invalid_type_id;
            if (case_type == invalid_type_id) {
                continue;
            }

            uint16_t type_idx = 0;
            auto it = std::find(module_->type_refs.begin(), module_->type_refs.end(), case_type);
            if (it == module_->type_refs.end()) {
                type_idx = static_cast<uint16_t>(module_->type_refs.size());
                module_->type_refs.push_back(case_type);
            } else {
                type_idx = static_cast<uint16_t>(std::distance(module_->type_refs.begin(), it));
            }

            uint8_t test_reg = registers_.allocate();
            emit_abc(Opcode::MOVE, test_reg, target_reg, 0);
            emit_abx(Opcode::IS, test_reg, type_idx);
            emit_asbx(Opcode::JMPF, test_reg, 0);
            uint32_t skip_jump = static_cast<uint32_t>(current_func_->instructions.size() - 1);
            registers_.free(test_reg);

            int32_t offset = static_cast<int32_t>(label_pc) - static_cast<int32_t>(current_func_->instructions.size() + 1);
            emit_jump(Opcode::JMP, offset);

            patch_jump(skip_jump, static_cast<int32_t>(current_func_->instructions.size()));
        }
    } else {
        // Traditional switch dispatch
        auto& cases = control_stack_.back().cases;
        for (const auto& [expr, label_pc] : cases) {
            expr->accept(this);
            uint8_t case_val_reg = result_reg_;

            emit_abc(Opcode::ISEQ, target_reg, case_val_reg, 0);

            uint32_t skip_jump = emit_jump(Opcode::JMP, 0);

            int32_t offset = static_cast<int32_t>(label_pc) - static_cast<int32_t>(current_func_->instructions.size() + 1);
            emit_jump(Opcode::JMP, offset);

            patch_jump(skip_jump, static_cast<int32_t>(current_func_->instructions.size()));

            registers_.free(case_val_reg);
        }
    }

    // If no match, jump to default or end
    if (default_pc != UINT32_MAX) {
        int32_t offset = static_cast<int32_t>(default_pc) - static_cast<int32_t>(current_func_->instructions.size() + 1);
        emit_jump(Opcode::JMP, offset);
    }

    // 7. End Label
    uint32_t end_pc = static_cast<uint32_t>(current_func_->instructions.size());
    patch_jump(jump_to_end, static_cast<int32_t>(end_pc));

    // Patch breaks
    for (uint32_t idx : control_stack_.back().break_jumps) {
        patch_jump(idx, static_cast<int32_t>(end_pc));
    }

    control_stack_.pop_back();
    registers_.free(target_reg);
}

void AstCompiler::visit(LabelStatement* stmt)
{
    if (control_stack_.empty() || control_stack_.back().is_loop) {
        fail("Case/Default label outside of switch statement");
        return;
    }

    uint32_t current_pc = static_cast<uint32_t>(current_func_->instructions.size());
    auto& scope = control_stack_.back();

    if (stmt->type.type == TokenType::DEFAULT) {
        scope.default_pc = current_pc;
        return;
    }

    // CASE label
    if (scope.is_sum_switch && stmt->is_pattern_match) {
        // Sum type pattern matching case
        scope.pattern_cases.push_back({stmt, current_pc});

        // Emit payload binding code if there are bindings
        if (!stmt->bindings.empty()) {
            const Type* sum_type = runtime_->get_type(scope.sum_type_id);
            auto sum_id = sum_type->type_params[0].as<SumID>();
            const SumDef* sum_def = runtime_->type_table_.get(sum_id);

            StringView variant_name;
            if (auto path_expr = dynamic_cast<PathExpression*>(stmt->expr)) {
                if (!path_expr->parts.empty()) {
                    if (auto rhs_var = dynamic_cast<IdentifierExpression*>(path_expr->parts.back())) {
                        variant_name = rhs_var->ident.loc.view();
                    }
                }
            }

            if (!variant_name.empty()) {
                const VariantDef* variant = sum_def->find_variant(variant_name);

                if (variant && variant->payload_type != invalid_type_id) {
                    // Extract payload and bind to variable
                    uint8_t payload_reg = registers_.allocate();
                    emit_abc(Opcode::SUMGETPAYLOAD, payload_reg, scope.sum_target_reg,
                        static_cast<uint8_t>(variant->tag_value));

                    // For single binding, assign payload directly
                    if (stmt->bindings.size() == 1) {
                        auto* binding = stmt->bindings[0];
                        String var_name(binding->identifier_.loc.view());
                        uint8_t var_reg = allocate_local(var_name, false);
                        emit_abc(Opcode::MOVE, var_reg, payload_reg, 0);
                        registers_.free(payload_reg);
                    } else {
                        // Multiple bindings - payload is a tuple
                        for (size_t i = 0; i < stmt->bindings.size(); ++i) {
                            auto* binding = stmt->bindings[i];
                            String var_name(binding->identifier_.loc.view());
                            uint8_t var_reg = allocate_local(var_name, false);
                            emit_abc(Opcode::GETTUPLE, var_reg, payload_reg, static_cast<uint8_t>(i));
                        }
                        registers_.free(payload_reg);
                    }
                }
            }
        }
    } else if (scope.is_object_switch && stmt->is_pattern_match) {
        scope.object_pattern_cases.push_back({stmt, current_pc});

        if (stmt->bindings.size() == 1) {
            auto* binding = stmt->bindings[0];
            String var_name(binding->identifier_.loc.view());
            uint8_t var_reg = allocate_local(var_name, false);
            emit_abc(Opcode::MOVE, var_reg, scope.object_target_reg, 0);

            TypeID binding_type = binding->type_id_;
            if (binding_type != invalid_type_id) {
                uint16_t type_idx = 0;
                auto it = std::find(module_->type_refs.begin(), module_->type_refs.end(), binding_type);
                if (it == module_->type_refs.end()) {
                    type_idx = static_cast<uint16_t>(module_->type_refs.size());
                    module_->type_refs.push_back(binding_type);
                } else {
                    type_idx = static_cast<uint16_t>(std::distance(module_->type_refs.begin(), it));
                }
                emit_abx(Opcode::CAST, var_reg, type_idx);
            }
        }
    } else {
        // Traditional case
        scope.cases.push_back({stmt->expr, current_pc});
    }
}

void AstCompiler::visit(JumpStatement* stmt)
{
    if (stmt->op.type == TokenType::RETURN) {
        if (stmt->exprs.empty()) {
            // Void return
            emit_abc(Opcode::CLOSEUPVALS, 0, 0, 0);
            emit(Instruction::make_abc(Opcode::RETVOID, 0, 0, 0));
        } else if (stmt->exprs.size() == 1) {
            // Single return value
            stmt->exprs[0]->accept(this);
            emit_abc(Opcode::CLOSEUPVALS, 0, 0, 0);
            emit_abc(Opcode::RET, result_reg_, 0, 0);
            registers_.free(result_reg_);
        } else {
            // Multiple return values - create tuple
            // Compile each expression and store in consecutive registers
            Vector<uint8_t> value_regs;
            for (auto* expr : stmt->exprs) {
                expr->accept(this);
                value_regs.push_back(result_reg_);
            }

            // Allocate contiguous registers: tuple_reg + element slots
            uint8_t tuple_reg = registers_.allocate_contiguous(1 + static_cast<uint8_t>(value_regs.size()));

            // Move values to consecutive registers starting at tuple_reg + 1
            for (size_t i = 0; i < value_regs.size(); ++i) {
                uint8_t target_reg = tuple_reg + 1 + static_cast<uint8_t>(i);
                if (value_regs[i] != target_reg) {
                    emit_abc(Opcode::MOVE, target_reg, value_regs[i], 0);
                }
                registers_.free(value_regs[i]);
            }

            // NEWTUPLE r0, count - creates tuple from r0+1..r0+count
            emit_abc(Opcode::NEWTUPLE, tuple_reg, static_cast<uint8_t>(stmt->exprs.size()), 0);

            // Return the tuple
            emit_abc(Opcode::CLOSEUPVALS, 0, 0, 0);
            emit_abc(Opcode::RET, tuple_reg, 0, 0);
            registers_.free(tuple_reg);
        }
        block_terminated_ = true;
    } else if (stmt->op.type == TokenType::BREAK) {
        if (control_stack_.empty()) {
            fail("Break statement outside of control structure");
            return;
        }
        uint32_t idx = emit_jump(Opcode::JMP, 0);
        control_stack_.back().break_jumps.push_back(idx);
        block_terminated_ = true;
    } else if (stmt->op.type == TokenType::CONTINUE) {
        // Find nearest loop scope
        int scope_idx = static_cast<int>(control_stack_.size()) - 1;
        while (scope_idx >= 0 && !control_stack_[scope_idx].is_loop) {
            scope_idx--;
        }

        if (scope_idx < 0) {
            fail("Continue statement outside of loop");
            return;
        }
        uint32_t idx = emit_jump(Opcode::JMP, 0);
        control_stack_[scope_idx].continue_jumps.push_back(idx);
        block_terminated_ = true;
    }
}

void AstCompiler::visit(DeclList* stmt)
{
    if (stmt->decls.size() > 1 && stmt->decls[0]->init) {
        auto* init_expr = stmt->decls[0]->init;
        bool all_same_init = true;
        for (auto* decl : stmt->decls) {
            if (decl->init != init_expr) {
                all_same_init = false;
                break;
            }
        }

        if (all_same_init) {
            init_expr->accept(this);
            uint8_t tuple_reg = result_reg_;
            size_t element_count = 0;
            const Type* type = runtime_->get_type(init_expr->type_id_);
            if (type && type->type_kind == TK_tuple) {
                element_count = runtime_->get_tuple_element_count(init_expr->type_id_);
                if (element_count < stmt->decls.size()) {
                    fail("Tuple destructuring has more variables than tuple elements");
                    return;
                }
            } else if (init_expr->type_id_ == invalid_type_id) {
                element_count = stmt->decls.size();
            } else {
                fail("Tuple destructuring requires a tuple initializer");
                return;
            }

            for (size_t i = 0; i < stmt->decls.size(); ++i) {
                uint8_t var_reg = allocate_local(stmt->decls[i]->identifier_.loc.view(), false);
                if (i > 255) {
                    fail("Tuple index must be a constant integer 0-255");
                    return;
                }
                emit_abc(Opcode::GETTUPLE, var_reg, tuple_reg, static_cast<uint8_t>(i));
            }

            registers_.free(tuple_reg);
            return;
        }
    }

    for (auto* decl : stmt->decls) {
        decl->accept(this);
    }
}

void AstCompiler::visit(EmptyStatement* stmt)
{
    // No code to emit
}

// == Visitor Methods (Expressions) ===========================================

void AstCompiler::visit(BinaryExpression* expr)
{
    TRACK_SOURCE(expr);

    if (try_emit_const(expr)) return;

    expr->lhs->accept(this);
    uint8_t lhs_reg = result_reg_;

    expr->rhs->accept(this);
    uint8_t rhs_reg = result_reg_;

    auto* ref = runtime_->find_script_binary_op(expr->op.type,
        expr->lhs->type_id_, expr->rhs->type_id_);
    if (ref) {
        uint8_t result;
        Vector<uint8_t> args = {lhs_reg, rhs_reg};
        emit_script_operator_call(*ref, args, result);
        registers_.free(lhs_reg);
        registers_.free(rhs_reg);
        result_reg_ = result;
        return;
    }

    uint8_t result = registers_.allocate();
    Opcode op = token_to_binary_opcode(expr->op.type);
    emit_abc(op, result, lhs_reg, rhs_reg);

    registers_.free(lhs_reg);
    registers_.free(rhs_reg);

    result_reg_ = result;
}

void AstCompiler::visit(ComparisonExpression* expr)
{
    if (try_emit_const(expr)) return;

    expr->lhs->accept(this);
    uint8_t lhs_reg = result_reg_;
    expr->rhs->accept(this);
    uint8_t rhs_reg = result_reg_;

    TypeID lhs_tid = expr->lhs->type_id_;
    TypeID rhs_tid = expr->rhs->type_id_;

    // Check for script-defined comparison operators
    // EQEQ: direct call to eq operator
    // NOTEQ: call eq, then NOT
    // LT: direct call to lt operator
    // GT: call lt with swapped args
    // LTEQ/GTEQ: leave as opcodes so runtime can synthesize via lt/eq
    const ScriptFunctionRef* ref = nullptr;
    bool negate = false;
    bool swap_args = false;

    switch (expr->op.type) {
    case TokenType::EQEQ:
        ref = runtime_->find_script_binary_op(TokenType::EQEQ, lhs_tid, rhs_tid);
        break;
    case TokenType::NOTEQ:
        ref = runtime_->find_script_binary_op(TokenType::EQEQ, lhs_tid, rhs_tid);
        negate = true;
        break;
    case TokenType::LT:
        ref = runtime_->find_script_binary_op(TokenType::LT, lhs_tid, rhs_tid);
        break;
    case TokenType::GT:
        ref = runtime_->find_script_binary_op(TokenType::LT, rhs_tid, lhs_tid);
        swap_args = true;
        break;
    case TokenType::GTEQ:
        // Runtime synthesis: (b < a) || (a == b)
        break;
    case TokenType::LTEQ:
        // Runtime synthesis: (a < b) || (a == b)
        break;
    default:
        break;
    }

    if (ref) {
        uint8_t call_result;
        Vector<uint8_t> args;
        if (swap_args) {
            args = {rhs_reg, lhs_reg};
        } else {
            args = {lhs_reg, rhs_reg};
        }
        emit_script_operator_call(*ref, args, call_result);
        registers_.free(lhs_reg);
        registers_.free(rhs_reg);

        if (negate) {
            uint8_t neg_result = registers_.allocate();
            emit_abc(Opcode::NOT, neg_result, call_result, 0);
            registers_.free(call_result);
            result_reg_ = neg_result;
        } else {
            result_reg_ = call_result;
        }
        return;
    }

    uint8_t result = registers_.allocate();
    Opcode op = token_to_comparison_opcode(expr->op.type);
    emit_abc(op, result, lhs_reg, rhs_reg);

    registers_.free(lhs_reg);
    registers_.free(rhs_reg);
    result_reg_ = result;
}

void AstCompiler::visit(LogicalExpression* expr)
{
    TRACK_SOURCE(expr);

    // If the whole expression is constant, emit a single load.
    if (try_emit_const(expr)) return;

    auto load_literal = [&](uint8_t reg, bool value) {
        if (expr->type_id_ == runtime_->bool_type()) {
            emit_abc(Opcode::LOADB, reg, value ? 1 : 0, 0);
        } else {
            emit_asbx(Opcode::LOADI, reg, value ? 1 : 0);
        }
    };

    // DCE: constant LHS short-circuit (only one branch needed)
    if (expr->lhs->is_const_) {
        AstConstEvaluator lhs_eval(script_, expr->lhs);
        if (!lhs_eval.failed_ && !lhs_eval.result_.empty()) {
            bool lhs_val = const_is_truthy(lhs_eval.result_.back(), runtime_);
            uint8_t result = registers_.allocate();
            if (expr->op.type == TokenType::ANDAND) {
                if (!lhs_val) {
                    // false && X → false (RHS never evaluated)
                    load_literal(result, false);
                } else {
                    // true && X → X
                    expr->rhs->accept(this);
                    emit_abc(Opcode::MOVE, result, result_reg_, 0);
                    registers_.free(result_reg_);
                }
            } else {
                if (lhs_val) {
                    // true || X → true (RHS never evaluated)
                    load_literal(result, true);
                } else {
                    // false || X → X
                    expr->rhs->accept(this);
                    emit_abc(Opcode::MOVE, result, result_reg_, 0);
                    registers_.free(result_reg_);
                }
            }
            result_reg_ = result;
            return;
        }
    }

    // Short-circuit evaluation for && and ||
    expr->lhs->accept(this);
    uint8_t lhs_reg = result_reg_;

    uint8_t result = registers_.allocate();

    if (expr->op.type == TokenType::ANDAND) {
        // For &&: if LHS is false, short-circuit to false
        emit_asbx(Opcode::JMPF, lhs_reg, 0);
        uint32_t jmp_false_idx = static_cast<uint32_t>(current_func_->instructions.size() - 1);

        expr->rhs->accept(this);
        emit_abc(Opcode::MOVE, result, result_reg_, 0);
        registers_.free(result_reg_);

        uint32_t jmp_end_idx = emit_jump(Opcode::JMP, 0);

        patch_jump(jmp_false_idx, static_cast<int32_t>(current_func_->instructions.size()));
        load_literal(result, false);

        patch_jump(jmp_end_idx, static_cast<int32_t>(current_func_->instructions.size()));
    } else {
        // For ||: if LHS is true, short-circuit to true
        emit_asbx(Opcode::JMPT, lhs_reg, 0);
        uint32_t jmp_true_idx = static_cast<uint32_t>(current_func_->instructions.size() - 1);

        expr->rhs->accept(this);
        emit_abc(Opcode::MOVE, result, result_reg_, 0);
        registers_.free(result_reg_);

        uint32_t jmp_end_idx = emit_jump(Opcode::JMP, 0);

        patch_jump(jmp_true_idx, static_cast<int32_t>(current_func_->instructions.size()));
        load_literal(result, true);

        patch_jump(jmp_end_idx, static_cast<int32_t>(current_func_->instructions.size()));
    }

    registers_.free(lhs_reg);
    result_reg_ = result;
}

void AstCompiler::visit(UnaryExpression* expr)
{
    if (try_emit_const(expr)) return;

    expr->rhs->accept(this);
    uint8_t operand_reg = result_reg_;

    if (expr->op.type == TokenType::MINUS) {
        auto* ref = runtime_->find_script_unary_op(TokenType::MINUS, expr->rhs->type_id_);
        if (ref) {
            uint8_t call_result;
            Vector<uint8_t> args = {operand_reg};
            emit_script_operator_call(*ref, args, call_result);
            registers_.free(operand_reg);
            result_reg_ = call_result;
            return;
        }
    }

    uint8_t result = registers_.allocate();

    Opcode op;
    switch (expr->op.type) {
    case TokenType::MINUS:
        op = Opcode::NEG;
        break;
    case TokenType::NOT:
        op = Opcode::NOT;
        break;
    default:
        fail(fmt::format("Unknown unary operator: {}", static_cast<int>(expr->op.type)));
        return;
    }

    emit_abc(op, result, operand_reg, 0);

    registers_.free(operand_reg);
    result_reg_ = result;
}

void AstCompiler::visit(AssignExpression* expr)
{
    pending_fixed_array_field_.active = false;

    // Check if this is a compound assignment (+=, *=, etc.)
    bool is_compound = expr->op.type != TokenType::EQ;

    if (is_compound) {
        if (dynamic_cast<TupleLiteral*>(expr->lhs)) {
            fail("Tuple assignment does not support compound operators");
            return;
        }
    }

    // 1. Compile RHS
    expr->rhs->accept(this);
    uint8_t rhs_reg = result_reg_;

    // 2. Handle LHS
    if (auto tuple_lhs = dynamic_cast<TupleLiteral*>(expr->lhs)) {
        size_t element_count = 0;
        const Type* type = runtime_->get_type(expr->rhs->type_id_);
        if (type && type->type_kind == TK_tuple) {
            element_count = runtime_->get_tuple_element_count(expr->rhs->type_id_);
            if (element_count != tuple_lhs->elements.size()) {
                fail("Tuple assignment element count mismatch");
                return;
            }
        } else if (expr->rhs->type_id_ == invalid_type_id) {
            element_count = tuple_lhs->elements.size();
        } else {
            fail("Tuple assignment requires tuple RHS");
            return;
        }

        for (size_t i = 0; i < tuple_lhs->elements.size(); ++i) {
            auto* path = dynamic_cast<PathExpression*>(tuple_lhs->elements[i]);
            if (!path || path->parts.size() != 1) {
                fail("Tuple assignment only supports variable targets");
                return;
            }
            auto* var = dynamic_cast<IdentifierExpression*>(path->parts[0]);
            if (!var) {
                fail("Tuple assignment only supports variable targets");
                return;
            }
            if (i > 255) {
                fail("Tuple index must be a constant integer 0-255");
                return;
            }
            String var_name(var->ident.loc.view());
            auto git = module_globals_.find(var_name);
            if (git != module_globals_.end()) {
                if (git->second.is_const) {
                    fail(fmt::format("cannot assign to const global '{}'", var_name));
                    return;
                }
                uint8_t temp_reg = registers_.allocate();
                emit_abc(Opcode::GETTUPLE, temp_reg, rhs_reg, static_cast<uint8_t>(i));
                emit_abx(Opcode::SETGLOBAL, temp_reg, git->second.slot);
                registers_.free(temp_reg);
            } else if (is_captured_variable(var_name)) {
                uint8_t temp_reg = registers_.allocate();
                emit_abc(Opcode::GETTUPLE, temp_reg, rhs_reg, static_cast<uint8_t>(i));
                emit_abc(Opcode::SETUPVAL, temp_reg, get_upvalue_index(var_name), 0);
                registers_.free(temp_reg);
            } else {
                uint8_t lhs_reg = get_local_register(var_name);
                emit_abc(Opcode::GETTUPLE, lhs_reg, rhs_reg, static_cast<uint8_t>(i));
            }
        }

        result_reg_ = rhs_reg;
        return;
    }

    if (auto path = dynamic_cast<PathExpression*>(expr->lhs)) {
        if (path->parts.size() == 1) {
            auto var = dynamic_cast<IdentifierExpression*>(path->parts[0]);
            if (!var) {
                fail("Assignment target must be identifier");
                return;
            }
            String var_name(var->ident.loc.view());
            auto git = module_globals_.find(var_name);
            if (git != module_globals_.end() && git->second.is_const) {
                fail(fmt::format("cannot assign to const global '{}'", var_name));
                return;
            }
            if (is_compound) {
                Opcode op_code = token_to_binary_opcode(expr->op.type);
                uint8_t current_reg = registers_.allocate();
                if (git != module_globals_.end()) {
                    emit_abx(Opcode::GETGLOBAL, current_reg, git->second.slot);
                } else if (is_captured_variable(var_name)) {
                    emit_abc(Opcode::GETUPVAL, current_reg, get_upvalue_index(var_name), 0);
                } else {
                    uint8_t lhs_reg = get_local_register(var_name);
                    emit_abc(Opcode::MOVE, current_reg, lhs_reg, 0);
                }

                uint8_t value_reg = registers_.allocate();
                emit_abc(op_code, value_reg, current_reg, rhs_reg);

                if (git != module_globals_.end()) {
                    emit_abx(Opcode::SETGLOBAL, value_reg, git->second.slot);
                } else if (is_captured_variable(var_name)) {
                    emit_abc(Opcode::SETUPVAL, value_reg, get_upvalue_index(var_name), 0);
                } else {
                    uint8_t lhs_reg = get_local_register(var_name);
                    emit_abc(Opcode::MOVE, lhs_reg, value_reg, 0);
                }

                registers_.free(current_reg);
                registers_.free(rhs_reg);
                result_reg_ = value_reg;
            } else {
                if (git != module_globals_.end()) {
                    emit_abx(Opcode::SETGLOBAL, rhs_reg, git->second.slot);
                } else if (is_captured_variable(var_name)) {
                    emit_abc(Opcode::SETUPVAL, rhs_reg, get_upvalue_index(var_name), 0);
                } else {
                    uint8_t lhs_reg = get_local_register(var_name);
                    emit_abc(Opcode::MOVE, lhs_reg, rhs_reg, 0);
                }
                result_reg_ = rhs_reg;
            }
        } else {
            path->parts[0]->accept(this);
            uint8_t struct_reg = result_reg_;
            TypeID current_type = path->parts[0]->type_id_;

            for (size_t i = 1; i + 1 < path->parts.size(); ++i) {
                auto ident = dynamic_cast<IdentifierExpression*>(path->parts[i]);
                if (!ident) {
                    fail("Path segment must be identifier");
                    return;
                }

                const Type* type = runtime_->get_type(current_type);
                if (!type || type->type_kind != TK_struct) {
                    fail("Field assignment on non-struct");
                    return;
                }

                auto struct_id = type->type_params[0].as<StructID>();
                const StructDef* struct_def = runtime_->type_table_.get(struct_id);
                uint32_t field_idx = struct_def->field_index(ident->ident.loc.view());
                const FieldDef& field = struct_def->fields[field_idx];

                uint8_t dest_reg = registers_.allocate();
                if (is_value_type(current_type)) {
                    emit_stack_field_get(dest_reg, struct_reg, field.offset, field.type_id);
                } else {
                    emit_field_get(dest_reg, struct_reg, field.offset, field.type_id);
                }
                registers_.free(struct_reg);
                struct_reg = dest_reg;
                current_type = field.type_id;
            }

            auto last_ident = dynamic_cast<IdentifierExpression*>(path->parts.back());
            if (!last_ident) {
                fail("Expected field name in path");
                return;
            }

            const Type* type = runtime_->get_type(current_type);
            if (!type || type->type_kind != TK_struct) {
                fail("Field assignment on non-struct");
                return;
            }
            auto struct_id = type->type_params[0].as<StructID>();
            const StructDef* struct_def = runtime_->type_table_.get(struct_id);
            uint32_t field_idx = struct_def->field_index(last_ident->ident.loc.view());
            const FieldDef& field = struct_def->fields[field_idx];

            if (is_value_type(current_type)) {
                emit_stack_field_set(struct_reg, field.offset, field.type_id, rhs_reg);
            } else {
                emit_field_set(struct_reg, field.offset, field.type_id, rhs_reg);
            }

            registers_.free(struct_reg);
            result_reg_ = rhs_reg;
        }
    } else if (auto index = dynamic_cast<IndexExpression*>(expr->lhs)) {
        // 1. Compile target (map/array)
        bool prev_short_circuit = allow_fixed_array_short_circuit_;
        allow_fixed_array_short_circuit_ = true;
        index->target->accept(this);
        allow_fixed_array_short_circuit_ = prev_short_circuit;
        uint8_t target_reg = result_reg_;

        const Type* type = runtime_->get_type(index->target->type_id_);
        if (!type) {
            fail("Assignment to unknown type");
            return;
        }

        if (type->type_kind == TK_fixed_array) {
            // Check if we have pending fixed array field info (from PathExpression)
            if (pending_fixed_array_field_.active && pending_fixed_array_field_.is_heap_struct) {
                // This is a fixed array field from a heap struct (including propsets)
                auto [has_const_index, index_val] = try_eval_const_int(index->index);

                uint8_t struct_reg = pending_fixed_array_field_.struct_reg;
                uint32_t base_offset = pending_fixed_array_field_.field_offset;
                uint32_t elem_size = pending_fixed_array_field_.elem_size;
                int32_t size = pending_fixed_array_field_.array_size;
                TypeID elem_type_id = pending_fixed_array_field_.elem_type_id;

                if (has_const_index) {
                    if (index_val < 0 || index_val >= size) {
                        fail("Fixed array index out of bounds");
                        registers_.free(struct_reg);
                        pending_fixed_array_field_.active = false;
                        return;
                    }

                    uint32_t effective_offset = base_offset + static_cast<uint32_t>(index_val) * elem_size;
                    uint8_t value_reg = rhs_reg;

                    if (is_compound) {
                        uint8_t current_reg = registers_.allocate();
                        // Read current value
                        emit_field_get(current_reg, struct_reg, effective_offset, elem_type_id);

                        value_reg = registers_.allocate();
                        Opcode op_code = token_to_binary_opcode(expr->op.type);
                        emit_abc(op_code, value_reg, current_reg, rhs_reg);

                        registers_.free(current_reg);
                        registers_.free(rhs_reg);
                    }

                    // Write value to element
                    emit_field_set(struct_reg, effective_offset, elem_type_id, value_reg);

                    registers_.free(struct_reg);
                    pending_fixed_array_field_.active = false;
                    result_reg_ = value_reg;
                    return;
                }

                // Variable index case
                index->index->accept(this);
                uint8_t idx_reg = result_reg_;

                uint8_t offset_reg = emit_fixed_array_element_offset(idx_reg, base_offset, elem_size);

                uint8_t value_reg = rhs_reg;
                if (is_compound) {
                    uint8_t current_reg = registers_.allocate();
                    // Read current value at computed offset
                    Opcode get_op = field_offset_get_opcode(elem_type_id);
                    emit_abc(get_op, current_reg, struct_reg, offset_reg);

                    value_reg = registers_.allocate();
                    Opcode op_code = token_to_binary_opcode(expr->op.type);
                    emit_abc(op_code, value_reg, current_reg, rhs_reg);

                    registers_.free(current_reg);
                    registers_.free(rhs_reg);
                }

                // Write value at computed offset
                Opcode set_op = field_offset_set_opcode(elem_type_id);
                emit_abc(set_op, value_reg, struct_reg, offset_reg);

                registers_.free(offset_reg);
                registers_.free(idx_reg);
                registers_.free(struct_reg);
                pending_fixed_array_field_.active = false;
                result_reg_ = value_reg;
                return;
            }

            // Normal fixed array assignment (stack-backed)
            int32_t index_val = -1;
            bool has_const_index = false;
            if (auto lit = dynamic_cast<LiteralExpression*>(index->index)) {
                if (lit->data.is<int32_t>()) {
                    index_val = lit->data.as<int32_t>();
                    has_const_index = true;
                }
            } else {
                AstConstEvaluator eval(script_, index->index);
                if (!eval.failed_ && !eval.result_.empty() && eval.result_.back().type_id == runtime_->int_type()) {
                    index_val = eval.result_.back().data.ival;
                    has_const_index = true;
                }
            }

            int32_t size = type->type_params[1].as<int32_t>();
            TypeID elem_type_id = type->type_params[0].as<TypeID>();
            const Type* elem_type = runtime_->get_type(elem_type_id);
            if (!elem_type) {
                fail("Fixed array element type not found");
                return;
            }

            if (has_const_index) {
                if (index_val < 0 || index_val >= size) {
                    fail("Fixed array index out of bounds");
                    return;
                }

                uint32_t offset = static_cast<uint32_t>(static_cast<size_t>(index_val) * elem_type->size);
                uint8_t value_reg = rhs_reg;
                if (is_compound) {
                    uint8_t current_reg = registers_.allocate();
                    emit_stack_field_get(current_reg, target_reg, offset, elem_type_id);

                    value_reg = registers_.allocate();
                    Opcode op_code = token_to_binary_opcode(expr->op.type);
                    emit_abc(op_code, value_reg, current_reg, rhs_reg);

                    registers_.free(current_reg);
                    registers_.free(rhs_reg);
                }

                emit_stack_field_set(target_reg, offset, elem_type_id, value_reg);
                registers_.free(target_reg);
                result_reg_ = value_reg;
                return;
            }

            index->index->accept(this);
            uint8_t idx_reg = result_reg_;

            uint8_t value_reg = rhs_reg;
            if (is_compound) {
                uint8_t current_reg = registers_.allocate();
                emit_abc(Opcode::STACK_INDEXGET, current_reg, target_reg, idx_reg);

                value_reg = registers_.allocate();
                Opcode op_code = token_to_binary_opcode(expr->op.type);
                emit_abc(op_code, value_reg, current_reg, rhs_reg);

                registers_.free(current_reg);
                registers_.free(rhs_reg);
            }

            emit_abc(Opcode::STACK_INDEXSET, target_reg, idx_reg, value_reg);
            registers_.free(idx_reg);
            registers_.free(target_reg);
            result_reg_ = value_reg;
            return;
        }

        index->index->accept(this);
        uint8_t key_reg = result_reg_;

        uint8_t value_reg = rhs_reg;
        if (is_compound) {
            uint8_t current_reg = registers_.allocate();
            if (type->type_kind == TK_map) {
                emit_abc(Opcode::MAPGET, current_reg, target_reg, key_reg);
            } else if (type->type_kind == TK_array) {
                emit_abc(Opcode::GETARRAY, current_reg, target_reg, key_reg);
            } else {
                fail("Index assignment only supported for maps and arrays");
                registers_.free(current_reg);
                return;
            }

            value_reg = registers_.allocate();
            Opcode op_code = token_to_binary_opcode(expr->op.type);
            emit_abc(op_code, value_reg, current_reg, rhs_reg);

            registers_.free(current_reg);
            registers_.free(rhs_reg);
        }

        if (type->type_kind == TK_map) {
            emit_abc(Opcode::MAPSET, target_reg, key_reg, value_reg);
        } else if (type->type_kind == TK_array) {
            emit_abc(Opcode::SETARRAY, target_reg, key_reg, value_reg);
        } else {
            fail("Index assignment only supported for maps and arrays");
        }

        registers_.free(key_reg);
        registers_.free(target_reg);
        result_reg_ = value_reg;
    } else {
        // PathExpression, IndexExpression, etc.
        fail("Only variable, dot, and index assignment is currently supported in bytecode compiler");
    }
}

void AstCompiler::visit(ConditionalExpression* expr)
{
    // If whole expression is constant, emit a single load.
    if (try_emit_const(expr)) return;

    // DCE: constant condition — compile only the taken branch.
    if (expr->test->is_const_) {
        AstConstEvaluator test_eval(script_, expr->test);
        if (!test_eval.failed_ && !test_eval.result_.empty()) {
            bool cond = const_is_truthy(test_eval.result_.back(), runtime_);
            if (cond) {
                expr->true_branch->accept(this);
            } else {
                expr->false_branch->accept(this);
            }
            return;
        }
    }

    // 1. Compile Condition
    expr->test->accept(this);
    uint8_t cond_reg = result_reg_;

    // Allocate result register for the whole expression
    uint8_t final_result = registers_.allocate();

    // 2. JMPF to False Branch
    emit_asbx(Opcode::JMPF, cond_reg, 0);
    uint32_t jmp_to_false_idx = static_cast<uint32_t>(current_func_->instructions.size() - 1);
    registers_.free(cond_reg);

    // 3. True Branch
    expr->true_branch->accept(this);
    emit_abc(Opcode::MOVE, final_result, result_reg_, 0);
    registers_.free(result_reg_);

    // 4. JMP to End
    uint32_t jmp_to_end_idx = emit_jump(Opcode::JMP, 0);

    // 5. False Branch
    patch_jump(jmp_to_false_idx, static_cast<int32_t>(current_func_->instructions.size()));

    expr->false_branch->accept(this);
    emit_abc(Opcode::MOVE, final_result, result_reg_, 0);
    registers_.free(result_reg_);

    // 6. End
    patch_jump(jmp_to_end_idx, static_cast<int32_t>(current_func_->instructions.size()));

    result_reg_ = final_result;
}

void AstCompiler::visit(IdentifierExpression* expr)
{
    if (try_emit_const(expr)) return;

    String var_name(expr->ident.loc.view());

    // Check module globals first
    auto git = module_globals_.find(var_name);
    if (git != module_globals_.end()) {
        uint8_t result = registers_.allocate();
        emit_abx(Opcode::GETGLOBAL, result, git->second.slot);
        result_reg_ = result;
        return;
    }

    // Check if this is a captured variable (upvalue)
    if (is_captured_variable(var_name)) {
        uint8_t upval_idx = get_upvalue_index(var_name);
        uint8_t result = registers_.allocate();
        emit_abc(Opcode::GETUPVAL, result, upval_idx, 0);
        result_reg_ = result;
        return;
    }

    if (auto imported = expr->env_.find(var_name)) {
        if (imported->kind == Export::Kind::function) {
            if (imported->is_native) {
                fail(fmt::format("Cannot use native function '{}' as a closure value", var_name));
                return;
            }

            bool is_local_provider = imported->provider_module.empty()
                || imported->provider_module == script_->name();
            if (!is_local_provider) {
                fail(fmt::format("Cannot use imported function '{}.{}' as a closure value", imported->provider_module, var_name));
                return;
            }

            uint32_t func_idx = module_->get_function_index(var_name);
            if (func_idx == UINT32_MAX) {
                fail(fmt::format("Unable to materialize closure for function '{}'", var_name));
                return;
            }

            if (func_idx > std::numeric_limits<uint16_t>::max()) {
                fail(fmt::format("Function index too large for closure: {}", var_name));
                return;
            }

            uint8_t result = registers_.allocate();
            emit_abx(Opcode::CLOSURE, result, static_cast<uint16_t>(func_idx));
            result_reg_ = result;
            return;
        }

        if (imported->kind == Export::Kind::variable) {
            if (try_emit_export_const(imported->const_value)) {
                return;
            }

            if (imported->is_const) {
                if (try_emit_imported_const_from_provider(var_name, *imported)) {
                    return;
                }
                fail(fmt::format("Unable to materialize imported const: {}", var_name));
                return;
            }
        }
    }

    uint8_t var_reg = get_local_register(var_name);

    uint8_t result = registers_.allocate();
    emit_abc(Opcode::MOVE, result, var_reg, 0);

    result_reg_ = result;
}

void AstCompiler::visit(PathExpression* expr)
{
    if (expr->parts.empty()) {
        fail("Empty path expression");
        return;
    }

    if (expr->parts.size() == 1) {
        expr->parts[0]->accept(this);
        return;
    }

    const Type* expr_type = runtime_->get_type(expr->type_id_);
    if (expr_type && expr_type->type_kind == TK_sum && expr->parts.size() == 2) {
        bool is_sum_decl = false;
        if (auto lhs_ident = dynamic_cast<IdentifierExpression*>(expr->parts.front())) {
            String lhs_name(lhs_ident->ident.loc.view());
            auto exp = expr->env_.find(lhs_name);
            if (exp && exp->kind == Export::Kind::type) {
                TypeID tid = exp->type_id;
                if (tid == invalid_type_id && exp->decl) {
                    tid = exp->decl->type_id_;
                }
                if (tid != invalid_type_id) {
                    const Type* lhs_type = runtime_->get_type(tid);
                    is_sum_decl = lhs_type && lhs_type->type_kind == TK_sum;
                }
            }
        }

        if (is_sum_decl) {
            auto sum_id = expr_type->type_params[0].as<SumID>();
            const SumDef* sum_def = runtime_->type_table_.get(sum_id);

            auto var_rhs = dynamic_cast<IdentifierExpression*>(expr->parts.back());
            if (!var_rhs || !sum_def) {
                fail("Invalid sum variant path expression");
                return;
            }

            StringView variant_name = var_rhs->ident.loc.view();
            const VariantDef* variant = sum_def->find_variant(variant_name);
            if (!variant) {
                fail(fmt::format("Variant '{}' not found", variant_name));
                return;
            }

            uint16_t type_idx = static_cast<uint16_t>(module_->type_refs.size());
            module_->type_refs.push_back(expr->type_id_);

            uint8_t dest_reg = registers_.allocate();
            if (is_value_type(expr->type_id_)) {
                emit_abx(Opcode::STACK_ALLOC, dest_reg, type_idx);
            } else {
                emit_abx(Opcode::NEWSUM, dest_reg, type_idx);
            }
            emit_abc(Opcode::SUMINIT, dest_reg, static_cast<uint8_t>(variant->tag_value), 255);

            result_reg_ = dest_reg;
            return;
        }
    }

    expr->parts[0]->accept(this);
    uint8_t current_reg = result_reg_;
    TypeID current_type = expr->parts[0]->type_id_;

    for (size_t i = 1; i < expr->parts.size(); ++i) {
        auto ident = dynamic_cast<IdentifierExpression*>(expr->parts[i]);
        if (!ident) {
            fail("Path segment must be identifier");
            return;
        }

        const Type* type = runtime_->get_type(current_type);
        if (!type || type->type_kind != TK_struct) {
            fail("Path operator on non-struct in bytecode compiler");
            return;
        }

        auto struct_id = type->type_params[0].as<StructID>();
        const StructDef* struct_def = runtime_->type_table_.get(struct_id);
        uint32_t field_idx = struct_def->field_index(ident->ident.loc.view());
        const FieldDef& field = struct_def->fields[field_idx];

        // Check if this is a fixed array field that might be indexed
        const Type* field_type = runtime_->get_type(field.type_id);
        if (allow_fixed_array_short_circuit_ && field_type && field_type->type_kind == TK_fixed_array && field_type->type_params[0].is<TypeID>() && field_type->type_params[1].is<int32_t>()) {
            // This is a fixed array field - set up info for potential direct indexing
            TypeID elem_type_id = field_type->type_params[0].as<TypeID>();
            const Type* elem_type = runtime_->get_type(elem_type_id);
            if (elem_type) {
                pending_fixed_array_field_.struct_reg = current_reg;
                pending_fixed_array_field_.field_offset = field.offset;
                pending_fixed_array_field_.elem_size = elem_type->size;
                pending_fixed_array_field_.array_size = field_type->type_params[1].as<int32_t>();
                pending_fixed_array_field_.elem_type_id = elem_type_id;
                pending_fixed_array_field_.is_heap_struct = !is_value_type(current_type);
                pending_fixed_array_field_.active = true;
                // Don't emit field access - leave it for IndexExpression to handle directly
                current_type = field.type_id;
                // Don't free current_reg - IndexExpression will need it
                result_reg_ = current_reg;
                return;
            }
        }

        uint8_t dest_reg = registers_.allocate();
        if (is_value_type(current_type)) {
            emit_stack_field_get(dest_reg, current_reg, field.offset, field.type_id);
        } else {
            emit_field_get(dest_reg, current_reg, field.offset, field.type_id);
        }

        registers_.free(current_reg);
        current_reg = dest_reg;
        current_type = field.type_id;
    }

    result_reg_ = current_reg;
}

void AstCompiler::visit(IndexExpression* expr)
{
    pending_fixed_array_field_.active = false;

    if (try_emit_const(expr)) return;

    // 1. Compile target
    bool prev_short_circuit = allow_fixed_array_short_circuit_;
    allow_fixed_array_short_circuit_ = true;
    expr->target->accept(this);
    allow_fixed_array_short_circuit_ = prev_short_circuit;
    uint8_t target_reg = result_reg_;

    const Type* type = runtime_->get_type(expr->target->type_id_);
    if (!type) {
        fail("Index on invalid type");
        return;
    }

    if (type->type_kind == TK_tuple) {
        // 2. Determine index (constant int)
        int32_t index_val = -1;
        if (auto lit = dynamic_cast<LiteralExpression*>(expr->index)) {
            if (lit->data.is<int32_t>()) {
                index_val = lit->data.as<int32_t>();
            }
        } else {
            AstConstEvaluator eval(script_, expr->index);
            if (!eval.failed_ && !eval.result_.empty() && eval.result_.back().type_id == runtime_->int_type()) {
                index_val = eval.result_.back().data.ival;
            }
        }

        if (index_val < 0 || index_val > 255) {
            fail("Tuple index must be a constant integer 0-255");
            return;
        }

        // 3. Emit GETTUPLE
        uint8_t dest_reg = registers_.allocate();
        emit_abc(Opcode::GETTUPLE, dest_reg, target_reg, static_cast<uint8_t>(index_val));

        registers_.free(target_reg);
        result_reg_ = dest_reg;
    } else if (type->type_kind == TK_fixed_array) {
        // Check if we have pending fixed array field info (from PathExpression)
        if (pending_fixed_array_field_.active && pending_fixed_array_field_.is_heap_struct) {
            // This is a fixed array field from a heap struct (including propsets)
            // Use direct element access without copying the whole array
            auto [has_const_index, index_val] = try_eval_const_int(expr->index);

            uint8_t struct_reg = pending_fixed_array_field_.struct_reg;
            uint32_t base_offset = pending_fixed_array_field_.field_offset;
            uint32_t elem_size = pending_fixed_array_field_.elem_size;
            int32_t size = pending_fixed_array_field_.array_size;
            TypeID elem_type_id = pending_fixed_array_field_.elem_type_id;

            uint8_t dest_reg = registers_.allocate();

            if (has_const_index) {
                if (index_val < 0 || index_val >= size) {
                    fail("Fixed array index out of bounds");
                    registers_.free(struct_reg);
                    registers_.free(dest_reg);
                    pending_fixed_array_field_.active = false;
                    return;
                }

                // Compute effective offset: field_offset + index * elem_size
                uint32_t effective_offset = base_offset + static_cast<uint32_t>(index_val) * elem_size;

                // Emit direct field access at computed offset
                emit_field_get(dest_reg, struct_reg, effective_offset, elem_type_id);

                registers_.free(struct_reg);
            } else {
                // Variable index: emit code to compute offset
                expr->index->accept(this);
                uint8_t idx_reg = result_reg_;

                uint8_t offset_reg = emit_fixed_array_element_offset(idx_reg, base_offset, elem_size);
                registers_.free(idx_reg);

                // Now emit the field access at the computed offset
                Opcode get_op = field_offset_get_opcode(elem_type_id);

                emit_abc(get_op, dest_reg, struct_reg, offset_reg);
                registers_.free(offset_reg);
                registers_.free(struct_reg);
            }

            pending_fixed_array_field_.active = false;
            result_reg_ = dest_reg;
            return;
        }

        // Normal fixed array handling (stack-backed)
        int32_t index_val = -1;
        bool has_const_index = false;
        if (auto lit = dynamic_cast<LiteralExpression*>(expr->index)) {
            if (lit->data.is<int32_t>()) {
                index_val = lit->data.as<int32_t>();
                has_const_index = true;
            }
        } else {
            AstConstEvaluator eval(script_, expr->index);
            if (!eval.failed_ && !eval.result_.empty() && eval.result_.back().type_id == runtime_->int_type()) {
                index_val = eval.result_.back().data.ival;
                has_const_index = true;
            }
        }

        int32_t size = type->type_params[1].as<int32_t>();
        TypeID elem_type_id = type->type_params[0].as<TypeID>();
        const Type* elem_type = runtime_->get_type(elem_type_id);
        if (!elem_type) {
            fail("Fixed array element type not found");
            return;
        }

        uint8_t dest_reg = registers_.allocate();

        if (has_const_index) {
            if (index_val < 0 || index_val >= size) {
                fail("Fixed array index out of bounds");
                return;
            }

            uint32_t offset = static_cast<uint32_t>(static_cast<size_t>(index_val) * elem_type->size);
            emit_stack_field_get(dest_reg, target_reg, offset, elem_type_id);
            registers_.free(target_reg);
        } else {
            expr->index->accept(this);
            uint8_t idx_reg = result_reg_;
            emit_abc(Opcode::STACK_INDEXGET, dest_reg, target_reg, idx_reg);
            registers_.free(idx_reg);
            registers_.free(target_reg);
        }

        result_reg_ = dest_reg;
    } else if (type->type_kind == TK_array) {
        // 2. Compile Index
        expr->index->accept(this);
        uint8_t idx_reg = result_reg_;

        // 3. Emit GETARRAY
        uint8_t dest_reg = registers_.allocate();
        emit_abc(Opcode::GETARRAY, dest_reg, target_reg, idx_reg);

        registers_.free(idx_reg);
        registers_.free(target_reg);
        result_reg_ = dest_reg;
    } else if (type->type_kind == TK_map) {
        // 2. Compile Key
        expr->index->accept(this);
        uint8_t key_reg = result_reg_;

        // 3. Emit MAPGET
        uint8_t dest_reg = registers_.allocate();
        emit_abc(Opcode::MAPGET, dest_reg, target_reg, key_reg);

        registers_.free(key_reg);
        registers_.free(target_reg);
        result_reg_ = dest_reg;
    } else {
        fail("Indexing not supported on this type");
    }
}

void AstCompiler::visit(CallExpression* expr)
{
    TRACK_SOURCE(expr);

    // Handle explicit str()/hash() calls on types with registered operators
    if (expr->args.size() == 1) {
        auto* path_expr = dynamic_cast<PathExpression*>(expr->expr);
        if (path_expr && path_expr->parts.size() == 1) {
            auto* ident = dynamic_cast<IdentifierExpression*>(path_expr->parts.front());
            if (ident) {
                StringView name = ident->ident.loc.view();
                const ScriptFunctionRef* ref = nullptr;
                if (name == "str") {
                    ref = runtime_->find_str_op(expr->args[0]->type_id_);
                } else if (name == "hash") {
                    ref = runtime_->find_hash_op(expr->args[0]->type_id_);
                }
                if (ref) {
                    expr->args[0]->accept(this);
                    uint8_t arg_reg = result_reg_;
                    uint8_t call_result;
                    Vector<uint8_t> args = {arg_reg};
                    emit_script_operator_call(*ref, args, call_result);
                    registers_.free(arg_reg);
                    result_reg_ = call_result;
                    return;
                }
            }
        }
    }

    const Type* expr_type = runtime_->get_type(expr->type_id_);
    if (expr_type && expr_type->type_kind == TK_sum) {
        auto path_expr = dynamic_cast<PathExpression*>(expr->expr);
        if (path_expr && path_expr->parts.size() == 2) {
            bool is_sum_decl = false;
            if (auto lhs_ident = dynamic_cast<IdentifierExpression*>(path_expr->parts.front())) {
                String lhs_name(lhs_ident->ident.loc.view());
                auto exp = expr->env_.find(lhs_name);
                if (exp && exp->kind == Export::Kind::type) {
                    TypeID tid = exp->type_id;
                    if (tid == invalid_type_id && exp->decl) {
                        tid = exp->decl->type_id_;
                    }
                    if (tid != invalid_type_id) {
                        const Type* lhs_type = runtime_->get_type(tid);
                        is_sum_decl = lhs_type && lhs_type->type_kind == TK_sum;
                    }
                }
            }

            if (is_sum_decl) {
                auto sum_id = expr_type->type_params[0].as<SumID>();
                const SumDef* sum_def = runtime_->type_table_.get(sum_id);

                auto var_rhs = dynamic_cast<IdentifierExpression*>(path_expr->parts.back());
                if (!var_rhs || !sum_def) {
                    fail("Invalid sum variant call expression");
                    return;
                }

                StringView variant_name = var_rhs->ident.loc.view();
                const VariantDef* variant = sum_def->find_variant(variant_name);
                if (!variant) {
                    fail(fmt::format("Variant '{}' not found", variant_name));
                    return;
                }

                uint16_t type_idx = static_cast<uint16_t>(module_->type_refs.size());
                module_->type_refs.push_back(expr->type_id_);

                uint8_t dest_reg = registers_.allocate();
                if (is_value_type(expr->type_id_)) {
                    emit_abx(Opcode::STACK_ALLOC, dest_reg, type_idx);
                } else {
                    emit_abx(Opcode::NEWSUM, dest_reg, type_idx);
                }

                if (expr->args.size() == 1) {
                    expr->args[0]->accept(this);
                    uint8_t payload_reg = result_reg_;
                    emit_abc(Opcode::SUMINIT, dest_reg, static_cast<uint8_t>(variant->tag_value), payload_reg);
                    registers_.free(payload_reg);
                } else {
                    emit_abc(Opcode::SUMINIT, dest_reg, static_cast<uint8_t>(variant->tag_value), 255);
                }

                result_reg_ = dest_reg;
                return;
            }
        }
    }

    if (auto path_expr = dynamic_cast<PathExpression*>(expr->expr)) {
        TypeID newtype_type_id = invalid_type_id;
        if (path_expr->parts.size() == 1) {
            if (auto ident = dynamic_cast<IdentifierExpression*>(path_expr->parts.front())) {
                String name(ident->ident.loc.view());
                auto exp = expr->env_.find(name);
                if (exp) {
                    newtype_type_id = resolve_newtype_type_id(runtime_, exp);
                }
            }
        } else if (path_expr->parts.size() == 2) {
            auto lhs_ident = dynamic_cast<IdentifierExpression*>(path_expr->parts.front());
            auto rhs_ident = dynamic_cast<IdentifierExpression*>(path_expr->parts.back());
            if (lhs_ident && rhs_ident) {
                String lhs_name(lhs_ident->ident.loc.view());
                auto exp = expr->env_.find(lhs_name);
                if (exp && exp->kind == Export::Kind::module_alias && !exp->provider_module.empty()) {
                    auto* provider = runtime_->get_module(exp->provider_module);
                    if (provider) {
                        auto export_ptr = provider->exports().find(String(rhs_ident->ident.loc.view()));
                        if (export_ptr) {
                            newtype_type_id = resolve_newtype_type_id(runtime_, export_ptr);
                        }
                    }
                } else if (exp && exp->decl) {
                    if (auto import_decl = dynamic_cast<const AliasedImportDecl*>(exp->decl)) {
                        if (import_decl->loaded_module) {
                            auto export_ptr = import_decl->loaded_module->exports().find(String(rhs_ident->ident.loc.view()));
                            if (export_ptr) {
                                newtype_type_id = resolve_newtype_type_id(runtime_, export_ptr);
                            }
                        }
                    }
                }
            }
        }

        if (newtype_type_id != invalid_type_id) {
            if (expr->args.size() != 1) {
                fail("Newtype constructor expects 1 argument");
                return;
            }

            expr->args[0]->accept(this);
            uint8_t src_reg = result_reg_;

            TypeID target_tid = newtype_type_id;
            uint16_t type_idx = 0;
            auto it = std::find(module_->type_refs.begin(), module_->type_refs.end(), target_tid);
            if (it == module_->type_refs.end()) {
                type_idx = static_cast<uint16_t>(module_->type_refs.size());
                module_->type_refs.push_back(target_tid);
            } else {
                type_idx = static_cast<uint16_t>(std::distance(module_->type_refs.begin(), it));
            }

            emit_abx(Opcode::CAST, src_reg, type_idx);
            result_reg_ = src_reg;
            return;
        }
    }

    if (expr->intrinsic_id.has_value()) {
        uint16_t intrinsic_id = static_cast<uint16_t>(expr->intrinsic_id.value());

        bool append_propset_type_arg = expr->intrinsic_id.value() == IntrinsicId::GetPropset;
        if (append_propset_type_arg && expr->inferred_type_args.size() != 1) {
            fail("get_propset intrinsic requires exactly one explicit type argument");
            return;
        }

        uint8_t hidden_arg_count = append_propset_type_arg ? 1 : 0;
        uint8_t count = 1 + static_cast<uint8_t>(expr->args.size()) + hidden_arg_count;
        uint8_t base_reg = registers_.allocate_contiguous(count);
        uint8_t dest_reg = base_reg;

        for (size_t i = 0; i < expr->args.size(); ++i) {
            expr->args[i]->accept(this);
            uint8_t target_reg = base_reg + 1 + static_cast<uint8_t>(i);
            if (result_reg_ != target_reg) {
                emit_abc(Opcode::MOVE, target_reg, result_reg_, 0);
                registers_.free(result_reg_);
            }
        }

        if (append_propset_type_arg) {
            uint8_t target_reg = base_reg + 1 + static_cast<uint8_t>(expr->args.size());
            TypeID tid = expr->inferred_type_args[0];
            if (tid.value <= std::numeric_limits<int16_t>::max()) {
                emit_asbx(Opcode::LOADI, target_reg, static_cast<int16_t>(tid.value));
            } else {
                uint32_t k_idx = add_constant_int(static_cast<int32_t>(tid.value));
                if (k_idx > std::numeric_limits<uint16_t>::max()) {
                    fail("Type id constant index too large");
                    return;
                }
                emit_abx(Opcode::LOADK, target_reg, static_cast<uint16_t>(k_idx));
            }
        }

        uint8_t argc = static_cast<uint8_t>(expr->args.size() + hidden_arg_count);
        if (intrinsic_id <= std::numeric_limits<uint8_t>::max()) {
            emit_abc(Opcode::CALLINTR, dest_reg, static_cast<uint8_t>(intrinsic_id), argc);
        } else {
            uint8_t id_reg = registers_.allocate();
            if (intrinsic_id <= std::numeric_limits<int16_t>::max()) {
                emit_asbx(Opcode::LOADI, id_reg, static_cast<int16_t>(intrinsic_id));
            } else {
                uint32_t k_idx = add_constant_int(static_cast<int32_t>(intrinsic_id));
                if (k_idx > std::numeric_limits<uint16_t>::max()) {
                    fail("Intrinsic id constant index too large");
                    registers_.free(id_reg);
                    return;
                }
                emit_abx(Opcode::LOADK, id_reg, static_cast<uint16_t>(k_idx));
            }
            emit_abc(Opcode::CALLINTR_R, dest_reg, id_reg, argc);
            registers_.free(id_reg);
        }

        for (size_t i = 0; i < expr->args.size() + hidden_arg_count; ++i) {
            registers_.free(base_reg + 1 + static_cast<uint8_t>(i));
        }

        result_reg_ = dest_reg;
        return;
    }

    bool is_local_function_value = false;
    if (auto ident = as_identifier(expr->expr)) {
        String name(ident->ident.loc.view());
        is_local_function_value = (local_vars_.find(name) != local_vars_.end())
            || (upvalue_indices_.find(name) != upvalue_indices_.end());
    } else if (auto path_expr = dynamic_cast<PathExpression*>(expr->expr)) {
        if (path_expr->parts.size() == 1) {
            if (auto first = dynamic_cast<IdentifierExpression*>(path_expr->parts.front())) {
                String name(first->ident.loc.view());
                is_local_function_value = (local_vars_.find(name) != local_vars_.end())
                    || (upvalue_indices_.find(name) != upvalue_indices_.end());
            }
        }
    }

    const Type* callee_type = runtime_->get_type(expr->expr->type_id_);
    bool is_direct_symbol_call = false;
    if (auto path_expr = dynamic_cast<PathExpression*>(expr->expr)) {
        if (!path_expr->parts.empty() && path_expr->parts.size() <= 2) {
            bool all_identifiers = true;
            for (auto* part : path_expr->parts) {
                if (!dynamic_cast<IdentifierExpression*>(part)) {
                    all_identifiers = false;
                    break;
                }
            }
            is_direct_symbol_call = all_identifiers;
        }
    }

    if (is_local_function_value || (!is_direct_symbol_call && callee_type && callee_type->type_kind == TK_function)) {
        // Compile callee (closure value)
        expr->expr->accept(this);
        uint8_t closure_reg = result_reg_;

        uint8_t count = 1 + static_cast<uint8_t>(expr->args.size());
        uint8_t base_reg = registers_.allocate_contiguous(count);
        uint8_t dest_reg = base_reg;

        for (size_t i = 0; i < expr->args.size(); ++i) {
            expr->args[i]->accept(this);
            uint8_t target_reg = base_reg + 1 + static_cast<uint8_t>(i);
            if (result_reg_ != target_reg) {
                emit_abc(Opcode::MOVE, target_reg, result_reg_, 0);
                registers_.free(result_reg_);
            }
        }

        uint8_t argc = static_cast<uint8_t>(expr->args.size());
        emit_abc(Opcode::CALLCLOSURE, dest_reg, closure_reg, argc);

        for (size_t i = 0; i < expr->args.size(); ++i) {
            registers_.free(base_reg + 1 + static_cast<uint8_t>(i));
        }
        registers_.free(closure_reg);

        result_reg_ = dest_reg;
        return;
    }

    // 1. Identify function
    String func_name;
    String abi_call_target;
    Script* provider_module = expr->resolved_provider;
    const Export* resolved_export = nullptr;
    if (auto ident = as_identifier(expr->expr)) {
        func_name = String(ident->ident.loc.view());
        auto exp = expr->env_.find(func_name);
        if (exp && exp->kind == Export::Kind::function) {
            resolved_export = exp;
        }
        if (exp && exp->function_abi && !exp->function_abi->call_target.empty()) {
            abi_call_target = exp->function_abi->call_target;
        }
    } else if (auto path_expr = dynamic_cast<PathExpression*>(expr->expr)) {
        if (path_expr->parts.size() == 2) {
            auto lhs_ident = dynamic_cast<IdentifierExpression*>(path_expr->parts.front());
            auto rhs_ident = dynamic_cast<IdentifierExpression*>(path_expr->parts.back());
            if (!lhs_ident || !rhs_ident) {
                fail("Indirect calls not supported");
                return;
            }

            func_name = String(rhs_ident->ident.loc.view());
            if (!provider_module) {
                auto lhs_exp = expr->env_.find(String(lhs_ident->ident.loc.view()));
                if (lhs_exp && lhs_exp->kind == Export::Kind::module_alias
                    && !lhs_exp->provider_module.empty()) {
                    provider_module = runtime_->get_module(lhs_exp->provider_module);
                }
            }

            if (provider_module) {
                auto export_ptr = provider_module->exports().find(func_name);
                if (export_ptr && export_ptr->function_abi
                    && !export_ptr->function_abi->call_target.empty()) {
                    abi_call_target = export_ptr->function_abi->call_target;
                }
                if (export_ptr && export_ptr->kind == Export::Kind::function) {
                    resolved_export = export_ptr;
                }
            }

            if (!provider_module && abi_call_target.empty()) {
                fail("Indirect calls not supported");
                return;
            }
        } else {
            fail("Indirect calls not supported");
            return;
        }
    } else {
        fail("Indirect calls not supported");
        return;
    }

    if (provider_module && abi_call_target.empty()) {
        auto export_ptr = provider_module->exports().find(func_name);
        if (export_ptr && export_ptr->function_abi
            && !export_ptr->function_abi->call_target.empty()) {
            abi_call_target = export_ptr->function_abi->call_target;
        }
        if (export_ptr && export_ptr->kind == Export::Kind::function) {
            resolved_export = export_ptr;
        }
    }

    if (!provider_module && resolved_export && !resolved_export->provider_module.empty()
        && resolved_export->provider_module != script_->name()) {
        provider_module = runtime_->get_module(resolved_export->provider_module);
    }
    if (resolved_export && abi_call_target.empty() && resolved_export->function_abi
        && !resolved_export->function_abi->call_target.empty()) {
        abi_call_target = resolved_export->function_abi->call_target;
    }

    auto is_native_decl = [](const Declaration* decl) {
        if (!decl) { return false; }
        for (const auto& ann : decl->annotations_) {
            if (ann.name.loc.view() == "native") {
                return true;
            }
        }
        return false;
    };

    // Check if this is a call to a generic function
    if (!expr->inferred_type_args.empty()) {
        const FunctionDefinition* generic_def = expr->resolved_func;
        if (generic_def && !generic_def->is_generic()) {
            fail(fmt::format("Generic call '{}' missing resolved generic definition", func_name));
            return;
        }

        Script* defining_script = expr->resolved_provider;
        if (!defining_script) {
            defining_script = provider_module ? provider_module : script_;
        }

        std::unique_ptr<Script> materialized_template_holder;
        const FunctionDefinition* call_signature_def = generic_def;
        if (!call_signature_def) {
            String materialize_error;
            if (!runtime_->materialize_generic_function_definition(
                    defining_script,
                    func_name,
                    materialized_template_holder,
                    call_signature_def,
                    &materialize_error)) {
                fail(materialize_error.empty()
                        ? fmt::format("Generic call '{}' missing resolved generic definition", func_name)
                        : materialize_error);
                return;
            }
        }

        uint32_t inst_limit = ctx_ ? ctx_->limits.max_generic_function_instantiations : 0;
        if (inst_limit != 0 && generic_instantiation_count_ + 1 > inst_limit) {
            fail("generic function instantiation limit exceeded");
            return;
        }
        ++generic_instantiation_count_;

        String inst_error;
        BytecodeModule* defining_module = (defining_script == script_) ? module_ : nullptr;
        std::optional<Runtime::GenericInstantiation> inst_opt;
        if (generic_def) {
            inst_opt = runtime_->ensure_generic_instantiation(
                defining_script,
                defining_module,
                const_cast<FunctionDefinition*>(generic_def),
                expr->inferred_type_args,
                &inst_error);
        } else {
            inst_opt = runtime_->ensure_generic_instantiation_by_name(
                defining_script,
                defining_module,
                func_name,
                expr->inferred_type_args,
                &inst_error);
        }

        if (!inst_opt.has_value()) {
            fail(inst_error.empty() ? "Failed to instantiate generic function" : inst_error);
            return;
        }
        Runtime::GenericInstantiation inst = *inst_opt;

        const FunctionDefinition* func_def = call_signature_def;
        size_t total_args = func_def ? func_def->params.size() : expr->args.size();
        uint8_t count = 1 + static_cast<uint8_t>(total_args);
        uint8_t base_reg = registers_.allocate_contiguous(count);
        uint8_t dest_reg = base_reg;

        for (size_t i = 0; i < expr->args.size(); ++i) {
            expr->args[i]->accept(this);
            uint8_t target_reg = base_reg + 1 + static_cast<uint8_t>(i);
            if (result_reg_ != target_reg) {
                emit_abc(Opcode::MOVE, target_reg, result_reg_, 0);
                registers_.free(result_reg_);
            }
        }

        if (func_def) {
            for (size_t i = expr->args.size(); i < func_def->params.size(); ++i) {
                if (func_def->params[i]->init) {
                    func_def->params[i]->init->accept(this);
                    uint8_t target_reg = base_reg + 1 + static_cast<uint8_t>(i);
                    if (result_reg_ != target_reg) {
                        emit_abc(Opcode::MOVE, target_reg, result_reg_, 0);
                        registers_.free(result_reg_);
                    }
                }
            }
        }

        uint8_t argc = static_cast<uint8_t>(total_args);
        bool can_call_local = (defining_script == script_) && !inst.is_native;
        if (can_call_local) {
            if (inst.func_idx == UINT32_MAX || inst.func_idx > std::numeric_limits<uint8_t>::max()) {
                fail("Instantiated function missing from module");
                return;
            }
            emit_abc(Opcode::CALL, dest_reg, static_cast<uint8_t>(inst.func_idx), argc);
        } else {
            String qualified = String(defining_script->name()) + "." + inst.mangled_name;
            InternedString interned = nw::kernel::strings().intern(qualified);
            uint32_t ext_ref_idx = module_->add_external_ref(interned);
            if (ext_ref_idx > std::numeric_limits<uint8_t>::max()) {
                fail("External ref index > 255 (too many external calls in module)");
                return;
            }
            emit_abc(Opcode::CALLEXT, dest_reg, static_cast<uint8_t>(ext_ref_idx), argc);
        }

        for (size_t i = 0; i < total_args; ++i) {
            registers_.free(base_reg + 1 + static_cast<uint8_t>(i));
        }

        result_reg_ = dest_reg;
        return;
    }

    bool is_local = false;
    uint32_t func_idx = UINT32_MAX;

    String self_prefix = String(script_->name()) + ".";
    bool abi_local_target = !abi_call_target.empty()
        && abi_call_target.size() >= self_prefix.size()
        && abi_call_target.compare(0, self_prefix.size(), self_prefix) == 0;

    if (provider_module && provider_module != script_) {
        String qualified_name = !abi_call_target.empty()
            ? abi_call_target
            : String(provider_module->name()) + "." + func_name;
        InternedString interned = nw::kernel::strings().intern(qualified_name);
        func_idx = module_->add_external_ref(interned);
    } else {
        if (!abi_call_target.empty() && !abi_local_target) {
            InternedString interned = nw::kernel::strings().intern(abi_call_target);
            func_idx = module_->add_external_ref(interned);
        }

        if (func_idx == UINT32_MAX) {
            // First check if function is in current module
            String local_lookup_name = func_name;
            if (abi_local_target) {
                local_lookup_name = abi_call_target.substr(self_prefix.size());
            }
            uint32_t local_idx = module_->get_function_index(local_lookup_name);
            if (local_idx != UINT32_MAX) {
                const CompiledFunction* func = module_->functions[local_idx];
                if (func->source_ast && is_native_decl(func->source_ast)) {
                    // Local native declaration - add external ref
                    String qualified_name = !abi_call_target.empty() ? abi_call_target : String(script_->name()) + "." + local_lookup_name;
                    InternedString interned = nw::kernel::strings().intern(qualified_name);
                    func_idx = module_->add_external_ref(interned);
                } else {
                    // Local script function
                    is_local = true;
                    func_idx = local_idx;
                }
            } else {
                if (!provider_module && resolved_export && !resolved_export->provider_module.empty()) {
                    provider_module = runtime_->get_module(resolved_export->provider_module);
                }

                if (provider_module) {
                    // Found in imports - add external ref with qualified name
                    String qualified_name = !abi_call_target.empty()
                        ? abi_call_target
                        : String(provider_module->name()) + "." + func_name;
                    InternedString interned = nw::kernel::strings().intern(qualified_name);
                    func_idx = module_->add_external_ref(interned);
                } else {
                    // Not found in imports - check core prelude
                    Script* prelude = runtime_->core_prelude();
                    if (prelude && prelude != script_) {
                        auto exp = prelude->exports().find(func_name);
                        if (exp) {
                            String qualified_name = String(prelude->name()) + "." + func_name;
                            InternedString interned = nw::kernel::strings().intern(qualified_name);
                            func_idx = module_->add_external_ref(interned);
                        }
                    }

                    if (func_idx == UINT32_MAX) {
                        // Not found in imports or prelude - try as unqualified native function name
                        // First try unqualified, then qualified with current module
                        InternedString interned = nw::kernel::strings().intern(func_name);
                        if (runtime_->find_external_function(func_name) != UINT32_MAX) {
                            func_idx = module_->add_external_ref(interned);
                        } else {
                            String qualified_name = String(script_->name()) + "." + func_name;
                            interned = nw::kernel::strings().intern(qualified_name);
                            if (runtime_->find_external_function(qualified_name) != UINT32_MAX) {
                                func_idx = module_->add_external_ref(interned);
                            } else {
                                fail(fmt::format("Unknown function: {}", func_name));
                                return;
                            }
                        }
                    }
                }
            }
        }
    }

    // Local ref indices are per-module, so 255 limit is very unlikely to be hit
    if (is_local && func_idx > 255) {
        fail("Local function index too large for CALL instruction");
        return;
    }
    if (!is_local && func_idx > 255) {
        fail("External ref index > 255 (too many external calls in module)");
        return;
    }

    // Calculate total args including defaults
    const FunctionDefinition* func_def = expr->resolved_func;
    size_t total_args = func_def ? func_def->params.size() : expr->args.size();

    // 2. Allocate contiguous block for result + args
    uint8_t count = 1 + static_cast<uint8_t>(total_args);
    uint8_t base_reg = registers_.allocate_contiguous(count);
    uint8_t dest_reg = base_reg;

    // 3. Compile provided arguments and move to proper slots
    for (size_t i = 0; i < expr->args.size(); ++i) {
        expr->args[i]->accept(this);
        uint8_t target_reg = base_reg + 1 + static_cast<uint8_t>(i);

        if (result_reg_ != target_reg) {
            emit_abc(Opcode::MOVE, target_reg, result_reg_, 0);
            registers_.free(result_reg_);
        }
    }

    // 4. Compile default arguments for missing parameters
    if (func_def) {
        for (size_t i = expr->args.size(); i < func_def->params.size(); ++i) {
            if (func_def->params[i]->init) {
                func_def->params[i]->init->accept(this);
                uint8_t target_reg = base_reg + 1 + static_cast<uint8_t>(i);
                if (result_reg_ != target_reg) {
                    emit_abc(Opcode::MOVE, target_reg, result_reg_, 0);
                    registers_.free(result_reg_);
                }
            }
        }
    }

    // 5. Emit call instruction
    uint8_t argc = static_cast<uint8_t>(total_args);
    if (is_local) {
        emit_abc(Opcode::CALL, dest_reg, static_cast<uint8_t>(func_idx), argc);
    } else {
        emit_abc(Opcode::CALLEXT, dest_reg, static_cast<uint8_t>(func_idx), argc);
    }

    // 6. Free argument registers
    for (size_t i = 0; i < total_args; ++i) {
        registers_.free(base_reg + 1 + static_cast<uint8_t>(i));
    }

    result_reg_ = dest_reg;
}

void AstCompiler::visit(CastExpression* expr)
{
    if (try_emit_const(expr)) return;

    // 1. Compile expression
    expr->expr->accept(this);
    uint8_t src_reg = result_reg_;

    // 2. Get Target Type ID
    TypeID target_tid = expr->target_type->type_id_;
    uint16_t type_idx = 0;
    auto it = std::find(module_->type_refs.begin(), module_->type_refs.end(), target_tid);
    if (it == module_->type_refs.end()) {
        type_idx = static_cast<uint16_t>(module_->type_refs.size());
        module_->type_refs.push_back(target_tid);
    } else {
        type_idx = static_cast<uint16_t>(std::distance(module_->type_refs.begin(), it));
    }

    // 3. Allocate result register
    uint8_t dest_reg = registers_.allocate();

    // 4. Emit Opcode
    if (expr->op.type == TokenType::AS) {
        // CAST dest, type_idx (using dest as src initially)
        if (dest_reg != src_reg) {
            emit_abc(Opcode::MOVE, dest_reg, src_reg, 0);
        }
        emit_abx(Opcode::CAST, dest_reg, type_idx);
    } else if (expr->op.type == TokenType::IS) {
        // IS dest, type_idx
        if (dest_reg != src_reg) {
            emit_abc(Opcode::MOVE, dest_reg, src_reg, 0);
        }
        emit_abx(Opcode::IS, dest_reg, type_idx);
    } else {
        fail("Unknown cast operator");
    }

    registers_.free(src_reg);
    result_reg_ = dest_reg;
}

void AstCompiler::visit(LiteralExpression* expr)
{
    uint8_t result = registers_.allocate();

    if (expr->data.is<int32_t>()) {
        int32_t val = expr->data.as<int32_t>();
        // Check if value fits in 16-bit signed immediate
        if (val >= -32768 && val <= 32767) {
            emit_asbx(Opcode::LOADI, result, static_cast<int16_t>(val));
        } else {
            // Use constant pool
            uint32_t k_idx = add_constant_int(val);
            if (k_idx > 65535) {
                fail("Constant pool overflow");
                return;
            }
            emit_abx(Opcode::LOADK, result, static_cast<uint16_t>(k_idx));
        }
    } else if (expr->data.is<float>()) {
        float val = expr->data.as<float>();
        uint32_t k_idx = add_constant_float(val);
        if (k_idx > 65535) {
            fail("Constant pool overflow");
            return;
        }
        emit_abx(Opcode::LOADK, result, static_cast<uint16_t>(k_idx));
    } else if (expr->data.is<bool>()) {
        bool val = expr->data.as<bool>();
        emit_abc(Opcode::LOADB, result, val ? 1 : 0, 0);
    } else if (expr->data.is<PString>()) {
        PString val = expr->data.as<PString>();
        uint32_t k_idx = add_constant_string(val);
        if (k_idx > 65535) {
            fail("Constant pool overflow");
            return;
        }
        emit_abx(Opcode::LOADK, result, static_cast<uint16_t>(k_idx));
    } else {
        // Other types: Location, ObjectID, etc.
        fail(fmt::format("Unsupported literal type in bytecode generation"));
        return;
    }

    result_reg_ = result;
}

void AstCompiler::visit(TupleLiteral* expr)
{
    // 1. Allocate block for elements
    // We need 1 (result/base) + count (elements)
    uint8_t count = static_cast<uint8_t>(expr->elements.size());
    uint8_t base_reg = registers_.allocate_contiguous(count + 1);

    // 2. Compile elements
    for (size_t i = 0; i < expr->elements.size(); ++i) {
        expr->elements[i]->accept(this);
        uint8_t target = base_reg + 1 + static_cast<uint8_t>(i);
        if (result_reg_ != target) {
            emit_abc(Opcode::MOVE, target, result_reg_, 0);
            registers_.free(result_reg_);
        }
    }

    // 3. Emit NEWTUPLE
    emit_abc(Opcode::NEWTUPLE, base_reg, count, 0);

    // 4. Free element registers
    for (size_t i = 0; i < count; ++i) {
        registers_.free(base_reg + 1 + static_cast<uint8_t>(i));
    }

    result_reg_ = base_reg;
}

void AstCompiler::visit(BraceInitLiteral* expr)
{
    // 1. Resolve Type
    TypeID tid = expr->type_id_;
    if (tid == invalid_type_id) {
        fail("Struct literal missing TypeID");
        return;
    }

    const Type* type = runtime_->get_type(tid);
    if (!type) {
        fail("Unknown type for brace initialization");
        return;
    }

    // 2. Add TypeID to module type_refs
    uint16_t type_idx = 0;
    auto it = std::find(module_->type_refs.begin(), module_->type_refs.end(), tid);
    if (it == module_->type_refs.end()) {
        type_idx = static_cast<uint16_t>(module_->type_refs.size());
        module_->type_refs.push_back(tid);
    } else {
        type_idx = static_cast<uint16_t>(std::distance(module_->type_refs.begin(), it));
    }

    // 3. Emit Creation Opcode
    uint8_t dest_reg = registers_.allocate();

    if (type->type_kind == TK_struct) {
        // 4. Initialize fields (Struct)
        auto struct_id = type->type_params[0].as<StructID>();
        const StructDef* struct_def = runtime_->type_table_.get(struct_id);

        // Check if this is a value type (stack-allocated)
        bool use_stack = is_value_type(tid);

        if (use_stack) {
            emit_abx(Opcode::STACK_ALLOC, dest_reg, type_idx);
        } else {
            emit_abx(Opcode::NEWSTRUCT, dest_reg, type_idx);
        }

        if (expr->init_type == BraceInitType::field) {
            for (const auto& item : expr->items) {
                auto var = dynamic_cast<IdentifierExpression*>(item.key);
                if (!var) continue;
                StringView field_name = var->ident.loc.view();
                uint32_t field_idx = struct_def->field_index(field_name);
                const FieldDef& field = struct_def->fields[field_idx];

                item.value->accept(this);
                uint8_t val_reg = result_reg_;

                if (use_stack) {
                    emit_stack_field_set(dest_reg, field.offset, field.type_id, val_reg);
                } else {
                    emit_abc(Opcode::SETFIELD, dest_reg, static_cast<uint8_t>(field_idx), val_reg);
                }
                registers_.free(val_reg);
            }
        } else if (expr->init_type == BraceInitType::list) {
            for (size_t i = 0; i < expr->items.size(); ++i) {
                expr->items[i].value->accept(this);
                uint8_t val_reg = result_reg_;
                const FieldDef& field = struct_def->fields[i];

                if (use_stack) {
                    emit_stack_field_set(dest_reg, field.offset, field.type_id, val_reg);
                } else {
                    emit_abc(Opcode::SETFIELD, dest_reg, static_cast<uint8_t>(i), val_reg);
                }
                registers_.free(val_reg);
            }
        }
    } else if (type->type_kind == TK_map) {
        emit_abx(Opcode::NEWMAP, dest_reg, type_idx);

        // 4. Initialize elements (Map)
        for (const auto& item : expr->items) {
            // Key
            item.key->accept(this);
            uint8_t key_reg = result_reg_;

            // Value
            item.value->accept(this);
            uint8_t val_reg = result_reg_;

            emit_abc(Opcode::MAPSET, dest_reg, key_reg, val_reg);

            registers_.free(val_reg);
            registers_.free(key_reg);
        }
    } else if (type->type_kind == TK_fixed_array) {
        TypeID elem_type_id = type->type_params[0].as<TypeID>();
        int32_t size = type->type_params[1].as<int32_t>();
        const Type* elem_type = runtime_->get_type(elem_type_id);
        if (!elem_type) {
            fail("Fixed array element type not found");
            return;
        }

        emit_abx(Opcode::STACK_ALLOC, dest_reg, type_idx);

        size_t limit = expr->items.size();
        size_t max_size = static_cast<size_t>(size);
        if (limit > max_size) {
            limit = max_size;
        }

        for (size_t i = 0; i < limit; ++i) {
            expr->items[i].value->accept(this);
            uint8_t val_reg = result_reg_;
            uint32_t offset = static_cast<uint32_t>(i * elem_type->size);
            emit_stack_field_set(dest_reg, offset, elem_type_id, val_reg);
            registers_.free(val_reg);
        }
    } else if (type->type_kind == TK_array) {
        uint32_t size = static_cast<uint32_t>(expr->items.size());

        // Load size into dest_reg
        if (size <= 32767) {
            emit_asbx(Opcode::LOADI, dest_reg, static_cast<int16_t>(size));
        } else {
            uint32_t k_idx = add_constant_int(static_cast<int32_t>(size));
            emit_abx(Opcode::LOADK, dest_reg, static_cast<uint16_t>(k_idx));
        }

        // NEWARRAY dest_reg, type_idx
        emit_abx(Opcode::NEWARRAY, dest_reg, type_idx);

        // Initialize elements
        for (size_t i = 0; i < expr->items.size(); ++i) {
            expr->items[i].value->accept(this);
            uint8_t val_reg = result_reg_;

            // Load index into register
            uint8_t idx_reg = registers_.allocate();
            if (i <= 32767) {
                emit_asbx(Opcode::LOADI, idx_reg, static_cast<int16_t>(i));
            } else {
                uint32_t k_idx = add_constant_int(static_cast<int32_t>(i));
                emit_abx(Opcode::LOADK, idx_reg, static_cast<uint16_t>(k_idx));
            }

            // SETARRAY rA[rB] = rC
            emit_abc(Opcode::SETARRAY, dest_reg, idx_reg, val_reg);

            registers_.free(idx_reg);
            registers_.free(val_reg);
        }
    } else {
        fail("Brace initialization only supported for structs, maps, and arrays");
        return;
    }

    result_reg_ = dest_reg;
}

void AstCompiler::visit(FStringExpression* expr)
{
    if (try_emit_const(expr)) return;

    uint8_t total_args = 0;
    for (size_t i = 0; i < expr->parts.size(); ++i) {
        if (!expr->parts[i].empty() || (total_args == 0 && expr->expressions.empty())) {
            total_args++;
        }
        if (i < expr->expressions.size()) {
            total_args++;
        }
    }

    uint8_t count = 1 + total_args;
    uint8_t base_reg = registers_.allocate_contiguous(count);
    uint8_t dest_reg = base_reg;
    uint8_t arg_idx = 0;

    for (size_t i = 0; i < expr->parts.size(); ++i) {
        if (!expr->parts[i].empty()) {
            uint32_t k_idx = add_constant_string(expr->parts[i]);
            if (k_idx > 65535) {
                fail("Constant pool overflow in f-string");
                return;
            }
            uint8_t target_reg = static_cast<uint8_t>(base_reg + 1 + arg_idx);
            emit_abx(Opcode::LOADK, target_reg, static_cast<uint16_t>(k_idx));
            arg_idx++;
        } else if (arg_idx == 0 && expr->expressions.empty()) {
            uint32_t k_idx = add_constant_string("");
            uint8_t target_reg = static_cast<uint8_t>(base_reg + 1 + arg_idx);
            emit_abx(Opcode::LOADK, target_reg, static_cast<uint16_t>(k_idx));
            arg_idx++;
        }

        if (i < expr->expressions.size()) {
            expr->expressions[i]->accept(this);
            uint8_t expr_reg = result_reg_;

            // If the expression type has a str operator, call it first
            auto* str_ref = runtime_->find_str_op(expr->expressions[i]->type_id_);
            if (str_ref) {
                uint8_t str_result;
                Vector<uint8_t> args = {expr_reg};
                emit_script_operator_call(*str_ref, args, str_result);
                registers_.free(expr_reg);
                expr_reg = str_result;
            }

            uint8_t target_reg = static_cast<uint8_t>(base_reg + 1 + arg_idx);
            if (expr_reg != target_reg) {
                emit_abc(Opcode::MOVE, target_reg, expr_reg, 0);
            }
            registers_.free(expr_reg);
            arg_idx++;
        }
    }

    emit_abc(Opcode::CALLINTR, dest_reg,
        static_cast<uint8_t>(IntrinsicId::StringConcat), total_args);

    for (uint8_t i = 1; i < count; ++i) {
        registers_.free(static_cast<uint8_t>(base_reg + i));
    }

    result_reg_ = dest_reg;
}

void AstCompiler::visit(GroupingExpression* expr)
{
    if (try_emit_const(expr)) return;

    if (expr->expr) {
        expr->expr->accept(this);
    }
}

void AstCompiler::visit(TypeExpression* expr)
{
    // Type expressions don't generate code
}

void AstCompiler::visit(EmptyExpression* expr)
{
    // No code to emit
}

void AstCompiler::visit(LambdaExpression* expr)
{
    // 1. Compile lambda body as a separate function in module
    uint16_t func_idx = compile_lambda(expr);
    if (failed_) return;

    if (func_idx >= module_->functions.size()) {
        fail("Lambda function index out of range");
        return;
    }

    const CompiledFunction* lambda_func = module_->functions[func_idx];

    // 2. Allocate register for the closure
    uint8_t dest = registers_.allocate();

    // 3. Emit CLOSURE instruction
    emit_abx(Opcode::CLOSURE, dest, func_idx);

    // 4. Emit packed upvalue descriptors (4 per word)
    const Vector<uint8_t>& descriptors = lambda_func->upvalue_descriptors;
    size_t count = descriptors.size();
    for (size_t i = 0; i < count; i += 4) {
        uint32_t word = 0;
        size_t limit = std::min<size_t>(count - i, 4);
        for (size_t j = 0; j < limit; ++j) {
            word |= static_cast<uint32_t>(descriptors[i + j]) << (8 * j);
        }
        emit(Instruction{word});
    }

    result_reg_ = dest;
}

uint16_t AstCompiler::compile_lambda(LambdaExpression* lambda)
{
    CompiledFunction* saved_func = current_func_;
    RegisterAllocator saved_registers = registers_;
    auto saved_locals = std::move(local_vars_);
    auto saved_upvalues = std::move(upvalue_indices_);
    LambdaExpression* saved_lambda = current_lambda_;

    String lambda_name = fmt::format("$lambda_{}", this->lambda_counter_++);

    CompiledFunction* compiled = new CompiledFunction(lambda_name);
    compiled->param_count = static_cast<uint8_t>(lambda->params.size());
    if (lambda->return_type) {
        compiled->return_type = lambda->return_type->type_id_;
    } else {
        compiled->return_type = runtime_->get_function_return_type(lambda->type_id_);
    }
    compiled->upvalue_count = static_cast<uint8_t>(lambda->captures.size());
    compiled->function_type = lambda->type_id_;

    for (size_t i = 0; i < lambda->captures.size(); ++i) {
        const auto& cap = lambda->captures[i];
        uint8_t descriptor = 0;

        auto local_it = saved_locals.find(cap.name);
        if (local_it != saved_locals.end()) {
            descriptor = (local_it->second.register_index << 1) | 0x01;
        } else {
            auto upval_it = saved_upvalues.find(cap.name);
            if (upval_it != saved_upvalues.end()) {
                descriptor = upval_it->second << 1;
            }
        }
        compiled->upvalue_descriptors.push_back(descriptor);
    }

    current_func_ = compiled;
    current_lambda_ = lambda;
    registers_.reset();
    local_vars_.clear();
    upvalue_indices_.clear();

    for (size_t i = 0; i < lambda->captures.size(); ++i) {
        upvalue_indices_[lambda->captures[i].name] = static_cast<uint8_t>(i);
    }

    for (size_t i = 0; i < lambda->params.size(); ++i) {
        auto* param = lambda->params[i];
        String param_name(param->identifier());
        uint8_t reg = static_cast<uint8_t>(i);
        registers_.mark_used(reg);
        local_vars_[param_name] = VariableInfo{reg, true};
    }

    if (lambda->body) {
        lambda->body->accept(this);
    }

    if (compiled->instructions.empty() || compiled->instructions.back().opcode() != Opcode::RET) {
        emit(Instruction::make_abc(Opcode::CLOSEUPVALS, 0, 0, 0));
        emit(Instruction::make_abc(Opcode::RETVOID, 0, 0, 0));
    }

    compiled->register_count = registers_.high_water_mark();

    uint16_t func_idx = static_cast<uint16_t>(module_->functions.size());
    module_->functions.push_back(compiled);
    lambda->compiled_func_index = func_idx;

    // Restore compilation state
    current_func_ = saved_func;
    registers_ = saved_registers;
    local_vars_ = std::move(saved_locals);
    upvalue_indices_ = std::move(saved_upvalues);
    current_lambda_ = saved_lambda;

    return func_idx;
}

bool AstCompiler::is_captured_variable(StringView name) const
{
    return upvalue_indices_.find(String(name)) != upvalue_indices_.end();
}

uint8_t AstCompiler::get_upvalue_index(StringView name) const
{
    auto it = upvalue_indices_.find(String(name));
    CHECK_F(it != upvalue_indices_.end(), "Unknown upvalue: {}", name);
    return it->second;
}

} // namespace nw::smalls
