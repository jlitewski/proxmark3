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

#ifndef offsetof
#define offsetof(type, field) ((size_t) &(((type *) 0)->field))
#endif

#include "dbprint.h" // logging
#include "proxmark3_arm.h" // LED control
#include "ticks.h"
#include "pm3_cmd.h" // return defines

// Word size alignment
#define ALIGN_BYTES sizeof(uint32_t) // Word size of the Atmel SAM7S should be 4 bytes (32-bit)
#define ALIGN_MASK (0xFFFF + 1 - ALIGN_BYTES)

// The values set by the linker for what we have as bounds of memory to use
extern uint32_t _stack_start[], __bss_start__[], __bss_end__[];

// Memory defines
#define MEM_SIZE 65536 // Total memory size (in bytes) of the Atmel SAM7S series MCU we use
#define MEM_USABLE ((size_t)_stack_start - (size_t)__bss_end__) // The memory (in bytes) we can use

// Block configuration
#define BLOCK_SPLIT_THRESHOLD 16
#define MAX_BLOCKS 32 // 32 blocks should give us an overall overhead of 768 bytes

typedef struct Block pBlock;
typedef struct Heap pHeap;

struct PACKED Block {
    void *address; // The memory address this block points to
    int16_t size;  // The size of the data at `address`
    pBlock *next;  // The next block in the list, or nullptr if there is none
};

struct PACKED Heap {
    pBlock *fresh; // Fresh (never used) Blocks List
    pBlock *free;  // Free (previously used) Blocks List
    pBlock *used;  // Currently used Blocks List
    size_t top;    // Top free address
};

/**
 * @brief The FPGA Queue
 * @note
 * This is a buffer where we can queue things up to be sent through the FPGA, for
 * any purpose (fake tag, as reader, whatever).
 * @note 
 * We go MSB first, since that is the order in which they go out on the wire. 
 */
static fpga_queue_t fpgaQueue = {
    .max = -1,
    .bit = 8,
    .data = nullptr
};

// This will automagically calculate the overhead we have if we tweak the values we use
#define OVERHEAD (MAX_BLOCKS * sizeof(pBlock))

static pHeap *heap = nullptr;
static size_t free_space = 0;

/**
 * @brief Initialize the palloc heap and blocks. 
 * This must be used before any other `palloc_*` function!
 */
void palloc_init(void) {
    // Set up the heap
    heap = (pHeap*)(__bss_start__);
    heap->free = nullptr;
    heap->used = nullptr;
    heap->fresh = (pBlock*)(heap + 1);
    heap->top = (size_t)(heap->fresh + MAX_BLOCKS);

    // Set up the fresh blocks to use
    pBlock *block = heap->fresh;
    uint8_t i = MAX_BLOCKS - 1;
    while(i--) {
        block->next = block + 1;
        block->size = 0;
        block++;
    }

    // Calculate the amount of free space we have in the heap
    free_space = (MEM_USABLE - OVERHEAD);

    block->size = 0;
    block->next = nullptr; // Set the last next block to nullptr to signal end of list
}

/**
 * @brief Merge the blocks inbetween `from` and `to` together into the fresh list
 * 
 * @param from The block to strat from
 * @param to The block to end at
 */
static void merge_blocks(pBlock *from, pBlock *to) {
    if(PRINT_DEBUG) Dbprintf(" - Palloc: Merging blocks...");
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
 * @brief Inserts a block into the heap, sorted by it's address
 * 
 * @param blk The block to insert
 */
static void insert_block(pBlock *blk) {
    if(PRINT_DEBUG) Dbprintf(" - Palloc: Inserting block into heap...");
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
 * @brief Compress the blocks in the Heap to help deal with fragmentation
 */
static void compact_heap(void) {
    if(PRINT_DEBUG) Dbprintf(" - Palloc: Compacting heap...");

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
            if(PRINT_DEBUG) Dbprintf(" - Palloc: Merging blocks %x & %x...", ptr->address, prev->address);
            size_t newSize = ((size_t)prev->address - (size_t)ptr->address + prev->size);
            ptr->size = newSize;
            pBlock *next = prev->next;

            merge_blocks(ptr->next, prev->next);

            ptr->next = next;
        }

        ptr = ptr->next;
    }

    if(PRINT_DEBUG) Dbprintf(" - Palloc: Heap Compacted!");
}

