#pragma once

#include "SourceLocation.hpp"
#include "types.hpp"

#include <cstdint>

namespace nw::smalls {

// Forward Decls
struct BytecodeModule;
struct FunctionDefinition;
struct Value;

// == Opcodes =================================================================
// ============================================================================

enum class Opcode : uint8_t {
    // Arithmetic (binary r0 = r1 op r2)
    ADD = 0,
    SUB,
    MUL,
    DIV,
    MOD,

    // Unary (r0 = op r1)
    NEG,

    // Logical (binary r0 = r1 op r2)
    AND,
    OR,

    // Unary logical (r0 = op r1)
    NOT,

    // Comparison (binary r0 = r1 op r2, result is bool)
    EQ,
    NE,
    LT,
    LE,
    GT,
    GE,

    // Constants & Moves
    LOADK,   // r0 = constants[bx]
    LOADI,   // r0 = sbx (signed 16-bit immediate)
    LOADB,   // r0 = b (boolean, 0 or 1)
    MOVE,    // r0 = r1
    LOADNIL, // r0 = nil (zero value)

    // Memory Operations
    GETFIELD, // r0 = r1.field[c] (struct field access by index - SLOW)
    SETFIELD, // r0.field[b] = r1 (struct field write by index - SLOW)
    GETTUPLE, // r0 = r1[c] (tuple element access by index - SLOW)
    GETARRAY, // r0 = r1[r2] (runtime array index)
    SETARRAY, // r0[r1] = r2 (runtime array set)

    // Optimized Field/Tuple Access
    // Immediate versions: C (or B for SET) is index into module.field_offsets
    // Register versions (_R): C (or B for SET) is a register containing the index
    FIELDGETI,
    FIELDGETI_R, // Int
    FIELDSETI,
    FIELDSETI_R,
    FIELDGETF,
    FIELDGETF_R, // Float
    FIELDSETF,
    FIELDSETF_R,
    FIELDGETB,
    FIELDGETB_R, // Bool
    FIELDSETB,
    FIELDSETB_R,
    FIELDGETS,
    FIELDGETS_R, // String
    FIELDSETS,
    FIELDSETS_R,
    FIELDGETO,
    FIELDGETO_R, // Object
    FIELDSETO,
    FIELDSETO_R,
    FIELDGETH,
    FIELDGETH_R, // Heap
    FIELDSETH,
    FIELDSETH_R,

    // Fixed array field indexing (for inline arrays in heap structs/propsets)
    // These access elements at runtime-computed offsets within fixed array fields
    // A=dest/val, B=struct_reg, C=offset_reg (offset includes base field offset + index*elem_size)
    FIELDGETI_OFF_R, // Read int from heap struct at computed offset
    FIELDSETI_OFF_R, // Write int to heap struct at computed offset
    FIELDGETF_OFF_R, // Read float from heap struct at computed offset
    FIELDSETF_OFF_R, // Write float to heap struct at computed offset
    FIELDGETB_OFF_R, // Read bool from heap struct at computed offset
    FIELDSETB_OFF_R, // Write bool to heap struct at computed offset
    FIELDGETS_OFF_R, // Read string from heap struct at computed offset
    FIELDSETS_OFF_R, // Write string to heap struct at computed offset
    FIELDGETO_OFF_R, // Read object from heap struct at computed offset
    FIELDSETO_OFF_R, // Write object to heap struct at computed offset
    FIELDGETH_OFF_R, // Read heap value from heap struct at computed offset
    FIELDSETH_OFF_R, // Write heap value to heap struct at computed offset

    // Control Flow
    JMP,        // pc += jump_offset (24-bit signed)
    JMPT,       // if r0 then pc += sbx
    JMPF,       // if !r0 then pc += sbx
    CALL,       // r0 = call(func_idx=b, argc=c, args start at r0+1)
    CALLNATIVE, // r0 = call_native(func_idx=b, argc=c, args start at r0+1)
    CALLEXT,    // r0 = call_external(ext_idx=b, argc=c, args start at r0+1)
    CALLEXT_R,  // r0 = call_external(ext_idx=r[b], argc=c) - large index variant
    CALLINTR,   // r0 = call_intrinsic(id=b, argc=c, args start at r0+1)
    CALLINTR_R, // r0 = call_intrinsic(id=r[b], argc=c) - large index variant
    RET,        // return r0
    RETVOID,    // return (void function)

