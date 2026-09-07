/* ALERT telemetry receiver app - see alert.h for the overview.
 *
 * Copyright 2026 cdomotor-g. Apache-2.0, like the egzumer base it lives in.
 */
#ifdef ENABLE_ALERT

#include <string.h>

#include "app/alert.h"
#include "app/alert_decode.h"
#include "app/alert_stations.h"
#include "audio.h"
#include "driver/backlight.h"
#include "driver/bk4819.h"
#include "driver/keyboard.h"
#include "driver/gpio.h"
#include "driver/st7565.h"
#include "driver/system.h"
#include "external/printf/printf.h"
#include "functions.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "ui/helper.h"
#include "ui/ui.h"
#ifdef ENABLE_UART
	#include "driver/uart.h"
#endif
#ifdef ENABLE_USB
	#include "driver/vcp.h"
#endif
#ifdef ENABLE_UART
#endif
#ifdef ENABLE_ALERT_ADC
	#include "ARMCM0.h"
	#include "board.h"
	#include "bsp/dp32g030/portcon.h"
	#include "bsp/dp32g030/saradc.h"
	#include "bsp/dp32g030/syscon.h"
	#include "driver/adc.h"
#endif

// ---------------------------------------------------------------------------
// configuration (persisted in gEeprom.ALERT_CFG[5], see settings.c)

enum { INPUT_MODEM = 0, INPUT_ADC = 1 };
enum { MODE_FFSK_1200_1800 = 0, MODE_FFSK_1200_2400, MODE_SAME, MODE_DIRECT, MODE_N };
enum { SYNC_0000 = 0, SYNC_FFFF, SYNC_AAAA, SYNC_5555, SYNC_N };

static struct {
	uint8_t  input;        // INPUT_*
	uint8_t  polarity;     // ALERT_POL_NEGATIVE / STANDARD / ANY
	bool     voice;        // read new readings out loud
	bool     gate;         // only accept captures made while the squelch was open
	bool     show_unknown; // show addresses that are not in the station table
	bool     bitrev;       // reverse the bit order inside each modem byte
	bool     monitor;      // leave the audio path open (listen to the bursts)
	uint8_t  mode;         // MODE_*  (modem input)
	uint8_t  sync;         // SYNC_*  (modem input)
	bool     invert;       // invert modem RX data
	bool     sync4;        // 4-byte sync word instead of 2
	uint8_t  rxgain;       // 0..3 modem RX gain
	uint16_t baud;         // 200 .. 1200
	uint8_t  pktlen;       // modem capture length, bytes
	bool     confirm;      // require the same reading twice in one burst
} cfg;

#define CFG_MAGIC 0xA0u

void ALERT_LoadConfig(void)
{
	const uint8_t *b = gEeprom.ALERT_CFG;
	if ((b[4] & 0xF0u) != CFG_MAGIC) {
		cfg.input = INPUT_MODEM; cfg.polarity = ALERT_POL_NEGATIVE;
		cfg.voice = true;  cfg.gate = true;  cfg.show_unknown = true;
		cfg.bitrev = false; cfg.monitor = false;
		cfg.mode = MODE_FFSK_1200_1800; cfg.sync = SYNC_0000; cfg.invert = false; cfg.sync4 = false;
		cfg.rxgain = 3; cfg.baud = 300; cfg.pktlen = 96; cfg.confirm = false;
		return;
	}
	cfg.input        = b[0] & 1u;
	cfg.polarity     = (b[0] >> 1) & 3u;
	cfg.voice        = b[0] & (1u << 3);
	cfg.gate         = b[0] & (1u << 4);
	cfg.show_unknown = b[0] & (1u << 5);
	cfg.bitrev       = b[0] & (1u << 6);
	cfg.monitor      = b[0] & (1u << 7);
	cfg.mode         = b[1] & 3u;
	cfg.sync         = (b[1] >> 2) & 3u;
	cfg.invert       = b[1] & (1u << 4);
	cfg.sync4        = b[1] & (1u << 5);
	cfg.rxgain       = (b[1] >> 6) & 3u;
	cfg.baud         = (uint16_t)b[2] * 10u;
	cfg.pktlen       = b[3];
	cfg.confirm      = b[4] & 1u;
	if (cfg.polarity > ALERT_POL_ANY) cfg.polarity = ALERT_POL_NEGATIVE;
	if (cfg.baud < 200 || cfg.baud > 1200) cfg.baud = 300;
	if (cfg.pktlen < 16) cfg.pktlen = 96;
#ifndef ENABLE_ALERT_ADC
	cfg.input = INPUT_MODEM;
#endif
}

void ALERT_StoreConfig(void)
{
	uint8_t *b = gEeprom.ALERT_CFG;
	b[0] = (cfg.input & 1u) | ((cfg.polarity & 3u) << 1) | (cfg.voice << 3) | (cfg.gate << 4)
	     | (cfg.show_unknown << 5) | (cfg.bitrev << 6) | (cfg.monitor << 7);
	b[1] = (cfg.mode & 3u) | ((cfg.sync & 3u) << 2) | (cfg.invert << 4) | (cfg.sync4 << 5) | ((cfg.rxgain & 3u) << 6);
	b[2] = (uint8_t)(cfg.baud / 10u);
	b[3] = cfg.pktlen;
	b[4] = CFG_MAGIC | (cfg.confirm ? 1u : 0u);
}

// ---------------------------------------------------------------------------
// state

typedef struct {
	uint16_t id;
	uint16_t value;
	uint8_t  kind;
	uint8_t  format;
	int8_t   rssi_dbm;
	uint8_t  seq;        // burst sequence number (readings from one burst share it)
} History_t;

