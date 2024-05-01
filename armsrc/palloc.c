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
// thi-ng/tinyalloc - for the general idea of the blocks and heap used
// rhempel/umm_malloc - for the best-fit algo
//==============================================================================
#include "palloc.h"

#include "util.h" // nbytes
#include "dbprint.h" // logging

#define ALIGN_BYTES (4)
#define ALIGN_MASK  (0xFFFF + 1 - ALIGN_BYTES)

extern uint32_t _stack_start[], __bss_end__[];

typedef struct pBlock pBlock;
struct pBlock {
    void *address;
    pBlock *next;
    size_t size;
};


