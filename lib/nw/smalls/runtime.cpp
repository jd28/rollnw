#include "runtime.hpp"

#include "../kernel/Kernel.hpp"
#include "../kernel/Strings.hpp"
#include "../log.hpp"
#include "../resources/StaticDirectory.hpp"
#include "../util/macros.hpp"
#include "../util/platform.hpp"
#include "AstCompiler.hpp"
#include "AstResolver.hpp"
#include "Context.hpp"
#include "NullVisitor.hpp"
#include "Smalls.hpp"
#include "Token.hpp"
#include "TypeResolver.hpp"
#include "Validator.hpp"
#include "VirtualMachine.hpp"
#include "stdlib.hpp"

#include "xxhash/xxh3.h"
#include <absl/hash/hash.h>
#include <absl/strings/str_cat.h>
#include <fmt/core.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace nw::smalls {

// == Value ===================================================================
// ============================================================================

Value Value::make_heap(HeapPtr ptr, TypeID tid)
{
    Value val(tid);
    val.storage = ValueStorage::heap;
    val.data.hptr = ptr;
    return val;
}

Value Value::make_bool(bool v)
{
    Value val(nw::kernel::runtime().bool_type());
    val.data.bval = v;
    return val;
}

Value Value::make_int(int32_t v)
{
    Value val(nw::kernel::runtime().int_type());
    val.data.ival = v;
    return val;
}

Value Value::make_float(float v)
{
    Value val(nw::kernel::runtime().float_type());
    val.data.fval = v;
    return val;
}

Value Value::make_string(HeapPtr ptr)
{
    Value val(nw::kernel::runtime().string_type());
    val.storage = ValueStorage::heap;
    val.data.hptr = ptr;
    return val;
}

Value Value::make_object(ObjectHandle obj)
{
    Value val(nw::kernel::runtime().object_type());
    val.data.oval = obj;
    return val;
}

Value Value::make_stack(uint32_t offset, TypeID tid)
{
    Value val(tid);
    val.storage = ValueStorage::stack;
    val.data.stack_offset = offset;
    return val;
}

// == Value Hashing/Equality ==================================================
// ============================================================================

size_t ValueHash::operator()(const Value& v) const noexcept
{
    auto& rt = nw::kernel::runtime();
    if (rt.is_object_like_type(v.type_id)) {
        return absl::HashOf(v.data.oval.to_ull());
    }
    if (rt.is_handle_type(v.type_id) && v.storage == ValueStorage::heap && v.data.hptr.value != 0) {
        if (auto* handle = rt.get_handle(v.data.hptr)) {
            return absl::HashOf(handle->to_ull());
        }
    }
    const Type* type = rt.get_type(v.type_id);

    if (!type) {
        return 0;
    }

    switch (type->primitive_kind) {
    case PK_int:
        return absl::HashOf(v.data.ival);
    case PK_string:
        if (v.data.hptr.value != 0) {
            return absl::HashOf(rt.get_string_view(v.data.hptr));
        }
        return 0;
    default:
        try {
            Value result = rt.execute_hash_op(v);
            if (result.type_id == rt.int_type()) {
                return static_cast<size_t>(static_cast<uint32_t>(result.data.ival));
            }
        } catch (...) {
        }
        return 0;
    }
}

bool ValueEq::operator()(const Value& a, const Value& b) const noexcept
{
    if (a.type_id != b.type_id) {
        return false;
    }

    auto& rt = nw::kernel::runtime();
    if (rt.is_object_like_type(a.type_id)) {
        return a.data.oval == b.data.oval;
    }
    if (rt.is_handle_type(a.type_id)) {
        if (a.storage != ValueStorage::heap || b.storage != ValueStorage::heap) {
            return false;
        }
        if (a.data.hptr.value == 0 || b.data.hptr.value == 0) {
            return a.data.hptr.value == b.data.hptr.value;
        }
        auto* ah = rt.get_handle(a.data.hptr);
        auto* bh = rt.get_handle(b.data.hptr);
        return ah && bh && *ah == *bh;
    }
    const Type* type = rt.get_type(a.type_id);

    if (!type) {
        return false;
    }

    switch (type->primitive_kind) {
    case PK_int:
        return a.data.ival == b.data.ival;
    case PK_string:
        if (a.data.hptr.value == 0 && b.data.hptr.value == 0) {
            return true;
        }
        if (a.data.hptr.value == 0 || b.data.hptr.value == 0) {
            return false;
        }
        return rt.get_string_view(a.data.hptr) == rt.get_string_view(b.data.hptr);
    default:
        try {
            Value result = rt.execute_binary_op(TokenType::EQEQ, a, b);
            if (result.type_id == rt.bool_type()) {
                return result.data.ival != 0;
            }
        } catch (...) {
        }
        return false;
    }
}

// == Runtime =================================================================
// ============================================================================

const std::type_index Runtime::type_index{typeid(Runtime)};

Runtime::Runtime(MemoryResource* scope)
    : Service(scope)
    , type_table_(scope)
    , arena_(MB(256))
    , scope_(&arena_)
    , resman_{nw::kernel::global_allocator(), &kernel::resman()}
    , gc_{std::make_unique<GarbageCollector>(&heap_, this)}
    , vm_{std::make_unique<VirtualMachine>()}
{
    void* mem = allocator()->allocate(sizeof(Context), alignof(Context));
    diagnostic_context_ = new (mem) Context();
    diagnostic_context_->arena = &arena_;
    diagnostic_context_->scope = &scope_;
    diagnostic_context_->config = diagnostic_config_;
    gc_->set_vm(vm_.get());
}

Runtime::~Runtime() = default;

void Runtime::initialize(nw::kernel::ServiceInitTime time)
{
    if (time == nw::kernel::ServiceInitTime::kernel_start || time == kernel::ServiceInitTime::module_pre_load) {
        // Enable filesystem module loading for stdlib imports.
        add_module_path(std::filesystem::path("stdlib") / "core");

        LOG_F(INFO, "[runtime] Initializing Runtime and registering internal types");
        register_internal_types();
        register_core_prelude(*this);
        register_core_test(*this);
        register_core_string(*this);
        register_core_array(*this);
        register_core_map(*this);
        register_core_math(*this);
        register_core_effects(*this);
        register_core_object(*this);
        register_core_item(*this);
        register_core_creature(*this);
        register_core_area(*this);
        register_core_door(*this);
        register_core_encounter(*this);
        register_core_module(*this);
        register_core_placeable(*this);
        register_core_player(*this);
        register_core_sound(*this);
        register_core_store(*this);
        register_core_trigger(*this);
        register_core_waypoint(*this);
    }
}

void Runtime::register_internal_types()
{
    // Primitive Types - cache IDs for type checking
    void_id_ = type_table_.add({
        .name = nw::kernel::strings().intern("void"),
        .type_kind = TK_primitive,
        .primitive_kind = PK_unspecified,
        .size = 0,
        .alignment = 0,
    });

    int_id_ = type_table_.add({
        .name = nw::kernel::strings().intern("int"),
        .type_kind = TK_primitive,
        .primitive_kind = PK_int,
        .size = sizeof(int32_t),
        .alignment = alignof(int32_t),
    });

    float_id_ = type_table_.add({
        .name = nw::kernel::strings().intern("float"),
        .type_kind = TK_primitive,
        .primitive_kind = PK_float,
        .size = sizeof(float),
        .alignment = alignof(float),
    });

    string_id_ = type_table_.add({
        .name = nw::kernel::strings().intern("string"),
        .type_kind = TK_primitive,
        .primitive_kind = PK_string,
        .size = sizeof(HeapPtr),
        .alignment = alignof(HeapPtr),
    });

    string_backing_id_ = type_table_.add({
        .name = nw::kernel::strings().intern("__string_backing"),
        .type_kind = TK_opaque,
        .size = 0,
    });

    bool_id_ = type_table_.add({
        .name = nw::kernel::strings().intern("bool"),
        .type_kind = TK_primitive,
        .primitive_kind = PK_bool,
        .size = sizeof(bool),
        .alignment = alignof(bool),
    });

    any_id_ = type_table_.add({
        .name = nw::kernel::strings().intern("any"),
        .type_kind = TK_primitive,
        .primitive_kind = PK_unspecified,
        .size = sizeof(Value),
        .alignment = alignof(Value),
    });

    // Vec3 type (3D vector with x, y, z floats)
    vec3_id_ = type_table_.add({
        .name = nw::kernel::strings().intern("vec3"),
        .type_kind = TK_opaque,
        .size = sizeof(glm::vec3),
        .alignment = alignof(glm::vec3),
    });

    // Object handle (reference to engine-owned object)
    object_id_ = type_table_.add({
        .name = nw::kernel::strings().intern("object"),
        .type_kind = TK_opaque,
        .size = sizeof(ObjectHandle),
        .alignment = alignof(ObjectHandle),
    });

    register_object_subtype("Module", ObjectType::module);
    register_object_subtype("Area", ObjectType::area);
    register_object_subtype("Creature", ObjectType::creature);
    register_object_subtype("Item", ObjectType::item);
    register_object_subtype("Trigger", ObjectType::trigger);
    register_object_subtype("Placeable", ObjectType::placeable);
    register_object_subtype("Door", ObjectType::door);
    register_object_subtype("Waypoint", ObjectType::waypoint);
    register_object_subtype("Encounter", ObjectType::encounter);
    register_object_subtype("Store", ObjectType::store);
    register_object_subtype("Sound", ObjectType::sound);
    register_object_subtype("Player", ObjectType::player);

    // Wildcard types for generic native functions
    any_array_id_ = type_table_.add({
        .name = nw::kernel::strings().intern("array!(?)"),
        .type_kind = TK_any_array,
        .size = sizeof(HeapPtr),
        .alignment = alignof(HeapPtr),
    });

    any_map_id_ = type_table_.add({
        .name = nw::kernel::strings().intern("map!(?, ?)"),
        .type_kind = TK_any_map,
        .size = sizeof(HeapPtr),
        .alignment = alignof(HeapPtr),
    });

    LOG_F(INFO, "[runtime] Registered {} types", type_table_.types_.size());

    // Register operators for primitive types
    register_primitive_operators();
}

nlohmann::json Runtime::stats() const
{
    auto retention_to_string = [](CompilerStateRetention retention) -> const char* {
        switch (retention) {
        case CompilerStateRetention::full:
            return "full";
        case CompilerStateRetention::source_map:
            return "source_map";
        case CompilerStateRetention::bytecode_only:
            return "none";
        }
        return "unknown";
    };

    size_t module_functions = 0;
    size_t module_globals = 0;
    size_t ast_discarded_count = 0;
    size_t export_count = 0;
    size_t export_with_decl_count = 0;
    size_t export_with_function_abi_count = 0;
    for (const auto& [_, module] : bytecode_cache_) {
        module_functions += module->functions.size();
        module_globals += module->global_count;
    }
    for (const auto& [_, script] : modules_) {
        if (!script) {
            continue;
        }
        if (script->ast_discarded()) {
            ++ast_discarded_count;
        }
        for (const auto& [_, exp] : script->exports()) {
            ++export_count;
            if (exp.decl) {
                ++export_with_decl_count;
            }
            if (exp.function_abi.has_value()) {
                ++export_with_function_abi_count;
            }
        }
    }

    return {
        {"compiler_state_retention", retention_to_string(compiler_state_retention())},
        {"module_count", modules_.size()},
        {"ast_discarded_module_count", ast_discarded_count},
        {"compiled_module_count", bytecode_cache_.size()},
        {"compiled_function_count", module_functions},
        {"global_slot_count", module_globals},
        {"export_count", export_count},
        {"export_with_decl_count", export_with_decl_count},
        {"export_with_function_abi_count", export_with_function_abi_count},
        {"generic_function_template_count", generic_function_templates_.size()},
        {"instantiated_generic_function_count", instantiation_cache_.size()},
        {"instantiated_generic_type_count", type_instantiation_cache_.size()},
        {"source_map_cache_entries", line_offsets_.size()},
        {"compiler_arena_used_bytes", arena_.used()},
        {"compiler_arena_capacity_bytes", arena_.capacity()},
    };
}

Runtime::CompilerStateRetention Runtime::compiler_state_retention() const noexcept
{
    switch (diagnostic_config_.debug_level) {
    case DebugLevel::full:
        return CompilerStateRetention::full;
    case DebugLevel::source_map:
        return CompilerStateRetention::source_map;
    case DebugLevel::none:
        return CompilerStateRetention::bytecode_only;
    }
    return CompilerStateRetention::source_map;
}

void Runtime::maybe_compact_script_state(Script* script)
{
    if (!script || !script->can_discard_ast()) {
        return;
    }

    auto retention = compiler_state_retention();
    bool should_discard_ast = retention == CompilerStateRetention::bytecode_only
        || (retention == CompilerStateRetention::source_map && script->export_count() == 0);

    if (should_discard_ast) {
        script->discard_ast();
    }

    // Source text must remain alive for any retained AST/decl token views.
    // Only drop source when AST has been compacted.
    if (retention == CompilerStateRetention::bytecode_only && script->ast_discarded()) {
        script->discard_source();
        line_offsets_.erase(String(script->name()));
    }
}

void Runtime::register_generic_function_templates(Script* script)
{
    if (!script) {
        return;
    }

    auto module = String(script->name());
    String module_source(script->text());
    if (module_source.empty()) {
        return;
    }

    bool has_generic_function = false;

    for (auto* node : script->ast().decls) {
        auto* fn = dynamic_cast<FunctionDefinition*>(node);
        if (!fn || !fn->is_generic()) {
            continue;
        }
        has_generic_function = true;

        Runtime::GenericFunctionTemplateKey key{module, fn->identifier()};
        generic_function_templates_[key] = Runtime::GenericFunctionTemplate{
            .type_param_count = static_cast<uint32_t>(fn->type_params.size()),
        };
    }

    if (has_generic_function) {
        generic_template_module_sources_[module] = std::move(module_source);
    } else {
        generic_template_module_sources_.erase(module);
    }
}

const Runtime::GenericFunctionTemplate* Runtime::find_generic_function_template(StringView module, StringView function) const
{
    Runtime::GenericFunctionTemplateKey key{String(module), String(function)};
    auto it = generic_function_templates_.find(key);
    if (it == generic_function_templates_.end()) {
        return nullptr;
    }
    return &it->second;
}

FunctionDefinition* Runtime::materialize_generic_function_template(
    Script* defining_script,
    StringView function_name,
    std::unique_ptr<Script>& template_holder,
    String* error_out)
{
    if (!defining_script) {
        if (error_out) {
            *error_out = "invalid generic template lookup: null defining script";
        }
        return nullptr;
    }

    auto* tpl = find_generic_function_template(defining_script->name(), function_name);
    if (!tpl) {
        return nullptr;
    }

    auto source_it = generic_template_module_sources_.find(String(defining_script->name()));
    if (source_it == generic_template_module_sources_.end()) {
        if (error_out) {
            *error_out = fmt::format(
                "Missing stored source for generic template module '{}'",
                defining_script->name());
        }
        return nullptr;
    }

    String temp_module(defining_script->name());
    template_holder = std::make_unique<Script>(temp_module, source_it->second, defining_script->ctx());
    template_holder->resolve();
    if (template_holder->errors() > 0) {
        if (error_out) {
            *error_out = fmt::format(
                "Failed to resolve stored generic template '{}.{}'",
                defining_script->name(),
                function_name);
        }
        return nullptr;
    }

    for (auto* node : template_holder->ast().decls) {
        auto* fn = dynamic_cast<FunctionDefinition*>(node);
        if (!fn) {
            continue;
        }
        if (fn->identifier() == function_name && fn->is_generic()) {
            return fn;
        }
    }

    if (error_out) {
        *error_out = fmt::format(
            "Stored generic template '{}.{}' missing function declaration",
            defining_script->name(),
            function_name);
    }
    return nullptr;
}

// -- Type System -------------------------------------------------------------

TypeID Runtime::register_type(StringView name, Type type)
{
    type.name = nw::kernel::strings().intern(name);
    return type_table_.add(type);
}

const Type* Runtime::get_type(TypeID id) const
{
    return type_table_.get(id);
}

const Type* Runtime::get_type(StringView name) const
{
    return type_table_.get(name);
}

TypeID Runtime::type_id(StringView name, bool define)
{
    auto it = type_table_.name_to_index_.find(name);
    if (it != type_table_.name_to_index_.end()) {
        return TypeID{it->second};
    }

    if (!define) { return invalid_type_id; }

    // Create new opaque type with this name
    Type new_type{
        .name = nw::kernel::strings().intern(name),
        .type_kind = TK_opaque,
    };

    return type_table_.add(new_type);
}

StringView Runtime::type_name(TypeID id) const
{
    auto* type = type_table_.get(id);
    return type ? type->name.view() : "<unknown>";
}

// -- Type Metadata -----------------------------------------------------------

BraceInitType Runtime::get_init_mode(TypeID id) const
{
    auto it = type_metadata_.find(id);
    if (it != type_metadata_.end()) {
        return it->second.init_mode;
    }

    auto* type = type_table_.get(id);
    if (!type) { return BraceInitType::none; }

    switch (type->type_kind) {
    case TK_array:
    case TK_fixed_array:
        return BraceInitType::list;
    case TK_map:
        return BraceInitType::kv;
    case TK_struct:
        return BraceInitType::field;
    default:
        return BraceInitType::none;
    }
}

void Runtime::set_type_metadata(TypeID id, TypeMetadata metadata)
{
    type_metadata_[id] = metadata;
}

const TypeMetadata* Runtime::get_type_metadata(TypeID id) const
{
    auto it = type_metadata_.find(id);
    if (it != type_metadata_.end()) {
        return &it->second;
    }
    return nullptr;
}

void Runtime::register_operator_alias_info(TypeID type_id, StringView op_name, bool is_explicit)
{
    if (type_id == invalid_type_id) {
        return;
    }

    auto& info = operator_alias_info_[type_id];
    if (op_name == "eq") {
        info.has_eq = true;
        if (is_explicit) { info.has_explicit_eq = true; }
    } else if (op_name == "hash") {
        info.has_hash = true;
        if (is_explicit) { info.has_explicit_hash = true; }
    } else if (op_name == "lt") {
        info.has_lt = true;
        if (is_explicit) { info.has_explicit_lt = true; }
    }
}

Runtime::OperatorAliasInfo Runtime::get_operator_alias_info(TypeID type_id) const
{
    auto it = operator_alias_info_.find(type_id);
    if (it != operator_alias_info_.end()) {
        return it->second;
    }
    return {};
}

// -- Default Structural Operators --------------------------------------------

