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
// at91sam7s USB CDC device implementation
// based on the "Basic USB Example" from ATMEL (doc6123.pdf)
//-----------------------------------------------------------------------------

#include "usb_cdc.h"

#include "at91sam7s512.h"
#include "proxmark3_arm.h"
#include "usart_defs.h"

/*
AT91SAM7S256  USB Device Port
• Embedded 328-byte dual-port RAM for endpoints
• Four endpoints
– Endpoint 0: 8 bytes
– Endpoint 1 and 2: 64 bytes ping-pong
– Endpoint 3: 64 bytes
– Ping-pong Mode (two memory banks) for bulk endpoints
*/

//
#define AT91C_EP_CONTROL 0
#define AT91C_EP_OUT     1  // cfg bulk out
#define AT91C_EP_IN      2  // cfg bulk in
#define AT91C_EP_NOTIFY  3  // cfg cdc notification interrup

// The endpoint size is defined in usb_cdc.h

enum usb {
    // USB Descriptors
    USBDSC_DEVICE          = 0x01, // Device Descriptor
    USBDSC_CONFIG          = 0x02, // Configuration Descriptor
    USBDSC_STRING          = 0x03, // String Descriptor
    USBDSC_INTERFACE       = 0x04, // Interface Descriptor
    USBDSC_ENDPOINT        = 0x05, // Endpoint Descriptor
    USBDSC_DEVICE_QUALIFER = 0x06, // Device Qualifier Descriptor
    USBDSC_OTHER_SPEED_CFG = 0x07, // Other Speed Configuration Descriptor
    USBDSC_INTERFACE_PWR   = 0x08, // Interface Power Descriptor
    USBDSC_OTG             = 0x09, // USB On-The-Go Descriptor
    USBDSC_IAD             = 0x0B, // Interface Association Descriptor
    USBDSC_BOS             = 0x0F, // BOS Descriptor

    // USB Configuration Attributes
    USBCFG_ATR_DEFAULT = (0x01 << 7), // Default Value (Bit 7 is set)
    USBCFG_ATR_SELFPWR = (0x01 << 6), // Self-powered (Supports if set)
    USBCFG_ATR_RWAKEUP = (0x01 << 5), // Remote Wakeup (Supports if set)
    USBCFG_ATR_HNP     = (0x01 << 1), // HNP (Supports if set)
    USBCFG_ATR_SRP     = (0x01),      // SRP (Supports if set)

    // USB Standard get/set/clr codes
    USBGET_STATUS_ZERO       = 0x0080,
    USBCLR_FEATURE_ZERO      = 0x0100,
    USBSET_FEATURE_ZERO      = 0x0300,

    USBGET_STATUS_INTERFACE  = 0x0081,
    USBCLR_FEATURE_INTERFACE = 0x0101,
    USBSET_FEATURE_INTERFACE = 0x0301,

    USBGET_STATUS_ENDPOINT   = 0x0082,
    USBCLR_FEATURE_ENDPOINT  = 0x0102,
    USBSET_FEATURE_ENDPOINT  = 0x0302,

    USBSET_ADDRESS           = 0x0500,

    USBGET_DESCRIPTOR        = 0x0680,
    USBSET_DESCRIPTOR        = 0x0700,

    USBGET_CONFIG            = 0x0880,
    USBSET_CONFIG            = 0x0900,

    USBGET_INTERFACE         = 0x0A81,
    USBSET_INTERFACE         = 0x0B01,

    USB_SYNCH_FRAME          = 0x0C82,

    // USB CDC specific codes
    USBGET_CDC_LINE_CODING   = 0x21A1,
    USBSET_CDC_LINE_CODING   = 0x2021,

    USBSET_CDC_CTRL_LINE_STATE = 0x2221
};

enum endpoint {
    // Endpoint Transfer Types
    EP_TT_CRTL      = 0x00, // Control Transfer
    EP_TT_ISO       = 0x01, // Isochronous Transfer
    EP_TT_BULK      = 0x02, // Bulk Transfer
    EP_TT_INTERRUPT = 0x03, // Interrupt Transfer

    // Endpoint definitions
    // (bit7 | 0 = OUT, 1 = IN)
    EP00_IN  = 0x80,
    EP00_OUT = 0x00,
    EP01_IN  = 0x81,
    EP01_OUT = 0x01,
    EP02_IN  = 0x82,
    EP02_OUT = 0x02,
    EP03_IN  = 0x83,
    EP03_OUT = 0x03,
    EP04_IN  = 0x84,
    EP04_OUT = 0x04
};


/* WCID specific Request Code */
#define MS_OS_DESCRIPTOR_INDEX          0xEE
#define MS_VENDOR_CODE                  0x1C
#define MS_EXTENDED_COMPAT_ID           0x04
#define MS_EXTENDED_PROPERTIES          0x05
#define MS_WCID_GET_DESCRIPTOR          0xC0
#define MS_WCID_GET_FEATURE_DESCRIPTOR  0xC1

static bool isAsyncRequestFinished = false;
static AT91PS_UDP pUdp = AT91C_BASE_UDP;
static uint8_t btConfiguration = 0;
static uint8_t btConnection    = 0;
static uint8_t btReceiveBank   = AT91C_UDP_RX_DATA_BK0;

static const char devDescriptor[] = {
    /* Device descriptor */
    0x12,                      // Length
    USBDSC_DEVICE,     // Descriptor Type (DEVICE)
    0x00, 0x02,                // Complies with USB Spec. Release (0200h = release 2.00)  0210 == release 2.10
    2,                         // Device Class:    Communication Device Class
    0,                         // Device Subclass: CDC class sub code ACM [ice 0x02 = win10 virtual comport ]
    0,                         // Device Protocol: CDC Device protocol (unused)
    AT91C_USB_EP_CONTROL_SIZE, // MaxPacketSize0
    0xc4, 0x9a,                // Vendor ID  [0x9ac4 = J. Westhues]
    0x8f, 0x4b,                // Product ID [0x4b8f = Proxmark-3 RFID Instrument]
    0x00, 0x01,                // BCD Device release number (1.00)
    1,                         // index Manufacturer
    2,                         // index Product
    3,                         // index SerialNumber
    1                          // Number of Configs
};

