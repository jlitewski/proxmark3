//-----------------------------------------------------------------------------
// Copyright (C) Proxmark3 contributors. See AUTHORS.md for details.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// See LICENSE.txt for the text of the license.
//-----------------------------------------------------------------------------
// NXTGEN Proxmark3 unmanaged memory manager
//
// This is designed to be a replacement to the BigBuf implementation the
// Iceman firmware uses
//============================== Special Thanks ================================
// thi-ng/tinyalloc - for the initial chunk of code palloc used
//==============================================================================
#include "palloc.h"

#include "util.h"    // nbytes
#include "dbprint.h" // logging
#include "pm3_cmd.h" // return defines

#define nullptr NULL // Helper define for null pointers

// Word size alignment
#define ALIGN_BYTES sizeof(uint32_t) // Word size of the Atmel SAM7S should be 4 bytes (32-bit)
#define ALIGN_MASK (0xFFFF + 1 - ALIGN_BYTES)

// The values set by the linker for what we have as bounds of memory to use
extern uint32_t _stack_start[], __bss_end__[];

// Memory defines
#define MEM_SIZE 65536 // Total memory size (in bytes) of the Atmel SAM7S series MCU we use
#define MEM_USABLE ((size_t)_stack_start - (size_t)__bss_end__) // The memory (in bytes) we can use

// Block configuration
#define MAX_BLOCK_SIZE 32000 // 32k should be more than enough
#define BLOCK_SPLIT_THRESHOLD 16
#define MAX_BLOCKS 32 // 32 blocks should give us an overall overhead of 768 bytes

typedef struct Block pBlock;

struct Block {
    void *address; // The memory address this block points to
    pBlock *next;  // The next block in the list, or NULL if there is none
    int16_t size;  // A block shouldn't be over 32kb big
};

typedef struct {
    bool init;     // Flag for if the heap was initialized
    pBlock *free;  // Free (previously used) Blocks List
    pBlock *used;  // Currently used Blocks List
    pBlock *fresh; // Fresh (never used) Blocks List
    size_t top;    // Top free address
} pHeap;

// This will automagically calculate the overhead we have if we tweak the values we use
#define OVERHEAD (MAX_BLOCKS * sizeof(pBlock))

static pHeap *heap = nullptr;
static size_t space_free = 0;

/**
 * @brief Initialize the palloc heap and blocks. 
 * This must be used before any other `palloc_*` function!
 */
void palloc_init(void) {
    // Set up the heap
    heap = (pHeap*)(_stack_start - __bss_end__);
    heap->init = false; // Signal that we haven't finished initializing yet
    heap->free = nullptr;
    heap->used = nullptr;
    heap->fresh = (pBlock*)(heap + 1);
    heap->top = (size_t)(heap->fresh + MAX_BLOCKS);

    // Set up the fresh blocks to use
    pBlock *block = heap->fresh;
    uint8_t i = (MAX_BLOCKS - 1);
    while(i--) {
        block->next = block + 1;
        block++;
    }

    // Calculate the amount of free space we have in the heap
    space_free = (MEM_USABLE - OVERHEAD);

    block->next = nullptr; // Set the last next block to nullptr to signal end of list
    heap->init = true; // Signal that we have initialized the heap
}

static pBlock *allocate_block(size_t alloc) {
    pBlock *ptr = heap->free;
    pBlock *prev = nullptr;
    size_t top = heap->top;

    
}

/**
 * @brief Allocates a block of memory to use. This acts like `calloc` internally, so the pointer
 * that's returned can safely be used and won't have issues with garbage data. Each block has a
 * hard limit of 32kb that can be allocated for, any amount over this will return a nullptr
 * 
 * @param numElement The number of elements to allocate data for
 * @param size The size of each element
 * @return the address of the block of memory, or nullptr
 */
void *palloc(uint16_t numElement, const uint16_t size) {
    if(((heap == NULL) || !(heap->init))) return false; // Can't allocate memory if we haven't initialized any

    size_t orig = numElement;
    numElement *= size;

    if((numElement - space_free) < 0) return nullptr; // We would overflow if we attempted to allocate this memory
    else if(numElement / size == orig) { // Sanity check to make sure the math maths
        pBlock *blk = allocate_block(numElement);

        if(blk != nullptr) {
            palloc_copy(blk->address, 0, blk->size); // Zero the memory
            space_free -= blk->size; // Remove the space we took up with this allocation
            return blk->address;
        }
    }

    return nullptr; // There's something wrong with the size allocation, abort
}

/**
 * @brief Copy `len` data from `src` to `ptr`. Functions like `memcpy` and `memset`
 * 
 * @param ptr The pointer to have the data copied to
 * @param src The source of the data to copy, or a number to copy into the pointer
 * @param len The amount of data (in bytes) to copy from the source to the pointer
 */
