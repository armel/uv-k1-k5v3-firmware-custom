/* Copyright 2025 bg7nzl
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef APP_DIGMODE_H
#define APP_DIGMODE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Digital-mode UART protocol (PC <-> radio)
 *
 * Frame: SYNC(0xAB) CMD(1B) LEN(1B) PAYLOAD(0..N) CRC8(1B)
 * CRC8 = XOR of all bytes from SYNC to last payload byte
 *
 * This is distinct from the stock Quansheng protocol which uses
 * header 0xAB 0xCD - our CMD byte will never be 0xCD.
 */

#define DIGMODE_SYNC   0xAB

#define DIGMODE_CMD_START_TX   0x01   // PC->radio: enter TX (payload: base_freq 4B [+ power 1B])
#define DIGMODE_CMD_STOP_TX    0x02   // PC->radio: stop TX
#define DIGMODE_CMD_SET_FREQ   0x03   // PC->radio: freq(2B)*5 + apply_at(4B)
#define DIGMODE_CMD_STATUS     0x04   // PC->radio: request status
#define DIGMODE_CMD_ACK        0x05   // radio->PC: ack/nak (payload: cmd 1B + result×5)
#define DIGMODE_CMD_SYNC_REQ   0x06   // PC->radio: NTP time sync request
#define DIGMODE_CMD_SYNC_RESP  0x07   // radio->PC: NTP time sync response
#define DIGMODE_CMD_NOOP       0x08   // PC->radio: heartbeat (no payload)
#define DIGMODE_CMD_SCHED_TX   0x09   // PC->radio: scheduled TX (base 4B + interval 4B + power 1B + freq[] 2B×N)
#define DIGMODE_CMD_SCHED_APP  0x0A   // PC->radio: append entries to schedule (freq[] 2B×N)

#define DIGMODE_MAX_CMD        0x0A

#define DIGMODE_RESULT_OK      0x00
#define DIGMODE_RESULT_ERR     0x01

#define DIGMODE_FREQ_COPIES    5
#define DIGMODE_FIFO_SIZE      8
#define DIGMODE_MAX_CRC_FAILS  10
#define DIGMODE_HEARTBEAT_MS   1000   // link-loss timeout
#define DIGMODE_SCHED_MAX      256    // max entries in schedule buffer

/* Persistent mode flag: true from first UART activation until reboot */
extern volatile bool gDigmodeEntered;

/* TX-active flag: true only while PA is on */
extern volatile bool gDigmodeTxActive;

/* Display state — read by ui/digmode.c */
typedef struct {
    uint32_t base_freq;        // base TX frequency (10 Hz units)
    uint16_t cur_audio_dhz;    // current audio offset (0.1 Hz units)
    uint32_t cur_rf_freq;      // actual RF output (10 Hz units)
    uint8_t  fifo_depth;       // entries in scheduling FIFO / remaining schedule
    uint8_t  crc_fail_count;   // recent CRC failures
    bool     tx_active;
    bool     sched_waiting;    // waiting for start_at
    uint32_t countdown_ms;     // ms until scheduled TX start
} DigmodeDisplayState_t;

extern DigmodeDisplayState_t gDigmodeDisplay;

/*
 * Try to parse a digital-mode frame from the UART/DMA buffer.
 * Returns bytes consumed (>0), 0 if need more data, 1 to skip.
 */
uint16_t DIGMODE_ProcessByte(const uint8_t *buf, uint16_t available,
                             uint16_t buf_size, uint16_t start_idx);

/*
 * Called every 10ms from the main loop to service the scheduling FIFO.
 */
void DIGMODE_Poll(void);

#endif
