#pragma once

#include "../kernel/Kernel.hpp"
#include "../objects/ObjectHandle.hpp"
#include "../resources/ResourceManager.hpp"
#include "../util/HandlePool.hpp"
#include "Array.hpp"
#include "Context.hpp"
#include "GarbageCollector.hpp"
#include "ScriptHeap.hpp"
#include "SourceLocation.hpp"
#include "Token.hpp"
#include "types.hpp"

#include <fmt/format.h>

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>

namespace nw::smalls {

struct BytecodeModule;

// Forward declarations
struct BraceInitLiteral;
struct BytecodeModule;
struct CompiledFunction;
struct FunctionDefinition;
struct GCRootVisitor;
struct IArray;
struct ModuleBuilder;
struct NativeStructLayout;
struct Script;
struct Token;
struct VirtualMachine;

struct StringRepr {
    HeapPtr backing;
    uint32_t offset;
    uint32_t length;
};

// == Value ===================================================================
// ============================================================================

/// Storage location for a Value
enum class ValueStorage : uint8_t {
    immediate, // Primitives: int, float, bool, ObjectHandle
    heap,      // HeapPtr: strings, structs, arrays, maps
    stack      // stack_offset: frame-relative offset into CallFrame::stack_
};

/// Runtime value representation
struct Value {
    TypeID type_id = invalid_type_id;
    ValueStorage storage = ValueStorage::immediate;

    union {
        bool bval;             // bool primitives
        int32_t ival;          // int primitives
        float fval;            // float primitives
        ObjectHandle oval;     // Game entity IDs (creatures, items, etc.)
        HeapPtr hptr;          // Script heap: strings, structs, arrays
        uint32_t stack_offset; // Frame stack: offset into CallFrame::stack_
    } data = {.ival = 0};

    Value() = default;
    explicit Value(TypeID tid)
        : type_id(tid)
    {
    }

    static Value make_bool(bool v);
    static Value make_int(int32_t v);
    static Value make_float(float v);
    static Value make_string(HeapPtr ptr);
    static Value make_object(ObjectHandle obj);
    static Value make_heap(HeapPtr ptr, TypeID tid);
    static Value make_stack(uint32_t offset, TypeID tid);
};

// == Value Hashing/Equality ==================================================

struct ValueHash {
    size_t operator()(const Value& v) const noexcept;
};

struct ValueEq {
    bool operator()(const Value& a, const Value& b) const noexcept;
};

// == ScriptClosure ===========================================================

/// C++ representation of a smalls closure reference for use in native struct fields.
/// 4 bytes, POD. Zero = no callback.
struct ScriptClosure {
    HeapPtr ptr{0};
    explicit operator bool() const { return ptr.value != 0; }
};

/// C++ representation of a smalls string field for use in native struct fields.
/// 4 bytes, POD. Matches the HeapPtr layout that smalls uses for string struct fields.
struct ScriptString {
    HeapPtr ptr{0};
    explicit operator bool() const { return ptr.value != 0; }
    StringView view(const Runtime& rt) const;
};

// == Closure Support =========================================================

/// Upvalue - A box that references a captured variable
/// When "open": location points to a register in the enclosing frame
/// When "closed": location points to &closed, value has been moved to heap
struct Upvalue {
    HeapPtr heap_ptr{0};
    Value* location = nullptr; // Points to register (open) or &closed (closed)
    Value closed;              // Storage when upvalue is closed
    Upvalue* next = nullptr;   // Linked list for frame's open upvalues

    bool is_open() const noexcept { return location != &closed; }
};

struct CompiledFunction;
struct BytecodeModule;

/// Closure - A function + captured environment
/// Stored on the heap, referenced by Value with TK_function type
struct Closure {
    const CompiledFunction* function = nullptr;
    const BytecodeModule* module = nullptr;
    Vector<Upvalue*> upvalues;

    Closure() = default;
    Closure(const CompiledFunction* func, const BytecodeModule* mod)
        : function(func)
        , module(mod)
    {
    }
};

// == Operator Registry =======================================================

/// Function signature for binary operator execution
using BinaryOpExecutor = std::function<Value(const Value&, const Value&)>;

/// Function signature for unary operator execution
using UnaryOpExecutor = std::function<Value(const Value&)>;

/// Reference to a script-defined function (persists after AST discarded)
struct ScriptFunctionRef {
    String module_path;   // e.g., "core.math"
    String function_name; // e.g., "add_point"

    bool operator==(const ScriptFunctionRef& other) const
    {
        return module_path == other.module_path && function_name == other.function_name;
    }
};

/// Metadata for a binary operator overload
struct BinaryOpOverload {
    TypeID lhs_type;
    TypeID rhs_type;
    TypeID result_type;
    BinaryOpExecutor execute;                     // Native operator implementation
    std::optional<ScriptFunctionRef> script_func; // Script operator (if defined)
};

/// Metadata for a unary operator overload
struct UnaryOpOverload {
    TypeID operand_type;
    TypeID result_type;
    UnaryOpExecutor execute;                      // Native operator implementation
    std::optional<ScriptFunctionRef> script_func; // Script operator (if defined)
};

// == Handle System ===========================================================

/// Ownership mode for native handles
enum class OwnershipMode : uint8_t {
    VM_OWNED,     // Created by scripts, GC can free
    ENGINE_OWNED, // Engine manages lifetime, never GC'd
    BORROWED,     // Temporary reference, never GC'd
};

/// Entry in handle registry tracking a single handle
struct HandleEntry {
    HeapPtr vm_value;   // VM heap Value containing this handle
    OwnershipMode mode; // Ownership mode for this handle
};

// == MapInstance =============================================================

struct MapInstance {
    TypeID key_type;
    TypeID value_type;
    uint32_t iteration_count = 0;
    absl::flat_hash_map<Value, Value, ValueHash, ValueEq> data;

    MapInstance(TypeID k, TypeID v)
        : key_type(k)
        , value_type(v)
    {
    }

    ~MapInstance() = default;
};

struct MapIterator {
    HeapPtr map_ptr;
    absl::flat_hash_map<Value, Value, ValueHash, ValueEq>::iterator current;
    absl::flat_hash_map<Value, Value, ValueHash, ValueEq>::iterator end;
};

// == Module Interface Metadata ===============================================
// ============================================================================

/// Metadata for a native function parameter
struct ParamMetadata {
    String name;
    TypeID type_id;
};

/// Metadata for a native function
struct FunctionMetadata {
    String name;
    TypeID return_type;
    Vector<ParamMetadata> params;
};

/// Interface definition for a native module (for .smalli generation)
struct ModuleInterface {
    String module_path;
    Vector<String> opaque_types;
    Vector<FunctionMetadata> functions;
};

/// Signature for native function wrappers called by the VM
using NativeFunctionWrapper = std::function<Value(Runtime*, const Value*, uint8_t)>;

/// Metadata and wrapper for a registered native function
struct NativeFunction {
    String name;
    NativeFunctionWrapper wrapper;
    FunctionMetadata metadata;
};

/// Unified external function (native or cross-module script)
/// Used by CALLEXT opcode to call functions outside the current module
struct ExternalFunction {
    InternedString qualified_name;
    FunctionMetadata metadata;

    // If script_module != nullptr, this is a cross-module script function
    // Otherwise it's a native function
    const BytecodeModule* script_module = nullptr;
    uint32_t func_idx = 0;