namespace {

TypeID unwrap_type(const Runtime& rt, TypeID type_id)
{
    while (true) {
        const Type* t = rt.get_type(type_id);
        if (!t) return type_id;
        if (t->type_kind == TK_alias || t->type_kind == TK_newtype) {
            type_id = t->type_params[0].as<TypeID>();
        } else {
            return type_id;
        }
    }
}

template <typename Check>
bool struct_fields_support(const Runtime& rt, TypeID struct_type_id, Check&& check)
{
    const Type* type = rt.get_type(struct_type_id);
    if (!type || type->type_kind != TK_struct) { return false; }
    auto struct_id = type->type_params[0].as<StructID>();
    const StructDef* def = rt.type_table_.get(struct_id);
    if (!def) { return false; }
    for (uint32_t i = 0; i < def->field_count; ++i) {
        if (!check(def->fields[i].type_id)) { return false; }
    }
    return true;
}

template <typename Check>
bool sum_payloads_support(const Runtime& rt, const Type* type, Check&& check)
{
    if (!type || type->type_kind != TK_sum) { return false; }
    auto sum_id = type->type_params[0].as<SumID>();
    const SumDef* def = rt.type_table_.get(sum_id);
    if (!def) { return false; }
    for (uint32_t i = 0; i < def->variant_count; ++i) {
        TypeID pt = def->variants[i].payload_type;
        if (pt != invalid_type_id && !check(pt)) {
            return false;
        }
    }
    return true;
}

enum class DefaultOpKind {
    eq,
    lt,
    hash,
};

bool default_op_precheck(const Runtime& rt, TypeID type_id, DefaultOpKind kind)
{
    auto info = rt.get_operator_alias_info(type_id);
    switch (kind) {
    case DefaultOpKind::eq:
        return info.has_eq;
    case DefaultOpKind::lt:
        return info.has_lt;
    case DefaultOpKind::hash:
        // A type with an explicit eq won't receive default hash — hash must
        // match equality semantics, so the user must define both explicitly.
        if (info.has_explicit_eq) {
            return false;
        }
        return rt.has_hash_op(type_id);
    }
    return false;
}

bool type_supports_default_op(const Runtime& rt, TypeID type_id,
    absl::flat_hash_set<TypeID>& visiting, DefaultOpKind kind)
{
    type_id = unwrap_type(rt, type_id);
    const Type* type = rt.get_type(type_id);
    if (!type) { return false; }

    // Primitive defaults are operator-specific.
    if (type->type_kind == TK_primitive) {
        if (kind == DefaultOpKind::hash && type_id == rt.float_type()) {
            return false;
        }
        return true;
    }

    // Respect explicit/previous registrations.
    if (default_op_precheck(rt, type_id, kind)) { return true; }

    if (!visiting.insert(type_id).second) { return false; } // cycle guard

    bool ok = false;
    if (type->type_kind == TK_struct) {
        ok = struct_fields_support(rt, type_id,
            [&](TypeID field_type) {
                return type_supports_default_op(rt, field_type, visiting, kind);
            });
    } else if (type->type_kind == TK_sum) {
        ok = sum_payloads_support(rt, type,
            [&](TypeID payload_type) {
                return type_supports_default_op(rt, payload_type, visiting, kind);
            });
    } else if (type->type_kind == TK_array) {
        TypeID elem = type->type_params[0].as<TypeID>();
        ok = (elem != invalid_type_id)
            && type_supports_default_op(rt, elem, visiting, kind);
    }

    visiting.erase(type_id);
    return ok;
}

bool type_supports_default_eq(const Runtime& rt, TypeID type_id, absl::flat_hash_set<TypeID>& visiting)
{
    return type_supports_default_op(rt, type_id, visiting, DefaultOpKind::eq);
}

bool type_supports_default_lt(const Runtime& rt, TypeID type_id, absl::flat_hash_set<TypeID>& visiting)
{
    return type_supports_default_op(rt, type_id, visiting, DefaultOpKind::lt);
}

bool type_supports_default_hash(const Runtime& rt, TypeID type_id, absl::flat_hash_set<TypeID>& visiting)
{
    return type_supports_default_op(rt, type_id, visiting, DefaultOpKind::hash);
}

Value default_struct_eq(Runtime& rt, const StructDef* def, const Value& lhs, const Value& rhs)
{
    for (uint32_t i = 0; i < def->field_count; ++i) {
        Value lv = rt.read_struct_value_field(lhs, def, i);
        Value rv = rt.read_struct_value_field(rhs, def, i);
        Value eq_result = rt.execute_binary_op(TokenType::EQEQ, lv, rv);
        if (eq_result.type_id != rt.bool_type() || !eq_result.data.bval) {
            return Value::make_bool(false);
        }
    }
    return Value::make_bool(true);
}

Value default_struct_lt(Runtime& rt, const StructDef* def, const Value& lhs, const Value& rhs)
{
    for (uint32_t i = 0; i < def->field_count; ++i) {
        Value lv = rt.read_struct_value_field(lhs, def, i);
        Value rv = rt.read_struct_value_field(rhs, def, i);
        // Check if lv < rv
        Value lt_result = rt.execute_binary_op(TokenType::LT, lv, rv);
        if (lt_result.type_id == rt.bool_type() && lt_result.data.bval) {
            return Value::make_bool(true);
        }
        // Check if lv > rv (i.e. rv < lv) to break ties
        Value gt_result = rt.execute_binary_op(TokenType::LT, rv, lv);
        if (gt_result.type_id == rt.bool_type() && gt_result.data.bval) {
            return Value::make_bool(false);
        }
        // Fields are equal — continue to next
    }
    return Value::make_bool(false); // All fields equal => not less than
}

Value default_struct_hash(Runtime& rt, const StructDef* def, const Value& val)
{
    XXH3_state_t state;
    XXH3_64bits_reset(&state);
    for (uint32_t i = 0; i < def->field_count; ++i) {
        Value fv = rt.read_struct_value_field(val, def, i);
        Value hash_result = rt.execute_hash_op(fv);
        if (hash_result.type_id == rt.int_type()) {
            int32_t h = hash_result.data.ival;
            XXH3_64bits_update(&state, &h, sizeof(h));
        }
    }
    uint64_t result = XXH3_64bits_digest(&state);
    return Value::make_int(static_cast<int32_t>(static_cast<uint32_t>(result)));
}

// -- Default Sum Operators ---------------------------------------------------

Value default_sum_eq(Runtime& rt, const SumDef* def, const Value& lhs, const Value& rhs)
{
    uint32_t ltag = rt.read_sum_value_tag(lhs, def);
    uint32_t rtag = rt.read_sum_value_tag(rhs, def);
    if (ltag != rtag) { return Value::make_bool(false); }
    if (ltag >= def->variant_count) { return Value::make_bool(false); }
    TypeID pt = def->variants[ltag].payload_type;
    if (pt == invalid_type_id) { return Value::make_bool(true); } // unit variant
    Value lv = rt.read_sum_value_payload(lhs, def, ltag);
    Value rv = rt.read_sum_value_payload(rhs, def, ltag);
    return rt.execute_binary_op(TokenType::EQEQ, lv, rv);
}

Value default_sum_lt(Runtime& rt, const SumDef* def, const Value& lhs, const Value& rhs)
{
    uint32_t ltag = rt.read_sum_value_tag(lhs, def);
    uint32_t rtag = rt.read_sum_value_tag(rhs, def);
    if (ltag != rtag) { return Value::make_bool(ltag < rtag); }
    if (ltag >= def->variant_count) { return Value::make_bool(false); }
    TypeID pt = def->variants[ltag].payload_type;
    if (pt == invalid_type_id) { return Value::make_bool(false); } // same unit variant
    Value lv = rt.read_sum_value_payload(lhs, def, ltag);
    Value rv = rt.read_sum_value_payload(rhs, def, ltag);
    return rt.execute_binary_op(TokenType::LT, lv, rv);
}

Value default_sum_hash(Runtime& rt, const SumDef* def, const Value& val)
{
    XXH3_state_t state;
    XXH3_64bits_reset(&state);
    uint32_t tag = rt.read_sum_value_tag(val, def);
    XXH3_64bits_update(&state, &tag, sizeof(tag));
    if (tag < def->variant_count) {
        TypeID pt = def->variants[tag].payload_type;
        if (pt != invalid_type_id) {
            Value pv = rt.read_sum_value_payload(val, def, tag);
            Value h = rt.execute_hash_op(pv);
            if (h.type_id == rt.int_type()) {
                int32_t hv = h.data.ival;
                XXH3_64bits_update(&state, &hv, sizeof(hv));
            }
        }
    }
    uint64_t result = XXH3_64bits_digest(&state);
    return Value::make_int(static_cast<int32_t>(static_cast<uint32_t>(result)));
}

// -- Default Array Operators -------------------------------------------------

Value default_array_eq(Runtime& rt, const Value& lhs, const Value& rhs)
{
    size_t llen = rt.array_size(lhs.data.hptr);
    size_t rlen = rt.array_size(rhs.data.hptr);
    if (llen != rlen) { return Value::make_bool(false); }
    for (uint32_t i = 0; i < static_cast<uint32_t>(llen); ++i) {
        Value lv, rv;
        if (!rt.array_get(lhs.data.hptr, i, lv) || !rt.array_get(rhs.data.hptr, i, rv)) {
            return Value::make_bool(false);
        }
        Value eq = rt.execute_binary_op(TokenType::EQEQ, lv, rv);
        if (eq.type_id != rt.bool_type() || !eq.data.bval) {
            return Value::make_bool(false);
        }
    }
    return Value::make_bool(true);
}

Value default_array_lt(Runtime& rt, const Value& lhs, const Value& rhs)
{
    size_t llen = rt.array_size(lhs.data.hptr);
    size_t rlen = rt.array_size(rhs.data.hptr);
    uint32_t minlen = static_cast<uint32_t>(llen < rlen ? llen : rlen);
    for (uint32_t i = 0; i < minlen; ++i) {
        Value lv, rv;
        if (!rt.array_get(lhs.data.hptr, i, lv) || !rt.array_get(rhs.data.hptr, i, rv)) {
            break;
        }
        Value lt = rt.execute_binary_op(TokenType::LT, lv, rv);
        if (lt.type_id == rt.bool_type() && lt.data.bval) { return Value::make_bool(true); }
        Value gt = rt.execute_binary_op(TokenType::LT, rv, lv);
        if (gt.type_id == rt.bool_type() && gt.data.bval) { return Value::make_bool(false); }
    }
    return Value::make_bool(llen < rlen); // shorter array is less if prefix matches
}

Value default_array_hash(Runtime& rt, const Value& val)
{
    XXH3_state_t state;
    XXH3_64bits_reset(&state);
    size_t len = rt.array_size(val.data.hptr);
    uint32_t len32 = static_cast<uint32_t>(len);
    XXH3_64bits_update(&state, &len32, sizeof(len32));
    for (uint32_t i = 0; i < len32; ++i) {
        Value elem;
        if (!rt.array_get(val.data.hptr, i, elem)) { break; }
        Value h = rt.execute_hash_op(elem);
        if (h.type_id == rt.int_type()) {
            int32_t hv = h.data.ival;
            XXH3_64bits_update(&state, &hv, sizeof(hv));
        }
    }
    uint64_t result = XXH3_64bits_digest(&state);
    return Value::make_int(static_cast<int32_t>(static_cast<uint32_t>(result)));
}

} // anonymous namespace

void Runtime::register_default_struct_operators()
{
    // Fixpoint: keep iterating until no new defaults are registered.
    // This ensures that if struct A has a field of struct B, B's defaults
    // are registered before A's eligibility is checked.
    bool changed = true;
    while (changed) {
        changed = false;
        const size_t type_count = type_table_.types_.size();
        for (size_t i = 0; i < type_count; ++i) {
            TypeID type_id{static_cast<int32_t>(i + 1)};
            const Type* type = type_table_.get(type_id);
            if (!type) continue;

            auto kind = type->type_kind;
            if (kind != TK_struct && kind != TK_sum && kind != TK_array) continue;

            auto info = get_operator_alias_info(type_id);

            // Default eq
            if (!info.has_eq) {
                absl::flat_hash_set<TypeID> visiting;
                if (type_supports_default_eq(*this, type_id, visiting)) {
                    if (kind == TK_struct) {
                        const StructDef* def = type_table_.get(type->type_params[0].as<StructID>());
                        register_binary_op(TokenType::EQEQ, type_id, type_id, bool_id_,
                            [def](const Value& a, const Value& b) -> Value {
                                return default_struct_eq(nw::kernel::runtime(), def, a, b);
                            });
                    } else if (kind == TK_sum) {
                        const SumDef* def = type_table_.get(type->type_params[0].as<SumID>());
                        register_binary_op(TokenType::EQEQ, type_id, type_id, bool_id_,
                            [def](const Value& a, const Value& b) -> Value {
                                return default_sum_eq(nw::kernel::runtime(), def, a, b);
                            });
                    } else { // TK_array
                        register_binary_op(TokenType::EQEQ, type_id, type_id, bool_id_,
                            [](const Value& a, const Value& b) -> Value {
                                return default_array_eq(nw::kernel::runtime(), a, b);
                            });
                    }
                    register_operator_alias_info(type_id, "eq", false);
                    changed = true;
                }
            }

            info = get_operator_alias_info(type_id);

            // Default lt
            if (!info.has_lt) {
                absl::flat_hash_set<TypeID> visiting;
                if (type_supports_default_lt(*this, type_id, visiting)) {
                    if (kind == TK_struct) {
                        const StructDef* def = type_table_.get(type->type_params[0].as<StructID>());
                        register_binary_op(TokenType::LT, type_id, type_id, bool_id_,
                            [def](const Value& a, const Value& b) -> Value {
                                return default_struct_lt(nw::kernel::runtime(), def, a, b);
                            });
                    } else if (kind == TK_sum) {
                        const SumDef* def = type_table_.get(type->type_params[0].as<SumID>());
                        register_binary_op(TokenType::LT, type_id, type_id, bool_id_,
                            [def](const Value& a, const Value& b) -> Value {
                                return default_sum_lt(nw::kernel::runtime(), def, a, b);
                            });
                    } else { // TK_array
                        register_binary_op(TokenType::LT, type_id, type_id, bool_id_,
                            [](const Value& a, const Value& b) -> Value {
                                return default_array_lt(nw::kernel::runtime(), a, b);
                            });
                    }
                    register_operator_alias_info(type_id, "lt", false);
                    changed = true;
                }
            }

            info = get_operator_alias_info(type_id);

            // Default hash — only when there is no explicit eq.
            if (!info.has_hash && !info.has_explicit_eq) {
                absl::flat_hash_set<TypeID> visiting;
                if (type_supports_default_hash(*this, type_id, visiting)) {
                    if (kind == TK_struct) {
                        const StructDef* def = type_table_.get(type->type_params[0].as<StructID>());
                        register_native_hash_op(type_id,
                            [def](const Value& val) -> Value {
                                return default_struct_hash(nw::kernel::runtime(), def, val);
                            });
                    } else if (kind == TK_sum) {
                        const SumDef* def = type_table_.get(type->type_params[0].as<SumID>());
                        register_native_hash_op(type_id,
                            [def](const Value& val) -> Value {
                                return default_sum_hash(nw::kernel::runtime(), def, val);
                            });
                    } else { // TK_array
                        register_native_hash_op(type_id,
                            [](const Value& val) -> Value {
                                return default_array_hash(nw::kernel::runtime(), val);
                            });
                    }
                    register_operator_alias_info(type_id, "hash", false);
                    changed = true;
                }
            }
        }
    }
}

// -- Type Checking -----------------------------------------------------------

TypeID Runtime::type_check_binary_op(Token op, TypeID lhs, TypeID rhs) const
{
    TypeID is_good{}; // Invalid by default
    bool is_eq = false;

    switch (op.type) {
    default:
        return invalid_type_id;

    case TokenType::EQ:
        if (is_type_convertible(lhs, rhs)) {
            return lhs;
        }
    case TokenType::MODEQ:
    case TokenType::MOD:
        if (lhs == int_id_ && rhs == int_id_) {
            return int_id_;
        }
    case TokenType::PLUS:
    case TokenType::PLUSEQ:
        is_eq = op.type == TokenType::PLUSEQ;
        if ((lhs == int_id_ || lhs == float_id_) && (rhs == int_id_ || rhs == float_id_)) {
            if (lhs == float_id_ || rhs == float_id_) {
                is_good = float_id_;
            } else {
                is_good = int_id_;
            }
        } else if (lhs == vec3_id_ && rhs == vec3_id_) {
            is_good = vec3_id_;
        } else if (lhs == string_id_ && rhs == string_id_) {
            is_good = string_id_;
        }
        break;
    case TokenType::MINUS:
    case TokenType::MINUSEQ:
        is_eq = op.type == TokenType::MINUSEQ;
        if ((lhs == int_id_ || lhs == float_id_) && (rhs == int_id_ || rhs == float_id_)) {
            if (lhs == float_id_ || rhs == float_id_) {
                is_good = float_id_;
            } else {
                is_good = int_id_;
            }
        } else if (lhs == vec3_id_ && rhs == vec3_id_) {
            is_good = vec3_id_;
        }
        break;
    case TokenType::TIMES:
    case TokenType::TIMESEQ:
        is_eq = op.type == TokenType::TIMESEQ;
        if ((lhs == int_id_ || lhs == float_id_) && (rhs == int_id_ || rhs == float_id_)) {
            if (lhs == float_id_ || rhs == float_id_) {
                is_good = float_id_;
            } else {
                is_good = int_id_;
            }
        } else if ((lhs == vec3_id_ || lhs == float_id_) && (rhs == vec3_id_ || rhs == float_id_)) {
            if (lhs == vec3_id_ || rhs == vec3_id_) {
                is_good = vec3_id_;
            } else {
                is_good = float_id_;
            }
        }
        break;
    case TokenType::DIV:
    case TokenType::DIVEQ:
        is_eq = op.type == TokenType::DIVEQ;
        if ((lhs == int_id_ || lhs == float_id_) && (rhs == int_id_ || rhs == float_id_)) {
            if (lhs == float_id_ || rhs == float_id_) {
                is_good = float_id_;
            } else {
                is_good = int_id_;
            }
        } else if (lhs == vec3_id_ && rhs == float_id_) {
            is_good = vec3_id_;
        }
        break;
    case TokenType::ANDAND:
    case TokenType::OROR:
        if (lhs == bool_id_ && rhs == bool_id_) {
            return bool_id_;
        }
        if (lhs == int_id_ && rhs == int_id_) {
            return int_id_;
        }
        if ((lhs == bool_id_ && rhs == int_id_) || (lhs == int_id_ && rhs == bool_id_)) {
            return bool_id_;
        }
        break;

    case TokenType::EQEQ:
    case TokenType::NOTEQ:
    case TokenType::LT:
    case TokenType::LTEQ:
    case TokenType::GT:
    case TokenType::GTEQ:
        if ((op.type == TokenType::EQEQ || op.type == TokenType::NOTEQ)
            && lhs == rhs
            && (is_object_like_type(lhs) || is_handle_type(lhs))) {
            return bool_id_;
        }
        if ((lhs == int_id_ || lhs == float_id_ || lhs == bool_id_)
            && (rhs == int_id_ || rhs == float_id_ || rhs == bool_id_)) {
            return bool_id_;
        }
        if (lhs == string_id_ && rhs == string_id_) {
            return bool_id_;
        }
        break;
    }

    if (is_eq) {
        if (is_type_convertible(lhs, rhs) && is_good != invalid_type_id) {
            return lhs;
        } else {
            return invalid_type_id;
        }
    }

    if (is_good != invalid_type_id) {
        return is_good;
    }

    // Consult script operator registry for user-defined types
    switch (op.type) {
    case TokenType::PLUS:
    case TokenType::MINUS:
    case TokenType::TIMES:
    case TokenType::DIV:
    case TokenType::MOD: {
        auto it = binary_ops_.find(std::make_tuple(op.type, lhs, rhs));
        if (it != binary_ops_.end()) {
            return it->second.result_type;
        }
        break;
    }
    case TokenType::EQEQ:
    case TokenType::NOTEQ: {
        // NOTEQ is synthesized from EQEQ
        auto it = binary_ops_.find(std::make_tuple(TokenType::EQEQ, lhs, rhs));
        if (it != binary_ops_.end()) {
            return bool_id_;
        }
        break;
    }
    case TokenType::LT: {
        auto it = binary_ops_.find(std::make_tuple(TokenType::LT, lhs, rhs));
        if (it != binary_ops_.end()) {
            return bool_id_;
        }
        break;
    }
    case TokenType::GT: {
        // GT is synthesized from LT with swapped args
        auto it = binary_ops_.find(std::make_tuple(TokenType::LT, rhs, lhs));
        if (it != binary_ops_.end()) {
            return bool_id_;
        }
        break;
    }
    case TokenType::LTEQ: {
        // LTEQ is synthesized as (a < b) || (a == b)
        auto lt_it = binary_ops_.find(std::make_tuple(TokenType::LT, lhs, rhs));
        auto eq_it = binary_ops_.find(std::make_tuple(TokenType::EQEQ, lhs, rhs));
        if (lt_it != binary_ops_.end() && eq_it != binary_ops_.end()) {
            return bool_id_;
        }
        break;
    }
    case TokenType::GTEQ: {
        // GTEQ is synthesized as (b < a) || (a == b)
        auto lt_it = binary_ops_.find(std::make_tuple(TokenType::LT, rhs, lhs));
        auto eq_it = binary_ops_.find(std::make_tuple(TokenType::EQEQ, lhs, rhs));
        if (lt_it != binary_ops_.end() && eq_it != binary_ops_.end()) {
            return bool_id_;
        }
        break;
    }
    default:
        break;
    }

    return invalid_type_id;
}

bool Runtime::is_type_convertible(TypeID lhs, TypeID rhs) const
{
    if (lhs == object_id_ && is_object_subtype(rhs)) {
        return true;
    }

    return lhs == rhs
        || (lhs == float_id_ && rhs == int_id_)
        || lhs == any_id_;
}

TypeID Runtime::register_object_subtype(StringView name, nw::ObjectType tag)
{
    TypeID type = type_table_.add({
        .name = nw::kernel::strings().intern(name),
        .type_kind = TK_opaque,
        .size = sizeof(ObjectHandle),
        .alignment = alignof(ObjectHandle),
    });

    object_subtype_tags_[type] = tag;
    object_tag_types_[tag] = type;
    return type;
}

bool Runtime::is_object_subtype(TypeID type) const
{
    return object_subtype_tags_.contains(type);
}

bool Runtime::is_object_like_type(TypeID type) const
{
    return type == object_id_ || is_object_subtype(type);
}

std::optional<nw::ObjectType> Runtime::object_subtype_tag(TypeID type) const
{
    auto it = object_subtype_tags_.find(type);
    if (it == object_subtype_tags_.end()) {
        return std::nullopt;
    }
    return it->second;
}

TypeID Runtime::object_subtype_for_tag(nw::ObjectType tag) const
{
    auto it = object_tag_types_.find(tag);
    if (it == object_tag_types_.end()) {
        return invalid_type_id;
    }
    return it->second;
}

// -- Operator Registry -------------------------------------------------------

void Runtime::register_binary_op(TokenType op, TypeID lhs, TypeID rhs, TypeID result, BinaryOpExecutor executor)
{
    binary_ops_[std::make_tuple(op, lhs, rhs)] = BinaryOpOverload{lhs, rhs, result, std::move(executor)};
}

void Runtime::register_unary_op(TokenType op, TypeID operand, TypeID result, UnaryOpExecutor executor)
{
    unary_ops_[std::make_pair(op, operand)] = UnaryOpOverload{operand, result, std::move(executor)};
}

