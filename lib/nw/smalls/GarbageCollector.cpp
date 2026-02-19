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
    remembered_objects_.reserve(1024);
    remembered_retained_.reserve(1024);
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
    if (stored_value.value == 0) { return; }
    auto* header = heap_->try_get_header(stored_value);
    if (!header) {
        return;
    }

    const bool major_marking = (phase_ == GCPhase::mark_incremental);
    const bool minor_marking = (minor_phase_ != MinorPhase::idle && header->generation == 0);
    if ((major_marking || minor_marking) && header->mark_color == static_cast<uint8_t>(MarkColor::WHITE)) {
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
    constexpr size_t max_budget = static_cast<size_t>(-1) / 2;
    while (!collect_minor_step(max_budget)) {
    }
}

bool GarbageCollector::collect_minor_step(size_t work_budget)
{
    if (minor_phase_ == MinorPhase::idle && phase_ == GCPhase::mark_incremental) {
        while (!mark_step(config_.incremental_work_budget)) {
        }
        finish_major_gc();
    }

    if (minor_phase_ == MinorPhase::idle) {
        begin_minor_cycle();
    }

    if (work_budget == 0) {
        work_budget = 1;
    }

    const bool has_time_budget = config_.minor_step_time_budget_us > 0;
    const auto deadline = std::chrono::high_resolution_clock::now() + std::chrono::microseconds(config_.minor_step_time_budget_us);
    auto out_of_time = [&]() {
        return has_time_budget && std::chrono::high_resolution_clock::now() >= deadline;
    };

    size_t remaining = work_budget;
    while (remaining > 0) {
        if (out_of_time()) {
            return false;
        }
        switch (minor_phase_) {
        case MinorPhase::mark_roots: {
            auto phase_start = std::chrono::high_resolution_clock::now();
            phase_ = GCPhase::mark_roots;
            mark_roots(true);
            auto phase_end = std::chrono::high_resolution_clock::now();
            stats_.minor_roots_time_us += static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(phase_end - phase_start).count());
            minor_phase_ = MinorPhase::scan_remembered;
            --remaining;
            break;
        }
        case MinorPhase::scan_remembered: {
            auto phase_start = std::chrono::high_resolution_clock::now();
            phase_ = GCPhase::mark_incremental;
            const size_t chunk = std::min<size_t>(remaining, 128);
            const bool done = mark_from_remembered_step(chunk, has_time_budget ? &deadline : nullptr);
            auto phase_end = std::chrono::high_resolution_clock::now();
            stats_.minor_remembered_scan_time_us += static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(phase_end - phase_start).count());
            if (done) {
                minor_phase_ = MinorPhase::trace_gray;
                remembered_scan_cursor_ = 0;
                remembered_objects_.swap(remembered_retained_);
                remembered_retained_.clear();
                remembered_set_.clear();
                remembered_set_.reserve(remembered_objects_.size());
                for (HeapPtr ptr : remembered_objects_) {
                    remembered_set_.insert(ptr.value);
                }
            }
            remaining = 0;
            break;
        }
        case MinorPhase::trace_gray: {
            auto phase_start = std::chrono::high_resolution_clock::now();
            phase_ = GCPhase::mark_incremental;
            const size_t chunk = std::min<size_t>(remaining, 128);
            const bool done = process_gray_stack_step(true, chunk, has_time_budget ? &deadline : nullptr);
            auto phase_end = std::chrono::high_resolution_clock::now();
            stats_.minor_trace_time_us += static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(phase_end - phase_start).count());
            if (done) {
                minor_phase_ = MinorPhase::sweep_promote;
            }
            remaining = 0;
            break;
        }
        case MinorPhase::sweep_promote: {
            auto phase_start = std::chrono::high_resolution_clock::now();
            phase_ = GCPhase::sweep;
            const size_t chunk = std::min<size_t>(remaining, 128);
            const bool done = sweep_and_promote_young_step(chunk, has_time_budget ? &deadline : nullptr);
            auto phase_end = std::chrono::high_resolution_clock::now();
            stats_.minor_sweep_promote_time_us += static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(phase_end - phase_start).count());
            if (done) {
                finish_minor_cycle();
                return true;
            }
            remaining = 0;
            break;
        }
        case MinorPhase::idle:
            return true;
        }
    }

    return minor_phase_ == MinorPhase::idle;
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
    auto to_us = [](auto start, auto end) -> uint64_t {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
    };

    // Inline fast path for root marking
    auto mark_root_ptr = [this, young_only](HeapPtr* ptr) {
        if (!ptr || ptr->value == 0) [[unlikely]] { return; }

        auto* header = heap_->try_get_header(*ptr);
        if (!header) [[unlikely]] { return; }

        if (young_only && header->generation != 0) [[unlikely]] { return; }

        if (header->mark_color == static_cast<uint8_t>(MarkColor::WHITE)) {
            header->mark_color = static_cast<uint8_t>(MarkColor::GRAY);
            gray_stack_.push_back(*ptr);
        }
    };

    uint64_t stack_slots_total = 0;
    uint64_t stack_slots_scanned = 0;
    uint64_t stack_slots_skipped = 0;
    uint64_t register_values_total = 0;
    uint64_t register_values_heap = 0;
    uint64_t runtime_stack_values_total = 0;
    uint64_t runtime_stack_values_heap = 0;
    uint64_t module_global_roots_visited = 0;
    uint64_t handle_roots_visited = 0;

    uint64_t frame_slots_us = 0;
    uint64_t registers_us = 0;
    uint64_t runtime_stack_us = 0;
    uint64_t module_globals_us = 0;
    uint64_t handle_roots_us = 0;

    // VM roots - directly access VM internals to avoid virtual dispatch
    if (vm_) {
        auto frame_slots_start = std::chrono::high_resolution_clock::now();
        // Frame stack roots
        for (auto& frame : vm_->frames_) {
            // Stack-allocated value types
            for (auto& slot : frame.stack_layout_) {
                ++stack_slots_total;
                if (slot.offset >= frame.stack_.size()) {
                    ++stack_slots_skipped;
                    continue;
                }
                if (!slot.scan_heap_refs) {
                    ++stack_slots_skipped;
                    continue;
                }
                ++stack_slots_scanned;
                uint8_t* slot_data = frame.stack_.data() + slot.offset;
                runtime_->scan_value_heap_refs(slot.type_id, slot_data, [&](HeapPtr* child_ptr) {
                    mark_root_ptr(child_ptr);
                });
            }

            // Open upvalues
            for (Upvalue* uv = frame.open_upvalues; uv; uv = uv->next) {
                if (uv->heap_ptr.value != 0) {
                    mark_root_ptr(&uv->heap_ptr);
                }
                if (!uv->is_open() && uv->closed.storage == ValueStorage::heap && uv->closed.data.hptr.value != 0) {
                    mark_root_ptr(&uv->closed.data.hptr);
                }
            }
        }
        frame_slots_us = to_us(frame_slots_start, std::chrono::high_resolution_clock::now());

        auto registers_start = std::chrono::high_resolution_clock::now();
        // Register roots
        for (size_t i = 0; i < vm_->stack_top_; ++i) {
            ++register_values_total;
            Value& value = vm_->registers_[i];
            if (value.storage == ValueStorage::heap && value.data.hptr.value != 0) {
                ++register_values_heap;
                mark_root_ptr(&value.data.hptr);

                // Handle closure upvalues
                const Type* type = runtime_->get_type(value.type_id);
                if (type && type->type_kind == TK_function) {
                    Closure* closure = runtime_->get_closure(value.data.hptr);
                    if (closure) {
                        for (Upvalue* uv : closure->upvalues) {
                            if (!uv) continue;
                            if (uv->heap_ptr.value != 0) {
                                mark_root_ptr(&uv->heap_ptr);
                            }
                            if (!uv->is_open() && uv->closed.storage == ValueStorage::heap && uv->closed.data.hptr.value != 0) {
                                mark_root_ptr(&uv->closed.data.hptr);
                            }
                        }
                    }
                }
            }
        }
        registers_us = to_us(registers_start, std::chrono::high_resolution_clock::now());
    }

    auto runtime_stack_start = std::chrono::high_resolution_clock::now();
    // Runtime stack roots
    for (auto& value : runtime_->stack_) {
        ++runtime_stack_values_total;
        if (value.storage == ValueStorage::heap && value.data.hptr.value != 0) {
            ++runtime_stack_values_heap;
            mark_root_ptr(&value.data.hptr);
        }
    }
    runtime_stack_us = to_us(runtime_stack_start, std::chrono::high_resolution_clock::now());

    // Module globals and other roots via visitor pattern (less frequent)
    enum class RootSource : uint8_t {
        none,
        module_globals,
        handle_roots,
    };

    RootSource source = RootSource::none;
    struct RootVisitor : GCRootVisitor {
        decltype(mark_root_ptr)& marker;
        RootSource* source = nullptr;
        uint64_t* module_globals_visited = nullptr;
        uint64_t* handle_roots_visited = nullptr;

        explicit RootVisitor(decltype(mark_root_ptr)& m, RootSource* src,
            uint64_t* module_count, uint64_t* handle_count)
            : marker(m)
            , source(src)
            , module_globals_visited(module_count)
            , handle_roots_visited(handle_count)
        {
        }

        void visit_root(HeapPtr* ptr) override
        {
            marker(ptr);
            if (!source) {
                return;
            }
            switch (*source) {
            case RootSource::module_globals:
                if (module_globals_visited) {
                    ++(*module_globals_visited);
                }
                break;
            case RootSource::handle_roots:
                if (handle_roots_visited) {
                    ++(*handle_roots_visited);
                }
                break;
            default:
                break;
            }
        }
    };

    RootVisitor visitor(mark_root_ptr, &source, &module_global_roots_visited, &handle_roots_visited);

    source = RootSource::module_globals;
    auto module_globals_start = std::chrono::high_resolution_clock::now();
    runtime_->enumerate_module_globals(visitor);
    module_globals_us = to_us(module_globals_start, std::chrono::high_resolution_clock::now());

    source = RootSource::handle_roots;
    auto handle_roots_start = std::chrono::high_resolution_clock::now();
    runtime_->enumerate_handle_roots(visitor, young_only);
    handle_roots_us = to_us(handle_roots_start, std::chrono::high_resolution_clock::now());

    stats_.minor_roots_frame_slots_us += frame_slots_us;
    stats_.minor_roots_registers_us += registers_us;
    stats_.minor_roots_runtime_stack_us += runtime_stack_us;
    stats_.minor_roots_module_globals_us += module_globals_us;
    stats_.minor_roots_handle_roots_us += handle_roots_us;

    stats_.minor_stack_slots_total += stack_slots_total;
    stats_.minor_stack_slots_scanned += stack_slots_scanned;
    stats_.minor_stack_slots_skipped += stack_slots_skipped;
    stats_.minor_register_values_total += register_values_total;
    stats_.minor_register_values_heap += register_values_heap;
    stats_.minor_runtime_stack_values_total += runtime_stack_values_total;
    stats_.minor_runtime_stack_values_heap += runtime_stack_values_heap;
    stats_.minor_module_global_roots_visited += module_global_roots_visited;
    stats_.minor_handle_roots_visited += handle_roots_visited;

    // Propsets are NOT GC-scanned - all propset data is cleaned up on object destruction
}