static const char cfgDescriptor[] = {

    /* Configuration 1 descriptor */
    // -----------------------------
    9,                                          // Length
    USBDSC_CONFIG,               // Descriptor Type
    (9 + 9 + 5 + 5 + 4 + 5 + 7 + 9 + 7 + 7), 0, // Total Length 2 EP + Control
    2,                                          // Number of Interfaces
    1,                                          // Index value of this Configuration (used in SetConfiguration from Host)
    0,                                          // Configuration string index
    USBCFG_ATR_DEFAULT,                         // Attributes 0xA0
    0xFA,                                       // Max Power consumption

    /* Interface 0 Descriptor */
    /* CDC Communication Class Interface Descriptor Requirement for Notification */
    // -----------------------------------------------------------
    9,          // Length
    USBDSC_INTERFACE, // Descriptor Type
    0,          // Interface Number
    0,          // Alternate Setting
    1,          // Number of Endpoints in this interface
    2,          // Interface Class code    (Communication Interface Class)
    2,          // Interface Subclass code (Abstract Control Model)
    1,          // InterfaceProtocol       (Common AT Commands, V.25term)
    0,          // iInterface

    /* Header Functional Descriptor */
    5,          // Function Length
    0x24,       // Descriptor type:    CS_INTERFACE
    0,          // Descriptor subtype: Header Functional Descriptor
    0x10, 0x01, // bcd CDC:1.1

    /* ACM Functional Descriptor */
    4,          // Function Length
    0x24,       // Descriptor Type:    CS_INTERFACE
    2,          // Descriptor Subtype: Abstract Control Management Functional Descriptor
    2,          // Capabilities        D1, Device supports the request combination of Set_Line_Coding, Set_Control_Line_State, Get_Line_Coding, and the notification Serial_State

    /* Union Functional Descriptor */
    5,          // Function Length
    0x24,       // Descriptor Type:    CS_INTERFACE
    6,          // Descriptor Subtype: Union Functional Descriptor
    0,          // MasterInterface:    Communication Class Interface
    1,          // SlaveInterface0:    Data Class Interface

    /* Call Management Functional Descriptor */
    5,          // Function Length
    0x24,       // Descriptor Type:    CS_INTERFACE
    1,          // Descriptor Subtype: Call Management Functional Descriptor
    0,          // Capabilities:       Device sends/receives call management information only over the Communication Class interface. Device does not handle call management itself
    1,          // Data Interface:     Data Class Interface

    /* CDC Notification Endpoint descriptor */
    // ---------------------------------------
    7,                               // Length
    USBDSC_ENDPOINT,         // Descriptor Type
    EP03_IN,                         // EndpointAddress:   Endpoint 03 - IN
    EP_TT_INTERRUPT,                 // Attributes
    AT91C_USB_EP_CONTROL_SIZE, 0x00, // MaxPacket Size:    EP0 - 8
    0xFF,                            // Interval polling

    /* Interface 1 Descriptor */
    /* CDC Data Class Interface 1 Descriptor Requirement */
    9,                // Length
    USBDSC_INTERFACE, // Descriptor Type
    1,                // Interface Number
    0,                // Alternate Setting
    2,                // Number of Endpoints
    0x0A,             // Interface Class:     CDC Data interface class
    0,                // Interface Subclass:  not used
    0,                // Interface Protocol:  No class specific protocol required (usb spec)
    0,                // Interface

    /* Endpoint descriptor */
    7,                           // Length
    USBDSC_ENDPOINT,             // Descriptor Type
    EP01_OUT,                    // Endpoint Address:    Endpoint 01 - OUT
    EP_TT_BULK,                  // Attributes:          BULK
    AT91C_USB_EP_OUT_SIZE, 0x00, // MaxPacket Size:      64 bytes
    0,                           // Interval:            ignored for bulk

    /* Endpoint descriptor */
    7,                           // Length
    USBDSC_ENDPOINT,             // Descriptor Type
    EP02_IN,                     // Endpoint Address:    Endpoint 02 - IN
    EP_TT_BULK,                  // Attribute:           BULK
    AT91C_USB_EP_IN_SIZE, 0x00,  // MaxPacket Size:      64 bytes
    0                            // Interval:            ignored for bulk
};

// BOS descriptor
static const char bosDescriptor[] = {
    0x5,
    USBDSC_BOS,
    0xC,
    0x0,
    0x1,  // 1 device capability
    0x7,
    0x10, // USB_DEVICE_CAPABITY_TYPE,
    0x2,
    0x2,  // LPM capability bit set
    0x0,
    0x0,
    0x0
};

static const char StrLanguageCodes[] = {
    4,             // Length
    USBDSC_STRING, // Type is string
    0x09, 0x04     // supported language Code 0 = 0x0409 (English)
};

// Note: ModemManager (Linux) ignores Proxmark3 devices by matching the
// manufacturer string "proxmark.org". Don't change this.
// or use the blacklisting file.
static const char StrManufacturer[] = {
    26,            // Length
    USBDSC_STRING, // Type is string
    'p', 0, 'r', 0, 'o', 0, 'x', 0, 'm', 0, 'a', 0, 'r', 0, 'k', 0, '.', 0, 'o', 0, 'r', 0, 'g', 0,
};

static const char StrProduct[] = {
    20,            // Length
    USBDSC_STRING, // Type is string
    'p', 0, 'r', 0, 'o', 0, 'x', 0, 'm', 0, 'a', 0, 'r', 0, 'k', 0, '3', 0
};

#ifndef WITH_FLASH
static const char StrSerialNumber[] = {
    14,            // Length
    USBDSC_STRING, // Type is string
    'N', 0, 'X', 0, 'T', 0, 'G', 0, 'E', 0, 'N', 0
};
#else // WITH_FLASH is defined