/**
 * @brief Takes a usable block from the heap and allocates it for the data we need. This will split
 * up blocks as well to keep things as compact as possible.
 * 
 * @param alloc The amount of space we need to allocate, in bytes
 * @return The pointer to the block of memory we allocated, or `nullptr` if we couldn't allocate the memory
 */
static pBlock *allocate_block(size_t alloc) {
    if(PRINT_DEBUG) Dbprintf(" - Palloc: Allocating block with size of %u", alloc);
    pBlock *ptr = heap->free;
    pBlock *prev = nullptr;
    size_t top = heap->top;

    // Try to get a free block
    while(ptr != nullptr) {
        const bool isTop = ((size_t)ptr->address + ptr->size >= top);

        if(isTop || ptr->size >= alloc) {
            if(PRINT_DEBUG) Dbprintf(" - Palloc: Found suitable block!");
            if(prev != nullptr) prev->next = ptr->next;
            else heap->free = ptr->next;

            ptr->next = heap->used;
            heap->used = ptr;

            if(isTop) {
                ptr->size = alloc;
                heap->top = (size_t)ptr->address + alloc;
            } else if(heap->fresh != nullptr) {
                size_t excess = ptr->size - alloc;

                if(excess >= BLOCK_SPLIT_THRESHOLD) { // Split the block
                    if(PRINT_DEBUG) Dbprintf(" - Palloc: Spliting block %x...", ptr->address);
                    ptr->size = alloc;
                    pBlock *split = heap->fresh;
                    heap->fresh = split->next;
                    split->address = (void*)((size_t)ptr->address + alloc);
                    split->size = excess;
                    insert_block(split);
                    compact_heap();
                }
            }

            return ptr;
        }

        prev = ptr;
        ptr = ptr->next;
    }

    // We didn't match any free blocks, try to get a fresh one
    if(heap->fresh != nullptr) {
        if(PRINT_DEBUG) Dbprintf(" - Palloc: Using a fresh block for allocation...");
        ptr = heap->fresh;
        heap->fresh = ptr->next;
        ptr->address = (void*)top;
        ptr->next = heap->used;
        ptr->size = alloc;
        heap->used = ptr;
        heap->top = top + alloc;

        return ptr;
    }

    if(PRINT_ERROR) Dbprintf(" - Palloc: "_RED_("Unable to allocate a new block!"));

    // We were unable to get a block
    return nullptr;
    
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
memptr_t *palloc(uint16_t numElement, const uint16_t size) {
    if(PRINT_DEBUG) Dbprintf(" - Palloc: Allocating memory... (size %u numElement %u)", size, numElement);

    if(heap == nullptr) return nullptr; // Can't allocate memory if we haven't initialized any
    
    size_t allocSize = numElement * size;

    if(allocSize & ALIGN_MASK) { // Make sure we align our sizes
        allocSize += (allocSize + ALIGN_BYTES - 1) & ~ALIGN_MASK;
    }

    if(PRINT_DEBUG) Dbprintf("Allocation size: %u", allocSize);

    if((allocSize > MAX_BLOCK_SIZE) || (allocSize > free_space)) { // We would overflow if we attempted to allocate this memory
        if(PRINT_ERROR) Dbprintf(" - Palloc: "_RED_("Allocation size is too big!") " (%u)", allocSize);
        return nullptr;
    } else if(allocSize < (numElement * size)) { // Sanity check to make sure we are allocating enough memory
        if(PRINT_ERROR) Dbprintf(" - Palloc: "_RED_("Allocation sanity check failed!") " (%u < %u * %u)", allocSize, numElement, size);
        return nullptr;
    }

    pBlock *blk = allocate_block(allocSize);

    if(blk != nullptr) {
        palloc_set(blk->address, 0, blk->size); // Zero the memory
        free_space -= blk->size; // Remove the space we took up with this allocation
        return blk->address;
    }

    if(PRINT_ERROR) Dbprintf(" - Palloc: " _RED_("There was an issue with allocating memory!"));
    return nullptr; // There's something wrong with the size allocation, abort
}

/**
 * @brief Copy `len` data from `src` to `ptr`. Functions like `memcpy`
 * 
 * @param ptr The pointer to have the data copied to
 * @param src The source of the data to copy
 * @param len The amount of data (in bytes) to copy from the source to the pointer
 */
void palloc_copy(void *ptr, const void *src, uint16_t len) {
    if(ptr == nullptr) return; // Can't put data into a null pointer
    uint16_t *copyptr = (uint16_t*)ptr;
    uint16_t *copysrc = (uint16_t*)src;

     // Set as many full words as we can (the SAM7S512 has 16-bit word sizes in Thumb mode)
    size_t full_words = (len / sizeof(uint16_t));
    for(uint16_t i = 0; i < full_words; i++) {
        copyptr[i] = copysrc[i];
    }

    // Copy any remaining bytes (if needed)
    uint8_t remainder = (len % sizeof(uint16_t));
    if(remainder >= 1) {
        uint8_t *copyptr_byte = (uint8_t*)&copyptr[full_words];
        uint8_t *copysrc_byte = (uint8_t*)&copysrc[full_words];
        *copyptr_byte = *copysrc_byte;
    }
}

/**
 * @brief Sets `len` data in `ptr` to `value`. This set data in 16-bit word sections, with any remainers
 * set byte by byte as needed.
 * 
 * @param ptr The pointer to have the data set to
 * @param value The value to set
 * @param len The amount of data (in bytes) to set
 */
void palloc_set(void *ptr, const uint16_t value, uint16_t len) {
    if(ptr == nullptr) return;
    uint16_t *setptr = (uint16_t*)ptr;

    // Set as many full words as we can (the SAM7S512 has 16-bit word sizes in Thumb mode)
    size_t full_words = (len / sizeof(uint16_t));
    for(uint16_t i = 0; i < full_words; i++) {
        setptr[i] = value;
    }

    // Copy any remaining bytes (if needed)
    uint8_t remainder = (len % sizeof(uint16_t));
    if(remainder >= 1) {
        uint8_t *setbyte = (uint8_t*)&setptr[full_words];
        *setbyte = (uint8_t)value;
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
    return palloc_freeEX(ptr, false);
}

/**
 * @brief Free the memory a pointer holds
 * 
 * @param ptr The pointer to free
 * @param verbose Flag to print extended info and debug messages
 * @return true If the memory at pointer was freed
 * @return false otherwise
 */
bool palloc_freeEX(void *ptr, bool verbose) {
    if(PRINT_DEBUG) Dbprintf(" - Palloc: Freeing allocated memory at %x", ptr);
    if(heap == NULL) return false; // Can't free memory if we haven't initialized any

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

            insert_block(blk); // Insert the block into the free list
            compact_heap();   // Compact our free space to keep fragmentation low
            free_space += blk->size; // Add the amount of space back into our counter

            if(PRINT_DEBUG) Dbprintf(" - Palloc: Memory Freed!");
            return true;
        }

        prev = blk;
        blk = blk->next;
    }

    if(PRINT_DEBUG) Dbprintf(" - Palloc: "_YELLOW_("Couldn't find a block for this memory, are you sure it's ours?"));

    return false; // Couldn't find the address of this memory in our blocks
}