void Runtime::register_script_binary_op(TokenType op, TypeID lhs, TypeID rhs, TypeID result,
    StringView module_path, StringView function_name)
{
    auto key = std::make_tuple(op, lhs, rhs);

    // Check if operator already registered
    if (binary_ops_.contains(key)) {
        LOG_F(ERROR, "Binary operator already registered for types ({}, {})",
            type_name(lhs), type_name(rhs));
        return;
    }

    // Register with script function reference (no executor yet - requires VM)
    binary_ops_[key] = BinaryOpOverload{
        lhs, rhs, result,
        {}, // No native executor
        ScriptFunctionRef{String(module_path), String(function_name)}};
}

void Runtime::register_script_unary_op(TokenType op, TypeID operand, TypeID result,
    StringView module_path, StringView function_name)
{
    auto key = std::make_pair(op, operand);

    // Check if operator already registered
    if (unary_ops_.contains(key)) {
        LOG_F(ERROR, "Unary operator already registered for type {}", type_name(operand));
        return;
    }

    // Register with script function reference (no executor yet - requires VM)
    unary_ops_[key] = UnaryOpOverload{
        operand, result,
        {}, // No native executor
        ScriptFunctionRef{String(module_path), String(function_name)}};
}

void Runtime::register_str_op(TypeID operand, StringView module_path, StringView function_name)
{
    if (str_ops_.contains(operand)) {
        LOG_F(ERROR, "str operator already registered for type {}", type_name(operand));
        return;
    }
    str_ops_[operand] = ScriptFunctionRef{String(module_path), String(function_name)};
}

void Runtime::register_hash_op(TypeID operand, StringView module_path, StringView function_name)
{
    if (hash_ops_.contains(operand)) {
        LOG_F(ERROR, "hash operator already registered for type {}", type_name(operand));
        return;
    }
    hash_ops_[operand] = ScriptFunctionRef{String(module_path), String(function_name)};
}

void Runtime::register_native_hash_op(TypeID operand, std::function<Value(const Value&)> executor)
{
    if (native_hash_ops_.contains(operand) || hash_ops_.contains(operand)) {
        LOG_F(ERROR, "native hash operator already registered for type {}", type_name(operand));
        return;
    }
    native_hash_ops_[operand] = executor;

    // Register as ExternalFunction so the compiler can emit CALLEXT for hash(x).
    // The synthetic qualified name is "__builtin.hash_typeid_N".
    String func_name = fmt::format("hash_typeid_{}", operand.value);
    String qualified_name = "__builtin." + func_name;

    auto captured = executor;
    ExternalFunction ext;
    ext.qualified_name = nw::kernel::strings().intern(qualified_name);
    ext.metadata.return_type = int_type();
    ext.script_module = nullptr;
    ext.native_wrapper = [captured](Runtime*, const Value* args, uint8_t) -> Value {
        return captured(args[0]);
    };
    register_external_function(std::move(ext));

    // Expose via find_hash_op() so the compiler's existing hash(x) path works.
    hash_ops_[operand] = ScriptFunctionRef{"__builtin", std::move(func_name)};
}

const ScriptFunctionRef* Runtime::find_script_binary_op(TokenType op, TypeID lhs, TypeID rhs) const
{
    auto it = binary_ops_.find(std::make_tuple(op, lhs, rhs));
    if (it != binary_ops_.end() && it->second.script_func) {
        return &*it->second.script_func;
    }
    return nullptr;
}

const ScriptFunctionRef* Runtime::find_script_unary_op(TokenType op, TypeID operand) const
{
    auto it = unary_ops_.find(std::make_pair(op, operand));
    if (it != unary_ops_.end() && it->second.script_func) {
        return &*it->second.script_func;
    }
    return nullptr;
}

const ScriptFunctionRef* Runtime::find_str_op(TypeID type) const
{
    auto it = str_ops_.find(type);
    return it != str_ops_.end() ? &it->second : nullptr;
}

const ScriptFunctionRef* Runtime::find_hash_op(TypeID type) const
{
    auto it = hash_ops_.find(type);
    return it != hash_ops_.end() ? &it->second : nullptr;
}

bool Runtime::has_hash_op(TypeID type) const
{
    return hash_ops_.contains(type) || native_hash_ops_.contains(type);
}

bool Runtime::supports_equality_type(TypeID type) const
{
    if (type == invalid_type_id || type == any_type()) { return true; }
    if (is_object_like_type(type) || is_handle_type(type)) { return true; }

    const Type* t = get_type(type);
    if (!t) { return false; }
    if (t->type_kind == TK_primitive) { return true; }

    return binary_ops_.contains(std::make_tuple(TokenType::EQEQ, type, type));
}

bool Runtime::supports_hash_type(TypeID type) const
{
    if (type == invalid_type_id || type == any_type()) { return true; }
    if (is_object_like_type(type) || is_handle_type(type)) { return true; }

    const Type* t = get_type(type);
    if (!t) { return false; }
    if (t->type_kind == TK_primitive) { return true; }

    return has_hash_op(type);
}

bool Runtime::supports_map_key_type(TypeID type) const
{
    if (type == invalid_type_id || type == any_type() || type == any_array_type() || type == any_map_type()) {
        return true;
    }

    const Type* t = get_type(type);
    if (!t) {
        return true;
    }

    if (t->type_kind == TK_type_param) { return true; }

    while (t && (t->type_kind == TK_newtype || t->type_kind == TK_alias)) {
        if (!t->type_params[0].is<TypeID>()) {
            return false;
        }
        type = t->type_params[0].as<TypeID>();
        t = get_type(type);
    }

    if (!t) {
        return false;
    }

    // Policy: map keys are intentionally narrow.
    return type == int_type() || type == string_type();
}

Value Runtime::execute_binary_op(TokenType op, const Value& lhs, const Value& rhs)
{
    // Synthesize non-foundational comparison operators from LT and EQEQ
    switch (op) {
    case TokenType::GT:
        // a > b → b < a
        return execute_binary_op(TokenType::LT, rhs, lhs);

    case TokenType::GTEQ:
        // a >= b → (b < a) || (a == b)
        {
            auto lt_result = execute_binary_op(TokenType::LT, rhs, lhs);
            auto eq_result = execute_binary_op(TokenType::EQEQ, lhs, rhs);
            if (lt_result.type_id == bool_id_ && eq_result.type_id == bool_id_) {
                return Value::make_bool(lt_result.data.bval || eq_result.data.bval);
            }
            return Value{invalid_type_id};
        }

    case TokenType::NOTEQ:
        // a != b → !(a == b)
        {
            auto result = execute_binary_op(TokenType::EQEQ, lhs, rhs);
            if (result.type_id == bool_id_) {
                return Value::make_bool(!result.data.bval);
            }
            return Value{invalid_type_id};
        }

    case TokenType::LTEQ:
        // a <= b → (a < b) || (a == b)
        {
            auto lt_result = execute_binary_op(TokenType::LT, lhs, rhs);
            auto eq_result = execute_binary_op(TokenType::EQEQ, lhs, rhs);
            if (lt_result.type_id == bool_id_ && eq_result.type_id == bool_id_) {
                return Value::make_bool(lt_result.data.bval || eq_result.data.bval);
            }
            return Value{invalid_type_id};
        }

    default:
        break;
    }

    // Coerce int<->bool for comparisons
    Value coerced_lhs = lhs;
    Value coerced_rhs = rhs;
    if (lhs.type_id == int_id_ && rhs.type_id == bool_id_) {
        coerced_lhs = Value::make_bool(lhs.data.ival != 0);
    } else if (lhs.type_id == bool_id_ && rhs.type_id == int_id_) {
        coerced_rhs = Value::make_bool(rhs.data.ival != 0);
    }

    auto it = binary_ops_.find(std::make_tuple(op, coerced_lhs.type_id, coerced_rhs.type_id));
    if (it != binary_ops_.end()) {
        const auto& overload = it->second;

        // Try native executor first
        if (overload.execute) {
            return overload.execute(coerced_lhs, coerced_rhs);
        }

        if (overload.script_func) {
            Script* mod = load_module(overload.script_func->module_path);
            if (mod) {
                BytecodeModule* bmod = get_or_compile_module(mod);
                if (bmod) {
                    return vm_->execute(bmod, overload.script_func->function_name,
                        {coerced_lhs, coerced_rhs}, default_gas_limit);
                }
            }
            return Value{invalid_type_id};
        }
    }

    if (op == TokenType::EQEQ) {
        if (is_object_like_type(coerced_lhs.type_id) && is_object_like_type(coerced_rhs.type_id)) {
            return Value::make_bool(coerced_lhs.data.oval == coerced_rhs.data.oval);
        }

        if (coerced_lhs.type_id != coerced_rhs.type_id) {
            return Value{invalid_type_id};
        }

        if (is_handle_type(coerced_lhs.type_id)) {
            if (coerced_lhs.storage != ValueStorage::heap || coerced_rhs.storage != ValueStorage::heap) {
                return Value::make_bool(false);
            }
            if (coerced_lhs.data.hptr.value == 0 || coerced_rhs.data.hptr.value == 0) {
                return Value::make_bool(coerced_lhs.data.hptr.value == coerced_rhs.data.hptr.value);
            }
            auto* lhs_handle = get_handle(coerced_lhs.data.hptr);
            auto* rhs_handle = get_handle(coerced_rhs.data.hptr);
            return Value::make_bool(lhs_handle && rhs_handle && *lhs_handle == *rhs_handle);
        }
    }

    return Value{invalid_type_id};
}

Value Runtime::execute_unary_op(TokenType op, const Value& val)
{
    if (op == TokenType::NOT) {
        const Type* type = get_type(val.type_id);
        if (type && type->type_kind == TK_function) {
            bool has_closure = val.storage == ValueStorage::heap && val.data.hptr.value != 0;
            return Value::make_bool(!has_closure);
        }
    }

    auto it = unary_ops_.find(std::make_pair(op, val.type_id));
    if (it != unary_ops_.end()) {
        const auto& overload = it->second;
        if (overload.execute) { return overload.execute(val); }

        if (overload.script_func) {
            Script* mod = load_module(overload.script_func->module_path);
            if (mod) {
                BytecodeModule* bmod = get_or_compile_module(mod);
                if (bmod) {
                    return vm_->execute(bmod, overload.script_func->function_name, {val}, default_gas_limit);
                }
            }
            return {};
        }
    }
    return {};
}

Value Runtime::execute_str_op(const Value& val)
{
    auto it = str_ops_.find(val.type_id);
    if (it != str_ops_.end()) {
        Script* mod = load_module(it->second.module_path);
        if (mod) {
            BytecodeModule* bmod = get_or_compile_module(mod);
            if (bmod) {
                return vm_->execute(bmod, it->second.function_name, {val}, default_gas_limit);
            }
        }
    }
    return {};
}

Value Runtime::execute_hash_op(const Value& val)
{
    if (is_object_like_type(val.type_id)) {
        return Value::make_int(static_cast<int32_t>(
            static_cast<uint32_t>(absl::HashOf(val.data.oval.to_ull()))));
    }
    if (is_handle_type(val.type_id) && val.storage == ValueStorage::heap && val.data.hptr.value != 0) {
        if (auto* handle = get_handle(val.data.hptr)) {
            return Value::make_int(static_cast<int32_t>(
                static_cast<uint32_t>(absl::HashOf(handle->to_ull()))));
        }
    }

    // Handle primitive types inline (they don't need registered hash ops)
    const Type* type = get_type(val.type_id);
    if (type && type->type_kind == TK_primitive) {
        switch (type->primitive_kind) {
        case PK_int:
        case PK_bool:
            return Value::make_int(static_cast<int32_t>(
                static_cast<uint32_t>(absl::HashOf(val.data.ival))));
        case PK_float:
            return Value::make_int(static_cast<int32_t>(
                static_cast<uint32_t>(absl::HashOf(val.data.fval))));
        case PK_string:
            if (val.data.hptr.value != 0) {
                return Value::make_int(static_cast<int32_t>(
                    static_cast<uint32_t>(absl::HashOf(get_string_view(val.data.hptr)))));
            }
            return Value::make_int(0);
        default:
            break;
        }
    }
    // Try native executor (default structural hash is registered here)
    auto native_it = native_hash_ops_.find(val.type_id);
    if (native_it != native_hash_ops_.end()) {
        return native_it->second(val);
    }
    // Fall back to script-defined hash
    auto it = hash_ops_.find(val.type_id);
    if (it != hash_ops_.end()) {
        Script* mod = load_module(it->second.module_path);
        if (mod) {
            BytecodeModule* bmod = get_or_compile_module(mod);
            if (bmod) {
                return vm_->execute(bmod, it->second.function_name, {val}, default_gas_limit);
            }
        }
    }
    return {};
}

ExecutionResult Runtime::execute_script(Script* script, StringView function_name, const Vector<Value>& args,
    uint64_t gas_limit)
{
    BytecodeModule* module = get_or_compile_module(script);
    if (!module) {
        LOG_F(ERROR, "[runtime] Failed to get bytecode for script '{}'", script->name());
        return ExecutionResult{
            .value = Value{},
            .failed = true,
            .error_message = fmt::format("Failed to get bytecode for script '{}'", script->name()),
            .stack_trace = {},
            .error_module = {},
            .error_location = {},
            .error_snippet = {}};
    }

    Value result = vm_->execute(module, function_name, args, gas_limit);

    if (vm_->failed()) {
        ExecutionResult exec_result{
            .value = Value{},
            .failed = true,
            .error_message = String(vm_->error_message()),
            .stack_trace = vm_->get_stack_trace(),
            .error_module = {},
            .error_location = {},
            .error_snippet = {}};

        SourceLocation loc;
        StringView module_name;
        if (vm_->get_top_frame_location(loc, module_name)) {
            exec_result.error_location = loc;
            exec_result.error_module = String(module_name);
            StringView line_view;
            if (get_source_line(module_name, loc.range.start.line, line_view)) {
                exec_result.error_snippet = String(line_view);
            }
        }
        vm_->reset();
        return exec_result;
    }

    return ExecutionResult{
        .value = result,
        .failed = false,
        .error_message = {},
        .stack_trace = {},
        .error_module = {},
        .error_location = {},
        .error_snippet = {}};
}

ExecutionResult Runtime::execute_script(StringView path, StringView function_name, const Vector<Value>& args,
    uint64_t gas_limit)
{
    Script* script = get_module(path);
    if (!script) {
        LOG_F(ERROR, "[runtime] Failed to find or load module '{}'", path);
        return ExecutionResult{
            .value = Value{},
            .failed = true,
            .error_message = fmt::format("Failed to find or load module '{}'", path),
            .stack_trace = {},
            .error_module = {},
            .error_location = {},
            .error_snippet = {}};
    }
    return execute_script(script, function_name, args, gas_limit);
}

void Runtime::reset_error()
{
    vm_->reset();
}

void Runtime::fail(StringView msg)
{
    vm_->fail(msg);
}

namespace {

Vector<size_t> build_line_offsets(StringView text)
{
    Vector<size_t> offsets;
    offsets.push_back(0);
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            offsets.push_back(i + 1);
        }
    }
    return offsets;
}

} // namespace

bool Runtime::get_source_line(StringView module_name, size_t line, StringView& out_line)
{
    if (diagnostic_config_.debug_level == DebugLevel::none) {
        return false;
    }
    if (line == 0) {
        return false;
    }

    auto script_it = modules_.find(String(module_name));
    if (script_it == modules_.end() || !script_it->second) {
        return false;
    }

    Script* script = script_it->second;
    StringView text = script->text();

    auto offsets_it = line_offsets_.find(String(module_name));
    if (offsets_it == line_offsets_.end()) {
        auto offsets = build_line_offsets(text);
        offsets_it = line_offsets_.insert({String(module_name), std::move(offsets)}).first;
    }

    const auto& offsets = offsets_it->second;
    if (line > offsets.size()) {
        return false;
    }

    size_t start = offsets[line - 1];
    size_t end = text.size();
    if (line < offsets.size()) {
        end = offsets[line] - 1;
    }

    if (end < start || start >= text.size()) {
        return false;
    }

    if (end > start && text[end - 1] == '\r') {
        end -= 1;
    }

    out_line = text.substr(start, end - start);
    return true;
}

BytecodeModule* Runtime::get_or_compile_module(Script* script)
{
    auto it = bytecode_cache_.find(script);
    if (it != bytecode_cache_.end()) {
        return it->second.get();
    }

    // Ensure script is parsed and resolved
    // Note: resolve() calls parse() if needed
    script->resolve();
    register_generic_function_templates(script);

    // Ensure imported modules are compiled first so their exported functions are
    // registered as external functions and can be resolved by this module.
    for (const auto& dep : script->dependencies()) {
        if (dep.empty()) {
            continue;
        }
        Script* dep_script = get_module(dep);
        if (!dep_script) {
            continue;
        }
        (void)get_or_compile_module(dep_script);
    }

    if (script->errors() > 0) {
        LOG_F(ERROR, "[runtime] Script '{}' has errors, cannot compile", script->name());
        return nullptr;
    }

    // Cache module early to support same-module generic instantiation during compile.
    auto module_uptr = std::make_unique<BytecodeModule>(String(script->name()));
    BytecodeModule* module = module_uptr.get();
    bytecode_cache_[script] = std::move(module_uptr);

    AstCompiler compiler(script, module, this, diagnostic_context_);

    if (!compiler.compile()) {
        LOG_F(ERROR, "[runtime] Compilation failed for '{}': {}", script->name(), compiler.error_message_);
        bytecode_cache_.erase(script);
        return nullptr;
    }

    // Register this module's script functions as external functions
    // so other modules can call them
    for (uint32_t i = 0; i < module->functions.size(); ++i) {
        const CompiledFunction* func = module->functions[i];
        String qualified_name = String(script->name()) + "." + func->name;

        // Skip if already registered (e.g., native functions)
        if (find_external_function(qualified_name) != UINT32_MAX) {
            continue;
        }

        ExternalFunction ext;
        ext.qualified_name = nw::kernel::strings().intern(qualified_name);
        ext.script_module = module;
        ext.func_idx = i;
        register_external_function(std::move(ext));
    }

    // Resolve external function references
    if (!module->resolve_external_refs(this)) {
        LOG_F(ERROR, "[runtime] Failed to resolve external refs for '{}'", script->name());
        bytecode_cache_.erase(script);
        return nullptr;
    }

    // Allocate module globals storage
    if (module->global_count > 0) {
        module->globals.resize(module->global_count);
    }

    // Auto-run __init if present
    if (module->global_count > 0) {
        vm_->execute(module, "__init", {}, default_gas_limit);
    }

    maybe_compact_script_state(script);

    return module;
}

void Runtime::enumerate_module_globals(GCRootVisitor& visitor)
{
    for (auto& [script, module] : bytecode_cache_) {
        for (uint32_t i = 0; i < module->global_count; ++i) {
            if (module->globals[i].storage == ValueStorage::heap && module->globals[i].data.hptr.value != 0) {
                visitor.visit_root(&module->globals[i].data.hptr);
            }
        }
    }

    for (auto& root : config_roots_) {
        if (root.value != 0) {
            visitor.visit_root(&root);
        }
    }
}