void GarbageCollector::process_gray_stack(bool young_only)
{
    // Prefetch next objects to hide memory latency
    constexpr size_t prefetch_distance = 4;

    while (!gray_stack_.empty()) {
        // Prefetch upcoming objects
        for (size_t i = 1; i <= prefetch_distance && i <= gray_stack_.size(); ++i) {
            HeapPtr prefetch_ptr = gray_stack_[gray_stack_.size() - i];
            if (prefetch_ptr.value != 0) {
                void* obj_ptr = heap_->get_ptr(prefetch_ptr);
                rollnw_prefetch(obj_ptr, 1, 3); // Write prefetch, high temporal locality
            }
        }

        HeapPtr ptr = gray_stack_.back();
        gray_stack_.pop_back();
        trace_object(ptr, young_only);
        set_black(ptr);
    }
}

bool GarbageCollector::mark_from_remembered_step(size_t work_budget, const std::chrono::high_resolution_clock::time_point* deadline)
{
    size_t work_done = 0;
    while (remembered_scan_cursor_ < remembered_objects_.size() && work_done < work_budget) {
        if (deadline && std::chrono::high_resolution_clock::now() >= *deadline) {
            return false;
        }
        HeapPtr current = remembered_objects_[remembered_scan_cursor_++];
        auto* header = heap_->try_get_header(current);
        if (!header || header->generation != 1) {
            remembered_set_.erase(current.value);
            ++work_done;
            continue;
        }

        bool has_young_refs = trace_object(current, true);
        if (has_young_refs) {
            remembered_retained_.push_back(current);
        } else {
            remembered_set_.erase(current.value);
            uint32_t obj_start = current.value;
            uint32_t obj_end = obj_start + header->alloc_size;
            card_table_.clear_range(obj_start, obj_end);
        }
        ++work_done;
    }

    return remembered_scan_cursor_ >= remembered_objects_.size();
}

