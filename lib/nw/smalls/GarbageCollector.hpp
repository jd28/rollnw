#pragma once

#include "ScriptHeap.hpp"

#include <absl/container/flat_hash_set.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <vector>

#include <xsimd/xsimd.hpp>

// Portable bit manipulation intrinsics
#if defined(_MSC_VER)
#include <intrin.h>

inline int rollnw_ctzll(uint64_t x)
{
    unsigned long index;
    _BitScanForward64(&index, x);
    return static_cast<int>(index);
}

inline int rollnw_popcountll(uint64_t x)
{
    return static_cast<int>(__popcnt64(x));
}

inline void rollnw_prefetch(const void* ptr, int rw, int locality)
{
    // rw: 0=read, 1=write
    // locality: 0=no locality, 3=high locality
    _mm_prefetch(reinterpret_cast<const char*>(ptr),
        locality >= 3 ? _MM_HINT_T0 : locality >= 2 ? _MM_HINT_T1
            : locality >= 1                         ? _MM_HINT_T2
                                                    : _MM_HINT_NTA);
}

#else // GCC/Clang

inline int rollnw_ctzll(uint64_t x)
{
    return __builtin_ctzll(x);
}

inline int rollnw_popcountll(uint64_t x)
{
    return __builtin_popcountll(x);
}

inline void rollnw_prefetch(const void* ptr, int rw, int locality)
{
    // GCC/Clang require rw/locality to be compile-time constants.
    const int loc = locality < 0 ? 0 : (locality > 3 ? 3 : locality);
    if (rw != 0) {
        switch (loc) {
        case 0:
            __builtin_prefetch(ptr, 1, 0);
            break;
        case 1:
            __builtin_prefetch(ptr, 1, 1);
            break;
        case 2:
            __builtin_prefetch(ptr, 1, 2);
            break;
        default:
            __builtin_prefetch(ptr, 1, 3);
            break;
        }
    } else {
        switch (loc) {
        case 0:
            __builtin_prefetch(ptr, 0, 0);
            break;
        case 1:
            __builtin_prefetch(ptr, 0, 1);
            break;
        case 2:
            __builtin_prefetch(ptr, 0, 2);
            break;
        default:
            __builtin_prefetch(ptr, 0, 3);
            break;
        }
    }
}

#endif

namespace nw::smalls {

struct Runtime;
struct VirtualMachine;

class CardTable {
public:
    static constexpr size_t card_size = 512;
    static constexpr size_t max_heap_size = GB(2);
    static constexpr size_t num_cards = max_heap_size / card_size;
    static constexpr size_t bits_per_word = 64;
    static constexpr size_t num_words = (num_cards + bits_per_word - 1) / bits_per_word;

    CardTable()
    {
        cards_ = new uint64_t[num_words]();
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
        if (card_index < num_cards) {
            size_t word_idx = card_index / bits_per_word;
            size_t bit_idx = card_index % bits_per_word;
            cards_[word_idx] |= (1ULL << bit_idx);
        }
    }

    bool is_dirty(uint32_t offset) const
    {
        size_t card_index = offset / card_size;
        if (card_index >= num_cards) { return false; }
        size_t word_idx = card_index / bits_per_word;
        size_t bit_idx = card_index % bits_per_word;
        return (cards_[word_idx] >> bit_idx) & 1;
    }

    bool is_range_dirty(uint32_t start, uint32_t end) const
    {
        size_t start_card = start / card_size;
        size_t end_card = (end > 0 ? (end - 1) / card_size : 0);
        if (start_card >= num_cards) { return false; }
        if (end_card >= num_cards) { end_card = num_cards - 1; }

        size_t start_word = start_card / bits_per_word;
        size_t end_word = end_card / bits_per_word;
        size_t start_bit = start_card % bits_per_word;
        size_t end_bit = end_card % bits_per_word;

        if (start_word == end_word) {
            // Range within single word
            uint64_t mask = ((1ULL << (end_bit - start_bit + 1)) - 1) << start_bit;
            return (cards_[start_word] & mask) != 0;
        }

        // Check partial first word
        if (start_bit != 0) {
            uint64_t mask = ~((1ULL << start_bit) - 1);
            if (cards_[start_word] & mask) { return true; }
            ++start_word;
        }

        // Check full words in middle
        for (size_t word = start_word; word < end_word; ++word) {
            if (cards_[word] != 0) { return true; }
        }

        // Check partial last word (or full last word when end_bit == 63)
        if (end_bit != bits_per_word - 1) {
            uint64_t mask = (1ULL << (end_bit + 1)) - 1;
            return (cards_[end_word] & mask) != 0;
        } else {
            return cards_[end_word] != 0;
        }
    }

