#pragma once

#if __STDC_HOSTED__
#include <stddef.h>
#endif
#include <stdint.h>
#include <string.h>

namespace xapi_heap {

struct Statistics
{
    size_t committed_bytes, live_bytes, peak_live_bytes;
    size_t live_allocations, peak_allocations, search_steps;
};

// A process-local heap. The allocator owns every block returned by grow();
// that callback and wait() must not allocate or call back into this heap.
class Heap
{
    struct alignas(16) Block
    {
        size_t capacity;
        Block *prev, *next;
        size_t requested;
        uint64_t magic;
        bool available;
    };

    struct Links
    {
        Block *prev, *next;
    };

    typedef void *(*Grow)(size_t);
    typedef void (*Wait)();
    static constexpr size_t alignment = 16, minimum_chunk = 65536, page_size = 4096;
    static constexpr uint64_t magic = 0x584a42494e484541ULL;
    static constexpr unsigned bin_count = 64;
    Grow grow_;
    Wait wait_;
    Block *head_, *tail_, *bins_[bin_count];
    unsigned char *begin_, *end_;
    uint64_t nonempty_;
    int mutex_;
    Statistics stats_;

    class Guard
    {
        Heap &heap_;

      public:
        explicit Guard(Heap &heap) : heap_(heap) { heap_.lock(); }
        ~Guard() { __atomic_store_n(&heap_.mutex_, 0, __ATOMIC_RELEASE); }
    };

    void lock()
    {
        // This is a userspace mutex: waiters yield without allocating, and
        // scheduling stays enabled while the owner serializes brk growth.
        while (__atomic_exchange_n(&mutex_, 1, __ATOMIC_ACQUIRE))
            while (__atomic_load_n(&mutex_, __ATOMIC_RELAXED))
                wait_();
    }

    static bool aligned_size(size_t size, size_t *out)
    {
        if (!size) size = 1;
        if (size > (size_t)-1 - (alignment - 1)) return false;
        *out = (size + alignment - 1) & ~(alignment - 1);
        return true;
    }

    static unsigned bin(size_t capacity)
    {
        // Exact 16-byte classes through 512 bytes cover common C++ nodes and
        // strings. Larger free blocks use logarithmic classes; only free
        // blocks in a candidate class are searched, never live allocations.
        if (capacity <= 512) return unsigned(capacity / alignment - 1);
        unsigned result = 32;
        for (size_t value = capacity >> 10; value && result < bin_count - 1; value >>= 1)
            ++result;
        return result;
    }

    static Links &links(Block *block) { return *reinterpret_cast<Links *>(block + 1); }

    void insert(Block *block)
    {
        const unsigned index = bin(block->capacity);
        links(block).prev = NULL;
        links(block).next = bins_[index];
        if (bins_[index]) links(bins_[index]).prev = block;
        bins_[index] = block;
        nonempty_ |= uint64_t(1) << index;
    }

    void remove(Block *block)
    {
        const unsigned index = bin(block->capacity);
        const Links old = links(block);
        if (old.prev) links(old.prev).next = old.next;
        else bins_[index] = old.next;
        if (old.next) links(old.next).prev = old.prev;
        if (!bins_[index]) nonempty_ &= ~(uint64_t(1) << index);
    }

    static bool adjacent(Block *left, Block *right)
    {
        return left && right && reinterpret_cast<unsigned char *>(left + 1) + left->capacity ==
                                   reinterpret_cast<unsigned char *>(right);
    }

    Block *merge_next(Block *block)
    {
        Block *next = block->next;
        block->capacity += sizeof(Block) + next->capacity;
        block->next = next->next;
        if (block->next) block->next->prev = block;
        else tail_ = block;
        next->magic = 0;
        return block;
    }

    void split(Block *block, size_t size)
    {
        if (block->capacity - size < sizeof(Block) + alignment) return;
        Block *rest = reinterpret_cast<Block *>(reinterpret_cast<unsigned char *>(block + 1) + size);
        rest->capacity = block->capacity - size - sizeof(Block);
        rest->prev = block;
        rest->next = block->next;
        rest->requested = 0;
        rest->magic = magic;
        rest->available = true;
        if (rest->next) rest->next->prev = rest;
        else tail_ = rest;
        block->next = rest;
        block->capacity = size;
        if (rest->next && rest->next->available && adjacent(rest, rest->next))
        {
            remove(rest->next);
            merge_next(rest);
        }
        insert(rest);
    }

    Block *find(size_t size)
    {
        uint64_t candidates = nonempty_ & (~uint64_t(0) << bin(size));
        while (candidates)
        {
            const unsigned index = unsigned(__builtin_ctzll(candidates));
            for (Block *block = bins_[index]; block; block = links(block).next)
            {
                ++stats_.search_steps;
                if (block->capacity >= size)
                {
                    remove(block);
                    return block;
                }
            }
            candidates &= candidates - 1;
        }
        return NULL;
    }

