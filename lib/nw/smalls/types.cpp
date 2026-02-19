#include "types.hpp"

#include "Ast.hpp"

namespace nw::smalls {

namespace {

bool struct_decl_has_annotation(const StructDecl* decl, StringView name)
{
    if (!decl) {
        return false;
    }
    for (const auto& ann : decl->annotations_) {
        if (ann.name.loc.view() == name) {
            return true;
        }
    }
    return false;
}

int16_t find_generic_param_index(StringView type_name, const Vector<String>& type_params)
{
    for (size_t i = 0; i < type_params.size(); ++i) {
        if (type_params[i] == type_name) {
            return static_cast<int16_t>(i);
        }
    }
    return -1;
}

} // namespace

// == Structs =================================================================
// ============================================================================

uint32_t StructDef::field_index(StringView name) const noexcept
{
    for (uint32_t i = 0; i < field_count; ++i) {
        if (fields[i].name.view() == name) {
            return i;
        }
    }

    return UINT32_MAX;
}

// == TypeTable ===============================================================
// ============================================================================

TypeTable::TypeTable(MemoryResource* allocator)
    : allocator_{allocator}
{
}

TypeTable::~TypeTable()
{
    for (auto it : structs_) {
        allocator_->deallocate(it);
    }
    for (auto it : tuples_) {
        if (it) {
            if (it->element_types) {
                allocator_->deallocate(it->element_types);
            }
            if (it->offsets) {
                allocator_->deallocate(it->offsets);
            }
            allocator_->deallocate(it);
        }
    }
    for (auto it : sums_) {
        if (it) {
            if (it->variants) {
                allocator_->deallocate(it->variants);
            }
            allocator_->deallocate(it);
        }
    }
    for (auto it : functions_) {
        if (it) {
            if (it->param_types) {
                allocator_->deallocate(it->param_types);
            }
            allocator_->deallocate(it);
        }
    }
}

TypeID TypeTable::reserve(StringView qualified_name)
{
    auto interned = nw::kernel::strings().intern(qualified_name);
    auto it = name_to_index_.find(interned);
    if (it != name_to_index_.end()) {
        return TypeID{it->second};
    }

    int idx = static_cast<int>(types_.size()) + 1;
    Type placeholder{
        .name = interned,
        .type_kind = TK_unspecified,
    };
    types_.push_back(placeholder);
    name_to_index_.insert({interned, idx});
    return TypeID{idx};
}

void TypeTable::define(TypeID id, const StructDecl* decl, StringView module_path)
{
    if (id == invalid_type_id || id.value < 1 || id.value > types_.size()) {
        return;
    }

    uint32_t field_count = 0;
    for (auto* d : decl->decls) {
        if (auto* vdl = dynamic_cast<const DeclList*>(d)) {
            field_count += vdl->decls.size();
        } else if (auto* vd = dynamic_cast<const VarDecl*>(d)) {
            field_count++;
        }
    }

    void* mem = allocator_->allocate(sizeof(StructDef), alignof(StructDef));
    auto* def = new (mem) StructDef{};
    def->field_count = field_count;
    def->generic_param_count = static_cast<uint32_t>(decl->type_params.size());
    def->decl = decl;
    def->is_propset = struct_decl_has_annotation(decl, "propset");
    uint32_t struct_alignment = 1;

    if (field_count > 0) {
        void* fields_mem = allocator_->allocate(sizeof(FieldDef) * field_count, alignof(FieldDef));
        def->fields = static_cast<FieldDef*>(fields_mem);

        uint32_t idx = 0;
        uint32_t offset = 0;

        auto process_field = [&](const VarDecl* vd) {
            const Type* field_type = get(vd->type_id_);

            uint32_t field_size = 0;
            uint32_t field_alignment = 1;

            if (field_type) {
                field_size = field_type->size;
                field_alignment = field_type->alignment;
                if (field_alignment == 0) { field_alignment = 1; }
            }

            const bool is_unmanaged_array_field = def->is_propset && field_type && field_type->type_kind == TK_array;
            if (is_unmanaged_array_field) {
                // Propset array fields store TypedHandle (u64) inline, not HeapPtr.
                field_size = sizeof(uint64_t);
                field_alignment = alignof(uint64_t);
            }

            struct_alignment = std::max(struct_alignment, field_alignment);
            offset = (offset + field_alignment - 1) & ~(field_alignment - 1);

            auto& field = def->fields[idx];
            field.name = nw::kernel::strings().intern(vd->identifier_.loc.view());
            field.type_id = vd->type_id_;
            field.offset = offset;
            field.generic_param_index = vd->type
                ? find_generic_param_index(vd->type->str(), decl->type_params)
                : int16_t{-1};

            // Propset array fields are unmanaged (engine-managed via TypedHandle, not GC).
            field.is_unmanaged_array = is_unmanaged_array_field;

            offset += field_size;
            idx++;
        };

        for (auto* d : decl->decls) {
            if (auto* vdl = dynamic_cast<const DeclList*>(d)) {
                for (auto* vd : vdl->decls) {
                    process_field(vd);
                }
            } else if (auto* vd = dynamic_cast<const VarDecl*>(d)) {
                process_field(vd);
            }
        }

        def->size = (offset + struct_alignment - 1) & ~(struct_alignment - 1);
    } else {
        def->fields = nullptr;
        def->size = 0;
    }

    def->alignment = struct_alignment;

    StructID struct_id{static_cast<int32_t>(structs_.size()) + 1};
    structs_.push_back(def);
    struct_to_index_.insert({def, structs_.size()});

    Type& type = types_[id.value - 1];
    type.type_params = {struct_id, {}};
    type.type_kind = TK_struct;
    type.size = def->size;
    type.alignment = struct_alignment;

    compute_heap_ref_info(def, type);
    type_to_index_.insert({type, id.value});
}

void TypeTable::define(TypeID id, const TypeAlias* decl, StringView module_path, TypeID aliased_type_id)
{
    if (id == invalid_type_id || id.value < 1 || id.value > types_.size()) {
        return;
    }

    Type& type = types_[id.value - 1];
    type.type_params = {aliased_type_id, {}};
    type.type_kind = TK_alias;

    type_to_index_.insert({type, id.value});
}

void TypeTable::define(TypeID id, const NewtypeDecl* decl, StringView module_path, TypeID wrapped_type_id)
{
    if (id == invalid_type_id || id.value < 1 || id.value > types_.size()) {
        return;
    }

    Type& type = types_[id.value - 1];
    type.type_params = {wrapped_type_id, {}};
    type.type_kind = TK_newtype;
    if (const Type* wrapped = get(wrapped_type_id)) {
        type.size = wrapped->size;
        type.alignment = wrapped->alignment;
        type.contains_heap_refs = wrapped->contains_heap_refs;
    }

    type_to_index_.insert({type, id.value});
}

TypeID TypeTable::add(const StructDecl* decl, StringView module_path)
{
    uint32_t field_count = 0;
    for (auto* d : decl->decls) {
        if (auto* vdl = dynamic_cast<const DeclList*>(d)) {
            field_count += vdl->decls.size();
        } else if (auto* vd = dynamic_cast<const VarDecl*>(d)) {
            field_count++;
        }
    }

    void* mem = allocator_->allocate(sizeof(StructDef), alignof(StructDef));
    auto* def = new (mem) StructDef{};
    def->field_count = field_count;
    def->generic_param_count = static_cast<uint32_t>(decl->type_params.size());
    def->decl = decl;
    def->is_propset = struct_decl_has_annotation(decl, "propset");
    uint32_t struct_alignment = 1;

    if (field_count > 0) {
        void* fields_mem = allocator_->allocate(sizeof(FieldDef) * field_count, alignof(FieldDef));
        def->fields = static_cast<FieldDef*>(fields_mem);

        uint32_t idx = 0;
        uint32_t offset = 0;

        // Calculate field offsets using C++ struct packing rules
        auto process_field = [&](const VarDecl* vd) {
            const Type* field_type = get(vd->type_id_);

            uint32_t field_size = 0;
            uint32_t field_alignment = 1;

            if (field_type) {
                field_size = field_type->size;
                field_alignment = field_type->alignment;
                if (field_alignment == 0) { field_alignment = 1; }
            }
            // else: Type not resolved yet - use 0 size, will be fixed during resolution

            const bool is_unmanaged_array_field = def->is_propset && field_type && field_type->type_kind == TK_array;
            if (is_unmanaged_array_field) {
                // Propset array fields store TypedHandle (u64) inline, not HeapPtr.
                field_size = sizeof(uint64_t);
                field_alignment = alignof(uint64_t);
            }

            struct_alignment = std::max(struct_alignment, field_alignment);
            offset = (offset + field_alignment - 1) & ~(field_alignment - 1);

            auto& field = def->fields[idx];
            field.name = nw::kernel::strings().intern(vd->identifier_.loc.view());
            field.type_id = vd->type_id_;
            field.offset = offset;
            field.generic_param_index = vd->type
                ? find_generic_param_index(vd->type->str(), decl->type_params)
                : int16_t{-1};

            field.is_unmanaged_array = is_unmanaged_array_field;

            offset += field_size;
            idx++;
        };

        for (auto* d : decl->decls) {
            if (auto* vdl = dynamic_cast<const DeclList*>(d)) {
                for (auto* vd : vdl->decls) {
                    process_field(vd);
                }
            } else if (auto* vd = dynamic_cast<const VarDecl*>(d)) {
                process_field(vd);
            }
        }

        def->size = (offset + struct_alignment - 1) & ~(struct_alignment - 1);
    } else {
        def->fields = nullptr;
        def->size = 0;
    }

    def->alignment = struct_alignment;

    StructID struct_id{static_cast<int32_t>(structs_.size()) + 1};
    structs_.push_back(def);
    struct_to_index_.insert({def, structs_.size()});

    String qualified_name;
    if (!module_path.empty()) {
        qualified_name = String(module_path) + "." + String(decl->identifier());
    } else {
        qualified_name = String(decl->identifier());
    }

    Type type{
        .name = nw::kernel::strings().intern(qualified_name),
        .type_params = {struct_id, {}},
        .type_kind = TK_struct,
        .primitive_kind = PK_unspecified,
        .size = def->size,
        .alignment = struct_alignment,
    };
    compute_heap_ref_info(def, type);
    return add(type);
}

const StructDef* TypeTable::get(StructID id) const noexcept
{
    if (id.value < 1 || id.value > structs_.size()) {
        return nullptr;
    }
    return structs_[id.value - 1];
}

const TupleDef* TypeTable::get(TupleID id) const noexcept
{
    if (id.value < 1 || id.value > tuples_.size()) {
        return nullptr;
    }
    return tuples_[id.value - 1];
}

const SumDef* TypeTable::get(SumID id) const noexcept
{
    if (id.value < 1 || id.value > sums_.size()) {
        return nullptr;
    }
    return sums_[id.value - 1];
}

const FunctionDef* TypeTable::get(FunctionID id) const noexcept
{
    if (id.value < 1 || id.value > functions_.size()) {
        return nullptr;
    }
    return functions_[id.value - 1];
}

TupleID TypeTable::add_tuple(TupleDef* tuple_def)
{
    auto it = tuple_to_index_.find(tuple_def);
    if (it != tuple_to_index_.end()) {
        // Duplicate found - free the newly allocated tuple before returning existing one
        if (tuple_def->element_types) {
            allocator_->deallocate(tuple_def->element_types);
        }
        if (tuple_def->offsets) {
            allocator_->deallocate(tuple_def->offsets);
        }
        allocator_->deallocate(tuple_def);
        return TupleID{static_cast<int>(it->second + 1)};
    }

    tuples_.push_back(tuple_def);
    size_t idx = tuples_.size();
    tuple_to_index_[tuple_def] = idx - 1;
    return TupleID{static_cast<int>(idx)};
}

SumID TypeTable::add_sum(SumDef* sum_def)
{
    sums_.push_back(sum_def);
    size_t idx = sums_.size();
    return SumID{static_cast<int>(idx)};
}

StructID TypeTable::add_struct(StructDef* struct_def)
{
    structs_.push_back(struct_def);
    size_t idx = structs_.size();
    struct_to_index_[struct_def] = idx - 1;
    return StructID{static_cast<int>(idx)};
}

FunctionID TypeTable::add_function(FunctionDef* func_def)
{
    auto it = function_to_index_.find(func_def);
    if (it != function_to_index_.end()) {
        if (func_def->param_types) {
            allocator_->deallocate(func_def->param_types);
        }
        allocator_->deallocate(func_def);
        return FunctionID{static_cast<int>(it->second + 1)};
    }

    functions_.push_back(func_def);
    size_t idx = functions_.size();
    function_to_index_[func_def] = idx - 1;
    return FunctionID{static_cast<int>(idx)};
}

TypeID TypeTable::add(const TypeAlias* decl, StringView module_path, TypeID aliased_type_id)
{
    String qualified_name;
    if (!module_path.empty()) {
        qualified_name = String(module_path) + "." + String(decl->identifier());
    } else {
        qualified_name = String(decl->identifier());
    }

    return add({
        .name = nw::kernel::strings().intern(qualified_name),
        .type_params = {aliased_type_id, {}},
        .type_kind = TK_alias,
        .primitive_kind = PK_unspecified,
    });
}

TypeID TypeTable::add(const NewtypeDecl* decl, StringView module_path, TypeID wrapped_type_id)
{
    String qualified_name;
    if (!module_path.empty()) {
        qualified_name = String(module_path) + "." + String(decl->identifier());
    } else {
        qualified_name = String(decl->identifier());
    }

    return add({
        .name = nw::kernel::strings().intern(qualified_name),
        .type_params = {wrapped_type_id, {}},
        .type_kind = TK_newtype,
        .primitive_kind = PK_unspecified,
    });
}

TypeID TypeTable::add(Type type)
{
    auto it = type_to_index_.find(type);
    if (it != type_to_index_.end()) {
        return TypeID{it->second};
    }

    int idx = static_cast<int>(types_.size()) + 1;
    types_.push_back(type);
    name_to_index_.insert({type.name, idx});
    type_to_index_.insert({type, idx});
    return TypeID{idx};
}

const Type* TypeTable::get(TypeID id) const
{
    if (id == invalid_type_id || id.value < 1 || id.value > types_.size()) {
        return nullptr;
    }

    return &types_[id.value - 1];
}

const Type* TypeTable::get(std::string_view name) const
{
    auto it = name_to_index_.find(name);
    if (it == std::end(name_to_index_)) { return nullptr; }
    return &types_[it->second - 1];
}

void TypeTable::define(TypeID id, const SumDecl* decl, StringView module_path)
{
    if (id == invalid_type_id || id.value < 1 || id.value > types_.size()) {
        return;
    }

    uint32_t variant_count = static_cast<uint32_t>(decl->variants.size());
    if (variant_count == 0) {
        return;
    }

    void* mem = allocator_->allocate(sizeof(SumDef), alignof(SumDef));
    auto* def = new (mem) SumDef{};
    def->variant_count = variant_count;
    def->generic_param_count = static_cast<uint32_t>(decl->type_params.size());
    def->decl = decl;

    void* variants_mem = allocator_->allocate(sizeof(VariantDef) * variant_count, alignof(VariantDef));
    def->variants = static_cast<VariantDef*>(variants_mem);

    uint32_t tag_size = sizeof(uint32_t);
    uint32_t tag_alignment = alignof(uint32_t);
    uint32_t max_payload_size = 0;
    uint32_t max_payload_alignment = tag_alignment;
    bool contains_heap_refs = false;

    for (uint32_t i = 0; i < variant_count; ++i) {
        auto* variant_decl = decl->variants[i];
        auto& variant = def->variants[i];

        variant.name = nw::kernel::strings().intern(variant_decl->identifier_.loc.view());
        variant.tag_value = i;

        if (variant_decl->is_unit()) {
            variant.payload_type = invalid_type_id;
            variant.payload_offset = 0;
            variant.generic_param_index = -1;
        } else {
            variant.payload_type = variant_decl->payload->type_id_;
            auto* type_expr = dynamic_cast<TypeExpression*>(variant_decl->payload);
            variant.generic_param_index = type_expr
                ? find_generic_param_index(type_expr->str(), decl->type_params)
                : int16_t{-1};

            const Type* payload_type = get(variant.payload_type);

            if (payload_type) {
                if (is_heap_type(variant.payload_type) || payload_type->contains_heap_refs) {
                    contains_heap_refs = true;
                }

                uint32_t payload_size = payload_type->size;
                uint32_t payload_alignment = payload_type->alignment;
                if (payload_alignment == 0) { payload_alignment = 1; }

                max_payload_size = std::max(max_payload_size, payload_size);
                max_payload_alignment = std::max(max_payload_alignment, payload_alignment);
            }
        }
    }

    def->tag_offset = 0;
    uint32_t union_offset = (tag_size + max_payload_alignment - 1) & ~(max_payload_alignment - 1);
    def->union_offset = union_offset;
    def->union_size = max_payload_size;

    for (uint32_t i = 0; i < variant_count; ++i) {
        // Payload offsets are relative to the start of the union.
        def->variants[i].payload_offset = 0;
    }

    uint32_t total_size = union_offset + max_payload_size;
    uint32_t struct_alignment = max_payload_alignment;
    total_size = (total_size + struct_alignment - 1) & ~(struct_alignment - 1);

    def->size = total_size;
    def->alignment = struct_alignment;

    SumID sum_id{static_cast<int32_t>(sums_.size()) + 1};
    sums_.push_back(def);
    sum_to_index_.insert({decl, sums_.size() - 1});

    Type& type = types_[id.value - 1];
    type.type_params = {sum_id, {}};
    type.type_kind = TK_sum;
    type.size = def->size;
    type.alignment = def->alignment;
    type.contains_heap_refs = contains_heap_refs;

    type_to_index_.insert({type, id.value});
}

TypeID TypeTable::add(const SumDecl* decl, StringView module_path)
{
    uint32_t variant_count = static_cast<uint32_t>(decl->variants.size());
    if (variant_count == 0) {
        return invalid_type_id;
    }

    void* mem = allocator_->allocate(sizeof(SumDef), alignof(SumDef));
    auto* def = new (mem) SumDef{};
    def->variant_count = variant_count;
    def->generic_param_count = static_cast<uint32_t>(decl->type_params.size());
    def->decl = decl;

    void* variants_mem = allocator_->allocate(sizeof(VariantDef) * variant_count, alignof(VariantDef));
    def->variants = static_cast<VariantDef*>(variants_mem);

    uint32_t tag_size = sizeof(uint32_t);
    uint32_t tag_alignment = alignof(uint32_t);
    uint32_t max_payload_size = 0;
    uint32_t max_payload_alignment = tag_alignment;
    bool contains_heap_refs = false;

    for (uint32_t i = 0; i < variant_count; ++i) {
        auto* variant_decl = decl->variants[i];
        auto& variant = def->variants[i];

        variant.name = nw::kernel::strings().intern(variant_decl->identifier_.loc.view());
        variant.tag_value = i;

        if (variant_decl->is_unit()) {
            variant.payload_type = invalid_type_id;
            variant.payload_offset = 0;
            variant.generic_param_index = -1;
        } else {
            variant.payload_type = variant_decl->payload->type_id_;
            auto* type_expr = dynamic_cast<TypeExpression*>(variant_decl->payload);
            variant.generic_param_index = type_expr
                ? find_generic_param_index(type_expr->str(), decl->type_params)
                : int16_t{-1};

            const Type* payload_type = get(variant.payload_type);

            if (payload_type) {
                if (is_heap_type(variant.payload_type) || payload_type->contains_heap_refs) {
                    contains_heap_refs = true;
                }

                uint32_t payload_size = payload_type->size;
                uint32_t payload_alignment = payload_type->alignment;
                if (payload_alignment == 0) { payload_alignment = 1; }

                max_payload_size = std::max(max_payload_size, payload_size);
                max_payload_alignment = std::max(max_payload_alignment, payload_alignment);
            }
        }
    }

    def->tag_offset = 0;
    uint32_t union_offset = (tag_size + max_payload_alignment - 1) & ~(max_payload_alignment - 1);
    def->union_offset = union_offset;
    def->union_size = max_payload_size;

    for (uint32_t i = 0; i < variant_count; ++i) {
        // Payload offsets are relative to the start of the union.
        def->variants[i].payload_offset = 0;
    }

    uint32_t total_size = union_offset + max_payload_size;
    uint32_t struct_alignment = max_payload_alignment;
    total_size = (total_size + struct_alignment - 1) & ~(struct_alignment - 1);

    def->size = total_size;
    def->alignment = struct_alignment;

    SumID sum_id{static_cast<int32_t>(sums_.size()) + 1};
    sums_.push_back(def);
    sum_to_index_.insert({decl, sums_.size() - 1});

    String qualified_name;
    if (!module_path.empty()) {
        qualified_name = String(module_path) + "." + String(decl->identifier());
    } else {
        qualified_name = String(decl->identifier());
    }

    return add({
        .name = nw::kernel::strings().intern(qualified_name),
        .type_params = {sum_id, {}},
        .type_kind = TK_sum,
        .primitive_kind = PK_unspecified,
        .size = def->size,
        .alignment = def->alignment,
        .contains_heap_refs = contains_heap_refs,
    });
}

bool TypeTable::is_heap_type(TypeID tid) const
{
    const Type* type = get(tid);
    if (!type) return false;

    if (type->type_kind == TK_newtype) {
        TypeID wrapped = type->type_params[0].as<TypeID>();
        if (wrapped == invalid_type_id || wrapped == tid) {
            return false;
        }
        return is_heap_type(wrapped);
    }

    switch (type->type_kind) {
    case TK_primitive:
        return type->primitive_kind == PK_string;
    case TK_array:
    case TK_map:
    case TK_tuple:
    case TK_sum:
    case TK_function:
        return true;
    case TK_struct:
        return true;
    default:
        return false;
    }
}

void TypeTable::compute_heap_ref_info(StructDef* def, Type& type)
{
    if (!def || def->field_count == 0) {
        type.contains_heap_refs = false;
        return;
    }

    std::vector<uint32_t> heap_offsets;

    auto append_value_type_offsets = [&](auto&& self, TypeID tid, uint32_t base_offset) -> void {
        const Type* t = get(tid);
        if (!t || !t->contains_heap_refs) {
            return;
        }

        // Heap-reference types are stored as HeapPtr at the start of their slot.
        if (is_heap_type(tid)) {
            heap_offsets.push_back(base_offset);
            return;
        }

        if (t->type_kind == TK_struct) {
            if (!t->type_params[0].is<StructID>()) {
                return;
            }
            StructID sid = t->type_params[0].as<StructID>();
            const StructDef* nested_def = this->get(sid);
            if (!nested_def) {
                return;
            }
            for (uint32_t j = 0; j < nested_def->heap_ref_count; ++j) {
                heap_offsets.push_back(base_offset + nested_def->heap_ref_offsets[j]);
            }
            return;
        }

        if (t->type_kind == TK_fixed_array) {
            TypeID elem_tid = t->type_params[0].as<TypeID>();
            int32_t count = t->type_params[1].as<int32_t>();
            const Type* elem_type = get(elem_tid);
            if (!elem_type || count <= 0) {
                return;
            }

            uint32_t elem_size = elem_type->size;
            for (int32_t i = 0; i < count; ++i) {
                self(self, elem_tid, base_offset + static_cast<uint32_t>(i) * elem_size);
            }
            return;
        }
    };

    for (uint32_t i = 0; i < def->field_count; ++i) {
        const FieldDef& field = def->fields[i];
        const Type* field_type = get(field.type_id);

        if (!field_type) continue;

        // Skip unmanaged array fields - they're not GC-managed
        if (field.is_unmanaged_array) {
            continue;
        }

        if (is_heap_type(field.type_id)) {
            heap_offsets.push_back(field.offset);
        } else if (field_type->contains_heap_refs) {
            append_value_type_offsets(append_value_type_offsets, field.type_id, field.offset);
        }
    }

    if (!heap_offsets.empty()) {
        type.contains_heap_refs = true;
        def->heap_ref_count = static_cast<uint32_t>(heap_offsets.size());
        void* mem = allocator_->allocate(sizeof(uint32_t) * heap_offsets.size(), alignof(uint32_t));
        def->heap_ref_offsets = static_cast<uint32_t*>(mem);
        std::memcpy(def->heap_ref_offsets, heap_offsets.data(), sizeof(uint32_t) * heap_offsets.size());
    } else {
        type.contains_heap_refs = false;
        def->heap_ref_count = 0;
        def->heap_ref_offsets = nullptr;
    }
}

} // namespace nw::smalls
