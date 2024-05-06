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
// NXTGEN Proxmark3 trace manager
//
// This is degigned to be a replacement for the tracing functions in
// the Iceman fork
//-----------------------------------------------------------------------------

#include "tracer.h"

#include "palloc.h"
#include "pm3_cmd.h"
#include "dbprint.h"

static uint16_t trace_len = 0;       // How long our current trace is
static uint16_t free_space = 0;      // The amount of free space, in bytes
static bool tracing = false;         // Flag for if we are currently tracing or not
static uint16_t *blk_addr = nullptr; // The address to the space we have from palloc

/**
 * @brief Sets up the proxmark to begin tracing RFID waveforms.
 * 
 * This will set up a chunk of memory (if one hasn't already been set up) and set up Tracer to
 * accept trace logging commands.
 */
void start_tracing(void) {
    if(blk_addr == nullptr) { // Set up the block address if we don't already have one
        free_space = get_max_trace_length();
        blk_addr = palloc(1, free_space);
    }

    if(!tracing) {
        tracing = true;
    }

    // TODO maybe trigger the FGPA stuff here to enable tracing?
}

/**
 * @brief Toggle tracing on and off. This only works if memory has been set up for tracing to work
 * in the first place.
 */
void toggle_tracing(void) {
    if(blk_addr != nullptr) tracing = !tracing;
}

/**
 * @brief Stops the Proxmark from tracing more RFID waveforms.
 * 
 * This doesn't release the memory that Tracer has for the current trace
 */
void stop_tracing(void) {
    tracing = false;

    // TODO maybe trigger the FPGA stuff here to disable tracing?
}

/**
 * @brief Return if we are currently tracing or not.
 * 
 * @return `true` if we are tracing
 * @return `false` otherwise
 */
bool is_tracing(void) {
    return tracing && blk_addr != nullptr;
}

/**
 * @brief Get the maximum trace length we can have stored in memory (max is 32kb)
 * 
 * @return the maximum amount of space we can use for trace data
 */
uint16_t get_max_trace_length(void) {
    return (palloc_space_left() > MAX_BLOCK_SIZE ? MAX_BLOCK_SIZE : palloc_space_left());
}

/**
 * @brief Get the current trace's length
 * 
 * @return the length of the trace
 */
uint16_t get_trace_length(void) {
    return trace_len;
}

/**
 * @brief Returns the amount of space we have left that we can store trace data, in bytes
 */
uint16_t get_trace_space_left(void) {
    return free_space;
}

/**
 * @brief Gets the pointer of memory where Tracer is storing it's trace data. 
 * 
 ** DO NOT FREE THIS MEMORY! Use `release_trace()` to do that.
 * 
 * @return The pointer in memory of the trace data (`uint16_t*`) or `nullptr` if none has been set up
 */
uint16_t *get_current_trace(void) {
    return blk_addr;
}

/**
 * @brief Check to see if we have trace data
 * 
 * @return `true` if we do have trace data 
 * @return `false` otherwise
 */
bool has_trace_data(void) {
    return (trace_len > 0);
}

/**
 * @brief Releases the memory Tracer has back to the Proxmark. This will destroy the current trace!
 */
void release_trace(void) {
    if(blk_addr != nullptr) {
        tracing = false;
        trace_len = 0;
        free_space = 0;
        if(palloc_free(blk_addr)) {
            blk_addr = nullptr;
        } else {
            Dbprintf(_RED_("Error releasing Tracer memory back to SRAM! Please unplug your Proxmark!"));
        }
    }
}