#define HISTORY_N 6
static History_t history[HISTORY_N];
static uint8_t   historyCount;
static uint8_t   burstSeq;

static struct {
	uint16_t syncs;      // modem sync words seen / ADC bursts seen
	uint16_t frames;     // valid frames decoded
	uint16_t gated;      // captures dropped because the squelch was closed
	uint16_t unknown;    // frames whose address is not in the table
} stats;

// capture buffer shared by both inputs
static uint8_t  capBuf[256];
static uint16_t capLen;          // bytes (modem) or bits (ADC)

// On-screen diagnostics. The serial log cannot be read while the app is
// running - APP_RunAlert blocks APP_Update, which is what services the USB
// command handler - so the only reliable channel back from inside this app is
// the screen itself.
static uint16_t dbgKeyCount;     // key transitions seen from KEYBOARD_Poll
static int16_t  dbgLastKey = -1; // last key code seen
static uint16_t dbgIrqCount;     // times REG_0C reported an interrupt pending
static uint16_t dbgIrqBits;      // last REG_02 word
static bool     capturing;
static bool     capGate;         // squelch was open at some point during the capture
static int16_t  capRssi;

static bool     sqOpen;
static int16_t  rssiDbm;
static bool     running;
static bool     redraw;
static uint8_t  view;            // VIEW_*
static uint8_t  setIndex;        // selected settings row
static uint8_t  lastCap[8];      // first bytes of the last capture, for the raw view
static uint16_t lastCapLen;
static uint16_t tick;
static bool     voiceBusy;

enum { VIEW_MAIN = 0, VIEW_SETTINGS, VIEW_RAW };

// ---------------------------------------------------------------------------
// BK4819 FSK engine as a bit slicer (MODEM input)

// same scaling as scale_freq() in driver/bk4819.c: register units for TONE2
static uint16_t tone_reg(uint16_t hz)
{
	return (uint16_t)((((uint32_t)hz * 1353245u) + (1u << 16)) >> 17);
}

// Debug telemetry over the USB serial port. The decoder already logs decoded
// packets this way; this reports the state needed to tell "keys never arrive"
// from "keys arrive but exit is broken", and "no RF event" from "RF event but
// no decode". Costs nothing when ENABLE_UART is off.

static void ModemStop(void)
{
	BK4819_WriteRegister(BK4819_REG_59, (1u << 14) | (1u << 15));   // clear FIFOs, RX/TX off
	BK4819_WriteRegister(BK4819_REG_58, 0);                          // FSK disable
	BK4819_WriteRegister(BK4819_REG_70, 0);
	capturing = false;
}

static void ModemArm(void)
{
	uint16_t reg58;

	// TONE2 = FSK bit clock; enable it with a healthy gain
	BK4819_WriteRegister(BK4819_REG_70, (1u << 7) | (96u << 0));
	BK4819_WriteRegister(BK4819_REG_72, tone_reg(cfg.baud));

	// REG_58: <15:13> TX mode, <12:10> RX mode, <9:8> RX gain, <5:4> preamble
	//         type, <3:1> RX bandwidth, <0> enable   (Beken register list)
	switch (cfg.mode) {
		default:
		case MODE_FFSK_1200_1800: reg58 = (1u << 13) | (7u << 10) | (1u << 1); break;
		case MODE_FFSK_1200_2400: reg58 = (3u << 13) | (4u << 10) | (4u << 1); break;
		case MODE_SAME:           reg58 = (5u << 13) | (0u << 10) | (2u << 1); break;
		case MODE_DIRECT:         reg58 = (0u << 13) | (0u << 10) | (0u << 1); break;
	}
	reg58 |= ((uint16_t)cfg.rxgain << 8) | 1u;
	BK4819_WriteRegister(BK4819_REG_58, reg58);

	// sync word = the idle pattern the transmitter sends before the data, so
	// the engine starts capturing at the start of the preamble
	{
		static const uint16_t syncWords[SYNC_N] = { 0x0000, 0xFFFF, 0xAAAA, 0x5555 };
		const uint16_t sw = syncWords[cfg.sync];
		BK4819_WriteRegister(BK4819_REG_5A, sw);
		BK4819_WriteRegister(BK4819_REG_5B, sw);
	}

	BK4819_WriteRegister(BK4819_REG_5C, 0x5625);                       // CRC off
	BK4819_WriteRegister(BK4819_REG_5D, (uint16_t)cfg.pktlen << 8);    // capture length
	BK4819_WriteRegister(BK4819_REG_5E, (64u << 3) | (4u << 0));       // RX FIFO almost-full at 4 words

	// REG_59: <15> clr TX FIFO, <14> clr RX FIFO, <13> scramble, <12> RX en,
	//         <10> invert RX, <7:4> preamble length, <3> 4-byte sync
	{
		const uint16_t base = ((uint16_t)cfg.invert << 10) | ((uint16_t)cfg.sync4 << 3);
		BK4819_WriteRegister(BK4819_REG_59, base | (1u << 14) | (1u << 15));
		BK4819_WriteRegister(BK4819_REG_59, base);
		BK4819_WriteRegister(BK4819_REG_59, base | (1u << 12));
	}

	BK4819_WriteRegister(BK4819_REG_3F,
		BK4819_REG_3F_FSK_RX_SYNC | BK4819_REG_3F_FSK_FIFO_ALMOST_FULL | BK4819_REG_3F_FSK_RX_FINISHED |
		BK4819_REG_3F_SQUELCH_FOUND | BK4819_REG_3F_SQUELCH_LOST);
	BK4819_WriteRegister(BK4819_REG_02, 0);

	capturing = false;
	capLen    = 0;
}

