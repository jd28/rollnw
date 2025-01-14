#pragma once

#include "../kernel/Memory.hpp"
#include "../log.hpp"

#include "xxhash/xxh3.h"

#include <cassert>
#include <cstdint>
#include <cstring>

namespace nw {

template <typename T>
class HndlPtrMap {
private:
    struct Entry {
        uint64_t key;
        T* value;
    };

    Entry* entries_ = nullptr;
    size_t capacity_ = 0;
    size_t size_ = 0;
    size_t first_slot_collisions_ = 0; // Track collisions in first attempted slot
    MemoryResource* allocator_ = nullptr;

    static constexpr size_t MAX_LOAD_NUM = 7;
    static constexpr size_t MAX_LOAD_DEN = 10;
    static constexpr uint64_t EMPTY_KEY = UINT64_MAX;

    static size_t next_power_2(size_t n)
    {
        n--;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        n |= n >> 32;
        return n + 1;
    }

    static size_t required_capacity(size_t n)
    {
        return next_power_2((n * MAX_LOAD_DEN + MAX_LOAD_NUM - 1) / MAX_LOAD_NUM);
    }

public:
    explicit HndlPtrMap(size_t initial_capacity = 16, MemoryResource* scope = nw::kernel::global_allocator())
    {
        allocator_ = scope;
        resize(next_power_2(initial_capacity));
    }

    ~HndlPtrMap()
    {
        allocator_->deallocate(entries_, capacity_ * sizeof(Entry));
    }

    void reserve(size_t n)
    {
        size_t required = required_capacity(n);
        if (required > capacity_) { resize(required); }
    }

    bool insert(uint64_t key, T* value)
    {
        CHECK_F(key != EMPTY_KEY, "attempting to use empty key");

        if (size_ * MAX_LOAD_DEN >= capacity_ * MAX_LOAD_NUM) {
            resize(capacity_ * 2);
        }

        uint64_t hash = XXH3_64bits(&key, sizeof(key));
        size_t idx = hash & (capacity_ - 1);

        if (entries_[idx].key == EMPTY_KEY || entries_[idx].key == key) {
            if (entries_[idx].key == EMPTY_KEY) {
                size_++;
            }
            entries_[idx].key = key;
            entries_[idx].value = value;
            return true;
        }

        first_slot_collisions_++;

        size_t i = 0; // Quadratic probing index
        while (true) {
            size_t probe_idx = (idx + i + i * i) & (capacity_ - 1); // Quadratic probing
            if (entries_[probe_idx].key == EMPTY_KEY || entries_[probe_idx].key == key) {
                if (entries_[probe_idx].key == EMPTY_KEY) {
                    size_++;
                }
                entries_[probe_idx].key = key;
                entries_[probe_idx].value = value;
                return true;
            }
            i++;
            if (i >= capacity_) {
                CHECK_F(false, "Quadratic probing failed: no available slots");
            }
        }
        return false;
    }

    T* get(uint64_t key) const
    {
        CHECK_F(key != EMPTY_KEY, "attempting to use empty key");

        uint64_t hash = XXH3_64bits(&key, sizeof(key));
        size_t idx = hash & (capacity_ - 1);

        if (entries_[idx].key == key) {
            return entries_[idx].value;
        }

        size_t i = 0;
        while (true) {
            size_t probe_idx = (idx + i + i * i) & (capacity_ - 1);
            if (entries_[probe_idx].key == key) {
                return entries_[probe_idx].value;
            }
            if (entries_[probe_idx].key == EMPTY_KEY) {
                return nullptr;
            }
            i++;
            if (i >= capacity_) {
                return nullptr;
            }
        }
        return nullptr;
    }

    bool remove(uint64_t key) noexcept
    {
        CHECK_F(key != EMPTY_KEY, "attempting to use empty key");

        uint64_t hash = XXH3_64bits(&key, sizeof(key));
        size_t idx = hash & (capacity_ - 1);

        if (entries_[idx].key == key) {
            entries_[idx].key = EMPTY_KEY;
            entries_[idx].value = nullptr;
            size_--;
            return true;
        }

        size_t i = 0;
        while (true) {
            size_t probe_idx = (idx + i + i * i) & (capacity_ - 1);
            if (entries_[probe_idx].key == key) {
                entries_[probe_idx].key = EMPTY_KEY;
                entries_[probe_idx].value = nullptr;
                size_--;
                return true;
            }
            if (entries_[probe_idx].key == EMPTY_KEY) {
                return false;
            }
            i++;
            if (i >= capacity_) {
                return false;
            }
        }

        return false;
    }

    /// Get current number of elements
    size_t size() const noexcept { return size_; }

    /// Get current capacity
    size_t capacity() const noexcept { return capacity_; }

    /// Get number of first slot collisions
    size_t get_first_slot_collisions() const noexcept { return first_slot_collisions_; }

private:
    void resize(size_t new_capacity)
    {
        Entry* old_entries = entries_;
        size_t old_capacity = capacity_;

        entries_ = static_cast<Entry*>(allocator_->allocate(new_capacity * sizeof(Entry), alignof(Entry)));
        capacity_ = new_capacity;
        size_ = 0;
        first_slot_collisions_ = 0;

        for (size_t i = 0; i < capacity_; i++) {
            entries_[i].key = EMPTY_KEY;
            entries_[i].value = nullptr;
        }

        for (size_t i = 0; i < old_capacity; i++) {
            if (old_entries[i].key != EMPTY_KEY) {
                insert(old_entries[i].key, old_entries[i].value);
            }
        }

        allocator_->deallocate(old_entries, old_capacity * sizeof(Entry));
    }
};

}
