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
// NXTGEN Proxmark Card/Tag emulator
//-----------------------------------------------------------------------------

#include "cardemu.h"

#include <stdbool.h>
#include <stdint.h>

#include "ansi.h"
#include "common.h"
#include "palloc.h"
#include "pm3_cmd.h"
#include "dbprint.h"
#include "util.h"

static bool is_emulating = false;
static memptr_t *emu_addr = nullptr;

void start_emulation(void) {
    // TODO Flesh out an actual emulator
    is_emulating = true;
}

void stop_emulation(void) {
    // TODO Flesh out an actual emulator
    is_emulating = false;
}

bool emulator_running(void) {
    return is_emulating;
}

bool has_emulator_data(void) {
    return (emu_addr != nullptr);
}

memptr_t *get_emulator_address(void) {
    if(emu_addr == nullptr) {
        emu_addr = (memptr_t*)palloc(1, CARD_MEMORY_SIZE);
    }

    return emu_addr;
}

void clear_emulator(void) {
    if(emu_addr != nullptr) {
        palloc_set(emu_addr, 0, CARD_MEMORY_SIZE);
    }
}

void release_emuator(void) {
    if(emu_addr != nullptr) {
        is_emulating = false;
        palloc_free(emu_addr);
        emu_addr = nullptr;
        return;
    }

    if(PRINT_ERROR) Dbprintf("Unable to release emulator, no memory to release.");
}

int set_emulator_memory(const uint8_t *data, uint16_t offset, uint16_t len) {
    if(emu_addr == nullptr) {
        emu_addr = (memptr_t*)palloc(1, CARD_MEMORY_SIZE);
        if(emu_addr == nullptr) return PM3_EMALLOC;
    }

    if(offset + len <= CARD_MEMORY_SIZE) {
        palloc_copy(emu_addr + offset, data, len);
        return PM3_SUCCESS;
    }

    if(PRINT_ERROR) Dbprintf(_RED_("Tried to set memory out of emulator bounds! %d > %d"), (offset + len), CARD_MEMORY_SIZE);
    return PM3_EOUTOFBOUND;
}

int get_emulator_memory(uint8_t *out, uint16_t offset, uint16_t len) {
    if(emu_addr == nullptr) {
        if(PRINT_ERROR) Dbprintf(_RED_("Unable to get emulator memory! No memory set!"));
        return PM3_ENODATA;
    }

    if(offset + len <= CARD_MEMORY_SIZE) {
        palloc_copy(out, (emu_addr + offset), len);
        return PM3_SUCCESS;
    }

    if(PRINT_ERROR) Dbprintf(_RED_("Tried to read memory out of emulator bounds! %d > %d"), (offset + len), CARD_MEMORY_SIZE);
    return PM3_EOUTOFBOUND;
}