/**
 * @brief Returns the amount of blocks in a specific Block container
 * 
 * @param ptr The pointer of the Block container to count
 * @return The amount of blocks in that container (`int8_t`) or -1 for uninitialized heap
 */
static int count_blocks(pBlock *ptr) {
    if(heap == nullptr) return -1;

    int count = 0;

    while(ptr != nullptr) {
        count++;
        pBlock *blk = ptr->next;
        ptr = blk->next;
    }

    return count;
}

/**
 * @brief Returns the amount of Blocks that are free to use
 * (These blocks were previously used)
 * 
 * @return The number of free blocks in the Heap, or -1 if the heap hasn't been initialized
 */
int palloc_free_blocks(void) {
    return count_blocks(heap->free);
}

/**
 * @brief Returns the amount of Blocks that are currently being used
 * 
 * @return The number of used Blocks in the Heap, or -1 if the heap hasn't been initialized
 */
int palloc_used_blocks(void) {
    return count_blocks(heap->used);
}

/**
 * @brief Returns the number of Blocks that haven't been used before
 * 
 * @return The number of fresh Blocks in the Heap, or -1 if the heap hasn't been initialized
 */
int palloc_fresh_blocks(void) {
    return count_blocks(heap->fresh);
}

/**
 * @brief Returns the amount of sram we have left to allocate stuff with. This is only taking the
 * microprocessor sram into account, not any connect flash memory space
 * 
 * @return The amount of sram we have left, in bytes 
 */