// Manually calculated size of descriptor with unique ID:
// offset  0, lengt h 1: total length field
// offset  1, length  1: descriptor type field
// offset  2, length 12: 6x unicode chars (original string)
// offset 14, length  4: 2x unicode chars (underscores)      [[ to avoid descriptor being (size % 8) == 0, OS bug workaround ]]
// offset 18, length 32: 16x unicode chars (8-byte serial as hex characters)
// ============================
// total: 50 bytes
#define USB_STRING_DESCRIPTOR_SERIAL_NUMBER_LENGTH  50
char StrSerialNumber[] = {
    14,            // Length is initially identical to non-unique version ... The length updated at boot, if unique serial is available
    USBDSC_STRING, // Type is string
    'N', 0, 'X', 0, 'T', 0, 'G', 0, 'E', 0, 'N', 0,
    '_', 0, '_', 0,
    'x', 0, 'x', 0, 'x', 0, 'x', 0, 'x', 0, 'x', 0, 'x', 0, 'x', 0,
    'x', 0, 'x', 0, 'x', 0, 'x', 0, 'x', 0, 'x', 0, 'x', 0, 'x', 0,
};

void usb_update_serial(uint64_t newSerialNumber) {
    static bool configured = false; // TODO: enable by setting to false here...
    if (configured) {
        return;
    }

    // run this only once per boot... even if it fails to find serial number
    configured = true;
    // reject serial number if all-zero or all-ones
    if ((newSerialNumber == 0x0000000000000000) || (newSerialNumber == 0xFFFFFFFFFFFFFFFF)) {
        return;
    }

    // Descriptor is, effectively, initially identical to non-unique serial
    // number because it reports the shorter length in the first byte.
    // Convert uniqueID's eight bytes to 16 unicode characters in the
    // descriptor and, finally, update the descriptor's length, which
    // causes the serial number to become visible.
    for (uint8_t i = 0; i < 8; i++) {
        // order of nibbles chosen to match display order from `hw status`
        uint8_t nibble1 = (newSerialNumber >> ((8 * i) + 4)) & 0xFu; // bitmasks [0xF0, 0xF000, 0xF00000, ... 0xF000000000000000]
        uint8_t nibble2 = (newSerialNumber >> ((8 * i) + 0)) & 0xFu; // bitmasks [0x0F, 0x0F00, 0x0F0000, ... 0x0F00000000000000]
        char c1 = nibble1 < 10 ? '0' + nibble1 : 'A' + (nibble1 - 10);
        char c2 = nibble2 < 10 ? '0' + nibble2 : 'A' + (nibble2 - 10);
        StrSerialNumber[18 + (4 * i) + 0] = c1; // [ 18, 22, .., 42, 46 ]
        StrSerialNumber[18 + (4 * i) + 2] = c2; // [ 20, 24, .., 44, 48 ]
    }

    StrSerialNumber[0] = USB_STRING_DESCRIPTOR_SERIAL_NUMBER_LENGTH;
}
#endif


// size includes their own field.
static const char StrMS_OSDescriptor[] = {
    18,            // length 0x12
    USBDSC_STRING, // Type is string
    'M', 0, 'S', 0, 'F', 0, 'T', 0, '1', 0, '0', 0, '0', 0, MS_VENDOR_CODE, 0
};

static const char *getStringDescriptor(uint8_t idx) {
    switch (idx) {
        case 0:
            return StrLanguageCodes;
        case 1:
            return StrManufacturer;
        case 2:
            return StrProduct;
        case 3:
            return StrSerialNumber;
        case MS_OS_DESCRIPTOR_INDEX:
            return StrMS_OSDescriptor;
        default:
            return (NULL);
    }
}

// Bitmap for all status bits in CSR which must be written as 1 to cause no effect
#define REG_NO_EFFECT_1_ALL      AT91C_UDP_RX_DATA_BK0 | AT91C_UDP_RX_DATA_BK1 \
    |AT91C_UDP_STALLSENT   | AT91C_UDP_RXSETUP \
    |AT91C_UDP_TXCOMP

// Clear flags in the UDP_CSR register and waits for synchronization
#define UDP_CLEAR_EP_FLAGS(endpoint, flags) { \
        volatile unsigned int reg; \
        reg = pUdp->UDP_CSR[(endpoint)]; \
        reg |= REG_NO_EFFECT_1_ALL; \
        reg &= ~(flags); \
        pUdp->UDP_CSR[(endpoint)] = reg; \
    }

// reset flags in the UDP_CSR register and waits for synchronization
#define UDP_SET_EP_FLAGS(endpoint, flags) { \
        volatile unsigned int reg; \
        reg = pUdp->UDP_CSR[(endpoint)]; \
        reg |= REG_NO_EFFECT_1_ALL; \
        reg |= (flags); \
        pUdp->UDP_CSR[(endpoint)] = reg; \
    }


typedef struct {
    uint32_t BitRate;
    uint8_t Format;
    uint8_t ParityType;
    uint8_t DataBits;
} AT91S_CDC_LINE_CODING, *AT91PS_CDC_LINE_CODING;

static AT91S_CDC_LINE_CODING line = { // purely informative, actual values don't matter
    USART_BAUD_RATE, // baudrate
    0,               // 1 Stop Bit
    0,               // None Parity
    8                // 8 Data bits
};

// timer counts in 21.3us increments (1024/48MHz), rounding applies
// WARNING: timer can't measure more than 1.39s (21.3us * 0xffff)
static void SpinDelayUs(int us) {
    int ticks = ((MCK / 1000000) * us + 512) >> 10;

    // Borrow a PWM unit for my real-time clock
    AT91C_BASE_PWMC->PWMC_ENA = PWM_CHANNEL(0);

    // 48 MHz / 1024 gives 46.875 kHz
    AT91C_BASE_PWMC_CH0->PWMC_CMR = PWM_CH_MODE_PRESCALER(10);      // Channel Mode Register
    AT91C_BASE_PWMC_CH0->PWMC_CDTYR = 0;                            // Channel Duty Cycle Register
    AT91C_BASE_PWMC_CH0->PWMC_CPRDR = 0xffff;                       // Channel Period Register

    uint16_t start = AT91C_BASE_PWMC_CH0->PWMC_CCNTR;

    for (;;) {
        uint16_t now = AT91C_BASE_PWMC_CH0->PWMC_CCNTR;
        if (now == (uint16_t)(start + ticks))
            return;

        WDT_HIT();
    }
}

