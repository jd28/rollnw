
#include "BytecodeVerifier.hpp"

#include "runtime.hpp"

#include <fmt/format.h>

namespace nw::smalls {
namespace {

inline bool fail(String* out, StringView msg)
{
    if (out) {
        *out = String(msg);
    }
    return false;
}

inline bool check_reg(String* out, uint8_t r, uint8_t reg_count, StringView context)
{
    if (r >= reg_count) {
        return fail(out, fmt::format("{}: register out of range: r{} (reg_count={})", context, r, reg_count));
    }
    return true;
}

inline bool check_reg_range(String* out, uint8_t first, uint8_t last, uint8_t reg_count, StringView context)
{
    if (first >= reg_count || last >= reg_count || last < first) {
        return fail(out, fmt::format("{}: register range out of range: r{}..r{} (reg_count={})", context, first, last, reg_count));
    }
    return true;
}

bool verify_function(const BytecodeModule* module, const CompiledFunction* func, String* out)
{
    if (!module || !func) {
        return fail(out, "verify: null module/function");
    }

    const uint8_t reg_count = func->register_count;
    if (reg_count < func->param_count) {
        return fail(out, fmt::format("verify: {}: register_count {} < param_count {}", func->name, reg_count, func->param_count));
    }

    const size_t n = func->instructions.size();
    for (size_t pc = 0; pc < n; ++pc) {
        const Instruction instr = func->instructions[pc];
        const Opcode op = instr.opcode();
        const uint8_t a = instr.arg_a();
        const uint8_t b = instr.arg_b();
        const uint8_t c = instr.arg_c();

        const auto ctx = [&]() -> String {
            return fmt::format("verify: {}: pc {} op {}", func->name, pc, static_cast<uint32_t>(op));
        };

        switch (op) {
        // rA = rB op rC
        case Opcode::ADD:
        case Opcode::SUB:
        case Opcode::MUL:
        case Opcode::DIV:
        case Opcode::MOD:
        case Opcode::AND:
        case Opcode::OR:
        case Opcode::EQ:
        case Opcode::NE:
        case Opcode::LT:
        case Opcode::LE:
        case Opcode::GT:
        case Opcode::GE:
        case Opcode::GETARRAY:
        case Opcode::SETARRAY:
        case Opcode::MAPGET:
        case Opcode::MAPSET:
        case Opcode::STACK_INDEXGET:
        case Opcode::STACK_INDEXSET:
            if (!check_reg(out, a, reg_count, ctx())) { return false; }
            if (!check_reg(out, b, reg_count, ctx())) { return false; }
            if (!check_reg(out, c, reg_count, ctx())) { return false; }
            break;

        // rA = op rB
        case Opcode::NEG:
        case Opcode::NOT:
        case Opcode::TYPEOF:
        case Opcode::MOVE:
        case Opcode::STACK_COPY:
        case Opcode::SUMGETTAG:
        case Opcode::SUMGETPAYLOAD:
            if (!check_reg(out, a, reg_count, ctx())) { return false; }
            if (!check_reg(out, b, reg_count, ctx())) { return false; }
            if (op == Opcode::SUMGETPAYLOAD) {
                // c is immediate variant index
            }
            break;

        case Opcode::LOADNIL:
        case Opcode::LOADI:
        case Opcode::LOADB:
            if (!check_reg(out, a, reg_count, ctx())) { return false; }
            break;

        case Opcode::LOADK: {
            if (!check_reg(out, a, reg_count, ctx())) { return false; }
            const uint16_t k = instr.arg_bx();
            if (k >= module->constants.size()) {
                return fail(out, fmt::format("{}: constant index out of range: {} (constants={})", ctx(), k, module->constants.size()));
            }
            break;
        }

        // control flow
        case Opcode::JMP: {
            const int32_t off = instr.arg_jump();
            const int64_t target = static_cast<int64_t>(pc + 1) + static_cast<int64_t>(off);
            if (target < 0 || target > static_cast<int64_t>(n)) {
                return fail(out, fmt::format("{}: JMP target out of range: {}", ctx(), target));
            }
            break;
        }
        case Opcode::JMPT:
        case Opcode::JMPF: {
            if (!check_reg(out, a, reg_count, ctx())) { return false; }
            const int32_t off = static_cast<int32_t>(instr.arg_sbx());
            const int64_t target = static_cast<int64_t>(pc + 1) + static_cast<int64_t>(off);
            if (target < 0 || target > static_cast<int64_t>(n)) {
                return fail(out, fmt::format("{}: JMP* target out of range: {}", ctx(), target));
            }
            break;
        }

        // test-and-skip
        case Opcode::ISEQ:
        case Opcode::ISNE:
        case Opcode::ISLT:
        case Opcode::ISLE:
        case Opcode::ISGT:
        case Opcode::ISGE:
            if (!check_reg(out, a, reg_count, ctx())) { return false; }
            if (!check_reg(out, b, reg_count, ctx())) { return false; }
            break;

        // tuple/field immediate slow ops
        case Opcode::GETFIELD:
        case Opcode::GETTUPLE:
            if (!check_reg(out, a, reg_count, ctx())) { return false; }
            if (!check_reg(out, b, reg_count, ctx())) { return false; }
            break;
        case Opcode::SETFIELD:
            if (!check_reg(out, a, reg_count, ctx())) { return false; }
            if (!check_reg(out, c, reg_count, ctx())) { return false; }
            break;

        // optimized field access: immediate
        case Opcode::FIELDGETI:
        case Opcode::FIELDGETF:
        case Opcode::FIELDGETB:
        case Opcode::FIELDGETS:
        case Opcode::FIELDGETO:
        case Opcode::FIELDGETH:
            if (!check_reg(out, a, reg_count, ctx())) { return false; }
            if (!check_reg(out, b, reg_count, ctx())) { return false; }
            if (c >= module->field_offsets.size()) {
                return fail(out, fmt::format("{}: field ref index out of range: {}", ctx(), c));
            }
            break;

        // optimized field access: register-indexed
        case Opcode::FIELDGETI_R:
        case Opcode::FIELDGETF_R:
        case Opcode::FIELDGETB_R:
        case Opcode::FIELDGETS_R:
        case Opcode::FIELDGETO_R:
        case Opcode::FIELDGETH_R:
            if (!check_reg(out, a, reg_count, ctx())) { return false; }
            if (!check_reg(out, b, reg_count, ctx())) { return false; }
            if (!check_reg(out, c, reg_count, ctx())) { return false; }
            break;

        case Opcode::FIELDSETI:
        case Opcode::FIELDSETF:
        case Opcode::FIELDSETB:
        case Opcode::FIELDSETS:
        case Opcode::FIELDSETO:
        case Opcode::FIELDSETH:
            if (!check_reg(out, a, reg_count, ctx())) { return false; }
            if (!check_reg(out, c, reg_count, ctx())) { return false; }
            if (b >= module->field_offsets.size()) {
                return fail(out, fmt::format("{}: field ref index out of range: {}", ctx(), b));
            }
            break;

        case Opcode::FIELDSETI_R:
        case Opcode::FIELDSETF_R:
        case Opcode::FIELDSETB_R:
        case Opcode::FIELDSETS_R:
        case Opcode::FIELDSETO_R:
        case Opcode::FIELDSETH_R:
            if (!check_reg(out, a, reg_count, ctx())) { return false; }
            if (!check_reg(out, b, reg_count, ctx())) { return false; }
            if (!check_reg(out, c, reg_count, ctx())) { return false; }
            break;

        // field access at runtime-computed offset (for fixed array field indexing)
        case Opcode::FIELDGETI_OFF_R:
        case Opcode::FIELDGETF_OFF_R:
        case Opcode::FIELDGETB_OFF_R:
        case Opcode::FIELDGETS_OFF_R:
        case Opcode::FIELDGETO_OFF_R:
        case Opcode::FIELDGETH_OFF_R:
            if (!check_reg(out, a, reg_count, ctx())) { return false; }
            if (!check_reg(out, b, reg_count, ctx())) { return false; }
            if (!check_reg(out, c, reg_count, ctx())) { return false; }
            break;

        case Opcode::FIELDSETI_OFF_R:
        case Opcode::FIELDSETF_OFF_R:
        case Opcode::FIELDSETB_OFF_R:
        case Opcode::FIELDSETS_OFF_R:
        case Opcode::FIELDSETO_OFF_R:
        case Opcode::FIELDSETH_OFF_R:
            if (!check_reg(out, a, reg_count, ctx())) { return false; }
            if (!check_reg(out, b, reg_count, ctx())) { return false; }
            if (!check_reg(out, c, reg_count, ctx())) { return false; }
            break;

        // calls
        case Opcode::CALL:
        case Opcode::CALLNATIVE:
        case Opcode::CALLEXT:
        case Opcode::CALLINTR: {
            const uint8_t dest = a;
            const uint8_t argc = c;
            if (!check_reg(out, dest, reg_count, ctx())) { return false; }
            if (argc > 0) {
                const uint8_t last = static_cast<uint8_t>(dest + argc);
                if (!check_reg_range(out, static_cast<uint8_t>(dest + 1), last, reg_count, ctx())) { return false; }
            }
            if (op == Opcode::CALL && b >= module->functions.size()) {
                return fail(out, fmt::format("{}: function index out of range: {} (functions={})", ctx(), b, module->functions.size()));
            }
            if (op == Opcode::CALLEXT && b >= module->external_indices.size()) {
                return fail(out, fmt::format("{}: external ref index out of range: {} (externals={})", ctx(), b, module->external_indices.size()));
            }
            break;
        }

        case Opcode::CALLEXT_R:
        case Opcode::CALLINTR_R: {
            const uint8_t dest = a;
            const uint8_t id_reg = b;
            const uint8_t argc = c;
            if (!check_reg(out, dest, reg_count, ctx())) { return false; }
            if (!check_reg(out, id_reg, reg_count, ctx())) { return false; }
            if (argc > 0) {
                const uint8_t last = static_cast<uint8_t>(dest + argc);
                if (!check_reg_range(out, static_cast<uint8_t>(dest + 1), last, reg_count, ctx())) { return false; }
            }
            break;
        }

        case Opcode::CALLCLOSURE: {
            const uint8_t dest = a;
            const uint8_t closure_reg = b;
            const uint8_t argc = c;
            if (!check_reg(out, dest, reg_count, ctx())) { return false; }
            if (!check_reg(out, closure_reg, reg_count, ctx())) { return false; }
            if (argc > 0) {
                const uint8_t last = static_cast<uint8_t>(dest + argc);
                if (!check_reg_range(out, static_cast<uint8_t>(dest + 1), last, reg_count, ctx())) { return false; }
            }
            break;
        }

        // returns
        case Opcode::RET:
            if (!check_reg(out, a, reg_count, ctx())) { return false; }
            break;
        case Opcode::RETVOID:
            break;

        // allocations / type refs
        case Opcode::NEWARRAY:
        case Opcode::NEWMAP:
        case Opcode::NEWSTRUCT:
        case Opcode::NEWSUM:
        case Opcode::CAST:
        case Opcode::IS:
        case Opcode::STACK_ALLOC: {
            if (!check_reg(out, a, reg_count, ctx())) { return false; }
            const uint16_t type_idx = instr.arg_bx();
            if (type_idx >= module->type_refs.size()) {
                return fail(out, fmt::format("{}: type index out of range: {} (type_refs={})", ctx(), type_idx, module->type_refs.size()));
            }
            break;
        }

        case Opcode::NEWTUPLE: {
            const uint8_t dest = a;
            const uint8_t count = b;
            if (!check_reg(out, dest, reg_count, ctx())) { return false; }
            if (count > 0) {
                const uint8_t last = static_cast<uint8_t>(dest + count);
                if (!check_reg_range(out, static_cast<uint8_t>(dest + 1), last, reg_count, ctx())) { return false; }
            }
            break;
        }

        // stack value type ops
        case Opcode::STACK_FIELDGET: {
            if (!check_reg(out, a, reg_count, ctx())) { return false; }
            if (!check_reg(out, b, reg_count, ctx())) { return false; }
            if (c >= module->field_offsets.size()) {
                return fail(out, fmt::format("{}: field ref index out of range: {}", ctx(), c));
            }
            break;
        }
        case Opcode::STACK_FIELDGET_R: {
            if (!check_reg(out, a, reg_count, ctx())) { return false; }
            if (!check_reg(out, b, reg_count, ctx())) { return false; }
            if (!check_reg(out, c, reg_count, ctx())) { return false; }
            break;
        }
        case Opcode::STACK_FIELDSET: {
            if (!check_reg(out, a, reg_count, ctx())) { return false; }
            if (!check_reg(out, c, reg_count, ctx())) { return false; }
            if (b >= module->field_offsets.size()) {
                return fail(out, fmt::format("{}: field ref index out of range: {}", ctx(), b));
            }
            break;
        }
        case Opcode::STACK_FIELDSET_R: {
            if (!check_reg(out, a, reg_count, ctx())) { return false; }
            if (!check_reg(out, b, reg_count, ctx())) { return false; }
            if (!check_reg(out, c, reg_count, ctx())) { return false; }
            break;
        }

        // sum init
        case Opcode::SUMINIT:
            if (!check_reg(out, a, reg_count, ctx())) { return false; }
            if (c != 255 && !check_reg(out, c, reg_count, ctx())) { return false; }
            break;

        // module globals
        case Opcode::GETGLOBAL:
        case Opcode::SETGLOBAL: {
            if (!check_reg(out, a, reg_count, ctx())) { return false; }
            const uint16_t slot = instr.arg_bx();
            if (slot >= module->global_count) {
                return fail(out, fmt::format("{}: global slot out of range: {} (globals={})", ctx(), slot, module->global_count));
            }
            break;
        }

        case Opcode::CLOSEUPVALS:
            break;

        case Opcode::CLOSURE: {
            if (!check_reg(out, a, reg_count, ctx())) { return false; }
            const uint16_t func_idx = instr.arg_bx();
            if (func_idx >= module->functions.size()) {
                return fail(out, fmt::format("{}: closure function index out of range: {} (functions={})", ctx(), func_idx, module->functions.size()));
            }

            const CompiledFunction* callee = module->functions[func_idx];
            const uint8_t upc = callee ? callee->upvalue_count : 0;
            const size_t words = (static_cast<size_t>(upc) + 3) / 4;
            if (pc + words >= n) {
                return fail(out, fmt::format("{}: missing closure descriptor words: need {}", ctx(), words));
            }

            // Validate descriptor indices and skip the raw words.
            size_t up_idx = 0;
            for (size_t w = 0; w < words; ++w) {
                const uint32_t raw = func->instructions[pc + 1 + w].raw;
                for (size_t i = 0; i < 4 && up_idx < upc; ++i, ++up_idx) {
                    const uint8_t desc = static_cast<uint8_t>((raw >> (8 * i)) & 0xFF);
                    const bool is_local = (desc & 0x1) != 0;
                    const uint8_t index = static_cast<uint8_t>(desc >> 1);

                    if (is_local) {
                        if (index >= reg_count) {
                            return fail(out, fmt::format("{}: closure local upvalue reg out of range: r{}", ctx(), index));
                        }
                    } else {
                        // Capturing from parent's upvalues; only valid if current function is itself a closure.
                        if (func->upvalue_count == 0) {
                            return fail(out, fmt::format("{}: closure captures non-local upvalue but enclosing function has no upvalues", ctx()));
                        }
                        if (index >= func->upvalue_count) {
                            return fail(out, fmt::format("{}: closure upvalue index out of range: {} (enclosing upvalues={})", ctx(), index, func->upvalue_count));
                        }
                    }
                }
            }

            pc += words;
            break;
        }

        case Opcode::GETUPVAL:
        case Opcode::SETUPVAL:
            if (!check_reg(out, a, reg_count, ctx())) { return false; }
            break;

        default:
            return fail(out, fmt::format("{}: unknown opcode {}", ctx(), static_cast<uint32_t>(op)));
        }
    }

    return true;
}

} // namespace

bool verify_bytecode_module(const BytecodeModule* module, String* error_message)
{
    if (!module) {
        return fail(error_message, "verify: null module");
    }

    if (module->field_offsets.size() != module->field_types.size()) {
        return fail(error_message, fmt::format("verify: {}: field_offsets ({}) != field_types ({})", module->module_name, module->field_offsets.size(), module->field_types.size()));
    }

    if (module->external_refs.size() != module->external_indices.size()) {
        return fail(error_message, fmt::format("verify: {}: external_refs ({}) != external_indices ({})", module->module_name, module->external_refs.size(), module->external_indices.size()));
    }

    if (module->globals.size() < module->global_count) {
        return fail(error_message, fmt::format("verify: {}: globals size ({}) < global_count ({})", module->module_name, module->globals.size(), module->global_count));
    }

    for (const CompiledFunction* f : module->functions) {
        if (!verify_function(module, f, error_message)) {
            return false;
        }
    }

    return true;
}

} // namespace nw::smalls
