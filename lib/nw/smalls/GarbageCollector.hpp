#pragma once

#include "ScriptHeap.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

namespace nw::smalls {

struct Runtime;
struct VirtualMachine;

class CardTable {
public:
    static constexpr size_t card_size = 512;
    static constexpr size_t max_heap_size = GB(2);
    static constexpr size_t table_size = max_heap_size / card_size;

    CardTable()
    {
        cards_ = new uint8_t[table_size]();
    }

    ~CardTable()
    {
        delete[] cards_;
    }

    CardTable(const CardTable&) = delete;
    CardTable& operator=(const CardTable&) = delete;

    void mark_dirty(uint32_t offset)
    {
        size_t card_index = offset / card_size;
        if (card_index < table_size) {
            cards_[card_index] = 1;
        }
    }

    bool is_dirty(uint32_t offset) const
    {
        size_t card_index = offset / card_size;
        return card_index < table_size && cards_[card_index] != 0;
    }

    bool is_range_dirty(uint32_t start, uint32_t end) const
    {
        size_t start_card = start / card_size;
        size_t end_card = (end > 0 ? (end - 1) / card_size : 0);
        if (start_card >= table_size) { return false; }
        if (end_card >= table_size) { end_card = table_size - 1; }
        for (size_t i = start_card; i <= end_card; ++i) {
            if (cards_[i] != 0) { return true; }
        }
        return false;
    }

    void clear_range(uint32_t start, uint32_t end)
    {
        size_t start_card = start / card_size;
        size_t end_card = (end > 0 ? (end - 1) / card_size : 0);
        if (start_card >= table_size) { return; }
        if (end_card >= table_size) { end_card = table_size - 1; }
        for (size_t i = start_card; i <= end_card; ++i) {
            cards_[i] = 0;
        }
    }

    void clear_card(uint32_t offset)
    {
        size_t card_index = offset / card_size;
        if (card_index < table_size) {
            cards_[card_index] = 0;
        }
    }

    void clear_all()
    {
        std::memset(cards_, 0, table_size);
    }

    template <typename Func>
    void for_each_dirty(Func&& func)
    {
        for (size_t i = 0; i < table_size; ++i) {
            if (cards_[i] != 0) {
                uint32_t start_offset = static_cast<uint32_t>(i * card_size);
                func(start_offset, static_cast<uint32_t>(card_size));
            }
        }
    }

    size_t dirty_count() const
    {
        size_t count = 0;
        for (size_t i = 0; i < table_size; ++i) {
            if (cards_[i] != 0) {
                ++count;
            }
        }
        return count;
    }

private:
    uint8_t* cards_ = nullptr;
};

struct GCConfig {
    uint8_t promotion_threshold = 2;
    float major_threshold_percent = 0.8f;
    size_t incremental_work_budget = 100;
};

struct GCStats {
    uint64_t minor_collections = 0;
    uint64_t major_collections = 0;
    uint64_t objects_freed = 0;
    uint64_t bytes_freed = 0;
    uint64_t total_pause_time_us = 0;
    uint64_t max_pause_time_us = 0;
};

enum class GCPhase : uint8_t {
    idle,
    mark_roots,
    mark_incremental,
    sweep
};

struct GarbageCollector {
    explicit GarbageCollector(ScriptHeap* heap, Runtime* runtime);
    ~GarbageCollector();

    GarbageCollector(const GarbageCollector&) = delete;
    GarbageCollector& operator=(const GarbageCollector&) = delete;

    void collect_minor();
    void collect_major();
    void collect_full();
    bool mark_step(size_t work_budget);
    void on_allocation(size_t size);
    void write_barrier(HeapPtr target, HeapPtr stored_value);
    void write_barrier_marking(HeapPtr target, HeapPtr stored_value);
    // Write barrier for root slots (module globals, config roots) that are not
    // themselves inside ScriptHeap. Shades stored_value gray during incremental mark.
    void write_barrier_root(HeapPtr stored_value);

    void start_major_gc();
    void finish_major_gc();
    bool is_marking() const noexcept { return phase_ == GCPhase::mark_incremental; }

    GCPhase phase() const noexcept { return phase_; }
    const GCStats& stats() const noexcept { return stats_; }
    const GCConfig& config() const noexcept { return config_; }
    void set_config(const GCConfig& config) { config_ = config; }

    CardTable& card_table() { return card_table_; }

    bool is_young(HeapPtr ptr) const;
    bool is_old(HeapPtr ptr) const;

    void register_object(HeapPtr ptr, size_t size);
    void set_vm(VirtualMachine* vm) { vm_ = vm; }

private:
    void mark_roots(bool young_only);
    void mark_from_dirty_cards();
    void process_gray_stack(bool young_only);
    void promote_survivors();
    void sweep_young();
    void sweep_all();
    bool trace_object(HeapPtr ptr, bool young_only);
    void shade_gray(HeapPtr ptr);
    void set_black(HeapPtr ptr);

    ScriptHeap* heap_;
    Runtime* runtime_;
    VirtualMachine* vm_ = nullptr;

    GCConfig config_;
    GCStats stats_;
    GCPhase phase_ = GCPhase::idle;

    CardTable card_table_;
    std::vector<HeapPtr> gray_stack_;

    HeapPtr all_objects_{0};
    size_t young_bytes_ = 0;
    size_t old_bytes_ = 0;
};

inline void gc_write_barrier(
    GarbageCollector* gc,
    HeapPtr target,
    HeapPtr stored_value)
{
    if (!gc || target.value == 0 || stored_value.value == 0) {
        return;
    }
    gc->write_barrier(target, stored_value);
}

} // namespace nw::smalls
