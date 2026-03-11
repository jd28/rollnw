#pragma once

#include "Bytecode.hpp"
#include "VirtualMachineIntrinsics.hpp"
#include "runtime.hpp"

#include <cassert>
#include <memory>

namespace nw::smalls {

/// GC root visitor interface for heap reference enumeration
struct GCRootVisitor {
    virtual ~GCRootVisitor() = default;
    virtual void visit_root(HeapPtr* ptr) = 0;
};

/// Tracks a stack-allocated value type for GC enumeration
struct StackSlot {
    uint32_t offset;             // Byte offset in stack_
    TypeID type_id;              // Type of the value (for heap ref enumeration)
    bool scan_heap_refs = false; // True when type can contain HeapPtr transitively
};

/// Execution state for a single function call
struct GarbageCollector; // Forward declaration

struct CallFrame {
    BytecodeModule* module = nullptr; // Module this function belongs to (mutable for globals)
    const CompiledFunction* function = nullptr;
    Closure* closure = nullptr;   // Active closure (null for non-closure calls)
    uint32_t pc = 0;              // Program counter (kept for debug/stack-trace; ip_ is authoritative in hot path)
    uint32_t base_register = 0;   // Index into global register file
    uint32_t return_register = 0; // Where to store result in caller's frame

    // Caller's ip_ at the point this frame was pushed (restored by pop_frame).
    const Instruction* saved_ip = nullptr;

    // Per-frame stack storage for value types
    Vector<uint8_t> stack_;
    uint32_t stack_top_ = 0;
    Vector<StackSlot> stack_layout_; // For GC root enumeration

    // Allocate space on frame stack for a value type
    uint32_t stack_alloc(uint32_t size, uint32_t alignment, TypeID type_id, bool scan_heap_refs);

    // Free stack space back to a previous offset (for scope exit)
    void stack_free_to(uint32_t offset);

    // Enumerate heap references in stack-allocated values
    void enumerate_stack_roots(GCRootVisitor& visitor, struct Runtime* runtime);

    // Open upvalues captured from this frame
    Upvalue* open_upvalues = nullptr;

    friend struct GarbageCollector; // Allow GC to access private members for optimized root scanning
};

/// Register-based virtual machine for Smalls bytecode
struct VirtualMachine {
    VirtualMachine();

    /// Executes a function in a module
    /// @param module The module containing the function
    /// @param function_name The name of the function to call
    /// @param args Arguments to pass to the function
    /// @return Result value, or Value with invalid_type_id on error
    /// @param gas_limit Gas budget for this execution (0 means unlimited). Only applied for top-level entry.
    Value execute(BytecodeModule* module, StringView function_name, const Vector<Value>& args,
        uint64_t gas_limit = 0);

    /// Executes a pre-resolved compiled function in a module.
    /// @param module The module containing the function
    /// @param function The compiled function to call
    /// @param args Arguments to pass to the function
    /// @return Result value, or Value with invalid_type_id on error
    /// @param gas_limit Gas budget for this execution (0 means unlimited). Only applied for top-level entry.
    Value execute(BytecodeModule* module, const CompiledFunction* function, const Vector<Value>& args,
        uint64_t gas_limit = 0);

    /// Executes a closure directly
    /// @param gas_limit Gas budget for this execution (0 means unlimited). Only applied for top-level entry.
    Value execute_closure(Closure* closure, const Vector<Value>& args, uint64_t gas_limit = 0);

    /// Main execution loop (no step-limit check — fast path)
    /// @param module The module being executed
    /// @param entry_depth Stop execution when frames.size() reaches this depth (for reentrancy)
    bool run(BytecodeModule* module, size_t entry_depth = 0);

    /// Step-limited execution loop — only called when step_limit_enabled_ is true
    bool run_limited(BytecodeModule* module, size_t entry_depth = 0);

    /// Reset VM state
    void reset();

    /// Limit the number of instructions executed before failing.
    /// Used for fuzzing and other bounded execution contexts.
    void set_step_limit(uint64_t max_steps)
    {
        step_limit_enabled_ = true;
        remaining_steps_ = max_steps;
    }

    void clear_step_limit()
    {
        step_limit_enabled_ = false;
        remaining_steps_ = 0;
    }

    /// Returns true if execution failed
    bool failed() const { return failed_; }
    StringView error_message() const { return error_message_; }

    /// Aborts script with message
    void fail(StringView msg);

    /// Enumerate VM roots for GC
    void enumerate_roots(GCRootVisitor& visitor, Runtime* runtime);

    /// Generates a stack trace for the current call stack
    String get_stack_trace() const;

    /// Get the top frame source location
    bool get_top_frame_location(SourceLocation& out_loc, StringView& module_name) const;