size_t palloc_sram_left(void) {
    return free_space;
}

/**
 * @brief Manually compact the heap. Palloc does a good job at doing this itself, but in some
 * dire situations, it might be useful to manually do it.
 */
void palloc_compact_heap(void) {
    if(heap == nullptr) return; // Sanity checking

    compact_heap();
}

/**
 * @brief Checks the integrity of the heap
 * 
 * @return `true` if the heap is okay 
 * @return `false` otherwise
 */
bool palloc_heap_integrity(void) {
    int count = palloc_used_blocks();

    return count == 0;
}

void palloc_status(void) {
    Dbprintf("--- " _CYAN_("Memory") " --------------------");
    Dbprintf(" - Heap Top:............... "_YELLOW_("0x%x"), heap->top);
    Dbprintf(" - Usable:................. "_YELLOW_("%d"), MEM_USABLE);
    Dbprintf(" - Free:................... "_YELLOW_("%d"), palloc_sram_left());
    Dbprintf(" - Heap Initialized:....... %s", (heap != nullptr ? _GREEN_("YES") : _RED_("NO")));
    Dbprintf(" - Heap Status:............ %s", (palloc_heap_integrity() ? _GREEN_("OK") : _RED_("INTEGRITY ISSUES")));

    Dbprintf("--- " _CYAN_("Blocks") " --------------------");
    Dbprintf(" - Fresh:.................. "_YELLOW_("%d"), palloc_fresh_blocks());
    Dbprintf(" - Used:................... "_YELLOW_("%d"), palloc_used_blocks());
    Dbprintf(" - Free:................... "_YELLOW_("%d"), palloc_free_blocks());
}

uint32_t palloc_sram_size() {
    return MEM_USABLE;
}

/**
 * @brief Create a general purpose 8-bit buffer
 * 
 * @param numElement the amount of elements in this buffer
 * @return a buffer8u_t object, or an empty buffer if we couldn't allocate the space for it
 */
buffer8u_t palloc_buffer8(uint16_t numElement) {
    buffer8u_t buffer = { .data = nullptr, .size = 0 }; // initialize a "empty" buffer

    if(heap == nullptr || numElement > MAX_BLOCK_SIZE) return buffer; // Sanity checking

    if(numElement & ALIGN_MASK) { // Make sure we align our sizes
        numElement = (numElement + ALIGN_BYTES - 1) & ~ALIGN_MASK;
    }

    // We would overflow if we attempted to allocate this memory
    if((numElement > free_space)) return buffer; // Return the "empty" buffer

    pBlock *blk = allocate_block(numElement);
    if(blk != nullptr) {
        palloc_set(blk->address, 0, blk->size); // Remove any garbage
        buffer.data = (uint8_t*)blk->address;
        buffer.size = blk->size;
    }

    return buffer;
}

/**
 * @brief Create a general purpose 16-bit buffer
 * 
 * @param numElement the amount of elements in this buffer
 * @return a buffer16u_t object, or an empty buffer if we couldn't allocate the space for it
 */
