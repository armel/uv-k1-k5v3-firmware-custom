/* ALERT (ERTS) telemetry frame decoder.
 *
 * Portable C, no dependencies, host-testable (tools/test_alert_decode.c).
 * Bit layouts follow the MegaNet codec (packets.js FORMATS table) and the BoM
 * "ERTS Data Formats" spec: four asynchronous 10-bit words (start + 8 data
 * bits LSB-first + stop), 300 baud, negative logic in Australian field
 * hardware (start = 1, stop = 0), carrying a 13-bit address and 11-bit value.
 *
 * Supported wire formats:
 *   ABF  ALERT Binary Format         check bits 10 / 10 / 11 / 11, no CRC
 *   EIF  Enhanced IFLOWS Format      check bits 11, 6-bit FCS x^6+x^4+x^3+1
 *   A2C  ALERT2 concentration record 4 packed bytes (no framing) - for
 *        payloads already delivered as bytes, e.g. an ALERT2 IND 0x74 element
 */
#ifndef APP_ALERT_DECODE_H
#define APP_ALERT_DECODE_H

#include <stdbool.h>
#include <stdint.h>

enum {
	ALERT_FMT_NONE = 0,
	ALERT_FMT_ABF  = 1,
	ALERT_FMT_EIF  = 2,
	ALERT_FMT_A2C  = 3,
};

enum {
	ALERT_POL_STANDARD = 0,   // start = 0, stop = 1 (idle high)
	ALERT_POL_NEGATIVE = 1,   // start = 1, stop = 0 (idle low) - ALERT field hardware
};

#define ALERT_VALUE_FULL_SCALE 2047u   // 11 bits all set: over-range or dead sensor

typedef struct {
	uint16_t id;        // 13-bit ALERT address
	uint16_t value;     // 11-bit reading
	uint8_t  format;    // ALERT_FMT_*
	uint8_t  polarity;  // ALERT_POL_* (async formats only)
	uint16_t bit_pos;   // bit offset of the frame's first start bit in the scanned buffer
} AlertReading_t;

// Bit accessor: bit n of a buffer, MSB of byte 0 first (the order the BK4819
// FSK FIFO delivers bits when read as 16-bit words, low byte first).
static inline uint8_t ALERT_GetBit(const uint8_t *buf, uint32_t n)
{
	return (buf[n >> 3] >> (7u - (n & 7u))) & 1u;
}

// CRC-6, generator x^6 + x^4 + x^3 + 1, no init, no reflection, no final xor,
// fed MSB first. Identical to packets.js crc6().
uint8_t ALERT_Crc6(uint32_t bits, uint8_t nbits);

// Decode a 32-bit payload (bit 0 = first transmitted data bit of word 1, i.e.
// the MegaNet "bits32" string read left to right with index 0 = MSB of `payload`).
// Returns the format found, or ALERT_FMT_NONE. Fills *out on success.
uint8_t ALERT_DecodePayload32(uint32_t payload, AlertReading_t *out);

// Decode one ALERT2 concentration record (4 bytes: addr lo, DDDAAAAA, data lo, status).
bool ALERT_DecodeA2C(const uint8_t b[4], AlertReading_t *out);

// Scan a captured bitstream for async ALERT frames.
//
// Emulates a UART: waits for a start bit (a bit that differs from the idle
// level), takes 10 bits, checks the stop bit, slips one bit on a framing
// error. Every run of four consecutive good words (no more than `max_gap`
// idle bits between them) is tried against ABF and EIF. Both polarities are
// tried when `polarity` is ALERT_POL_ANY.
//
// Returns the number of readings written to `out` (at most `max_out`).
#define ALERT_POL_ANY 2
int ALERT_ScanBits(const uint8_t *buf, uint32_t nbits, uint8_t polarity,
                   uint8_t max_gap, AlertReading_t *out, int max_out);

#endif