    // Only valid for native functions (script_module == nullptr)
    NativeFunctionWrapper native_wrapper;

    bool is_native() const { return script_module == nullptr; }
};

// == Execution Result ========================================================

/// Result of script execution with error information
struct ExecutionResult {
    Value value;
    bool failed;
    String error_message;
    String stack_trace;
    String error_module;
    SourceLocation error_location;
    String error_snippet;

    bool ok() const { return !failed; }
    explicit operator bool() const { return !failed; }
};

// == VM ======================================================================
// ============================================================================

struct Runtime : public nw::kernel::Service {
    const static std::type_index type_index;

    explicit Runtime(MemoryResource* scope);
    ~Runtime(); // Needed for unique_ptr dtor

    virtual void initialize(nw::kernel::ServiceInitTime time) override;
    void register_internal_types();
    virtual nlohmann::json stats() const override;

    static constexpr uint64_t default_gas_limit = 100000;

    /// Executes a script function
    /// @param script The script to execute
    /// @param function_name The function to call (default: "main")
    /// @param args Arguments to pass
    /// @return ExecutionResult with value and error information
    ExecutionResult execute_script(Script* script, StringView function_name, const Vector<Value>& args = {},
        uint64_t gas_limit = default_gas_limit);

    /// Executes a script function by script path
    ExecutionResult execute_script(StringView path, StringView function_name, const Vector<Value>& args = {},
        uint64_t gas_limit = default_gas_limit);

    /// Resets VM error state for fresh execution
    void reset_error();
    void fail(StringView msg);

    /// Gets or compiles bytecode for a script
    BytecodeModule* get_or_compile_module(Script* script);

    // -- Type System ---------------------------------------------------------

    /// Accessors for cached primitive type IDs
    TypeID void_type() const { return void_id_; }
    TypeID int_type() const { return int_id_; }
    TypeID float_type() const { return float_id_; }
    TypeID string_type() const { return string_id_; }
    TypeID bool_type() const { return bool_id_; }
    TypeID any_type() const { return any_id_; }
    TypeID vec3_type() const { return vec3_id_; }
    TypeID object_type() const { return object_id_; }
    TypeID any_array_type() const { return any_array_id_; }
    TypeID any_map_type() const { return any_map_id_; }

    /// Registers a type in the runtime type table
    /// @param name Fully-qualified type name (e.g., "core.creature.attack.AttackResult")
    /// @param type The type definition
    /// @return The TypeID for the registered type
    TypeID register_type(StringView name, Type type);

    /// Gets a type by ID
    /// @return The type definition, or nullptr if not found
    const Type* get_type(TypeID id) const;

    /// Gets a type by fully-qualified name
    /// @return The type definition, or nullptr if not found
    const Type* get_type(StringView name) const;

    /// Gets the type ID for a fully-qualified name
    /// @param name The type name
    /// @param define If true, creates a new type ID if not found
    /// @return The TypeID, or invalid TypeID if not found and define=false
    TypeID type_id(StringView name, bool define = false);

    /// Gets the name for a type ID
    /// @return The type name, or "<unknown>" if invalid
    StringView type_name(TypeID id) const;

    // -- Type Metadata -------------------------------------------------------

    /// Gets the initialization mode for a type
    /// @param id The type ID
    /// @return The init mode (inferred from type kind if no custom metadata set)
    BraceInitType get_init_mode(TypeID id) const;

    /// Sets custom metadata for a type
    /// @param id The type ID
    /// @param metadata The metadata to set
    void set_type_metadata(TypeID id, TypeMetadata metadata);

    /// Gets metadata for a type
    /// @param id The type ID
    /// @return Pointer to metadata, or nullptr if no custom metadata set
    const TypeMetadata* get_type_metadata(TypeID id) const;

    // -- Operator Alias Metadata ---------------------------------------------

    struct OperatorAliasInfo {
        bool has_eq = false;            ///< Any eq (explicit or default structural)
        bool has_hash = false;          ///< Any hash (explicit or default structural)
        bool has_lt = false;            ///< Any lt (explicit or default structural)
        bool has_explicit_eq = false;   ///< From [[operator(eq)]]
        bool has_explicit_hash = false; ///< From [[operator(hash)]]
        bool has_explicit_lt = false;   ///< From [[operator(lt)]]
    };

    /// Records operator alias metadata for a type.
    /// @param is_explicit  true for [[operator(...)]] annotations, false for synthesized defaults
    void register_operator_alias_info(TypeID type_id, StringView op_name, bool is_explicit = true);

    /// Gets operator alias metadata for a type
    OperatorAliasInfo get_operator_alias_info(TypeID type_id) const;

    /// Registers default structural eq/lt/hash for all eligible struct types.
    /// Must be called after all explicit [[operator(...)]] aliases are registered
    /// and before validation. Skips types that already have explicit operators.
    void register_default_struct_operators();

    /// Returns true if a type supports equality comparison against itself.
    bool supports_equality_type(TypeID type) const;

    /// Returns true if a type has a hash implementation.
    bool supports_hash_type(TypeID type) const;

    /// Returns true if a type is valid as a map key.
    /// Policy: int/string plus newtypes/aliases whose underlying type is int/string.
    bool supports_map_key_type(TypeID type) const;

    // -- Type Checking -------------------------------------------------------

    /// Checks if a binary operator can be applied to two types
    /// @return The result type, or invalid_type_id if operation is not valid
    TypeID type_check_binary_op(Token op, TypeID lhs, TypeID rhs) const;

    /// Checks if rhs type can be implicitly converted to lhs type
    bool is_type_convertible(TypeID lhs, TypeID rhs) const;

    // -- Operator Registry ---------------------------------------------------

    /// Registers a binary operator overload
    /// @param op Operator token type
    /// @param lhs Left-hand side type
    /// @param rhs Right-hand side type
    /// @param result Result type
    /// @param executor Function to execute the operation
    void register_binary_op(TokenType op, TypeID lhs, TypeID rhs, TypeID result, BinaryOpExecutor executor);

    /// Registers a unary operator overload
    /// @param op Operator token type
    /// @param operand Operand type
    /// @param result Result type
    /// @param executor Function to execute the operation
    void register_unary_op(TokenType op, TypeID operand, TypeID result, UnaryOpExecutor executor);

    /// Registers a script-defined binary operator
    /// @param op Operator token type (or INVALID for special operators like "str")
    /// @param lhs Left-hand side type
    /// @param rhs Right-hand side type
    /// @param result Result type
    /// @param module_path Module containing the operator function
    /// @param function_name Name of the operator function
    void register_script_binary_op(TokenType op, TypeID lhs, TypeID rhs, TypeID result,
        StringView module_path, StringView function_name);

    /// Registers a script-defined unary operator
    /// @param op Operator token type (or INVALID for special operators like "str")
    /// @param operand Operand type
    /// @param result Result type
    /// @param module_path Module containing the operator function
    /// @param function_name Name of the operator function
    void register_script_unary_op(TokenType op, TypeID operand, TypeID result,
        StringView module_path, StringView function_name);

    /// Registers a str operator for a type
    void register_str_op(TypeID operand, StringView module_path, StringView function_name);

    /// Registers a hash operator for a type (script-defined)
    void register_hash_op(TypeID operand, StringView module_path, StringView function_name);

    /// Registers a native hash operator for a type (e.g. default structural hash)
    void register_native_hash_op(TypeID operand, std::function<Value(const Value&)> executor);

