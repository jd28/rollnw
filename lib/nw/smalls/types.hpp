#pragma once

#include "../kernel/Memory.hpp"
#include "../util/InternedString.hpp"
#include "../util/StrongID.hpp"
#include "../util/Variant.hpp"
#include "../util/enum_flags.hpp"

#include <absl/container/flat_hash_map.h>

namespace nw::smalls {

// Forwardl Decls
struct StructDecl;
struct SumDecl;
struct TypeAlias;
struct NewtypeDecl;
enum class BraceInitType;

// Tags
struct TypeTag { };
using TypeID = StrongID<TypeTag>;
constexpr TypeID invalid_type_id{};

struct StructTag { };
using StructID = StrongID<StructTag>;

struct TupleTag { };
using TupleID = StrongID<TupleTag>;

struct SumTag { };
using SumID = StrongID<SumTag>;

struct FunctionTag { };
using FunctionID = StrongID<FunctionTag>;

// Type parameter variant - used in Type::type_params
using TypeParam = Variant<TypeID, StructID, TupleID, SumID, FunctionID, int32_t>;

// Internal struct field definition
struct FieldDef {
    InternedString name;
    TypeID type_id;
    uint32_t offset;
    int16_t generic_param_index = -1;

    bool operator==(const FieldDef& rhs) const noexcept = default;
};

// Internal struct definition
struct StructDef {
    uint32_t field_index(StringView name) const noexcept;

    uint32_t size;        // Total size in bytes
    uint32_t alignment;   // Alignment requirement in bytes
    uint32_t field_count; // Number of fields in the struct
    uint32_t generic_param_count = 0;
    FieldDef* fields;
    const StructDecl* decl = nullptr;

    // GC support: byte offsets of HeapPtr fields for root enumeration
    uint32_t heap_ref_count = 0;
    uint32_t* heap_ref_offsets = nullptr;
};

// Internal tuple definition
struct TupleDef {
    uint32_t size;          // Total size in bytes
    uint32_t alignment;     // Alignment requirement
    uint32_t element_count; // Number of elements
    TypeID* element_types;  // Array of element type IDs
    uint32_t* offsets;      // Byte offset for each element
};

// Internal sum type variant definition
struct VariantDef {
    InternedString name;     // Variant name (e.g., "Ok", "Err")
    uint32_t tag_value;      // Discriminant value (0, 1, 2, ...)
    TypeID payload_type;     // Payload type (invalid_type_id for unit variants)
    uint32_t payload_offset; // Byte offset of payload within union
    int16_t generic_param_index = -1;

    bool operator==(const VariantDef& rhs) const noexcept = default;
};

// Internal sum type definition
struct SumDef {
    uint32_t size;          // Total size (tag + padding + union)
    uint32_t alignment;     // Alignment requirement
    uint32_t variant_count; // Number of variants
    uint32_t generic_param_count = 0;
    VariantDef* variants;  // Array of variant definitions
    uint32_t tag_offset;   // Offset of tag (always 0)
    uint32_t union_offset; // Offset of union data (after tag + padding)
    uint32_t union_size;   // Size of union (max payload size)
    const SumDecl* decl;   // Back-pointer to AST declaration (for nominal typing)

    const VariantDef* find_variant(StringView name) const noexcept
    {
        for (uint32_t i = 0; i < variant_count; ++i) {
            if (variants[i].name.view() == name) {
                return &variants[i];
            }
        }
        return nullptr;
    }

    bool is_unit_variant(StringView name) const noexcept
    {
        auto* variant = find_variant(name);
        return variant && variant->payload_type == invalid_type_id;
    }