static void ModemReadWords(unsigned words)
{
	while (words--) {
		const uint16_t w = BK4819_ReadRegister(BK4819_REG_5F);
		if (capLen < sizeof(capBuf)) capBuf[capLen++] = (uint8_t)(w & 0xff);
		if (capLen < sizeof(capBuf)) capBuf[capLen++] = (uint8_t)(w >> 8);
	}
}

static void ProcessCapture(const uint8_t *buf, uint32_t nbits, bool gated, int16_t rssi);

static void ModemPoll(void)
{
	// squelch state straight from the chip: REG_0C<1> 1 = open
	sqOpen = (BK4819_ReadRegister(BK4819_REG_0C) & 2u) != 0;
	if (capturing && sqOpen)
		capGate = true;

	// Bounded. This used to be "while (REG_0C & 1)", which assumes writing 0 to
	// REG_02 always clears the pending flag. If it does not, the loop never
	// exits: keys are never polled, PTT is never read, the screen never
	// redraws, and the only way out is a power cycle - exactly what was seen on
	// hardware, with the display frozen on its very first frame.
	for (uint8_t guard = 0; guard < 16 && (BK4819_ReadRegister(BK4819_REG_0C) & 1u); guard++) {
		BK4819_WriteRegister(BK4819_REG_02, 0);
		const uint16_t irq = BK4819_ReadRegister(BK4819_REG_02);
		dbgIrqCount++;
		dbgIrqBits = irq;

		if (irq & BK4819_REG_02_FSK_RX_SYNC) {
			capturing = true;
			capLen    = 0;
			capGate   = sqOpen;
			capRssi   = rssiDbm;
			stats.syncs++;
			redraw = true;
		}
		if ((irq & BK4819_REG_02_FSK_FIFO_ALMOST_FULL) && capturing)
			ModemReadWords(4);

		if (irq & BK4819_REG_02_FSK_RX_FINISHED) {
			if (capturing) {
				if (capLen < cfg.pktlen)
					ModemReadWords((cfg.pktlen - capLen + 1u) / 2u);
				capturing = false;
				if (cfg.bitrev) {
					for (uint16_t i = 0; i < capLen; i++) {
						uint8_t b = capBuf[i];
						b = (uint8_t)(((b & 0x55u) << 1) | ((b & 0xAAu) >> 1));
						b = (uint8_t)(((b & 0x33u) << 2) | ((b & 0xCCu) >> 2));
						capBuf[i] = (uint8_t)((b << 4) | (b >> 4));
					}
				}
				ProcessCapture(capBuf, (uint32_t)capLen * 8u, capGate, capRssi);
			}
			// re-arm immediately: the next burst may follow within a few bits
			{
				const uint16_t base = ((uint16_t)cfg.invert << 10) | ((uint16_t)cfg.sync4 << 3);
				BK4819_WriteRegister(BK4819_REG_59, base | (1u << 14));
				BK4819_WriteRegister(BK4819_REG_59, base | (1u << 12));
			}
			capLen = 0;
		}
	}
}

// ---------------------------------------------------------------------------
// MCU ADC + software AFSK demodulator (ADC input, optional)

#ifdef ENABLE_ALERT_ADC

#define ADC_FS 9600u

volatile bool gAlertAdcRun;

// quarter-wave-symmetric 32-entry sine, int8
static const int8_t sinTab[32] = {
	  0,  25,  49,  71,  90, 106, 117, 125, 127, 125, 117, 106,  90,  71,  49,  25,
	  0, -25, -49, -71, -90,-106,-117,-125,-127,-125,-117,-106, -90, -71, -49, -25
};

#define CORR_W 16   // correlator window, samples (1.67 ms)

static struct {
	int32_t  dc_x16;
	uint8_t  ph1, ph2;             // tone phases, 1/256 cycle
	int16_t  h1c[CORR_W], h1s[CORR_W], h2c[CORR_W], h2s[CORR_W];
	uint8_t  hi;
	int32_t  s1c, s1s, s2c, s2s;
	uint8_t  spb;                  // samples per bit
	uint8_t  phase;                // bit clock phase 0..spb-1
	uint8_t  lastBit;
	volatile bool active;          // main loop tells us the squelch is open
	volatile bool ready;           // a capture is waiting in adcCap
	uint8_t  buf[256];
	volatile uint16_t nbits;
} adc;

static inline int8_t sin256(uint8_t ph) { return sinTab[ph >> 3]; }
static inline int8_t cos256(uint8_t ph) { return sinTab[(uint8_t)(ph + 64u) >> 3]; }

static void AdcStart(void)
{
	ADC_Config_t c;
	memset(&c, 0, sizeof(c));
	c.CLK_SEL            = SYSCON_CLK_SEL_W_SARADC_SMPL_VALUE_DIV2;
	c.CH_SEL             = ADC_CH3;
	c.AVG                = SARADC_CFG_AVG_VALUE_1_SAMPLE;
	c.CONT               = SARADC_CFG_CONT_VALUE_SINGLE;
	c.MEM_MODE           = SARADC_CFG_MEM_MODE_VALUE_CHANNEL;
	c.SMPL_CLK           = SARADC_CFG_SMPL_CLK_VALUE_INTERNAL;
	c.SMPL_WIN           = SARADC_CFG_SMPL_WIN_VALUE_15_CYCLE;
	c.SMPL_SETUP         = SARADC_CFG_SMPL_SETUP_VALUE_1_CYCLE;
	c.ADC_TRIG           = SARADC_CFG_ADC_TRIG_VALUE_CPU;
	c.CALIB_KD_VALID     = SARADC_CALIB_KD_VALID_VALUE_YES;
	c.CALIB_OFFSET_VALID = SARADC_CALIB_OFFSET_VALID_VALUE_YES;
	c.DMA_EN             = SARADC_CFG_DMA_EN_VALUE_DISABLE;
	c.IE_CHx_EOC         = SARADC_IE_CHx_EOC_VALUE_NONE;
	c.IE_FIFO_FULL       = SARADC_IE_FIFO_FULL_VALUE_DISABLE;
	c.IE_FIFO_HFULL      = SARADC_IE_FIFO_HFULL_VALUE_DISABLE;

	// PA8 (pin 9): UART1 RX -> SAR ADC channel 3
	PORTCON_PORTA_SEL1 = (PORTCON_PORTA_SEL1 & ~PORTCON_PORTA_SEL1_A8_MASK) | PORTCON_PORTA_SEL1_A8_BITS_SARADC_CH3;

	ADC_Configure(&c);
	ADC_Enable();
	ADC_SoftReset();

	memset(&adc, 0, sizeof(adc));
	adc.dc_x16 = 2048 * 16;
	adc.spb    = (uint8_t)(ADC_FS / cfg.baud);

	ADC_Start();
	gAlertAdcRun = true;
	SysTick_Config(48000000u / ADC_FS);      // 9600 Hz tick, scheduler divides it down
}