    /// Looks up a script-defined binary operator
    const ScriptFunctionRef* find_script_binary_op(TokenType op, TypeID lhs, TypeID rhs) const;

    /// Looks up a script-defined unary operator
    const ScriptFunctionRef* find_script_unary_op(TokenType op, TypeID operand) const;

    /// Looks up a str operator for a type
    const ScriptFunctionRef* find_str_op(TypeID type) const;

    /// Looks up the script-defined hash operator for a type (null if native-only)
    const ScriptFunctionRef* find_hash_op(TypeID type) const;

    /// Returns true if any hash operator (native or script) is registered for a type
    bool has_hash_op(TypeID type) const;

    /// Executes a binary operation on two Values using registered operators
    /// @return Result value, or Value with invalid_type_id on failure
    Value execute_binary_op(TokenType op, const Value& lhs, const Value& rhs);

    /// Executes a unary operation on a Value using registered operators
    /// @return Result value, or Value with invalid_type_id on failure
    Value execute_unary_op(TokenType op, const Value& val);

    /// Executes a str operator on a Value
    Value execute_str_op(const Value& val);

    /// Executes a hash operator on a Value
    Value execute_hash_op(const Value& val);

    // -- Module System -------------------------------------------------------

    /// Creates a builder for registering a C++/internal module
    ModuleBuilder module(StringView path);

    /// Loads a module by path from disk
    Script* load_module(StringView path);

    /// Loads a module from source code
    Script* load_module_from_source(StringView path, StringView source);

    /// Gets a cached module, or loads it if not cached
    Script* get_module(StringView path);

    /// Converts a filesystem path to a module name
    /// @param filepath Path like "core/math/vector.smalls"
    /// @param base_path Base path to make the filepath relative to (empty = use as-is)
    /// @return Module name like "core.math.vector", or empty string on failure
    String path_to_module_name(const std::filesystem::path& filepath,
        const std::filesystem::path& base_path = {}) const;

    /// Converts a module name to a filesystem path
    /// @param module_name Module name like "core.math.vector"
    /// @param extension File extension to append (default: ".smalls")
    /// @return Filesystem path like "core/math/vector.smalls"
    std::filesystem::path module_name_to_path(StringView module_name,
        StringView extension = ".smalls") const;

    /// Adds a path to the module search path
    void add_module_path(const std::filesystem::path& path);

    /// Gets the list of module search paths
    const Vector<std::filesystem::path>& module_paths() const { return module_paths_; }

    /// Core prelude module (loaded once if available)
    Script* core_prelude();
    Script* core_test();

    // -- Generic Function Instantiation --------------------------------------

    struct GenericInstantiation {
        String mangled_name;
        uint32_t func_idx = UINT32_MAX; // index in defining script module, if script function
        bool is_native = false;
    };

    /// Ensures a specialized instantiation exists in the defining module.
    /// For script generics, compiles the specialized function into the defining module.
    /// For native generics, requires a native specialization registered under the mangled name.
    /// If defining_module is non-null, instantiates into that module.
    std::optional<GenericInstantiation> ensure_generic_instantiation(
        Script* defining_script,
        BytecodeModule* defining_module,
        FunctionDefinition* generic_func,
        const Vector<TypeID>& type_args,
        String* error_out = nullptr);

    // -- Generic Type Instantiation -------------------------------------------

    /// Gets or creates an instantiated version of a generic type (sum type or struct)
    /// @param generic_type_id The TypeID of the generic type definition
    /// @param type_args Concrete types to substitute for type parameters
    /// @return TypeID of the instantiated type, or invalid_type_id on failure
    TypeID get_or_instantiate_type(TypeID generic_type_id, const Vector<TypeID>& type_args);

    /// Sets the diagnostic context (for LSP or custom diagnostic handling)
    void set_diagnostic_context(Context* ctx) { diagnostic_context_ = ctx; }

    /// Gets the current diagnostic context
    Context* diagnostic_context() const { return diagnostic_context_; }

    /// Sets runtime diagnostic configuration
    void set_diagnostic_config(const DiagnosticConfig& config)
    {
        diagnostic_config_ = config;
        if (diagnostic_context_) {
            diagnostic_context_->config = config;
        }
    }

    /// Gets runtime diagnostic configuration
    const DiagnosticConfig& diagnostic_config() const { return diagnostic_config_; }

    /// Get a source line for a module
    /// @param module_name Module path (e.g., "core.math")
    /// @param line 1-based line index
    /// @param out_line Output view of the line
    /// @return true if the line could be resolved
    bool get_source_line(StringView module_name, size_t line, StringView& out_line);

    // -- Native Module Interfaces --------------------------------------------

    /// Gets all registered native module interfaces
    const Vector<ModuleInterface>& native_interfaces() const { return native_interfaces_; }

    /// Gets the native module interface for a given module path
    /// @param module_path Module path (e.g., "core.combat")
    /// @return Pointer to the module interface, or nullptr if no native module with this name
    const ModuleInterface* get_native_module(StringView module_path) const;

    /// Registers a native module interface (used for .smalli generation)
    void register_native_interface(ModuleInterface interface)
    {
        native_interfaces_.push_back(std::move(interface));
    }

    // -- Native Function Registry --------------------------------------------

    /// Registers a native function and returns its index
    uint32_t register_native_function(NativeFunction func);

    /// Gets a registered native function by index
    const NativeFunction* get_native_function(uint32_t index) const;

    /// Finds a native function by name
    /// @return index or UINT32_MAX if not found
    uint32_t find_native_function(StringView name) const;

    // -- External Function Registry ------------------------------------------

    /// Registers an external function (native or cross-module script)
    /// @return index into external_functions_
    uint32_t register_external_function(ExternalFunction func);

    /// Gets an external function by index
    const ExternalFunction* get_external_function(uint32_t index) const;

    /// Finds an external function by qualified name
    /// @return index or UINT32_MAX if not found
    uint32_t find_external_function(StringView qualified_name) const;

    // -- Core Test State ------------------------------------------------------

    void record_test_result(StringView name, bool passed);
    void reset_test_state();
    void report_test_summary();
    uint32_t test_count() const { return test_count_; }
    uint32_t test_failures() const { return test_failures_; }

    // -- Handle System -------------------------------------------------------

    /// Registers a handle type with the runtime
    /// @param name Type name for scripts (e.g., "Effect")
    /// @param runtime_type Runtime object type id (e.g., RuntimeObjectPool::TYPE_EFFECT)
    /// @return The TypeID for the registered handle type
    TypeID add_handle_type(StringView name, uint8_t runtime_type);

    /// Allocates a typed handle on the VM heap
    /// @param type_id The type ID for the handle
    /// @return HeapPtr to allocated handle, or HeapPtr{0} on failure
    /// Layout: [ObjectHeader | TypedHandle]
    HeapPtr alloc_handle(TypeID type_id);

    /// Gets a typed handle pointer from a heap pointer
    /// @param ptr Heap pointer to handle
    /// @return Pointer to typed handle, or nullptr if invalid
    TypedHandle* get_handle(HeapPtr ptr);

    /// Gets or creates the heap cell for a typed handle and sets ownership.
    HeapPtr intern_handle(TypedHandle value, OwnershipMode mode, bool force_mode = false);

    /// Registers a handle in the registry (low-level; prefer intern_handle)
    /// @param h Typed handle
    /// @param vm_value HeapPtr to the Value on VM heap
    /// @param mode Ownership mode
    void register_handle(TypedHandle h, HeapPtr vm_value, OwnershipMode mode);

