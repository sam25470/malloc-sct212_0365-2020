#define _DEFAULT_SOURCE

#include "myalloc.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <inttypes.h>
#include <limits.h>

/*
 * A block consists of this header followed by its payload.
 * HDR is rounded up to 16 bytes, so every payload starts on a
 * 16-byte boundary when the header itself is correctly positioned.
 */
typedef struct block {
    size_t size;              /* Aligned payload capacity, in bytes. */
    struct block *next;       /* Next block in increasing address order. */
    int free;                 /* 1 = available, 0 = allocated. */
    uint32_t magic;           /* Helps detect damaged/foreign headers. */
} block_t;

#define ALIGNMENT ((size_t)16)
#define ALIGN_MASK (ALIGNMENT - 1)
#define HDR (((sizeof(block_t) + ALIGN_MASK) & ~ALIGN_MASK))
#define MIN_PAYLOAD ALIGNMENT
#define HEAP_CHUNK ((size_t)64 * 1024)
#define BLOCK_MAGIC UINT32_C(0xB10CA110)
#define DEAD_MAGIC  UINT32_C(0xDEADF00D)

static block_t *head = NULL;
static block_t *tail = NULL;
static size_t heap_bytes = 0;
static fit_policy_t placement_policy = FIT_FIRST;

/* Round a size up to a multiple of 16. Zero means overflow. */
static size_t align_up(size_t n)
{
    if (n > SIZE_MAX - ALIGN_MASK)
        return 0;
    return (n + ALIGN_MASK) & ~ALIGN_MASK;
}

/* Return the user-visible payload address for a block. */
static void *payload(block_t *block)
{
    return (void *)((char *)block + HDR);
}

/* Write an allocator diagnostic without using stdio output functions. */
static void allocator_message(const char *message)
{
    size_t length = 0;
    while (message[length] != '\0')
        ++length;
    (void)write(STDERR_FILENO, message, length);
}

/*
 * Obtain a new region from the process heap.
 *
 * We request at least 64 KiB, or enough for the requested payload and
 * its header. The returned sbrk address may not be aligned, so padding
 * is inserted before the block header. The payload is then 16-byte aligned.
 */
static block_t *grow(size_t need)
{
    void *old_break;
    uintptr_t raw_address;
    uintptr_t header_address;
    size_t padding;
    size_t wanted;
    size_t request;
    void *region;
    block_t *new_block;

    if (need > SIZE_MAX - HDR - ALIGN_MASK)
        return NULL;

    wanted = HDR + need + ALIGN_MASK;
    request = wanted > HEAP_CHUNK ? wanted : HEAP_CHUNK;

    /* Keep the payload capacity and subsequent block addresses aligned. */
    if (request > SIZE_MAX - ALIGN_MASK)
        return NULL;
    request = align_up(request);
    if (request == 0 || request > (size_t)INTPTR_MAX)
        return NULL;

    old_break = sbrk(0);
    if (old_break == (void *)-1)
        return NULL;

    raw_address = (uintptr_t)old_break;
    header_address = (raw_address + HDR + ALIGN_MASK) & ~(uintptr_t)ALIGN_MASK;
    header_address -= HDR;
    padding = (size_t)(header_address - raw_address);

    if (padding > SIZE_MAX - request)
        return NULL;
    if (padding + request > (size_t)INTPTR_MAX)
        return NULL;

    region = sbrk((intptr_t)(padding + request));
    if (region == (void *)-1)
        return NULL;

    /* sbrk returns the old break; use it rather than assuming continuity. */
    new_block = (block_t *)((char *)region + padding);
    new_block->size = request - HDR;
    new_block->next = NULL;
    new_block->free = 1;
    new_block->magic = BLOCK_MAGIC;

    if (tail != NULL)
        tail->next = new_block;
    else
        head = new_block;

    tail = new_block;

    if (heap_bytes <= SIZE_MAX - (padding + request))
        heap_bytes += padding + request;
    else
        heap_bytes = SIZE_MAX; /* Defensive saturation; practically unreachable. */

    return new_block;
}

