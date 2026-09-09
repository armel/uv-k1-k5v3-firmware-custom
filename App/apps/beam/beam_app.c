/* Copyright 2026 Armel F4HWN
 * Licensed under the Apache License, Version 2.0.
 *
 * BEAM overlay app.  Packet version 2 remains wire-compatible with the
 * resident implementation: one selected VFO is sent and a received VFO is
 * stored in the first free memory channel.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../app_api.h"

#define PACKET_MAGIC   0xBEA5u
#define PACKET_VERSION 2u
#define PACKET_START   0xABCDu
#define PACKET_END     0xDCBAu
#define PACKET_WORDS   36u

enum {
    STATUS_READY = 0,
    STATUS_TX_WAIT,
    STATUS_TX_DONE,
    STATUS_RX_WAIT,
    STATUS_RX_SAVED,
    STATUS_RX_FULL,
    STATUS_ERROR,
};

typedef struct {
    uint16_t magic;
    uint8_t version;
    uint8_t pad;
    app_beam_channel_t channel;
} beam_payload_t;

_Static_assert(sizeof(beam_payload_t) == 48u,
               "BEAM v2 wire payload layout changed");

static const uint16_t OBFUSCATION[8] = {
    0x6C16, 0xE614, 0x912E, 0x400D, 0x3521, 0x40D5, 0x0313, 0x80E9
};

static const app_api_t *A;
static uint32_t packet_store[18]; /* 72 bytes, 4-byte aligned for the payload */
static uint16_t copiedChannel;
static uint8_t mode, status;
static bool receiving, running, dirty;

static uint16_t *packet(void) { return (uint16_t *)packet_store; }
static beam_payload_t *payload(void) { return (beam_payload_t *)&packet()[2]; }

static void clear_bytes(void *ptr, uint8_t count)
{
    uint8_t *p = (uint8_t *)ptr;
    while (count--) *p++ = 0;
}

static uint16_t crc16(const void *buffer, uint16_t size)
{
    const uint8_t *data = (const uint8_t *)buffer;
    uint16_t crc = 0;
    while (size--) {
        crc ^= (uint16_t)*data++ << 8;
        for (uint8_t bit = 0; bit < 8u; bit++)
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
    }
    return crc;
}

static void obfuscate(void)
{
    uint16_t *p = packet();
    for (uint8_t i = 0; i < 32u; i++)
        p[i + 1u] ^= OBFUSCATION[i & 7u];
}

static void draw(void)
{
    const char *state;
    switch (status) {
        case STATUS_TX_WAIT:  state = "SENDING";  break;
        case STATUS_TX_DONE:  state = "SENT";     break;
        case STATUS_RX_WAIT:  state = "WAITING";  break;
        case STATUS_RX_SAVED: state = "RECEIVED"; break;
        case STATUS_RX_FULL:  state = "MEM FULL"; break;
        case STATUS_ERROR:    state = "ERROR";    break;
        default:              state = mode ? "BEAM RX" : "BEAM TX"; break;
    }
    A->beam_draw(state);
    A->blit_status();
    A->blit_full();
}

static void stop_rx(void)
{
    if (receiving) {
        A->beam_rx(false);
        receiving = false;
    }
}

static void send_packet(void)
{
    uint16_t *p = packet();
    clear_bytes(packet_store, sizeof(packet_store));
    p[0] = PACKET_START;
    payload()->magic = PACKET_MAGIC;
    payload()->version = PACKET_VERSION;
    A->beam_get(&payload()->channel);
    p[34] = crc16(&p[1], 2u + 64u);
    p[35] = PACKET_END;
    obfuscate();

    status = STATUS_TX_WAIT;
    draw();
    A->beam_send(p);
    status = STATUS_TX_DONE;
    for (uint8_t i = 0; i < 3u; i++) {
        A->play_tone(880, 60);
        if (i < 2u) A->delay_ms(20);
    }
    dirty = true;
}

static void start(void)
{
    stop_rx();
    /* A channel save is committed only after app_main() returns.  Do not let a
       second RX overwrite that pending save; exit and relaunch to receive more. */
    if (mode && copiedChannel != 0xFFFFu)
        return;
    A->beam_prepare();
    if (!mode) {
        send_packet();
        return;
    }
    A->beam_rx(true);
    receiving = true;
    status = STATUS_RX_WAIT;
    dirty = true;
}

static bool valid_packet(void)
{
    uint16_t *p = packet();
    if (p[0] != PACKET_START || p[35] != PACKET_END)
        return false;
    obfuscate();
    if (p[34] != crc16(&p[1], 2u + 64u))
        return false;
    return payload()->magic == PACKET_MAGIC && payload()->version == PACKET_VERSION;
}

static void poll_rx(void)
{
    if (!receiving)
        return;
    const uint8_t result = A->beam_rx_poll(packet());
    if (result == APP_BEAM_RX_WAIT)
        return;
    if (result == APP_BEAM_RX_ERROR || !valid_packet()) {
        status = STATUS_ERROR;
        A->backlight_on();
        dirty = true;
        return;
    }

    copiedChannel = A->beam_save(&payload()->channel);
    A->beam_rx(false);
    receiving = false;
    status = copiedChannel == 0xFFFFu ? STATUS_RX_FULL : STATUS_RX_SAVED;
    A->backlight_on();
    dirty = true;
}

static void key_press(uint8_t key)
{
    switch (key) {
        case APP_KEY_UP:
        case APP_KEY_DOWN:
            stop_rx();
            mode ^= 1u;
            status = STATUS_READY;
            dirty = true;
            break;
        case APP_KEY_MENU:
            start();
            break;
        case APP_KEY_EXIT:
            running = false;
            break;
        case APP_KEY_PTT:
            break;
        default:
            A->play_tone(500, 60);
            break;
    }
}

__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api)
{
    A = api;
    mode = 0;
    status = STATUS_READY;
    copiedChannel = 0xFFFFu;
    receiving = false;
    running = true;
    dirty = true;
    A->backlight_on();

    uint8_t previous = APP_KEY_INVALID;
    uint8_t batteryTicks = 0;
    while (running) {
        const uint8_t key = A->get_key();
        if (key == APP_KEY_SAVER) {
            previous = APP_KEY_INVALID;
        } else if (key == APP_KEY_WAKE) {
            previous = APP_KEY_INVALID;
            dirty = true;
        } else {
            if (key != previous && key != APP_KEY_INVALID) {
                A->backlight_on();
                key_press(key);
            }
            previous = key;
        }

        poll_rx();
        if (dirty) { draw(); dirty = false; }
        A->delay_ms(10);
        A->backlight_update();
        if (++batteryTicks >= 50u) {
            batteryTicks = 0;
            A->battery_sample();
            dirty = true;
        }
    }

    stop_rx();
    A->beam_leave();
}