    // Type Operations
    CAST,   // r0 = cast<type_id=bx>(r1)
    TYPEOF, // r0 = typeof(r1)
    IS,     // r0 = (r1 is type_id=bx)

    // Test and Skip (skip next instruction if true)
    ISEQ, // if rA == rB skip next
    ISNE, // if rA != rB skip next
    ISLT, // if rA < rB skip next
    ISLE, // if rA <= rB skip next
    ISGT, // if rA > rB skip next
    ISGE, // if rA >= rB skip next

    // Special (collections)
    NEWSTRUCT, // r0 = alloc_struct(type_id=bx)
    NEWTUPLE,  // r0 = tuple(r0+1..r0+b) (b elements)
    NEWARRAY,  // r0 = array<type_id=bx>(size=r1)
    NEWMAP,    // r0 = map<key_type=bx, val_type=c>()
    MAPGET,    // r0 = r1[r2] (map access)
    MAPSET,    // r0[r1] = r2 (map set)

    // Stack value type operations
    STACK_ALLOC,      // rA = stack_alloc(type_idx=Bx) - allocate value type on frame stack
    STACK_COPY,       // memcpy(stack+rA, stack+rB, size from type) - copy value type
    STACK_FIELDGET,   // rA = *(stack + rB + field_offset[C]) - load field from stack value
    STACK_FIELDGET_R, // rA = *(stack + rB + field_offset[rC]) - register-indexed variant
    STACK_FIELDSET,   // *(stack + rA + field_offset[B]) = rC - store field in stack value
    STACK_FIELDSET_R, // *(stack + rA + field_offset[rB]) = rC - register-indexed variant
    STACK_INDEXGET,   // rA = rB[rC] (fixed array index get)
    STACK_INDEXSET,   // rA[rB] = rC (fixed array index set)

    // Sum type operations
    NEWSUM,        // rA = alloc_sum(type_idx=Bx) - allocate sum value
    SUMINIT,       // rA.tag=B, rA.payload=rC (C=255 means no payload)
    SUMGETTAG,     // rA = tag from sum in rB (reads uint32 at offset 0)
    SUMGETPAYLOAD, // rA = payload from sum in rB, variant_idx=C

    // Closure operations
    CLOSURE,     // rA = closure(func_idx=Bx), upvalue descriptors follow in next instructions
    GETUPVAL,    // rA = upvalue[B]
    SETUPVAL,    // upvalue[B] = rA
    CLOSEUPVALS, // close all open upvalues for current frame
    CALLCLOSURE, // rA = call_closure(closure=rB, argc=C, args start at rA+1)

    // Module globals
    GETGLOBAL, // rA = module.globals[Bx]
    SETGLOBAL, // module.globals[Bx] = rA
};

// == Instruction Encoding ====================================================
// ============================================================================

/// 32-bit fixed-width instruction
///
/// ABC format (most common):
///   Bits 31-24: Opcode
///   Bits 23-16: Register A
///   Bits 15-8:  Register B
///   Bits 7-0:   Register C
///
/// ABx format (for constants, type IDs):
///   Bits 31-24: Opcode
///   Bits 23-16: Register A
///   Bits 15-0:  16-bit unsigned immediate (Bx)
///
/// AsBx format (for small signed immediates):
///   Bits 31-24: Opcode
///   Bits 23-16: Register A
///   Bits 15-0:  16-bit signed immediate (sBx)
///
/// Jump format (for branches):
///   Bits 31-24: Opcode
///   Bits 23-0:  24-bit signed offset
struct Instruction {
    uint32_t raw;

    // Accessors
    Opcode opcode() const { return static_cast<Opcode>((raw >> 24) & 0xFF); }
    uint8_t arg_a() const { return static_cast<uint8_t>((raw >> 16) & 0xFF); }
    uint8_t arg_b() const { return static_cast<uint8_t>((raw >> 8) & 0xFF); }
    uint8_t arg_c() const { return static_cast<uint8_t>(raw & 0xFF); }
    uint16_t arg_bx() const { return static_cast<uint16_t>(raw & 0xFFFF); }
    int16_t arg_sbx() const { return static_cast<int16_t>(raw & 0xFFFF); }

    // 24-bit signed jump offset (sign-extended)
    int32_t arg_jump() const
    {
        int32_t offset = static_cast<int32_t>(raw & 0xFFFFFF);
        // Sign-extend from 24 bits to 32 bits
        if (offset & 0x800000) {
            offset |= 0xFF000000;
        }
        return offset;
    }