/* Locate a suitable free block using the selected placement policy. */
static block_t *find_fit(size_t need)
{
    block_t *current;
    block_t *best = NULL;

    for (current = head; current != NULL; current = current->next) {
        if (current->magic != BLOCK_MAGIC)
            return NULL;

        if (!current->free || current->size < need)
            continue;

        if (placement_policy == FIT_FIRST)
            return current;

        if (best == NULL || current->size < best->size)
            best = current;
    }

    return best;
}

/* Split a block if its remainder can hold a header and 16 payload bytes. */
static void split_block(block_t *block, size_t need)
{
    size_t remainder;
    block_t *new_block;

    if (block->size < need)
        return;

    remainder = block->size - need;
    if (remainder < HDR + MIN_PAYLOAD)
        return;

    /* Do byte arithmetic: block_t pointer arithmetic would scale by sizeof. */
    new_block = (block_t *)((char *)payload(block) + need);
    new_block->size = remainder - HDR;
    new_block->next = block->next;
    new_block->free = 1;
    new_block->magic = BLOCK_MAGIC;

    block->size = need;
    block->next = new_block;

    if (tail == block)
        tail = new_block;
}

/*
 * Merge every pair of physically adjacent free blocks.
 * List neighbours are not necessarily physical neighbours because libc
 * or another subsystem may have moved the program break between our calls.
 */
static void coalesce(void)
{
    block_t *current = head;

    while (current != NULL && current->next != NULL) {
        block_t *next = current->next;
        void *physical_end = (char *)payload(current) + current->size;

        if (current->magic == BLOCK_MAGIC &&
            next->magic == BLOCK_MAGIC &&
            current->free && next->free &&
            physical_end == (void *)next) {

            if (current->size <= SIZE_MAX - HDR - next->size) {
                current->size += HDR + next->size;
                current->next = next->next;
                next->magic = DEAD_MAGIC;

                if (tail == next)
                    tail = current;
                /* Do not advance: the enlarged block may merge again. */
            } else {
                current = next;
            }
        } else {
            current = next;
        }
    }
}

/* Find a block by exact payload address; never dereference an arbitrary ptr. */
static block_t *find_pointer(void *ptr)
{
    block_t *current;

    for (current = head; current != NULL; current = current->next) {
        if (current->magic != BLOCK_MAGIC)
            return NULL;
        if (payload(current) == ptr)
            return current;
    }
    return NULL;
}

void *my_malloc(size_t size)
{
    size_t need;
    block_t *chosen;

    if (size == 0)
        return NULL;

    need = align_up(size);
    if (need == 0)
        return NULL;

    chosen = find_fit(need);
    if (chosen == NULL) {
        if (grow(need) == NULL)
            return NULL;
        chosen = find_fit(need);
        if (chosen == NULL)
            return NULL;
    }

    split_block(chosen, need);
    chosen->free = 0;
    return payload(chosen);
}

void my_free(void *ptr)
{
    block_t *block;

    if (ptr == NULL)
        return;

    block = find_pointer(ptr);
    if (block == NULL) {
        allocator_message("my_free: invalid pointer\n");
        return;
    }

    if (block->free) {
        allocator_message("my_free: double free detected\n");
        return;
    }

    block->free = 1;
    coalesce();
}

void *my_calloc(size_t nmemb, size_t size)
{
    size_t total;
    void *memory;

    if (nmemb == 0 || size == 0)
        return NULL;

    if (size > SIZE_MAX / nmemb)
        return NULL;

    total = nmemb * size;
    memory = my_malloc(total);
    if (memory != NULL)
        memset(memory, 0, total);

    return memory;
}

