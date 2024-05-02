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

#include "util.h" // nbytes
#include "dbprint.h" // logging

// Word size alignment (4 was the BigBuf default)
#define ALIGN_BYTES (4)
#define ALIGN_MASK  (0xFFFF + 1 - ALIGN_BYTES)

#define MAX_BLOCK_SIZE 32000 // 32k should be more than enough
#define BLOCK_SPLIT_THRESHOLD 16
#define MAX_BLOCKS 32 // 32 blocks will give us an overall overhead of 768 bytes

extern uint32_t _stack_start[], __bss_end__[];

typedef struct Block pBlock;

struct Block {
    void *address; // The memory address this block points to
    pBlock *next;  // The next block in the list, or NULL if there is none
    int16_t size;  // A block shouldn't be over 32kb big
};

typedef struct {
    pBlock *free;  // Free (Previously Used) Blocks List
    pBlock *used;  // Used Blocks List
    pBlock *fresh; // Fresh (Never Used) Blocks List
    uint32_t top;  // Top free address
} pHeap;

static pHeap *heap = NULL;

/**
 * @brief Initialize the palloc heap and blocks. 
 * This must be used before any other palloc function!
 */
void palloc_init(void) {
    // Set up the heap
    heap = (pHeap*)(_stack_start - __bss_end__);
    heap->free = NULL;
    heap->used = NULL;
    heap->fresh = (pBlock*)(heap + 1);
    heap->top = (size_t)(heap->fresh + MAX_BLOCKS);

    // Set up the fresh blocks to use
    pBlock *block = heap->fresh;
    uint8_t i = (MAX_BLOCKS - 1);
    while(i--) {
        block->next = block + 1;
        block++;
    }

    block->next = NULL;
}

/**
 * @brief Copy `len` data from `src` to `ptr`
 * 
 * @param ptr The pointer to have the data copied to
 * @param src The source of the data to copy
 * @param len The amount of data (in bytes) to copy from the source to the pointer
 */
void palloc_copy(void *ptr, const void *src, uint16_t len) {
    size_t *dst = (size_t*)ptr;
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
}

static void insert(pBlock *blk) {
    pBlock *ptr = heap->free;
    pBlock *prev = NULL;
    
}

/**
 * @brief Free the memory a pointer holds
 * 
 * @param ptr The pointer to free
 * @return true If the memory at pointer was freed
 * @return false otherwise
 */
bool palloc_free(void *ptr) {
    pBlock *blk = heap->used;
    pBlock *prev = NULL;

    while(blk != NULL) {
        if(ptr == blk->address) {
            if(prev) {
                prev->next = blk->next;
            } else {
                heap->used = blk->next;
            }

            return true;
        }

        prev = blk;
        blk = blk->next;
    }

    return false;
}

/**
 * @brief Returns the amount of blocks in a specific Block container
 * 
 * @param ptr The pointer of the Block container to count
 * @return The amount of blocks in that container (`uint8_t`)
 */
static uint8_t count_blocks(pBlock *ptr) {
    uint8_t count = 0;

    while(ptr != NULL) {
        count++;
        ptr = ptr->next;
    }

    return count;
}

/**
 * @brief Returns the amount of Blocks that are free to use
 * (These blocks were previously used)
 * 
 * @return The number of free blocks in the Heap
 */
uint8_t palloc_get_free(void) {
    return count_blocks(heap->free);
}

/**
 * @brief Returns the amount of Blocks that are currently being used
 * 
 * @return The number of used Blocks in the Heap
 */
uint8_t palloc_get_used(void) {
    return count_blocks(heap->used);
}

/**
 * @brief Returns the number of Blocks that haven't been used before
 * 
 * @return The number of fresh Blocks in the Heap
 */
uint8_t palloc_get_fresh(void) {
    return count_blocks(heap->fresh);
}
