//-----------------------------------------------------------------------------
// Copyright (C) Artyom Gnatyuk, 2020
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
// LF emul  -   Very simple mode. Simulate only predefined IDs
//              Short click - select next slot and start simulation
//-----------------------------------------------------------------------------

// TODO Maybe move parts over to cardemu?

#include "standalone.h"
#include "proxmark3_arm.h"
#include "appmain.h"
#include "fpgaloader.h"
#include "lfops.h"
#include "util.h"
#include "dbprint.h"
#include "ticks.h"
#include "string.h"
#include "palloc.h"
#include "cardemu.h"
#include "commonutil.h"

#define MAX_IND 16 // 4 LEDs - 2^4 combinations

// Predefined IDs must be stored in predefined_ids[].
static uint64_t predefined_ids[] = {0x565A1140BE, 0x365A398149, 0x5555555555, 0xFFFFFFFFFF};
static uint8_t predefined_slots;
static uint16_t *memory_addr = nullptr;
static uint16_t buffer_len = 0;

void ModInfo(void) {
    DbpString("  LF EM4100 simulator standalone mode");
}

static void fill_buff(uint8_t bit) {
    palloc_set((memory_addr + buffer_len), bit, LF_CLK_125KHZ / 2);
    buffer_len += (LF_CLK_125KHZ / 2);

    palloc_set((memory_addr + buffer_len), bit^1, LF_CLK_125KHZ / 2);
    buffer_len += (LF_CLK_125KHZ / 2);
}

static void construct_EM410x_emul(uint64_t id) {
    uint8_t i, j;
    int binary[4] = {0, 0, 0, 0};
    int parity[4] = {0, 0, 0, 0};
    buffer_len = 0;

    for (i = 0; i < 9; i++)
        fill_buff(1);

    for (i = 0; i < 10; i++) {
        for (j = 3; j >= 0; j--, id /= 2)
            binary[j] = id % 2;

        for (j = 0; j < 4; j++)
            fill_buff(binary[j]);

        fill_buff(binary[0] ^ binary[1] ^ binary[2] ^ binary[3]);
        for (j = 0; j < 4; j++)
            parity[j] ^= binary[j];
    }

    for (j = 0; j < 4; j++)
        fill_buff(parity[j]);

    fill_buff(0);
}

static void LED_Slot(uint8_t i) {
    LEDsoff();

    if (predefined_slots > 4) {
        LED(i % MAX_IND, 0); //binary indication for predefined_slots > 4
    } else {
        LED(1 << i, 0); //simple indication for predefined_slots <=4
    }
}

void RunMod(void) {
    StandAloneMode();
    FpgaDownloadAndGo(FPGA_BITSTREAM_LF);

    memory_addr = palloc(1, (MAX_BLOCK_SIZE / 4)); //8k bytes should be enough?
    if(memory_addr == nullptr) {
        Dbprintf(_RED_("Unable to allocate memory for the EM4100 Emulator!"));
        return;
    }

    Dbprintf("[=] >>  LF EM4100 emulator started  <<");

    uint8_t selected = 0; //selected slot after start
    predefined_slots = ARRAYLEN(predefined_ids);

    for (;;) {
        WDT_HIT();
        if (data_available()) break;

        SpinDelay(100);
        SpinUp(100);
        LED_Slot(selected);
        construct_EM410x_emul(rev_quads(predefined_ids[selected]));
        SimulateTagLowFrequency(buffer_len, 0, true);

        selected = (selected + 1) % predefined_slots;
    }

    memory_addr = palloc(1, (MAX_BLOCK_SIZE / 4)); //8k bytes should be enough?
    if(memory_addr == nullptr) {
        Dbprintf(_RED_("Unable to allocate memory for the EM4100 Emulator!"));
        return;
    }
}
