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

#ifndef CARDEMU_H__
#define CARDEMU_H__

#include "common.h"
#include "palloc.h"

#define CARD_MEMORY_SIZE 4096 // 4Kb should be a good size

//======================
// LF Card Defines
//======================

#define LF_CLK_125KHZ 64

void start_emulation(void);
void stop_emulation(void);
bool emulator_running(void);
bool has_emulator_data(void);

uint16_t *get_emulator_address(void);
void clear_emulator(void);
void release_emuator(void);

int set_emulator_memory(const uint8_t *data, uint16_t offset, uint16_t len);
int get_emulator_memory(uint8_t *out, uint16_t offset, uint16_t len);

#endif // CARDEMU_H__