    /// Get current frame's stack data (for value-type access)
    uint8_t* current_frame_stack_data();

private:
    static constexpr size_t maximum_registers = 8192;
    std::unique_ptr<Value[]> registers_;
    size_t stack_top_ = 0;
    Vector<CallFrame> frames_;

    friend struct GarbageCollector; // Allow GC to access private members for optimized root scanning

    Value last_result_;
    bool failed_ = false;
    String error_message_;

    bool step_limit_enabled_ = false;
    uint64_t remaining_steps_ = 0;

    bool gas_enabled_ = false;
    uint64_t remaining_gas_ = 0;

    // Cached base register offset of the current frame.
    // Updated by push_frame() and pop_frame() so reg() never touches frames_.
    uint32_t current_base_ = 0;

    // Stable pointer to the current (top) call frame.
    // Updated only by push_frame() and pop_frame(); removed from DISPATCH() hot path.
    CallFrame* frame_ptr_ = nullptr;

    // Direct instruction pointer for the hot dispatch loop.
    // Invariant: ip_ points to the NEXT instruction to execute.
    // Updated by push_frame, pop_frame, and branch (JMP/JMPT/JMPF) handlers.
    const Instruction* ip_ = nullptr;
    const Instruction* ip_end_ = nullptr; // one past the last instruction of the current frame

    // Cached pointer to the kernel Runtime, valid for the duration of run().
    // Avoids repeated service-table scans on every opcode dispatch.
    Runtime* rt_ = nullptr;

    // Per-struct offset→elem_type cache for op_field_offset_access.
    // Key: (struct_type_id.value << 32) | byte_offset  →  elem TypeID
    // Eliminates the O(field_count) linear scan on every propset array field read.
    absl::flat_hash_map<uint64_t, TypeID> field_offset_cache_;
    absl::flat_hash_set<int32_t> field_offset_cache_built_; // struct_type_id.values already cached

    inline Value& reg(uint8_t r)
    {
        assert(current_base_ + r < maximum_registers);
        return registers_[current_base_ + r];
    }

    bool consume_gas(uint64_t amount = 1);
    void init_gas(uint64_t gas_limit);

    void push_frame(BytecodeModule* module, const CompiledFunction* func, uint32_t ret_reg, Closure* closure);
    void pop_frame();

    Upvalue* get_or_create_upvalue(CallFrame& frame, uint8_t reg);
    void close_upvalues(CallFrame& frame);

    void copy_args_to_callee(uint32_t caller_base, uint8_t dest_reg, uint8_t argc,
        size_t caller_frame_index, const char* opcode_name);

    // Inline helpers used by opcode dispatch
    enum class Truthiness { False,
        True,
        Error };
    Truthiness evaluate_truthiness(const Value& val, const char* opcode_name);
    bool resolve_type_ref(uint16_t type_idx, const BytecodeModule* mod, TypeID& out_tid);
    void* resolve_sum_data(const Value& sum_val, CallFrame& frame, const SumDef*& out_def, const char* opcode_name);
    void call_native_pointer(NativeFunctionPointer fn, Runtime& rt, const Value* args, uint8_t argc,
        uint8_t dest_reg, StringView func_name);
    void call_native_wrapper(const NativeFunctionWrapper& wrapper, Runtime& rt, const Value* args, uint8_t argc,
        uint8_t dest_reg, StringView func_name);
    void setup_script_call(uint8_t dest_reg, uint8_t argc, BytecodeModule* target_module,
        const CompiledFunction* callee, Closure* closure, const char* opcode_name);
    Value read_stack_value(uint8_t* ptr, TypeID field_type, uint32_t base_offset, uint32_t field_offset,
        Runtime& rt);
    void write_stack_value(uint8_t* ptr, TypeID field_type, const Value& val, uint8_t* stack_data,
        Runtime& rt);

    template <bool StepLimited>
    bool run_impl(BytecodeModule* module, size_t entry_depth);

    // Opcode handlers
    void op_arithmetic(Opcode op, uint8_t a, uint8_t b, uint8_t c);
    void op_comparison(Opcode op, uint8_t a, uint8_t b, uint8_t c);
    void op_test_and_skip(Opcode op, uint8_t a, uint8_t b);
    void op_logical(Opcode op, uint8_t a, uint8_t b, uint8_t c);
    void op_field_access(Opcode op, uint8_t a, uint8_t b, uint8_t c, const BytecodeModule* module);
    void op_field_offset_access(Opcode op, uint8_t a, uint8_t b, uint8_t c);
    void call_intrinsic(IntrinsicId id, uint8_t dest_reg, uint8_t argc);

    // Builds the field-offset → elem_type cache for a struct type.
    // Called once per struct type on first FIELDX_OFF_R access.
    void ensure_field_offset_cache(TypeID struct_type_id, const StructDef* struct_def);

    // Syncs ip_ → frame_ptr_->pc for get_stack_trace() and error reporting.
    void sync_pc_for_debug();
};

} // namespace nw::smalls