bool GarbageCollector::process_gray_stack_step(bool young_only, size_t work_budget, const std::chrono::high_resolution_clock::time_point* deadline)
{
    size_t work_done = 0;
    while (!gray_stack_.empty() && work_done < work_budget) {
        if (deadline && std::chrono::high_resolution_clock::now() >= *deadline) {
            return false;
        }
        HeapPtr ptr = gray_stack_.back();
        gray_stack_.pop_back();
        trace_object(ptr, young_only);
        set_black(ptr);
        ++work_done;
    }

    return gray_stack_.empty();
}

bool GarbageCollector::sweep_and_promote_young_step(size_t work_budget, const std::chrono::high_resolution_clock::time_point* deadline)
{
    size_t work_done = 0;
    size_t freed_count = 0;
    size_t freed_bytes = 0;

    while (young_sweep_current_.value != 0 && work_done < work_budget) {
        if (deadline && std::chrono::high_resolution_clock::now() >= *deadline) {
            if (freed_count != 0) {
                stats_.objects_freed += freed_count;
                stats_.bytes_freed += freed_bytes;
            }
            return false;
        }
        HeapPtr current = young_sweep_current_;
        auto* header = heap_->try_get_header(current);

        HeapPtr next_young{0};
        if (header) {
            next_young = header->next_young;
        }

        // Skip objects that are not in young generation (shouldn't happen often)
        if (!header || header->generation != 0) {
            // Remove from young list
            if (young_sweep_prev_.value == 0) {
                heap_->set_young_objects(next_young);
            } else {
                auto* prev_header = heap_->try_get_header(young_sweep_prev_);
                if (prev_header) {
                    prev_header->next_young = next_young;
                } else {
                    heap_->set_young_objects(next_young);
                    young_sweep_prev_ = HeapPtr{0};
                }
            }
            // Track in all_objects list - this object stays in place
            young_sweep_all_prev_ = current;
            young_sweep_current_ = next_young;
            ++work_done;
            continue;
        }

        const bool white = (header->mark_color == static_cast<uint8_t>(MarkColor::WHITE));
        const bool protected_handle = white && runtime_->is_non_vm_owned_handle_cell(current);
        if (white && !protected_handle) {
            HeapPtr next_obj = header->next_object;

            // O(1) removal from all_objects list using tracked previous pointer
            if (young_sweep_all_prev_.value == 0) {
                heap_->set_all_objects(next_obj);
            } else {
                auto* prev_header = heap_->get_header(young_sweep_all_prev_);
                prev_header->next_object = next_obj;
            }

            // Remove from young list
            if (young_sweep_prev_.value == 0) {
                heap_->set_young_objects(next_young);
            } else {
                auto* prev_header = heap_->try_get_header(young_sweep_prev_);
                if (prev_header) {
                    prev_header->next_young = next_young;
                } else {
                    heap_->set_young_objects(next_young);
                    young_sweep_prev_ = HeapPtr{0};
                }
            }

            freed_bytes += header->alloc_size;
            ++freed_count;
            runtime_->destruct_object(current);
            heap_->free(current);
            // young_sweep_all_prev_ stays the same (we removed current, so prev is still prev)
            young_sweep_current_ = next_young;
            ++work_done;
            continue;
        }

        header->age++;
        if (header->age >= config_.promotion_threshold) {
            header->generation = 1;
            heap_->add_old_bytes(header->alloc_size);
            card_table_.mark_dirty(current.value);
            enqueue_remembered_object(current);

            // Remove from young list (promoted to old)
            if (young_sweep_prev_.value == 0) {
                heap_->set_young_objects(next_young);
            } else {
                auto* prev_header = heap_->try_get_header(young_sweep_prev_);
                if (prev_header) {
                    prev_header->next_young = next_young;
                } else {
                    heap_->set_young_objects(next_young);
                    young_sweep_prev_ = HeapPtr{0};
                }
            }
            // Track in all_objects list - this object stays in place
            young_sweep_all_prev_ = current;
        } else {
            // Object stays in young generation - advance both trackers
            young_sweep_prev_ = current;
            young_sweep_all_prev_ = current;
        }

        header->mark_color = 0;
        young_sweep_current_ = next_young;
        ++work_done;
    }

    if (freed_count != 0) {
        stats_.objects_freed += freed_count;
        stats_.bytes_freed += freed_bytes;
    }

    return young_sweep_current_.value == 0;
}