/*
 *----------------------------------------------------------------------------
 * \fn    usb_disable
 * \brief This function deactivates the USB device
 *----------------------------------------------------------------------------
*/
void usb_disable(void) {
    // Disconnect the USB device
    AT91C_BASE_PIOA->PIO_ODR = GPIO_USB_PU;

    // Clear all lingering interrupts
    if (pUdp->UDP_ISR & AT91C_UDP_ENDBUSRES) {
        pUdp->UDP_ICR = AT91C_UDP_ENDBUSRES;
    }
}

/*
 *----------------------------------------------------------------------------
 * \fn    usb_enable
 * \brief This function Activates the USB device
 *----------------------------------------------------------------------------
*/
void usb_enable(void) {
    // Set the PLL USB Divider
    AT91C_BASE_CKGR->CKGR_PLLR |= AT91C_CKGR_USBDIV_1 ;

    // Specific Chip USB Initialisation
    // Enables the 48MHz USB clock UDPCK and System Peripheral USB Clock
    AT91C_BASE_PMC->PMC_SCER |= AT91C_PMC_UDP;
    AT91C_BASE_PMC->PMC_PCER = (1 << AT91C_ID_UDP);

    AT91C_BASE_UDP->UDP_FADDR = 0;
    AT91C_BASE_UDP->UDP_GLBSTATE = 0;

    // Enable UDP PullUp (USB_DP_PUP) : enable & Clear of the corresponding PIO
    // Set in PIO mode and Configure in Output
    AT91C_BASE_PIOA->PIO_PER = GPIO_USB_PU; // Set in PIO mode
    AT91C_BASE_PIOA->PIO_OER = GPIO_USB_PU; // Configure as Output

    // Clear for set the Pullup resistor
    AT91C_BASE_PIOA->PIO_CODR = GPIO_USB_PU;

    // Disconnect and reconnect USB controller for 100ms
    usb_disable();

    SpinDelayUs(100 * 1000);

    // Reconnect USB reconnect
    AT91C_BASE_PIOA->PIO_SODR = GPIO_USB_PU;
    AT91C_BASE_PIOA->PIO_OER = GPIO_USB_PU;
}

/*
 *----------------------------------------------------------------------------
 * \fn    usb_check
 * \brief Test if the device is configured and handle enumeration
 *----------------------------------------------------------------------------
*/
static int usb_reconnect = 0;
static int usb_configured = 0;
void SetUSBreconnect(int value) {
    usb_reconnect = value;
}

int GetUSBreconnect(void) {
    return usb_reconnect;
}

void SetUSBconfigured(int value) {
    usb_configured = value;
}

int GetUSBconfigured(void) {
    return usb_configured;
}

bool usb_check(void) {

    // interrupt status register
    AT91_REG isr = pUdp->UDP_ISR;

    // end of bus reset
    if (isr & AT91C_UDP_ENDBUSRES) {
        pUdp->UDP_ICR = AT91C_UDP_ENDBUSRES;
        // reset all endpoints
        pUdp->UDP_RSTEP  = (unsigned int) - 1;
        pUdp->UDP_RSTEP  = 0;
        // Enable the function
        pUdp->UDP_FADDR = AT91C_UDP_FEN;
        // Configure endpoint 0  (enable control endpoint)
        pUdp->UDP_CSR[AT91C_EP_CONTROL] = (AT91C_UDP_EPEDS | AT91C_UDP_EPTYPE_CTRL);
    } else if (isr & AT91C_UDP_EPINT0) {
        pUdp->UDP_ICR = AT91C_UDP_EPINT0;
        AT91F_CDC_Enumerate();
    }

    return (btConfiguration) ? true : false;
}

bool usb_poll(void) {
    if (usb_check() == false) {
        return false;
    }

    return (pUdp->UDP_CSR[AT91C_EP_OUT] & btReceiveBank);
}

inline uint16_t usb_available_length(void) {
    return (((pUdp->UDP_CSR[AT91C_EP_OUT] & AT91C_UDP_RXBYTECNT) >> 16) & 0x7FF);
}

/**
    In github PR #129, some users appears to get a false positive from
    usb_poll, which returns true, but the usb_read operation
    still returns 0.
    This check is basically the same as above, but also checks
    that the length available to read is non-zero, thus hopefully fixes the
    bug.
**/
bool usb_poll_validate_length(void) {

    if (usb_check() == false) {
        return false;
    }

    if (!(pUdp->UDP_CSR[AT91C_EP_OUT] & btReceiveBank)) {
        return false;
    }

    return (((pUdp->UDP_CSR[AT91C_EP_OUT] & AT91C_UDP_RXBYTECNT) >> 16) > 0);
}

/*
 *----------------------------------------------------------------------------
 * \fn    usb_read
 * \brief Read available data from Endpoint 1 OUT (host to device)
 *----------------------------------------------------------------------------
*/
uint32_t usb_read(uint8_t *data, size_t len) {

    if (len == 0) {
        return 0;
    }

    uint8_t bank = btReceiveBank;
    uint16_t packetSize, nbBytesRcv = 0;
    uint16_t time_out = 0;

    while (len)  {
        if (usb_check() == false) {
            break;
        }

        if (pUdp->UDP_CSR[AT91C_EP_OUT] & bank) {

            packetSize = (((pUdp->UDP_CSR[AT91C_EP_OUT] & AT91C_UDP_RXBYTECNT) >> 16) & 0x7FF);
            packetSize = MIN(packetSize, len);
            len -= packetSize;

            while (packetSize--) {
                data[nbBytesRcv++] = pUdp->UDP_FDR[AT91C_EP_OUT];
            }

            // flip bank
            UDP_CLEAR_EP_FLAGS(AT91C_EP_OUT, bank)

            if (bank == AT91C_UDP_RX_DATA_BK0) {
                bank = AT91C_UDP_RX_DATA_BK1;
            } else {
                bank = AT91C_UDP_RX_DATA_BK0;
            }
        }

        if (time_out++ == 0x1FFF) {
            break;
        }
    }

    btReceiveBank = bank;
    return nbBytesRcv;
}