static void AdcStop(void)
{
	gAlertAdcRun = false;
	SysTick_Config(480000);                  // back to the 10 ms system tick
	ADC_Disable();
	PORTCON_PORTA_SEL1 = (PORTCON_PORTA_SEL1 & ~PORTCON_PORTA_SEL1_A8_MASK) | PORTCON_PORTA_SEL1_A8_BITS_UART1_RX;
	BOARD_ADC_Init();                        // battery channels back
}

// 9600 Hz, from SystickHandler()
void ALERT_AdcTick(void)
{
	const int32_t raw = ADC_GetValue(ADC_CH3);
	ADC_Start();

	// DC removal (slow tracker) and scaling to ~8 bits
	adc.dc_x16 += ((raw << 4) - adc.dc_x16) >> 7;
	const int32_t x = (raw - (adc.dc_x16 >> 4)) >> 3;

	// two quadrature correlators with a sliding window
	const int16_t p1c = (int16_t)(x * cos256(adc.ph1)), p1s = (int16_t)(x * sin256(adc.ph1));
	const int16_t p2c = (int16_t)(x * cos256(adc.ph2)), p2s = (int16_t)(x * sin256(adc.ph2));
	adc.ph1 += (uint8_t)(256u * 1200u / ADC_FS);          // 32
	adc.ph2 += (uint8_t)((256u * 2200u + ADC_FS / 2) / ADC_FS); // 59 (2212 Hz)

	adc.s1c += p1c - adc.h1c[adc.hi]; adc.h1c[adc.hi] = p1c;
	adc.s1s += p1s - adc.h1s[adc.hi]; adc.h1s[adc.hi] = p1s;
	adc.s2c += p2c - adc.h2c[adc.hi]; adc.h2c[adc.hi] = p2c;
	adc.s2s += p2s - adc.h2s[adc.hi]; adc.h2s[adc.hi] = p2s;
	if (++adc.hi >= CORR_W) adc.hi = 0;

	const int32_t a1 = adc.s1c >> 6, b1 = adc.s1s >> 6, a2 = adc.s2c >> 6, b2 = adc.s2s >> 6;
	const uint8_t bit = ((a1 * a1 + b1 * b1) > (a2 * a2 + b2 * b2)) ? 1u : 0u;   // 1200 Hz = 1

	// bit clock recovery: pull the phase towards the transitions
	if (bit != adc.lastBit) {
		const uint8_t half = adc.spb / 2u;
		if (adc.phase < half) adc.phase -= adc.phase / 4u;
		else                  adc.phase += (adc.spb - adc.phase) / 4u;
		adc.lastBit = bit;
	}

	if (++adc.phase >= adc.spb) {
		adc.phase = 0;
		// sample point: middle of the bit is half a bit ago; use the current
		// decision, which the correlator window (~half a bit) already delays
		if (adc.active && !adc.ready && adc.nbits < sizeof(adc.buf) * 8u) {
			if (bit) adc.buf[adc.nbits >> 3] |=  (uint8_t)(0x80u >> (adc.nbits & 7u));
			else     adc.buf[adc.nbits >> 3] &= (uint8_t)~(0x80u >> (adc.nbits & 7u));
			adc.nbits++;
			if (adc.nbits >= sizeof(adc.buf) * 8u)
				adc.ready = true;
		}
	}
}

static void AdcPoll(void)
{
	sqOpen = (BK4819_ReadRegister(BK4819_REG_0C) & 2u) != 0;

	if (!adc.active && sqOpen && !adc.ready) {
		// burst begins: start collecting bits
		adc.nbits  = 0;
		capRssi    = rssiDbm;
		adc.active = true;
		stats.syncs++;
		redraw = true;
	} else if (adc.active && !sqOpen) {
		// burst ended
		adc.active = false;
		adc.ready  = true;
	}

	if (adc.ready) {
		const uint16_t n = adc.nbits;
		if (n >= 40) {
			memcpy(capBuf, adc.buf, (n + 7u) / 8u);
			ProcessCapture(capBuf, n, true, capRssi);
		}
		adc.nbits = 0;
		adc.ready = false;
		if (adc.active) adc.active = sqOpen;   // buffer-full case: keep going
	}
}
#endif // ENABLE_ALERT_ADC

// ---------------------------------------------------------------------------
// decoded readings