    bool is_payload_variant(StringView name) const noexcept
    {
        auto* variant = find_variant(name);
        return variant && variant->payload_type != invalid_type_id;
    }
};

// Internal function type definition (for closures and function references)
struct FunctionDef {
    uint32_t param_count; // Number of parameters
    TypeID* param_types;  // Array of parameter type IDs
    TypeID return_type;   // Return type (invalid_type_id for void)
};

struct FunctionDefHash {
    size_t operator()(const FunctionDef* def) const noexcept
    {
        size_t seed = absl::HashOf(0, def->param_count, def->return_type);
        for (uint32_t i = 0; i < def->param_count; ++i) {
            seed = absl::HashOf(seed, def->param_types[i]);
        }
        return seed;
    }
};

struct FunctionDefEq {
    bool operator()(const FunctionDef* a, const FunctionDef* b) const noexcept
    {
        if (a->param_count != b->param_count || a->return_type != b->return_type) {
            return false;
        }
        for (uint32_t i = 0; i < a->param_count; ++i) {
            if (a->param_types[i] != b->param_types[i]) {
                return false;
            }
        }
        return true;
    }
};

struct StructDefHash {
    size_t operator()(const StructDef* def) const noexcept
    {
        size_t seed = absl::HashOf(0, def->size, def->field_count);

        for (uint32_t i = 0; i < def->field_count; ++i) {
            const FieldDef& f = def->fields[i];
            seed = absl::HashOf(seed, f.name, f.type_id);
            // probably not include f.offset; that's layout-specific
        }

        return seed;
    }
};

struct StructDefEq {
    bool operator()(const StructDef* a, const StructDef* b) const noexcept
    {
        if (a->field_count != b->field_count || a->size != b->size) {
            return false;
        }

        for (uint32_t i = 0; i < a->field_count; ++i) {
            const FieldDef& fa = a->fields[i];
            const FieldDef& fb = b->fields[i];
            if (fa.name != fb.name || fa.type_id != fb.type_id) {
                return false;
            }
        }
        return true;
    }
};

struct TupleDefHash {
    size_t operator()(const TupleDef* def) const noexcept
    {
        size_t seed = absl::HashOf(0, def->size, def->alignment, def->element_count);

        for (uint32_t i = 0; i < def->element_count; ++i) {
            seed = absl::HashOf(seed, def->element_types[i]);
        }

        return seed;
    }
};

struct TupleDefEq {
    bool operator()(const TupleDef* a, const TupleDef* b) const noexcept
    {
        if (a->element_count != b->element_count || a->size != b->size || a->alignment != b->alignment) {
            return false;
        }

        for (uint32_t i = 0; i < a->element_count; ++i) {
            if (a->element_types[i] != b->element_types[i]) {
                return false;
            }
        }
        return true;
    }
};

struct SumDefHash {
    size_t operator()(const SumDef* def) const noexcept
    {
        // Nominal typing: use declaration pointer as identity
        return absl::HashOf(def->decl);
    }
};

struct SumDefEq {
    bool operator()(const SumDef* a, const SumDef* b) const noexcept
    {
        // Nominal typing: same declaration = same type
        return a->decl == b->decl;
    }
};

// == Type ====================================================================
// ============================================================================

enum TypeKind : uint8_t {
    TK_unspecified = 0, // Type Params:
    TK_primitive,       // None
    TK_struct,          // 0: StructID
    TK_alias,           // 0: TypeID (aliased type)
    TK_newtype,         // 0: TypeID (wrapped type)
    TK_array,           // 0: TypeID (element type) - dynamic heap array
    TK_fixed_array,     // 0: TypeID (element type), 1: int (size N) - inline value type T[N]
    TK_map,             // 0: TypeID, 1: TypeID
    TK_tuple,           // 0: TupleID
    TK_sum,             // 0: SumID
    TK_function,        // 0: FunctionID - function type fn(T1, T2): R
    TK_opaque,          // None
    TK_type_param,      // Generic type parameter (e.g., $T)
    TK_any_array,       // Wildcard: matches any array<T> (for generic native functions)
    TK_any_map,         // Wildcard: matches any map<K,V> (for generic native functions)
};

enum PrimitiveKind : uint8_t {
    PK_unspecified,
    PK_int,
    PK_float,
    PK_string,
    PK_bool,
};

struct Type {
    InternedString name;
    std::array<TypeParam, 2> type_params;
    TypeKind type_kind = TK_unspecified;
    PrimitiveKind primitive_kind = PK_unspecified;
    uint32_t size = 0;
    uint32_t alignment = 0;
    bool contains_heap_refs = false; // True if type contains HeapPtr fields (for GC)

    bool operator==(const Type& rhs) const noexcept = default;
};

template <typename H>
H AbslHashValue(H h, const Type& type)
{
    return H::combine(std::move(h),
        type.name,
        type.type_params,
        type.type_kind,
        type.primitive_kind,
        type.size,
        type.alignment,
        type.contains_heap_refs);
}

enum class SemanticFlags : uint8_t {
    none = 0x00,
    value = 0x01,
    reference = 0x02,
    trivially_copyable = 0x04,
    trivially_destructible = 0x08,
};

DEFINE_ENUM_FLAGS(SemanticFlags)

struct TypeMetadata {
    BraceInitType init_mode{};
    uint8_t param_count = 0;
    SemanticFlags semantic_flags = SemanticFlags::value;
};

// == TypeTable ===============================================================
// ============================================================================

struct TypeTable {
    TypeTable(MemoryResource* allocator = nw::kernel::global_allocator());
    ~TypeTable();

    TypeID reserve(StringView qualified_name);
    void define(TypeID id, const StructDecl* decl, StringView module_path);
    void define(TypeID id, const TypeAlias* decl, StringView module_path, TypeID aliased_type_id);
    void define(TypeID id, const NewtypeDecl* decl, StringView module_path, TypeID wrapped_type_id);

    TypeID add(const StructDecl* decl, StringView module_path);
    TypeID add(const TypeAlias* decl, StringView module_path, TypeID aliased_type_id);
    TypeID add(const NewtypeDecl* decl, StringView module_path, TypeID wrapped_type_id);
    TypeID add(Type type);

    const StructDef* get(StructID id) const noexcept;
    const TupleDef* get(TupleID id) const noexcept;
    const SumDef* get(SumID id) const noexcept;
    const FunctionDef* get(FunctionID id) const noexcept;
    const Type* get(TypeID id) const;
    const Type* get(std::string_view name) const;

    TupleID add_tuple(TupleDef* tuple_def);
    SumID add_sum(SumDef* sum_def);
    StructID add_struct(StructDef* struct_def);
    FunctionID add_function(FunctionDef* func_def);
    TypeID add(const SumDecl* decl, StringView module_path);
    void define(TypeID id, const SumDecl* decl, StringView module_path);

    void compute_heap_ref_info(StructDef* def, Type& type);
    bool is_heap_type(TypeID tid) const;

    MemoryResource* allocator_;

    std::vector<Type> types_;
    absl::flat_hash_map<Type, int> type_to_index_;
    absl::flat_hash_map<InternedString, int, InternedStringHash, InternedStringEq> name_to_index_;

    std::vector<StructDef*> structs_;
    absl::flat_hash_map<const StructDef*, size_t> struct_to_index_;

    std::vector<TupleDef*> tuples_;
    absl::flat_hash_map<const TupleDef*, size_t, TupleDefHash, TupleDefEq> tuple_to_index_;

    std::vector<SumDef*> sums_;
    absl::flat_hash_map<const SumDecl*, size_t> sum_to_index_;

    std::vector<FunctionDef*> functions_;
    absl::flat_hash_map<const FunctionDef*, size_t, FunctionDefHash, FunctionDefEq> function_to_index_;
};

} // namespace nw::smalls
