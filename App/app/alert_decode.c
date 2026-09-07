/* ALERT (ERTS) telemetry frame decoder - see alert_decode.h.
 *
 * Reference: MegaNet packets.js (cdomotor-g/MegaNet), whose FORMATS table this
 * mirrors bit for bit. Payload bit positions are numbered 0..31 in
 * transmission order: word k data bit i (LSB first) is position 8k + i.
 *
 *   ABF   A0..A5 [1][0]   A6..A11 [1][0]   A12 D0..D4 [1][1]   D5..D10 [1][1]
 *   EIF   A0..A5 [1][1]   A6..A12 D0       D1..D8              D9 D10 R5..R0
 *
 * Address and data bits are LSB first; the EIF FCS is MSB first.
 */
#include "app/alert_decode.h"

#define CRC6_POLY 0x19u   // x^6 + x^4 + x^3 + 1

uint8_t ALERT_Crc6(uint32_t bits, uint8_t nbits)
{
	uint8_t reg = 0;
	for (int i = nbits - 1; i >= 0; i--) {
		const uint8_t b  = (bits >> i) & 1u;
		const uint8_t fb = ((reg >> 5) & 1u) ^ b;
		reg = (reg << 1) & 0x3fu;
		if (fb)
			reg ^= CRC6_POLY;
	}
	return reg;
}

// payload bit at transmission position p (0 = first sent)
#define P(p) ((payload >> (31u - (p))) & 1u)

// gather n bits starting at position p, LSB first
static uint16_t lsb_first(uint32_t payload, uint8_t p, uint8_t n)
{
	uint16_t v = 0;
	for (uint8_t i = 0; i < n; i++)
		v |= (uint16_t)P(p + i) << i;
	return v;
}

// gather n bits starting at position p, MSB first
static uint16_t msb_first(uint32_t payload, uint8_t p, uint8_t n)
{
	uint16_t v = 0;
	for (uint8_t i = 0; i < n; i++)
		v = (v << 1) | P(p + i);
	return v;
}

uint8_t ALERT_DecodePayload32(uint32_t payload, AlertReading_t *out)
{
	const uint8_t k1 = (P(6) << 1) | P(7);      // word 1 check bits
	const uint8_t k2 = (P(14) << 1) | P(15);    // word 2 check bits

	if (k1 == 2u && k2 == 2u) {
		// ABF: words 3 and 4 carry check bits 11
		if (P(22) && P(23) && P(30) && P(31)) {
			const uint16_t a = lsb_first(payload, 0, 6)
			                 | (lsb_first(payload, 8, 6) << 6)
			                 | (lsb_first(payload, 16, 1) << 12);
			const uint16_t d = lsb_first(payload, 17, 5)
			                 | (lsb_first(payload, 24, 6) << 5);
			out->id     = a;
			out->value  = d;
			out->format = ALERT_FMT_ABF;
			return ALERT_FMT_ABF;
		}
		return ALERT_FMT_NONE;
	}

	if (k1 == 3u) {
		// EIF: A0..A5 | A6..A12 D0 | D1..D8 | D9 D10 R5..R0
		const uint16_t a = lsb_first(payload, 0, 6)
		                 | (lsb_first(payload, 8, 7) << 6);
		const uint16_t d = lsb_first(payload, 15, 1)
		                 | (lsb_first(payload, 16, 8) << 1)
		                 | (lsb_first(payload, 24, 2) << 9);
		const uint8_t r  = (uint8_t)msb_first(payload, 26, 6);
		const uint32_t crc_in = ((uint32_t)a << 11) | d;   // address then data, MSB first
		if (ALERT_Crc6(crc_in, 24) == r) {
			out->id     = a;
			out->value  = d;
			out->format = ALERT_FMT_EIF;
			return ALERT_FMT_EIF;
		}
	}

	return ALERT_FMT_NONE;
}

bool ALERT_DecodeA2C(const uint8_t b[4], AlertReading_t *out)
{
	if (b[3] != 0)
		return false;
	out->id       = ((uint16_t)(b[1] & 0x1fu) << 8) | b[0];
	out->value    = ((uint16_t)(b[1] >> 5) << 8) | b[2];
	out->format   = ALERT_FMT_A2C;
	out->polarity = ALERT_POL_STANDARD;
	out->bit_pos  = 0;
	return true;
}

static int scan_polarity(const uint8_t *buf, uint32_t nbits, uint8_t polarity,
                         uint8_t max_gap, AlertReading_t *out, int max_out)
{
	const uint8_t idle  = (polarity == ALERT_POL_NEGATIVE) ? 0u : 1u;
	const uint8_t start = idle ^ 1u;

	uint8_t  words[4];
	uint32_t word_pos[4];
	uint8_t  nwords = 0;
	int      found  = 0;
	uint32_t pos    = 0;

	while (pos + 10u <= nbits && found < max_out) {
		if (ALERT_GetBit(buf, pos) != start) {
			pos++;
			continue;
		}
		// a real start bit follows an idle bit (the previous stop bit or preamble)
		if (pos > 0 && ALERT_GetBit(buf, pos - 1u) != idle) {
			pos++;
			continue;
		}
		// candidate word: start bit at pos, stop bit at pos+9
		if (ALERT_GetBit(buf, pos + 9u) != idle) {
			pos++;               // framing error: slip one bit
			nwords = 0;
			continue;
		}
		uint8_t w = 0;
		for (uint8_t i = 0; i < 8; i++)
			w |= (uint8_t)(ALERT_GetBit(buf, pos + 1u + i) << i);   // LSB first

		// drop the run if this word does not follow the previous one closely
		if (nwords && (pos - (word_pos[nwords - 1] + 10u)) > max_gap)
			nwords = 0;

		if (nwords == 4) {
			words[0] = words[1]; words[1] = words[2]; words[2] = words[3];
			word_pos[0] = word_pos[1]; word_pos[1] = word_pos[2]; word_pos[2] = word_pos[3];
			nwords = 3;
		}
		words[nwords]    = w;
		word_pos[nwords] = pos;
		nwords++;
		pos += 10u;

		if (nwords == 4) {
			// assemble the 32-bit payload in transmission order:
			// position 8k+i = data bit i of word k
			uint32_t payload = 0;
			for (uint8_t k = 0; k < 4; k++)
				for (uint8_t i = 0; i < 8; i++)
					payload |= (uint32_t)((words[k] >> i) & 1u) << (31u - (8u * k + i));

			AlertReading_t r;
			if (ALERT_DecodePayload32(payload, &r) != ALERT_FMT_NONE) {
				r.polarity = polarity;
				r.bit_pos  = (uint16_t)word_pos[0];
				out[found++] = r;
				nwords = 0;      // a frame never overlaps the next
			}
		}
	}
	return found;
}

int ALERT_ScanBits(const uint8_t *buf, uint32_t nbits, uint8_t polarity,
                   uint8_t max_gap, AlertReading_t *out, int max_out)
{
	if (polarity != ALERT_POL_ANY)
		return scan_polarity(buf, nbits, polarity, max_gap, out, max_out);

	int n = scan_polarity(buf, nbits, ALERT_POL_NEGATIVE, max_gap, out, max_out);
	if (n < max_out)
		n += scan_polarity(buf, nbits, ALERT_POL_STANDARD, max_gap, out + n, max_out - n);
	return n;
}