void *my_realloc(void *ptr, size_t size)
{
    block_t *block;
    size_t need;
    size_t old_size;
    void *new_memory;

    if (ptr == NULL)
        return my_malloc(size);

    if (size == 0) {
        my_free(ptr);
        return NULL;
    }

    block = find_pointer(ptr);
    if (block == NULL || block->free) {
        allocator_message("my_realloc: invalid pointer\n");
        return NULL;
    }

    need = align_up(size);
    if (need == 0)
        return NULL;

    old_size = block->size;

    /* The existing allocation is already large enough. */
    if (need <= old_size) {
        split_block(block, need);
        return ptr;
    }

    /*
     * Try to absorb the next block without moving the payload.
     * It must be both free and physically adjacent.
     */
    if (block->next != NULL &&
        block->next->magic == BLOCK_MAGIC &&
        block->next->free &&
        (char *)payload(block) + block->size == (char *)block->next) {

        block_t *next = block->next;

        if (block->size <= SIZE_MAX - HDR - next->size &&
            block->size + HDR + next->size >= need) {

            block->size += HDR + next->size;
            block->next = next->next;
            next->magic = DEAD_MAGIC;

            if (tail == next)
                tail = block;

            split_block(block, need);
            return ptr;
        }
    }

    /* Otherwise allocate elsewhere, copy the old contents, then free. */
    new_memory = my_malloc(size);
    if (new_memory == NULL)
        return NULL;

    memcpy(new_memory, ptr, old_size < size ? old_size : size);
    my_free(ptr);
    return new_memory;
}

void heap_stats(heap_stats_t *out)
{
    block_t *current;

    if (out == NULL)
        return;

    out->heap_bytes = heap_bytes;
    out->used_bytes = 0;
    out->free_bytes = 0;
    out->blocks = 0;
    out->free_blocks = 0;
    out->largest_free = 0;

    for (current = head; current != NULL; current = current->next) {
        if (current->magic != BLOCK_MAGIC)
            break;

        ++out->blocks;
        if (current->free) {
            out->free_bytes += current->size;
            ++out->free_blocks;
            if (current->size > out->largest_free)
                out->largest_free = current->size;
        } else {
            out->used_bytes += current->size;
        }
    }
}

void heap_dump(void)
{
    block_t *current;
    size_t index = 0;
    char line[160];

    for (current = head; current != NULL; current = current->next, ++index) {
        int length;

        if (current->magic != BLOCK_MAGIC) {
            allocator_message("heap_dump: invalid block header\n");
            return;
        }

        length = snprintf(line, sizeof(line),
                          "[%zu] %p size=%zu %s\n",
                          index, payload(current), current->size,
                          current->free ? "FREE" : "USED");

        if (length > 0) {
            size_t count = (size_t)length;
            if (count >= sizeof(line))
                count = sizeof(line) - 1;
            (void)write(STDOUT_FILENO, line, count);
        }
    }
}

void set_policy(fit_policy_t p)
{
    if (p == FIT_FIRST || p == FIT_BEST)
        placement_policy = p;
}

int heap_check(void)
{
    block_t *current;
    block_t *previous = NULL;

    for (current = head; current != NULL; current = current->next) {
        uintptr_t current_address = (uintptr_t)current;
        uintptr_t payload_address = (uintptr_t)payload(current);

        if (current->magic != BLOCK_MAGIC)
            return 0;

        if ((payload_address & ALIGN_MASK) != 0)
            return 0;

        if ((current->size & ALIGN_MASK) != 0)
            return 0;

        if (current->free != 0 && current->free != 1)
            return 0;

        if (previous != NULL) {
            uintptr_t previous_address = (uintptr_t)previous;

            if (current_address <= previous_address)
                return 0;

            /* Adjacent free blocks should already have been coalesced. */
            if (previous->free && current->free &&
                (char *)payload(previous) + previous->size == (char *)current)
                return 0;
        }

        previous = current;
    }

    if ((head == NULL) != (tail == NULL))
        return 0;

    if (tail != NULL && tail->next != NULL)
        return 0;

    return 1;
}