buffer16u_t palloc_buffer16(uint16_t numElement) {
    buffer16u_t buffer = { .data = nullptr, .size = 0 }; // initialize a "empty" buffer
    size_t alloc = numElement * sizeof(uint16_t); // Adjust for the buffer type

    if(heap == nullptr || alloc > MAX_BLOCK_SIZE) return buffer; // Sanity checking

    if(alloc & ALIGN_MASK) { // Make sure we align our sizes
        alloc = (alloc + ALIGN_BYTES - 1) & ~ALIGN_MASK;
    }

    // We would overflow if we attempted to allocate this memory
    if((alloc > free_space)) return buffer; // Return the "empty" buffer

    pBlock *blk = allocate_block(alloc);
    if(blk != nullptr) {
        palloc_set(blk->address, 0, blk->size); // Remove any garbage
        buffer.data = (uint16_t*)blk->address;
        buffer.size = blk->size;
    }

    return buffer;
}

/**
 * @brief Create a general purpose 32-bit buffer
 * 
 * @param numElement the amount of elements in this buffer
 * @return a buffer32u_t object, or an empty buffer if we couldn't allocate the space for it
 */
buffer32u_t palloc_buffer32(uint16_t numElement) {
    buffer32u_t buffer = { .data = nullptr, .size = 0 }; // initialize a "empty" buffer
    size_t alloc = numElement * sizeof(uint32_t); // Adjust for the buffer type

    if(heap == nullptr || alloc > MAX_BLOCK_SIZE) return buffer; // Sanity checking

    if(alloc & ALIGN_MASK) { // Make sure we align our sizes
        alloc = (alloc + ALIGN_BYTES - 1) & ~ALIGN_MASK;
    }

    // We would overflow if we attempted to allocate this memory
    if((alloc > free_space)) return buffer; // Return the "empty" buffer

    pBlock *blk = allocate_block(alloc);
    if(blk != nullptr) {
        palloc_set(blk->address, 0, blk->size); // Remove any garbage
        buffer.data = (uint32_t*)blk->address;
        buffer.size = blk->size;
    }

    return buffer;
}

/**
 * @brief Get the fpga queue object
 * 
 * @return The FPGA queue, or `nullptr` if there was an issue allocating memory for it
 */
fpga_queue_t *get_fpga_queue(void) {
    if(fpgaQueue.data == nullptr) { // If the queue hasn't been initialized yet, do so
        pBlock *blk = allocate_block(QUEUE_BUFFER_SIZE);

        if(blk != nullptr) { // If we did get data to initialize
            palloc_set(blk->address, 0, blk->size); // Remove any garbage
            fpgaQueue.data = blk->address;
        } else return nullptr;
    }

    return &fpgaQueue;
}

/**
 * @brief Resets the FPGA queue back to default, but doesn't release the underlying buffer
 */
void reset_fpga_queue(void) {
    if(fpgaQueue.data != nullptr) {
        palloc_set(fpgaQueue.data, 0, QUEUE_BUFFER_SIZE);
        fpgaQueue.max = -1;
        fpgaQueue.bit = 8;
    }
}

/**
 * @brief Resets the FPGA queue and releases the underlying buffer
 */
void free_fpga_queue(void) {
    if(fpgaQueue.data != nullptr) {
        reset_fpga_queue();
        palloc_free(fpgaQueue.data);
        fpgaQueue.data = nullptr;
    }
}

void stuff_bit_in_queue(uint8_t bit) {
    if(fpgaQueue.data != nullptr) {
        if(fpgaQueue.max >= (QUEUE_BUFFER_SIZE - 1)) {
            Dbprintf(_RED_("FPGA Queue Buffer Overflow!"));
            return;
        }

        // Add another byte to the buffer if needed
        if(fpgaQueue.bit >= 8) {
            fpgaQueue.max++;
            fpgaQueue.data[fpgaQueue.max] = 0;
            fpgaQueue.bit = 0;
        }

        if(bit) fpgaQueue.data[fpgaQueue.max] |= (1 << (7 - fpgaQueue.bit));

        if(fpgaQueue.max >= QUEUE_BUFFER_SIZE) return;

        fpgaQueue.bit++;
    }
}
