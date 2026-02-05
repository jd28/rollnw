#include "Bytecode.hpp"
#include "runtime.hpp"

#include "../log.hpp"

#include <absl/strings/str_join.h>
#include <fmt/format.h>

#include <stdexcept>

namespace nw::smalls {

// == Opcodes =================================================================
// ============================================================================

static StringView opcode_name(Opcode op)
{
    switch (op) {
    case Opcode::ADD:
        return "ADD";
    case Opcode::SUB:
        return "SUB";
    case Opcode::MUL:
        return "MUL";
    case Opcode::DIV:
        return "DIV";
    case Opcode::MOD:
        return "MOD";
    case Opcode::NEG:
        return "NEG";
    case Opcode::AND:
        return "AND";
    case Opcode::OR:
        return "OR";
    case Opcode::NOT:
        return "NOT";
    case Opcode::EQ:
        return "EQ";
    case Opcode::NE:
        return "NE";
    case Opcode::LT:
        return "LT";
    case Opcode::LE:
        return "LE";
    case Opcode::GT:
        return "GT";
    case Opcode::GE:
        return "GE";
    case Opcode::LOADK:
        return "LOADK";
    case Opcode::LOADI:
        return "LOADI";
    case Opcode::LOADB:
        return "LOADB";
    case Opcode::MOVE:
        return "MOVE";
    case Opcode::LOADNIL:
        return "LOADNIL";
    case Opcode::GETFIELD:
        return "GETFIELD";
    case Opcode::SETFIELD:
        return "SETFIELD";
    case Opcode::GETTUPLE:
        return "GETTUPLE";
    case Opcode::FIELDGETI:
        return "FIELDGETI";
    case Opcode::FIELDGETI_R:
        return "FIELDGETI_R";
    case Opcode::FIELDSETI:
        return "FIELDSETI";
    case Opcode::FIELDSETI_R:
        return "FIELDSETI_R";
    case Opcode::FIELDGETF:
        return "FIELDGETF";
    case Opcode::FIELDGETF_R:
        return "FIELDGETF_R";
    case Opcode::FIELDSETF:
        return "FIELDSETF";
    case Opcode::FIELDSETF_R:
        return "FIELDSETF_R";
    case Opcode::FIELDGETB:
        return "FIELDGETB";
    case Opcode::FIELDGETB_R:
        return "FIELDGETB_R";
    case Opcode::FIELDSETB:
        return "FIELDSETB";
    case Opcode::FIELDSETB_R:
        return "FIELDSETB_R";
    case Opcode::FIELDGETS:
        return "FIELDGETS";
    case Opcode::FIELDGETS_R:
        return "FIELDGETS_R";
    case Opcode::FIELDSETS:
        return "FIELDSETS";
    case Opcode::FIELDSETS_R:
        return "FIELDSETS_R";
    case Opcode::FIELDGETO:
        return "FIELDGETO";
    case Opcode::FIELDGETO_R:
        return "FIELDGETO_R";
    case Opcode::FIELDSETO:
        return "FIELDSETO";
    case Opcode::FIELDSETO_R:
        return "FIELDSETO_R";
    case Opcode::FIELDGETH:
        return "FIELDGETH";
    case Opcode::FIELDGETH_R:
        return "FIELDGETH_R";
    case Opcode::FIELDSETH:
        return "FIELDSETH";
    case Opcode::FIELDSETH_R:
        return "FIELDSETH_R";
    case Opcode::GETARRAY:
        return "GETARRAY";
    case Opcode::SETARRAY:
        return "SETARRAY";
    case Opcode::NEWSTRUCT:
        return "NEWSTRUCT";
    case Opcode::JMP:
        return "JMP";
    case Opcode::JMPT:
        return "JMPT";
    case Opcode::JMPF:
        return "JMPF";
    case Opcode::CALL:
        return "CALL";
    case Opcode::CALLNATIVE:
        return "CALLNATIVE";
    case Opcode::CALLINTR:
        return "CALLINTR";
    case Opcode::CALLINTR_R:
        return "CALLINTR_R";
    case Opcode::RET:
        return "RET";
    case Opcode::RETVOID:
        return "RETVOID";
    case Opcode::CAST:
        return "CAST";
    case Opcode::TYPEOF:
        return "TYPEOF";
    case Opcode::IS:
        return "IS";
    case Opcode::ISEQ:
        return "ISEQ";
    case Opcode::ISNE:
        return "ISNE";
    case Opcode::ISLT:
        return "ISLT";
    case Opcode::ISLE:
        return "ISLE";
    case Opcode::ISGT:
        return "ISGT";
    case Opcode::ISGE:
        return "ISGE";
    case Opcode::NEWTUPLE:
        return "NEWTUPLE";
    case Opcode::NEWARRAY:
        return "NEWARRAY";
    case Opcode::NEWMAP:
        return "NEWMAP";
    case Opcode::MAPGET:
        return "MAPGET";
    case Opcode::MAPSET:
        return "MAPSET";
    case Opcode::STACK_ALLOC:
        return "STACK_ALLOC";
    case Opcode::STACK_COPY:
        return "STACK_COPY";
    case Opcode::STACK_FIELDGET:
        return "STACK_FIELDGET";
    case Opcode::STACK_FIELDGET_R:
        return "STACK_FIELDGET_R";
    case Opcode::STACK_FIELDSET:
        return "STACK_FIELDSET";
    case Opcode::STACK_FIELDSET_R:
        return "STACK_FIELDSET_R";
    case Opcode::STACK_INDEXGET:
        return "STACK_INDEXGET";
    case Opcode::STACK_INDEXSET:
        return "STACK_INDEXSET";
    case Opcode::NEWSUM:
        return "NEWSUM";
    case Opcode::SUMINIT:
        return "SUMINIT";
    case Opcode::SUMGETTAG:
        return "SUMGETTAG";
    case Opcode::SUMGETPAYLOAD:
        return "SUMGETPAYLOAD";
    case Opcode::CLOSURE:
        return "CLOSURE";
    case Opcode::GETUPVAL:
        return "GETUPVAL";
    case Opcode::SETUPVAL:
        return "SETUPVAL";
    case Opcode::CLOSEUPVALS:
        return "CLOSEUPVALS";
    case Opcode::CALLCLOSURE:
        return "CALLCLOSURE";
    case Opcode::GETGLOBAL:
        return "GETGLOBAL";
    case Opcode::SETGLOBAL:
        return "SETGLOBAL";
    default:
        return "";
    }
}

// == Instruction =============================================================
// ============================================================================

Instruction Instruction::make_abc(Opcode op, uint8_t a, uint8_t b, uint8_t c)
{
    return Instruction{
        (static_cast<uint32_t>(op) << 24)
        | (static_cast<uint32_t>(a) << 16)
        | (static_cast<uint32_t>(b) << 8)
        | static_cast<uint32_t>(c)};
}

Instruction Instruction::make_abx(Opcode op, uint8_t a, uint16_t bx)
{
    return Instruction{
        (static_cast<uint32_t>(op) << 24)
        | (static_cast<uint32_t>(a) << 16)
        | static_cast<uint32_t>(bx)};
}

Instruction Instruction::make_asbx(Opcode op, uint8_t a, int16_t sbx)
{
    uint16_t bx = static_cast<uint16_t>(sbx);
    return Instruction{
        (static_cast<uint32_t>(op) << 24)
        | (static_cast<uint32_t>(a) << 16)
        | static_cast<uint32_t>(bx)};
}

Instruction Instruction::make_jump(Opcode op, int32_t offset)
{
    // Pack 24-bit signed offset
    uint32_t offset_bits = static_cast<uint32_t>(offset) & 0xFFFFFF;
    return Instruction{
        (static_cast<uint32_t>(op) << 24) | offset_bits};
}

// == Constant ================================================================
// ============================================================================

Constant Constant::make_int(int32_t v)
{
    Constant c;
    c.type_id = invalid_type_id; // Will be set by compiler
    c.data.ival = v;
    return c;
}

Constant Constant::make_float(float v)
{
    Constant c;
    c.type_id = invalid_type_id;
    c.data.fval = v;
    return c;
}

Constant Constant::make_string(uint32_t idx)
{
    Constant c;
    c.type_id = invalid_type_id;
    c.data.string_idx = idx;
    return c;
}

// == CompiledFunction ========================================================
// ============================================================================

CompiledFunction::CompiledFunction(String name_)
    : name(std::move(name_))
{
}

void CompiledFunction::disassemble(String* result, const BytecodeModule* module) const
{

    fmt::format_to(std::back_inserter(*result), "Function: {} (params: {}, regs: {})\n", name, param_count, register_count);

    for (size_t i = 0; i < instructions.size(); ++i) {
        const auto& instr = instructions[i];
        Opcode op = instr.opcode();

        auto op_name = opcode_name(op);
        if (!op_name.empty()) {
            fmt::format_to(std::back_inserter(*result), "  {:04d} {:<10} ", i, op_name);
        } else {
            fmt::format_to(std::back_inserter(*result), "  {:04d} OP_{:<7} ", i, int(op));
        }

        switch (op) {
        // ABC format
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
            fmt::format_to(std::back_inserter(*result), "r{}, r{}, r{}", instr.arg_a(), instr.arg_b(), instr.arg_c());
            break;

        case Opcode::NEG:
        case Opcode::NOT:
        case Opcode::MOVE:
        case Opcode::CAST:
        case Opcode::TYPEOF:
        case Opcode::ISEQ:
        case Opcode::ISNE:
        case Opcode::ISLT:
        case Opcode::ISLE:
        case Opcode::ISGT:
        case Opcode::ISGE:
        case Opcode::NEWTUPLE: // r0 = tuple(start=r0+1, count=b) -> uses B for count
            fmt::format_to(std::back_inserter(*result), "r{}, r{}", instr.arg_a(), instr.arg_b());
            if (instr.arg_c() != 0) fmt::format_to(std::back_inserter(*result), ", {}", instr.arg_c());
            break;

        case Opcode::LOADNIL:
        case Opcode::RETVOID:
            fmt::format_to(std::back_inserter(*result), "r{}", instr.arg_a());
            break;

        case Opcode::RET:
            fmt::format_to(std::back_inserter(*result), "r{}", instr.arg_a());
            break;

        case Opcode::CALL:
        case Opcode::CALLNATIVE:
            // rA = call(func=B, argc=C)
            fmt::format_to(std::back_inserter(*result), "r{}, func={}, argc={}", instr.arg_a(), instr.arg_b(), instr.arg_c());
            break;

        case Opcode::CALLCLOSURE:
            fmt::format_to(std::back_inserter(*result), "r{}, closure=r{}, argc={}", instr.arg_a(), instr.arg_b(), instr.arg_c());
            break;

        case Opcode::GETFIELD:
        case Opcode::GETTUPLE:
        case Opcode::FIELDGETI:
        case Opcode::FIELDGETF:
        case Opcode::FIELDGETB:
        case Opcode::FIELDGETS:
        case Opcode::FIELDGETO:
        case Opcode::FIELDGETH:
            // rA = rB[C_imm]
            fmt::format_to(std::back_inserter(*result), "r{}, r{}[{}]", instr.arg_a(), instr.arg_b(), instr.arg_c());
            break;

        case Opcode::FIELDGETI_R:
        case Opcode::FIELDGETF_R:
        case Opcode::FIELDGETB_R:
        case Opcode::FIELDGETS_R:
        case Opcode::FIELDGETO_R:
        case Opcode::FIELDGETH_R:
            // rA = rB[rC]
            fmt::format_to(std::back_inserter(*result), "r{}, r{}[r{}]", instr.arg_a(), instr.arg_b(), instr.arg_c());
            break;

        case Opcode::SETFIELD:
        case Opcode::FIELDSETI:
        case Opcode::FIELDSETF:
        case Opcode::FIELDSETB:
        case Opcode::FIELDSETS:
        case Opcode::FIELDSETO:
        case Opcode::FIELDSETH:
            // rA[B_imm] = rC
            fmt::format_to(std::back_inserter(*result), "r{}[{}], r{}", instr.arg_a(), instr.arg_b(), instr.arg_c());
            break;

        case Opcode::FIELDSETI_R:
        case Opcode::FIELDSETF_R:
        case Opcode::FIELDSETB_R:
        case Opcode::FIELDSETS_R:
        case Opcode::FIELDSETO_R:
        case Opcode::FIELDSETH_R:
            // rA[rB] = rC
            fmt::format_to(std::back_inserter(*result), "r{}[r{}], r{}", instr.arg_a(), instr.arg_b(), instr.arg_c());
            break;

        // ABx format
        case Opcode::LOADK: {
            uint16_t bx = instr.arg_bx();
            fmt::format_to(std::back_inserter(*result), "r{}, k({})", instr.arg_a(), bx);
            if (module && bx < module->constants.size()) {
                const auto& k = module->constants[bx];
                absl::StrAppend(result, " ; ");
                // This assumes we can inspect constant types, which relies on the Variant or tracking
                // Since Constant is a struct with a union, we need type_id or some way to know.
                // The current Constant struct has 'type_id' member.
                // But to print it, we need the runtime... which we don't have here.
                // We'll just print based on likely type or if we can infer it.
                // For now, simple heuristics:
                // Actually, Constant struct definition:
                // union { int32_t ival; float fval; bool bval; uint32_t string_idx; } data;
                // This is ambiguous without type info.
                // But we can try to guess or just print index.
                // Wait, BytecodeModule has string_pool.
                // We can't safely access the union without type_id which requires runtime to decode type_id.
                // So we'll just print the index.
            }
            break;
        }
        case Opcode::NEWSTRUCT:
        case Opcode::NEWARRAY:
        case Opcode::NEWSUM:
        case Opcode::NEWMAP: // NEWMAP key=bx, val=c ?? No, uses ABC or ABx?
            // NEWMAP was r0 = map<key=bx, val=c>. That doesn't fit ABx.
            // Let's assume ABC for NEWMAP for now, or maybe it needs a special encoding.
            // Bytecode.hpp says NEWMAP is Opcode, but encoding?
            // If it needs two type IDs, it needs more than 8 bits each usually?
            // Assuming ABx for now for single type arg instructions.
            fmt::format_to(std::back_inserter(*result), "r{}, type={}", instr.arg_a(), instr.arg_bx());
            break;

        case Opcode::SUMINIT:
            // rA.tag=B, rA.payload=rC (C=255 means no payload)
            if (instr.arg_c() == 255) {
                fmt::format_to(std::back_inserter(*result), "r{}.tag={}", instr.arg_a(), instr.arg_b());
            } else {
                fmt::format_to(std::back_inserter(*result), "r{}.tag={}, r{}.payload=r{}", instr.arg_a(), instr.arg_b(), instr.arg_a(), instr.arg_c());
            }
            break;

        case Opcode::SUMGETTAG:
            fmt::format_to(std::back_inserter(*result), "r{}, r{}", instr.arg_a(), instr.arg_b());
            break;

        case Opcode::SUMGETPAYLOAD:
            fmt::format_to(std::back_inserter(*result), "r{}, r{}, variant={}", instr.arg_a(), instr.arg_b(), instr.arg_c());
            break;

        case Opcode::GETUPVAL:
            fmt::format_to(std::back_inserter(*result), "r{}, upval={}", instr.arg_a(), instr.arg_b());
            break;

        case Opcode::SETUPVAL:
            fmt::format_to(std::back_inserter(*result), "upval={}, r{}", instr.arg_b(), instr.arg_a());
            break;

        case Opcode::CLOSEUPVALS:
            if (!result->empty() && result->back() == ' ') {
                result->pop_back();
            }
            break;

        case Opcode::CLOSURE: {
            uint16_t func_idx = instr.arg_bx();
            size_t upvalue_count = 0;
            if (module && func_idx < module->functions.size()) {
                upvalue_count = module->functions[func_idx]->upvalue_count;
            }

            fmt::format_to(std::back_inserter(*result), "r{}, func={}, upvalues={}", instr.arg_a(), func_idx, upvalue_count);
            absl::StrAppend(result, "\n");

            size_t words = (upvalue_count + 3) / 4;
            size_t remaining = upvalue_count;
            for (size_t w = 0; w < words && i + 1 < instructions.size(); ++w) {
                ++i;
                uint32_t raw = instructions[i].raw;
                size_t limit = remaining < 4 ? remaining : 4;
                String descs;
                for (size_t j = 0; j < limit; ++j) {
                    uint8_t desc = static_cast<uint8_t>((raw >> (8 * j)) & 0xFF);
                    bool is_local = (desc & 0x1) != 0;
                    uint8_t index = static_cast<uint8_t>(desc >> 1);
                    if (j > 0) {
                        absl::StrAppend(&descs, ", ");
                    }
                    if (is_local) {
                        absl::StrAppend(&descs, "local r", index);
                    } else {
                        absl::StrAppend(&descs, "upval ", index);
                    }
                }
                remaining -= limit;
                fmt::format_to(std::back_inserter(*result), "  {:04d} UPVALS     {}", i, descs);
                absl::StrAppend(result, "\n");
            }
            continue;
        }

        // AsBx format
        case Opcode::LOADI:
            fmt::format_to(std::back_inserter(*result), "r{}, {}", instr.arg_a(), instr.arg_sbx());
            break;

        case Opcode::LOADB:
            fmt::format_to(std::back_inserter(*result), "r{}, {}", instr.arg_a(), instr.arg_b() ? "true" : "false");
            break;

        case Opcode::JMPT:
        case Opcode::JMPF:
            fmt::format_to(std::back_inserter(*result), "r{}, {:+d} ; -> {:04d}", instr.arg_a(), instr.arg_sbx(), i + 1 + instr.arg_sbx());
            break;

        // Module globals (ABx format)
        case Opcode::GETGLOBAL:
            fmt::format_to(std::back_inserter(*result), "r{}, g{}", instr.arg_a(), instr.arg_bx());
            break;
        case Opcode::SETGLOBAL:
            fmt::format_to(std::back_inserter(*result), "g{}, r{}", instr.arg_bx(), instr.arg_a());
            break;

        // Jump format
        case Opcode::JMP:
            fmt::format_to(std::back_inserter(*result), "{:+d} ; -> {:04d}", instr.arg_jump(), i + 1 + instr.arg_jump());
            break;

        default:
            fmt::format_to(std::back_inserter(*result), "raw={:08x}", instr.raw);
            break;
        }
        absl::StrAppend(result, "\n");
    }
}

// == Bytecode Module =========================================================
// ============================================================================

BytecodeModule::BytecodeModule(String name)
    : module_name(std::move(name))
{
}

String BytecodeModule::disassemble() const
{
    String result;
    result.reserve(10000);

    fmt::format_to(std::back_inserter(result), "Module: {}\nStrings: {}\n", module_name, string_pool.size());
    for (size_t i = 0; i < string_pool.size(); ++i) {
        fmt::format_to(std::back_inserter(result), "  [{}] \"{}\"\n", i, string_pool[i]);
    }
    // We can't safely print constants without type info easily here
    fmt::format_to(std::back_inserter(result), "Constants: {}\n", constants.size());

    absl::StrAppend(&result, "\n");
    for (const auto* func : functions) {
        func->disassemble(&result, this);
        absl::StrAppend(&result, "\n");
    }
    return result;
}

BytecodeModule::~BytecodeModule()
{
    for (auto* func : functions) {
        delete func;
    }
}

const CompiledFunction* BytecodeModule::get_function(StringView name) const
{
    auto it = function_indices.find(String(name));
    if (it == function_indices.end()) {
        return nullptr;
    }
    return functions[it->second];
}

uint32_t BytecodeModule::get_function_index(StringView name) const
{
    auto it = function_indices.find(String(name));
    if (it == function_indices.end()) {
        return UINT32_MAX;
    }
    return it->second;
}

CompiledFunction* BytecodeModule::add_function(CompiledFunction* func)
{
    uint32_t idx = static_cast<uint32_t>(functions.size());
    function_indices[func->name] = idx;
    functions.push_back(func);
    return func;
}

uint32_t BytecodeModule::add_string(StringView str)
{
    auto it = string_indices.find(str);
    if (it != string_indices.end()) {
        return it->second;
    }

    uint32_t idx = static_cast<uint32_t>(string_pool.size());
    string_pool.emplace_back(str);
    string_indices[str] = idx;
    return idx;
}

const String& BytecodeModule::get_string(uint32_t idx) const
{
    if (idx >= string_pool.size()) {
        throw std::out_of_range("String index out of range");
    }
    return string_pool[idx];
}

uint32_t BytecodeModule::add_constant(const Constant& c)
{
    uint32_t idx = static_cast<uint32_t>(constants.size());
    constants.push_back(c);
    return idx;
}

const Constant& BytecodeModule::get_constant(uint32_t idx) const
{
    if (idx >= constants.size()) {
        throw std::out_of_range("Constant index out of range");
    }
    return constants[idx];
}

uint32_t BytecodeModule::add_field_ref(uint32_t offset, TypeID type_id)
{
    // Try to deduplicate
    for (size_t i = 0; i < field_offsets.size(); ++i) {
        if (field_offsets[i] == offset && field_types[i] == type_id) {
            return static_cast<uint32_t>(i);
        }
    }
    uint32_t idx = static_cast<uint32_t>(field_offsets.size());
    field_offsets.push_back(offset);
    field_types.push_back(type_id);
    return idx;
}

uint32_t BytecodeModule::add_external_ref(InternedString qualified_name)
{
    auto it = external_ref_map.find(qualified_name);
    if (it != external_ref_map.end()) {
        return it->second;
    }
    uint32_t idx = static_cast<uint32_t>(external_refs.size());
    external_refs.push_back(qualified_name);
    external_indices.push_back(UINT32_MAX); // unresolved
    external_ref_map[qualified_name] = idx;
    return idx;
}

bool BytecodeModule::resolve_external_refs(Runtime* runtime)
{
    bool all_resolved = true;
    external_indices.resize(external_refs.size());

    for (size_t i = 0; i < external_refs.size(); ++i) {
        uint32_t ext_idx = runtime->find_external_function(external_refs[i].view());
        if (ext_idx == UINT32_MAX) {
            LOG_F(ERROR, "[bytecode] Failed to resolve external function '{}' in module '{}'",
                external_refs[i].view(), module_name);
            all_resolved = false;
        }
        external_indices[i] = ext_idx;
    }
    return all_resolved;
}

} // namespace nw::smalls