static uint8_t usb_read_ng_buffer[64] = {0};
static uint8_t usb_read_ng_bufoffset = 0;
static uint8_t usb_read_ng_buflen = 0;

uint32_t usb_read_ng(uint8_t *data, size_t len) {

    if (len == 0) {
        return 0;
    }

    uint8_t bank = btReceiveBank;
    uint16_t packetSize, nbBytesRcv = 0;
    uint16_t time_out = 0;

    // take first from local buffer
    if (len <= usb_read_ng_buflen) {

        // if local buffer has all data

        for (uint8_t i = 0; i < len; i++) {
            data[nbBytesRcv++] = usb_read_ng_buffer[usb_read_ng_bufoffset + i];
        }

        usb_read_ng_buflen -= len;

        if (usb_read_ng_buflen == 0) {
            usb_read_ng_bufoffset = 0;
        } else {
            usb_read_ng_bufoffset += len;
        }

        return nbBytesRcv;

    } else {

        // take all data from local buffer,  then read from usb

        for (uint8_t i = 0; i < usb_read_ng_buflen; i++) {
            data[nbBytesRcv++] = usb_read_ng_buffer[usb_read_ng_bufoffset + i];
        }

        len -= usb_read_ng_buflen;
        usb_read_ng_buflen = 0;
        usb_read_ng_bufoffset = 0;
    }


    while (len)  {

        if (usb_check() == false) {
            break;
        }

        if ((pUdp->UDP_CSR[AT91C_EP_OUT] & bank)) {

            uint16_t available = (((pUdp->UDP_CSR[AT91C_EP_OUT] & AT91C_UDP_RXBYTECNT) >> 16) & 0x7FF);

            packetSize = MIN(available, len);
            available -= packetSize;
            len -= packetSize;

            while (packetSize--) {
                data[nbBytesRcv++] = pUdp->UDP_FDR[AT91C_EP_OUT];
            }

            // fill the local buffer with the remaining bytes
            for (uint16_t i = 0; i < available; i++) {
                usb_read_ng_buffer[i] = pUdp->UDP_FDR[AT91C_EP_OUT];
            }

            // update number of available bytes in local bytes
            usb_read_ng_buflen = available;

            // flip bank
            UDP_CLEAR_EP_FLAGS(AT91C_EP_OUT, bank)

            if (bank == AT91C_UDP_RX_DATA_BK0) {
                bank = AT91C_UDP_RX_DATA_BK1;
            } else {
                bank = AT91C_UDP_RX_DATA_BK0;
            }
        }

        if (time_out++ == 0x1FFF) {
            break;
        }
    }

    btReceiveBank = bank;
    return nbBytesRcv;
}

/*
 *----------------------------------------------------------------------------
 * \fn    usb_write
 * \brief Send through endpoint 2 (device to host)
 *----------------------------------------------------------------------------
*/
int usb_write(const uint8_t *data, const size_t len) {

    if (len == 0) {
        return PM3_EINVARG;
    }

    if (usb_check() == false) {
        return PM3_EIO;
    }

    // can we write?
    if ((pUdp->UDP_CSR[AT91C_EP_IN] & AT91C_UDP_TXPKTRDY) != 0) {
        return PM3_EIO;
    }

    size_t length = len;
    uint32_t cpt = 0;

    // send first chunk
    cpt = MIN(length, AT91C_USB_EP_IN_SIZE);
    length -= cpt;
    while (cpt--) {
        pUdp->UDP_FDR[AT91C_EP_IN] = *data++;
    }

    UDP_SET_EP_FLAGS(AT91C_EP_IN, AT91C_UDP_TXPKTRDY);
    while (!(pUdp->UDP_CSR[AT91C_EP_IN] & AT91C_UDP_TXPKTRDY)) {};

    while (length) {
        // Send next chunk
        cpt = MIN(length, AT91C_USB_EP_IN_SIZE);
        length -= cpt;
        while (cpt--) {
            pUdp->UDP_FDR[AT91C_EP_IN] = *data++;
        }

        // Wait for previous chunk to be sent
        // (iceman) when is the bankswapping done?
        while (!(pUdp->UDP_CSR[AT91C_EP_IN] & AT91C_UDP_TXCOMP)) {
            if (usb_check() == false) {
                return PM3_EIO;
            }
        }

        UDP_CLEAR_EP_FLAGS(AT91C_EP_IN, AT91C_UDP_TXCOMP);
        while (pUdp->UDP_CSR[AT91C_EP_IN] & AT91C_UDP_TXCOMP) {};

        UDP_SET_EP_FLAGS(AT91C_EP_IN, AT91C_UDP_TXPKTRDY);
        while (!(pUdp->UDP_CSR[AT91C_EP_IN] & AT91C_UDP_TXPKTRDY)) {};
    }

    // Wait for the end of transfer
    while (!(pUdp->UDP_CSR[AT91C_EP_IN] & AT91C_UDP_TXCOMP)) {
        if (usb_check() == false) {
            return PM3_EIO;
        }
    }

    UDP_CLEAR_EP_FLAGS(AT91C_EP_IN, AT91C_UDP_TXCOMP);
    while (pUdp->UDP_CSR[AT91C_EP_IN] & AT91C_UDP_TXCOMP) {};


    if (len % AT91C_USB_EP_IN_SIZE == 0) {
        // like AT91F_USB_SendZlp(), in non ping-pong mode
        UDP_SET_EP_FLAGS(AT91C_EP_IN, AT91C_UDP_TXPKTRDY);
        while (!(pUdp->UDP_CSR[AT91C_EP_IN] & AT91C_UDP_TXCOMP)) {};

        UDP_CLEAR_EP_FLAGS(AT91C_EP_IN, AT91C_UDP_TXCOMP);
        while (pUdp->UDP_CSR[AT91C_EP_IN] & AT91C_UDP_TXCOMP) {};
    }

    return PM3_SUCCESS;
}