#ifdef ENABLE_VOICE
static void SpeakDigits(uint16_t id, uint16_t value)
{
	// station id then value, digit by digit (the voice ROM has digits only)
	uint8_t n = 0;
	uint16_t v[2] = { id, value };
	gVoiceWriteIndex = 0;
	gVoiceReadIndex  = 0;
	for (uint8_t k = 0; k < 2; k++) {
		uint8_t d[5], nd = 0;
		do { d[nd++] = v[k] % 10u; v[k] /= 10u; } while (v[k]);
		while (nd && n < 8)
			gVoiceID[n++] = (VOICE_ID_t)d[--nd];
	}
	gVoiceWriteIndex = n;
	AUDIO_PlaySingleVoice(false);
	voiceBusy = true;
}
#endif

static void Announce(const History_t *h)
{
	BACKLIGHT_TurnOn();
#ifdef ENABLE_VOICE
	if (cfg.voice && gEeprom.VOICE_PROMPT != VOICE_PROMPT_OFF) {
		SpeakDigits(h->id, h->value);
		return;
	}
#endif
	(void)h;
}

static void PushHistory(const AlertReading_t *r, int16_t rssi, uint8_t kind, uint8_t seq)
{
	if (historyCount < HISTORY_N)
		historyCount++;
	for (uint8_t i = historyCount - 1; i > 0; i--)
		history[i] = history[i - 1];
	history[0].id       = r->id;
	history[0].value    = r->value;
	history[0].kind     = kind;
	history[0].format   = r->format;
	history[0].rssi_dbm = (int8_t)((rssi < -127) ? -127 : (rssi > 0 ? 0 : rssi));
	history[0].seq      = seq;
}

static void ProcessCapture(const uint8_t *buf, uint32_t nbits, bool gated, int16_t rssi)
{
	AlertReading_t r[8];

	lastCapLen = (uint16_t)nbits;
	memcpy(lastCap, buf, sizeof(lastCap));

	if (cfg.gate && !gated) {
		stats.gated++;
		redraw = true;
		return;
	}

	const int n = ALERT_ScanBits(buf, nbits, cfg.polarity, 20, r, 8);
	if (n <= 0) {
		redraw = true;
		return;
	}

	burstSeq++;
	uint8_t announced = 0;
	for (int i = 0; i < n; i++) {
		// collapse repeats of the same reading inside one burst
		bool dup = false;
		for (int j = 0; j < i; j++)
			if (r[j].id == r[i].id && r[j].value == r[i].value) { dup = true; break; }
		if (dup)
			continue;
		if (cfg.confirm) {
			bool again = false;
			for (int j = i + 1; j < n; j++)
				if (r[j].id == r[i].id && r[j].value == r[i].value) { again = true; break; }
			if (!again)
				continue;
		}

		const char *name; uint8_t kind;
		const bool known = ALERT_LookupStation(r[i].id, &name, &kind);
		if (!known) {
			stats.unknown++;
			if (!cfg.show_unknown)
				continue;
		}
		stats.frames++;
		PushHistory(&r[i], rssi, kind, burstSeq);

#ifdef ENABLE_UART
		{
			char line[48];
			sprintf(line, "ALERT,%u,%u,%s,%d,%s\r\n", r[i].id, r[i].value,
			        r[i].format == ALERT_FMT_EIF ? "EIF" : "ABF", rssi, name);
			UART_Send(line, strlen(line));
#ifdef ENABLE_USB
			VCP_SendStr(line);
#endif
		}
#endif
		if (!announced++)
			Announce(&history[0]);
	}
	redraw = true;
}

// ---------------------------------------------------------------------------
// display

static void FormatValue(char *out, uint16_t value, uint8_t kind)
{
	if (value == ALERT_VALUE_FULL_SCALE) {
		strcpy(out, "FULL");
		return;
	}
	switch (kind) {
		case ALERT_KIND_BATT:   // tenths of a volt (MegaNet convention)
			sprintf(out, "%u.%uV", value / 10u, value % 10u);
			break;
		case ALERT_KIND_RAIN:   // tip count; 0.2 mm/tip assumed unless recorded
			sprintf(out, "%u", value);
			break;
		default:
			sprintf(out, "%u", value);
			break;
	}
}

static void DrawStatus(void)
{
	char s[24];
	memset(gStatusLine, 0, sizeof(gStatusLine));
	sprintf(s, "ALERT %s", cfg.input == INPUT_ADC ? "ADC" : "MDM");
	UI_PrintStringSmallBufferNormal(s, gStatusLine + 0);
	sprintf(s, "%4ddBm %s", rssiDbm, sqOpen ? "SQ" : "  ");
	UI_PrintStringSmallBufferNormal(s, gStatusLine + 60);
	if (capturing
#ifdef ENABLE_ALERT_ADC
	    || adc.active
#endif
	   )
		UI_PrintStringSmallBufferNormal("*", gStatusLine + 122);
	ST7565_BlitStatusLine();
}