    /// Looks up a handle in the registry (low-level; prefer intern_handle)
    /// @param h Typed handle
    /// @return HeapPtr to the handle, or HeapPtr{0} if not found
    HeapPtr lookup_handle(TypedHandle h) const;

    /// Updates ownership for a registered handle (low-level; prefer intern_handle)
    /// @param h Typed handle
    /// @param mode New ownership mode
    /// @return true if updated, false if handle not registered
    bool set_handle_ownership(TypedHandle h, OwnershipMode mode);

    /// Checks if a TypeID corresponds to a registered handle type
    /// @param tid TypeID to check
    /// @return true if tid is a handle type
    bool is_handle_type(TypeID tid) const;

    /// Enumerate ENGINE_OWNED/BORROWED handle heap cells as GC roots.
    /// VM_OWNED handles are intentionally excluded so they can be collected.
    void enumerate_handle_roots(GCRootVisitor& visitor);

    /// Registers a finalizer callback for a script handle type.
    /// The callback is invoked when a VM_OWNED handle heap cell is finalized.
    void register_handle_destructor(TypeID handle_type, std::function<void(TypedHandle, HeapPtr)> destructor);

    /// Checks if a registered C++ type is compatible with a declared script type
    /// Handles wildcard types (any_array, any_map) and handle type placeholders
    /// @param cpp_type TypeID from C++ native function registration
    /// @param script_type TypeID from script [[native]] declaration
    /// @return true if types are compatible
    bool native_types_compatible(TypeID cpp_type, TypeID script_type) const;

    // Global handle registry (TypedHandle -> Entry)
    absl::flat_hash_map<TypedHandle, HandleEntry> global_handle_registry_;

    // Handle destructors (TypeID -> Destructor)
    absl::flat_hash_map<TypeID, std::function<void(TypedHandle, HeapPtr)>> handle_destructors_;

    // Script handle type IDs by runtime type (TypedHandle.type)
    std::array<TypeID, 256> handle_type_to_typeid_{};

    // -- Tuple Types ---------------------------------------------------------

    /// Register or lookup a tuple type by its element types
    /// @param element_types Vector of TypeIDs for tuple elements
    /// @return TypeID for the tuple type (reuses existing if structurally equal)
    TypeID register_tuple_type(const Vector<TypeID>& element_types);

    /// Get the number of elements in a tuple type
    /// @param tuple_type TypeID of a tuple type
    /// @return Number of elements, or 0 if not a tuple type
    size_t get_tuple_element_count(TypeID tuple_type) const;

    /// Get the type of an element at a specific index in a tuple
    /// @param tuple_type TypeID of a tuple type
    /// @param index Zero-based element index
    /// @return TypeID of the element, or invalid_type_id if out of bounds or not a tuple
    TypeID get_tuple_element_type(TypeID tuple_type, size_t index) const;

    // -- Function Types ------------------------------------------------------

    /// Register or lookup a function type by its signature
    /// @param param_types Vector of TypeIDs for function parameters
    /// @param return_type Return type (invalid_type_id for void)
    /// @return TypeID for the function type (reuses existing if structurally equal)
    TypeID register_function_type(const Vector<TypeID>& param_types, TypeID return_type);

    /// Get the number of parameters in a function type
    size_t get_function_param_count(TypeID func_type) const;

    /// Get a parameter type at a specific index
    TypeID get_function_param_type(TypeID func_type, size_t index) const;

    /// Get the return type of a function type
    TypeID get_function_return_type(TypeID func_type) const;

    // -- Closures ------------------------------------------------------------

    /// Allocate a closure on the script heap
    HeapPtr alloc_closure(TypeID func_type, const CompiledFunction* func, const BytecodeModule* module, size_t upvalue_count);

    /// Get a closure from a heap pointer
    Closure* get_closure(HeapPtr ptr) const;

    /// Allocate an upvalue on the script heap
    Upvalue* alloc_upvalue();

    // -- Script Heap ---------------------------------------------------------

    /// Allocate a string on the script heap
    HeapPtr alloc_string(StringView str);

    /// Allocate a string view referencing existing backing data
    HeapPtr alloc_string_view(HeapPtr backing, uint32_t offset, uint32_t length);

    /// Get raw string data pointer (including offset)
    const char* get_string_data(HeapPtr str_ptr) const;

    /// Get string length
    uint32_t get_string_length(HeapPtr str_ptr) const;

    /// Get string as StringView
    StringView get_string_view(HeapPtr str_ptr) const;

    /// Allocate a struct instance on the script heap
    HeapPtr alloc_struct(TypeID type_id);

    /// Allocate a tuple instance on the script heap
    HeapPtr alloc_tuple(TypeID type_id);

    /// Allocate a sum type instance on the script heap
    HeapPtr alloc_sum(TypeID type_id);

    /// Allocate and initialize a struct from a const struct literal expression
    /// @param expr The struct literal expression to evaluate
    /// @return HeapPtr to the allocated struct, or HeapPtr{0} on failure
    HeapPtr alloc_struct_literal(const BraceInitLiteral* expr);

    // -- Struct Field Access -------------------------------------------------

    /// Read a field value from a struct by index (for VM opcodes)
    /// @param struct_ptr Pointer to struct on heap
    /// @param struct_def The struct definition
    /// @param field_index Index of the field (0-based)
    /// @return The field value, or Value with invalid_type_id on failure
    Value read_struct_field_by_index(HeapPtr struct_ptr, const StructDef* struct_def, uint32_t field_index) const;

    /// Write a field value to a struct by index (for VM opcodes)
    /// @param value The value to write
    /// @return true on success, false on failure
    bool write_struct_field_by_index(HeapPtr struct_ptr, const StructDef* struct_def, uint32_t field_index, const Value& value);

    /// Read a field value from a struct by offset (for optimized VM opcodes)
    Value read_field_at_offset(HeapPtr struct_ptr, uint32_t offset, TypeID type_id) const;

    /// Reads a field from a struct Value, handling both heap and stack (value_type) storage
    Value read_struct_value_field(const Value& struct_val, const StructDef* def, uint32_t field_index);

    /// Reads the variant tag from a sum Value, handling both heap and stack storage
    uint32_t read_sum_value_tag(const Value& sum_val, const SumDef* def);

    /// Reads the payload from a sum Value for a specific variant, handling both heap and stack storage
    Value read_sum_value_payload(const Value& sum_val, const SumDef* def, uint32_t variant_idx);

    /// Write a field value to a struct by offset (for optimized VM opcodes)
    bool write_field_at_offset(HeapPtr struct_ptr, uint32_t offset, TypeID type_id, const Value& value);

    /// Read a field value from a struct by name (for const eval, debugging)
    /// @param struct_ptr Pointer to struct on heap
    /// @param struct_type_id Type ID of the struct
    /// @param field_name Name of the field to read
    /// @return The field value, or Value with invalid_type_id on failure
    Value read_struct_field(HeapPtr struct_ptr, TypeID struct_type_id, StringView field_name) const;

    /// Write a field value to a struct by name (for const eval, debugging)
    /// @param struct_ptr Pointer to struct on heap
    /// @param struct_type_id Type ID of the struct
    /// @param field_name Name of the field to write
    /// @param value The value to write
    /// @return true on success, false on failure
    bool write_struct_field(HeapPtr struct_ptr, TypeID struct_type_id, StringView field_name, const Value& value);