void palloc_copy(void *ptr, const void *src, uint16_t len) {
    if(ptr == nullptr) return; // Can't put data into a null pointer
    size_t *dst = (size_t*)ptr;

    // Perform a memcpy (src pointer isn't null and is within the range of values we can access)
    if((src != nullptr) && (uintptr_t)src >= (MEM_SIZE - MEM_USABLE)) {
        const size_t *data = (size_t*)src;

        size_t endLen = len & 0x03;

        while((len -= 4) > endLen) {
            *dst++ = *data++;
        }

        uint8_t *dstByte =  (uint8_t*)dst;
        uint8_t *dataByte = (uint8_t*)data;

        while(endLen--) {
            *dstByte++ = *dataByte++;
        }

        return;
    } else { // perform a memset (src pointer is either null (0) or not within the usable memory, treat is as a number)
        size_t num = (size_t)src;

        while(len--) {
            *dst++ = num;
        }

        return;
    }
}

/**
 * @brief Inserts a block into the heap's free list, sorted by it's address
 * 
 * @param blk The block to insert
 */
static void insert_free(pBlock *blk) {
    pBlock *ptr = heap->free;
    pBlock *prev = nullptr;

    while(ptr != nullptr) {
        if((size_t)blk->address <= (size_t)ptr->address) break;

        prev = ptr;
        ptr = ptr->next;
    }

    if(prev != nullptr) prev->next = blk;
    else heap->free = blk;

    blk->next = ptr;
}

/**
 * @brief Merge the blocks inbetween `from` and `to` together into the fresh list
 * 
 * @param from The block to strat from
 * @param to The block to end at
 */
static void merge_blocks(pBlock *from, pBlock *to) {
    pBlock *scan;

    // Merge the Blocks together into the fresh list.
    //
    // Note, this doesn't reset the data that these blocks stored. We do that in `palloc()`, so
    // we don't have to here
    while(from != to) {
        scan = from->next;
        from->next = heap->fresh;
        heap->fresh = from;
        from->address = 0;
        from->size = 0;
        from = scan;
    }
}

/**
 * @brief Compress the blocks in the Free list of the Heap to help deal with fragmentation
 */
static void compact_free(void) {
    pBlock *ptr = heap->free;
    pBlock *prev, *scan;

    while(ptr != nullptr) {
        prev = ptr;
        scan = ptr->next;

        while(scan != nullptr && ((size_t)prev->address + prev->size == (size_t)scan->address)) {
            prev = scan;
            scan = scan->next;
        }

        if(prev != ptr) { // We can merge blocks together. This DOESN'T check against the 32kb max size we have
            size_t newSize = ((size_t)prev->address - (size_t)ptr->address + prev->size);
            ptr->size = newSize;
            pBlock *next = prev->next;
            merge_blocks(ptr->next, prev->next);

            ptr->next = next;
        }

        ptr = ptr->next;
    }
}

/**
 * @brief Free the memory a pointer holds
 * 
 * @param ptr The pointer to free
 * @return true If the memory at pointer was freed
 * @return false otherwise
 */
bool palloc_free(void *ptr) {
    if(((heap == NULL) || !(heap->init))) return false; // Can't free memory if we haven't initialized any

    pBlock *blk = heap->used;
    pBlock *prev = nullptr;

    // Check to see where this memory is at
    while(blk != nullptr) {
        if(ptr == blk->address) {
            if(prev) {
                prev->next = blk->next;
            } else {
                heap->used = blk->next;
            }

            insert_free(blk); // Insert the block into the free list
            compact_free();   // Compact our free space to keep fragmentation low

            return true;
        }

        prev = blk;
        blk = blk->next;
    }

    return false; // Couldn't find the address of this memory in our blocks
}

/**
 * @brief Returns the amount of blocks in a specific Block container
 * 
 * @param ptr The pointer of the Block container to count
 * @return The amount of blocks in that container (`int8_t`) or -1 for uninitialized heap
 */
static int8_t count_blocks(pBlock *ptr) {
    if(((heap == NULL) || !(heap->init))) return -1; // Can't count blocks if we don't have any
    int8_t count = 0;

    while(ptr != nullptr) {
        count++;
        ptr = ptr->next;
    }

    return count;
}

/**
 * @brief Returns the amount of Blocks that are free to use
 * (These blocks were previously used)
 * 
 * @return The number of free blocks in the Heap, or -1 if the heap hasn't been initialized
 */
int8_t palloc_get_free(void) {
    return count_blocks(heap->free);
}

/**
 * @brief Returns the amount of Blocks that are currently being used
 * 
 * @return The number of used Blocks in the Heap, or -1 if the heap hasn't been initialized
 */
int8_t palloc_get_used(void) {
    return count_blocks(heap->used);
}

/**
 * @brief Returns the number of Blocks that haven't been used before
 * 
 * @return The number of fresh Blocks in the Heap, or -1 if the heap hasn't been initialized
 */
int8_t palloc_get_fresh(void) {
    return count_blocks(heap->fresh);
}