void GarbageCollector::begin_minor_cycle()
{
    minor_start_time_ = std::chrono::high_resolution_clock::now();
    gray_stack_.clear();

    HeapPtr current = heap_->young_objects();
    while (current.value != 0) {
        auto* header = heap_->try_get_header(current);
        if (!header || header->generation != 0) {
            current = header ? header->next_young : HeapPtr{0};
            continue;
        }
        header->mark_color = static_cast<uint8_t>(MarkColor::WHITE);
        current = header->next_young;
    }

    remembered_scan_cursor_ = 0;
    remembered_retained_.clear();
    young_sweep_prev_ = HeapPtr{0};
    young_sweep_all_prev_ = HeapPtr{0};
    young_sweep_current_ = heap_->young_objects();
    minor_phase_ = MinorPhase::mark_roots;
}

void GarbageCollector::finish_minor_cycle()
{
    phase_ = GCPhase::idle;
    minor_phase_ = MinorPhase::idle;
    remembered_scan_cursor_ = 0;
    young_sweep_prev_ = HeapPtr{0};
    young_sweep_all_prev_ = HeapPtr{0};
    young_sweep_current_ = HeapPtr{0};
    ++stats_.minor_collections;

    auto end = std::chrono::high_resolution_clock::now();
    uint64_t pause_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(end - minor_start_time_).count());
    stats_.total_pause_time_us += pause_us;
    if (pause_us > stats_.max_pause_time_us) {
        stats_.max_pause_time_us = pause_us;
    }
}