/*
 *----------------------------------------------------------------------------
 * \fn     async_usb_write_start
 * \brief  Start async write process
 * \return PM3_EIO if USB is invalid, PM3_SUCCESS if it is ready for write
 *
 * This function checks if the USB is connected, and wait until the FIFO
 * is ready to be filled.
 *
 * Warning: usb_write() should not be called between
 * async_usb_write_start() and async_usb_write_stop().
 *----------------------------------------------------------------------------
*/
int async_usb_write_start(void) {

    if (usb_check() == false) {
        return PM3_EIO;
    }

    while (pUdp->UDP_CSR[AT91C_EP_IN] & AT91C_UDP_TXPKTRDY) {
        if (usb_check() == false) {
            return PM3_EIO;
        }
    }

    isAsyncRequestFinished = false;
    return PM3_SUCCESS;
}

/*
 *----------------------------------------------------------------------------
 * \fn    async_usb_write_pushByte
 * \brief Push one byte to the FIFO of IN endpoint (time-critical)
 *
 * This function simply push a byte to the FIFO of IN endpoint.
 * The FIFO size is AT91C_USB_EP_IN_SIZE. Make sure this function is not called
 * over AT91C_USB_EP_IN_SIZE times between each async_usb_write_requestWrite().
 *----------------------------------------------------------------------------
*/
inline void async_usb_write_pushByte(uint8_t data) {
    pUdp->UDP_FDR[AT91C_EP_IN] = data;
    isAsyncRequestFinished = false;
}

/*
 *----------------------------------------------------------------------------
 * \fn     async_usb_write_requestWrite
 * \brief  Request a write operation (time-critical)
 * \return false if the last write request is not finished, true if success
 *
 * This function requests a write operation from FIFO to the USB bus,
 * and switch the internal banks of FIFO. It doesn't wait for the end of
 * transmission from FIFO to the USB bus.
 *
 * Note: This function doesn't check if the usb is valid, as it is
 * time-critical.
 *----------------------------------------------------------------------------
*/
inline bool async_usb_write_requestWrite(void) {

    // check if last request is finished
    if (pUdp->UDP_CSR[AT91C_EP_IN] & AT91C_UDP_TXPKTRDY) {
        return false;
    }

    // clear transmission completed flag
    UDP_CLEAR_EP_FLAGS(AT91C_EP_IN, AT91C_UDP_TXCOMP);
    while (pUdp->UDP_CSR[AT91C_EP_IN] & AT91C_UDP_TXCOMP) {};

    // start of transmission
    UDP_SET_EP_FLAGS(AT91C_EP_IN, AT91C_UDP_TXPKTRDY);

    // hack: no need to wait if UDP_CSR and UDP_FDR are not used immediately.
    // while (!(pUdp->UDP_CSR[AT91C_EP_IN] & AT91C_UDP_TXPKTRDY)) {};
    isAsyncRequestFinished = true;
    return true;
}

/*
 *----------------------------------------------------------------------------
 * \fn     async_usb_write_stop
 * \brief  Stop async write process
 * \return PM3_EIO if USB is invalid, PM3_SUCCESS if data is written
 *
 * This function makes sure the data left in the FIFO is written to the
 * USB bus.
 *
 * Warning: usb_write() should not be called between
 * async_usb_write_start() and async_usb_write_stop().
 *----------------------------------------------------------------------------
*/
int async_usb_write_stop(void) {
    // Wait for the end of transfer
    while (pUdp->UDP_CSR[AT91C_EP_IN] & AT91C_UDP_TXPKTRDY) {
        if (usb_check() == false) {
            return PM3_EIO;
        }
    }

    // clear transmission completed flag
    UDP_CLEAR_EP_FLAGS(AT91C_EP_IN, AT91C_UDP_TXCOMP);
    while (pUdp->UDP_CSR[AT91C_EP_IN] & AT91C_UDP_TXCOMP) {};

    // FIFO is not empty, request a write in non-ping-pong mode
    if (isAsyncRequestFinished == false) {
        UDP_SET_EP_FLAGS(AT91C_EP_IN, AT91C_UDP_TXPKTRDY);

        while (!(pUdp->UDP_CSR[AT91C_EP_IN] & AT91C_UDP_TXCOMP)) {
            if (usb_check() == false) {
                return PM3_EIO;
            }
        }

        UDP_CLEAR_EP_FLAGS(AT91C_EP_IN, AT91C_UDP_TXCOMP);
        while (pUdp->UDP_CSR[AT91C_EP_IN] & AT91C_UDP_TXCOMP) {};
    }
    return PM3_SUCCESS;
}