    Block *grow(size_t size)
    {
        if (size > (size_t)-1 - sizeof(Block) - (alignment - 1) - (page_size - 1)) return NULL;
        size_t bytes = (size + sizeof(Block) + alignment - 1 + page_size - 1) & ~(page_size - 1);
        if (bytes < minimum_chunk) bytes = minimum_chunk;
        if (bytes > (size_t)-1 - stats_.committed_bytes) return NULL;
        void *memory = grow_(bytes);
        if (!memory || memory == reinterpret_cast<void *>((size_t)-1)) return NULL;
        const uintptr_t address = reinterpret_cast<uintptr_t>(memory);
        const size_t padding = (alignment - address % alignment) % alignment;
        auto *block = reinterpret_cast<Block *>(address + padding);
        block->capacity = (bytes - padding - sizeof(Block)) & ~(alignment - 1);
        block->prev = tail_;
        block->next = NULL;
        block->requested = 0;
        block->magic = magic;
        block->available = true;
        if (!head_) head_ = block;
        if (tail_) tail_->next = block;
        tail_ = block;
        auto *first = reinterpret_cast<unsigned char *>(block);
        auto *end = reinterpret_cast<unsigned char *>(block + 1) + block->capacity;
        if (!begin_ || reinterpret_cast<uintptr_t>(first) < reinterpret_cast<uintptr_t>(begin_)) begin_ = first;
        if (!end_ || reinterpret_cast<uintptr_t>(end) > reinterpret_cast<uintptr_t>(end_)) end_ = end;
        stats_.committed_bytes += bytes;
        if (block->prev && block->prev->available && adjacent(block->prev, block))
        {
            block = block->prev;
            remove(block);
            merge_next(block);
        }
        return block;
    }

    Block *checked(void *pointer)
    {
        const uintptr_t value = reinterpret_cast<uintptr_t>(pointer);
        if (!begin_ || value % alignment || value < reinterpret_cast<uintptr_t>(begin_) + sizeof(Block) ||
            value >= reinterpret_cast<uintptr_t>(end_)) return NULL;
        auto *block = static_cast<Block *>(pointer) - 1;
        return block->magic == magic && !block->available ? block : NULL;
    }

    void account(size_t old_size, size_t new_size)
    {
        stats_.live_bytes = stats_.live_bytes - old_size + new_size;
        if (stats_.live_bytes > stats_.peak_live_bytes) stats_.peak_live_bytes = stats_.live_bytes;
    }

  public:
    constexpr Heap(Grow grow, Wait wait)
        : grow_(grow), wait_(wait), head_(NULL), tail_(NULL), bins_{}, begin_(NULL), end_(NULL), nonempty_(0),
          mutex_(0), stats_{}
    {
    }

    void *allocate(size_t bytes)
    {
        size_t size;
        if (!aligned_size(bytes, &size)) return NULL;
        Guard guard(*this);
        Block *block = find(size);
        if (!block) block = grow(size);
        if (!block) return NULL;
        split(block, size);
        block->available = false;
        block->requested = bytes ? bytes : 1;
        account(0, block->requested);
        ++stats_.live_allocations;
        if (stats_.live_allocations > stats_.peak_allocations) stats_.peak_allocations = stats_.live_allocations;
        return block + 1;
    }

    void deallocate(void *pointer)
    {
        if (!pointer) return;
        Guard guard(*this);
        Block *block = checked(pointer);
        if (!block) return;
        account(block->requested, 0);
        --stats_.live_allocations;
        block->available = true;
        block->requested = 0;
        if (block->next && block->next->available && adjacent(block, block->next))
        {
            remove(block->next);
            merge_next(block);
        }
        if (block->prev && block->prev->available && adjacent(block->prev, block))
        {
            block = block->prev;
            remove(block);
            merge_next(block);
        }
        insert(block);
    }

    void *reallocate(void *pointer, size_t bytes)
    {
        if (!pointer) return allocate(bytes);
        if (!bytes) { deallocate(pointer); return NULL; }
        size_t size, old_bytes;
        if (!aligned_size(bytes, &size)) return NULL;
        {
            Guard guard(*this);
            Block *block = checked(pointer);
            if (!block) return NULL;
            old_bytes = block->requested;
            if (block->capacity < size && block->next && block->next->available && adjacent(block, block->next) &&
                size - block->capacity <= sizeof(Block) + block->next->capacity)
            {
                remove(block->next);
                merge_next(block);
            }
            if (block->capacity >= size)
            {
                split(block, size);
                account(old_bytes, bytes);
                block->requested = bytes;
                return pointer;
            }
        }
        // A caller owns its allocation until it publishes the result. Copying
        // outside the mutex avoids blocking unrelated allocations on big moves.
        void *replacement = allocate(bytes);
        if (!replacement) return NULL;
        memcpy(replacement, pointer, old_bytes < bytes ? old_bytes : bytes);
        deallocate(pointer);
        return replacement;
    }

    Statistics statistics()
    {
        Guard guard(*this);
        return stats_;
    }

    bool owns(void *pointer)
    {
        Guard guard(*this);
        return checked(pointer) != NULL;
    }
};

} // namespace xapi_heap

// Diagnostic local-runtime query, present only in the browser heap variant.
extern "C" int xapi_browser_heap_stats(xapi_heap::Statistics *out);