static void DrawMain(void)
{
	char s[32];

	UI_DisplayClear();

	if (historyCount == 0) {
		const uint32_t f = gRxVfo->pRX->Frequency;
		sprintf(s, "%u.%05u MHz", (unsigned)(f / 100000u), (unsigned)(f % 100000u));
		UI_PrintStringSmallNormal(s, 0, 127, 0);
		UI_PrintStringSmallNormal("WAITING FOR ALERT", 0, 127, 2);
		sprintf(s, "%u SITES %s", ALERT_StationCount(), ALERT_StationSource());
		UI_PrintStringSmallNormal(s, 0, 127, 3);
		sprintf(s, "SQL %u.%u  %s", gEeprom.SQUELCH_LEVEL, gEeprom.SQUELCH_TENTHS,
		        cfg.voice ? "VOICE" : "QUIET");
		UI_PrintStringSmallNormal(s, 0, 127, 5);

		// Diagnostics on the last line. K rises if the key matrix reaches this
		// app at all, I rises when the BK4819 raises an interrupt, and the two
		// flags show squelch and capture state.
		sprintf(s, "K%u:%d I%u:%04x %c%c", dbgKeyCount, (int)dbgLastKey,
		        dbgIrqCount, dbgIrqBits, sqOpen ? 'Q' : '-', capturing ? 'C' : '-');
		UI_PrintStringSmallNormal(s, 0, 127, 6);
		UI_PrintStringSmallNormal("MENU=SET *=VOICE 1=RAW", 0, 127, 6);
	} else {
		const History_t *h = &history[0];
		const char *name; uint8_t kind;
		ALERT_LookupStation(h->id, &name, &kind);

		// line 0: station name (or unknown), line 1-2: value big, right: kind/id
		if (name[0]) {
			UI_PrintStringSmallNormal(name, 0, 0, 0);
		} else {
			sprintf(s, "ID %u NOT IN TABLE", h->id);
			UI_PrintStringSmallNormal(s, 0, 0, 0);
		}
		FormatValue(s, h->value, h->kind);
		UI_PrintString(s, 0, 0, 1, 8);
		sprintf(s, "%s", ALERT_KindLabel(h->kind));
		UI_PrintStringSmallNormal(s, 88, 0, 1);
		sprintf(s, "#%u", h->id);
		UI_PrintStringSmallNormal(s, 88, 0, 2);

		// lines 3..6: history
		for (uint8_t i = 0; i < 4 && i < historyCount; i++) {
			const History_t *e = &history[i];
			const char *n2; uint8_t k2; char v[8];
			ALERT_LookupStation(e->id, &n2, &k2);
			FormatValue(v, e->value, e->kind);
			sprintf(s, "%4u %-9.9s %6s", e->id, n2[0] ? n2 : "?", v);
			UI_PrintStringSmallNormal(s, 0, 0, 3 + i);
		}
	}
	ST7565_BlitFullScreen();
}

// settings rows
enum {
	SET_INPUT, SET_POLARITY, SET_VOICE, SET_GATE, SET_UNKNOWN, SET_CONFIRM, SET_MONITOR,
	SET_MODE, SET_BAUD, SET_SYNC, SET_SYNC4, SET_INVERT, SET_BITREV, SET_RXGAIN, SET_PKTLEN,
	SET_SQL, SET_N
};

static const char *const setNames[SET_N] = {
	"INPUT", "POLARITY", "VOICE", "SQ GATE", "UNKNOWN", "CONFIRM", "MONITOR",
	"MDM MODE", "BAUD", "SYNC", "SYNC LEN", "INVERT", "BIT REV", "RX GAIN", "CAPTURE",
	"SQL LEVEL"
};

static void SetValueString(uint8_t idx, char *s)
{
	static const char *const onoff[2]   = { "OFF", "ON" };
	static const char *const pol[3]     = { "NEG", "STD", "ANY" };
	static const char *const modes[MODE_N] = { "FFSK1218", "FFSK1224", "SAME", "DIRECT" };
	static const char *const syncs[SYNC_N] = { "0000", "FFFF", "AAAA", "5555" };
	switch (idx) {
		case SET_INPUT:    strcpy(s, cfg.input == INPUT_ADC ? "ADC PA8" : "BK MODEM"); break;
		case SET_POLARITY: strcpy(s, pol[cfg.polarity]); break;
		case SET_VOICE:    strcpy(s, onoff[cfg.voice]); break;
		case SET_GATE:     strcpy(s, onoff[cfg.gate]); break;
		case SET_UNKNOWN:  strcpy(s, cfg.show_unknown ? "SHOW" : "HIDE"); break;
		case SET_CONFIRM:  strcpy(s, cfg.confirm ? "2 COPIES" : "OFF"); break;
		case SET_MONITOR:  strcpy(s, onoff[cfg.monitor]); break;
		case SET_MODE:     strcpy(s, modes[cfg.mode]); break;
		case SET_BAUD:     sprintf(s, "%u", cfg.baud); break;
		case SET_SYNC:     strcpy(s, syncs[cfg.sync]); break;
		case SET_SYNC4:    strcpy(s, cfg.sync4 ? "4 BYTES" : "2 BYTES"); break;
		case SET_INVERT:   strcpy(s, onoff[cfg.invert]); break;
		case SET_BITREV:   strcpy(s, onoff[cfg.bitrev]); break;
		case SET_RXGAIN:   sprintf(s, "%u", cfg.rxgain); break;
		case SET_PKTLEN:   sprintf(s, "%u B", cfg.pktlen); break;
		case SET_SQL:      sprintf(s, "%u.%u", gEeprom.SQUELCH_LEVEL, gEeprom.SQUELCH_TENTHS); break;
		default: s[0] = 0; break;
	}
}

static void ApplySquelch(void)
{
	RADIO_ConfigureSquelchAndOutputPower(gRxVfo);
	BK4819_SetupSquelch(
		gRxVfo->SquelchOpenRSSIThresh,    gRxVfo->SquelchCloseRSSIThresh,
		gRxVfo->SquelchOpenNoiseThresh,   gRxVfo->SquelchCloseNoiseThresh,
		gRxVfo->SquelchCloseGlitchThresh, gRxVfo->SquelchOpenGlitchThresh);
	// BK4819_SetupSquelch zeroes REG_70 (our bit clock) and mutes AF
	if (cfg.input == INPUT_MODEM)
		ModemArm();
	if (cfg.monitor)
		BK4819_SetAF(BK4819_AF_FM);
}

static void StepSquelch(int dir)
{
	int lvl = gEeprom.SQUELCH_LEVEL * 10 + gEeprom.SQUELCH_TENTHS + dir;
	if (lvl < 0) lvl = 0;
	if (lvl > 90) lvl = 90;
	gEeprom.SQUELCH_LEVEL  = (uint8_t)(lvl / 10);
	gEeprom.SQUELCH_TENTHS = (uint8_t)(lvl % 10);
	ApplySquelch();
}