    // -- Tuple Element Access ------------------------------------------------

    /// Read an element value from a tuple by index (for VM opcodes and const eval)
    Value read_tuple_element_by_index(HeapPtr tuple_ptr, const TupleDef* tuple_def, uint32_t element_index) const;

    /// Write an element value to a tuple by index (for VM opcodes)
    /// @return true on success, false on failure
    bool write_tuple_element_by_index(HeapPtr tuple_ptr, const TupleDef* tuple_def, uint32_t element_index, const Value& value);

    /// Read an element value from a tuple by index (for const eval)
    /// @param tuple_ptr Pointer to tuple on heap
    /// @param tuple_type_id Type ID of the tuple
    /// @param element_index Index of the element (0-based)
    /// @return The element value, or Value with invalid_type_id on failure
    Value read_tuple_element(HeapPtr tuple_ptr, TypeID tuple_type_id, uint32_t element_index) const;

    // -- Sum Type Access -----------------------------------------------------

    /// Write the discriminant tag of a sum type (heap version)
    void write_sum_tag(HeapPtr sum_ptr, const SumDef* sum_def, uint32_t tag_value);
    /// Write the discriminant tag of a sum type (raw pointer version for stack values)
    void write_sum_tag(void* data, const SumDef* sum_def, uint32_t tag_value);

    /// Write the payload of a sum type variant (heap version)
    void write_sum_payload(HeapPtr sum_ptr, const SumDef* sum_def, uint32_t variant_idx, const Value& payload);
    /// Write the payload of a sum type variant (raw pointer version for stack values)
    void write_sum_payload(void* data, const SumDef* sum_def, uint32_t variant_idx, const Value& payload);

    /// Read the discriminant tag of a sum type (heap version)
    uint32_t read_sum_tag(HeapPtr sum_ptr, const SumDef* sum_def) const;
    /// Read the discriminant tag of a sum type (raw pointer version for stack values)
    uint32_t read_sum_tag(void* data, const SumDef* sum_def) const;

    /// Read the payload of a sum type variant (heap version)
    Value read_sum_payload(HeapPtr sum_ptr, const SumDef* sum_def, uint32_t variant_idx) const;
    /// Read the payload of a sum type variant (raw pointer version for stack values)
    Value read_sum_payload(void* data, const SumDef* sum_def, uint32_t variant_idx) const;

    /// Allocate an array on the script heap
    /// @param element_type_id The type ID of array elements
    /// @param count Number of elements in the array
    /// @return HeapPtr to the allocated array, or HeapPtr{0} on failure
    HeapPtr alloc_array(TypeID element_type_id, uint32_t count);

    /// Get an element from an array
    /// @param array_ptr Heap pointer to the array
    /// @param index The index
    /// @param out_value Output parameter for the value (if valid)
    /// @return true if index valid, false otherwise
    bool array_get(HeapPtr array_ptr, uint32_t index, Value& out_value) const;

    /// Set an element in an array
    /// @param array_ptr Heap pointer to the array
    /// @param index The index
    /// @param value The value to set
    /// @return true if successful
    bool array_set(HeapPtr array_ptr, uint32_t index, const Value& value);

    /// Get the size of an array
    /// @param array_ptr Heap pointer to the array
    /// @return Number of elements
    size_t array_size(HeapPtr array_ptr) const;

    /// Create a specialized array based on element type (NEW API)
    /// @param element_type_id The type ID of array elements
    /// @param initial_capacity Initial capacity (0 for default)
    /// @return HeapPtr to array instance, or HeapPtr{0} on failure
    HeapPtr create_array_typed(TypeID element_type_id, size_t initial_capacity = 0);

    /// Get IArray pointer from HeapPtr (NEW API)
    /// @param array_ptr Heap pointer to array
    /// @return Pointer to IArray, or nullptr if not an array
    IArray* get_array_typed(HeapPtr array_ptr) const;

    /// Allocate a map on the script heap
    /// @param key_type_id The type ID of map keys
    /// @param value_type_id The type ID of map values
    /// @return HeapPtr to the allocated map, or HeapPtr{0} on failure
    HeapPtr alloc_map(TypeID key_type_id, TypeID value_type_id);

    /// Get a value from a map
    /// @param map_ptr Heap pointer to the map
    /// @param key The key to lookup
    /// @param out_value Output parameter for the value (if found)
    /// @return true if key was found, false otherwise
    bool map_get(HeapPtr map_ptr, const Value& key, Value& out_value) const;

    /// Set a key-value pair in a map
    /// @param map_ptr Heap pointer to the map
    /// @param key The key
    /// @param value The value to set
    /// @return true if inserted, false if overwritten
    bool map_set(HeapPtr map_ptr, const Value& key, const Value& value);

    /// Check if a map contains a key
    /// @param map_ptr Heap pointer to the map
    /// @param key The key to check
    /// @return true if the key exists
    bool map_contains(HeapPtr map_ptr, const Value& key) const;

    /// Get the size of a map
    /// @param map_ptr Heap pointer to the map
    /// @return Number of key-value pairs in the map
    size_t map_size(HeapPtr map_ptr) const;

    /// Remove a key-value pair from a map
    /// @param map_ptr Heap pointer to the map
    /// @param key The key
    /// @return true if removed, false if missing
    bool map_remove(HeapPtr map_ptr, const Value& key);

    /// Clear all entries from a map
    /// @param map_ptr Heap pointer to the map
    void map_clear(HeapPtr map_ptr);

    /// Start iterating over a map
    /// @param map_ptr Heap pointer to the map
    /// @return Heap pointer to the iterator
    HeapPtr map_iter_begin(HeapPtr map_ptr);

    /// Get next key-value pair from map iterator
    /// @param iter_ptr Heap pointer to the iterator
    /// @param key Output: the key
    /// @param value Output: the value
    /// @return true if valid, false if iteration complete
    bool map_iter_next(HeapPtr iter_ptr, Value& key, Value& value);

    /// End map iteration and cleanup
    /// @param map_ptr Heap pointer to the map
    /// @param iter_ptr Heap pointer to the iterator
    void map_iter_end(HeapPtr map_ptr, HeapPtr iter_ptr);

    /// Invoke a ScriptClosure with the given arguments
    Value call_closure(ScriptClosure closure, const Vector<Value>& args);

    /// Materialize language-level zero/default values for heap-backed categories
    /// within an already allocated storage blob.
    ///
    /// This converts raw zeroed memory into valid defaults for non-nullable
    /// heap categories (string/array/map), recursively for value aggregates.
    /// Closures remain nullable and keep HeapPtr{0}.
    void initialize_zero_defaults(TypeID type_id, uint8_t* base);

    /// Load a config file and materialize its top-level struct into a C++ type.
    /// The config file contains a bare struct literal. A user prelude module makes
    /// native types visible without explicit imports.
    /// For [[native]] types, the heap struct has identical layout to T, so we
    /// return a direct pointer into the script heap (zero-copy).
    /// @tparam T C++ type registered via ModuleBuilder::native_struct<T>()
    /// @param path Module-style path to the .smalls config file
    /// @param prelude_module Optional module path for type declarations (persists if set)
    /// @return Pointer to T on the script heap, or nullptr on failure
    template <typename T>
    T* load_config(StringView path, StringView prelude_module = "");