void Runtime::register_primitive_operators()
{
// Helper macros for common patterns
#define REG_BIN_OP_II(op, expr)                                  \
    register_binary_op(TokenType::op, int_id_, int_id_, int_id_, \
        [](const Value& l, const Value& r) { return Value::make_int(expr); })

#define REG_BIN_OP_FF(op, expr)                                        \
    register_binary_op(TokenType::op, float_id_, float_id_, float_id_, \
        [](const Value& l, const Value& r) { return Value::make_float(expr); })

#define REG_BIN_OP_IF(op, expr)                                      \
    register_binary_op(TokenType::op, int_id_, float_id_, float_id_, \
        [](const Value& l, const Value& r) { return Value::make_float(expr); })

#define REG_BIN_OP_FI(op, expr)                                      \
    register_binary_op(TokenType::op, float_id_, int_id_, float_id_, \
        [](const Value& l, const Value& r) { return Value::make_float(expr); })

#define REG_BIN_OP_BB(op, expr)                                     \
    register_binary_op(TokenType::op, bool_id_, bool_id_, bool_id_, \
        [](const Value& l, const Value& r) { return Value::make_bool(expr); })

#define REG_CMP_OP_II(op, expr)                                   \
    register_binary_op(TokenType::op, int_id_, int_id_, bool_id_, \
        [](const Value& l, const Value& r) { return Value::make_bool(expr); })

#define REG_CMP_OP_FF(op, expr)                                       \
    register_binary_op(TokenType::op, float_id_, float_id_, bool_id_, \
        [](const Value& l, const Value& r) { return Value::make_bool(expr); })

#define REG_CMP_OP_IF(op, expr)                                     \
    register_binary_op(TokenType::op, int_id_, float_id_, bool_id_, \
        [](const Value& l, const Value& r) { return Value::make_bool(expr); })

#define REG_CMP_OP_FI(op, expr)                                     \
    register_binary_op(TokenType::op, float_id_, int_id_, bool_id_, \
        [](const Value& l, const Value& r) { return Value::make_bool(expr); })

    // Arithmetic: +, -, *, /, %
    REG_BIN_OP_II(PLUS, l.data.ival + r.data.ival);
    REG_BIN_OP_FF(PLUS, l.data.fval + r.data.fval);
    REG_BIN_OP_IF(PLUS, static_cast<float>(l.data.ival) + r.data.fval);
    REG_BIN_OP_FI(PLUS, l.data.fval + static_cast<float>(r.data.ival));

    REG_BIN_OP_II(MINUS, l.data.ival - r.data.ival);
    REG_BIN_OP_FF(MINUS, l.data.fval - r.data.fval);
    REG_BIN_OP_IF(MINUS, static_cast<float>(l.data.ival) - r.data.fval);
    REG_BIN_OP_FI(MINUS, l.data.fval - static_cast<float>(r.data.ival));

    REG_BIN_OP_II(TIMES, l.data.ival * r.data.ival);
    REG_BIN_OP_FF(TIMES, l.data.fval * r.data.fval);
    REG_BIN_OP_IF(TIMES, static_cast<float>(l.data.ival) * r.data.fval);
    REG_BIN_OP_FI(TIMES, l.data.fval * static_cast<float>(r.data.ival));

    // Division with zero checks
    register_binary_op(TokenType::DIV, int_id_, int_id_, int_id_,
        [](const Value& l, const Value& r) {
            if (r.data.ival == 0) return Value{}; // Return invalid value on division by zero
            return Value::make_int(l.data.ival / r.data.ival);
        });
    register_binary_op(TokenType::DIV, float_id_, float_id_, float_id_,
        [](const Value& l, const Value& r) {
            if (r.data.fval == 0.0f) return Value{}; // Return invalid value on division by zero
            return Value::make_float(l.data.fval / r.data.fval);
        });
    register_binary_op(TokenType::DIV, int_id_, float_id_, float_id_,
        [](const Value& l, const Value& r) {
            if (r.data.fval == 0.0f) return Value{}; // Return invalid value on division by zero
            return Value::make_float(static_cast<float>(l.data.ival) / r.data.fval);
        });
    register_binary_op(TokenType::DIV, float_id_, int_id_, float_id_,
        [](const Value& l, const Value& r) {
            if (r.data.ival == 0) return Value{}; // Return invalid value on division by zero
            return Value::make_float(l.data.fval / static_cast<float>(r.data.ival));
        });

    // Modulo with zero check (integers only)
    register_binary_op(TokenType::MOD, int_id_, int_id_, int_id_,
        [](const Value& l, const Value& r) {
            if (r.data.ival == 0) return Value{}; // Return invalid value on modulo by zero
            return Value::make_int(l.data.ival % r.data.ival);
        });

    // Comparison: < and == (foundational - other comparisons derived from these)
    REG_CMP_OP_II(LT, l.data.ival < r.data.ival);
    REG_CMP_OP_FF(LT, l.data.fval < r.data.fval);
    REG_CMP_OP_IF(LT, static_cast<float>(l.data.ival) < r.data.fval);
    REG_CMP_OP_FI(LT, l.data.fval < static_cast<float>(r.data.ival));

    REG_CMP_OP_II(EQEQ, l.data.ival == r.data.ival);
    REG_CMP_OP_FF(EQEQ, l.data.fval == r.data.fval);
    REG_CMP_OP_IF(EQEQ, static_cast<float>(l.data.ival) == r.data.fval);
    REG_CMP_OP_FI(EQEQ, l.data.fval == static_cast<float>(r.data.ival));

    register_binary_op(TokenType::EQEQ, bool_id_, bool_id_, bool_id_,
        [](const Value& l, const Value& r) { return Value::make_bool(l.data.bval == r.data.bval); });
    register_binary_op(TokenType::LT, bool_id_, bool_id_, bool_id_,
        [](const Value& l, const Value& r) { return Value::make_bool(l.data.bval < r.data.bval); });

    register_binary_op(TokenType::EQEQ, string_id_, string_id_, bool_id_,
        [](const Value& l, const Value& r) {
            auto& rt = nw::kernel::runtime();
            if (l.data.hptr.value == 0 || r.data.hptr.value == 0) {
                return Value{invalid_type_id};
            }
            return Value::make_bool(rt.get_string_view(l.data.hptr) == rt.get_string_view(r.data.hptr));
        });
    register_binary_op(TokenType::LT, string_id_, string_id_, bool_id_,
        [](const Value& l, const Value& r) {
            auto& rt = nw::kernel::runtime();
            if (l.data.hptr.value == 0 || r.data.hptr.value == 0) {
                return Value{invalid_type_id};
            }
            return Value::make_bool(rt.get_string_view(l.data.hptr) < rt.get_string_view(r.data.hptr));
        });

    // Logical: &&, ||
    REG_BIN_OP_II(ANDAND, l.data.ival && r.data.ival);
    REG_BIN_OP_II(OROR, l.data.ival || r.data.ival);
    REG_BIN_OP_BB(ANDAND, l.data.bval && r.data.bval);
    REG_BIN_OP_BB(OROR, l.data.bval || r.data.bval);

    // Mixed int/bool for logical operators (promote to bool)
    register_binary_op(TokenType::ANDAND, int_id_, bool_id_, bool_id_,
        [](const Value& l, const Value& r) { return Value::make_bool((l.data.ival != 0) && r.data.bval); });
    register_binary_op(TokenType::ANDAND, bool_id_, int_id_, bool_id_,
        [](const Value& l, const Value& r) { return Value::make_bool(l.data.bval && (r.data.ival != 0)); });
    register_binary_op(TokenType::OROR, int_id_, bool_id_, bool_id_,
        [](const Value& l, const Value& r) { return Value::make_bool((l.data.ival != 0) || r.data.bval); });
    register_binary_op(TokenType::OROR, bool_id_, int_id_, bool_id_,
        [](const Value& l, const Value& r) { return Value::make_bool(l.data.bval || (r.data.ival != 0)); });

    // String concatenation
    register_binary_op(TokenType::PLUS, string_id_, string_id_, string_id_,
        [](const Value& l, const Value& r) {
            auto& rt = nw::kernel::runtime();
            if (l.data.hptr.value == 0 || r.data.hptr.value == 0) {
                return Value{invalid_type_id};
            }
            StringView lhs_view = rt.get_string_view(l.data.hptr);
            StringView rhs_view = rt.get_string_view(r.data.hptr);
            String result = String(lhs_view) + String(rhs_view);
            HeapPtr result_ptr = rt.alloc_string(result);
            return Value::make_string(result_ptr);
        });

    // Unary operators
    register_unary_op(TokenType::MINUS, int_id_, int_id_,
        [](const Value& v) { return Value::make_int(-v.data.ival); });
    register_unary_op(TokenType::MINUS, float_id_, float_id_,
        [](const Value& v) { return Value::make_float(-v.data.fval); });

    register_unary_op(TokenType::NOT, int_id_, int_id_,
        [](const Value& v) { return Value::make_int(!v.data.ival); });
    register_unary_op(TokenType::NOT, bool_id_, bool_id_,
        [](const Value& v) { return Value::make_bool(!v.data.bval); });

    register_unary_op(TokenType::PLUS, int_id_, int_id_,
        [](const Value& v) { return v; });
    register_unary_op(TokenType::PLUS, float_id_, float_id_,
        [](const Value& v) { return v; });

#undef REG_BIN_OP_II
#undef REG_BIN_OP_FF
#undef REG_BIN_OP_IF
#undef REG_BIN_OP_FI
#undef REG_BIN_OP_BB
#undef REG_CMP_OP_II
#undef REG_CMP_OP_FF
#undef REG_CMP_OP_IF
#undef REG_CMP_OP_FI

    LOG_F(INFO, "[runtime] Registered {} binary ops, {} unary ops", binary_ops_.size(), unary_ops_.size());
}

// -- Module System -----------------------------------------------------------

String Runtime::normalize_module_name(StringView path)
{
    String path_str(path);
    for (char& c : path_str) {
        if (c == '/' || c == '\\') {
            c = '.';
        }
    }
    return path_str;
}

ModuleBuilder Runtime::module(StringView path)
{
    return ModuleBuilder(this, path);
}

Script* Runtime::load_module(StringView path)
{
    String path_str = normalize_module_name(path);

    auto it = modules_.find(path_str);
    if (it != modules_.end()) {
        return it->second;
    }

    // Check for circular dependency
    for (const auto& loading : loading_stack_) {
        if (loading == path_str) {
            String cycle_path;
            for (const auto& mod : loading_stack_) {
                if (!cycle_path.empty()) cycle_path += " -> ";
                cycle_path += mod;
            }
            cycle_path += " -> " + path_str;
            LOG_F(ERROR, "[runtime] Circular dependency detected: {}", cycle_path);
            return nullptr;
        }
    }

    // Add to loading stack
    loading_stack_.push_back(path_str);

    // LOG_F(INFO, "[runtime] Loading module: {}", path);

    // Build registry if we've added module paths since last build
    if (resman_needs_build_) {
        resman_.build_registry();
        resman_needs_build_ = false;
    }

    // Convert module name to resource path (dots to slashes)
    auto resource_path = module_name_to_path(path_str, "");
    String resref_str = path_to_string(resource_path);

    // Load via resman (includes module paths, falls back to kernel resman)
    auto data = resman_.demand({Resref(resref_str), ResourceType::smalls});

    if (!data.bytes.size()) {
        LOG_F(ERROR, "[runtime] Failed to load module: {}", path);
        loading_stack_.pop_back();
        return nullptr;
    }

    // Create and parse script using runtime allocator
    void* mem = allocator()->allocate(sizeof(Script), alignof(Script));
    auto* script = new (mem) Script(std::move(data), diagnostic_context_);
    script->parse();
    script->resolve();

    modules_.emplace(path_str, script);

    // Remove from loading stack
    loading_stack_.pop_back();

    return script;
}

Script* Runtime::load_module_from_source(StringView path, StringView source)
{
    String path_str = normalize_module_name(path);

    // Check if already loaded
    auto it = modules_.find(path_str);
    if (it != modules_.end()) {
        return it->second;
    }

    // Check for circular dependency
    for (const auto& loading : loading_stack_) {
        if (loading == path_str) {
            String cycle_path;
            for (const auto& mod : loading_stack_) {
                if (!cycle_path.empty()) cycle_path += " -> ";
                cycle_path += mod;
            }
            cycle_path += " -> " + path_str;
            LOG_F(ERROR, "[runtime] Circular dependency detected: {}", cycle_path);
            return nullptr;
        }
    }

    // Add to loading stack
    loading_stack_.push_back(path_str);

    LOG_F(INFO, "[runtime] Loading module from source: {}", path_str);

    // Create and parse script from source using runtime allocator
    void* mem = allocator()->allocate(sizeof(Script), alignof(Script));
    auto* script = new (mem) Script(path_str, source, diagnostic_context_);
    script->parse();
    script->resolve();

    modules_.emplace(path_str, script);

    // Remove from loading stack
    loading_stack_.pop_back();

    return script;
}

Script* Runtime::get_module(StringView path)
{
    String path_str = normalize_module_name(path);
    auto it = modules_.find(path_str);
    if (it != modules_.end()) {
        return it->second;
    }
    return load_module(path);
}

String Runtime::path_to_module_name(const std::filesystem::path& filepath,
    const std::filesystem::path& base_path) const
{
    namespace fs = std::filesystem;

    fs::path relative = filepath;
    if (!base_path.empty()) {
        std::error_code ec;
        relative = fs::relative(filepath, base_path, ec);
        if (ec) {
            return {};
        }
    }

    String stem = path_to_string(relative.replace_extension());
    if (stem.empty()) {
        return {};
    }

    for (char& c : stem) {
        if (c == '/' || c == '\\') {
            c = '.';
        }
    }

    return stem;
}

std::filesystem::path Runtime::module_name_to_path(StringView module_name,
    StringView extension) const
{
    String path_str(module_name);

    for (char& c : path_str) {
        if (c == '.') {
            c = '/';
        }
    }

    path_str += extension;
    return std::filesystem::path(path_str);
}

void Runtime::add_module_path(const std::filesystem::path& path)
{
    namespace fs = std::filesystem;
    if (fs::exists(path) && fs::is_directory(path)) {
        fs::path canonical = fs::canonical(path);
        for (const auto& existing : module_paths_) {
            if (existing == canonical) {
                return;
            }
        }

        module_paths_.push_back(canonical);
        void* ptr = allocator()->allocate(sizeof(StaticDirectory), alignof(StaticDirectory));
        resman_.add_custom_container(new (ptr) StaticDirectory(path, allocator()));
        resman_needs_build_ = true;
        LOG_F(INFO, "[runtime] Added module path: {}", path_to_string(path));
    } else {
        LOG_F(WARNING, "[runtime] Module path does not exist or is not a directory: {}", path_to_string(path));
    }
}

Script* Runtime::core_prelude()
{
    if (!core_prelude_) {
        for (const auto& loading : loading_stack_) {
            if (loading == "core.prelude") {
                return nullptr;
            }
        }
        core_prelude_ = load_module("core.prelude");
    }
    return core_prelude_;
}

Script* Runtime::core_test()
{
    if (!core_test_) {
        for (const auto& loading : loading_stack_) {
            if (loading == "core.test") {
                return nullptr;
            }
        }
        core_test_ = load_module("core.test");
    }
    return core_test_;
}

void Runtime::record_test_result(StringView name, bool passed)
{
    test_count_ += 1;
    if (!passed) {
        test_failures_ += 1;
        fmt::print(stderr, "  FAIL: {}\n", name);
    } else {
        fmt::print("  PASS: {}\n", name);
    }
}

void Runtime::reset_test_state()
{
    test_count_ = 0;
    test_failures_ = 0;
}

void Runtime::report_test_summary()
{
    fmt::print("  Tests: {} passed, {} failed, {} total\n",
        test_count_ - test_failures_, test_failures_, test_count_);
    if (test_failures_ > 0) {
        fail("test failures");
    }
}

// Core stdlib native registration lives in lib/nw/smalls/native/*.

const ModuleInterface* Runtime::get_native_module(StringView module_path) const
{
    String normalized(module_path);
    for (char& c : normalized) {
        if (c == '/' || c == '\\') {
            c = '.';
        }
    }

    for (const auto& interface : native_interfaces_) {
        if (interface.module_path == normalized) {
            return &interface;
        }
    }
    return nullptr;
}

uint32_t Runtime::register_native_function(NativeFunction func)
{
    uint32_t native_idx = static_cast<uint32_t>(native_functions_.size());
    native_function_names_[func.name] = native_idx;

    // Also register as external function for unified CALLEXT
    ExternalFunction ext;
    ext.qualified_name = nw::kernel::strings().intern(func.name);
    ext.metadata = func.metadata;
    ext.script_module = nullptr; // marks as native
    ext.func_idx = native_idx;
    ext.native_wrapper = func.wrapper;
    register_external_function(std::move(ext));

    native_functions_.push_back(std::move(func));
    return native_idx;
}

const NativeFunction* Runtime::get_native_function(uint32_t index) const
{
    if (index >= native_functions_.size()) {
        return nullptr;
    }
    return &native_functions_[index];
}

uint32_t Runtime::find_native_function(StringView name) const
{
    auto it = native_function_names_.find(name);
    if (it != native_function_names_.end()) {
        return it->second;
    }
    return UINT32_MAX;
}

// -- External Function Registry ----------------------------------------------

uint32_t Runtime::register_external_function(ExternalFunction func)
{
    auto it = external_function_names_.find(func.qualified_name);
    if (it != external_function_names_.end()) {
        return it->second;
    }
    uint32_t index = static_cast<uint32_t>(external_functions_.size());
    external_function_names_[func.qualified_name] = index;
    external_functions_.push_back(std::move(func));
    return index;
}

const ExternalFunction* Runtime::get_external_function(uint32_t index) const
{
    if (index >= external_functions_.size()) {
        return nullptr;
    }
    return &external_functions_[index];
}

uint32_t Runtime::find_external_function(StringView qualified_name) const
{
    InternedString interned = nw::kernel::strings().intern(qualified_name);
    auto it = external_function_names_.find(interned);
    if (it != external_function_names_.end()) {
        return it->second;
    }

    // Native generics are referenced with a mangled suffix (e.g. core.array.map!(int,int)).
    // Allow resolving to the base generic implementation (core.array.map) when a concrete
    // specialization is not registered.
    size_t generic_pos = qualified_name.find("!(");
    if (generic_pos != StringView::npos) {
        StringView base_name = qualified_name.substr(0, generic_pos);
        InternedString base_interned = nw::kernel::strings().intern(base_name);
        auto bit = external_function_names_.find(base_interned);
        if (bit != external_function_names_.end()) {
            return bit->second;
        }
    }
    return UINT32_MAX;
}

// -- Script Heap -------------------------------------------------------------

HeapPtr Runtime::alloc_string(StringView str)
{
    HeapPtr backing = heap_.allocate(str.size(), 1, string_backing_id_);
    char* data = static_cast<char*>(heap_.get_ptr(backing));
    memcpy(data, str.data(), str.size());

    HeapPtr repr = heap_.allocate(sizeof(StringRepr), alignof(StringRepr), string_id_);
    StringRepr* sr = static_cast<StringRepr*>(heap_.get_ptr(repr));
    sr->backing = backing;
    sr->offset = 0;
    sr->length = static_cast<uint32_t>(str.size());

    return repr;
}

HeapPtr Runtime::alloc_string_view(HeapPtr backing, uint32_t offset, uint32_t length)
{
    HeapPtr repr = heap_.allocate(sizeof(StringRepr), alignof(StringRepr), string_id_);
    StringRepr* sr = static_cast<StringRepr*>(heap_.get_ptr(repr));
    sr->backing = backing;
    sr->offset = offset;
    sr->length = length;
    return repr;
}

const char* Runtime::get_string_data(HeapPtr str_ptr) const
{
    StringRepr* sr = static_cast<StringRepr*>(heap_.get_ptr(str_ptr));
    return static_cast<const char*>(heap_.get_ptr(sr->backing)) + sr->offset;
}

uint32_t Runtime::get_string_length(HeapPtr str_ptr) const
{
    StringRepr* sr = static_cast<StringRepr*>(heap_.get_ptr(str_ptr));
    return sr->length;
}

StringView Runtime::get_string_view(HeapPtr str_ptr) const
{
    return StringView(get_string_data(str_ptr), get_string_length(str_ptr));
}

StringView ScriptString::view(const Runtime& rt) const
{
    if (ptr.value == 0) { return {}; }
    return rt.get_string_view(ptr);
}

HeapPtr Runtime::alloc_struct(TypeID type_id)
{
    const Type* type = get_type(type_id);
    CHECK_F(!!type, "Unknown type: {}", type_id.to_u64());
    HeapPtr ptr = heap_.allocate(type->size, type->alignment, type_id);
    void* data = heap_.get_ptr(ptr);
    memset(data, 0, type->size);
    this->initialize_zero_defaults(type_id, static_cast<uint8_t*>(data));
    return ptr;
}

HeapPtr Runtime::alloc_tuple(TypeID type_id)
{
    const Type* type = get_type(type_id);
    CHECK_F(!!type, "Unknown type: {}", type_id.to_u64());
    HeapPtr ptr = heap_.allocate(type->size, type->alignment, type_id);
    void* data = heap_.get_ptr(ptr);
    memset(data, 0, type->size);
    this->initialize_zero_defaults(type_id, static_cast<uint8_t*>(data));
    return ptr;
}

HeapPtr Runtime::alloc_sum(TypeID type_id)
{
    const Type* type = get_type(type_id);
    CHECK_F(!!type, "Unknown type: {}", type_id.to_u64());
    CHECK_F(type->type_kind == TK_sum, "alloc_sum called with non-sum type");
    HeapPtr ptr = heap_.allocate(type->size, type->alignment, type_id);
    void* data = heap_.get_ptr(ptr);
    memset(data, 0, type->size);
    return ptr;
}

HeapPtr Runtime::alloc_struct_literal(const BraceInitLiteral* expr)
{
    if (!expr || expr->type_id_ == invalid_type_id) {
        LOG_F(ERROR, "[runtime] Invalid struct literal expression");
        return HeapPtr{0};
    }

    const Type* type = get_type(expr->type_id_);
    if (!type || type->type_kind != TK_struct) {
        LOG_F(ERROR, "[runtime] Struct literal has invalid type");
        return HeapPtr{0};
    }

    HeapPtr struct_ptr = heap_.allocate(type->size, type->alignment, expr->type_id_);
    void* struct_data = heap_.get_ptr(struct_ptr);
    memset(struct_data, 0, type->size);
    this->initialize_zero_defaults(expr->type_id_, static_cast<uint8_t*>(struct_data));
    return struct_ptr;
}