static void ChangeSetting(uint8_t idx, int dir)
{
	bool rearm = false;
	switch (idx) {
#ifdef ENABLE_ALERT_ADC
		case SET_INPUT:
			if (cfg.input == INPUT_MODEM) { ModemStop(); AdcStart(); cfg.input = INPUT_ADC; }
			else                          { AdcStop(); cfg.input = INPUT_MODEM; rearm = true; }
			break;
#endif
		case SET_POLARITY: cfg.polarity = (uint8_t)((cfg.polarity + 3 + dir) % 3); break;
		case SET_VOICE:    cfg.voice = !cfg.voice; break;
		case SET_GATE:     cfg.gate = !cfg.gate; break;
		case SET_UNKNOWN:  cfg.show_unknown = !cfg.show_unknown; break;
		case SET_CONFIRM:  cfg.confirm = !cfg.confirm; break;
		case SET_MONITOR:
			cfg.monitor = !cfg.monitor;
			BK4819_SetAF(cfg.monitor ? BK4819_AF_FM : BK4819_AF_MUTE);
			if (cfg.monitor) AUDIO_AudioPathOn(); else AUDIO_AudioPathOff();
			break;
		case SET_MODE:     cfg.mode = (uint8_t)((cfg.mode + MODE_N + dir) % MODE_N); rearm = true; break;
		case SET_BAUD: {
			static const uint16_t bauds[] = { 200, 300, 600, 1200 };
			uint8_t i = 1;
			for (uint8_t k = 0; k < 4; k++) if (bauds[k] == cfg.baud) i = k;
			i = (uint8_t)((i + 4 + dir) % 4);
			cfg.baud = bauds[i];
			rearm = true;
#ifdef ENABLE_ALERT_ADC
			adc.spb = (uint8_t)(ADC_FS / cfg.baud);
#endif
			break;
		}
		case SET_SYNC:     cfg.sync = (uint8_t)((cfg.sync + SYNC_N + dir) % SYNC_N); rearm = true; break;
		case SET_SYNC4:    cfg.sync4 = !cfg.sync4; rearm = true; break;
		case SET_INVERT:   cfg.invert = !cfg.invert; rearm = true; break;
		case SET_BITREV:   cfg.bitrev = !cfg.bitrev; break;
		case SET_RXGAIN:   cfg.rxgain = (uint8_t)((cfg.rxgain + 4 + dir) & 3u); rearm = true; break;
		case SET_PKTLEN: {
			int v = cfg.pktlen + dir * 16;
			if (v < 16) v = 16;
			if (v > 240) v = 240;
			cfg.pktlen = (uint8_t)v;
			rearm = true;
			break;
		}
		case SET_SQL:      StepSquelch(dir); break;
		default: break;
	}
	if (rearm && cfg.input == INPUT_MODEM)
		ModemArm();
}

static void DrawSettings(void)
{
	char s[24], v[16];
	UI_DisplayClear();
	UI_PrintStringSmallNormal("ALERT SETTINGS", 0, 127, 0);
	// show a window of 6 rows around the selection
	uint8_t first = (setIndex >= 5) ? (uint8_t)(setIndex - 5) : 0;
	if (first > SET_N - 6) first = SET_N - 6;
	for (uint8_t i = 0; i < 6; i++) {
		const uint8_t idx = (uint8_t)(first + i);
		SetValueString(idx, v);
		sprintf(s, "%c%-9s %s", idx == setIndex ? '>' : ' ', setNames[idx], v);
		UI_PrintStringSmallNormal(s, 0, 0, 1 + i);
	}
	ST7565_BlitFullScreen();
}

static void DrawRaw(void)
{
	char s[28];
	UI_DisplayClear();
	UI_PrintStringSmallNormal("RAW CAPTURE", 0, 127, 0);
	sprintf(s, "SYNC %u  FRM %u", stats.syncs, stats.frames);
	UI_PrintStringSmallNormal(s, 0, 0, 1);
	sprintf(s, "GATED %u  UNKN %u", stats.gated, stats.unknown);
	UI_PrintStringSmallNormal(s, 0, 0, 2);
	sprintf(s, "LAST %u BITS", lastCapLen);
	UI_PrintStringSmallNormal(s, 0, 0, 3);
	sprintf(s, "%02X%02X %02X%02X %02X%02X %02X%02X", lastCap[0], lastCap[1], lastCap[2], lastCap[3],
	        lastCap[4], lastCap[5], lastCap[6], lastCap[7]);
	UI_PrintStringSmallNormal(s, 0, 0, 4);
	{	// first 20 bits as 0/1 so the preamble/start bits can be eyeballed
		uint8_t i;
		for (i = 0; i < 20; i++)
			s[i] = ALERT_GetBit(lastCap, i) ? '1' : '0';
		s[i] = 0;
		UI_PrintStringSmallNormal(s, 0, 0, 5);
	}
	sprintf(s, "%s %u BD", cfg.input == INPUT_ADC ? "ADC" : "MODEM", cfg.baud);
	UI_PrintStringSmallNormal(s, 0, 0, 6);
	ST7565_BlitFullScreen();
}

static void Draw(void)
{
	DrawStatus();
	switch (view) {
		case VIEW_SETTINGS: DrawSettings(); break;
		case VIEW_RAW:      DrawRaw(); break;
		default:            DrawMain(); break;
	}
	redraw = false;
}

// ---------------------------------------------------------------------------
// keys

