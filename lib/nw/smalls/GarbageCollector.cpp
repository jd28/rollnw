#include "GarbageCollector.hpp"

#include "../kernel/Kernel.hpp"
#include "VirtualMachine.hpp"
#include "runtime.hpp"

#include <chrono>

namespace nw::smalls {

GarbageCollector::GarbageCollector(ScriptHeap* heap, Runtime* runtime)
    : heap_(heap)
    , runtime_(runtime)
{
    gray_stack_.reserve(1024);
    heap_->set_gc(this);
}

GarbageCollector::~GarbageCollector()
{
    if (heap_) {
        heap_->set_gc(nullptr);
    }
}

bool GarbageCollector::is_young(HeapPtr ptr) const
{
    if (ptr.value == 0) return false;
    auto* header = heap_->try_get_header(ptr);
    if (!header) return false;
    return header->generation == 0;
}

bool GarbageCollector::is_old(HeapPtr ptr) const
{
    if (ptr.value == 0) return false;
    auto* header = heap_->try_get_header(ptr);
    if (!header) return false;
    return header->generation == 1;
}

void GarbageCollector::register_object(HeapPtr ptr, size_t size)
{
    if (ptr.value == 0) return;

    auto* header = heap_->get_header(ptr);
    header->generation = 0;
    header->age = 0;
    header->mark_color = 0;

    heap_->add_young_bytes(size);
}

void GarbageCollector::write_barrier(HeapPtr target, HeapPtr stored_value)
{
    if (target.value == 0 || stored_value.value == 0) { return; }
    auto* target_header = heap_->try_get_header(target);
    auto* value_header = heap_->try_get_header(stored_value);
    if (!target_header || !value_header) {
        return;
    }

    if (target_header->generation == 1 && value_header->generation == 0) {
        card_table_.mark_dirty(target.value);
    }

    if (phase_ == GCPhase::mark_incremental) {
        write_barrier_marking(target, stored_value);
    }
}

void GarbageCollector::write_barrier_marking(HeapPtr target, HeapPtr stored_value)
{
    if (target.value == 0 || stored_value.value == 0) { return; }

    auto* target_header = heap_->try_get_header(target);
    auto* value_header = heap_->try_get_header(stored_value);
    if (!target_header || !value_header) {
        return;
    }

    if (target_header->mark_color == static_cast<uint8_t>(MarkColor::BLACK) && value_header->mark_color == static_cast<uint8_t>(MarkColor::WHITE)) {
        shade_gray(stored_value);
    }
}

void GarbageCollector::write_barrier_root(HeapPtr stored_value)
{
    if (phase_ != GCPhase::mark_incremental || stored_value.value == 0) { return; }
    auto* header = heap_->try_get_header(stored_value);
    if (header && header->mark_color == static_cast<uint8_t>(MarkColor::WHITE)) {
        shade_gray(stored_value);
    }
}

void GarbageCollector::on_allocation(size_t size)
{
    if (phase_ == GCPhase::idle && heap_->old_bytes() > config_.major_threshold_percent * heap_->committed()) {
        start_major_gc();
    }

    if (phase_ == GCPhase::mark_incremental) {
        if (mark_step(config_.incremental_work_budget)) {
            finish_major_gc();
        }
    }
}

void GarbageCollector::collect_minor()
{
    auto start = std::chrono::high_resolution_clock::now();

    phase_ = GCPhase::mark_roots;
    mark_roots(true);

    mark_from_dirty_cards();

    phase_ = GCPhase::mark_incremental;
    process_gray_stack(true);

    phase_ = GCPhase::sweep;
    sweep_young();

    // After sweeping, age/promote the surviving young objects and clear their marks.
    promote_survivors();
    heap_->set_young_bytes(0);

    phase_ = GCPhase::idle;
    stats_.minor_collections++;

    auto end = std::chrono::high_resolution_clock::now();
    uint64_t pause_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
    stats_.total_pause_time_us += pause_us;
    if (pause_us > stats_.max_pause_time_us) {
        stats_.max_pause_time_us = pause_us;
    }
}

void GarbageCollector::collect_major()
{
    auto start = std::chrono::high_resolution_clock::now();

    HeapPtr current = heap_->all_objects();
    while (current.value != 0) {
        auto* header = heap_->get_header(current);
        header->mark_color = static_cast<uint8_t>(MarkColor::WHITE);
        current = header->next_object;
    }

    phase_ = GCPhase::mark_roots;
    mark_roots(false);

    phase_ = GCPhase::mark_incremental;
    process_gray_stack(false);

    phase_ = GCPhase::sweep;
    sweep_all();

    heap_->set_young_bytes(0);

    phase_ = GCPhase::idle;
    stats_.major_collections++;

    auto end = std::chrono::high_resolution_clock::now();
    uint64_t pause_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
    stats_.total_pause_time_us += pause_us;
    if (pause_us > stats_.max_pause_time_us) {
        stats_.max_pause_time_us = pause_us;
    }
}

void GarbageCollector::collect_full()
{
    collect_major();
}

void GarbageCollector::start_major_gc()
{
    HeapPtr current = heap_->all_objects();
    while (current.value != 0) {
        auto* header = heap_->get_header(current);
        header->mark_color = static_cast<uint8_t>(MarkColor::WHITE);
        current = header->next_object;
    }

    phase_ = GCPhase::mark_roots;
    mark_roots(false);
    phase_ = GCPhase::mark_incremental;
}

void GarbageCollector::finish_major_gc()
{
    auto start = std::chrono::high_resolution_clock::now();

    phase_ = GCPhase::sweep;
    sweep_all();
    heap_->set_young_bytes(0);
    phase_ = GCPhase::idle;
    stats_.major_collections++;

    auto end = std::chrono::high_resolution_clock::now();
    uint64_t pause_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
    stats_.total_pause_time_us += pause_us;
    if (pause_us > stats_.max_pause_time_us) {
        stats_.max_pause_time_us = pause_us;
    }
}

bool GarbageCollector::mark_step(size_t work_budget)
{
    if (phase_ != GCPhase::mark_incremental) {
        return true;
    }

    size_t work_done = 0;
    while (!gray_stack_.empty() && work_done < work_budget) {
        HeapPtr ptr = gray_stack_.back();
        gray_stack_.pop_back();
        trace_object(ptr, false);
        set_black(ptr);
        work_done++;
    }

    return gray_stack_.empty();
}

void GarbageCollector::mark_roots(bool young_only)
{
    struct RootVisitor : GCRootVisitor {
        GarbageCollector* gc;
        bool young_only;

        explicit RootVisitor(GarbageCollector* g, bool yo)
            : gc(g)
            , young_only(yo)
        {
        }

        void visit_root(HeapPtr* ptr) override
        {
            if (!ptr || ptr->value == 0) return;

            auto* header = gc->heap_->try_get_header(*ptr);
            if (!header) {
                return;
            }
            if (young_only && header->generation != 0) {
                return;
            }

            if (header->mark_color == static_cast<uint8_t>(MarkColor::WHITE)) {
                gc->shade_gray(*ptr);
            }
        }
    };

    RootVisitor visitor(this, young_only);

    if (vm_) {
        vm_->enumerate_roots(visitor, runtime_);
    }

    for (auto& value : runtime_->stack_) {
        if (value.storage == ValueStorage::heap && value.data.hptr.value != 0) {
            visitor.visit_root(&value.data.hptr);
        }
    }

    runtime_->enumerate_module_globals(visitor);
    runtime_->enumerate_handle_roots(visitor);
    runtime_->enumerate_propset_roots(visitor, young_only);
}

void GarbageCollector::mark_from_dirty_cards()
{
    HeapPtr current = heap_->all_objects();
    while (current.value != 0) {
        auto* header = heap_->get_header(current);

        if (header->generation == 1) {
            uint32_t obj_start = current.value;
            uint32_t obj_end = obj_start + header->alloc_size;

            if (card_table_.is_range_dirty(obj_start, obj_end)) {
                bool has_young_refs = trace_object(current, true);
                if (!has_young_refs) {
                    card_table_.clear_range(obj_start, obj_end);
                }
            }
        }

        current = header->next_object;
    }
}

void GarbageCollector::process_gray_stack(bool young_only)
{
    while (!gray_stack_.empty()) {
        HeapPtr ptr = gray_stack_.back();
        gray_stack_.pop_back();
        trace_object(ptr, young_only);
        set_black(ptr);
    }
}

void GarbageCollector::promote_survivors()
{
    HeapPtr current = heap_->all_objects();
    while (current.value != 0) {
        auto* header = heap_->get_header(current);

        if (header->generation == 0 && header->mark_color != static_cast<uint8_t>(MarkColor::WHITE)) {
            header->age++;
            if (header->age >= config_.promotion_threshold) {
                header->generation = 1;
                heap_->add_old_bytes(header->alloc_size);
                // Conservatively rescan newly-old objects in the next minor collection.
                // They may contain young refs that were written while the object was still young.
                card_table_.mark_dirty(current.value);
            }
            header->mark_color = 0;
        }

        current = header->next_object;
    }
}

void GarbageCollector::sweep_young()
{
    HeapPtr* prev = nullptr;
    HeapPtr prev_ptr{0};

    HeapPtr current = heap_->all_objects();
    size_t freed_count = 0;
    size_t freed_bytes = 0;

    while (current.value != 0) {
        auto* header = heap_->get_header(current);
        HeapPtr next = header->next_object;

        if (header->generation == 0 && header->mark_color == static_cast<uint8_t>(MarkColor::WHITE)) {
            if (prev_ptr.value == 0) {
                heap_->set_all_objects(next);
            } else {
                auto* prev_header = heap_->get_header(prev_ptr);
                prev_header->next_object = next;
            }

            freed_bytes += header->alloc_size;
            freed_count++;
            runtime_->destruct_object(current);
            heap_->free(current);
        } else {
            prev_ptr = current;
        }

        current = next;
    }

    stats_.objects_freed += freed_count;
    stats_.bytes_freed += freed_bytes;
}

void GarbageCollector::sweep_all()
{
    HeapPtr prev_ptr{0};
    HeapPtr current = heap_->all_objects();
    size_t freed_count = 0;
    size_t freed_bytes = 0;
    size_t new_old_bytes = 0;

    while (current.value != 0) {
        auto* header = heap_->get_header(current);
        HeapPtr next = header->next_object;

        if (header->mark_color == static_cast<uint8_t>(MarkColor::WHITE)) {
            if (prev_ptr.value == 0) {
                heap_->set_all_objects(next);
            } else {
                auto* prev_header = heap_->get_header(prev_ptr);
                prev_header->next_object = next;
            }

            freed_bytes += header->alloc_size;
            freed_count++;
            runtime_->destruct_object(current);
            heap_->free(current);
        } else {
            header->mark_color = 0;
            if (header->generation == 1) {
                new_old_bytes += header->alloc_size;
            }
            prev_ptr = current;
        }

        current = next;
    }

    heap_->set_old_bytes(new_old_bytes);
    stats_.objects_freed += freed_count;
    stats_.bytes_freed += freed_bytes;
}

bool GarbageCollector::trace_object(HeapPtr ptr, bool young_only)
{
    if (ptr.value == 0) { return false; }

    auto* header = heap_->try_get_header(ptr);
    if (!header) {
        return false;
    }
    const Type* type = runtime_->get_type(header->type_id);
    if (!type) { return false; }

    bool has_young_refs = false;

    auto try_mark_child = [this, young_only, &has_young_refs](HeapPtr child) {
        if (child.value == 0) return;
        auto* child_header = heap_->try_get_header(child);
        if (!child_header) { return; } // back-pointer corrupted; skip safely
        if (young_only && child_header->generation != 0) {
            return;
        }
        if (child_header->generation == 0) {
            has_young_refs = true;
        }
        if (child_header->mark_color == static_cast<uint8_t>(MarkColor::WHITE)) {
            shade_gray(child);
        }
    };

    switch (type->type_kind) {
    case TK_primitive:
        if (type->primitive_kind == PK_string) {
            StringRepr* sr = static_cast<StringRepr*>(heap_->get_ptr(ptr));
            if (sr->backing.value != 0) {
                auto* backing_header = heap_->try_get_header(sr->backing);
                if (!backing_header) {
                    break;
                }
                if (!young_only || backing_header->generation == 0) {
                    if (backing_header->generation == 0) {
                        has_young_refs = true;
                    }
                    if (backing_header->mark_color == static_cast<uint8_t>(MarkColor::WHITE)) {
                        // String backing has no outgoing heap refs; mark it directly.
                        backing_header->mark_color = static_cast<uint8_t>(MarkColor::BLACK);
                    }
                }
            }
        }
        break;

    case TK_struct:
    case TK_tuple: {
        uint8_t* data = static_cast<uint8_t*>(heap_->get_ptr(ptr));
        runtime_->scan_value_heap_refs(header->type_id, data, [&](HeapPtr* child_ptr) {
            try_mark_child(*child_ptr);
        });
        break;
    }

    case TK_array: {
        TypeID elem_type_id = type->type_params[0].as<TypeID>();
        const Type* elem_type = runtime_->get_type(elem_type_id);
        if (!elem_type) { return false; }

        bool is_heap = runtime_->type_table_.is_heap_type(elem_type_id);
        if (!is_heap && !elem_type->contains_heap_refs) return has_young_refs;

        if (type->type_params[1].empty()) {
            IArray* arr = static_cast<IArray*>(heap_->get_ptr(ptr));
            if (!arr) { return false; }

            if (auto* heap_arr = dynamic_cast<TypedArray<HeapPtr>*>(arr)) {
                for (const HeapPtr& hp : heap_arr->elements) {
                    try_mark_child(hp);
                }
            } else if (auto* struct_arr = dynamic_cast<StructArray*>(arr)) {
                if (elem_type->contains_heap_refs) {
                    for (size_t i = 0; i < struct_arr->count; ++i) {
                        uint8_t* elem_base = struct_arr->buffer.data() + (i * struct_arr->elem_size);
                        runtime_->scan_value_heap_refs(elem_type_id, elem_base, [&](HeapPtr* child_ptr) {
                            try_mark_child(*child_ptr);
                        });
                    }
                }
            } else {
                // Fallback for any other IArray impls.
                Value val;
                for (size_t i = 0; i < arr->size(); ++i) {
                    if (arr->get_value(i, val, *runtime_)) {
                        if (val.storage == ValueStorage::heap) {
                            try_mark_child(val.data.hptr);
                        }
                    }
                }
            }
        } else {
            uint8_t* data = static_cast<uint8_t*>(heap_->get_ptr(ptr));
            uint32_t count = *reinterpret_cast<uint32_t*>(data);

            size_t header_size = sizeof(uint32_t);
            size_t alignment = elem_type->alignment ? static_cast<size_t>(elem_type->alignment) : 1;
            size_t elements_start = (header_size + alignment - 1) & ~(alignment - 1);

            uint8_t* elements_data = data + elements_start;
            if (is_heap) {
                for (uint32_t i = 0; i < count; ++i) {
                    HeapPtr* child_ptr = reinterpret_cast<HeapPtr*>(elements_data + i * elem_type->size);
                    try_mark_child(*child_ptr);
                }
            } else if (elem_type->contains_heap_refs) {
                for (uint32_t i = 0; i < count; ++i) {
                    uint8_t* elem_base = elements_data + i * elem_type->size;
                    runtime_->scan_value_heap_refs(elem_type_id, elem_base, [&](HeapPtr* child_ptr) {
                        try_mark_child(*child_ptr);
                    });
                }
            }
        }
        break;
    }

    case TK_map: {
        MapInstance* map = static_cast<MapInstance*>(heap_->get_ptr(ptr));
        if (!map) { return false; }

        bool key_is_heap = runtime_->type_table_.is_heap_type(map->key_type);
        bool val_is_heap = runtime_->type_table_.is_heap_type(map->value_type);

        for (auto& [key, val] : map->data) {
            if (key_is_heap && key.storage == ValueStorage::heap) {
                try_mark_child(key.data.hptr);
            }
            if (val_is_heap && val.storage == ValueStorage::heap) {
                try_mark_child(val.data.hptr);
            }
        }
        break;
    }

    case TK_function: {
        Closure* closure = static_cast<Closure*>(heap_->get_ptr(ptr));
        if (!closure) { return false; }

        for (Upvalue* uv : closure->upvalues) {
            if (!uv) continue;

            if (uv->heap_ptr.value != 0) {
                try_mark_child(uv->heap_ptr);
            }

            if (!uv->is_open() && uv->closed.storage == ValueStorage::heap) {
                try_mark_child(uv->closed.data.hptr);
            }
        }
        break;
    }

    case TK_sum:
    case TK_fixed_array: {
        if (!type->contains_heap_refs) return has_young_refs;
        uint8_t* data = static_cast<uint8_t*>(heap_->get_ptr(ptr));
        runtime_->scan_value_heap_refs(header->type_id, data, [&](HeapPtr* child_ptr) {
            try_mark_child(*child_ptr);
        });
        break;
    }

    default:
        break;
    }

    return has_young_refs;
}

void GarbageCollector::shade_gray(HeapPtr ptr)
{
    if (ptr.value == 0) return;

    auto* header = heap_->try_get_header(ptr);
    if (!header) {
        return;
    }
    header->mark_color = static_cast<uint8_t>(MarkColor::GRAY);
    gray_stack_.push_back(ptr);
}

void GarbageCollector::set_black(HeapPtr ptr)
{
    if (ptr.value == 0) return;

    auto* header = heap_->try_get_header(ptr);
    if (!header) {
        return;
    }
    header->mark_color = static_cast<uint8_t>(MarkColor::BLACK);
}

} // namespace nw::smalls