namespace {

TypeID unwrap_newtype(TypeID type_id, const Runtime& rt)
{
    const Type* type = rt.get_type(type_id);
    while (type && type->type_kind == TK_newtype) {
        TypeID wrapped = type->type_params[0].as<TypeID>();
        if (wrapped == invalid_type_id || wrapped == type_id) {
            return invalid_type_id;
        }
        type_id = wrapped;
        type = rt.get_type(type_id);
    }
    return type_id;
}

Value read_field_as_value(void* ptr, TypeID type_id, const Runtime& rt)
{
    if (const Type* type = rt.get_type(type_id); type && type->type_kind == TK_newtype) {
        TypeID base_type = unwrap_newtype(type_id, rt);
        if (base_type == invalid_type_id) {
            return Value{};
        }
        Value base_value = read_field_as_value(ptr, base_type, rt);
        if (base_value.type_id == invalid_type_id) {
            return Value{};
        }
        base_value.type_id = type_id;
        return base_value;
    }

    if (type_id == rt.int_type()) {
        return Value::make_int(*static_cast<int32_t*>(ptr));
    } else if (type_id == rt.float_type()) {
        return Value::make_float(*static_cast<float*>(ptr));
    } else if (type_id == rt.bool_type()) {
        return Value::make_bool(*static_cast<bool*>(ptr));
    } else if (type_id == rt.string_type()) {
        return Value::make_string(*static_cast<HeapPtr*>(ptr));
    } else if (type_id == rt.object_type()) {
        return Value::make_object(*static_cast<ObjectHandle*>(ptr));
    } else {
        const Type* type = rt.get_type(type_id);
        if (type && (type->type_kind == TK_struct || type->type_kind == TK_tuple || type->type_kind == TK_array || type->type_kind == TK_map || type->type_kind == TK_sum || type->type_kind == TK_function)) {
            return Value::make_heap(*static_cast<HeapPtr*>(ptr), type_id);
        }
        return Value{};
    }
}

void write_value_to_field(void* ptr, TypeID type_id, const Value& value, const Runtime& rt)
{
    if (const Type* type = rt.get_type(type_id); type && type->type_kind == TK_newtype) {
        TypeID base_type = unwrap_newtype(type_id, rt);
        if (base_type != invalid_type_id) {
            write_value_to_field(ptr, base_type, value, rt);
            return;
        }
    }

    if (type_id == rt.int_type()) {
        *static_cast<int32_t*>(ptr) = value.data.ival;
    } else if (type_id == rt.float_type()) {
        *static_cast<float*>(ptr) = value.data.fval;
    } else if (type_id == rt.bool_type()) {
        *static_cast<bool*>(ptr) = value.data.bval;
    } else if (type_id == rt.string_type()) {
        *static_cast<HeapPtr*>(ptr) = value.data.hptr;
    } else if (type_id == rt.object_type()) {
        *static_cast<ObjectHandle*>(ptr) = value.data.oval;
    } else {
        const Type* type = rt.get_type(type_id);
        if (type && type->type_kind == TK_function && type_id != value.type_id) {
            LOG_F(ERROR, "Function field type mismatch: expected {}, got {}",
                rt.type_name(type_id), rt.type_name(value.type_id));
        }
        *static_cast<HeapPtr*>(ptr) = value.data.hptr;
    }
}

} // namespace

void Runtime::initialize_zero_defaults(TypeID type_id, uint8_t* base)
{
    if (!base) {
        return;
    }

    const Type* type = get_type(type_id);
    while (type && (type->type_kind == TK_newtype || type->type_kind == TK_alias)) {
        if (!type->type_params[0].is<TypeID>()) {
            return;
        }
        type_id = type->type_params[0].as<TypeID>();
        type = get_type(type_id);
    }

    if (!type) {
        return;
    }

    if (type_id == string_type()) {
        HeapPtr* ptr = reinterpret_cast<HeapPtr*>(base);
        if (ptr->value == 0) {
            *ptr = alloc_string(StringView{});
        }
        return;
    }

    // Heap-stored categories are represented inline as a HeapPtr handle.
    // Only string/array/map are materialized to non-null empty defaults.
    if (type_table_.is_heap_type(type_id)) {
        HeapPtr* ptr = reinterpret_cast<HeapPtr*>(base);
        if (ptr->value != 0) {
            return;
        }

        switch (type->type_kind) {
        case TK_array: {
            TypeID elem_type_id = type->type_params[0].as<TypeID>();
            if (!type->type_params[1].empty()) {
                uint32_t count = static_cast<uint32_t>(std::max(0, type->type_params[1].as<int32_t>()));
                *ptr = alloc_array(elem_type_id, count);
            } else {
                *ptr = create_array_typed(elem_type_id, 0);
            }
            break;
        }
        case TK_map: {
            TypeID key_type = type->type_params[0].as<TypeID>();
            TypeID value_type = type->type_params[1].as<TypeID>();
            *ptr = alloc_map(key_type, value_type);
            break;
        }
        case TK_function:
            // Closures are nullable; keep HeapPtr{0}.
            break;
        default:
            // Other heap categories do not materialize from zero by default here.
            break;
        }
        return;
    }

    switch (type->type_kind) {
    case TK_struct: {
        auto struct_id = type->type_params[0].as<StructID>();
        const StructDef* def = type_table_.get(struct_id);
        if (!def) {
            return;
        }

        for (uint32_t i = 0; i < def->field_count; ++i) {
            const FieldDef& field = def->fields[i];
            initialize_zero_defaults(field.type_id, base + field.offset);
        }
        return;
    }
    case TK_tuple: {
        auto tuple_id = type->type_params[0].as<TupleID>();
        const TupleDef* def = type_table_.get(tuple_id);
        if (!def) {
            return;
        }

        for (uint32_t i = 0; i < def->element_count; ++i) {
            initialize_zero_defaults(def->element_types[i], base + def->offsets[i]);
        }
        return;
    }
    case TK_fixed_array: {
        TypeID elem_type_id = type->type_params[0].as<TypeID>();
        int32_t count = type->type_params[1].as<int32_t>();
        if (count <= 0) {
            return;
        }

        const Type* elem_type = get_type(elem_type_id);
        if (!elem_type) {
            return;
        }

        for (int32_t i = 0; i < count; ++i) {
            initialize_zero_defaults(elem_type_id,
                base + static_cast<uint32_t>(i) * elem_type->size);
        }
        return;
    }
    case TK_sum: {
        auto sum_id = type->type_params[0].as<SumID>();
        const SumDef* def = type_table_.get(sum_id);
        if (!def) {
            return;
        }

        uint32_t tag = *reinterpret_cast<uint32_t*>(base + def->tag_offset);
        if (tag >= def->variant_count) {
            return;
        }

        const VariantDef& variant = def->variants[tag];
        if (variant.payload_type == invalid_type_id) {
            return;
        }

        initialize_zero_defaults(variant.payload_type,
            base + def->union_offset + variant.payload_offset);
        return;
    }
    default:
        return;
    }
}

Value Runtime::read_struct_field_by_index(HeapPtr struct_ptr, const StructDef* struct_def, uint32_t field_index) const
{
    ENSURE_OR_RETURN_DEFAULT(struct_ptr.value != 0, "struct pointer is null");
    ENSURE_OR_RETURN_DEFAULT(struct_def, "struct defintion is invalid");
    ENSURE_OR_RETURN_DEFAULT(field_index < struct_def->field_count, "struct field index is invalid: {} >= {}",
        field_index, struct_def->field_count);

    void* struct_data = heap_.get_ptr(struct_ptr);
    if (!struct_data) {
        return Value{};
    }

    const FieldDef& field = struct_def->fields[field_index];
    void* field_ptr = static_cast<char*>(struct_data) + field.offset;

    return read_field_as_value(field_ptr, field.type_id, *this);
}

Value Runtime::read_struct_value_field(const Value& struct_val, const StructDef* def, uint32_t field_index)
{
    ENSURE_OR_RETURN_DEFAULT(def, "struct definition is null");
    ENSURE_OR_RETURN_DEFAULT(field_index < def->field_count, "struct field index out of range");
    const FieldDef& field = def->fields[field_index];
    Value mut_val = struct_val;
    void* data = get_value_data_ptr(mut_val);
    if (!data) return Value{};
    return read_field_as_value(static_cast<char*>(data) + field.offset, field.type_id, *this);
}

uint32_t Runtime::read_sum_value_tag(const Value& sum_val, const SumDef* def)
{
    if (!def) return UINT32_MAX;
    Value mut_val = sum_val;
    void* data = get_value_data_ptr(mut_val);
    if (!data) return UINT32_MAX;
    return read_sum_tag(data, def);
}

Value Runtime::read_sum_value_payload(const Value& sum_val, const SumDef* def, uint32_t variant_idx)
{
    if (!def || variant_idx >= def->variant_count) return Value{};
    Value mut_val = sum_val;
    void* data = get_value_data_ptr(mut_val);
    if (!data) return Value{};
    return read_sum_payload(data, def, variant_idx);
}

bool Runtime::write_struct_field_by_index(HeapPtr struct_ptr, const StructDef* struct_def, uint32_t field_index, const Value& value)
{
    ENSURE_OR_RETURN_FALSE(struct_ptr.value != 0, "struct pointer is null");
    ENSURE_OR_RETURN_FALSE(struct_def, "struct defintion is invalid");
    ENSURE_OR_RETURN_FALSE(field_index < struct_def->field_count, "struct field index is invalid: {} >= {}",
        field_index, struct_def->field_count);

    void* struct_data = heap_.get_ptr(struct_ptr);
    ENSURE_OR_RETURN_FALSE(struct_data, "invalid struct heap pointer: {}", struct_ptr.to_u64());
    const FieldDef& field = struct_def->fields[field_index];

    Value coerced_value = value;
    if (field.type_id != value.type_id) {
        if (field.type_id == bool_type() && value.type_id == int_type()) {
            coerced_value = Value::make_bool(value.data.ival != 0);
        } else if (field.type_id == int_type() && value.type_id == bool_type()) {
            coerced_value = Value::make_int(value.data.ival ? 1 : 0);
        } else {
            LOG_F(ERROR, "Field type mismatch: field[{}] '{}' expects type {} ({}), got {} ({})",
                field_index, field.name.view(),
                field.type_id.value, type_name(field.type_id),
                value.type_id.value, type_name(value.type_id));
            return false;
        }
    }

    void* field_ptr = static_cast<char*>(struct_data) + field.offset;
    write_value_to_field(field_ptr, field.type_id, coerced_value, *this);

    if (gc_ && type_table_.is_heap_type(field.type_id) && coerced_value.storage == ValueStorage::heap) {
        gc_->write_barrier(struct_ptr, coerced_value.data.hptr);
    }

    return true;
}

Value Runtime::read_field_at_offset(HeapPtr struct_ptr, uint32_t offset, TypeID type_id) const
{
    void* struct_data = heap_.get_ptr(struct_ptr);
    if (!struct_data) {
        return Value{};
    }
    void* field_ptr = static_cast<char*>(struct_data) + offset;
    return read_field_as_value(field_ptr, type_id, *this);
}

bool Runtime::write_field_at_offset(HeapPtr struct_ptr, uint32_t offset, TypeID type_id, const Value& value)
{
    void* struct_data = heap_.get_ptr(struct_ptr);
    if (!struct_data) {
        return false;
    }
    Value coerced_value = value;
    if (type_id != value.type_id) {
        if (type_id == bool_type() && value.type_id == int_type()) {
            coerced_value = Value::make_bool(value.data.ival != 0);
        } else if (type_id == int_type() && value.type_id == bool_type()) {
            coerced_value = Value::make_int(value.data.ival ? 1 : 0);
        } else {
            return false;
        }
    }
    void* field_ptr = static_cast<char*>(struct_data) + offset;
    write_value_to_field(field_ptr, type_id, coerced_value, *this);

    if (gc_ && type_table_.is_heap_type(type_id) && coerced_value.storage == ValueStorage::heap) {
        gc_->write_barrier(struct_ptr, coerced_value.data.hptr);
    }

    return true;
}

Value Runtime::read_struct_field(HeapPtr struct_ptr, TypeID struct_type_id, StringView field_name) const
{
    const Type* struct_type = get_type(struct_type_id);
    if (!struct_type || struct_type->type_kind != TK_struct) {
        return Value{};
    }

    if (!struct_type->type_params[0].is<StructID>()) {
        return Value{};
    }

    auto struct_id = struct_type->type_params[0].as<StructID>();
    const StructDef* struct_def = type_table_.get(struct_id);
    if (!struct_def) {
        return Value{};
    }

    uint32_t field_index = struct_def->field_index(field_name);
    if (field_index == UINT32_MAX) { return Value{}; }

    return read_struct_field_by_index(struct_ptr, struct_def, field_index);
}

bool Runtime::write_struct_field(HeapPtr struct_ptr, TypeID struct_type_id, StringView field_name, const Value& value)
{
    const Type* struct_type = get_type(struct_type_id);
    ENSURE_OR_RETURN_FALSE(struct_type && struct_type->type_kind == TK_struct, "invalid struct type: {}", struct_type_id.value);
    ENSURE_OR_RETURN_FALSE(struct_type->type_params[0].is<StructID>(), "struct type {} has does not have a struct id", struct_type_id.value);

    auto struct_id = struct_type->type_params[0].as<StructID>();
    const StructDef* struct_def = type_table_.get(struct_id);
    ENSURE_OR_RETURN_FALSE(struct_def, "failed to find struct definition for struct type {}, struct id: {}", struct_type_id.value, struct_id.value);

    uint32_t field_index = struct_def->field_index(field_name);
    ENSURE_OR_RETURN_FALSE(field_index != UINT32_MAX, "invalid field name '{}' for struct type {}, struct id: {}",
        field_name, struct_type_id.value, struct_id.value);

    return write_struct_field_by_index(struct_ptr, struct_def, field_index, value);
}

// -- Tuple Element Access ----------------------------------------------------

Value Runtime::read_tuple_element_by_index(HeapPtr tuple_ptr, const TupleDef* tuple_def, uint32_t element_index) const
{
    ENSURE_OR_RETURN_DEFAULT(tuple_ptr.value != 0, "tuple pointer is null");
    ENSURE_OR_RETURN_DEFAULT(tuple_def, "tuple defintion is invalid");
    ENSURE_OR_RETURN_DEFAULT(element_index < tuple_def->element_count, "tuple element index is invalid: {} >= {}",
        element_index, tuple_def->element_count);

    void* tuple_data = heap_.get_ptr(tuple_ptr);
    if (!tuple_data) {
        return Value{};
    }

    TypeID element_type = tuple_def->element_types[element_index];
    uint32_t offset = tuple_def->offsets[element_index];
    void* element_ptr = static_cast<char*>(tuple_data) + offset;

    return read_field_as_value(element_ptr, element_type, *this);
}

bool Runtime::write_tuple_element_by_index(HeapPtr tuple_ptr, const TupleDef* tuple_def, uint32_t element_index, const Value& value)
{
    ENSURE_OR_RETURN_DEFAULT(tuple_ptr.value != 0, "tuple pointer is null");
    ENSURE_OR_RETURN_DEFAULT(tuple_def, "tuple defintion is invalid");
    ENSURE_OR_RETURN_DEFAULT(element_index < tuple_def->element_count, "tuple element index is invalid: {} >= {}",
        element_index, tuple_def->element_count);

    void* tuple_data = heap_.get_ptr(tuple_ptr);
    if (!tuple_data) {
        return false;
    }

    TypeID element_type = tuple_def->element_types[element_index];
    if (element_type != value.type_id) {
        return false;
    }

    uint32_t offset = tuple_def->offsets[element_index];
    void* element_ptr = static_cast<char*>(tuple_data) + offset;

    write_value_to_field(element_ptr, element_type, value, *this);

    if (gc_ && type_table_.is_heap_type(element_type) && value.storage == ValueStorage::heap) {
        gc_->write_barrier(tuple_ptr, value.data.hptr);
    }

    return true;
}

Value Runtime::read_tuple_element(HeapPtr tuple_ptr, TypeID tuple_type_id, uint32_t element_index) const
{
    const Type* type = get_type(tuple_type_id);
    if (!type || type->type_kind != TK_tuple) {
        LOG_F(ERROR, "[runtime] Invalid tuple type");
        return Value{};
    }

    auto tuple_id = type->type_params[0].get<TupleID>();
    if (!tuple_id) {
        LOG_F(ERROR, "[runtime] Tuple type has no TupleID");
        return Value{};
    }

    const TupleDef* tuple_def = type_table_.get(*tuple_id);
    if (!tuple_def) {
        LOG_F(ERROR, "[runtime] TupleDef not found");
        return Value{};
    }

    return read_tuple_element_by_index(tuple_ptr, tuple_def, element_index);
}

// -- Sum Type Access ---------------------------------------------------------

void Runtime::write_sum_tag(HeapPtr sum_ptr, const SumDef* sum_def, uint32_t tag_value)
{
    void* data = heap_.get_ptr(sum_ptr);
    CHECK_F(data != nullptr, "sum pointer is null");
    write_sum_tag(data, sum_def, tag_value);
}

void Runtime::write_sum_tag(void* data, const SumDef* sum_def, uint32_t tag_value)
{
    CHECK_F(data != nullptr, "sum pointer is null");
    CHECK_F(sum_def != nullptr, "sum definition is null");
    CHECK_F(tag_value < sum_def->variant_count, "tag value out of range: {} >= {}", tag_value, sum_def->variant_count);

    uint32_t* tag_ptr = static_cast<uint32_t*>(data);
    *tag_ptr = tag_value;
}

void Runtime::write_sum_payload(HeapPtr sum_ptr, const SumDef* sum_def, uint32_t variant_idx, const Value& payload)
{
    void* data = heap_.get_ptr(sum_ptr);
    CHECK_F(data != nullptr, "sum pointer is null");
    write_sum_payload(data, sum_def, variant_idx, payload);

    if (gc_ && variant_idx < sum_def->variant_count) {
        const VariantDef& variant = sum_def->variants[variant_idx];
        if (variant.payload_type != invalid_type_id && type_table_.is_heap_type(variant.payload_type) && payload.storage == ValueStorage::heap) {
            gc_->write_barrier(sum_ptr, payload.data.hptr);
        }
    }
}

void Runtime::write_sum_payload(void* data, const SumDef* sum_def, uint32_t variant_idx, const Value& payload)
{
    CHECK_F(data != nullptr, "sum pointer is null");
    CHECK_F(sum_def != nullptr, "sum definition is null");
    CHECK_F(variant_idx < sum_def->variant_count, "variant index out of range");

    const VariantDef& variant = sum_def->variants[variant_idx];
    if (variant.payload_type == invalid_type_id) {
        return;
    }

    void* payload_ptr = static_cast<char*>(data) + sum_def->union_offset + variant.payload_offset;
    write_value_to_field(payload_ptr, variant.payload_type, payload, *this);
}

uint32_t Runtime::read_sum_tag(HeapPtr sum_ptr, const SumDef* sum_def) const
{
    void* data = heap_.get_ptr(sum_ptr);
    CHECK_F(data != nullptr, "sum pointer is null");
    return read_sum_tag(data, sum_def);
}

uint32_t Runtime::read_sum_tag(void* data, const SumDef* sum_def) const
{
    CHECK_F(data != nullptr, "sum pointer is null");
    CHECK_F(sum_def != nullptr, "sum definition is null");

    const uint32_t* tag_ptr = static_cast<const uint32_t*>(data);
    return *tag_ptr;
}

Value Runtime::read_sum_payload(HeapPtr sum_ptr, const SumDef* sum_def, uint32_t variant_idx) const
{
    void* data = heap_.get_ptr(sum_ptr);
    CHECK_F(data != nullptr, "sum pointer is null");
    return read_sum_payload(data, sum_def, variant_idx);
}

Value Runtime::read_sum_payload(void* data, const SumDef* sum_def, uint32_t variant_idx) const
{
    CHECK_F(data != nullptr, "sum pointer is null");
    CHECK_F(sum_def != nullptr, "sum definition is null");
    CHECK_F(variant_idx < sum_def->variant_count, "variant index out of range");

    const VariantDef& variant = sum_def->variants[variant_idx];
    if (variant.payload_type == invalid_type_id) {
        return Value{};
    }

    void* payload_ptr = static_cast<char*>(data) + sum_def->union_offset + variant.payload_offset;
    return read_field_as_value(payload_ptr, variant.payload_type, *this);
}

HeapPtr Runtime::alloc_array(TypeID element_type_id, uint32_t count)
{
    const Type* elem_type_ptr = get_type(element_type_id);
    CHECK_F(!!elem_type_ptr, "Unknown element type: {}", element_type_id.to_u64());

    // Copy info to avoid use-after-free if type_table_ reallocates
    uint32_t elem_size = elem_type_ptr->size;
    uint32_t elem_alignment = std::max(1u, elem_type_ptr->alignment);
    String elem_name(elem_type_ptr->name.view());

    // Create array type for header
    Type array_type{
        .name = nw::kernel::strings().intern(fmt::format("array!({}, {})", elem_name, count)),
        .type_params = {element_type_id, static_cast<int32_t>(count)},
        .type_kind = TK_array,
        .size = sizeof(HeapPtr),
        .alignment = alignof(HeapPtr),
    };
    TypeID array_type_id = type_table_.add(array_type);

    size_t header_size = sizeof(uint32_t);
    size_t elements_start = (header_size + elem_alignment - 1) & ~(elem_alignment - 1);
    size_t elements_size = count * elem_size;
    size_t total_size = elements_start + elements_size;
    size_t alignment = std::max(alignof(uint32_t), static_cast<size_t>(elem_alignment));

    HeapPtr ptr = heap_.allocate(total_size, alignment, array_type_id);
    void* data = heap_.get_ptr(ptr);

    uint32_t* count_ptr = static_cast<uint32_t*>(data);
    *count_ptr = count;

    void* elements_data = static_cast<uint8_t*>(data) + elements_start;
    memset(elements_data, 0, elements_size);

    if (elements_size > 0) {
        for (uint32_t i = 0; i < count; ++i) {
            this->initialize_zero_defaults(element_type_id,
                static_cast<uint8_t*>(elements_data) + (i * elem_size));
        }
    }

    return ptr;
}