void GarbageCollector::enqueue_remembered_object(HeapPtr ptr)
{
    if (ptr.value == 0) {
        return;
    }
    if (remembered_set_.insert(ptr.value).second) {
        remembered_objects_.push_back(ptr);
    }
}

void GarbageCollector::sweep_all()
{
    HeapPtr prev_ptr{0};
    HeapPtr current = heap_->all_objects();
    size_t freed_count = 0;
    size_t freed_bytes = 0;
    size_t new_old_bytes = 0;
    HeapPtr new_young_head{0};

    while (current.value != 0) {
        auto* header = heap_->get_header(current);
        HeapPtr next = header->next_object;

        const bool white = (header->mark_color == static_cast<uint8_t>(MarkColor::WHITE));
        const bool protected_handle = white && runtime_->is_non_vm_owned_handle_cell(current);
        if (white && !protected_handle) {
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
            } else {
                header->next_young = new_young_head;
                new_young_head = current;
            }
            prev_ptr = current;
        }

        current = next;
    }

    heap_->set_young_objects(new_young_head);
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
                    // Prefetch next element
                    if (i + 1 < count) {
                        rollnw_prefetch(elements_data + (i + 1) * elem_type->size, 0, 1);
                    }
                    HeapPtr* child_ptr = reinterpret_cast<HeapPtr*>(elements_data + i * elem_type->size);
                    try_mark_child(*child_ptr);
                }
            } else if (elem_type->contains_heap_refs) {
                for (uint32_t i = 0; i < count; ++i) {
                    // Prefetch next element
                    if (i + 1 < count) {
                        rollnw_prefetch(elements_data + (i + 1) * elem_type->size, 0, 1);
                    }
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

        auto it = map->data.begin();
        auto end = map->data.end();
        while (it != end) {
            auto& [key, val] = *it;

            // Prefetch next entry's values
            auto next_it = it;
            ++next_it;
            if (next_it != end) {
                if (key_is_heap && next_it->first.storage == ValueStorage::heap) {
                    rollnw_prefetch(heap_->get_ptr(next_it->first.data.hptr), 1, 1);
                }
                if (val_is_heap && next_it->second.storage == ValueStorage::heap) {
                    rollnw_prefetch(heap_->get_ptr(next_it->second.data.hptr), 1, 1);
                }
            }

            if (key_is_heap && key.storage == ValueStorage::heap) {
                try_mark_child(key.data.hptr);
            }
            if (val_is_heap && val.storage == ValueStorage::heap) {
                try_mark_child(val.data.hptr);
            }
            ++it;
        }
        break;
    }

    case TK_function: {
        Closure* closure = static_cast<Closure*>(heap_->get_ptr(ptr));
        if (!closure) { return false; }

        for (size_t i = 0; i < closure->upvalues.size(); ++i) {
            Upvalue* uv = closure->upvalues[i];
            if (!uv) continue;

            // Prefetch next upvalue's heap data
            if (i + 1 < closure->upvalues.size()) {
                Upvalue* next_uv = closure->upvalues[i + 1];
                if (next_uv && next_uv->heap_ptr.value != 0) {
                    rollnw_prefetch(heap_->get_ptr(next_uv->heap_ptr), 1, 2);
                }
            }

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
    if (ptr.value == 0) [[unlikely]] { return; }

    auto* header = heap_->try_get_header(ptr);
    if (!header) [[unlikely]] { return; }

    // Only shade if white (not yet marked)
    if (header->mark_color == static_cast<uint8_t>(MarkColor::WHITE)) {
        header->mark_color = static_cast<uint8_t>(MarkColor::GRAY);
        gray_stack_.push_back(ptr);
    }
}

void GarbageCollector::set_black(HeapPtr ptr)
{
    if (ptr.value == 0) [[unlikely]] { return; }

    auto* header = heap_->try_get_header(ptr);
    if (!header) [[unlikely]] { return; }

    header->mark_color = static_cast<uint8_t>(MarkColor::BLACK);
}

} // namespace nw::smalls
