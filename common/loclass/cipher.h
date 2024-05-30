//-----------------------------------------------------------------------------
// Borrowed initially from https://github.com/holiman/loclass
// Copyright (C) 2014 Martin Holst Swende
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
// WARNING
//
// THIS CODE IS CREATED FOR EXPERIMENTATION AND EDUCATIONAL USE ONLY.
//
// USAGE OF THIS CODE IN OTHER WAYS MAY INFRINGE UPON THE INTELLECTUAL
// PROPERTY OF OTHER PARTIES, SUCH AS INSIDE SECURE AND HID GLOBAL,
// AND MAY EXPOSE YOU TO AN INFRINGEMENT ACTION FROM THOSE PARTIES.
//
// THIS CODE SHOULD NEVER BE USED TO INFRINGE PATENTS OR INTELLECTUAL PROPERTY RIGHTS.
//-----------------------------------------------------------------------------
// It is a reconstruction of the cipher engine used in iClass, and RFID techology.
//
// The implementation is based on the work performed by
// Flavio D. Garcia, Gerhard de Koning Gans, Roel Verdult and
// Milosch Meriac in the paper "Dismantling IClass".
//-----------------------------------------------------------------------------

#ifndef CIPHER_H
#define CIPHER_H

#include "../include/common.h"

/**
* A cipher state of iClass s is an element of F 40/2, 
* consisting of the following four components:
*   1. The left register `l` = `(l 0 . . . l 7 ) ∈ F 8/2`
*   2. The right register  `r` = `(r 0 . . . r 7 ) ∈ F 8/2`
*   3. The top register    `t` = `(t 0 . . . t 15 ) ∈ F 16/2`
*   4. The bottom register `b` = `(b 0 . . . b 7 ) ∈ F 8/2`
**/
typedef struct {
    uint8_t l;
    uint8_t r;
    uint8_t b;
    uint16_t t;
} cipher_state_t;

void doMAC_N(uint8_t *address_data_p, uint8_t address_data_size, uint8_t *div_key_p, uint8_t mac[4]);

#ifndef ON_DEVICE
#include "pm3_cmd.h"

void doMAC(uint8_t *cc_nr_p, uint8_t *div_key_p, uint8_t mac[4]);

int testMAC(void);

#define opt_doTagMAC_1(cc_p, div_key_p) (cipher_state_t){0,0,0,0}
#define opt_doTagMAC_2(_init, nr, mac, div_key_p)
#define iclass_calc_div_key(csn, key, div_key, elite)
#define opt_doReaderMAC(ccnr, div_key, pmac)

#else
/**
 * @brief 
 * 
 * @param cc_nr_p The CC NR Pointer
 * @param div_key_p 
 * @param mac The MAC address `MAC(key, CC * NR)`
 */
void opt_doReaderMAC(uint8_t *cc_nr_p, uint8_t *div_key_p, uint8_t mac[4]);

void opt_doReaderMAC_2(cipher_state_t _init,  uint8_t *nr, uint8_t mac[4], const uint8_t *div_key_p);

/**
 * @brief 
 * 
 * @param cc_p The CC pointer
 * @param div_key_p 
 * @param mac The MAC address `MAC(key, CC * NR * 32x0)`
 */
void opt_doTagMAC(uint8_t *cc_p, const uint8_t *div_key_p, uint8_t mac[4]);

/**
 * @brief 
 * The tag MAC can be divided (both can, but no point in dividing the reader mac) into
 * two functions, since the first 8 bytes are known, we can pre-calculate the state
 * reached after feeding CC to the cipher.
 * @param cc_p
 * @param div_key_p
 * @return the cipher state
 */
cipher_state_t opt_doTagMAC_1(uint8_t *cc_p, const uint8_t *div_key_p);

/**
 * @brief 
 * The second part of the tag MAC calculation, since the CC is already calculated into the state,
 * this function is fed only the NR, and internally feeds the remaining 32 0-bits to generate the tag
 * MAC response.
 * @param _init - precalculated cipher state
 * @param nr - the reader challenge
 * @param mac - where to store the MAC
 * @param div_key_p - the key to use
 */
void opt_doTagMAC_2(cipher_state_t _init, uint8_t *nr, uint8_t mac[4], const uint8_t *div_key_p);

/**
 * @brief 
 * 
 * @param csn 
 * @param key 
 * @param div_key 
 * @param elite 
 */
void iclass_calc_div_key(uint8_t *csn, uint8_t *key, uint8_t *div_key, bool elite);
#endif // ON_DEVICE

#endif //CIPHER_H