    /// Sets the user prelude module (types visible without import in all scripts)
    void set_user_prelude(StringView module_path);

    /// Gets the current user prelude script
    Script* user_prelude() const { return user_prelude_; }

    TypeTable type_table_;
    ScriptHeap heap_;
    std::unique_ptr<GarbageCollector> gc_;
    Vector<Value> stack_;

    GarbageCollector* gc() { return gc_.get(); }

    void destruct_object(HeapPtr ptr);
    void* get_value_data_ptr(const Value& v);
    void enumerate_module_globals(GCRootVisitor& visitor);

    template <typename Callback>
    void scan_fixed_array_heap_refs(const Type* arr_type, uint8_t* base, Callback&& callback);

    template <typename Callback>
    void scan_value_heap_refs(TypeID type_id, uint8_t* base, Callback&& callback);

    void push(Value val) { stack_.push_back(val); }
    Value pop()
    {
        Value v = stack_.back();
        stack_.pop_back();
        return v;
    }
    Value& top() { return stack_.back(); }
    bool stack_empty() const { return stack_.empty(); }

private:
    friend struct ModuleBuilder;

    // Helper to register all primitive type operators
    void register_primitive_operators();

    // Module cache: module path -> Script
    // Scripts allocated from allocator(), not heap
    absl::flat_hash_map<String, Script*> modules_;

    // Cached line offsets for source mapping
    absl::flat_hash_map<String, Vector<size_t>> line_offsets_;

    // Bytecode cache: Script -> BytecodeModule
    // BytecodeModules are owned by Runtime
    absl::flat_hash_map<Script*, std::unique_ptr<BytecodeModule>> bytecode_cache_;

    // Generic function instantiation cache
    struct InstantiationKey {
        String module;
        String function;
        Vector<TypeID> type_args;

        bool operator==(const InstantiationKey& other) const = default;

        template <typename H>
        friend H AbslHashValue(H h, const InstantiationKey& k)
        {
            h = H::combine(std::move(h), k.module, k.function);
            for (auto t : k.type_args) {
                h = H::combine(std::move(h), t);
            }
            return h;
        }
    };
    absl::flat_hash_map<InstantiationKey, GenericInstantiation> instantiation_cache_;

    // Generic type instantiation cache
    struct TypeInstantiationKey {
        TypeID generic_type_id;
        Vector<TypeID> type_args;

        bool operator==(const TypeInstantiationKey& other) const = default;

        template <typename H>
        friend H AbslHashValue(H h, const TypeInstantiationKey& k)
        {
            h = H::combine(std::move(h), k.generic_type_id);
            for (auto t : k.type_args) {
                h = H::combine(std::move(h), t);
            }
            return h;
        }
    };
    absl::flat_hash_map<TypeInstantiationKey, TypeID> type_instantiation_cache_;

    // VM instance
    std::unique_ptr<VirtualMachine> vm_;

    // Stack of modules currently being loaded (for circular dependency detection)
    Vector<String> loading_stack_;

    // Paths to search for module files
    Vector<std::filesystem::path> module_paths_;

    // Memory and resource management for scripts
    MemoryArena arena_;
    MemoryScope scope_;
    ResourceManager resman_;
    bool resman_needs_build_ = false;

    // Diagnostic handler (can be overridden for LSP, etc.)
    Context* diagnostic_context_ = nullptr;
    DiagnosticConfig diagnostic_config_{};

    // Cached primitive type IDs for fast type checking
    TypeID void_id_{};
    TypeID int_id_{};
    TypeID float_id_{};
    TypeID string_id_{};
    TypeID string_backing_id_{};
    TypeID bool_id_{};
    TypeID any_id_{};
    TypeID vec3_id_{};
    TypeID object_id_{};
    TypeID any_array_id_{};
    TypeID any_map_id_{};

    // Operator registries
    // Key: (operator, lhs_type, rhs_type) -> overload
    absl::flat_hash_map<std::tuple<TokenType, TypeID, TypeID>, BinaryOpOverload> binary_ops_;
    // Key: (operator, operand_type) -> overload
    absl::flat_hash_map<std::pair<TokenType, TypeID>, UnaryOpOverload> unary_ops_;
    // Dedicated str/hash operators keyed by TypeID alone
    absl::flat_hash_map<TypeID, ScriptFunctionRef> str_ops_;
    absl::flat_hash_map<TypeID, ScriptFunctionRef> hash_ops_;
    // Native hash executors (e.g. default structural hash); looked up when hash_ops_ has no entry
    absl::flat_hash_map<TypeID, std::function<Value(const Value&)>> native_hash_ops_;

    // Type metadata (only for types with non-default metadata)
    absl::flat_hash_map<TypeID, TypeMetadata> type_metadata_;

    // Operator alias metadata (eq/hash validation)
    absl::flat_hash_map<TypeID, OperatorAliasInfo> operator_alias_info_;

    // Native module interfaces (for .smalli generation)
    Vector<ModuleInterface> native_interfaces_;

    // Native functions for VM execution
    Vector<NativeFunction> native_functions_;
    absl::flat_hash_map<String, uint32_t> native_function_names_;

    // External functions (unified native + cross-module script)
    Vector<ExternalFunction> external_functions_;
    absl::flat_hash_map<InternedString, uint32_t> external_function_names_;

    // Native struct layouts registered from C++ for validation
    absl::flat_hash_map<String, NativeStructLayout> native_struct_layouts_;

    // Built-in core prelude module
    Script* core_prelude_ = nullptr;
    Script* core_test_ = nullptr;
    Script* user_prelude_ = nullptr;

    Vector<HeapPtr> config_roots_;

    Value load_config_value(StringView path, StringView prelude_module);

    uint32_t test_count_ = 0;
    uint32_t test_failures_ = 0;

    friend struct NativeStructBuilder;
    friend struct ModuleBuilder;

public:
    /// Builds a module for registering C++ interfaces
    ModuleBuilder build_module(StringView path);

    /// Validates a smalls struct against a registered native layout
    /// @param struct_name Name of the struct type
    /// @param struct_def The smalls StructDef to validate
    /// @return true if valid, false if mismatch (logs error)
    bool validate_native_struct(StringView struct_name, const StructDef* struct_def) const;
};

// == Native Struct Registration ==============================================

/// Information about a native C++ struct field
struct NativeFieldInfo {
    String name;
    size_t offset;
    TypeID type_id;
};

/// Complete C++ struct layout for validation
struct NativeStructLayout {
    String type_name;
    size_t size;
    size_t alignment;
    Vector<NativeFieldInfo> fields;
};

// Forward declaration
struct ModuleBuilder;

/// Builder for registering native struct layouts
struct NativeStructBuilder {
    NativeStructBuilder(NativeStructLayout& layout, ModuleBuilder& parent)
        : layout_(layout)
        , parent_(parent)
    {
    }

    /// Registers a field using pointer-to-member (auto-deduces offset and type)
    template <typename T, typename FieldType>
    NativeStructBuilder& field(StringView field_name, FieldType T::* member_ptr);

