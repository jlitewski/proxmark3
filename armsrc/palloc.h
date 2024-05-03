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
//-----------------------------------------------------------------------------

#ifndef PALLOC_H__
#define PALLOC_H__

#include "common.h"

//-----------------------------------------------------------------------------
// Palloc (Proxmark ALLOCator) provides bare metal access to the memory
// provided by the Atmel SAM7S series MCU.
//
// It is up to the functions that request memory from palloc to gracefully
// handle situations where we can't allocate memory. It is also up to the
// functions that request memory to free it after it's done with it. The
// Proxmark3 only has 64kb of memory, so every bit literally counts
//-----------------------------------------------------------------------------

#define MAX_FRAME_SIZE          256 // maximum allowed ISO14443 frame
#define MAX_PARITY_SIZE         ((MAX_FRAME_SIZE + 7) / 8)

//====================
// General Functions
//====================

void palloc_init(void);
void *palloc(uint16_t numElement, const uint16_t size);
void palloc_copy(void *ptr, const void *src, uint16_t len);
bool palloc_free(void *ptr);

int8_t palloc_free_blocks(void);
int8_t palloc_used_blocks(void);
int8_t palloc_fresh_blocks(void);
size_t palloc_space_left(void);
void palloc_compact_heap(void);
bool palloc_heap_integrity(void);

//==============
// Buffer Stuff
//==============

typedef struct { // General purpose 8-bit buffer
    uint16_t size;
    uint8_t *data; // Pass this into `palloc_free()` to free the buffer
} buffer8u_t;

typedef struct { // General purpose 16-bit buffer
    uint16_t size;
    uint16_t *data; // Pass this into `palloc_free()` to free the buffer
} buffer16u_t;

typedef struct { // General purpose 32-bit buffer
    uint16_t size;
    uint32_t *data; // Pass this into `palloc_free()` to free the buffer
} buffer32u_t;

buffer8u_t  palloc_buffer8(uint16_t numElement);
buffer16u_t palloc_buffer16(uint16_t numElement);
buffer32u_t palloc_buffer32(uint16_t numElement);

//==============
// FPGA Stuff
//==============

// 8 data bits and 1 parity bit per payload byte, 1 correction bit, 1 SOC bit, 2 EOC bits
#define QUEUE_BUFFER_SIZE ((9 * MAX_FRAME_SIZE) + 1 + 1 + 2)

typedef struct {
    int16_t max; // -1 is no data, max data size is ~2.3k bytes
    uint8_t bit; // 0 through 8
    uint8_t *data;
} fpga_queue_t;

fpga_queue_t *get_fpga_queue(void);
void reset_fpga_queue(void);
void free_fpga_queue(void);
void stuff_bit_in_queue(uint8_t bit);

#endif //PALLOC_H__
