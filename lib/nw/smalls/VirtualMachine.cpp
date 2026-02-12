#include "VirtualMachine.hpp"

#include "BytecodeVerifier.hpp"

#include "../kernel/Kernel.hpp"
#include "../log.hpp"

#include <absl/strings/str_cat.h>
#include <cctype>
#include <cstring>

namespace nw::smalls {

VirtualMachine::VirtualMachine()
    : registers_(std::make_unique<Value[]>(maximum_registers))
{
    frames_.reserve(64);
}

void VirtualMachine::reset()
{
    for (auto& frame : frames_) {
        close_upvalues(frame);
    }
    stack_top_ = 0;
    frames_.clear();
    failed_ = false;
    error_message_.clear();

    gas_enabled_ = false;
    remaining_gas_ = 0;
}

bool VirtualMachine::consume_gas(uint64_t amount)
{
    if (!gas_enabled_) {
        return true;
    }

    if (amount == 0) {
        return true;
    }

    if (remaining_gas_ < amount) {
        remaining_gas_ = 0;
        fail("Script exceeded execution limit");
        return false;
    }

    remaining_gas_ -= amount;
    return true;
}

void VirtualMachine::fail(StringView msg)
{
    if (!failed_) {
        failed_ = true;
        error_message_ = String(msg);

        // Generate stack trace
        String stack_trace = get_stack_trace();
        LOG_F(ERROR, "[vm] {}\n{}", msg, stack_trace);
    }
}

void VirtualMachine::enumerate_roots(GCRootVisitor& visitor, Runtime* runtime)
{
    if (!runtime) {
        return;
    }

    for (auto& frame : frames_) {
        frame.enumerate_stack_roots(visitor, runtime);

        for (Upvalue* uv = frame.open_upvalues; uv; uv = uv->next) {
            if (uv->heap_ptr.value != 0) {
                visitor.visit_root(&uv->heap_ptr);
            }
            if (!uv->is_open() && uv->closed.storage == ValueStorage::heap && uv->closed.data.hptr.value != 0) {
                visitor.visit_root(&uv->closed.data.hptr);
            }
        }
    }

    for (size_t i = 0; i < stack_top_; ++i) {
        Value& value = registers_[i];
        if (value.storage == ValueStorage::heap && value.data.hptr.value != 0) {
            visitor.visit_root(&value.data.hptr);

            const Type* type = runtime->get_type(value.type_id);
            if (type && type->type_kind == TK_function) {
                Closure* closure = runtime->get_closure(value.data.hptr);
                if (closure) {
                    for (Upvalue* uv : closure->upvalues) {
                        if (!uv) { continue; }
                        if (uv->heap_ptr.value != 0) {
                            visitor.visit_root(&uv->heap_ptr);
                        }
                        if (!uv->is_open() && uv->closed.storage == ValueStorage::heap && uv->closed.data.hptr.value != 0) {
                            visitor.visit_root(&uv->closed.data.hptr);
                        }
                    }
                }
            }
        }
    }
}

uint8_t* VirtualMachine::current_frame_stack_data()
{
    if (frames_.empty()) { return nullptr; }
    return frames_.back().stack_.data();
}

String VirtualMachine::get_stack_trace() const
{
    auto& rt = nw::kernel::runtime();
    auto config = rt.diagnostic_config();

    if (frames_.empty()) {
        return "Stack trace: (no frames)\n";
    }

    String trace = "Stack trace:\n";

    for (size_t i = frames_.size(); i > 0; --i) {
        const CallFrame& frame = frames_[i - 1];

        if (!frame.function) {
            trace += fmt::format("  #{}: <unknown function>\n", frames_.size() - i);
            continue;
        }

        // Get function name
        const String& func_name = frame.function->name;
        StringView module_name = "<unknown module>";
        if (frame.module) {
            module_name = frame.module->module_name;
        }
        String qualified = fmt::format("{}::{}", module_name, func_name);

        // Get source location if available
        if (frame.pc > 0 && frame.pc - 1 < frame.function->debug_locations.size()) {
            const SourceLocation& loc = frame.function->debug_locations[frame.pc - 1];
            if (loc.range.start.line > 0) {
                trace += fmt::format("  #{}: {} at line {}, column {}\n",
                    frames_.size() - i,
                    qualified,
                    loc.range.start.line,
                    loc.range.start.column);
                if (i == frames_.size() && config.debug_level != DebugLevel::none) {
                    StringView line_view;
                    if (rt.get_source_line(module_name, loc.range.start.line, line_view)) {
                        trace += fmt::format("      {}\n      ", line_view);
                        for (size_t c = 0; c < loc.range.start.column; ++c) {
                            if (c < line_view.size() && line_view[c] == '\t') {
                                trace += '\t';
                            } else {
                                trace += ' ';
                            }
                        }
                        trace += '^';
                        trace += '\n';
                    }
                }
                continue;
            }
        }

        // Fallback: no source location
        trace += fmt::format("  #{}: {} at <unknown location>\n",
            frames_.size() - i,
            qualified);
    }

    return trace;
}

bool VirtualMachine::get_top_frame_location(SourceLocation& out_loc, StringView& module_name) const
{
    if (frames_.empty()) { return false; }

    const CallFrame& frame = frames_.back();
    if (!frame.function) { return false; }

    if (frame.pc > 0 && frame.pc - 1 < frame.function->debug_locations.size()) {
        out_loc = frame.function->debug_locations[frame.pc - 1];
        if (frame.module) {
            module_name = frame.module->module_name;
        } else {
            module_name = "";
        }
        return true;
    }

    return false;
}

void VirtualMachine::push_frame(const BytecodeModule* module, const CompiledFunction* func, uint32_t ret_reg, Closure* closure)
{
    if (gas_enabled_ && !consume_gas()) { return; }

    if (frames_.size() >= 64) {
        fail("Stack overflow: max call depth reached");
        return;
    }

    uint32_t base = static_cast<uint32_t>(stack_top_);

    if (base + func->register_count > maximum_registers) {
        fail("Register stack overflow");
        return;
    }

    stack_top_ = base + func->register_count;

    CallFrame frame;
    frame.module = module;
    frame.function = func;
    frame.closure = closure;
    frame.pc = 0;
    frame.base_register = base;
    frame.return_register = ret_reg;
    frame.open_upvalues = nullptr;
    frames_.push_back(frame);
}

void VirtualMachine::pop_frame()
{
    if (frames_.empty()) { return; }

    CallFrame frame = frames_.back();
    close_upvalues(frame);
    stack_top_ = frame.base_register;
    frames_.pop_back();
}

Upvalue* VirtualMachine::get_or_create_upvalue(CallFrame& frame, uint8_t reg)
{
    Value* location = &registers_[frame.base_register + reg];
    for (Upvalue* uv = frame.open_upvalues; uv; uv = uv->next) {
        if (uv->location == location) { return uv; }
    }

    auto& rt = nw::kernel::runtime();
    Upvalue* uv = rt.alloc_upvalue();
    uv->location = location;
    uv->next = frame.open_upvalues;
    frame.open_upvalues = uv;
    return uv;
}

void VirtualMachine::close_upvalues(CallFrame& frame)
{
    for (Upvalue* uv = frame.open_upvalues; uv;) {
        Upvalue* next = uv->next;
        uv->closed = *uv->location;
        uv->location = &uv->closed;
        uv = next;
    }
    frame.open_upvalues = nullptr;
}

void VirtualMachine::init_gas(uint64_t gas_limit)
{
    if (gas_limit == 0) {
        gas_enabled_ = false;
        remaining_gas_ = 0;
    } else {
        gas_enabled_ = true;
        remaining_gas_ = gas_limit;
    }
}

VirtualMachine::Truthiness VirtualMachine::evaluate_truthiness(const Value& val, const char* opcode_name)
{
    auto& rt = nw::kernel::runtime();
    if (val.type_id == rt.bool_type()) {
        return val.data.bval ? Truthiness::True : Truthiness::False;
    }
    if (val.type_id == rt.int_type()) {
        return val.data.ival != 0 ? Truthiness::True : Truthiness::False;
    }
    const Type* type = rt.get_type(val.type_id);
    if (type && type->type_kind == TK_function) {
        return val.storage == ValueStorage::heap && val.data.hptr.value != 0
            ? Truthiness::True
            : Truthiness::False;
    }
    fail(fmt::format("{} condition must be bool, int, or closure", opcode_name));
    return Truthiness::Error;
}

bool VirtualMachine::resolve_type_ref(uint16_t type_idx, const BytecodeModule* mod, TypeID& out_tid)
{
    if (type_idx >= mod->type_refs.size()) {
        fail("Type index out of range");
        return false;
    }
    out_tid = mod->type_refs[type_idx];
    return true;
}

void* VirtualMachine::resolve_sum_data(const Value& sum_val, CallFrame& frame,
    const SumDef*& out_def, const char* opcode_name)
{
    auto& rt = nw::kernel::runtime();
    const Type* type = rt.get_type(sum_val.type_id);
    if (!type || type->type_kind != TK_sum) {
        fail(fmt::format("{} called on non-sum type", opcode_name));
        return nullptr;
    }

    auto sum_id = type->type_params[0].as<SumID>();
    out_def = rt.type_table_.get(sum_id);
    if (!out_def) {
        fail("Invalid sum definition");
        return nullptr;
    }

    if (sum_val.storage == ValueStorage::stack) {
        return frame.stack_.data() + sum_val.data.stack_offset;
    }
    return rt.heap_.get_ptr(sum_val.data.hptr);
}

void VirtualMachine::call_native_wrapper(const NativeFunctionWrapper& wrapper, Runtime& rt,
    const Value* args, uint8_t argc, uint8_t dest_reg, StringView func_name)
{
    try {
        Value result = wrapper(&rt, args, argc);
        reg(dest_reg) = result;
    } catch (const std::exception& e) {
        fail(fmt::format("Native function '{}' threw exception: {}", func_name, e.what()));
    } catch (...) {
        fail(fmt::format("Native function '{}' threw unknown exception", func_name));
    }
}

void VirtualMachine::setup_script_call(uint8_t dest_reg, uint8_t argc,
    const BytecodeModule* target_module, const CompiledFunction* callee,
    Closure* closure, const char* opcode_name)
{
    Vector<Value> args;
    args.reserve(argc);
    for (uint8_t i = 0; i < argc; ++i) {
        args.push_back(reg(static_cast<uint8_t>(dest_reg + 1 + i)));
    }

    size_t caller_frame_index = frames_.empty() ? 0 : frames_.size() - 1;

    push_frame(target_module, callee, dest_reg, closure);
    if (failed_) { return; }

    copy_args_to_callee(args, caller_frame_index, opcode_name);
}

Value VirtualMachine::read_stack_value(uint8_t* ptr, TypeID field_type,
    uint32_t base_offset, uint32_t field_offset, Runtime& rt)
{
    if (field_type == rt.int_type()) {
        return Value::make_int(*reinterpret_cast<int32_t*>(ptr));
    }
    if (field_type == rt.float_type()) {
        return Value::make_float(*reinterpret_cast<float*>(ptr));
    }
    if (field_type == rt.bool_type()) {
        return Value::make_bool(*reinterpret_cast<bool*>(ptr));
    }
    if (field_type == rt.string_type()) {
        return Value::make_string(*reinterpret_cast<HeapPtr*>(ptr));
    }
    if (field_type == rt.object_type()) {
        return Value::make_object(*reinterpret_cast<ObjectHandle*>(ptr));
    }

    const Type* ft = rt.get_type(field_type);
    if (ft && (ft->type_kind == TK_struct || ft->type_kind == TK_fixed_array)) {
        return Value::make_stack(base_offset + field_offset, field_type);
    }
    return Value::make_heap(*reinterpret_cast<HeapPtr*>(ptr), field_type);
}

void VirtualMachine::write_stack_value(uint8_t* ptr, TypeID field_type, const Value& val,
    uint8_t* stack_data, Runtime& rt)
{
    if (field_type == rt.int_type()) {
        if (val.type_id == rt.bool_type()) {
            *reinterpret_cast<int32_t*>(ptr) = val.data.ival ? 1 : 0;
        } else {
            *reinterpret_cast<int32_t*>(ptr) = val.data.ival;
        }
    } else if (field_type == rt.float_type()) {
        *reinterpret_cast<float*>(ptr) = val.data.fval;
    } else if (field_type == rt.bool_type()) {
        if (val.type_id == rt.int_type()) {
            *reinterpret_cast<bool*>(ptr) = val.data.ival != 0;
        } else {
            *reinterpret_cast<bool*>(ptr) = val.data.bval;
        }
    } else if (field_type == rt.string_type()) {
        *reinterpret_cast<HeapPtr*>(ptr) = val.data.hptr;
    } else if (field_type == rt.object_type()) {
        *reinterpret_cast<ObjectHandle*>(ptr) = val.data.oval;
    } else if (val.storage == ValueStorage::stack) {
        const Type* ft = rt.get_type(field_type);
        if (ft) {
            std::memcpy(ptr, stack_data + val.data.stack_offset, ft->size);
        }
    } else {
        *reinterpret_cast<HeapPtr*>(ptr) = val.data.hptr;
    }
}

void VirtualMachine::copy_args_to_callee(Vector<Value>& args, size_t caller_frame_index, const char* opcode_name)
{
    CallFrame& caller_frame = frames_[caller_frame_index];
    CallFrame& callee_frame = frames_.back();
    auto& rt = nw::kernel::runtime();

    for (size_t i = 0; i < args.size(); ++i) {
        Value arg = args[i];
        if (arg.storage == ValueStorage::stack) {
            const Type* type = rt.get_type(arg.type_id);
            if (!type) {
                fail(fmt::format("{} argument has invalid type", opcode_name));
                return;
            }

            uint32_t dst_offset = callee_frame.stack_alloc(type->size, type->alignment, arg.type_id);
            std::memcpy(
                callee_frame.stack_.data() + dst_offset,
                caller_frame.stack_.data() + arg.data.stack_offset,
                type->size);
            arg = Value::make_stack(dst_offset, arg.type_id);
        }

        reg(static_cast<uint8_t>(i)) = arg;
    }
}

// == CallFrame Stack Allocation ==============================================

uint32_t CallFrame::stack_alloc(uint32_t size, uint32_t alignment, TypeID type_id)
{
    uint32_t aligned = (stack_top_ + alignment - 1) & ~(alignment - 1);
    if (aligned + size > stack_.size()) {
        stack_.resize(aligned + size);
    }

    stack_layout_.push_back({aligned, type_id});
    std::memset(stack_.data() + aligned, 0, size);

    stack_top_ = aligned + size;
    return aligned;
}

void CallFrame::stack_free_to(uint32_t offset)
{
    stack_top_ = offset;
    while (!stack_layout_.empty() && stack_layout_.back().offset >= offset) {
        stack_layout_.pop_back();
    }
}

void CallFrame::enumerate_stack_roots(GCRootVisitor& visitor, Runtime* runtime)
{
    for (const StackSlot& slot : stack_layout_) {
        uint8_t* base = stack_.data() + slot.offset;
        runtime->scan_value_heap_refs(slot.type_id, base, [&](HeapPtr* ptr) {
            visitor.visit_root(ptr);
        });
    }
}

Value VirtualMachine::execute(const BytecodeModule* module, StringView function_name, const Vector<Value>& args,
    uint64_t gas_limit)
{
    // Track entry frame depth for reentrant execution
    size_t entry_depth = frames_.size();
    Value saved_reg0{};
    bool saved_reg0_valid = false;

    if (entry_depth == 0) {
        reset();
        init_gas(gas_limit);
    } else if (!frames_.empty()) {
        saved_reg0 = reg(0);
        saved_reg0_valid = true;
    }

    if (!module->external_refs.empty() && (module->external_indices.empty() || module->external_indices[0] == UINT32_MAX)) {
        if (!const_cast<BytecodeModule*>(module)->resolve_external_refs(&nw::kernel::runtime())) {
            fail("Failed to resolve external function references");
            return {};
        }
    }

    const CompiledFunction* func = module->get_function(function_name);
    if (!func) {
        fail(fmt::format("Function not found: {}", function_name));
        return {};
    }

    String verify_error;
    if (!verify_bytecode_module(module, &verify_error)) {
        fail(verify_error);
        return {};
    }

    if (args.size() != func->param_count) {
        fail(fmt::format("Argument count mismatch for {}: expected {}, got {}",
            function_name, func->param_count, args.size()));
        return {};
    }

    push_frame(module, func, 0, nullptr);
    if (failed_) { return {}; }

    for (size_t i = 0; i < args.size(); ++i) {
        reg(static_cast<uint8_t>(i)) = args[i];
    }

    bool success = run(module, entry_depth);

    if (success) {
        // For reentrant calls, the return value was written to register 0 of the entry frame
        if (entry_depth > 0 && !frames_.empty()) {
            Value result = reg(0);
            if (saved_reg0_valid) {
                reg(0) = saved_reg0;
            }
            return result;
        }
        return last_result_;
    }

    if (entry_depth > 0 && saved_reg0_valid && !frames_.empty()) {
        reg(0) = saved_reg0;
    }

    return {};
}

Value VirtualMachine::execute_closure(Closure* closure, const Vector<Value>& args, uint64_t gas_limit)
{
    if (!closure || !closure->function || !closure->module) {
        fail("execute_closure: invalid closure");
        return {};
    }

    size_t entry_depth = frames_.size();
    Value saved_reg0{};
    bool saved_reg0_valid = false;
    if (entry_depth == 0) {
        reset();
        init_gas(gas_limit);
    } else if (!frames_.empty()) {
        saved_reg0 = reg(0);
        saved_reg0_valid = true;
    }

    const CompiledFunction* func = closure->function;
    const BytecodeModule* module = closure->module;

    String verify_error;
    if (!verify_bytecode_module(module, &verify_error)) {
        fail(verify_error);
        return {};
    }

    if (args.size() != func->param_count) {
        fail(fmt::format("Argument count mismatch: expected {}, got {}",
            func->param_count, args.size()));
        return {};
    }

    push_frame(module, func, 0, closure);
    if (failed_) return {};

    for (size_t i = 0; i < args.size(); ++i) {
        reg(static_cast<uint8_t>(i)) = args[i];
    }

    bool success = run(module, entry_depth);

    if (success) {
        if (entry_depth > 0 && !frames_.empty()) {
            Value result = reg(0);
            if (saved_reg0_valid) {
                reg(0) = saved_reg0;
            }
            return result;
        }
        return last_result_;
    }

    if (entry_depth > 0 && saved_reg0_valid && !frames_.empty()) {
        reg(0) = saved_reg0;
    }

    return {};
}

bool VirtualMachine::run(const BytecodeModule* module, size_t entry_depth)
{
    while (frames_.size() > entry_depth && !failed_) {
        if (step_limit_enabled_) {
            if (remaining_steps_ == 0) {
                fail("Script exceeded execution limit");
                break;
            }
            --remaining_steps_;
        }

        CallFrame& frame = frames_.back();
        if (frame.pc >= frame.function->instructions.size()) {
            pop_frame();
            continue;
        }

        const Instruction& instr = frame.function->instructions[frame.pc++];
        Opcode op = instr.opcode();
        uint8_t a = instr.arg_a();
        uint8_t b = instr.arg_b();
        uint8_t c = instr.arg_c();

        switch (op) {
        case Opcode::MOVE:
            reg(a) = reg(b);
            break;

        case Opcode::LOADK: {
            uint16_t bx = instr.arg_bx();
            const BytecodeModule* current_module = frame.module;
            if (bx >= current_module->constants.size()) {
                fail("Constant index out of range");
                break;
            }
            const auto& k = current_module->constants[bx];
            Value val;
            val.type_id = k.type_id;
            auto& rt = nw::kernel::runtime();
            if (val.type_id == rt.int_type()) {
                val.data.ival = k.data.ival;
            } else if (val.type_id == rt.float_type()) {
                val.data.fval = k.data.fval;
            } else if (val.type_id == rt.string_type()) {
                val.data.hptr = rt.alloc_string(current_module->get_string(k.data.string_idx));
            } else {
                fail("Unsupported constant type");
            }
            reg(a) = val;
            break;
        }

        case Opcode::LOADI:
            reg(a) = Value::make_int(instr.arg_sbx());
            break;

        case Opcode::LOADB:
            reg(a) = Value::make_bool(instr.arg_b() != 0);
            break;

        case Opcode::LOADNIL:
            reg(a) = Value{};
            break;

        case Opcode::JMP: {
            int32_t off = instr.arg_jump();
            if (off < 0 && gas_enabled_ && !consume_gas()) {
                break;
            }
            frame.pc = static_cast<uint32_t>(static_cast<int32_t>(frame.pc) + off);
        } break;

        case Opcode::JMPT: {
            Truthiness t = evaluate_truthiness(reg(a), "JMPT");
            if (t == Truthiness::Error) { break; }
            if (t == Truthiness::True) {
                int16_t off = instr.arg_sbx();
                if (off < 0 && gas_enabled_ && !consume_gas()) break;
                frame.pc = static_cast<uint32_t>(static_cast<int32_t>(frame.pc) + off);
            }
            break;
        }

        case Opcode::JMPF: {
            Truthiness t = evaluate_truthiness(reg(a), "JMPF");
            if (t == Truthiness::Error) { break; }
            if (t == Truthiness::False) {
                int16_t off = instr.arg_sbx();
                if (off < 0 && gas_enabled_ && !consume_gas()) break;
                frame.pc = static_cast<uint32_t>(static_cast<int32_t>(frame.pc) + off);
            }
            break;
        }

        case Opcode::ADD:
        case Opcode::SUB:
        case Opcode::MUL:
        case Opcode::DIV:
        case Opcode::MOD:
            op_arithmetic(op, a, b, c);
            break;

        case Opcode::EQ:
        case Opcode::NE:
        case Opcode::LT:
        case Opcode::LE:
        case Opcode::GT:
        case Opcode::GE:
            op_comparison(op, a, b, c);
            break;

        case Opcode::ISEQ:
        case Opcode::ISNE:
        case Opcode::ISLT:
        case Opcode::ISLE:
        case Opcode::ISGT:
        case Opcode::ISGE:
            op_test_and_skip(op, a, b);
            break;

        case Opcode::AND:
        case Opcode::OR:
            op_logical(op, a, b, c);
            break;

        case Opcode::NEG: {
            auto& rt = nw::kernel::runtime();
            const Value& operand = reg(b);
            if (operand.type_id == rt.int_type()) {
                reg(a) = Value::make_int(-operand.data.ival);
            } else if (operand.type_id == rt.float_type()) {
                reg(a) = Value::make_float(-operand.data.fval);
            } else {
                Value result = rt.execute_unary_op(TokenType::MINUS, operand);
                if (result.type_id == invalid_type_id) {
                    fail("Negation failed");
                } else {
                    reg(a) = result;
                }
            }
            break;
        }

        case Opcode::NOT: {
            Value result = nw::kernel::runtime().execute_unary_op(TokenType::NOT, reg(b));
            if (result.type_id == invalid_type_id) {
                fail("Logical not failed");
            } else {
                reg(a) = result;
            }
            break;
        }

        case Opcode::CALL: {
            uint8_t dest_reg = a;
            uint8_t func_idx = b;
            uint8_t argc = c;

            const BytecodeModule* current_module = frame.module;
            if (func_idx >= current_module->functions.size()) {
                fail("Function index out of range");
                break;
            }
            const CompiledFunction* callee = current_module->functions[func_idx];
            setup_script_call(dest_reg, argc, current_module, callee, nullptr, "CALL");
            break;
        }

        case Opcode::CALLNATIVE: {
            if (gas_enabled_ && !consume_gas()) break;
            uint8_t dest_reg = a;
            uint8_t func_idx = b;
            uint8_t argc = c;

            auto& rt = nw::kernel::runtime();
            const NativeFunction* func = rt.get_native_function(func_idx);
            if (!func) {
                fail("Native function index out of range");
                break;
            }

            const Value* args_ptr = (argc > 0) ? &reg(static_cast<uint8_t>(dest_reg + 1)) : nullptr;
            call_native_wrapper(func->wrapper, rt, args_ptr, argc, dest_reg, func->name);
            break;
        }

        case Opcode::CALLEXT: {
            uint8_t dest_reg = a;
            uint8_t ref_idx = b;
            uint8_t argc = c;

            const BytecodeModule* current_module = frame.module;

            if (ref_idx >= current_module->external_indices.size()) {
                fail("External ref index out of range");
                break;
            }
            uint32_t ext_idx = current_module->external_indices[ref_idx];
            if (ext_idx == UINT32_MAX) {
                fail(fmt::format("Unresolved external function: {}",
                    current_module->external_refs[ref_idx].view()));
                break;
            }

            auto& rt = nw::kernel::runtime();
            const ExternalFunction* ext = rt.get_external_function(ext_idx);
            if (!ext) {
                fail("External function index out of range");
                break;
            }

            if (ext->is_native()) {
                if (gas_enabled_ && !consume_gas()) break;
                const Value* args_ptr = (argc > 0) ? &reg(static_cast<uint8_t>(dest_reg + 1)) : nullptr;
                call_native_wrapper(ext->native_wrapper, rt, args_ptr, argc,
                    dest_reg, ext->qualified_name.view());
            } else {
                if (ext->func_idx >= ext->script_module->functions.size()) {
                    fail("External script function index out of range");
                    break;
                }
                const CompiledFunction* callee = ext->script_module->functions[ext->func_idx];
                setup_script_call(dest_reg, argc, ext->script_module, callee, nullptr, "CALLEXT");
            }
            break;
        }

        case Opcode::CALLINTR: {
            if (gas_enabled_ && !consume_gas()) { break; }
            uint8_t dest_reg = a;
            uint8_t intrinsic_idx = b;
            uint8_t argc = c;
            call_intrinsic(static_cast<IntrinsicId>(intrinsic_idx), dest_reg, argc);
            break;
        }

        case Opcode::CALLINTR_R: {
            if (gas_enabled_ && !consume_gas()) { break; }
            uint8_t dest_reg = a;
            uint8_t id_reg = b;
            uint8_t argc = c;

            Value id_val = reg(id_reg);
            if (id_val.type_id != nw::kernel::runtime().int_type()) {
                fail("Intrinsic id must be int");
                break;
            }
            call_intrinsic(static_cast<IntrinsicId>(id_val.data.ival), dest_reg, argc);
            break;
        }

        case Opcode::NEWARRAY: {
            uint8_t dest_reg = a;
            TypeID tid;
            if (!resolve_type_ref(instr.arg_bx(), frame.module, tid)) break;

            const Type* type = nw::kernel::runtime().get_type(tid);
            if (!type || type->type_kind != TK_array) {
                fail("NEWARRAY called with non-array type");
                break;
            }

            Value size_val = reg(dest_reg);
            if (size_val.type_id != nw::kernel::runtime().int_type()) {
                fail("Array size must be an integer");
                break;
            }

            if (size_val.data.ival < 0) {
                fail("Array size must be non-negative");
                break;
            }

            uint32_t count = static_cast<uint32_t>(size_val.data.ival);
            TypeID elem_type = type->type_params[0].as<TypeID>();

            if (type->type_params[1].empty()) {
                HeapPtr ptr = nw::kernel::runtime().create_array_typed(elem_type, count);
                if (ptr.value == 0) {
                    fail("Failed to allocate array");
                    break;
                }

                auto* arr = nw::kernel::runtime().get_array_typed(ptr);
                if (!arr) {
                    fail("Failed to initialize array");
                    break;
                }
                arr->resize(count);
                reg(dest_reg) = Value::make_heap(ptr, tid);
            } else {
                HeapPtr ptr = nw::kernel::runtime().alloc_array(elem_type, count);
                if (ptr.value == 0) {
                    fail("Failed to allocate array");
                    break;
                }
                reg(dest_reg) = Value::make_heap(ptr, tid);
            }
            break;
        }

        case Opcode::NEWTUPLE: {
            uint8_t dest_reg = a;
            uint8_t count = b;

            Vector<TypeID> element_types;
            element_types.reserve(count);
            for (uint8_t i = 0; i < count; ++i) {
                element_types.push_back(reg(static_cast<uint8_t>(dest_reg + 1 + i)).type_id);
            }

            TypeID tuple_tid = nw::kernel::runtime().register_tuple_type(element_types);
            if (tuple_tid == invalid_type_id) {
                fail("Failed to register tuple type");
                break;
            }

            HeapPtr ptr = nw::kernel::runtime().alloc_tuple(tuple_tid);
            if (ptr.value == 0) {
                fail("Failed to allocate tuple");
                break;
            }

            const Type* type = nw::kernel::runtime().get_type(tuple_tid);
            auto tuple_id = type->type_params[0].as<TupleID>();
            const TupleDef* tuple_def = nw::kernel::runtime().type_table_.get(tuple_id);

            for (uint8_t i = 0; i < count; ++i) {
                Value val = reg(static_cast<uint8_t>(dest_reg + 1 + i));
                if (!nw::kernel::runtime().write_tuple_element_by_index(ptr, tuple_def, i, val)) {
                    fail("Failed to write tuple element");
                    break;
                }
            }

            reg(dest_reg) = Value::make_heap(ptr, tuple_tid);
            break;
        }

        case Opcode::GETTUPLE: {
            uint8_t dest_reg = a;
            uint8_t src_reg = b;
            uint8_t index = c;

            Value tuple_val = reg(src_reg);
            HeapPtr tuple_ptr = tuple_val.data.hptr;

            const Type* type = nw::kernel::runtime().get_type(tuple_val.type_id);
            if (!type || type->type_kind != TK_tuple) {
                fail("GETTUPLE called on non-tuple");
                break;
            }

            auto tuple_id = type->type_params[0].as<TupleID>();
            const TupleDef* tuple_def = nw::kernel::runtime().type_table_.get(tuple_id);

            Value elem_val = nw::kernel::runtime().read_tuple_element_by_index(tuple_ptr, tuple_def, index);
            if (elem_val.type_id == invalid_type_id) {
                fail("Failed to read tuple element");
                break;
            }

            reg(dest_reg) = elem_val;
            break;
        }

        case Opcode::GETARRAY: {
            uint8_t dest_reg = a;
            uint8_t arr_reg = b;
            uint8_t idx_reg = c;

            Value arr_val = reg(arr_reg);
            Value idx_val = reg(idx_reg);

            if (idx_val.type_id != nw::kernel::runtime().int_type()) {
                fail("Array index must be integer");
                break;
            }

            Value result;
            if (!nw::kernel::runtime().array_get(arr_val.data.hptr, static_cast<uint32_t>(idx_val.data.ival), result)) {
                fail("Array access failed (index out of bounds?)");
                break;
            }
            reg(dest_reg) = result;
            break;
        }

        case Opcode::SETARRAY: {
            // rA[rB] = rC
            uint8_t arr_reg = a;
            uint8_t idx_reg = b;
            uint8_t val_reg = c;

            Value arr_val = reg(arr_reg);
            Value idx_val = reg(idx_reg);
            Value val = reg(val_reg);

            if (idx_val.type_id != nw::kernel::runtime().int_type()) {
                fail("Array index must be integer");
                break;
            }

            if (!nw::kernel::runtime().array_set(arr_val.data.hptr, static_cast<uint32_t>(idx_val.data.ival), val)) {
                fail("Array set failed");
                break;
            }
            break;
        }

        case Opcode::NEWMAP: {
            uint8_t dest_reg = a;
            TypeID tid;
            if (!resolve_type_ref(instr.arg_bx(), frame.module, tid)) break;

            const Type* type = nw::kernel::runtime().get_type(tid);
            if (!type || type->type_kind != TK_map) {
                fail("NEWMAP called with non-map type");
                break;
            }

            TypeID key_type = type->type_params[0].as<TypeID>();
            TypeID val_type = type->type_params[1].as<TypeID>();

            HeapPtr ptr = nw::kernel::runtime().alloc_map(key_type, val_type);
            if (ptr.value == 0) {
                fail("Failed to allocate map");
                break;
            }

            reg(dest_reg) = Value::make_heap(ptr, tid);
            break;
        }

        case Opcode::MAPGET: {
            uint8_t dest_reg = a;
            uint8_t map_reg = b;
            uint8_t key_reg = c;

            Value map_val = reg(map_reg);
            Value key_val = reg(key_reg);

            Value result;
            if (nw::kernel::runtime().map_get(map_val.data.hptr, key_val, result)) {
                reg(dest_reg) = result;
            } else {
                reg(dest_reg) = Value{};
            }
            break;
        }

        case Opcode::MAPSET: {
            uint8_t map_reg = a;
            uint8_t key_reg = b;
            uint8_t val_reg = c;

            Value map_val = reg(map_reg);
            Value key_val = reg(key_reg);
            Value val = reg(val_reg);

            (void)nw::kernel::runtime().map_set(map_val.data.hptr, key_val, val);
            break;
        }

        case Opcode::NEWSTRUCT: {
            uint8_t dest_reg = a;
            TypeID tid;
            if (!resolve_type_ref(instr.arg_bx(), frame.module, tid)) { break; }

            HeapPtr ptr = nw::kernel::runtime().alloc_struct(tid);
            if (ptr.value == 0) {
                fail("Failed to allocate struct");
                break;
            }

            reg(dest_reg) = Value::make_heap(ptr, tid);
            break;
        }

        case Opcode::NEWSUM: {
            uint8_t dest_reg = a;
            TypeID tid;
            if (!resolve_type_ref(instr.arg_bx(), frame.module, tid)) { break; }

            HeapPtr ptr = nw::kernel::runtime().alloc_sum(tid);
            if (ptr.value == 0) {
                fail("Failed to allocate sum type");
                break;
            }

            reg(dest_reg) = Value::make_heap(ptr, tid);
            break;
        }

        case Opcode::SUMINIT: {
            uint8_t sum_reg = a;
            uint8_t tag_value = b;
            uint8_t payload_reg = c;

            Value sum_val = reg(sum_reg);
            const SumDef* sum_def = nullptr;
            void* data = resolve_sum_data(sum_val, frame, sum_def, "SUMINIT");
            if (!data) { break; }
            if (tag_value >= sum_def->variant_count) {
                fail("Invalid sum variant index");
                break;
            }

            auto& rt = nw::kernel::runtime();
            rt.write_sum_tag(data, sum_def, tag_value);

            if (payload_reg != 255) {
                Value payload = reg(payload_reg);
                rt.write_sum_payload(data, sum_def, tag_value, payload);
            }
            break;
        }

        case Opcode::SUMGETTAG: {
            uint8_t dest_reg = a;
            uint8_t src_reg = b;

            Value sum_val = reg(src_reg);
            const SumDef* sum_def = nullptr;
            void* data = resolve_sum_data(sum_val, frame, sum_def, "SUMGETTAG");
            if (!data) { break; }

            uint32_t tag = nw::kernel::runtime().read_sum_tag(data, sum_def);
            if (tag >= sum_def->variant_count) {
                fail(fmt::format("SUMGETTAG: tag {} out of range (variant_count={})", tag, sum_def->variant_count));
                break;
            }
            reg(dest_reg) = Value::make_int(static_cast<int32_t>(tag));
            break;
        }

        case Opcode::SUMGETPAYLOAD: {
            uint8_t dest_reg = a;
            uint8_t src_reg = b;
            uint8_t variant_idx = c;

            Value sum_val = reg(src_reg);
            const SumDef* sum_def = nullptr;
            void* data = resolve_sum_data(sum_val, frame, sum_def, "SUMGETPAYLOAD");
            if (!data) { break; }

            if (variant_idx >= sum_def->variant_count) {
                fail("Invalid sum variant index");
                break;
            }

            reg(dest_reg) = nw::kernel::runtime().read_sum_payload(data, sum_def, variant_idx);
            break;
        }

        case Opcode::CLOSURE: {
            uint8_t dest_reg = a;
            uint16_t func_idx = instr.arg_bx();

            const BytecodeModule* current_module = frame.module;
            if (!current_module || func_idx >= current_module->functions.size()) {
                fail("Closure function index out of range");
                break;
            }

            const CompiledFunction* callee = current_module->functions[func_idx];
            auto& rt = nw::kernel::runtime();

            if (callee->function_type == invalid_type_id) {
                fail("Closure missing function type");
                break;
            }

            HeapPtr closure_ptr = rt.alloc_closure(callee->function_type, callee, current_module, callee->upvalue_count);
            Closure* closure = rt.get_closure(closure_ptr);
            if (!closure) {
                fail("Failed to allocate closure");
                break;
            }

            uint8_t upvalue_count = callee->upvalue_count;
            size_t words = (upvalue_count + 3) / 4;
            uint32_t upval_index = 0;
            bool ok = true;

            for (size_t w = 0; w < words; ++w) {
                if (frame.pc >= frame.function->instructions.size()) {
                    fail("Closure upvalue descriptor out of range");
                    ok = false;
                    break;
                }
                uint32_t raw = frame.function->instructions[frame.pc++].raw;
                for (size_t i = 0; i < 4 && upval_index < upvalue_count; ++i, ++upval_index) {
                    uint8_t desc = static_cast<uint8_t>((raw >> (8 * i)) & 0xFF);
                    bool is_local = (desc & 0x1) != 0;
                    uint8_t index = static_cast<uint8_t>(desc >> 1);

                    if (is_local) {
                        closure->upvalues.push_back(get_or_create_upvalue(frame, index));
                    } else {
                        if (!frame.closure || index >= frame.closure->upvalues.size()) {
                            fail("Closure upvalue index out of range");
                            ok = false;
                            break;
                        }
                        closure->upvalues.push_back(frame.closure->upvalues[index]);
                    }
                }
                if (!ok) { break; }
            }
            if (!ok) { break; }

            reg(dest_reg) = Value::make_heap(closure_ptr, callee->function_type);
            break;
        }

        case Opcode::GETUPVAL: {
            if (!frame.closure) {
                fail("GETUPVAL used without active closure");
                break;
            }
            if (b >= frame.closure->upvalues.size()) {
                fail("Upvalue index out of range");
                break;
            }
            Upvalue* uv = frame.closure->upvalues[b];
            if (!uv) {
                fail("Upvalue is null");
                break;
            }
            reg(a) = *uv->location;
            break;
        }

        case Opcode::SETUPVAL: {
            if (!frame.closure) {
                fail("SETUPVAL used without active closure");
                break;
            }
            if (b >= frame.closure->upvalues.size()) {
                fail("Upvalue index out of range");
                break;
            }
            Upvalue* uv = frame.closure->upvalues[b];
            if (!uv) {
                fail("Upvalue is null");
                break;
            }
            Value val = reg(a);
            *uv->location = val;

            auto& rt = nw::kernel::runtime();
            if (rt.gc() && !uv->is_open() && val.storage == ValueStorage::heap && val.data.hptr.value != 0) {
                rt.gc()->write_barrier(uv->heap_ptr, val.data.hptr);
            }
            break;
        }

        case Opcode::GETGLOBAL: {
            uint8_t dest = a;
            uint16_t slot = instr.arg_bx();
            if (slot >= frame.module->global_count) {
                fail("GETGLOBAL slot out of range");
                break;
            }
            reg(dest) = const_cast<BytecodeModule*>(frame.module)->globals[slot];
            break;
        }

        case Opcode::SETGLOBAL: {
            uint8_t src = a;
            uint16_t slot = instr.arg_bx();
            if (slot >= frame.module->global_count) {
                fail("SETGLOBAL slot out of range");
                break;
            }
            const_cast<BytecodeModule*>(frame.module)->globals[slot] = reg(src);
            break;
        }

        case Opcode::CLOSEUPVALS:
            close_upvalues(frame);
            break;

        case Opcode::CALLCLOSURE: {
            uint8_t dest_reg = a;
            uint8_t closure_reg = b;
            uint8_t argc = c;

            Value callee_val = reg(closure_reg);
            if (callee_val.storage != ValueStorage::heap || callee_val.data.hptr.value == 0) {
                fail("CALLCLOSURE expects a heap value");
                break;
            }

            auto& rt = nw::kernel::runtime();
            const Type* callee_type = rt.get_type(callee_val.type_id);
            if (!callee_type || callee_type->type_kind != TK_function) {
                fail("CALLCLOSURE expects a function value");
                break;
            }
            Closure* closure = rt.get_closure(callee_val.data.hptr);
            if (!closure || !closure->function || !closure->module) {
                fail("CALLCLOSURE has invalid closure");
                break;
            }

            setup_script_call(dest_reg, argc, closure->module, closure->function, closure, "CALLCLOSURE");
            break;
        }

        case Opcode::GETFIELD: {
            // rA = rB.field[C]
            uint8_t dest_reg = a;
            uint8_t src_reg = b;
            uint8_t field_idx = c;

            Value struct_val = reg(src_reg);
            const Type* type = nw::kernel::runtime().get_type(struct_val.type_id);
            if (!type || type->type_kind != TK_struct) {
                fail("GETFIELD called on non-struct");
                break;
            }

            auto struct_id = type->type_params[0].as<StructID>();
            const StructDef* struct_def = nw::kernel::runtime().type_table_.get(struct_id);

            Value val = nw::kernel::runtime().read_struct_field_by_index(struct_val.data.hptr, struct_def, field_idx);
            if (val.type_id == invalid_type_id) {
                fail("Failed to read struct field");
                break;
            }

            reg(dest_reg) = val;
            break;
        }

        case Opcode::SETFIELD: {
            // rA.field[B] = rC
            uint8_t struct_reg = a;
            uint8_t field_idx = b;
            uint8_t val_reg = c;

            Value struct_val = reg(struct_reg);
            const Type* type = nw::kernel::runtime().get_type(struct_val.type_id);
            if (!type || type->type_kind != TK_struct) {
                fail("SETFIELD called on non-struct");
                break;
            }

            auto struct_id = type->type_params[0].as<StructID>();
            const StructDef* struct_def = nw::kernel::runtime().type_table_.get(struct_id);

            Value val = reg(val_reg);
            if (!nw::kernel::runtime().write_struct_field_by_index(struct_val.data.hptr, struct_def, field_idx, val)) {
                fail("Failed to write struct field");
                break;
            }
            break;
        }

        case Opcode::FIELDGETI:
        case Opcode::FIELDGETI_R:
        case Opcode::FIELDGETF:
        case Opcode::FIELDGETF_R:
        case Opcode::FIELDGETB:
        case Opcode::FIELDGETB_R:
        case Opcode::FIELDGETS:
        case Opcode::FIELDGETS_R:
        case Opcode::FIELDGETO:
        case Opcode::FIELDGETO_R:
        case Opcode::FIELDGETH:
        case Opcode::FIELDGETH_R:
        case Opcode::FIELDSETI:
        case Opcode::FIELDSETI_R:
        case Opcode::FIELDSETF:
        case Opcode::FIELDSETF_R:
        case Opcode::FIELDSETB:
        case Opcode::FIELDSETB_R:
        case Opcode::FIELDSETS:
        case Opcode::FIELDSETS_R:
        case Opcode::FIELDSETO:
        case Opcode::FIELDSETO_R:
        case Opcode::FIELDSETH:
        case Opcode::FIELDSETH_R:
            op_field_access(op, a, b, c, frame.module);
            break;

        case Opcode::RET: {
            Value val = reg(a);
            uint32_t ret_reg = frame.return_register;

            // Stack values are frame-relative; copy them into the caller's frame before popping.
            if (val.storage == ValueStorage::stack) {
                auto& rt = nw::kernel::runtime();
                const Type* type = rt.get_type(val.type_id);
                if (!type) {
                    fail("RET has invalid value type");
                    break;
                }

                if (frames_.size() >= 2) {
                    CallFrame& caller_frame = frames_[frames_.size() - 2];
                    uint32_t dst_offset = caller_frame.stack_alloc(type->size, type->alignment, val.type_id);
                    std::memcpy(
                        caller_frame.stack_.data() + dst_offset,
                        frame.stack_.data() + val.data.stack_offset,
                        type->size);
                    val = Value::make_stack(dst_offset, val.type_id);
                } else {
                    // Returning a stack value out of the VM: materialize it on the heap.
                    HeapPtr ptr = rt.heap_.allocate(type->size, type->alignment, val.type_id);
                    void* data = rt.heap_.get_ptr(ptr);
                    std::memcpy(
                        data,
                        frame.stack_.data() + val.data.stack_offset,
                        type->size);
                    val = Value::make_heap(ptr, val.type_id);
                }
            }

            pop_frame();
            if (!frames_.empty()) {
                reg(static_cast<uint8_t>(ret_reg)) = val;
            } else {
                // Entry function return
                last_result_ = val;
            }
            break;
        }

        case Opcode::RETVOID:
            pop_frame();
            if (frames_.empty()) {
                last_result_ = Value{nw::kernel::runtime().void_type()};
            }
            break;

        case Opcode::CAST: {
            uint8_t reg_idx = a;
            TypeID target_tid;
            if (!resolve_type_ref(instr.arg_bx(), frame.module, target_tid)) { break; }
            Value& val = reg(reg_idx);

            auto& rt = nw::kernel::runtime();
            if (val.type_id == target_tid) {
                // No-op
            } else if (target_tid == rt.float_type() && val.type_id == rt.int_type()) {
                val = Value::make_float(static_cast<float>(val.data.ival));
            } else if (target_tid == rt.int_type() && val.type_id == rt.float_type()) {
                val = Value::make_int(static_cast<int32_t>(val.data.fval));
            } else if (rt.is_object_like_type(target_tid) && rt.is_object_like_type(val.type_id)) {
                bool valid_object_cast = false;
                if (target_tid == rt.object_type()) {
                    valid_object_cast = true;
                } else if (auto expected_tag = rt.object_subtype_tag(target_tid)) {
                    valid_object_cast = val.data.oval.type == *expected_tag;
                }

                if (valid_object_cast) {
                    val.type_id = target_tid;
                } else {
                    fail(fmt::format("Invalid cast from {} to {}", rt.type_name(val.type_id), rt.type_name(target_tid)));
                }
            } else {
                const Type* source_type = rt.get_type(val.type_id);
                const Type* target_type = rt.get_type(target_tid);
                bool newtype_cast = false;
                if (val.type_id == invalid_type_id && target_type && target_type->type_kind == TK_function) {
                    val = Value::make_heap(HeapPtr{0}, target_tid);
                    newtype_cast = true;
                }
                if (source_type && target_type) {
                    if (target_type->type_kind == TK_newtype) {
                        TypeID wrapped = target_type->type_params[0].as<TypeID>();
                        if (val.type_id == wrapped) {
                            val.type_id = target_tid;
                            newtype_cast = true;
                        }
                    } else if (source_type->type_kind == TK_newtype) {
                        TypeID wrapped = source_type->type_params[0].as<TypeID>();
                        if (target_tid == wrapped) {
                            val.type_id = target_tid;
                            newtype_cast = true;
                        }
                    }
                }

                if (!newtype_cast) {
                    fail(fmt::format("Invalid cast from {} to {}", rt.type_name(val.type_id), rt.type_name(target_tid)));
                }
            }
            break;
        }

        case Opcode::IS: {
            uint8_t reg_idx = a;
            TypeID target_tid;
            if (!resolve_type_ref(instr.arg_bx(), frame.module, target_tid)) { break; }
            Value val = reg(reg_idx);

            bool result = false;
            auto& rt = nw::kernel::runtime();
            if (rt.is_object_like_type(target_tid) && rt.is_object_like_type(val.type_id)) {
                if (target_tid == rt.object_type()) {
                    result = true;
                } else if (auto expected_tag = rt.object_subtype_tag(target_tid)) {
                    result = val.data.oval.type == *expected_tag;
                }
            } else {
                result = (val.type_id == target_tid);
            }
            reg(reg_idx) = Value::make_bool(result);
            break;
        }

            // == Stack Value Type Operations =====================================

        case Opcode::STACK_ALLOC: {
            uint8_t dest_reg = a;
            TypeID tid;
            if (!resolve_type_ref(instr.arg_bx(), frame.module, tid)) { break; }
            const Type* type = nw::kernel::runtime().get_type(tid);
            if (!type) {
                fail("Invalid type in STACK_ALLOC");
                break;
            }

            uint32_t offset = frame.stack_alloc(type->size, type->alignment, tid);
            nw::kernel::runtime().initialize_zero_defaults(tid, frame.stack_.data() + offset);
            reg(dest_reg) = Value::make_stack(offset, tid);
            break;
        }

        case Opcode::STACK_COPY: {
            uint8_t dst_reg = a;
            uint8_t src_reg = b;

            Value dst = reg(dst_reg);
            Value src = reg(src_reg);

            if (dst.storage != ValueStorage::stack || src.storage != ValueStorage::stack) {
                fail("STACK_COPY requires stack values");
                break;
            }

            const Type* type = nw::kernel::runtime().get_type(src.type_id);
            if (!type) {
                fail("Invalid type in STACK_COPY");
                break;
            }

            std::memcpy(
                frame.stack_.data() + dst.data.stack_offset,
                frame.stack_.data() + src.data.stack_offset,
                type->size);
            break;
        }

        case Opcode::STACK_FIELDGET:
        case Opcode::STACK_FIELDGET_R: {
            uint8_t dest_reg = a;
            uint8_t base_reg = b;
            uint32_t field_idx = (op == Opcode::STACK_FIELDGET)
                ? static_cast<uint32_t>(c)
                : static_cast<uint32_t>(reg(c).data.ival);

            Value base = reg(base_reg);
            if (base.storage != ValueStorage::stack) {
                fail("STACK_FIELDGET requires stack value");
                break;
            }

            if (field_idx >= frame.module->field_offsets.size()) {
                fail("Field reference index out of range in STACK_FIELDGET");
                break;
            }

            uint32_t field_offset = frame.module->field_offsets[field_idx];
            TypeID field_type = frame.module->field_types[field_idx];
            auto& rt = nw::kernel::runtime();
            uint8_t* ptr = frame.stack_.data() + base.data.stack_offset + field_offset;
            reg(dest_reg) = read_stack_value(ptr, field_type, base.data.stack_offset, field_offset, rt);
            break;
        }

        case Opcode::STACK_FIELDSET:
        case Opcode::STACK_FIELDSET_R: {
            uint8_t base_reg = a;
            uint8_t val_reg = c;
            uint32_t field_idx = (op == Opcode::STACK_FIELDSET)
                ? static_cast<uint32_t>(b)
                : static_cast<uint32_t>(reg(b).data.ival);

            Value base = reg(base_reg);
            if (base.storage != ValueStorage::stack) {
                fail("STACK_FIELDSET requires stack value");
                break;
            }

            if (field_idx >= frame.module->field_offsets.size()) {
                fail("Field reference index out of range in STACK_FIELDSET");
                break;
            }

            uint32_t field_offset = frame.module->field_offsets[field_idx];
            TypeID field_type = frame.module->field_types[field_idx];
            Value val = reg(val_reg);
            auto& rt = nw::kernel::runtime();
            uint8_t* ptr = frame.stack_.data() + base.data.stack_offset + field_offset;
            write_stack_value(ptr, field_type, val, frame.stack_.data(), rt);
            break;
        }

        case Opcode::STACK_INDEXGET: {
            uint8_t dest_reg = a;
            uint8_t base_reg = b;
            uint8_t idx_reg = c;

            Value base = reg(base_reg);
            if (base.storage != ValueStorage::stack) {
                fail("STACK_INDEXGET requires stack value");
                break;
            }

            Value idx_val = reg(idx_reg);
            auto& rt = nw::kernel::runtime();
            if (idx_val.type_id != rt.int_type()) {
                fail("Fixed array index must be integer");
                break;
            }

            const Type* type = rt.get_type(base.type_id);
            if (!type || type->type_kind != TK_fixed_array) {
                fail("STACK_INDEXGET requires fixed array value");
                break;
            }

            int32_t size = type->type_params[1].as<int32_t>();
            int32_t index = idx_val.data.ival;
            if (index < 0 || index >= size) {
                fail("Fixed array index out of bounds");
                break;
            }

            TypeID elem_type_id = type->type_params[0].as<TypeID>();
            const Type* elem_type = rt.get_type(elem_type_id);
            if (!elem_type) {
                fail("Fixed array element type not found");
                break;
            }

            uint32_t offset = static_cast<uint32_t>(index) * elem_type->size;
            uint8_t* ptr = frame.stack_.data() + base.data.stack_offset + offset;
            reg(dest_reg) = read_stack_value(ptr, elem_type_id, base.data.stack_offset, offset, rt);
            break;
        }

        case Opcode::STACK_INDEXSET: {
            uint8_t base_reg = a;
            uint8_t idx_reg = b;
            uint8_t val_reg = c;

            Value base = reg(base_reg);
            if (base.storage != ValueStorage::stack) {
                fail("STACK_INDEXSET requires stack value");
                break;
            }

            Value idx_val = reg(idx_reg);
            auto& rt = nw::kernel::runtime();
            if (idx_val.type_id != rt.int_type()) {
                fail("Fixed array index must be integer");
                break;
            }

            const Type* type = rt.get_type(base.type_id);
            if (!type || type->type_kind != TK_fixed_array) {
                fail("STACK_INDEXSET requires fixed array value");
                break;
            }

            int32_t size = type->type_params[1].as<int32_t>();
            int32_t index = idx_val.data.ival;
            if (index < 0 || index >= size) {
                fail("Fixed array index out of bounds");
                break;
            }

            TypeID elem_type_id = type->type_params[0].as<TypeID>();
            const Type* elem_type = rt.get_type(elem_type_id);
            if (!elem_type) {
                fail("Fixed array element type not found");
                break;
            }

            uint32_t offset = static_cast<uint32_t>(index) * elem_type->size;
            uint8_t* ptr = frame.stack_.data() + base.data.stack_offset + offset;
            Value val = reg(val_reg);
            write_stack_value(ptr, elem_type_id, val, frame.stack_.data(), rt);
            break;
        }

        default:
            fail(fmt::format("Unimplemented opcode: {}", static_cast<int>(op)));
            break;
        }
    }

    return !failed_;
}

void VirtualMachine::op_arithmetic(Opcode op, uint8_t a, uint8_t b, uint8_t c)
{
    auto& rt = nw::kernel::runtime();
    const Value& lv = reg(b);
    const Value& rv = reg(c);

    // Division/modulo by zero check
    if (op == Opcode::DIV || op == Opcode::MOD) {
        if (rv.type_id == rt.int_type() && rv.data.ival == 0) {
            fail(op == Opcode::DIV ? "Division by zero" : "Modulo by zero");
            return;
        }
        if (rv.type_id == rt.float_type() && rv.data.fval == 0.0f) {
            fail("Division by zero");
            return;
        }
    }

    // Fast path: int op int
    if (lv.type_id == rt.int_type() && rv.type_id == rt.int_type()) {
        int32_t result_val;
        switch (op) {
        case Opcode::ADD:
            result_val = lv.data.ival + rv.data.ival;
            break;
        case Opcode::SUB:
            result_val = lv.data.ival - rv.data.ival;
            break;
        case Opcode::MUL:
            result_val = lv.data.ival * rv.data.ival;
            break;
        case Opcode::DIV:
            result_val = lv.data.ival / rv.data.ival;
            break;
        case Opcode::MOD:
            result_val = lv.data.ival % rv.data.ival;
            break;
        default:
            goto slow_path;
        }
        reg(a) = Value::make_int(result_val);
        return;
    }

    // Fast path: float op float
    if (lv.type_id == rt.float_type() && rv.type_id == rt.float_type()) {
        float result_val;
        switch (op) {
        case Opcode::ADD:
            result_val = lv.data.fval + rv.data.fval;
            break;
        case Opcode::SUB:
            result_val = lv.data.fval - rv.data.fval;
            break;
        case Opcode::MUL:
            result_val = lv.data.fval * rv.data.fval;
            break;
        case Opcode::DIV:
            result_val = lv.data.fval / rv.data.fval;
            break;
        default:
            goto slow_path;
        }
        reg(a) = Value::make_float(result_val);
        return;
    }

slow_path:
    TokenType tt;
    switch (op) {
    case Opcode::ADD:
        tt = TokenType::PLUS;
        break;
    case Opcode::SUB:
        tt = TokenType::MINUS;
        break;
    case Opcode::MUL:
        tt = TokenType::TIMES;
        break;
    case Opcode::DIV:
        tt = TokenType::DIV;
        break;
    case Opcode::MOD:
        tt = TokenType::MOD;
        break;
    default:
        return;
    }

    Value result = rt.execute_binary_op(tt, lv, rv);
    if (result.type_id == invalid_type_id) {
        fail(fmt::format("Arithmetic operation failed for opcode {}", static_cast<int>(op)));
    } else {
        reg(a) = result;
    }
}

void VirtualMachine::op_comparison(Opcode op, uint8_t a, uint8_t b, uint8_t c)
{
    auto& rt = nw::kernel::runtime();
    const Value& lv = reg(b);
    const Value& rv = reg(c);

    // Fast path: int cmp int
    if (lv.type_id == rt.int_type() && rv.type_id == rt.int_type()) {
        bool result_val;
        switch (op) {
        case Opcode::EQ:
            result_val = lv.data.ival == rv.data.ival;
            break;
        case Opcode::NE:
            result_val = lv.data.ival != rv.data.ival;
            break;
        case Opcode::LT:
            result_val = lv.data.ival < rv.data.ival;
            break;
        case Opcode::LE:
            result_val = lv.data.ival <= rv.data.ival;
            break;
        case Opcode::GT:
            result_val = lv.data.ival > rv.data.ival;
            break;
        case Opcode::GE:
            result_val = lv.data.ival >= rv.data.ival;
            break;
        default:
            goto slow_path;
        }
        reg(a) = Value::make_bool(result_val);
        return;
    }

    // Fast path: float cmp float
    if (lv.type_id == rt.float_type() && rv.type_id == rt.float_type()) {
        bool result_val;
        switch (op) {
        case Opcode::EQ:
            result_val = lv.data.fval == rv.data.fval;
            break;
        case Opcode::NE:
            result_val = lv.data.fval != rv.data.fval;
            break;
        case Opcode::LT:
            result_val = lv.data.fval < rv.data.fval;
            break;
        case Opcode::LE:
            result_val = lv.data.fval <= rv.data.fval;
            break;
        case Opcode::GT:
            result_val = lv.data.fval > rv.data.fval;
            break;
        case Opcode::GE:
            result_val = lv.data.fval >= rv.data.fval;
            break;
        default:
            goto slow_path;
        }
        reg(a) = Value::make_bool(result_val);
        return;
    }

    // Fast path: bool cmp bool
    if (lv.type_id == rt.bool_type() && rv.type_id == rt.bool_type()) {
        bool result_val;
        switch (op) {
        case Opcode::EQ:
            result_val = lv.data.bval == rv.data.bval;
            break;
        case Opcode::NE:
            result_val = lv.data.bval != rv.data.bval;
            break;
        case Opcode::LT:
            result_val = lv.data.bval < rv.data.bval;
            break;
        case Opcode::LE:
            result_val = lv.data.bval <= rv.data.bval;
            break;
        case Opcode::GT:
            result_val = lv.data.bval > rv.data.bval;
            break;
        case Opcode::GE:
            result_val = lv.data.bval >= rv.data.bval;
            break;
        default:
            goto slow_path;
        }
        reg(a) = Value::make_bool(result_val);
        return;
    }

    // Fast path: string cmp string
    if (lv.type_id == rt.string_type() && rv.type_id == rt.string_type()) {
        StringView ls = (lv.data.hptr.value != 0) ? rt.get_string_view(lv.data.hptr) : StringView{};
        StringView rs = (rv.data.hptr.value != 0) ? rt.get_string_view(rv.data.hptr) : StringView{};
        bool result_val;
        switch (op) {
        case Opcode::EQ:
            result_val = ls == rs;
            break;
        case Opcode::NE:
            result_val = ls != rs;
            break;
        case Opcode::LT:
            result_val = ls < rs;
            break;
        case Opcode::LE:
            result_val = ls <= rs;
            break;
        case Opcode::GT:
            result_val = ls > rs;
            break;
        case Opcode::GE:
            result_val = ls >= rs;
            break;
        default:
            goto slow_path;
        }
        reg(a) = Value::make_bool(result_val);
        return;
    }

slow_path:
    TokenType tt;
    switch (op) {
    case Opcode::EQ:
        tt = TokenType::EQEQ;
        break;
    case Opcode::NE:
        tt = TokenType::NOTEQ;
        break;
    case Opcode::LT:
        tt = TokenType::LT;
        break;
    case Opcode::LE:
        tt = TokenType::LTEQ;
        break;
    case Opcode::GT:
        tt = TokenType::GT;
        break;
    case Opcode::GE:
        tt = TokenType::GTEQ;
        break;
    default:
        tt = TokenType::EQEQ;
        break;
    }

    Value result = rt.execute_binary_op(tt, lv, rv);
    if (result.type_id == invalid_type_id) {
        fail("Comparison failed");
    } else {
        reg(a) = result;
    }
}

void VirtualMachine::op_logical(Opcode op, uint8_t a, uint8_t b, uint8_t c)
{
    auto& rt = nw::kernel::runtime();
    const Value& lv = reg(b);
    const Value& rv = reg(c);

    if (lv.type_id == rt.bool_type() && rv.type_id == rt.bool_type()) {
        bool result_val = (op == Opcode::AND)
            ? (lv.data.bval && rv.data.bval)
            : (lv.data.bval || rv.data.bval);
        reg(a) = Value::make_bool(result_val);
        return;
    }

    if (lv.type_id == rt.int_type() && rv.type_id == rt.int_type()) {
        int32_t result_val = (op == Opcode::AND)
            ? (lv.data.ival && rv.data.ival)
            : (lv.data.ival || rv.data.ival);
        reg(a) = Value::make_int(result_val);
        return;
    }

    TokenType tt = (op == Opcode::AND) ? TokenType::ANDAND : TokenType::OROR;
    Value result = rt.execute_binary_op(tt, lv, rv);
    if (result.type_id == invalid_type_id) {
        fail("Logical operation failed");
    } else {
        reg(a) = result;
    }
}

void VirtualMachine::op_test_and_skip(Opcode op, uint8_t a, uint8_t b)
{
    auto& rt = nw::kernel::runtime();
    const Value& lv = reg(a);
    const Value& rv = reg(b);
    bool skip = false;

    // Fast path: int cmp int
    if (lv.type_id == rt.int_type() && rv.type_id == rt.int_type()) {
        switch (op) {
        case Opcode::ISEQ:
            skip = lv.data.ival == rv.data.ival;
            break;
        case Opcode::ISNE:
            skip = lv.data.ival != rv.data.ival;
            break;
        case Opcode::ISLT:
            skip = lv.data.ival < rv.data.ival;
            break;
        case Opcode::ISLE:
            skip = lv.data.ival <= rv.data.ival;
            break;
        case Opcode::ISGT:
            skip = lv.data.ival > rv.data.ival;
            break;
        case Opcode::ISGE:
            skip = lv.data.ival >= rv.data.ival;
            break;
        default:
            break;
        }
        if (skip) { frames_.back().pc++; }
        return;
    }

    // Fast path: float cmp float
    if (lv.type_id == rt.float_type() && rv.type_id == rt.float_type()) {
        switch (op) {
        case Opcode::ISEQ:
            skip = lv.data.fval == rv.data.fval;
            break;
        case Opcode::ISNE:
            skip = lv.data.fval != rv.data.fval;
            break;
        case Opcode::ISLT:
            skip = lv.data.fval < rv.data.fval;
            break;
        case Opcode::ISLE:
            skip = lv.data.fval <= rv.data.fval;
            break;
        case Opcode::ISGT:
            skip = lv.data.fval > rv.data.fval;
            break;
        case Opcode::ISGE:
            skip = lv.data.fval >= rv.data.fval;
            break;
        default:
            break;
        }
        if (skip) { frames_.back().pc++; }
        return;
    }

    // Fast path: string cmp string
    if (lv.type_id == rt.string_type() && rv.type_id == rt.string_type()) {
        StringView ls = (lv.data.hptr.value != 0) ? rt.get_string_view(lv.data.hptr) : StringView{};
        StringView rs = (rv.data.hptr.value != 0) ? rt.get_string_view(rv.data.hptr) : StringView{};
        switch (op) {
        case Opcode::ISEQ:
            skip = ls == rs;
            break;
        case Opcode::ISNE:
            skip = ls != rs;
            break;
        case Opcode::ISLT:
            skip = ls < rs;
            break;
        case Opcode::ISLE:
            skip = ls <= rs;
            break;
        case Opcode::ISGT:
            skip = ls > rs;
            break;
        case Opcode::ISGE:
            skip = ls >= rs;
            break;
        default:
            break;
        }
        if (skip) { frames_.back().pc++; }
        return;
    }

    // Slow path
    TokenType tt;
    switch (op) {
    case Opcode::ISEQ:
        tt = TokenType::EQEQ;
        break;
    case Opcode::ISNE:
        tt = TokenType::NOTEQ;
        break;
    case Opcode::ISLT:
        tt = TokenType::LT;
        break;
    case Opcode::ISLE:
        tt = TokenType::LTEQ;
        break;
    case Opcode::ISGT:
        tt = TokenType::GT;
        break;
    case Opcode::ISGE:
        tt = TokenType::GTEQ;
        break;
    default:
        tt = TokenType::EQEQ;
        break;
    }

    Value result = rt.execute_binary_op(tt, lv, rv);
    if (result.type_id == invalid_type_id) {
        fail("Comparison failed in test_and_skip");
        return;
    }

    if (result.type_id == rt.bool_type()) {
        skip = result.data.bval;
    } else {
        fail("Comparison result must be boolean");
        return;
    }

    if (skip) { frames_.back().pc++; }
}

void VirtualMachine::call_intrinsic(IntrinsicId id, uint8_t dest_reg, uint8_t argc)
{
    auto& rt = nw::kernel::runtime();
    auto read_int = [&](uint8_t reg_idx, int32_t& out) -> bool {
        Value val = reg(reg_idx);
        if (val.type_id != rt.int_type()) {
            fail("Intrinsic arguments must be int");
            return false;
        }
        out = val.data.ival;
        return true;
    };
    auto read_array = [&](uint8_t reg_idx, IArray*& out, TypeID& elem_type) -> bool {
        Value val = reg(reg_idx);
        const Type* type = rt.get_type(val.type_id);
        if (!type || type->type_kind != TK_array) {
            fail("Intrinsic expects array");
            return false;
        }
        if (!type->type_params[1].empty()) {
            fail("Intrinsic expects dynamic array");
            return false;
        }
        elem_type = type->type_params[0].as<TypeID>();
        out = rt.get_array_typed(val.data.hptr);
        if (!out) {
            fail("Intrinsic expects dynamic array");
            return false;
        }
        return true;
    };
    auto read_map = [&](uint8_t reg_idx, TypeID& key_type, TypeID& value_type, HeapPtr& map_ptr) -> bool {
        Value val = reg(reg_idx);
        const Type* type = rt.get_type(val.type_id);
        if (!type || type->type_kind != TK_map) {
            fail("Intrinsic expects map");
            return false;
        }
        if (!type->type_params[0].is<TypeID>() || !type->type_params[1].is<TypeID>()) {
            fail("Intrinsic expects map type parameters");
            return false;
        }
        key_type = type->type_params[0].as<TypeID>();
        value_type = type->type_params[1].as<TypeID>();
        map_ptr = val.data.hptr;
        return true;
    };
    auto read_string = [&](uint8_t reg_idx, const char* context) -> bool {
        Value val = reg(reg_idx);
        if (val.type_id != rt.string_type() || val.data.hptr.value == 0) {
            fail(fmt::format("{} expects string argument", context));
            return false;
        }
        return true;
    };

    switch (id) {
    case IntrinsicId::BitNot: {
        if (argc != 1) {
            fail("bit_not expects 1 argument");
            return;
        }
        int32_t value = 0;
        if (!read_int(static_cast<uint8_t>(dest_reg + 1), value)) return;
        reg(dest_reg) = Value::make_int(~value);
        return;
    }
    case IntrinsicId::BitAnd:
    case IntrinsicId::BitOr:
    case IntrinsicId::BitXor:
    case IntrinsicId::BitShl:
    case IntrinsicId::BitShr: {
        if (argc != 2) {
            fail("bit op expects 2 arguments");
            return;
        }
        int32_t lhs = 0;
        int32_t rhs = 0;
        if (!read_int(static_cast<uint8_t>(dest_reg + 1), lhs)) return;
        if (!read_int(static_cast<uint8_t>(dest_reg + 2), rhs)) return;

        int32_t result = 0;
        switch (id) {
        case IntrinsicId::BitAnd:
            result = lhs & rhs;
            break;
        case IntrinsicId::BitOr:
            result = lhs | rhs;
            break;
        case IntrinsicId::BitXor:
            result = lhs ^ rhs;
            break;
        case IntrinsicId::BitShl:
            result = lhs << rhs;
            break;
        case IntrinsicId::BitShr:
            result = lhs >> rhs;
            break;
        default:
            break;
        }
        reg(dest_reg) = Value::make_int(result);
        return;
    }
    case IntrinsicId::ArrayPush: {
        if (argc != 2) {
            fail("array.push expects 2 arguments");
            return;
        }
        IArray* arr = nullptr;
        TypeID elem_type = invalid_type_id;
        if (!read_array(static_cast<uint8_t>(dest_reg + 1), arr, elem_type)) return;
        Value val = reg(static_cast<uint8_t>(dest_reg + 2));
        if (val.type_id != elem_type) {
            fail("array.push value type mismatch");
            return;
        }
        arr->append_value(val, rt);
        reg(dest_reg) = Value(rt.void_type());
        return;
    }
    case IntrinsicId::ArrayPop: {
        if (argc != 1) {
            fail("array.pop expects 1 argument");
            return;
        }
        IArray* arr = nullptr;
        TypeID elem_type = invalid_type_id;
        if (!read_array(static_cast<uint8_t>(dest_reg + 1), arr, elem_type)) { return; }
        if (arr->size() == 0) {
            fail("array.pop on empty array");
            return;
        }
        Value result;
        if (!arr->get_value(arr->size() - 1, result, rt)) {
            fail("array.pop failed");
            return;
        }
        arr->resize(arr->size() - 1);
        reg(dest_reg) = result;
        return;
    }
    case IntrinsicId::ArrayLen: {
        if (argc != 1) {
            fail("array.len expects 1 argument");
            return;
        }
        IArray* arr = nullptr;
        TypeID elem_type = invalid_type_id;
        if (!read_array(static_cast<uint8_t>(dest_reg + 1), arr, elem_type)) { return; }
        reg(dest_reg) = Value::make_int(static_cast<int32_t>(arr->size()));
        return;
    }
    case IntrinsicId::ArrayClear: {
        if (argc != 1) {
            fail("array.clear expects 1 argument");
            return;
        }
        IArray* arr = nullptr;
        TypeID elem_type = invalid_type_id;
        if (!read_array(static_cast<uint8_t>(dest_reg + 1), arr, elem_type)) { return; }
        arr->clear();
        reg(dest_reg) = Value(rt.void_type());
        return;
    }
    case IntrinsicId::ArrayReserve: {
        if (argc != 2) {
            fail("array.reserve expects 2 arguments");
            return;
        }
        IArray* arr = nullptr;
        TypeID elem_type = invalid_type_id;
        if (!read_array(static_cast<uint8_t>(dest_reg + 1), arr, elem_type)) { return; }
        int32_t count = 0;
        if (!read_int(static_cast<uint8_t>(dest_reg + 2), count)) { return; }
        if (count < 0) {
            fail("array.reserve expects non-negative size");
            return;
        }
        arr->reserve(static_cast<size_t>(count));
        reg(dest_reg) = Value(rt.void_type());
        return;
    }
    case IntrinsicId::ArrayGet: {
        if (argc != 2) {
            fail("array.get expects 2 arguments");
            return;
        }
        IArray* arr = nullptr;
        TypeID elem_type = invalid_type_id;
        if (!read_array(static_cast<uint8_t>(dest_reg + 1), arr, elem_type)) { return; }
        int32_t index = 0;
        if (!read_int(static_cast<uint8_t>(dest_reg + 2), index)) { return; }
        if (index < 0) {
            fail("array.get index must be non-negative");
            return;
        }
        size_t idx = static_cast<size_t>(index);
        if (idx >= arr->size()) {
            fail("array.get index out of bounds");
            return;
        }

        if (auto* struct_arr = dynamic_cast<StructArray*>(arr)) {
            const void* src = struct_arr->element_data(idx);
            CallFrame& frame = frames_.back();
            uint32_t offset = frame.stack_alloc(struct_arr->elem_size, struct_arr->elem_alignment, elem_type);
            std::memcpy(frame.stack_.data() + offset, src, struct_arr->elem_size);
            reg(dest_reg) = Value::make_stack(offset, elem_type);
        } else {
            Value result;
            if (!arr->get_value(idx, result, rt)) {
                fail("array.get failed");
                return;
            }
            reg(dest_reg) = result;
        }
        return;
    }
    case IntrinsicId::ArraySet: {
        if (argc != 3) {
            fail("array.set expects 3 arguments");
            return;
        }
        IArray* arr = nullptr;
        TypeID elem_type = invalid_type_id;
        if (!read_array(static_cast<uint8_t>(dest_reg + 1), arr, elem_type)) { return; }
        int32_t index = 0;
        if (!read_int(static_cast<uint8_t>(dest_reg + 2), index)) { return; }
        if (index < 0) {
            fail("array.set index must be non-negative");
            return;
        }
        Value val = reg(static_cast<uint8_t>(dest_reg + 3));
        if (val.type_id != elem_type) {
            fail("array.set value type mismatch");
            return;
        }
        if (!arr->set_value(static_cast<size_t>(index), val, rt)) {
            fail("array.set index out of bounds");
            return;
        }
        reg(dest_reg) = Value(rt.void_type());
        return;
    }
    case IntrinsicId::MapLen: {
        if (argc != 1) {
            fail("map.len expects 1 argument");
            return;
        }
        TypeID key_type = invalid_type_id;
        TypeID value_type = invalid_type_id;
        HeapPtr map_ptr{};
        if (!read_map(static_cast<uint8_t>(dest_reg + 1), key_type, value_type, map_ptr)) { return; }
        reg(dest_reg) = Value::make_int(static_cast<int32_t>(rt.map_size(map_ptr)));
        return;
    }
    case IntrinsicId::MapHas: {
        if (argc != 2) {
            fail("map.has expects 2 arguments");
            return;
        }
        TypeID key_type = invalid_type_id;
        TypeID value_type = invalid_type_id;
        HeapPtr map_ptr{};
        if (!read_map(static_cast<uint8_t>(dest_reg + 1), key_type, value_type, map_ptr)) { return; }
        Value key_val = reg(static_cast<uint8_t>(dest_reg + 2));
        if (key_val.type_id != key_type) {
            fail("map.has key type mismatch");
            return;
        }
        reg(dest_reg) = Value::make_bool(rt.map_contains(map_ptr, key_val));
        return;
    }
    case IntrinsicId::MapGet: {
        if (argc != 2) {
            fail("map.get expects 2 arguments");
            return;
        }
        TypeID key_type = invalid_type_id;
        TypeID value_type = invalid_type_id;
        HeapPtr map_ptr{};
        if (!read_map(static_cast<uint8_t>(dest_reg + 1), key_type, value_type, map_ptr)) { return; }
        Value key_val = reg(static_cast<uint8_t>(dest_reg + 2));
        if (key_val.type_id != key_type) {
            fail("map.get key type mismatch");
            return;
        }
        Value result;
        if (!rt.map_get(map_ptr, key_val, result)) {
            fail("map.get missing key");
            return;
        }
        reg(dest_reg) = result;
        return;
    }
    case IntrinsicId::MapSet: {
        if (argc != 3) {
            fail("map.set expects 3 arguments");
            return;
        }
        TypeID key_type = invalid_type_id;
        TypeID value_type = invalid_type_id;
        HeapPtr map_ptr{};
        if (!read_map(static_cast<uint8_t>(dest_reg + 1), key_type, value_type, map_ptr)) { return; }
        Value key_val = reg(static_cast<uint8_t>(dest_reg + 2));
        Value value_val = reg(static_cast<uint8_t>(dest_reg + 3));
        if (key_val.type_id != key_type) {
            fail("map.set key type mismatch");
            return;
        }
        if (value_val.type_id != value_type) {
            fail("map.set value type mismatch");
            return;
        }
        reg(dest_reg) = Value::make_bool(rt.map_set(map_ptr, key_val, value_val));
        return;
    }
    case IntrinsicId::MapRemove: {
        if (argc != 2) {
            fail("map.remove expects 2 arguments");
            return;
        }
        TypeID key_type = invalid_type_id;
        TypeID value_type = invalid_type_id;
        HeapPtr map_ptr{};
        if (!read_map(static_cast<uint8_t>(dest_reg + 1), key_type, value_type, map_ptr)) { return; }
        Value key_val = reg(static_cast<uint8_t>(dest_reg + 2));
        if (key_val.type_id != key_type) {
            fail("map.remove key type mismatch");
            return;
        }
        reg(dest_reg) = Value::make_bool(rt.map_remove(map_ptr, key_val));
        return;
    }
    case IntrinsicId::MapClear: {
        if (argc != 1) {
            fail("map.clear expects 1 argument");
            return;
        }
        TypeID key_type = invalid_type_id;
        TypeID value_type = invalid_type_id;
        HeapPtr map_ptr{};
        if (!read_map(static_cast<uint8_t>(dest_reg + 1), key_type, value_type, map_ptr)) { return; }
        rt.map_clear(map_ptr);
        reg(dest_reg) = Value(rt.void_type());
        return;
    }
    case IntrinsicId::MapIterBegin: {
        if (argc != 1) {
            fail("map_iter_begin expects 1 argument");
            return;
        }
        TypeID key_type = invalid_type_id;
        TypeID value_type = invalid_type_id;
        HeapPtr map_ptr{};
        if (!read_map(static_cast<uint8_t>(dest_reg + 1), key_type, value_type, map_ptr)) { return; }
        HeapPtr iter_ptr = rt.map_iter_begin(map_ptr);
        reg(dest_reg) = Value::make_heap(iter_ptr, invalid_type_id);
        return;
    }
    case IntrinsicId::MapIterNext: {
        if (argc != 1) {
            fail("map_iter_next expects 1 argument");
            return;
        }
        Value iter_val = reg(static_cast<uint8_t>(dest_reg + 1));
        if (iter_val.storage != ValueStorage::heap || iter_val.data.hptr.value == 0) {
            fail("map_iter_next expects iterator argument");
            return;
        }
        Value key, value;
        bool valid = rt.map_iter_next(iter_val.data.hptr, key, value);
        reg(dest_reg) = Value::make_bool(valid);
        reg(static_cast<uint8_t>(dest_reg + 1)) = key;
        reg(static_cast<uint8_t>(dest_reg + 2)) = value;
        return;
    }
    case IntrinsicId::MapIterEnd: {
        if (argc != 2) {
            fail("map_iter_end expects 2 arguments");
            return;
        }
        TypeID key_type = invalid_type_id;
        TypeID value_type = invalid_type_id;
        HeapPtr map_ptr{};
        if (!read_map(static_cast<uint8_t>(dest_reg + 1), key_type, value_type, map_ptr)) { return; }
        Value iter_val = reg(static_cast<uint8_t>(dest_reg + 2));
        if (iter_val.storage != ValueStorage::heap || iter_val.data.hptr.value == 0) {
            fail("map_iter_end expects iterator argument");
            return;
        }
        rt.map_iter_end(map_ptr, iter_val.data.hptr);
        reg(dest_reg) = Value(rt.void_type());
        return;
    }
    case IntrinsicId::StringLen: {
        if (argc != 1) {
            fail("string.len expects 1 argument");
            return;
        }
        if (!read_string(static_cast<uint8_t>(dest_reg + 1), "string.len")) { return; }
        Value str_val = reg(static_cast<uint8_t>(dest_reg + 1));
        reg(dest_reg) = Value::make_int(static_cast<int32_t>(rt.get_string_length(str_val.data.hptr)));
        return;
    }
    case IntrinsicId::StringSubstr: {
        if (argc != 3) {
            fail("string.substr expects 3 arguments");
            return;
        }
        if (!read_string(static_cast<uint8_t>(dest_reg + 1), "string.substr")) { return; }
        Value str_val = reg(static_cast<uint8_t>(dest_reg + 1));
        int32_t start = 0, len = 0;
        if (!read_int(static_cast<uint8_t>(dest_reg + 2), start)) { return; }
        if (!read_int(static_cast<uint8_t>(dest_reg + 3), len)) { return; }

        StringRepr* sr = static_cast<StringRepr*>(rt.heap_.get_ptr(str_val.data.hptr));
        uint32_t str_len = sr->length;

        if (start < 0) start = 0;
        if (start > static_cast<int32_t>(str_len)) start = str_len;
        if (len < 0) len = 0;
        if (start + len > static_cast<int32_t>(str_len)) len = str_len - start;

        HeapPtr new_str = rt.alloc_string_view(sr->backing, sr->offset + start, len);
        reg(dest_reg) = Value::make_string(new_str);
        return;
    }
    case IntrinsicId::StringCharAt: {
        if (argc != 2) {
            fail("string.char_at expects 2 arguments");
            return;
        }
        if (!read_string(static_cast<uint8_t>(dest_reg + 1), "string.char_at")) { return; }
        Value str_val = reg(static_cast<uint8_t>(dest_reg + 1));
        int32_t idx = 0;
        if (!read_int(static_cast<uint8_t>(dest_reg + 2), idx)) { return; }

        StringView sv = rt.get_string_view(str_val.data.hptr);
        if (idx < 0 || static_cast<size_t>(idx) >= sv.size()) {
            fail("string.char_at index out of bounds");
            return;
        }
        reg(dest_reg) = Value::make_int(static_cast<int32_t>(static_cast<unsigned char>(sv[idx])));
        return;
    }
    case IntrinsicId::StringFind: {
        if (argc != 2) {
            fail("string.find expects 2 arguments");
            return;
        }
        if (!read_string(static_cast<uint8_t>(dest_reg + 1), "string.find")) { return; }
        if (!read_string(static_cast<uint8_t>(dest_reg + 2), "string.find")) { return; }
        Value str_val = reg(static_cast<uint8_t>(dest_reg + 1));
        Value needle_val = reg(static_cast<uint8_t>(dest_reg + 2));
        StringView haystack = rt.get_string_view(str_val.data.hptr);
        StringView needle = rt.get_string_view(needle_val.data.hptr);
        auto pos = haystack.find(needle);
        reg(dest_reg) = Value::make_int(pos == StringView::npos ? -1 : static_cast<int32_t>(pos));
        return;
    }
    case IntrinsicId::StringContains: {
        if (argc != 2) {
            fail("string.contains expects 2 arguments");
            return;
        }
        if (!read_string(static_cast<uint8_t>(dest_reg + 1), "string.contains")) { return; }
        if (!read_string(static_cast<uint8_t>(dest_reg + 2), "string.contains")) { return; }
        Value str_val = reg(static_cast<uint8_t>(dest_reg + 1));
        Value needle_val = reg(static_cast<uint8_t>(dest_reg + 2));
        StringView haystack = rt.get_string_view(str_val.data.hptr);
        StringView needle = rt.get_string_view(needle_val.data.hptr);
        reg(dest_reg) = Value::make_bool(haystack.find(needle) != StringView::npos);
        return;
    }
    case IntrinsicId::StringStartsWith: {
        if (argc != 2) {
            fail("string.starts_with expects 2 arguments");
            return;
        }
        if (!read_string(static_cast<uint8_t>(dest_reg + 1), "string.starts_with")) { return; }
        if (!read_string(static_cast<uint8_t>(dest_reg + 2), "string.starts_with")) { return; }
        Value str_val = reg(static_cast<uint8_t>(dest_reg + 1));
        Value prefix_val = reg(static_cast<uint8_t>(dest_reg + 2));
        StringView sv = rt.get_string_view(str_val.data.hptr);
        StringView prefix = rt.get_string_view(prefix_val.data.hptr);
        bool result = sv.size() >= prefix.size() && sv.substr(0, prefix.size()) == prefix;
        reg(dest_reg) = Value::make_bool(result);
        return;
    }
    case IntrinsicId::StringEndsWith: {
        if (argc != 2) {
            fail("string.ends_with expects 2 arguments");
            return;
        }
        if (!read_string(static_cast<uint8_t>(dest_reg + 1), "string.ends_with")) { return; }
        if (!read_string(static_cast<uint8_t>(dest_reg + 2), "string.ends_with")) { return; }
        Value str_val = reg(static_cast<uint8_t>(dest_reg + 1));
        Value suffix_val = reg(static_cast<uint8_t>(dest_reg + 2));
        StringView sv = rt.get_string_view(str_val.data.hptr);
        StringView suffix = rt.get_string_view(suffix_val.data.hptr);
        bool result = sv.size() >= suffix.size() && sv.substr(sv.size() - suffix.size()) == suffix;
        reg(dest_reg) = Value::make_bool(result);
        return;
    }
    case IntrinsicId::StringToUpper: {
        if (argc != 1) {
            fail("string.to_upper expects 1 argument");
            return;
        }
        if (!read_string(static_cast<uint8_t>(dest_reg + 1), "string.to_upper")) { return; }
        Value str_val = reg(static_cast<uint8_t>(dest_reg + 1));
        StringView sv = rt.get_string_view(str_val.data.hptr);
        String result(sv);
        for (char& c : result) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        reg(dest_reg) = Value::make_string(rt.alloc_string(result));
        return;
    }
    case IntrinsicId::StringToLower: {
        if (argc != 1) {
            fail("string.to_lower expects 1 argument");
            return;
        }
        if (!read_string(static_cast<uint8_t>(dest_reg + 1), "string.to_lower")) { return; }
        Value str_val = reg(static_cast<uint8_t>(dest_reg + 1));
        StringView sv = rt.get_string_view(str_val.data.hptr);
        String result(sv);
        for (char& c : result) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        reg(dest_reg) = Value::make_string(rt.alloc_string(result));
        return;
    }
    case IntrinsicId::StringTrim: {
        if (argc != 1) {
            fail("string.trim expects 1 argument");
            return;
        }
        if (!read_string(static_cast<uint8_t>(dest_reg + 1), "string.trim")) { return; }
        Value str_val = reg(static_cast<uint8_t>(dest_reg + 1));
        StringView sv = rt.get_string_view(str_val.data.hptr);
        size_t start = 0, end = sv.size();
        while (start < end && std::isspace(static_cast<unsigned char>(sv[start])))
            ++start;
        while (end > start && std::isspace(static_cast<unsigned char>(sv[end - 1])))
            --end;

        StringRepr* sr = static_cast<StringRepr*>(rt.heap_.get_ptr(str_val.data.hptr));
        HeapPtr new_str = rt.alloc_string_view(sr->backing, sr->offset + static_cast<uint32_t>(start),
            static_cast<uint32_t>(end - start));
        reg(dest_reg) = Value::make_string(new_str);
        return;
    }
    case IntrinsicId::StringReplace: {
        if (argc != 3) {
            fail("string.replace expects 3 arguments");
            return;
        }
        if (!read_string(static_cast<uint8_t>(dest_reg + 1), "string.replace")) { return; }
        if (!read_string(static_cast<uint8_t>(dest_reg + 2), "string.replace")) { return; }
        if (!read_string(static_cast<uint8_t>(dest_reg + 3), "string.replace")) { return; }
        Value str_val = reg(static_cast<uint8_t>(dest_reg + 1));
        Value old_val = reg(static_cast<uint8_t>(dest_reg + 2));
        Value new_val = reg(static_cast<uint8_t>(dest_reg + 3));
        StringView sv = rt.get_string_view(str_val.data.hptr);
        StringView old_sv = rt.get_string_view(old_val.data.hptr);
        StringView new_sv = rt.get_string_view(new_val.data.hptr);

        if (old_sv.empty()) {
            reg(dest_reg) = str_val;
            return;
        }

        String result;
        size_t pos = 0;
        while (pos < sv.size()) {
            size_t found = sv.find(old_sv, pos);
            if (found == StringView::npos) {
                result.append(sv.substr(pos));
                break;
            }
            result.append(sv.substr(pos, found - pos));
            result.append(new_sv);
            pos = found + old_sv.size();
        }
        reg(dest_reg) = Value::make_string(rt.alloc_string(result));
        return;
    }
    case IntrinsicId::StringSplit: {
        if (argc != 2) {
            fail("string.split expects 2 arguments");
            return;
        }
        if (!read_string(static_cast<uint8_t>(dest_reg + 1), "string.split")) { return; }
        if (!read_string(static_cast<uint8_t>(dest_reg + 2), "string.split")) { return; }
        Value str_val = reg(static_cast<uint8_t>(dest_reg + 1));
        Value delim_val = reg(static_cast<uint8_t>(dest_reg + 2));
        StringView sv = rt.get_string_view(str_val.data.hptr);
        StringView delim = rt.get_string_view(delim_val.data.hptr);

        HeapPtr arr_ptr = rt.create_array_typed(rt.string_type(), 4);
        IArray* arr = rt.get_array_typed(arr_ptr);
        TypeID arr_type = rt.heap_.get_header(arr_ptr)->type_id;

        if (delim.empty()) {
            Value elem = Value::make_string(rt.alloc_string(sv));
            arr->append_value(elem, rt);
        } else {
            size_t pos = 0;
            while (pos <= sv.size()) {
                size_t found = sv.find(delim, pos);
                if (found == StringView::npos) {
                    Value elem = Value::make_string(rt.alloc_string(sv.substr(pos)));
                    arr->append_value(elem, rt);
                    break;
                }
                Value elem = Value::make_string(rt.alloc_string(sv.substr(pos, found - pos)));
                arr->append_value(elem, rt);
                pos = found + delim.size();
            }
        }

        reg(dest_reg) = Value::make_heap(arr_ptr, arr_type);
        return;
    }
    case IntrinsicId::StringJoin: {
        if (argc != 2) {
            fail("string.join expects 2 arguments");
            return;
        }
        IArray* arr = nullptr;
        TypeID elem_type = invalid_type_id;
        if (!read_array(static_cast<uint8_t>(dest_reg + 1), arr, elem_type)) { return; }
        if (elem_type != rt.string_type()) {
            fail("string.join expects array<string>");
            return;
        }
        if (!read_string(static_cast<uint8_t>(dest_reg + 2), "string.join")) { return; }
        Value delim_val = reg(static_cast<uint8_t>(dest_reg + 2));
        StringView delim = rt.get_string_view(delim_val.data.hptr);

        String result;
        for (size_t i = 0; i < arr->size(); ++i) {
            if (i > 0) result.append(delim);
            Value elem;
            arr->get_value(i, elem, rt);
            if (elem.data.hptr.value != 0) {
                result.append(rt.get_string_view(elem.data.hptr));
            }
        }
        reg(dest_reg) = Value::make_string(rt.alloc_string(result));
        return;
    }
    case IntrinsicId::StringToInt: {
        if (argc != 1) {
            fail("string.to_int expects 1 argument");
            return;
        }
        if (!read_string(static_cast<uint8_t>(dest_reg + 1), "string.to_int")) { return; }
        Value str_val = reg(static_cast<uint8_t>(dest_reg + 1));
        StringView sv = rt.get_string_view(str_val.data.hptr);
        String s(sv);
        try {
            int32_t val = std::stoi(s);
            reg(dest_reg) = Value::make_int(val);
        } catch (...) {
            reg(dest_reg) = Value::make_int(0);
        }
        return;
    }
    case IntrinsicId::StringToFloat: {
        if (argc != 1) {
            fail("string.to_float expects 1 argument");
            return;
        }
        if (!read_string(static_cast<uint8_t>(dest_reg + 1), "string.to_float")) { return; }
        Value str_val = reg(static_cast<uint8_t>(dest_reg + 1));
        StringView sv = rt.get_string_view(str_val.data.hptr);
        String s(sv);
        try {
            float val = std::stof(s);
            reg(dest_reg) = Value::make_float(val);
        } catch (...) {
            reg(dest_reg) = Value::make_float(0.0f);
        }
        return;
    }
    case IntrinsicId::StringFromCharCode: {
        if (argc != 1) {
            fail("string.from_char_code expects 1 argument");
            return;
        }
        int32_t code = 0;
        if (!read_int(static_cast<uint8_t>(dest_reg + 1), code)) { return; }
        if (code < 0 || code > 255) {
            fail("string.from_char_code expects 0-255");
            return;
        }
        char c = static_cast<char>(code);
        reg(dest_reg) = Value::make_string(rt.alloc_string(StringView(&c, 1)));
        return;
    }
    case IntrinsicId::StringConcat: {
        String result;
        for (uint8_t i = 0; i < argc; ++i) {
            Value v = reg(static_cast<uint8_t>(dest_reg + 1 + i));
            if (v.type_id == rt.string_type()) {
                if (v.data.hptr.value != 0) {
                    absl::StrAppend(&result, rt.get_string_view(v.data.hptr));
                }
            } else if (v.type_id == rt.int_type()) {
                absl::StrAppend(&result, v.data.ival);
            } else if (v.type_id == rt.float_type()) {
                absl::StrAppend(&result, v.data.fval);
            } else if (v.type_id == rt.bool_type()) {
                absl::StrAppend(&result, v.data.bval ? "true" : "false");
            } else {
                fail("Cannot convert type to string in f-string");
                return;
            }
        }
        HeapPtr result_ptr = rt.alloc_string(result);
        reg(dest_reg) = Value::make_string(result_ptr);
        return;
    }
    case IntrinsicId::StringAppend: {
        if (argc != 2) {
            fail("string_append expects 2 arguments");
            return;
        }
        Value str_val = reg(static_cast<uint8_t>(dest_reg + 1));
        Value append_val = reg(static_cast<uint8_t>(dest_reg + 2));
        if (str_val.type_id != rt.string_type() || append_val.type_id != rt.string_type()) {
            fail("string_append expects string arguments");
            return;
        }
        String result;
        if (str_val.data.hptr.value != 0) {
            result = String(rt.get_string_view(str_val.data.hptr));
        }
        if (append_val.data.hptr.value != 0) {
            result.append(rt.get_string_view(append_val.data.hptr));
        }
        reg(dest_reg) = Value::make_string(rt.alloc_string(result));
        return;
    }
    case IntrinsicId::StringInsert: {
        if (argc != 3) {
            fail("string_insert expects 3 arguments");
            return;
        }
        Value str_val = reg(static_cast<uint8_t>(dest_reg + 1));
        Value pos_val = reg(static_cast<uint8_t>(dest_reg + 2));
        Value insert_val = reg(static_cast<uint8_t>(dest_reg + 3));
        if (str_val.type_id != rt.string_type() || insert_val.type_id != rt.string_type()) {
            fail("string_insert expects string arguments");
            return;
        }
        if (pos_val.type_id != rt.int_type()) {
            fail("string_insert expects int position");
            return;
        }
        String result;
        if (str_val.data.hptr.value != 0) {
            result = String(rt.get_string_view(str_val.data.hptr));
        }
        int32_t pos = pos_val.data.ival;
        if (pos < 0) pos = 0;
        if (pos > static_cast<int32_t>(result.size())) pos = static_cast<int32_t>(result.size());
        StringView to_insert;
        if (insert_val.data.hptr.value != 0) {
            to_insert = rt.get_string_view(insert_val.data.hptr);
        }
        result.insert(static_cast<size_t>(pos), to_insert);
        reg(dest_reg) = Value::make_string(rt.alloc_string(result));
        return;
    }
    case IntrinsicId::StringReverse: {
        if (argc != 1) {
            fail("string_reverse expects 1 argument");
            return;
        }
        Value str_val = reg(static_cast<uint8_t>(dest_reg + 1));
        if (str_val.type_id != rt.string_type()) {
            fail("string_reverse expects string argument");
            return;
        }
        String result;
        if (str_val.data.hptr.value != 0) {
            StringView sv = rt.get_string_view(str_val.data.hptr);
            result = String(sv.rbegin(), sv.rend());
        }
        reg(dest_reg) = Value::make_string(rt.alloc_string(result));
        return;
    }
    default:
        fail("Unknown intrinsic id");
        return;
    }
}

void VirtualMachine::op_field_access(Opcode op, uint8_t a, uint8_t b, uint8_t c, const BytecodeModule* module)
{
    bool is_get = false;
    uint32_t ref_idx = 0;
    uint8_t struct_reg = 0;
    uint8_t val_reg = 0;

    switch (op) {
    case Opcode::FIELDGETI:
    case Opcode::FIELDGETF:
    case Opcode::FIELDGETB:
    case Opcode::FIELDGETS:
    case Opcode::FIELDGETO:
    case Opcode::FIELDGETH:
        is_get = true;
        struct_reg = b;
        ref_idx = c;
        break;
    case Opcode::FIELDGETI_R:
    case Opcode::FIELDGETF_R:
    case Opcode::FIELDGETB_R:
    case Opcode::FIELDGETS_R:
    case Opcode::FIELDGETO_R:
    case Opcode::FIELDGETH_R:
        is_get = true;
        struct_reg = b;
        ref_idx = static_cast<uint32_t>(reg(c).data.ival);
        break;
    case Opcode::FIELDSETI:
    case Opcode::FIELDSETF:
    case Opcode::FIELDSETB:
    case Opcode::FIELDSETS:
    case Opcode::FIELDSETO:
    case Opcode::FIELDSETH:
        is_get = false;
        struct_reg = a;
        ref_idx = b;
        val_reg = c;
        break;
    case Opcode::FIELDSETI_R:
    case Opcode::FIELDSETF_R:
    case Opcode::FIELDSETB_R:
    case Opcode::FIELDSETS_R:
    case Opcode::FIELDSETO_R:
    case Opcode::FIELDSETH_R:
        is_get = false;
        struct_reg = a;
        ref_idx = static_cast<uint32_t>(reg(b).data.ival);
        val_reg = c;
        break;
    default:
        return;
    }

    if (ref_idx >= module->field_offsets.size()) {
        fail("Field reference index out of range");
        return;
    }

    uint32_t offset = module->field_offsets[ref_idx];
    TypeID type_id = module->field_types[ref_idx];
    Value struct_val = reg(struct_reg);

    if (is_get) {
        reg(a) = nw::kernel::runtime().read_field_at_offset(struct_val.data.hptr, offset, type_id);
    } else {
        Value val = reg(val_reg);
        if (!nw::kernel::runtime().write_field_at_offset(struct_val.data.hptr, offset, type_id, val)) {
            fail("Field write failed (type mismatch?)");
        }
    }
}

} // namespace nw::smalls