bool Runtime::array_get(HeapPtr array_ptr, uint32_t index, Value& out_value) const
{
    CHECK_F(array_ptr.value != 0, "Null array pointer");
    auto* header = heap_.get_header(array_ptr);
    const Type* type = get_type(header->type_id);
    CHECK_F(type && type->type_kind == TK_array, "HeapPtr is not an array");

    if (type->type_params[1].empty()) {
        auto* arr = static_cast<IArray*>(heap_.get_ptr(array_ptr));
        if (!arr) {
            return false;
        }
        return arr->get_value(index, out_value, *this);
    }

    void* data = heap_.get_ptr(array_ptr);
    uint32_t count = *static_cast<uint32_t*>(data);

    if (index >= count) return false;

    TypeID elem_type_id = type->type_params[0].as<TypeID>();
    const Type* elem_type = get_type(elem_type_id);

    size_t header_size = sizeof(uint32_t);
    size_t alignment = std::max(1u, elem_type->alignment);
    size_t elements_start = (header_size + alignment - 1) & ~(alignment - 1);
    size_t offset = elements_start + (index * elem_type->size);

    void* elem_ptr = static_cast<char*>(data) + offset;
    out_value = read_field_as_value(elem_ptr, elem_type_id, *this);
    return true;
}

bool Runtime::array_set(HeapPtr array_ptr, uint32_t index, const Value& value)
{
    CHECK_F(array_ptr.value != 0, "Null array pointer");
    auto* header = heap_.get_header(array_ptr);
    const Type* type = get_type(header->type_id);
    CHECK_F(type && type->type_kind == TK_array, "HeapPtr is not an array");

    TypeID elem_type_id = type->type_params[0].as<TypeID>();

    if (type->type_params[1].empty()) {
        auto* arr = static_cast<IArray*>(heap_.get_ptr(array_ptr));
        if (!arr) {
            return false;
        }
        bool result = arr->set_value(index, value, *this);
        if (result && gc_ && type_table_.is_heap_type(elem_type_id) && value.storage == ValueStorage::heap) {
            gc_->write_barrier(array_ptr, value.data.hptr);
        }
        return result;
    }

    void* data = heap_.get_ptr(array_ptr);
    uint32_t count = *static_cast<uint32_t*>(data);

    if (index >= count) return false;

    const Type* elem_type = get_type(elem_type_id);

    if (value.type_id != elem_type_id) return false;

    size_t header_size = sizeof(uint32_t);
    size_t alignment = std::max(1u, elem_type->alignment);
    size_t elements_start = (header_size + alignment - 1) & ~(alignment - 1);
    size_t offset = elements_start + (index * elem_type->size);

    void* elem_ptr = static_cast<char*>(data) + offset;
    write_value_to_field(elem_ptr, elem_type_id, value, *this);

    if (gc_ && type_table_.is_heap_type(elem_type_id) && value.storage == ValueStorage::heap) {
        gc_->write_barrier(array_ptr, value.data.hptr);
    }

    return true;
}

size_t Runtime::array_size(HeapPtr array_ptr) const
{
    CHECK_F(array_ptr.value != 0, "Null array pointer");
    auto* header = heap_.get_header(array_ptr);
    const Type* type = get_type(header->type_id);
    CHECK_F(type && type->type_kind == TK_array, "HeapPtr is not an array");

    if (type->type_params[1].empty()) {
        auto* arr = static_cast<IArray*>(heap_.get_ptr(array_ptr));
        return arr ? arr->size() : 0;
    }

    void* data = heap_.get_ptr(array_ptr);
    return *static_cast<uint32_t*>(data);
}

HeapPtr Runtime::create_array_typed(TypeID element_type_id, size_t initial_capacity)
{
    const Type* elem_type = get_type(element_type_id);
    if (!elem_type) {
        LOG_F(ERROR, "[runtime] Unknown element type: {}", element_type_id.to_u64());
        return {};
    }

    auto is_value_type = [&](TypeID type_id) -> bool {
        const Type* type = get_type(type_id);
        if (!type) return false;

        if (type->type_kind == TK_fixed_array) {
            return true;
        }

        if (type->type_kind == TK_struct) {
            auto struct_id = type->type_params[0].as<StructID>();
            const StructDef* def = type_table_.get(struct_id);
            if (def && def->decl) {
                for (const auto& ann : def->decl->annotations_) {
                    if (ann.name.loc.view() == "value_type") {
                        return true;
                    }
                }
            }
        }

        if (type->type_kind == TK_sum) {
            auto sum_id = type->type_params[0].as<SumID>();
            const SumDef* def = type_table_.get(sum_id);
            if (def && def->decl) {
                for (const auto& ann : def->decl->annotations_) {
                    if (ann.name.loc.view() == "value_type") {
                        return true;
                    }
                }
            }
        }

        return false;
    };

    // Create array type for the heap allocation
    String elem_name(elem_type->name.view());
    Type array_type{
        .name = nw::kernel::strings().intern(fmt::format("array!({})", elem_name)),
        .type_params = {element_type_id, TypeParam{}},
        .type_kind = TK_array,
        .size = sizeof(HeapPtr),
        .alignment = alignof(HeapPtr),
    };
    TypeID array_type_id = type_table_.add(array_type);

    // Dispatch based on element type - allocate on ScriptHeap and use placement new
    HeapPtr ptr{0};

    if (element_type_id == int_type()) {
        ptr = heap_.allocate(sizeof(TypedArray<int32_t>), alignof(TypedArray<int32_t>), array_type_id);
        new (heap_.get_ptr(ptr)) TypedArray<int32_t>(element_type_id, initial_capacity);
    } else if (element_type_id == float_type()) {
        ptr = heap_.allocate(sizeof(TypedArray<float>), alignof(TypedArray<float>), array_type_id);
        new (heap_.get_ptr(ptr)) TypedArray<float>(element_type_id, initial_capacity);
    } else if (element_type_id == bool_type()) {
        ptr = heap_.allocate(sizeof(TypedArray<bool>), alignof(TypedArray<bool>), array_type_id);
        new (heap_.get_ptr(ptr)) TypedArray<bool>(element_type_id, initial_capacity);
    } else if (element_type_id == string_type() || element_type_id == object_type()) {
        ptr = heap_.allocate(sizeof(TypedArray<HeapPtr>), alignof(TypedArray<HeapPtr>), array_type_id);
        new (heap_.get_ptr(ptr)) TypedArray<HeapPtr>(element_type_id, initial_capacity);
    } else if (elem_type->type_kind == TK_struct && is_value_type(element_type_id)) {
        // For value-type structs - use StructArray with raw bytes
        ptr = heap_.allocate(sizeof(StructArray), alignof(StructArray), array_type_id);
        new (heap_.get_ptr(ptr)) StructArray(element_type_id, elem_type->size, elem_type->alignment, initial_capacity);
    } else {
        // For any other heap-allocated type (arrays, maps, sum types, etc.)
        ptr = heap_.allocate(sizeof(TypedArray<HeapPtr>), alignof(TypedArray<HeapPtr>), array_type_id);
        new (heap_.get_ptr(ptr)) TypedArray<HeapPtr>(element_type_id, initial_capacity);
    }

    return ptr;
}

IArray* Runtime::get_array_typed(HeapPtr array_ptr) const
{
    if (array_ptr.value == 0) return nullptr;

    auto* header = heap_.get_header(array_ptr);
    const Type* type = get_type(header->type_id);

    if (!type || type->type_kind != TK_array || !type->type_params[1].empty()) {
        return nullptr;
    }

    return static_cast<IArray*>(heap_.get_ptr(array_ptr));
}

HeapPtr Runtime::alloc_map(TypeID key_type_id, TypeID value_type_id)
{
    const Type* key_type_ptr = get_type(key_type_id);
    const Type* val_type_ptr = get_type(value_type_id);
    ENSURE_OR_RETURN_VALUE(HeapPtr{}, !!key_type_ptr, "Unknown key type: {}", key_type_id.to_u64());
    ENSURE_OR_RETURN_VALUE(HeapPtr{}, !!val_type_ptr, "Unknown value type: {}", value_type_id.to_u64());

    // Copy names to avoid use-after-free if type_table_ reallocates
    String key_name(key_type_ptr->name.view());
    String val_name(val_type_ptr->name.view());
    ENSURE_OR_RETURN_VALUE(HeapPtr{}, supports_map_key_type(key_type_id),
        "Map key type must be int/string or newtype over int/string, got: {}", key_name);

    // Create map type for header
    Type map_type{
        .name = nw::kernel::strings().intern(fmt::format("map!({}, {})",
            key_name, val_name)),
        .type_params = {key_type_id, value_type_id},
        .type_kind = TK_map,
        .size = sizeof(HeapPtr),
        .alignment = alignof(HeapPtr),
    };
    TypeID map_type_id = type_table_.add(map_type);

    HeapPtr ptr = heap_.allocate(sizeof(MapInstance), alignof(MapInstance), map_type_id);
    void* data = heap_.get_ptr(ptr);

    new (data) MapInstance(key_type_id, value_type_id);

    return ptr;
}

bool Runtime::map_get(HeapPtr map_ptr, const Value& key, Value& out_value) const
{
    CHECK_F(map_ptr.value != 0, "Null map pointer");

    auto* header = heap_.get_header(map_ptr);
    const Type* type = get_type(header->type_id);
    CHECK_F(type && type->type_kind == TK_map,
        "HeapPtr is not a map (type: {})", type ? type_name(header->type_id) : "<unknown>");

    MapInstance* map = static_cast<MapInstance*>(heap_.get_ptr(map_ptr));
    auto it = map->data.find(key);
    if (it != map->data.end()) {
        out_value = it->second;
        return true;
    }
    return false;
}

bool Runtime::map_set(HeapPtr map_ptr, const Value& key, const Value& value)
{
    CHECK_F(map_ptr.value != 0, "Null map pointer");

    auto* header = heap_.get_header(map_ptr);
    const Type* type = get_type(header->type_id);
    CHECK_F(type && type->type_kind == TK_map,
        "HeapPtr is not a map (type: {})", type ? type_name(header->type_id) : "<unknown>");

    MapInstance* map = static_cast<MapInstance*>(heap_.get_ptr(map_ptr));
    CHECK_F(map->iteration_count == 0, "Cannot modify map while iterating");
    auto result = map->data.insert({key, value});
    if (!result.second) {
        result.first->second = value;
    }

    if (gc_) {
        if (type_table_.is_heap_type(map->key_type) && key.storage == ValueStorage::heap) {
            gc_->write_barrier(map_ptr, key.data.hptr);
        }
        if (type_table_.is_heap_type(map->value_type) && value.storage == ValueStorage::heap) {
            gc_->write_barrier(map_ptr, value.data.hptr);
        }
    }

    return result.second;
}

bool Runtime::map_contains(HeapPtr map_ptr, const Value& key) const
{
    CHECK_F(map_ptr.value != 0, "Null map pointer");

    auto* header = heap_.get_header(map_ptr);
    const Type* type = get_type(header->type_id);
    CHECK_F(type && type->type_kind == TK_map,
        "HeapPtr is not a map (type: {})", type ? type_name(header->type_id) : "<unknown>");

    MapInstance* map = static_cast<MapInstance*>(heap_.get_ptr(map_ptr));
    return map->data.contains(key);
}

size_t Runtime::map_size(HeapPtr map_ptr) const
{
    CHECK_F(map_ptr.value != 0, "Null map pointer");

    auto* header = heap_.get_header(map_ptr);
    const Type* type = get_type(header->type_id);
    CHECK_F(type && type->type_kind == TK_map,
        "HeapPtr is not a map (type: {})", type ? type_name(header->type_id) : "<unknown>");

    MapInstance* map = static_cast<MapInstance*>(heap_.get_ptr(map_ptr));
    return map->data.size();
}

bool Runtime::map_remove(HeapPtr map_ptr, const Value& key)
{
    CHECK_F(map_ptr.value != 0, "Null map pointer");

    auto* header = heap_.get_header(map_ptr);
    const Type* type = get_type(header->type_id);
    CHECK_F(type && type->type_kind == TK_map,
        "HeapPtr is not a map (type: {})", type ? type_name(header->type_id) : "<unknown>");

    MapInstance* map = static_cast<MapInstance*>(heap_.get_ptr(map_ptr));
    CHECK_F(map->iteration_count == 0, "Cannot modify map while iterating");
    return map->data.erase(key) > 0;
}

void Runtime::map_clear(HeapPtr map_ptr)
{
    CHECK_F(map_ptr.value != 0, "Null map pointer");

    auto* header = heap_.get_header(map_ptr);
    const Type* type = get_type(header->type_id);
    CHECK_F(type && type->type_kind == TK_map,
        "HeapPtr is not a map (type: {})", type ? type_name(header->type_id) : "<unknown>");

    MapInstance* map = static_cast<MapInstance*>(heap_.get_ptr(map_ptr));
    CHECK_F(map->iteration_count == 0, "Cannot modify map while iterating");
    map->data.clear();
}

HeapPtr Runtime::map_iter_begin(HeapPtr map_ptr)
{
    CHECK_F(map_ptr.value != 0, "Null map pointer");

    auto* header = heap_.get_header(map_ptr);
    const Type* type = get_type(header->type_id);
    CHECK_F(type && type->type_kind == TK_map,
        "HeapPtr is not a map (type: {})", type ? type_name(header->type_id) : "<unknown>");

    MapInstance* map = static_cast<MapInstance*>(heap_.get_ptr(map_ptr));
    map->iteration_count++;

    HeapPtr iter_ptr = heap_.allocate(sizeof(MapIterator), alignof(MapIterator), invalid_type_id);
    void* iter_data = heap_.get_ptr(iter_ptr);
    new (iter_data) MapIterator{map_ptr, map->data.begin(), map->data.end()};
    return iter_ptr;
}

bool Runtime::map_iter_next(HeapPtr iter_ptr, Value& key, Value& value)
{
    CHECK_F(iter_ptr.value != 0, "Null iterator pointer");
    MapIterator* iter = static_cast<MapIterator*>(heap_.get_ptr(iter_ptr));

    if (iter->current == iter->end) {
        return false;
    }

    key = iter->current->first;
    value = iter->current->second;
    ++iter->current;
    return true;
}

void Runtime::map_iter_end(HeapPtr map_ptr, HeapPtr iter_ptr)
{
    CHECK_F(map_ptr.value != 0, "Null map pointer");
    CHECK_F(iter_ptr.value != 0, "Null iterator pointer");

    MapInstance* map = static_cast<MapInstance*>(heap_.get_ptr(map_ptr));
    CHECK_F(map->iteration_count > 0, "Map iteration count underflow");
    map->iteration_count--;

    // Note: Iterator memory will be reclaimed by GC
}

// == NativeStructBuilder =====================================================
// ============================================================================

bool Runtime::validate_native_struct(StringView struct_name, const StructDef* struct_def) const
{
    auto it = native_struct_layouts_.find(String(struct_name));
    if (it == native_struct_layouts_.end()) {
        LOG_F(ERROR, "[native] [[native]] struct '{}' not registered in C++. "
                     "Call validate_native_struct<T>(\\\"{}\\\") at startup.",
            struct_name, struct_name);
        return false;
    }

    const auto& native_layout = it->second;

    if (native_layout.size != struct_def->size) {
        LOG_F(ERROR, "[native] Native struct '{}' size mismatch: C++ size={}, smalls size={}",
            struct_name, native_layout.size, struct_def->size);
        return false;
    }

    if (native_layout.alignment != struct_def->alignment) {
        LOG_F(ERROR, "[native] Native struct '{}' alignment mismatch: C++ alignment={}, smalls alignment={}",
            struct_name, native_layout.alignment, struct_def->alignment);
        return false;
    }

    if (native_layout.fields.size() != struct_def->field_count) {
        LOG_F(ERROR, "[native] Native struct '{}' field count mismatch: C++ has {} fields, smalls has {} fields",
            struct_name, native_layout.fields.size(), struct_def->field_count);
        return false;
    }

    for (size_t i = 0; i < native_layout.fields.size(); ++i) {
        const auto& cpp_field = native_layout.fields[i];
        const auto& smalls_field = struct_def->fields[i];

        if (cpp_field.name != smalls_field.name.view()) {
            LOG_F(ERROR, "[native] Native struct '{}' field name mismatch at index {}: C++ field '{}', smalls field '{}'",
                struct_name, i, cpp_field.name, smalls_field.name.view());
            return false;
        }

        if (cpp_field.offset != smalls_field.offset) {
            LOG_F(ERROR, "[native] Native struct '{}.{}' offset mismatch: C++ offset={}, smalls offset={}",
                struct_name, cpp_field.name, cpp_field.offset, smalls_field.offset);
            return false;
        }

        if (cpp_field.type_id != smalls_field.type_id) {
            const Type* smalls_type = get_type(smalls_field.type_id);
            if (cpp_field.type_id == any_type() && smalls_type && smalls_type->type_kind == TK_function) {
                // ScriptClosure (any_type) matches any function type — both are HeapPtr-sized
            } else {
                LOG_F(ERROR, "[native] Native struct '{}.{}' type mismatch: C++ type '{}', smalls type '{}'",
                    struct_name, cpp_field.name,
                    type_name(cpp_field.type_id), type_name(smalls_field.type_id));
                return false;
            }
        }
    }

    LOG_F(INFO, "[native] Validated native struct '{}' matches C++ layout", struct_name);
    return true;
}

// == Runtime Module Building =================================================

ModuleBuilder Runtime::build_module(StringView path)
{
    return ModuleBuilder(this, path);
}

// == ModuleBuilder ===========================================================
// ============================================================================

ModuleBuilder::ModuleBuilder(Runtime* runtime, StringView path)
    : runtime_{runtime}
    , path_{path}
    , script_{nullptr}
{
    // Allocate empty script for this module using runtime's allocator
    // TODO: Script constructor needs updating - for now use placeholder
    // void* mem = runtime_->allocator()->allocate(sizeof(Script), alignof(Script));
    // script_ = new (mem) Script(ResourceData{}, nullptr);

    interface_.module_path = String(path);
}

ModuleBuilder::~ModuleBuilder()
{
    if (!finalized_) {
        LOG_F(WARNING, "[runtime] ModuleBuilder for '{}' destroyed without finalize()", path_);
    }
}

/// ModuleBuilder::handle_type() implementation
ModuleBuilder& ModuleBuilder::handle_type(StringView name, uint8_t runtime_type)
{
    String qualified_name = path_ + "." + String(name);
    runtime_->add_handle_type(qualified_name, runtime_type);
    interface_.opaque_types.push_back(String(name));
    return *this;
}

void ModuleBuilder::finalize()
{
    if (finalized_) {
        LOG_F(ERROR, "[runtime] ModuleBuilder for '{}' already finalized", path_);
        return;
    }

    // Register the module interface for .smalli generation
    runtime_->register_native_interface(std::move(interface_));
    finalized_ = true;

    LOG_F(INFO, "[runtime] Finalized C++ module: {}", path_);
}

// == Handle System ===========================================================

TypeID Runtime::add_handle_type(StringView name, uint8_t runtime_type)
{
    // Register the type in the type system
    TypeID tid = type_id(name, true);
    handle_type_to_typeid_[runtime_type] = tid;
    return tid;
}

HeapPtr Runtime::alloc_handle(TypeID type_id)
{
    // Allocate space for ObjectHeader + TypedHandle
    size_t total_size = sizeof(ScriptHeap::ObjectHeader) + sizeof(TypedHandle);
    HeapPtr ptr = heap_.allocate(total_size, alignof(TypedHandle), type_id);
    if (ptr.value == 0) { return HeapPtr{0}; }

    // Get the header and handle pointers
    auto* header = static_cast<ScriptHeap::ObjectHeader*>(heap_.get_ptr(ptr));
    auto* handle = reinterpret_cast<TypedHandle*>(header + 1);

    // Placement new to initialize the handle
    new (handle) TypedHandle{};

    return ptr;
}

TypedHandle* Runtime::get_handle(HeapPtr ptr)
{
    if (ptr.value == 0) {
        return nullptr;
    }

    auto* header = static_cast<ScriptHeap::ObjectHeader*>(heap_.get_ptr(ptr));
    return reinterpret_cast<TypedHandle*>(header + 1);
}

HeapPtr Runtime::intern_handle(TypedHandle value, OwnershipMode mode, bool force_mode)
{
    HeapPtr existing = lookup_handle(value);
    if (existing.value != 0) {
        if (force_mode) {
            set_handle_ownership(value, mode);
        }
        return existing;
    }

    TypeID type_id = handle_type_to_typeid_[value.type];
    if (type_id == invalid_type_id) {
        return HeapPtr{0};
    }

    HeapPtr ptr = alloc_handle(type_id);
    if (ptr.value == 0) {
        return ptr;
    }

    auto* handle = get_handle(ptr);
    if (handle) {
        *handle = value;
    }

    register_handle(value, ptr, mode);
    return ptr;
}

HeapPtr Runtime::lookup_handle(TypedHandle h) const
{
    auto it = global_handle_registry_.find(h);
    if (it == global_handle_registry_.end()) {
        return HeapPtr{0};
    }
    return it->second.vm_value;
}