    /// Finishes struct registration and returns to ModuleBuilder
    ModuleBuilder& end_struct();

private:
    NativeStructLayout& layout_;
    ModuleBuilder& parent_;
};

/// Helper to deduce TypeID from C++ primitive/handle type
template <typename T>
TypeID deduce_type_id()
{
    auto& rt = nw::kernel::runtime();

    // Primitives
    if constexpr (std::is_same_v<T, int32_t>) {
        return rt.int_type();
    } else if constexpr (std::is_same_v<T, float>) {
        return rt.float_type();
    } else if constexpr (std::is_same_v<T, bool>) {
        return rt.bool_type();
    }
    // Engine value types
    else if constexpr (std::is_same_v<T, glm::vec3>) {
        return rt.vec3_type();
    } else if constexpr (std::is_same_v<T, ObjectHandle>) {
        return rt.object_type();
    }
    // ScriptString (mapped to smalls string)
    else if constexpr (std::is_same_v<T, ScriptString>) {
        return rt.string_type();
    }
    // ScriptClosure (mapped to smalls function type)
    else if constexpr (std::is_same_v<T, ScriptClosure>) {
        return rt.any_type(); // function types are polymorphic; resolved at load time
    }
    // Unsupported type
    else {
        static_assert(always_false<T>(),
            "Unsupported type in [[native]] structs");
        return invalid_type_id;
    }
}

template <typename T, typename FieldType>
NativeStructBuilder& NativeStructBuilder::field(StringView field_name, FieldType T::* member_ptr)
{
    size_t offset = reinterpret_cast<size_t>(&(static_cast<T*>(nullptr)->*member_ptr));
    TypeID type_id = deduce_type_id<FieldType>();

    layout_.fields.push_back(NativeFieldInfo{
        .name = String(field_name),
        .offset = offset,
        .type_id = type_id});
    return *this;
}

inline ModuleBuilder& NativeStructBuilder::end_struct()
{
    return parent_;
}

// == ModuleBuilder ===========================================================
// ============================================================================

/// Builder for registering C++/internal modules with types and functions
struct ModuleBuilder {
    explicit ModuleBuilder(Runtime* runtime, StringView path);
    ~ModuleBuilder();

    /// Registers a C++ type as opaque (engine-only, no script access to fields)
    template <typename T>
    ModuleBuilder& add_opaque_type(StringView name);

    /// Registers a C++ value type with layout validation
    template <typename T>
    ModuleBuilder& value_type(StringView name);

    /// Registers a native struct with field-level validation
    template <typename T>
    NativeStructBuilder& native_struct(StringView name);

    /// Registers a C++ function with parameter names for interface generation
    template <typename Ret, typename... Args>
    ModuleBuilder& function(StringView name, Ret (*func)(Args...));

    /// Registers a handle type with the runtime
    ModuleBuilder& handle_type(StringView name, uint8_t runtime_type);

