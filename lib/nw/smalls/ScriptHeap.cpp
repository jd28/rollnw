#include "ScriptHeap.hpp"

#include "GarbageCollector.hpp"

#include "../log.hpp"
#include "../util/platform.hpp"

#include <new>

#ifdef ROLLNW_OS_WINDOWS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace nw::smalls {

ScriptHeap::ScriptHeap()
{
#ifdef ROLLNW_OS_WINDOWS
    base_address_ = static_cast<uint8_t*>(
        VirtualAlloc(nullptr, RESERVE_SIZE, MEM_RESERVE, PAGE_NOACCESS));
    CHECK_F(base_address_ != nullptr, "Failed to reserve virtual memory");
#else
    base_address_ = static_cast<uint8_t*>(
        mmap(nullptr, RESERVE_SIZE, PROT_NONE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0));
    CHECK_F(base_address_ != MAP_FAILED, "Failed to reserve virtual memory");
#endif

#ifndef ROLLNW_OS_WINDOWS
    long ps = sysconf(_SC_PAGESIZE);
    if (ps > 0) {
        page_size_ = static_cast<size_t>(ps);
    }
#endif

    allocator_ = new OffsetAllocator::Allocator(
        static_cast<uint32_t>(RESERVE_SIZE),
        static_cast<uint32_t>(MAX_ALLOCS));
}

ScriptHeap::~ScriptHeap()
{
    delete allocator_;

#ifdef ROLLNW_OS_WINDOWS
    if (base_address_) {
        VirtualFree(base_address_, 0, MEM_RELEASE);
    }
#else
    if (base_address_ && base_address_ != MAP_FAILED) {
        munmap(base_address_, RESERVE_SIZE);
    }
#endif
}

HeapPtr ScriptHeap::allocate(size_t size, size_t alignment, TypeID type_id)
{
    // Ensure alignment is at least for pointer size (for back-pointer)
    alignment = std::max(alignment, alignof(void*));

    // Allocate space for: header + back-pointer + user data + padding
    size_t total_size = sizeof(ObjectHeader) + sizeof(void*) + size + (alignment - 1);
    uint32_t alloc_size = static_cast<uint32_t>(total_size);

    OffsetAllocator::Allocation alloc = allocator_->allocate(alloc_size);
    CHECK_F(alloc.offset != OffsetAllocator::Allocation::NO_SPACE,
        "ScriptHeap allocation failed - out of space");

    size_t end_offset = alloc.offset + alloc_size;
    if (end_offset > committed_size_) {
        commit_pages(committed_size_, end_offset - committed_size_);
    }

    // Place header at start of allocation
    uintptr_t alloc_start = reinterpret_cast<uintptr_t>(base_address_) + alloc.offset;
    ObjectHeader* header = new (reinterpret_cast<void*>(alloc_start)) ObjectHeader();
    header->alloc = alloc;
    header->type_id = type_id;
    header->mark_color = 0;
    header->generation = 0;
    header->age = 0;
    header->alloc_size = alloc_size;

    // Calculate aligned user pointer (after header + back-pointer space)
    uintptr_t after_header = alloc_start + sizeof(ObjectHeader) + sizeof(void*);
    uintptr_t aligned_ptr = (after_header + alignment - 1) & ~(alignment - 1);

    // Store back-pointer to header just before user data
    void** back_ptr = reinterpret_cast<void**>(aligned_ptr - sizeof(void*));
    *back_ptr = header;

    HeapPtr ptr = to_heap_ptr(reinterpret_cast<void*>(aligned_ptr));

    header->next_object = all_objects_;
    all_objects_ = ptr;
    young_bytes_ += alloc_size;

    if (gc_) {
        gc_->on_allocation(alloc_size);
    }

    return ptr;
}

void ScriptHeap::free(HeapPtr ptr)
{
    if (ptr.value == 0) return;

    ObjectHeader* header = get_header(ptr);
    allocator_->free(header->alloc);
}

void ScriptHeap::commit_pages(size_t offset, size_t size)
{
    size_t aligned_size = size;
    size_t aligned_offset = offset;
#ifndef ROLLNW_OS_WINDOWS
    const size_t page = page_size_;
    aligned_offset = offset & ~(page - 1);
    size_t end = offset + size;
    size_t aligned_end = (end + page - 1) & ~(page - 1);
    aligned_size = aligned_end - aligned_offset;
#endif

#ifdef ROLLNW_OS_WINDOWS
    void* result = VirtualAlloc(
        base_address_ + offset,
        aligned_size,
        MEM_COMMIT,
        PAGE_READWRITE);
    CHECK_F(result != nullptr, "Failed to commit pages");
#else
    int result = mprotect(
        base_address_ + aligned_offset,
        aligned_size,
        PROT_READ | PROT_WRITE);
    CHECK_F(result == 0, "Failed to commit pages");
#endif

#ifndef ROLLNW_OS_WINDOWS
    size_t commit_end = aligned_offset + aligned_size;
    if (commit_end > committed_size_) {
        committed_size_ = commit_end;
    }
#else
    committed_size_ += aligned_size;
#endif
}

} // namespace nw::smalls
