/* SPDX-License-Identifier: Apache-2.0 */
#ifndef APRSTX_PROTOCOL_H
#define APRSTX_PROTOCOL_H
#include <stdbool.h>
#include <stdint.h>
#define APRS_FRAME_CAP 128u
typedef struct {
    int32_t lat, lon; /* hundredths of arcminutes, signed N/E */
    char call[7];
    uint8_t ssid, path, table, symbol, valid;
} aprs_model_t;
typedef struct {
    const uint8_t *frame;
    uint16_t length, index, flags;
    uint8_t stage, bit, ones, zero, tail;
} aprs_hdlc_t;
bool aprs_call_valid(const char *call);
bool aprs_model_valid(const aprs_model_t *m);
void aprs_defaults(aprs_model_t *m);
bool aprs_cfg_decode16(const uint8_t raw[16], aprs_model_t *m);
void aprs_cfg_encode16(const aprs_model_t *m, uint8_t raw[16]);
uint16_t aprs_crc16(const uint8_t *bytes, uint16_t length);
/* Returns zero on error, including insufficient capacity or invalid config. */
uint16_t aprs_frame_build(const aprs_model_t *m, const char *comment,
                          uint8_t frame[APRS_FRAME_CAP]);
/* Fixed width DDMM.hhN / DDDMM.hhE; caller reserves 10 bytes. */
void aprs_coordinate(int32_t value, bool longitude, char *out);
void aprs_number(uint32_t value, char *out, uint8_t digits);
/* Small bounded values outside the modem loop; avoids a division runtime. */
uint32_t aprs_divmod(uint32_t *value, uint32_t divisor);
void aprs_hdlc_init(aprs_hdlc_t *s, const uint8_t *frame, uint16_t length,
                    uint16_t preamble, uint8_t tail);
bool aprs_hdlc_next(aprs_hdlc_t *s, uint8_t *bit);
#endif