static void OnKey(KEY_Code_t key)
{
	BACKLIGHT_TurnOn();
	redraw = true;

	if (view == VIEW_SETTINGS) {
		switch (key) {
			case KEY_UP:   ChangeSetting(setIndex, +1); break;
			case KEY_DOWN: ChangeSetting(setIndex, -1); break;
			case KEY_MENU: setIndex = (uint8_t)((setIndex + 1) % SET_N); break;
			case KEY_STAR: setIndex = (uint8_t)((setIndex + SET_N - 1) % SET_N); break;
			case KEY_EXIT:
				ALERT_StoreConfig();
				SETTINGS_SaveSettings();
				view = VIEW_MAIN;
				break;
			default: break;
		}
		return;
	}

	switch (key) {
		case KEY_EXIT:
			if (view == VIEW_RAW) view = VIEW_MAIN;
			else running = false;
			break;
		case KEY_MENU: view = VIEW_SETTINGS; setIndex = 0; break;
		case KEY_1:    view = (view == VIEW_RAW) ? VIEW_MAIN : VIEW_RAW; break;
		case KEY_STAR: cfg.voice = !cfg.voice; ALERT_StoreConfig(); break;
		case KEY_F:    ChangeSetting(SET_MONITOR, 0); break;
		case KEY_UP:   StepSquelch(+1); break;
		case KEY_DOWN: StepSquelch(-1); break;
		case KEY_0:
			historyCount = 0;
			memset(&stats, 0, sizeof(stats));
			break;
		case KEY_5:
			if (historyCount) Announce(&history[0]);
			break;
		default: break;
	}
}

// ---------------------------------------------------------------------------
// main loop

void APP_RunAlert(void)
{
	KEY_Code_t lastKey = KEY_INVALID;
	uint16_t   keyHeldMs = 0;
	uint16_t   pttHeldMs = 0;

	ALERT_LoadConfig();
	running = true;
	view    = VIEW_MAIN;
	redraw  = true;
	memset(&stats, 0, sizeof(stats));
	historyCount = 0;

	// normal RX on the current VFO, then squelch as configured, audio muted
	RADIO_SetupRegisters(true);
	FUNCTION_Select(FUNCTION_FOREGROUND);
	AUDIO_AudioPathOff();
	rssiDbm = BK4819_GetRSSI_dBm();

#ifdef ENABLE_ALERT_ADC
	if (cfg.input == INPUT_ADC) AdcStart();
	else                        ModemArm();
#else
	ModemArm();
#endif
	if (cfg.monitor) {
		BK4819_SetAF(BK4819_AF_FM);
		AUDIO_AudioPathOn();
	}

	while (running) {
		// keys (edge triggered)
		const KEY_Code_t key = KEYBOARD_Poll();
		if (key != lastKey) {
			dbgKeyCount++;
			dbgLastKey = (int16_t)key;
			if (key != KEY_INVALID && key != KEY_PTT)
				OnKey(key);
			lastKey = key;
		}

		// Escape that does not depend on the key matrix at all: the PTT is a
		// plain GPIO. If KEYBOARD_Poll() never reports anything in this context
		// - which is what the first hardware test suggests - this is still a
		// way out without a power cycle.
		if (GPIO_IsPttPressed()) {
			if (++pttHeldMs > 800)
				running = false;
		} else {
			pttHeldMs = 0;
		}

		// Escape hatch: holding any key for ~3 s always leaves the app. Edge
		// detection above fires once per press, so if a key were mis-mapped or
		// an edge missed there would otherwise be no way out but a power cycle
		// - which is exactly what was reported on first hardware test.
		if (key != KEY_INVALID && key == lastKey) {
			if (++keyHeldMs > 3000) {
				running = false;
			}
		} else {
			keyHeldMs = 0;
		}

		// signal input
#ifdef ENABLE_ALERT_ADC
		if (cfg.input == INPUT_ADC) AdcPoll();
		else                        ModemPoll();
		if (cfg.input == INPUT_ADC) adc.active = adc.active;   // (keeps volatile access explicit)
#else
		ModemPoll();
#endif

#ifdef ENABLE_VOICE
		if (voiceBusy) {
			if (gFlagPlayQueuedVoice) {
				gFlagPlayQueuedVoice = false;
				AUDIO_PlayQueuedVoice();
			}
			if (gVoiceReadIndex == 0 && gVoiceWriteIndex == 0) {
				// finished: voice playback muted the AF and may have touched the AF path
				voiceBusy = false;
				if (cfg.monitor) { BK4819_SetAF(BK4819_AF_FM); AUDIO_AudioPathOn(); }
				else             { BK4819_SetAF(BK4819_AF_MUTE); AUDIO_AudioPathOff(); }
			}
		}
#endif

		// housekeeping every ~100 ms: RSSI, status line
		if (gNextTimeslice) {
			gNextTimeslice = false;
			if ((++tick % 10) == 0) {
				rssiDbm = BK4819_GetRSSI_dBm();
				DrawStatus();
			}
			if ((tick % 50) == 0)
				redraw = true;   // ~500 ms heartbeat: a frozen screen is now visible as such
		}

		if (redraw)
			Draw();

		SYSTEM_DelayMs(1);
	}

	// leave: modem/ADC off, radio back to normal
#ifdef ENABLE_ALERT_ADC
	if (cfg.input == INPUT_ADC) AdcStop();
	else                        ModemStop();
#else
	ModemStop();
#endif
	BK4819_WriteRegister(BK4819_REG_3F, 0);
	BK4819_WriteRegister(BK4819_REG_02, 0);
	ALERT_StoreConfig();
	SETTINGS_SaveSettings();
	RADIO_SetupRegisters(true);
	gRequestDisplayScreen = DISPLAY_MAIN;
	gUpdateStatus  = true;
	gUpdateDisplay = true;
}

#endif // ENABLE_ALERT