/*
 *----------------------------------------------------------------------------
 * \fn    AT91F_USB_SendData
 * \brief Send Data through the control endpoint
 *----------------------------------------------------------------------------
*/
void AT91F_USB_SendData(AT91PS_UDP pudp, const char *pData, uint32_t length) {
    AT91_REG csr;

    do {
        uint32_t cpt = MIN(length, AT91C_USB_EP_CONTROL_SIZE);
        length -= cpt;

        while (cpt--) {
            pudp->UDP_FDR[AT91C_EP_CONTROL] = *pData++;
        }

        if (pudp->UDP_CSR[AT91C_EP_CONTROL] & AT91C_UDP_TXCOMP) {
            UDP_CLEAR_EP_FLAGS(AT91C_EP_CONTROL, AT91C_UDP_TXCOMP);
            while (pudp->UDP_CSR[AT91C_EP_CONTROL] & AT91C_UDP_TXCOMP) {};
        }

        UDP_SET_EP_FLAGS(AT91C_EP_CONTROL, AT91C_UDP_TXPKTRDY);

        do {
            csr = pudp->UDP_CSR[AT91C_EP_CONTROL];
            // Data IN stage has been stopped by a status OUT
            if (csr & AT91C_UDP_RX_DATA_BK0) {

                UDP_CLEAR_EP_FLAGS(AT91C_EP_CONTROL, AT91C_UDP_RX_DATA_BK0)
                return;
            }
        } while (!(csr & AT91C_UDP_TXCOMP));

    } while (length);

    if (pudp->UDP_CSR[AT91C_EP_CONTROL] & AT91C_UDP_TXCOMP) {
        UDP_CLEAR_EP_FLAGS(AT91C_EP_CONTROL, AT91C_UDP_TXCOMP);
        while (pudp->UDP_CSR[AT91C_EP_CONTROL] & AT91C_UDP_TXCOMP) {};
    }
}


//*----------------------------------------------------------------------------
//* \fn    AT91F_USB_SendZlp
//* \brief Send zero length packet through the control endpoint
//*----------------------------------------------------------------------------
void AT91F_USB_SendZlp(AT91PS_UDP pudp) {
    UDP_SET_EP_FLAGS(AT91C_EP_CONTROL, AT91C_UDP_TXPKTRDY);
    // for non ping-pong operation, wait until the FIFO is released
    // the flag for FIFO released is AT91C_UDP_TXCOMP rather than AT91C_UDP_TXPKTRDY
    while (!(pudp->UDP_CSR[AT91C_EP_CONTROL] & AT91C_UDP_TXCOMP)) {};
    UDP_CLEAR_EP_FLAGS(AT91C_EP_CONTROL, AT91C_UDP_TXCOMP);
    while (pudp->UDP_CSR[AT91C_EP_CONTROL] & AT91C_UDP_TXCOMP) {};
}

//*----------------------------------------------------------------------------
//* \fn    AT91F_USB_SendStall
//* \brief Stall the control endpoint
//*----------------------------------------------------------------------------
void AT91F_USB_SendStall(AT91PS_UDP pudp) {
    UDP_SET_EP_FLAGS(AT91C_EP_CONTROL, AT91C_UDP_FORCESTALL);
    while (!(pudp->UDP_CSR[AT91C_EP_CONTROL] & AT91C_UDP_ISOERROR)) {};
    UDP_CLEAR_EP_FLAGS(AT91C_EP_CONTROL, (AT91C_UDP_FORCESTALL | AT91C_UDP_ISOERROR));
    while (pudp->UDP_CSR[AT91C_EP_CONTROL] & (AT91C_UDP_FORCESTALL | AT91C_UDP_ISOERROR)) {};
}

