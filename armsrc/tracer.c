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
#include "util.h"

static uint16_t trace_len = 0;       // How long our current trace is
static uint16_t free_space = 0;      // The amount of free space, in bytes
static bool tracing = false;         // Flag for if we are currently tracing or not
static memptr_t *blk_addr = nullptr; // The address to the space we have from palloc

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
    return (palloc_sram_left() > MAX_BLOCK_SIZE ? MAX_BLOCK_SIZE : palloc_sram_left());
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
memptr_t *get_current_trace(void) {
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

/**
 * @brief Generic trace logger function. All protocols can use this to store traces. The traces produced
 * can be fetched client side using the various commands to do so.
 * 
 * @param *trace The trace data
 * @param len The length of the data
 * @param ts_start When the trace was started
 * @param ts_end then the trace ended
 * @param *parity The parity bit data
 * @param is_reader Flag set to indicate if this trace was produced by the reader or the tag
 * @return `true` if the trace was successfully stored in memory,
 * @return `false` if there was an issue doing so 
 */
bool RAMFUNC log_trace(const uint8_t *trace, uint16_t len, uint32_t ts_start, uint32_t ts_end, const uint8_t *parity, bool is_reader) {
    if(!tracing || blk_addr == nullptr) return false;

    tracelog_hdr_t *header = (tracelog_hdr_t*)(blk_addr + trace_len);
    uint16_t num_parity = (len - 1) / 8 + 1; // number of valid paritybytes in *parity

    // Check to make sure we won't overflow our block of memory
    if(TRACELOG_HDR_LEN + len + num_parity >= get_max_trace_length() - trace_len) {
        Dbprintf(_RED_("Cannot trace anymore! Memory almost full!"));
        tracing = false;
        return false;
    }

    uint32_t duration;
    if (ts_end > ts_start) {
        duration = ts_end - ts_start;
    } else {
        duration = (UINT32_MAX - ts_start) + ts_end;
    }

    if (duration > 0xFFFF) duration = 0xFFFF;

    header->timestamp = ts_start;
    header->duration = duration & 0xFFFF;
    header->data_len = len;
    header->isResponse = !is_reader;
    trace_len += TRACELOG_HDR_LEN;

    if(trace != nullptr && len > 0) {
        palloc_copy(header->frame, trace, len);
        trace_len += len;
    }

    // parity bytes
    if (num_parity != 0) {
        if (parity != NULL) {
            palloc_copy((void*)(trace + trace_len), parity, num_parity);
        } else {
            palloc_set((void*)(trace + trace_len), 0x00, num_parity);
        }
        
        trace_len += num_parity;
    }

    return true;
}

/**
 * @brief Modified trace logging function for ISO15639 tags. This is needed because the times between
 * `ts_start` and `ts_end` won't fit into a 16-bit number, so we have to scale it to do so.
 * 
 * @param *trace The trace data
 * @param len The length of the data
 * @param ts_start When the trace was started
 * @param ts_end then the trace ended
 * @param *parity The parity bit data
 * @param is_reader Flag set to indicate if this trace was produced by the reader or the tag
 * @return `true` if the trace was successfully stored in memory,
 * @return `false` if there was an issue doing so 
 */
bool RAMFUNC log_trace_ISO15639(const uint8_t *trace, uint16_t len, uint32_t ts_start, uint32_t ts_end, const uint8_t *parity, bool is_reader) {
    // Scale the duration to fit into a uint16_t
    uint32_t duration = (ts_end - ts_start);
    duration /= 32;
    ts_end = ts_start + duration;

    return log_trace(trace, len, ts_start, ts_end, parity, is_reader);
}

/**
 * @brief Modified trace logging function for bitstreams. The partial byte size is stored in the first
 * parity byte. (eg. `bitstream "1100 00100010"` would signal the partial byte is 4 bits)
 * 
 * @param *trace The trace bitstream data
 * @param len The length of the bitstream data
 * @param ts_start When the trace was started
 * @param ts_end then the trace ended
 * @param is_reader Flag set to indicate if this trace was produced by the reader or the tag
 * @return `true` if the trace was successfully stored in memory,
 * @return `false` if there was an issue doing so 
 */
bool RAMFUNC log_trace_from_stream(const uint8_t *trace, uint16_t len, uint32_t ts_start, uint32_t ts_end, bool is_reader) {
    if(len == 0) return false;

    uint8_t parity[(nbytes(len) - 1) / 8 + 1];
    palloc_set(parity, 0, sizeof(parity));

    // parity has amount of leftover bits
    parity[0] = len % 8;

    return log_trace(trace, nbytes(len), ts_start, ts_end, parity, is_reader);
}
