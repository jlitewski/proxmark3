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

#ifndef TRACER_H__
#define TRACER_H__

#include "util.h"

//-----------------------------------------------------------------------------
// Tracer is the next iteration of the Proxmark Tracing functions.
//
// The first version will use copies of BigBuf's tracing functions, but the
// intentions are to redesign how all of that is done to make it more
// memory efficient, since we now have the constraint of 32kb of maximum
// trace size compaired to BigBuf's ~42kb. It's a bit of a challenge, but
// it will make the Easy and the RDV4 better in the long run.
//-----------------------------------------------------------------------------
// The world could always use more Heros.
//-----------------------------------------------------------------------------

void start_tracing(void);
void toggle_tracing(void);
void stop_tracing(void);
bool is_tracing(void);

uint16_t get_max_trace_length(void);
uint16_t get_trace_length(void);
uint16_t get_trace_space_left(void);
memptr_t *get_current_trace(void);
bool has_trace_data(void);
void release_trace(void);

bool RAMFUNC log_trace(const uint8_t *trace, uint16_t len, uint32_t ts_start, uint32_t ts_end, const uint8_t *parity, bool is_reader);
bool RAMFUNC log_trace_ISO15639(const uint8_t *trace, uint16_t len, uint32_t ts_start, uint32_t ts_end, const uint8_t *parity, bool is_reader);
bool RAMFUNC log_trace_from_stream(const uint8_t *trace, uint16_t len, uint32_t ts_start, uint32_t ts_end, bool is_reader);

#endif // TRACER_H__