    /// Finalizes and registers the module in the runtime
    void finalize();

private:
    Runtime* runtime_;
    String path_;
    Script* script_;
    ModuleInterface interface_;
    bool finalized_ = false;
    std::unique_ptr<NativeStructBuilder> current_struct_builder_;
};

// == Template Implementations ================================================

template <typename T>
ModuleBuilder& ModuleBuilder::add_opaque_type(StringView name)
{
    Type type{
        .name = nw::kernel::strings().intern(name),
        .type_params = {},
        .type_kind = TK_opaque,
        .size = sizeof(T),
        .alignment = alignof(T),
    };
    runtime_->register_type(name, type);
    interface_.opaque_types.push_back(String(name));

    return *this;
}

template <typename T>
ModuleBuilder& ModuleBuilder::value_type(StringView name)
{
    // Get the smalls type (should be registered from .smalls file or elsewhere)
    const Type* smalls_type = runtime_->get_type(name);
    if (!smalls_type) {
        LOG_F(ERROR, "[runtime] Cannot validate value_type '{}' - type not found. "
                     "Ensure type is defined in .smalls file first.",
            name);
        return *this;
    }

    // Validate layout matches
    if (smalls_type->size != sizeof(T)) {
        LOG_F(ERROR, "[runtime] Layout mismatch for '{}': C++ size={}, smalls size={}",
            name, sizeof(T), smalls_type->size);
        return *this;
    }

    if (smalls_type->alignment != alignof(T)) {
        LOG_F(ERROR, "[runtime] Layout mismatch for '{}': C++ alignment={}, smalls alignment={}",
            name, alignof(T), smalls_type->alignment);
        return *this;
    }

    LOG_F(INFO, "[runtime] Validated value_type '{}': size={}, alignment={}",
        name, sizeof(T), alignof(T));

    // Add to interface for .smalli generation
    interface_.opaque_types.push_back(String(name));

    return *this;
}

template <typename T>
NativeStructBuilder& ModuleBuilder::native_struct(StringView name)
{
    String qualified_name = path_ + "." + String(name);

    // Create and register the layout immediately
    NativeStructLayout& layout = runtime_->native_struct_layouts_[qualified_name];
    layout.type_name = qualified_name;
    layout.size = sizeof(T);
    layout.alignment = alignof(T);

    LOG_F(INFO, "[native] Registered native struct '{}': size={}, alignment={}",
        qualified_name, sizeof(T), alignof(T));

    // Add to interface for .smalli generation
    interface_.opaque_types.push_back(String(name));

    // Create and store the struct builder
    current_struct_builder_ = std::make_unique<NativeStructBuilder>(layout, *this);
    return *current_struct_builder_;
}

template <typename T>
T* HeapPtrT<T>::get() const
{
    if (ptr.value == 0) return nullptr;
    return static_cast<T*>(nw::kernel::runtime().heap_.get_ptr(ptr));
}

// == Type Mapping Helpers ====================================================

namespace detail {

/// Maps C++ types to smalls TypeID at compile time
template <typename T>
TypeID cpp_to_typeid(Runtime* rt)
{
    if (!rt) {
        return invalid_type_id;
    }

    using Bare = std::remove_cv_t<std::remove_reference_t<T>>;

    if constexpr (std::is_same_v<Bare, int32_t> || std::is_same_v<Bare, int>) {
        return rt->int_type();
    } else if constexpr (std::is_same_v<Bare, float>) {
        return rt->float_type();
    } else if constexpr (std::is_same_v<Bare, bool>) {
        return rt->bool_type();
    } else if constexpr (std::is_same_v<Bare, void>) {
        return rt->void_type();
    } else if constexpr (std::is_same_v<Bare, TypedHandle>) {
        return invalid_type_id; // script is source of truth for handle types
    } else if constexpr (std::is_same_v<Bare, IArray*> || (std::is_pointer_v<Bare> && std::is_base_of_v<IArray, std::remove_pointer_t<Bare>>)) {
        return rt->any_array_type(); // Wildcard: matches any array<T>
    } else if constexpr (std::is_same_v<Bare, Value>) {
        return rt->any_type(); // Dynamic type: script determines actual type
    } else {
        // For other types, try to get by type name (opaque types)
        // This will need the type to be registered first
        return rt->type_id(typeid(Bare).name(), false);
    }
}

template <typename T>
T value_cast(Runtime* rt, const Value& v)
{
    using Bare = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (std::is_same_v<Bare, int32_t> || std::is_same_v<Bare, int>) {
        return static_cast<Bare>(v.data.ival);
    } else if constexpr (std::is_same_v<Bare, float>) {
        return static_cast<Bare>(v.data.fval);
    } else if constexpr (std::is_same_v<Bare, bool>) {
        return static_cast<Bare>(v.data.bval);
    } else if constexpr (std::is_same_v<Bare, TypedHandle>) {
        if (!rt || v.data.hptr.value == 0) {
            return Bare{};
        }
        auto* handle = rt->get_handle(v.data.hptr);
        if (!handle) { return Bare{}; }
        TypeID expected = rt->handle_type_to_typeid_[handle->type];
        if (expected == invalid_type_id || v.type_id != expected) {
            return Bare{};
        }
        return *handle;
    } else {
        return Bare{};
    }
}

template <typename T>
Value make_value(Runtime* rt, const T& val)
{
    using Bare = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (std::is_same_v<Bare, int32_t> || std::is_same_v<Bare, int>) {
        return Value::make_int(val);
    } else if constexpr (std::is_same_v<Bare, float>) {
        return Value::make_float(val);
    } else if constexpr (std::is_same_v<Bare, bool>) {
        return Value::make_bool(val);
    } else if constexpr (std::is_same_v<Bare, TypedHandle>) {
        if (!rt) { return Value{}; }
        TypeID type_id = rt->handle_type_to_typeid_[val.type];
        if (type_id == invalid_type_id) {
            return Value{};
        }
        HeapPtr ptr = rt->intern_handle(val, OwnershipMode::VM_OWNED);
        if (ptr.value != 0) {
            return Value::make_heap(ptr, type_id);
        }
        return Value{};
    } else {
        return Value{};
    }
}

} // namespace detail

/// ModuleBuilder::function() implementation with signature extraction
template <typename Ret, typename... Args>
ModuleBuilder& ModuleBuilder::function(StringView name, Ret (*func)(Args...))
{
    FunctionMetadata meta;
    meta.name = String(name);
    meta.return_type = detail::cpp_to_typeid<Ret>(runtime_);

    // Extract parameter types
    if constexpr (sizeof...(Args) > 0) {
        (meta.params.push_back(ParamMetadata{
             String(""), // Parameter name not available from function pointer
             detail::cpp_to_typeid<Args>(runtime_)}),
            ...);
    }

    interface_.functions.push_back(meta);

    // Create wrapper for VM execution
    NativeFunctionWrapper wrapper = [func](Runtime* rt, const Value* args, uint8_t argc) -> Value {
        if (argc != sizeof...(Args)) {
            return Value{};
        }

        auto invoke = [&]<size_t... I>(std::index_sequence<I...>) {
            if constexpr (std::is_void_v<Ret>) {
                func(detail::value_cast<Args>(rt, args[I])...);
                return Value(rt->void_type());
            } else {
                return detail::make_value(rt, func(detail::value_cast<Args>(rt, args[I])...));
            }
        };

        return invoke(std::index_sequence_for<Args...>{});
    };

    // Register with runtime
    runtime_->register_native_function(NativeFunction{
        .name = path_ + "." + String(name),
        .wrapper = std::move(wrapper),
        .metadata = std::move(meta)});

    return *this;
}

// == load_config<T> Implementation ===========================================

template <typename T>
T* Runtime::load_config(StringView path, StringView prelude_module)
{
    Value val = load_config_value(path, prelude_module);
    if (val.type_id == invalid_type_id || val.storage != ValueStorage::heap || val.data.hptr.value == 0) {
        return nullptr;
    }

    config_roots_.push_back(val.data.hptr);

    return static_cast<T*>(heap_.get_ptr(val.data.hptr));
}

template <typename Callback>
void Runtime::scan_fixed_array_heap_refs(const Type* arr_type, uint8_t* base, Callback&& callback)
{
    if (!arr_type || arr_type->type_kind != TK_fixed_array || !arr_type->contains_heap_refs) {
        return;
    }

    TypeID elem_type_id = arr_type->type_params[0].as<TypeID>();
    int32_t count = arr_type->type_params[1].as<int32_t>();
    const Type* elem_type = get_type(elem_type_id);
    if (!elem_type || count <= 0) {
        return;
    }

    uint32_t elem_size = elem_type->size;
    for (int32_t i = 0; i < count; ++i) {
        uint8_t* elem_base = base + static_cast<uint32_t>(i) * elem_size;

        if (elem_type->type_kind == TK_struct && elem_type->contains_heap_refs) {
            auto struct_id = elem_type->type_params[0].as<StructID>();
            const StructDef* def = type_table_.get(struct_id);
            if (!def) {
                continue;
            }
            for (uint32_t j = 0; j < def->heap_ref_count; ++j) {
                HeapPtr* ptr = reinterpret_cast<HeapPtr*>(elem_base + def->heap_ref_offsets[j]);
                if (ptr->value != 0) {
                    callback(ptr);
                }
            }
        } else if (elem_type->type_kind == TK_fixed_array && elem_type->contains_heap_refs) {
            scan_fixed_array_heap_refs(elem_type, elem_base, callback);
        } else if (type_table_.is_heap_type(elem_type_id)) {
            HeapPtr* ptr = reinterpret_cast<HeapPtr*>(elem_base);
            if (ptr->value != 0) {
                callback(ptr);
            }
        }
    }
}

template <typename Callback>
void Runtime::scan_value_heap_refs(TypeID type_id, uint8_t* base, Callback&& callback)
{
    const Type* type = get_type(type_id);
    if (!type) { return; }

    while (type->type_kind == TK_newtype || type->type_kind == TK_alias) {
        type_id = type->type_params[0].as<TypeID>();
        type = get_type(type_id);
        if (!type) { return; }
    }

    if (type_table_.is_heap_type(type_id)) {
        HeapPtr* ptr = reinterpret_cast<HeapPtr*>(base);
        if (ptr->value != 0) {
            callback(ptr);
        }
        return;
    }

    if (!type->contains_heap_refs) { return; }

    switch (type->type_kind) {
    case TK_struct: {
        auto struct_id = type->type_params[0].as<StructID>();
        const StructDef* def = type_table_.get(struct_id);
        if (!def) { return; }
        for (uint32_t i = 0; i < def->heap_ref_count; ++i) {
            HeapPtr* ptr = reinterpret_cast<HeapPtr*>(base + def->heap_ref_offsets[i]);
            if (ptr->value != 0) {
                callback(ptr);
            }
        }
        break;
    }
    case TK_tuple: {
        auto tuple_id = type->type_params[0].as<TupleID>();
        const TupleDef* def = type_table_.get(tuple_id);
        if (!def) { return; }
        for (uint32_t i = 0; i < def->element_count; ++i) {
            scan_value_heap_refs(def->element_types[i], base + def->offsets[i], callback);
        }
        break;
    }
    case TK_sum: {
        auto sum_id = type->type_params[0].as<SumID>();
        const SumDef* def = type_table_.get(sum_id);
        if (!def) { return; }

        uint32_t tag = *reinterpret_cast<uint32_t*>(base + def->tag_offset);
        if (tag >= def->variant_count) { return; }

        const VariantDef& variant = def->variants[tag];
        if (variant.payload_type == invalid_type_id) { return; }

        uint8_t* payload_base = base + def->union_offset + variant.payload_offset;
        scan_value_heap_refs(variant.payload_type, payload_base, callback);
        break;
    }
    case TK_fixed_array:
        scan_fixed_array_heap_refs(type, base, callback);
        break;
    default:
        break;
    }
}

} // nw::smalls

template <>
struct fmt::formatter<nw::smalls::TypeID> : fmt::formatter<std::string_view> {
    template <typename FormatContext>
    auto format(nw::smalls::TypeID type_id, FormatContext& ctx) const
    {
        return fmt::formatter<std::string_view>::format(nw::kernel::runtime().type_name(type_id), ctx);
    }
};