//*----------------------------------------------------------------------------
//* \fn    AT91F_CDC_Enumerate
//* \brief This function is a callback invoked when a SETUP packet is received
//* problem:
//* 1. this is for USB endpoint0.  the control endpoint.
//* 2. mixed with CDC ACM endpoint3 , interrupt, control endpoint
//*----------------------------------------------------------------------------
void AT91F_CDC_Enumerate(void) {
    uint8_t bmRequestType, bRequest;
    uint16_t wValue, wIndex, wLength, wStatus;

    if (!(pUdp->UDP_CSR[AT91C_EP_CONTROL] & AT91C_UDP_RXSETUP)) {
        return;
    }

    bmRequestType = pUdp->UDP_FDR[AT91C_EP_CONTROL];
    bRequest      = pUdp->UDP_FDR[AT91C_EP_CONTROL];
    wValue        = (pUdp->UDP_FDR[AT91C_EP_CONTROL] & 0xFF);
    wValue       |= (pUdp->UDP_FDR[AT91C_EP_CONTROL] << 8);
    wIndex        = (pUdp->UDP_FDR[AT91C_EP_CONTROL] & 0xFF);
    wIndex       |= (pUdp->UDP_FDR[AT91C_EP_CONTROL] << 8);
    wLength       = (pUdp->UDP_FDR[AT91C_EP_CONTROL] & 0xFF);
    wLength      |= (pUdp->UDP_FDR[AT91C_EP_CONTROL] << 8);

    if (bmRequestType & 0x80) {        // Data Phase Transfer Direction Device to Host
        UDP_SET_EP_FLAGS(AT91C_EP_CONTROL, AT91C_UDP_DIR);
        while (!(pUdp->UDP_CSR[AT91C_EP_CONTROL] & AT91C_UDP_DIR)) {};
    }
    UDP_CLEAR_EP_FLAGS(AT91C_EP_CONTROL, AT91C_UDP_RXSETUP);
    while ((pUdp->UDP_CSR[AT91C_EP_CONTROL] & AT91C_UDP_RXSETUP)) {};

    // Handle supported standard device request Cf Table 9-3 in USB specification Rev 1.1
    switch ((bRequest << 8) | bmRequestType) {
        case USBGET_DESCRIPTOR: {

            if (wValue == 0x100) {        // Return Device Descriptor
                AT91F_USB_SendData(pUdp, devDescriptor, MIN(sizeof(devDescriptor), wLength));
            } else if (wValue == 0x200) {   // Return Configuration Descriptor
                AT91F_USB_SendData(pUdp, cfgDescriptor, MIN(sizeof(cfgDescriptor), wLength));
            } else if ((wValue & 0xF00) == 0xF00) { // Return BOS Descriptor
                AT91F_USB_SendData(pUdp, bosDescriptor, MIN(sizeof(bosDescriptor), wLength));
            } else if ((wValue & 0x300) == 0x300) {  // Return String Descriptor

                const char *strDescriptor = getStringDescriptor(wValue & 0xff);
                if (strDescriptor != NULL) {
                    AT91F_USB_SendData(pUdp, strDescriptor, MIN(strDescriptor[0], wLength));
                } else {
                    AT91F_USB_SendStall(pUdp);
                }
            } else {
                AT91F_USB_SendStall(pUdp);
            }
        }
        break;
        case USBSET_ADDRESS:
            AT91F_USB_SendZlp(pUdp);
            pUdp->UDP_FADDR = (AT91C_UDP_FEN | (wValue & 0x7F));
            pUdp->UDP_GLBSTATE  = (wValue) ? AT91C_UDP_FADDEN : 0;
            break;
        case USBSET_CONFIG:

            /*
            *   Set or clear the device "configured" state.
            *   The LSB of wValue is the "Configuration Number". If this value is non-zero,
            *   it should be the same number as defined in the Configuration Descriptor;
            *   otherwise an error must have occurred.
            *   This device has only one configuration and its Config Number is CONF_NB (= 1).
            */
            AT91F_USB_SendZlp(pUdp);
            btConfiguration = wValue;
            pUdp->UDP_GLBSTATE  = (wValue) ? AT91C_UDP_CONFG : AT91C_UDP_FADDEN;

            // enable endpoints
            pUdp->UDP_CSR[AT91C_EP_OUT]    = (wValue) ? (AT91C_UDP_EPEDS | AT91C_UDP_EPTYPE_BULK_OUT) : 0;
            pUdp->UDP_CSR[AT91C_EP_IN]     = (wValue) ? (AT91C_UDP_EPEDS | AT91C_UDP_EPTYPE_BULK_IN)  : 0;
            pUdp->UDP_CSR[AT91C_EP_NOTIFY] = (wValue) ? (AT91C_UDP_EPEDS | AT91C_UDP_EPTYPE_INT_IN)   : 0;
            break;
        case USBGET_CONFIG:
            AT91F_USB_SendData(pUdp, (char *) & (btConfiguration), sizeof(btConfiguration));
            break;
        case USBGET_STATUS_ZERO:
            wStatus = 0;   // Device is Bus powered, remote wakeup disabled
            AT91F_USB_SendData(pUdp, (char *) &wStatus, sizeof(wStatus));
            break;
        case USBGET_STATUS_INTERFACE:
            wStatus = 0;   // reserved for future use
            AT91F_USB_SendData(pUdp, (char *) &wStatus, sizeof(wStatus));
            break;
        case USBGET_STATUS_ENDPOINT:
            wStatus = 0;
            wIndex &= 0x0F;
            if ((pUdp->UDP_GLBSTATE & AT91C_UDP_CONFG) && (wIndex <= AT91C_EP_NOTIFY)) {
                wStatus = (pUdp->UDP_CSR[wIndex] & AT91C_UDP_EPEDS) ? 0 : 1;
                AT91F_USB_SendData(pUdp, (char *) &wStatus, sizeof(wStatus));
            } else if ((pUdp->UDP_GLBSTATE & AT91C_UDP_FADDEN) && (wIndex == AT91C_EP_CONTROL)) {
                wStatus = (pUdp->UDP_CSR[wIndex] & AT91C_UDP_EPEDS) ? 0 : 1;
                AT91F_USB_SendData(pUdp, (char *) &wStatus, sizeof(wStatus));
            } else {
                AT91F_USB_SendStall(pUdp);
            }
            break;
        case USBSET_FEATURE_ZERO:
            AT91F_USB_SendStall(pUdp);
            break;
        case USBSET_FEATURE_INTERFACE:
            AT91F_USB_SendZlp(pUdp);
            break;
        case USBSET_FEATURE_ENDPOINT:
            wIndex &= 0x0F;
            if ((wValue == 0) && (wIndex >= AT91C_EP_OUT) && (wIndex <= AT91C_EP_NOTIFY)) {
                pUdp->UDP_CSR[wIndex] = 0;
                AT91F_USB_SendZlp(pUdp);
            } else {
                AT91F_USB_SendStall(pUdp);
            }
            break;
        case USBCLR_FEATURE_ZERO:
            AT91F_USB_SendStall(pUdp);
            break;
        case USBCLR_FEATURE_INTERFACE:
            AT91F_USB_SendZlp(pUdp);
            break;
        case USBCLR_FEATURE_ENDPOINT:
            wIndex &= 0x0F;
            if ((wValue == 0) && (wIndex >= AT91C_EP_OUT) && (wIndex <= AT91C_EP_NOTIFY)) {

                if (wIndex == AT91C_EP_OUT) {
                    pUdp->UDP_CSR[AT91C_EP_OUT] = (AT91C_UDP_EPEDS | AT91C_UDP_EPTYPE_BULK_OUT);
                } else if (wIndex == AT91C_EP_IN) {
                    pUdp->UDP_CSR[AT91C_EP_IN] = (AT91C_UDP_EPEDS | AT91C_UDP_EPTYPE_BULK_IN);
                } else if (wIndex == AT91C_EP_NOTIFY) {
                    pUdp->UDP_CSR[AT91C_EP_NOTIFY] = (AT91C_UDP_EPEDS | AT91C_UDP_EPTYPE_INT_IN);
                }

                AT91F_USB_SendZlp(pUdp);
            } else {
                AT91F_USB_SendStall(pUdp);
            }
            break;

        // handle CDC class requests
        case USBSET_CDC_LINE_CODING: {
            // ignore SET_LINE_CODING...
            while (!(pUdp->UDP_CSR[AT91C_EP_CONTROL] & AT91C_UDP_RX_DATA_BK0)) {};
            UDP_CLEAR_EP_FLAGS(AT91C_EP_CONTROL, AT91C_UDP_RX_DATA_BK0);
            AT91F_USB_SendZlp(pUdp);
            break;
        }
        case USBGET_CDC_LINE_CODING:
            AT91F_USB_SendData(pUdp, (char *) &line, MIN(sizeof(line), wLength));
            break;
        case USBSET_CDC_CTRL_LINE_STATE:
            btConnection = wValue;
            AT91F_USB_SendZlp(pUdp);
            break;
        default:
            AT91F_USB_SendStall(pUdp);
            break;
    }
}