    void clear_range(uint32_t start, uint32_t end)
    {
        size_t start_card = start / card_size;
        size_t end_card = (end > 0 ? (end - 1) / card_size : 0);
        if (start_card >= num_cards) { return; }
        if (end_card >= num_cards) { end_card = num_cards - 1; }

        size_t start_word = start_card / bits_per_word;
        size_t end_word = end_card / bits_per_word;
        size_t start_bit = start_card % bits_per_word;
        size_t end_bit = end_card % bits_per_word;

        if (start_word == end_word) {
            // Range within single word
            uint64_t mask = ((1ULL << (end_bit - start_bit + 1)) - 1) << start_bit;
            cards_[start_word] &= ~mask;
            return;
        }

        // Clear partial first word
        if (start_bit != 0) {
            uint64_t mask = (1ULL << start_bit) - 1;
            cards_[start_word] &= mask;
            ++start_word;
        }

        // Clear full words in middle
        for (size_t word = start_word; word < end_word; ++word) {
            cards_[word] = 0;
        }

        // Clear partial last word
        if (end_bit != bits_per_word - 1) {
            uint64_t mask = ~((1ULL << (end_bit + 1)) - 1);
            cards_[end_word] &= mask;
        } else {
            cards_[end_word] = 0;
        }
    }

    void clear_card(uint32_t offset)
    {
        size_t card_index = offset / card_size;
        if (card_index < num_cards) {
            size_t word_idx = card_index / bits_per_word;
            size_t bit_idx = card_index % bits_per_word;
            cards_[word_idx] &= ~(1ULL << bit_idx);
        }
    }

    void clear_all()
    {
        std::memset(cards_, 0, num_words * sizeof(uint64_t));
    }

    template <typename Func>
    void for_each_dirty(Func&& func)
    {
        for (size_t word_idx = 0; word_idx < num_words; ++word_idx) {
            uint64_t word = cards_[word_idx];
            if (word == 0) { continue; }

            // Iterate set bits using portable ctzll
            while (word != 0) {
                size_t bit_idx = static_cast<size_t>(rollnw_ctzll(word));
                size_t card_index = word_idx * bits_per_word + bit_idx;
                if (card_index < num_cards) {
                    uint32_t start_offset = static_cast<uint32_t>(card_index * card_size);
                    func(start_offset, static_cast<uint32_t>(card_size));
                }
                word &= (word - 1); // Clear lowest set bit
            }
        }
    }

    size_t dirty_count() const
    {
        size_t count = 0;
        for (size_t word_idx = 0; word_idx < num_words; ++word_idx) {
            count += static_cast<size_t>(rollnw_popcountll(cards_[word_idx]));
        }
        return count;
    }

private:
    uint64_t* cards_ = nullptr;
};

struct GCConfig {
    uint8_t promotion_threshold = 2;
    float major_threshold_percent = 0.8f;
    size_t incremental_work_budget = 100;
    uint64_t minor_step_time_budget_us = 0;
};

struct GCStats {
    uint64_t minor_collections = 0;
    uint64_t major_collections = 0;
    uint64_t objects_freed = 0;
    uint64_t bytes_freed = 0;
    uint64_t total_pause_time_us = 0;
    uint64_t max_pause_time_us = 0;
    uint64_t minor_roots_time_us = 0;
    uint64_t minor_roots_frame_slots_us = 0;
    uint64_t minor_roots_registers_us = 0;
    uint64_t minor_roots_runtime_stack_us = 0;
    uint64_t minor_roots_module_globals_us = 0;
    uint64_t minor_roots_handle_roots_us = 0;
    uint64_t minor_remembered_scan_time_us = 0;
    uint64_t minor_trace_time_us = 0;
    uint64_t minor_sweep_promote_time_us = 0;