void Runtime::register_handle(TypedHandle h, HeapPtr vm_value, OwnershipMode mode)
{
    // Check if handle already registered
    if (global_handle_registry_.contains(h)) {
        TypeID type_id = handle_type_to_typeid_[h.type];
        LOG_F(WARNING, "[runtime] Handle already registered (type={}, gen={}, id={}, pool_type={})",
            type_name(type_id), h.generation, h.id, h.type);
        return;
    }

    // Add to registry
    HandleEntry entry;
    entry.vm_value = vm_value;
    entry.mode = mode;
    global_handle_registry_[h] = entry;
}

bool Runtime::set_handle_ownership(TypedHandle h, OwnershipMode mode)
{
    auto it = global_handle_registry_.find(h);
    if (it == global_handle_registry_.end()) {
        return false;
    }

    it->second.mode = mode;
    return true;
}

std::optional<OwnershipMode> Runtime::handle_ownership(TypedHandle h) const
{
    auto it = global_handle_registry_.find(h);
    if (it == global_handle_registry_.end()) {
        return std::nullopt;
    }
    return it->second.mode;
}

bool Runtime::is_handle_type(TypeID tid) const
{
    for (const auto& entry : handle_type_to_typeid_) {
        if (entry == tid) { return true; }
    }
    return false;
}

void Runtime::enumerate_handle_roots(GCRootVisitor& visitor)
{
    for (auto& [h, entry] : global_handle_registry_) {
        (void)h;
        if (entry.mode == OwnershipMode::VM_OWNED) {
            continue;
        }

        if (entry.vm_value.value != 0) {
            visitor.visit_root(&entry.vm_value);
        }
    }
}

void Runtime::register_handle_destructor(TypeID handle_type, std::function<void(TypedHandle, HeapPtr)> destructor)
{
    if (handle_type == invalid_type_id) {
        return;
    }

    if (!destructor) {
        handle_destructors_.erase(handle_type);
        return;
    }

    handle_destructors_[handle_type] = std::move(destructor);
}

bool Runtime::native_types_compatible(TypeID cpp_type, TypeID script_type) const
{
    // Exact match
    if (cpp_type == script_type) {
        return true;
    }

    const Type* cpp_info = get_type(cpp_type);
    const Type* script_info = get_type(script_type);

    auto unwrap_newtype = [&](const Type* type, TypeID type_id) {
        while (type && type->type_kind == TK_newtype && !type->type_params.empty() && type->type_params[0].is<TypeID>()) {
            type_id = type->type_params[0].as<TypeID>();
            type = get_type(type_id);
        }
        return type_id;
    };

    bool cpp_is_newtype = cpp_info && cpp_info->type_kind == TK_newtype;
    bool script_is_newtype = script_info && script_info->type_kind == TK_newtype;

    // Allow C++ base type <-> script newtype compatibility for native interfaces.
    // This keeps script-side strong typing while avoiding custom C++ wrapper types
    // for each rule newtype.
    if (cpp_is_newtype != script_is_newtype) {
        TypeID cpp_base = unwrap_newtype(cpp_info, cpp_type);
        TypeID script_base = unwrap_newtype(script_info, script_type);
        if (cpp_base == script_base) {
            return true;
        }
    }

    // Wildcard for handle types (invalid_type_id from C++ accepts any handle type)
    if (cpp_type == invalid_type_id && is_handle_type(script_type)) {
        return true;
    }

    // Wildcard for arrays (any_array matches any array<T>)
    if (cpp_type == any_array_id_) {
        const Type* t = get_type(script_type);
        return t && t->type_kind == TK_array;
    }

    // Wildcard for maps (any_map matches any map<K,V>)
    if (cpp_type == any_map_id_) {
        const Type* t = get_type(script_type);
        return t && t->type_kind == TK_map;
    }

    // Wildcard for dynamic types (any_type from C++ accepts any script type)
    // Used for generic returns like $T where script determines actual type
    if (cpp_type == any_id_) {
        return true;
    }

    // Allow native ObjectHandle signatures to bind object subtypes.
    if (cpp_type == object_id_ && is_object_subtype(script_type)) {
        return true;
    }

    return false;
}

// == Tuple Type Helpers ======================================================

TypeID Runtime::register_tuple_type(const Vector<TypeID>& element_types)
{
    if (element_types.empty()) {
        return invalid_type_id;
    }

    void* mem = allocator()->allocate(sizeof(TupleDef), alignof(TupleDef));
    auto* tuple_def = new (mem) TupleDef{};
    tuple_def->element_count = static_cast<uint32_t>(element_types.size());

    void* types_mem = allocator()->allocate(sizeof(TypeID) * element_types.size(), alignof(TypeID));
    tuple_def->element_types = static_cast<TypeID*>(types_mem);

    void* offsets_mem = allocator()->allocate(sizeof(uint32_t) * element_types.size(), alignof(uint32_t));
    tuple_def->offsets = static_cast<uint32_t*>(offsets_mem);

    uint32_t offset = 0;
    uint32_t tuple_alignment = 1;

    for (size_t i = 0; i < element_types.size(); ++i) {
        const Type* elem_type = type_table_.get(element_types[i]);
        if (!elem_type) {
            return invalid_type_id;
        }

        uint32_t elem_size = elem_type->size;
        uint32_t elem_alignment = elem_type->alignment > 0 ? elem_type->alignment : 1;

        tuple_alignment = std::max(tuple_alignment, elem_alignment);
        offset = (offset + elem_alignment - 1) & ~(elem_alignment - 1);

        tuple_def->element_types[i] = element_types[i];
        tuple_def->offsets[i] = offset;

        offset += elem_size;
    }

    tuple_def->size = (offset + tuple_alignment - 1) & ~(tuple_alignment - 1);
    tuple_def->alignment = tuple_alignment;

    TupleID tuple_id = type_table_.add_tuple(tuple_def);

    String type_name = "(";
    for (size_t i = 0; i < element_types.size(); ++i) {
        if (i > 0) type_name += ", ";
        type_name += type_table_.get(element_types[i])->name.view();
    }
    type_name += ")";

    Type tuple_type{
        .name = nw::kernel::strings().intern(type_name),
        .type_params = {tuple_id, TypeParam{}},
        .type_kind = TK_tuple,
        .primitive_kind = PK_unspecified,
        .size = tuple_def->size,
        .alignment = tuple_def->alignment,
    };

    return type_table_.add(tuple_type);
}

size_t Runtime::get_tuple_element_count(TypeID tuple_type) const
{
    const Type* type = type_table_.get(tuple_type);
    ENSURE_OR_RETURN_ZERO(type && type->type_kind == TK_tuple, "inavlid tuple type {}", tuple_type.value);
    ENSURE_OR_RETURN_ZERO(type->type_params[0].is<TupleID>(), "tuple type {} does not have tuple id", tuple_type.value);

    auto tuple_id = type->type_params[0].as<TupleID>();
    const TupleDef* tuple_def = type_table_.get(tuple_id);
    ENSURE_OR_RETURN_ZERO(tuple_def, "unable to locate tuple definition for tuple id {}", tuple_id.value);

    return tuple_def->element_count;
}

TypeID Runtime::get_tuple_element_type(TypeID tuple_type, size_t index) const
{
    const Type* type = type_table_.get(tuple_type);
    ENSURE_OR_RETURN_VALUE(invalid_type_id, type && type->type_kind == TK_tuple, "inavlid tuple type {}", tuple_type.value);
    ENSURE_OR_RETURN_VALUE(invalid_type_id, type->type_params[0].is<TupleID>(), "tuple type {} does not have tuple id", tuple_type.value);

    auto tuple_id = type->type_params[0].as<TupleID>();
    const TupleDef* tuple_def = type_table_.get(tuple_id);
    ENSURE_OR_RETURN_VALUE(invalid_type_id, tuple_def, "invalid tuple definition for tuple id {}", tuple_id.value);
    ENSURE_OR_RETURN_VALUE(invalid_type_id, index < tuple_def->element_count, "invalid tuple index for tuple id {}: {} >= {}", tuple_id.value,
        index, tuple_def->element_count);

    return tuple_def->element_types[index];
}

TypeID Runtime::register_function_type(const Vector<TypeID>& param_types, TypeID return_type)
{
    TypeID normalized_return = return_type == invalid_type_id ? void_type() : return_type;
    void* mem = allocator()->allocate(sizeof(FunctionDef), alignof(FunctionDef));
    auto* func_def = new (mem) FunctionDef{};
    func_def->param_count = static_cast<uint32_t>(param_types.size());
    func_def->return_type = normalized_return;

    if (!param_types.empty()) {
        void* types_mem = allocator()->allocate(sizeof(TypeID) * param_types.size(), alignof(TypeID));
        func_def->param_types = static_cast<TypeID*>(types_mem);
        for (size_t i = 0; i < param_types.size(); ++i) {
            func_def->param_types[i] = param_types[i];
        }
    } else {
        func_def->param_types = nullptr;
    }

    FunctionID func_id = type_table_.add_function(func_def);

    String type_name = "fn(";
    for (size_t i = 0; i < param_types.size(); ++i) {
        if (i > 0) {
            absl::StrAppend(&type_name, ", ");
        }
        const Type* param_type = type_table_.get(param_types[i]);
        if (param_type) {
            absl::StrAppend(&type_name, param_type->name.view());
        }
    }
    absl::StrAppend(&type_name, ")");

    if (normalized_return != invalid_type_id && normalized_return != void_type()) {
        const Type* ret_type = type_table_.get(normalized_return);
        if (ret_type) {
            absl::StrAppend(&type_name, ": ", ret_type->name.view());
        }
    }

    Type func_type{
        .name = nw::kernel::strings().intern(type_name),
        .type_params = {func_id, TypeParam{}},
        .type_kind = TK_function,
        .primitive_kind = PK_unspecified,
        .size = sizeof(HeapPtr),
        .alignment = alignof(HeapPtr),
        .contains_heap_refs = true,
    };

    return type_table_.add(func_type);
}

size_t Runtime::get_function_param_count(TypeID func_type) const
{
    const Type* type = type_table_.get(func_type);
    ENSURE_OR_RETURN_ZERO(type && type->type_kind == TK_function, "invalid function type {}", func_type.value);
    ENSURE_OR_RETURN_ZERO(type->type_params[0].is<FunctionID>(), "function type {} does not have function id", func_type.value);

    auto func_id = type->type_params[0].as<FunctionID>();
    const FunctionDef* func_def = type_table_.get(func_id);
    ENSURE_OR_RETURN_ZERO(func_def, "unable to locate function definition for function id {}", func_id.value);

    return func_def->param_count;
}

TypeID Runtime::get_function_param_type(TypeID func_type, size_t index) const
{
    const Type* type = type_table_.get(func_type);
    ENSURE_OR_RETURN_VALUE(invalid_type_id, type && type->type_kind == TK_function, "invalid function type {}", func_type.value);
    ENSURE_OR_RETURN_VALUE(invalid_type_id, type->type_params[0].is<FunctionID>(), "function type {} does not have function id", func_type.value);

    auto func_id = type->type_params[0].as<FunctionID>();
    const FunctionDef* func_def = type_table_.get(func_id);
    ENSURE_OR_RETURN_VALUE(invalid_type_id, func_def, "invalid function definition for function id {}", func_id.value);
    ENSURE_OR_RETURN_VALUE(invalid_type_id, index < func_def->param_count, "invalid function param index: {} >= {}", index, func_def->param_count);

    return func_def->param_types[index];
}

TypeID Runtime::get_function_return_type(TypeID func_type) const
{
    const Type* type = type_table_.get(func_type);
    ENSURE_OR_RETURN_VALUE(invalid_type_id, type && type->type_kind == TK_function, "invalid function type {}", func_type.value);
    ENSURE_OR_RETURN_VALUE(invalid_type_id, type->type_params[0].is<FunctionID>(), "function type {} does not have function id", func_type.value);

    auto func_id = type->type_params[0].as<FunctionID>();
    const FunctionDef* func_def = type_table_.get(func_id);
    ENSURE_OR_RETURN_VALUE(invalid_type_id, func_def, "invalid function definition for function id {}", func_id.value);

    return func_def->return_type;
}

HeapPtr Runtime::alloc_closure(TypeID func_type, const CompiledFunction* func, const BytecodeModule* module, size_t upvalue_count)
{
    HeapPtr ptr = heap_.allocate(sizeof(Closure), alignof(Closure), func_type);
    auto* closure = new (heap_.get_ptr(ptr)) Closure(func, module);
    closure->upvalues.reserve(upvalue_count);
    return ptr;
}

Closure* Runtime::get_closure(HeapPtr ptr) const
{
    if (ptr.value == 0) {
        return nullptr;
    }
    return static_cast<Closure*>(heap_.get_ptr(ptr));
}

Upvalue* Runtime::alloc_upvalue()
{
    HeapPtr ptr = heap_.allocate(sizeof(Upvalue), alignof(Upvalue), invalid_type_id);
    auto* uv = new (heap_.get_ptr(ptr)) Upvalue();
    uv->heap_ptr = ptr;
    return uv;
}

void Runtime::destruct_object(HeapPtr ptr)
{
    if (ptr.value == 0) { return; }

    auto* header = heap_.get_header(ptr);
    const Type* type = get_type(header->type_id);
    if (!type) { return; }

    if (is_handle_type(header->type_id)) {
        TypedHandle* hptr = get_handle(ptr);
        if (!hptr) {
            return;
        }

        const TypedHandle h = *hptr;
        auto it = global_handle_registry_.find(h);
        if (it != global_handle_registry_.end()) {
            OwnershipMode mode = it->second.mode;
            if (it->second.mode != OwnershipMode::VM_OWNED) {
                LOG_F(ERROR,
                    "[runtime] Finalizing non-VM-owned handle cell (type={}, gen={}, id={}, pool_type={}, mode={})",
                    type_name(header->type_id), h.generation, h.id, h.type, static_cast<int>(it->second.mode));
            }

            auto dt = handle_destructors_.find(header->type_id);
            std::function<void(TypedHandle, HeapPtr)> dtor;
            if (mode == OwnershipMode::VM_OWNED && dt != handle_destructors_.end()) {
                dtor = dt->second;
            }

            global_handle_registry_.erase(it);

            if (dtor) {
                dtor(h, ptr);
            }
        }

        return;
    }

    void* data = heap_.get_ptr(ptr);

    switch (type->type_kind) {
    case TK_array:
        if (type->type_params[1].empty()) {
            static_cast<IArray*>(data)->~IArray();
        }
        break;
    case TK_map:
        static_cast<MapInstance*>(data)->~MapInstance();
        break;
    case TK_function:
        static_cast<Closure*>(data)->~Closure();
        break;
    default:
        break;
    }
}

void* Runtime::get_value_data_ptr(const Value& v)
{
    if (v.storage == ValueStorage::heap) {
        return heap_.get_ptr(v.data.hptr);
    } else if (v.storage == ValueStorage::stack) {
        uint8_t* stack_data = vm_->current_frame_stack_data();
        if (!stack_data) { return nullptr; }
        return stack_data + v.data.stack_offset;
    }
    return nullptr;
}

namespace {

struct GenericAstSnapshot : NullVisitor {
    struct NodeState {
        TypeID type_id{};
        bool is_const = false;
        immer::map<String, Export> env;
    };

    struct CallState {
        Vector<TypeID> inferred_type_args;
        std::optional<IntrinsicId> intrinsic_id;
        const FunctionDefinition* resolved_func = nullptr;
        Script* resolved_provider = nullptr;
    };

    struct IdentState {
        const Declaration* decl = nullptr;
    };

    struct TypeExprState {
        String qualified_name;
    };

    struct LambdaState {
        Vector<CapturedVar> captures;
        uint32_t compiled_func_index = 0;
    };

    absl::flat_hash_map<AstNode*, NodeState> nodes;
    absl::flat_hash_map<CallExpression*, CallState> calls;
    absl::flat_hash_map<IdentifierExpression*, IdentState> idents;
    absl::flat_hash_map<TypeExpression*, TypeExprState> type_exprs;
    absl::flat_hash_map<LambdaExpression*, LambdaState> lambdas;

    void snap_node(AstNode* n)
    {
        if (!n || nodes.contains(n)) {
            return;
        }
        nodes[n] = NodeState{n->type_id_, n->is_const_, n->env_};
    }

    void visit(FunctionDefinition* decl) override
    {
        snap_node(decl);
        if (decl->return_type) {
            decl->return_type->accept(this);
        }
        for (auto* p : decl->params) {
            if (p) {
                p->accept(this);
            }
        }
        if (decl->block) {
            decl->block->accept(this);
        }
    }

    void visit(VarDecl* decl) override
    {
        snap_node(decl);
        if (decl->type) {
            decl->type->accept(this);
        }
        if (decl->init) {
            decl->init->accept(this);
        }
    }

    void visit(TypeExpression* expr) override
    {
        snap_node(expr);
        if (!type_exprs.contains(expr)) {
            type_exprs[expr] = TypeExprState{expr->qualified_name};
        }
        if (expr->name) {
            expr->name->accept(this);
        }
        for (auto* p : expr->params) {
            if (p) {
                p->accept(this);
            }
        }
        if (expr->fn_return_type) {
            expr->fn_return_type->accept(this);
        }
    }

    void visit(BlockStatement* stmt) override
    {
        snap_node(stmt);
        for (auto* n : stmt->nodes) {
            if (n) {
                n->accept(this);
            }
        }
    }

    void visit(DeclList* stmt) override
    {
        snap_node(stmt);
        for (auto* d : stmt->decls) {
            if (d) {
                d->accept(this);
            }
        }
    }

    void visit(ExprStatement* stmt) override
    {
        snap_node(stmt);
        if (stmt->expr) {
            stmt->expr->accept(this);
        }
    }

    void visit(IfStatement* stmt) override
    {
        snap_node(stmt);
        if (stmt->expr) {
            stmt->expr->accept(this);
        }
        if (stmt->if_branch) {
            stmt->if_branch->accept(this);
        }
        if (stmt->else_branch) {
            stmt->else_branch->accept(this);
        }
    }

    void visit(ForStatement* stmt) override
    {
        snap_node(stmt);
        if (stmt->init) {
            stmt->init->accept(this);
        }
        if (stmt->check) {
            stmt->check->accept(this);
        }
        if (stmt->inc) {
            stmt->inc->accept(this);
        }
        if (stmt->block) {
            stmt->block->accept(this);
        }
    }

    void visit(ForEachStatement* stmt) override
    {
        snap_node(stmt);
        if (stmt->var) {
            stmt->var->accept(this);
        }
        if (stmt->key_var) {
            stmt->key_var->accept(this);
        }
        if (stmt->value_var) {
            stmt->value_var->accept(this);
        }
        if (stmt->collection) {
            stmt->collection->accept(this);
        }
        if (stmt->block) {
            stmt->block->accept(this);
        }
    }

    void visit(SwitchStatement* stmt) override
    {
        snap_node(stmt);
        if (stmt->target) {
            stmt->target->accept(this);
        }
        if (stmt->block) {
            stmt->block->accept(this);
        }
    }

    void visit(LabelStatement* stmt) override
    {
        snap_node(stmt);
        if (stmt->expr) {
            stmt->expr->accept(this);
        }
        for (auto* b : stmt->bindings) {
            if (b) {
                b->accept(this);
            }
        }
        if (stmt->guard) {
            stmt->guard->accept(this);
        }
    }

    void visit(JumpStatement* stmt) override
    {
        snap_node(stmt);
        for (auto* e : stmt->exprs) {
            if (e) {
                e->accept(this);
            }
        }
    }

    void visit(AssignExpression* expr) override
    {
        snap_node(expr);
        if (expr->lhs) {
            expr->lhs->accept(this);
        }
        if (expr->rhs) {
            expr->rhs->accept(this);
        }
    }

    void visit(BinaryExpression* expr) override
    {
        snap_node(expr);
        if (expr->lhs) {
            expr->lhs->accept(this);
        }
        if (expr->rhs) {
            expr->rhs->accept(this);
        }
    }

    void visit(ComparisonExpression* expr) override
    {
        snap_node(expr);
        if (expr->lhs) {
            expr->lhs->accept(this);
        }
        if (expr->rhs) {
            expr->rhs->accept(this);
        }
    }

    void visit(LogicalExpression* expr) override
    {
        snap_node(expr);
        if (expr->lhs) {
            expr->lhs->accept(this);
        }
        if (expr->rhs) {
            expr->rhs->accept(this);
        }
    }

    void visit(UnaryExpression* expr) override
    {
        snap_node(expr);
        if (expr->rhs) {
            expr->rhs->accept(this);
        }
    }

    void visit(ConditionalExpression* expr) override
    {
        snap_node(expr);
        if (expr->test) {
            expr->test->accept(this);
        }
        if (expr->true_branch) {
            expr->true_branch->accept(this);
        }
        if (expr->false_branch) {
            expr->false_branch->accept(this);
        }
    }

    void visit(GroupingExpression* expr) override
    {
        snap_node(expr);
        if (expr->expr) {
            expr->expr->accept(this);
        }
    }

    void visit(IndexExpression* expr) override
    {
        snap_node(expr);
        if (expr->target) {
            expr->target->accept(this);
        }
        if (expr->index) {
            expr->index->accept(this);
        }
    }

    void visit(TupleLiteral* expr) override
    {
        snap_node(expr);
        for (auto* it : expr->elements) {
            if (it) {
                it->accept(this);
            }
        }
    }

    void visit(BraceInitLiteral* expr) override
    {
        snap_node(expr);
        if (expr->type.type != TokenType::INVALID) {
            // token only
        }
        for (auto& item : expr->items) {
            if (item.key) {
                item.key->accept(this);
            }
            if (item.value) {
                item.value->accept(this);
            }
        }
    }