    // Factory methods
    static Instruction make_abc(Opcode op, uint8_t a, uint8_t b, uint8_t c);
    static Instruction make_abx(Opcode op, uint8_t a, uint16_t bx);
    static Instruction make_asbx(Opcode op, uint8_t a, int16_t sbx);
    static Instruction make_jump(Opcode op, int32_t offset);
};

// == Constant Pool ===========================================================
// ============================================================================

struct Constant {
    TypeID type_id;

    union {
        int32_t ival;
        float fval;
        uint32_t string_idx; // Index into BytecodeModule::string_pool
    } data;

    static Constant make_int(int32_t v);
    static Constant make_float(float v);
    static Constant make_string(uint32_t idx);
};

// == Compiled Function =======================================================
// ============================================================================

struct CompiledFunction {
    CompiledFunction() = default;
    explicit CompiledFunction(String name_);
    ~CompiledFunction() = default;

    // Non-copyable, moveable
    CompiledFunction(const CompiledFunction&) = delete;
    CompiledFunction& operator=(const CompiledFunction&) = delete;
    CompiledFunction(CompiledFunction&&) = default;
    CompiledFunction& operator=(CompiledFunction&&) = default;

    void disassemble(String* result, const BytecodeModule* module = nullptr) const;

    String name;
    uint8_t param_count = 0;
    uint8_t register_count = 0; // Total registers needed (including params)
    uint8_t upvalue_count = 0;  // Number of upvalues this function captures
    TypeID return_type = invalid_type_id;
    TypeID function_type = invalid_type_id;

    Vector<Instruction> instructions;
    Vector<uint32_t> constant_refs; // Indices into BytecodeModule::constants

    // For closures: describes how to capture each upvalue
    // Each entry is packed: bit 0 = is_local (from enclosing frame's register),
    // bits 1-7 = index (register index if local, upvalue index if not)
    Vector<uint8_t> upvalue_descriptors;

    // Debug info (maps instruction index to source location)
    Vector<SourceLocation> debug_locations;

    // Back-reference to source AST (for debugging/stack traces)
    const FunctionDefinition* source_ast = nullptr;
};

// == Bytecode Module =========================================================
// ============================================================================

struct BytecodeModule {
    explicit BytecodeModule(String _name);
    ~BytecodeModule();

    BytecodeModule(const BytecodeModule&) = delete;
    BytecodeModule& operator=(const BytecodeModule&) = delete;
    BytecodeModule(BytecodeModule&&) = default;
    BytecodeModule& operator=(BytecodeModule&&) = default;

    String disassemble() const;

    // Function management
    const CompiledFunction* get_function(StringView name) const;
    uint32_t get_function_index(StringView name) const;
    CompiledFunction* add_function(CompiledFunction* func);

    // String pool management (deduplicates)
    uint32_t add_string(StringView str);
    const String& get_string(uint32_t idx) const;

    // Constant pool management
    uint32_t add_constant(const Constant& c);
    const Constant& get_constant(uint32_t idx) const;

    // Field reference management
    uint32_t add_field_ref(uint32_t offset, TypeID type_id);

    // External function reference management (for cross-module/native calls)
    uint32_t add_external_ref(InternedString qualified_name);
    bool resolve_external_refs(struct Runtime* runtime);

    String module_name;
    Vector<String> string_pool;
    absl::flat_hash_map<String, uint32_t> string_indices;
    Vector<Constant> constants;
    Vector<CompiledFunction*> functions;
    absl::flat_hash_map<String, uint32_t> function_indices;
    Vector<TypeID> type_refs;
    Vector<uint32_t> field_offsets;
    Vector<TypeID> field_types;

    // Verification cache for VM entrypoints.
    bool verification_attempted = false;
    bool verification_passed = false;
    String verification_error;

    // Module-scope globals (var/const at top level). These are runtime state
    // that can be modified during execution (e.g., via SETGLOBAL opcode).
    uint32_t global_count = 0;
    Vector<Value> globals;

    // External function indirection table
    Vector<InternedString> external_refs;                           // qualified names (stable)
    Vector<uint32_t> external_indices;                              // resolved runtime indices (rebuilt on reload)
    absl::flat_hash_map<InternedString, uint32_t> external_ref_map; // name -> local ref index
};

} // namespace nw::smalls