    uint64_t minor_stack_slots_total = 0;
    uint64_t minor_stack_slots_scanned = 0;
    uint64_t minor_stack_slots_skipped = 0;
    uint64_t minor_register_values_total = 0;
    uint64_t minor_register_values_heap = 0;
    uint64_t minor_runtime_stack_values_total = 0;
    uint64_t minor_runtime_stack_values_heap = 0;
    uint64_t minor_module_global_roots_visited = 0;
    uint64_t minor_handle_roots_visited = 0;
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
    bool collect_minor_step(size_t work_budget);
    void collect_major();
    void collect_full();
    bool mark_step(size_t work_budget);
    void on_allocation(size_t size);
#if defined(__clang__) || defined(__GNUC__)
    __attribute__((always_inline))
#endif
    inline void write_barrier(HeapPtr target, HeapPtr stored_value)
    {
        // Fast path: skip if either pointer is null
        if (target.value == 0 || stored_value.value == 0) [[unlikely]] {
            return;
        }

        // Quick phase check: if no GC active and both objects are young,
        // we only need to reset age - defer to slow path
        const bool major_marking = (phase_ == GCPhase::mark_incremental);
        const bool minor_marking = (minor_phase_ != MinorPhase::idle);

        if (!major_marking && !minor_marking) [[likely]] {
            // Only need to check target generation and reset age
            auto* target_header = heap_->try_get_header(target);
            if (!target_header) [[unlikely]] { return; }

            if (target_header->generation == 0) {
                target_header->age = 0;
            } else {
                // Old generation target with young value - need remembered set
                auto* value_header = heap_->try_get_header(stored_value);
                if (!value_header) [[unlikely]] { return; }
                if (value_header->generation == 0) {
                    card_table_.mark_dirty(target.value);
                    enqueue_remembered_object(target);
                }
            }
            return;
        }

        // Slow path: full barrier with marking
        auto* target_header = heap_->try_get_header(target);
        auto* value_header = heap_->try_get_header(stored_value);
        if (!target_header || !value_header) [[unlikely]] {
            return;
        }

        if (target_header->generation == 0) {
            target_header->age = 0;
        }

        if (target_header->generation == 1 && value_header->generation == 0) {
            card_table_.mark_dirty(target.value);
            enqueue_remembered_object(target);
        }

        if (minor_marking
            && value_header->generation == 0
            && value_header->mark_color == static_cast<uint8_t>(MarkColor::WHITE)) {
            shade_gray(stored_value);
        }

        if (major_marking) {
            write_barrier_marking(target, stored_value);
        }
    }
    void write_barrier_marking(HeapPtr target, HeapPtr stored_value);
    // Write barrier for root slots (module globals, config roots) that are not
    // themselves inside ScriptHeap. Shades stored_value gray during incremental mark.
    void write_barrier_root(HeapPtr stored_value);

    void start_major_gc();
    void finish_major_gc();
    bool is_marking() const noexcept { return phase_ == GCPhase::mark_incremental; }
    bool is_minor_collecting() const noexcept { return minor_phase_ != MinorPhase::idle; }

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
    enum class MinorPhase : uint8_t {
        idle,
        mark_roots,
        scan_remembered,
        trace_gray,
        sweep_promote,
    };

    void mark_roots(bool young_only);
    bool mark_from_remembered_step(size_t work_budget, const std::chrono::high_resolution_clock::time_point* deadline = nullptr);
    void process_gray_stack(bool young_only);
    bool process_gray_stack_step(bool young_only, size_t work_budget, const std::chrono::high_resolution_clock::time_point* deadline = nullptr);
    bool sweep_and_promote_young_step(size_t work_budget, const std::chrono::high_resolution_clock::time_point* deadline = nullptr);
    void begin_minor_cycle();
    void finish_minor_cycle();
    void enqueue_remembered_object(HeapPtr ptr);
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
    std::vector<HeapPtr> remembered_objects_;
    std::vector<HeapPtr> remembered_retained_;
    absl::flat_hash_set<uint32_t> remembered_set_;
    size_t remembered_scan_cursor_ = 0;
    MinorPhase minor_phase_ = MinorPhase::idle;
    HeapPtr young_sweep_current_{0};
    HeapPtr young_sweep_prev_{0};
    HeapPtr young_sweep_all_prev_{0}; // Previous object in all_objects list during sweep
    std::chrono::high_resolution_clock::time_point minor_start_time_;

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