    void visit(PathExpression* expr) override
    {
        snap_node(expr);
        for (auto* p : expr->parts) {
            if (p) {
                p->accept(this);
            }
        }
    }

    void visit(IdentifierExpression* expr) override
    {
        snap_node(expr);
        if (!idents.contains(expr)) {
            idents[expr] = IdentState{expr->decl};
        }
        for (auto* tp : expr->type_params) {
            if (tp) {
                tp->accept(this);
            }
        }
    }

    void visit(CallExpression* expr) override
    {
        snap_node(expr);
        if (!calls.contains(expr)) {
            calls[expr] = CallState{expr->inferred_type_args, expr->intrinsic_id, expr->resolved_func, expr->resolved_provider};
        }
        if (expr->expr) {
            expr->expr->accept(this);
        }
        for (auto* a : expr->args) {
            if (a) {
                a->accept(this);
            }
        }
    }

    void visit(CastExpression* expr) override
    {
        snap_node(expr);
        if (expr->expr) {
            expr->expr->accept(this);
        }
        if (expr->target_type) {
            expr->target_type->accept(this);
        }
    }

    void visit(LiteralExpression* expr) override { snap_node(expr); }
    void visit(FStringExpression* expr) override
    {
        snap_node(expr);
        for (auto* e : expr->expressions) {
            if (e) {
                e->accept(this);
            }
        }
    }
    void visit(EmptyExpression* expr) override { snap_node(expr); }
    void visit(EmptyStatement* stmt) override { snap_node(stmt); }

    void visit(LambdaExpression* expr) override
    {
        snap_node(expr);
        if (!lambdas.contains(expr)) {
            lambdas[expr] = LambdaState{expr->captures, expr->compiled_func_index};
        }
        for (auto* p : expr->params) {
            if (p) {
                p->accept(this);
            }
        }
        if (expr->return_type) {
            expr->return_type->accept(this);
        }
        if (expr->body) {
            expr->body->accept(this);
        }
    }

    void restore()
    {
        for (auto& [node, st] : nodes) {
            node->type_id_ = st.type_id;
            node->is_const_ = st.is_const;
            node->env_ = st.env;
        }
        for (auto& [expr, st] : calls) {
            expr->inferred_type_args = st.inferred_type_args;
            expr->intrinsic_id = st.intrinsic_id;
            expr->resolved_func = st.resolved_func;
            expr->resolved_provider = st.resolved_provider;
        }
        for (auto& [expr, st] : idents) {
            expr->decl = st.decl;
        }
        for (auto& [expr, st] : type_exprs) {
            expr->qualified_name = st.qualified_name;
        }
        for (auto& [expr, st] : lambdas) {
            expr->captures = st.captures;
            expr->compiled_func_index = st.compiled_func_index;
        }
    }
};

struct GenericAstRestoreScope {
    GenericAstSnapshot snapshot;
    explicit GenericAstRestoreScope(FunctionDefinition* func)
    {
        if (func) {
            func->accept(&snapshot);
        }
    }
    ~GenericAstRestoreScope()
    {
        snapshot.restore();
    }
};

} // namespace

std::optional<Runtime::GenericInstantiation> Runtime::ensure_generic_instantiation(
    Script* defining_script,
    BytecodeModule* defining_module,
    FunctionDefinition* generic_func,
    const Vector<TypeID>& type_args,
    String* error_out)
{
    if (!defining_script) {
        if (error_out) {
            *error_out = "invalid generic instantiation: null defining script";
        }
        return std::nullopt;
    }

    std::unique_ptr<Script> template_holder;
    Script* resolution_script = defining_script;
    FunctionDefinition* resolved_generic_func = generic_func;
    String generic_name = generic_func ? generic_func->identifier() : String{};
    if (!resolved_generic_func) {
        if (auto* materialized = materialize_generic_function_template(defining_script, generic_name, template_holder, nullptr)) {
            resolved_generic_func = materialized;
            resolution_script = template_holder.get();
        }
    }

    if (!resolved_generic_func) {
        if (error_out) {
            *error_out = "invalid generic instantiation: null generic function";
        }
        return std::nullopt;
    }

    if (type_args.size() != resolved_generic_func->type_params.size()) {
        if (error_out) {
            *error_out = fmt::format("Type argument count mismatch for generic function '{}'", resolved_generic_func->identifier());
        }
        return std::nullopt;
    }

    String mangled_name;
    mangled_name.reserve(128);
    absl::StrAppend(&mangled_name, resolved_generic_func->identifier(), "!(");
    for (size_t i = 0; i < type_args.size(); ++i) {
        if (i > 0) {
            absl::StrAppend(&mangled_name, ",");
        }
        absl::StrAppend(&mangled_name, type_name(type_args[i]));
    }
    absl::StrAppend(&mangled_name, ")");

    String qualified_name = String(defining_script->name()) + "." + mangled_name;

    BytecodeModule* module = defining_module;
    if (!module) {
        module = get_or_compile_module(defining_script);
        if (!module) {
            if (error_out) {
                *error_out = fmt::format("Failed to compile defining module '{}'", defining_script->name());
            }
            return std::nullopt;
        }
    }

    bool runtime_managed = false;
    if (auto mit = bytecode_cache_.find(defining_script); mit != bytecode_cache_.end()) {
        runtime_managed = (mit->second.get() == module);
    }

    if (runtime_managed) {
        InstantiationKey key{String(defining_script->name()), resolved_generic_func->identifier(), type_args};
        auto it = instantiation_cache_.find(key);
        if (it != instantiation_cache_.end()) {
            return it->second;
        }
    }

    // Native generic declarations: require a specialization registered under the mangled name.
    for (const auto& ann : resolved_generic_func->annotations_) {
        if (ann.name.loc.view() == "native") {
            if (find_external_function(qualified_name) == UINT32_MAX) {
                if (error_out) {
                    *error_out = fmt::format(
                        "Missing native implementation for '{}' (no specialization and no base implementation)",
                        qualified_name);
                }
                return std::nullopt;
            }

            GenericInstantiation rec;
            rec.mangled_name = mangled_name;
            rec.func_idx = UINT32_MAX;
            rec.is_native = true;
            if (runtime_managed) {
                InstantiationKey key{String(defining_script->name()), resolved_generic_func->identifier(), type_args};
                instantiation_cache_[key] = rec;
            }
            return rec;
        }
    }

    // If already present in module (e.g., previous run but cache cleared), just register externally and cache.
    uint32_t existing_idx = module->get_function_index(mangled_name);
    if (existing_idx != UINT32_MAX) {
        if (runtime_managed && find_external_function(qualified_name) == UINT32_MAX) {
            ExternalFunction ext;
            ext.qualified_name = nw::kernel::strings().intern(qualified_name);
            ext.script_module = module;
            ext.func_idx = existing_idx;
            register_external_function(std::move(ext));
        }

        GenericInstantiation rec;
        rec.mangled_name = mangled_name;
        rec.func_idx = existing_idx;
        rec.is_native = false;
        if (runtime_managed) {
            InstantiationKey key{String(defining_script->name()), resolved_generic_func->identifier(), type_args};
            instantiation_cache_[key] = rec;
        }
        return rec;
    }

    GenericAstRestoreScope restore_scope(resolved_generic_func);

    absl::flat_hash_map<String, TypeID> substitutions;
    substitutions.reserve(resolved_generic_func->type_params.size());
    for (size_t i = 0; i < resolved_generic_func->type_params.size(); ++i) {
        substitutions[resolved_generic_func->type_params[i]] = type_args[i];
    }

    // Resolve the generic function under concrete substitutions.
    AstResolver resolver(resolution_script, resolution_script->ctx());
    resolver.env_stack_.clear();
    resolver.env_stack_.push_back(resolution_script->exports());
    resolver.global_decls_.clear();
    for (const auto& [name, exp] : resolution_script->exports()) {
        if (exp.decl) {
            resolver.global_decls_[name] = exp.decl;
        }
    }

    // Record providers for local decls and selective imports so generic calls can
    // be instantiated across module boundaries.
    for (auto* node : resolution_script->ast().decls) {
        if (!node) {
            continue;
        }
        if (auto* decl = dynamic_cast<Declaration*>(node)) {
            resolver.record_decl_provider(decl, resolution_script);
        }

        if (auto* import_decl = dynamic_cast<SelectiveImportDecl*>(node)) {
            if (!import_decl->loaded_module) {
                continue;
            }
            auto exports = import_decl->loaded_module->exports();
            for (const auto& sym : import_decl->imported_symbols) {
                String key(sym.loc.view());
                auto exp_ptr = exports.find(key);
                if (exp_ptr && exp_ptr->decl) {
                    resolver.record_decl_provider(exp_ptr->decl, import_decl->loaded_module);
                }
            }
        }
    }

    resolver.set_type_substitutions(substitutions);

    TypeResolver type_resolver{resolver};
    resolver.set_active_visitor(&type_resolver);
    type_resolver.visit(resolved_generic_func);
    resolver.set_active_visitor(nullptr);

    if (resolved_generic_func->block) {
        Validator validator{resolver};
        validator.visit(resolved_generic_func);
    }

    if (resolution_script->errors() > 0) {
        if (error_out) {
            *error_out = fmt::format("Generic instantiation '{}' produced errors", qualified_name);
        }
        return std::nullopt;
    }

    auto* compiled = new CompiledFunction(mangled_name);
    compiled->param_count = static_cast<uint8_t>(resolved_generic_func->params.size());
    compiled->register_count = compiled->param_count;
    compiled->return_type = resolved_generic_func->type_id_;
    compiled->source_ast = (resolution_script == defining_script) ? resolved_generic_func : nullptr;

    uint32_t func_idx = static_cast<uint32_t>(module->functions.size());
    module->add_function(compiled);

    AstCompiler compiler(resolution_script, module, this, diagnostic_context_);
    if (!compiler.compile_instantiated(compiled, resolved_generic_func)) {
        if (error_out) {
            *error_out = fmt::format("Failed to compile instantiated function '{}': {}", qualified_name, compiler.error_message_);
        }
        return std::nullopt;
    }

    // Register instantiated function as external for cross-module calls.
    if (runtime_managed) {
        ExternalFunction ext;
        ext.qualified_name = nw::kernel::strings().intern(qualified_name);
        ext.script_module = module;
        ext.func_idx = func_idx;
        register_external_function(std::move(ext));

        // Resolve any new external refs introduced by this instantiation.
        if (!module->resolve_external_refs(this)) {
            if (error_out) {
                *error_out = fmt::format("Failed to resolve external refs after instantiating '{}'", qualified_name);
            }
            return std::nullopt;
        }
    }

    GenericInstantiation rec;
    rec.mangled_name = mangled_name;
    rec.func_idx = func_idx;
    rec.is_native = false;
    if (runtime_managed) {
        InstantiationKey key{String(defining_script->name()), resolved_generic_func->identifier(), type_args};
        instantiation_cache_[key] = rec;
    }
    return rec;
}

std::optional<Runtime::GenericInstantiation> Runtime::ensure_generic_instantiation_by_name(
    Script* defining_script,
    BytecodeModule* defining_module,
    StringView generic_name,
    const Vector<TypeID>& type_args,
    String* error_out)
{
    if (!defining_script) {
        if (error_out) {
            *error_out = "invalid generic instantiation: null defining script";
        }
        return std::nullopt;
    }

    std::unique_ptr<Script> template_holder;
    String materialize_error;
    auto* generic_func = materialize_generic_function_template(
        defining_script,
        generic_name,
        template_holder,
        error_out ? error_out : &materialize_error);

    if (!generic_func) {
        return std::nullopt;
    }

    return ensure_generic_instantiation(
        defining_script,
        defining_module,
        generic_func,
        type_args,
        error_out);
}

bool Runtime::materialize_generic_function_definition(
    Script* defining_script,
    StringView function_name,
    std::unique_ptr<Script>& template_holder,
    const FunctionDefinition*& out_function,
    String* error_out)
{
    out_function = nullptr;
    auto* fn = materialize_generic_function_template(
        defining_script,
        function_name,
        template_holder,
        error_out);
    if (!fn) {
        return false;
    }
    out_function = fn;
    return true;
}

TypeID Runtime::get_or_instantiate_type(TypeID generic_type_id, const Vector<TypeID>& type_args)
{
    const Type* generic_type = get_type(generic_type_id);
    ENSURE_OR_RETURN_VALUE(invalid_type_id, generic_type, "invalid type id {}", generic_type_id.value);
    auto name = generic_type->name.view();

    TypeInstantiationKey key{generic_type_id, type_args};

    auto it = type_instantiation_cache_.find(key);
    if (it != type_instantiation_cache_.end()) { return it->second; }

    String mangled_name;
    mangled_name.reserve(200);
    absl::StrAppend(&mangled_name, name, "!(");
    for (size_t i = 0; i < type_args.size(); ++i) {
        if (i > 0) { absl::StrAppend(&mangled_name, ", "); }
        absl::StrAppend(&mangled_name, type_name(type_args[i]));
    }
    absl::StrAppend(&mangled_name, ")");

    if (generic_type->type_kind == TK_sum) {
        auto id = generic_type->type_params[0].as<SumID>();
        const SumDef* generic_def = type_table_.get(id);
        ENSURE_OR_RETURN_VALUE(invalid_type_id, generic_def, "invalid sum id {}", id.value);
        ENSURE_OR_RETURN_VALUE(invalid_type_id, type_args.size() == generic_def->generic_param_count, "Type argument count mismatch for generic type '{}'", name);

        uint32_t variant_count = generic_def->variant_count;
        void* mem = allocator()->allocate(sizeof(SumDef), alignof(SumDef));
        auto* new_def = new (mem) SumDef{};
        new_def->variant_count = variant_count;
        new_def->decl = nullptr;

        void* variants_mem = allocator()->allocate(sizeof(VariantDef) * variant_count, alignof(VariantDef));
        new_def->variants = static_cast<VariantDef*>(variants_mem);

        uint32_t tag_size = sizeof(uint32_t);
        uint32_t tag_alignment = alignof(uint32_t);
        uint32_t max_payload_size = 0;
        uint32_t max_payload_alignment = tag_alignment;

        for (uint32_t i = 0; i < variant_count; ++i) {
            const auto& src_variant = generic_def->variants[i];
            auto& dst_variant = new_def->variants[i];

            dst_variant.name = src_variant.name;
            dst_variant.tag_value = src_variant.tag_value;

            if (src_variant.payload_type == invalid_type_id) {
                dst_variant.payload_type = invalid_type_id;
                dst_variant.payload_offset = 0;
                dst_variant.generic_param_index = -1;
            } else {
                TypeID payload_type = src_variant.payload_type;

                if (src_variant.generic_param_index >= 0
                    && static_cast<size_t>(src_variant.generic_param_index) < type_args.size()) {
                    payload_type = type_args[src_variant.generic_param_index];
                }

                dst_variant.payload_type = payload_type;
                dst_variant.generic_param_index = src_variant.generic_param_index;
                const Type* payload_type_obj = get_type(payload_type);
                if (payload_type_obj) {
                    uint32_t payload_size = payload_type_obj->size;
                    uint32_t payload_alignment = payload_type_obj->alignment;
                    if (payload_alignment == 0) { payload_alignment = 1; }
                    max_payload_size = std::max(max_payload_size, payload_size);
                    max_payload_alignment = std::max(max_payload_alignment, payload_alignment);
                }
            }
        }

        new_def->tag_offset = 0;
        uint32_t union_offset = (tag_size + max_payload_alignment - 1) & ~(max_payload_alignment - 1);
        new_def->union_offset = union_offset;
        new_def->union_size = max_payload_size;
        new_def->alignment = max_payload_alignment;
        new_def->size = union_offset + max_payload_size;

        for (uint32_t i = 0; i < variant_count; ++i) {
            // Payload offsets are relative to the start of the union.
            new_def->variants[i].payload_offset = 0;
        }

        Type instantiated_type{
            .name = nw::kernel::strings().intern(mangled_name),
            .type_params = {type_table_.add_sum(new_def), TypeParam{}},
            .type_kind = TK_sum,
            .size = new_def->size,
            .alignment = new_def->alignment,
        };

        TypeID result = register_type(instantiated_type.name.view(), instantiated_type);
        type_instantiation_cache_[key] = result;
        return result;
    } else if (generic_type->type_kind == TK_struct) {
        auto id = generic_type->type_params[0].as<StructID>();
        const StructDef* generic_def = type_table_.get(id);
        ENSURE_OR_RETURN_VALUE(invalid_type_id, generic_def, "invalid struct id {}", id.value);
        ENSURE_OR_RETURN_VALUE(invalid_type_id, type_args.size() == generic_def->generic_param_count, "type argument count mismatch for generic type '{}'", name);

        uint32_t field_count = generic_def->field_count;
        void* mem = allocator()->allocate(sizeof(StructDef), alignof(StructDef));
        auto* new_def = new (mem) StructDef{};
        new_def->field_count = field_count;
        new_def->decl = nullptr;

        uint32_t struct_alignment = 1;
        uint32_t offset = 0;

        if (field_count > 0) {
            void* fields_mem = allocator()->allocate(sizeof(FieldDef) * field_count, alignof(FieldDef));
            new_def->fields = static_cast<FieldDef*>(fields_mem);

            for (uint32_t i = 0; i < field_count; ++i) {
                const auto& src_field = generic_def->fields[i];
                auto& dst_field = new_def->fields[i];

                dst_field.name = src_field.name;
                dst_field.generic_param_index = src_field.generic_param_index;

                TypeID field_type = src_field.type_id;
                if (src_field.generic_param_index >= 0
                    && static_cast<size_t>(src_field.generic_param_index) < type_args.size()) {
                    field_type = type_args[src_field.generic_param_index];
                }

                dst_field.type_id = field_type;

                const Type* field_type_obj = get_type(field_type);
                uint32_t field_size = 0;
                uint32_t field_alignment = 1;
                if (field_type_obj) {
                    field_size = field_type_obj->size;
                    field_alignment = field_type_obj->alignment;
                    if (field_alignment == 0) { field_alignment = 1; }
                }

                struct_alignment = std::max(struct_alignment, field_alignment);
                offset = (offset + field_alignment - 1) & ~(field_alignment - 1);
                dst_field.offset = offset;
                offset += field_size;
            }
        }

        new_def->size = (offset + struct_alignment - 1) & ~(struct_alignment - 1);
        new_def->alignment = struct_alignment;

        Type instantiated_type{
            .name = nw::kernel::strings().intern(mangled_name),
            .type_params = {type_table_.add_struct(new_def), TypeParam{}},
            .type_kind = TK_struct,
            .size = new_def->size,
            .alignment = new_def->alignment,
        };

        TypeID result = register_type(instantiated_type.name.view(), instantiated_type);
        type_instantiation_cache_[key] = result;
        return result;
    }

    return invalid_type_id;
}

// == Config Loading ===========================================================

void Runtime::set_user_prelude(StringView module_path)
{
    if (module_path.empty()) {
        user_prelude_ = nullptr;
        return;
    }
    user_prelude_ = get_module(module_path);
    if (!user_prelude_) {
        LOG_F(ERROR, "[config] Failed to load user prelude module '{}'", module_path);
    }
}

Value Runtime::call_closure(ScriptClosure closure, const Vector<Value>& args)
{
    if (!closure) {
        return Value{};
    }

    Closure* cl = get_closure(closure.ptr);
    if (!cl) {
        LOG_F(ERROR, "[config] call_closure: invalid closure heap pointer");
        return Value{};
    }

    return vm_->execute_closure(cl, args, default_gas_limit);
}

Value Runtime::load_config_value(StringView path, StringView prelude_module)
{
    Script* prev_prelude = user_prelude_;

    if (!prelude_module.empty()) {
        set_user_prelude(prelude_module);
    }

    for (const auto& search_path : module_paths_) {
        auto full_path = search_path / module_name_to_path(path);
        if (std::filesystem::exists(full_path)) {
            std::ifstream file(full_path);
            if (file.is_open()) {
                std::string content((std::istreambuf_iterator<char>(file)),
                    std::istreambuf_iterator<char>());

                // Trim trailing whitespace from content before appending semicolon
                while (!content.empty() && (content.back() == '\n' || content.back() == '\r' || content.back() == ' ' || content.back() == '\t')) {
                    content.pop_back();
                }
                String wrapped = fmt::format("var __config = {};", content);
                String config_module = fmt::format("__config.{}", path);

                auto* script = load_module_from_source(config_module, wrapped);
                if (script && script->errors() == 0) {
                    auto* module = get_or_compile_module(script);
                    if (module && module->global_count > 0) {
                        if (!prelude_module.empty()) {
                            user_prelude_ = prev_prelude;
                        }
                        return module->globals[0];
                    }
                } else if (script) {
                    LOG_F(ERROR, "[config] Config file '{}' has {} errors",
                        path, script->errors());
                }

                if (!prelude_module.empty()) {
                    user_prelude_ = prev_prelude;
                }
                return Value{};
            }
        }
    }

    LOG_F(ERROR, "[config] Config file '{}' not found in module paths", path);
    if (!prelude_module.empty()) {
        user_prelude_ = prev_prelude;
    }
    return Value{};
}

} // namespace nw::smalls
