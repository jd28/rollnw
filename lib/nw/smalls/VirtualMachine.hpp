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
    uint32_t offset; // Byte offset in stack_
    TypeID type_id;  // Type of the value (for heap ref enumeration)
};

/// Execution state for a single function call
struct CallFrame {
    const BytecodeModule* module = nullptr; // Module this function belongs to
    const CompiledFunction* function = nullptr;
    Closure* closure = nullptr;   // Active closure (null for non-closure calls)
    uint32_t pc = 0;              // Program counter
    uint32_t base_register = 0;   // Index into global register file
    uint32_t return_register = 0; // Where to store result in caller's frame

    // Per-frame stack storage for value types
    Vector<uint8_t> stack_;
    uint32_t stack_top_ = 0;
    Vector<StackSlot> stack_layout_; // For GC root enumeration

    // Allocate space on frame stack for a value type
    uint32_t stack_alloc(uint32_t size, uint32_t alignment, TypeID type_id);

    // Free stack space back to a previous offset (for scope exit)
    void stack_free_to(uint32_t offset);

    // Enumerate heap references in stack-allocated values
    void enumerate_stack_roots(GCRootVisitor& visitor, struct Runtime* runtime);

    // Open upvalues captured from this frame
    Upvalue* open_upvalues = nullptr;
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
    Value execute(const BytecodeModule* module, StringView function_name, const Vector<Value>& args,
        uint64_t gas_limit = 0);

    /// Executes a closure directly
    /// @param gas_limit Gas budget for this execution (0 means unlimited). Only applied for top-level entry.
    Value execute_closure(Closure* closure, const Vector<Value>& args, uint64_t gas_limit = 0);

    /// Main execution loop
    /// @param module The module being executed
    /// @param entry_depth Stop execution when frames.size() reaches this depth (for reentrancy)
    bool run(const BytecodeModule* module, size_t entry_depth = 0);

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

    // Cached pointer to the kernel Runtime, valid for the duration of run().
    // Avoids repeated service-table scans on every opcode dispatch.
    Runtime* rt_ = nullptr;

    inline Value& reg(uint8_t r)
    {
        assert(current_base_ + r < maximum_registers);
        return registers_[current_base_ + r];
    }

    bool consume_gas(uint64_t amount = 1);
    void init_gas(uint64_t gas_limit);

    void push_frame(const BytecodeModule* module, const CompiledFunction* func, uint32_t ret_reg, Closure* closure);
    void pop_frame();

    Upvalue* get_or_create_upvalue(CallFrame& frame, uint8_t reg);
    void close_upvalues(CallFrame& frame);

    void copy_args_to_callee(uint32_t caller_base, uint8_t dest_reg, uint8_t argc, size_t caller_frame_index, const char* opcode_name);

    // Inline helpers used by opcode dispatch
    enum class Truthiness { False,
        True,
        Error };
    Truthiness evaluate_truthiness(const Value& val, const char* opcode_name);
    bool resolve_type_ref(uint16_t type_idx, const BytecodeModule* mod, TypeID& out_tid);
    void* resolve_sum_data(const Value& sum_val, CallFrame& frame, const SumDef*& out_def, const char* opcode_name);
    void call_native_wrapper(const NativeFunctionWrapper& wrapper, Runtime& rt, const Value* args, uint8_t argc,
        uint8_t dest_reg, StringView func_name);
    void setup_script_call(uint8_t dest_reg, uint8_t argc, const BytecodeModule* target_module,
        const CompiledFunction* callee, Closure* closure, const char* opcode_name);
    Value read_stack_value(uint8_t* ptr, TypeID field_type, uint32_t base_offset, uint32_t field_offset,
        Runtime& rt);
    void write_stack_value(uint8_t* ptr, TypeID field_type, const Value& val, uint8_t* stack_data,
        Runtime& rt);

    // Opcode handlers
    void op_arithmetic(Opcode op, uint8_t a, uint8_t b, uint8_t c);
    void op_comparison(Opcode op, uint8_t a, uint8_t b, uint8_t c);
    void op_test_and_skip(Opcode op, uint8_t a, uint8_t b);
    void op_logical(Opcode op, uint8_t a, uint8_t b, uint8_t c);
    void op_field_access(Opcode op, uint8_t a, uint8_t b, uint8_t c, const BytecodeModule* module);
    void call_intrinsic(IntrinsicId id, uint8_t dest_reg, uint8_t argc);
};

} // namespace nw::smalls
