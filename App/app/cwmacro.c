 /* Copyright 2026 NR7Y
 * https://github.com/briand
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

 // CW Macro system implementation

#include "app/cwmacro.h"
#ifdef ENABLE_CODE_PRACTICE
#include "app/cpo.h"
#include "app/cpocall.h"
#endif
#include "driver/eeprom.h"
#include "driver/uart.h"
#include "external/printf/printf.h"
#include "misc.h"
#include "ui/main.h"
#include <string.h>

// Debug logging control - set to 1 to enable encoder debug output
#define CW_ENCODER_DEBUG 0

// Macro save/load debug logging - set to 1 to enable
#define CW_MACRO_DEBUG 0

// Morse code lookup table
// Pattern: LSB first, 0=dit, 1=dah
typedef struct {
	char ch;
	uint8_t length;   // Number of elements (1-9)
	uint8_t pattern;  // Bit pattern: 0=dit, 1=dah, LSB first; elements past bit 7 are dits
} MorseCode_t;

// bits in the lookup table are element-order-BACKWARDS, pop the stack.
static const MorseCode_t MORSE_TABLE[] = {
	// Letters A-Z (LSB first: bit 0 = first element, 0=dit, 1=dah)
	{'A', 2, 0b10},      // .-
	{'B', 4, 0b0001},    // -...
	{'C', 4, 0b0101},    // -.-.
	{'D', 3, 0b001},     // -..
	{'E', 1, 0b0},       // .
	{'F', 4, 0b0100},    // ..-.
	{'G', 3, 0b011},     // --.
	{'H', 4, 0b0000},       // ....
	{'I', 2, 0b00},       // ..
	{'J', 4, 0b1110},    // .---
	{'K', 3, 0b101},     // -.-
	{'L', 4, 0b0010},    // .-..
	{'M', 2, 0b11},      // --
	{'N', 2, 0b01},      // -.
	{'O', 3, 0b111},     // ---
	{'P', 4, 0b0110},    // .--.
	{'Q', 4, 0b1011},    // --.-
	{'R', 3, 0b010},     // .-.
	{'S', 3, 0b000},       // ...
	{'T', 1, 0b1},       // -
	{'U', 3, 0b100},     // ..-
	{'V', 4, 0b1000},    // ...-
	{'W', 3, 0b110},     // .--
	{'X', 4, 0b1001},    // -..-
	{'Y', 4, 0b1101},    // -.--
	{'Z', 4, 0b0011},    // --..
	// Numbers 0-9
	{'0', 5, 0b11111},   // -----
	{'1', 5, 0b11110},   // .----
	{'2', 5, 0b11100},   // ..---
	{'3', 5, 0b11000},   // ...--
	{'4', 5, 0b10000},   // ....-
	{'5', 5, 0b00000},       // .....
	{'6', 5, 0b00001},       // -....
	{'7', 5, 0b00011},      // --...
	{'8', 5, 0b00111},     // ---..
	{'9', 5, 0b01111},    // ----.
	// Punctuation
	{'/', 5, 0b01001},    // -..-.
	{'?', 6, 0b001100},   // ..--..
	{'.', 6, 0b101010},   // .-.-.- 
	{',', 6, 0b110011},   // --..--
	{'=', 5, 0b10001},    // -...-   BT prosign
	{'-', 6, 0b100001},   // -....-
	{'+', 5, 0b01010},   // .-.-. AR prosign
	{'(', 5, 0b01101},   // -.--.  KN prosign
	{'&', 5, 0b00010},   // .-...  AS prosign
	// Prosigns with no punctuation of their own
	{CW_PROSIGN_SK, 6, 0b101000},   // ...-.-
	{CW_PROSIGN_BK, 7, 0b1010001},  // -...-.-
	{CW_PROSIGN_KA, 5, 0b10101},    // -.-.-
	{CW_PROSIGN_SN, 5, 0b01000},    // ...-.
	{CW_PROSIGN_UR, 6, 0b010100},   // ..-.-.
	{CW_PROSIGN_SOS, 9, 0b000111000},  // ...---... (its ninth element, a dit, is past bit 7)
};

// Each prosign's letters, in lowercase for the TX display buffer and macro preview.
// No entry starts another, so a run of them splits into prosigns only one way.
static const char PROSIGN_LETTERS[][4] = {"sk", "bk", "ka", "sn", "ur", "sos"};

// Lowercase only ever comes from a prosign, whose letters always go in together
static bool IsProsignLetter(char c)
{
	return c >= 'a' && c <= 'z';
}

// Number of letters of the prosign that starts at s, or 0 if none does
static uint8_t ProsignLength(const char *s)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(PROSIGN_LETTERS); i++) {
		const size_t n = strlen(PROSIGN_LETTERS[i]);
		if (strncmp(s, PROSIGN_LETTERS[i], n) == 0)
			return (uint8_t)n;
	}
	return 0;
}
#define MORSE_TABLE_SIZE (sizeof(MORSE_TABLE) / sizeof(MORSE_TABLE[0]))

// Encoder state machine
static uint16_t s_encoder_pattern = 0;  // Accumulated pattern; 16 bits so a dah in bit 8 can't pass for SOS
static uint8_t s_encoder_length = 0;   // Number of elements in pattern
static bool s_encoder_space_pending = false;  // Word space before next char
static bool s_encoder_overflow = false;  // More than 9 elements sent (invalid char)

// Recording state
bool gCW_Recording = false;
uint8_t gCW_RecordMacroIndex = 0;
uint8_t gCW_RecordBuffer[CW_MACRO_MAX_LEN];
uint8_t gCW_RecordLength = 0;
bool gCW_RecordNewChar = false;

// Playback state (shared with cwkeyer.c)
bool gCW_PlaybackActive = false;
bool gCW_PlaybackRepeat = false;
uint8_t gCW_PlaybackMacroIndex = 0;
uint16_t gCW_MessageRepeatCountdown_500ms = 0;

// TX display buffer - shows characters being transmitted
char gCW_TX_Display[CW_TX_DISPLAY_SIZE];
uint8_t gCW_TX_DisplayIndex = 0;
bool gCW_TX_DisplayUpdated = false;
static const uint16_t MACRO_ADDRS[CW_MACRO_COUNT] = {
	CW_MACRO1_EEPROM_ADDR,
	CW_MACRO2_EEPROM_ADDR,
	CW_MACRO3_EEPROM_ADDR,
	CW_MACRO4_EEPROM_ADDR
};

bool CW_ValidateChar(char ch)
{
	// A-Z
	if (ch >= 'A' && ch <= 'Z') return true;
	// '+' (43) through '9' (57): +,-./ and 0-9, all contiguous and all valid
	if (ch >= '+' && ch <= '9') return true;
	if (ch == '&') return true;
	if (ch == '(') return true;
	if (ch == '=') return true;
	if (ch == '?') return true;
	if (CW_IS_PROSIGN(ch)) return true;
	return false;
}

static uint8_t compute_macro_checksum(const uint8_t *block, uint8_t length)
{
	uint8_t sum = 0;
	// Sum payload bytes only (block[1] .. block[length])
	for (uint8_t i = 1; i <= length; i++) {
		sum += block[i];
	}
	return sum;
}

uint8_t CW_MacroBlockLength(const uint8_t *block)
{
	const uint8_t raw_len = block[0];

	// 0xFF = empty block; a clear high bit means no signature
	if (raw_len == 0xFF || (raw_len & CW_MACRO_SIG) == 0)
		return 0;

	const uint8_t length = raw_len & ~CW_MACRO_SIG;
	if (length == 0 || length > CW_MACRO_MAX_LEN)
		return 0;

	if (block[CW_MACRO_CHECKSUM_OFFSET] != compute_macro_checksum(block, length))
		return 0;

	return length;
}

bool CW_MacroHeaderValid(void)
{
	uint8_t hdr[8];
	EEPROM_ReadBuffer(CW_MACRO_HDR_EEPROM_ADDR, hdr, sizeof(hdr));

	// A newer version may add fields, but the block size is what we rely on
	return memcmp(hdr, CW_MACRO_HDR_SIG, 3) == 0
		&& hdr[3] >= CW_MACRO_HDR_VERSION && hdr[3] != 0xFF
		&& hdr[5] == CW_MACRO_BLOCK_SIZE;
}

void CW_WriteMacroHeader(void)
{
	uint8_t hdr[CW_MACRO_HDR_SIZE];
	memset(hdr, 0xFF, sizeof(hdr));
	memcpy(hdr, CW_MACRO_HDR_SIG, 3);
	hdr[3] = CW_MACRO_HDR_VERSION;
	hdr[4] = CW_MACRO_COUNT;
	hdr[5] = CW_MACRO_BLOCK_SIZE;

	for (uint8_t i = 0; i < CW_MACRO_HDR_SIZE; i += 8)
		EEPROM_WriteBuffer(CW_MACRO_HDR_EEPROM_ADDR + i, hdr + i, 8);
}

uint8_t CW_GetMacroLength(uint8_t macroIndex)
{
	if (macroIndex >= CW_MACRO_COUNT)
		return 0;

	// No macro area header means no macros
	if (!CW_MacroHeaderValid())
		return 0;

	uint8_t block[CW_MACRO_BLOCK_SIZE];
	EEPROM_ReadBuffer(MACRO_ADDRS[macroIndex], block, CW_MACRO_BLOCK_SIZE);

#if CW_MACRO_DEBUG
	{
		char buf[64];
		sprintf_(buf, "CW_GetMacroLength: idx=%u raw_len=%u\r\n", macroIndex, block[0]);
		UART_Send(buf, strlen(buf));
	}
#endif

	return CW_MacroBlockLength(block);
}

uint8_t CW_LoadMacro(uint8_t macroIndex, char *buffer, uint8_t bufferSize)
{
	if (macroIndex >= CW_MACRO_COUNT || buffer == NULL || bufferSize == 0)
		return 0;
	
	uint8_t length = CW_GetMacroLength(macroIndex);
	if (length == 0) {
		buffer[0] = '\0';
		return 0;
	}
	
	// Read the macro data block (length + payload + checksum)
	uint8_t block[CW_MACRO_BLOCK_SIZE];
	EEPROM_ReadBuffer(MACRO_ADDRS[macroIndex], block, CW_MACRO_BLOCK_SIZE);

#if CW_MACRO_DEBUG
	{
		char buf[64];
		sprintf_(buf, "CW_LoadMacro: read %u bytes from EEPROM (payload length=%u)\r\n", CW_MACRO_BLOCK_SIZE, length);
		UART_Send(buf, strlen(buf));
		for (uint8_t i = 0; i < length && i < 10; i++) {
			sprintf_(buf, "  [%u]=%u '%c'%s\r\n", i, block[i+1], 
				CW_MACRO_GET_CHAR(block[i+1]), CW_MACRO_HAS_SPACE(block[i+1]) ? " +SPC" : "");
			UART_Send(buf, strlen(buf));
		}
	}
#endif
	
	// Decode into buffer with spaces
	uint8_t outPos = 0;
	for (uint8_t i = 0; i < length && outPos < bufferSize - 1; i++) {
		// Check if space precedes this character
		if (CW_MACRO_HAS_SPACE(block[i+1])) {
			if (outPos < bufferSize - 1) {
				buffer[outPos++] = ' ';
			}
		}
		
		// Get the character
		char ch = CW_MACRO_GET_CHAR(block[i+1]);
		if (outPos < bufferSize - 1) {
			buffer[outPos++] = ch;
		}
	}
	
	buffer[outPos] = '\0';
	return length;  // Return character count (not including spaces)
}

void CW_SaveMacro(uint8_t macroIndex, const char *buffer, uint8_t length)
{
	if (macroIndex >= CW_MACRO_COUNT || buffer == NULL)
		return;
	
	// Sanity check length
	if (length > CW_MACRO_MAX_LEN)
		length = CW_MACRO_MAX_LEN;

#if CW_MACRO_DEBUG
	{
		char buf[64];
		sprintf_(buf, "CW_SaveMacro: idx=%u len=%u\r\n", macroIndex, length);
		UART_Send(buf, strlen(buf));
		// Show all bytes being saved
		for (uint8_t i = 0; i < length && i < 20; i++) {
			sprintf_(buf, "  [%u]=%u (%c%s)\r\n", i, (uint8_t)buffer[i], 
				CW_MACRO_GET_CHAR(buffer[i]), CW_MACRO_HAS_SPACE(buffer[i]) ? " +SPC" : "");
			UART_Send(buf, strlen(buf));
		}
		if (length > 20) {
			sprintf_(buf, "  ... and %u more\r\n", length - 20);
			UART_Send(buf, strlen(buf));
		}
	}
#endif

	// Prepare data to write (CW_MACRO_BLOCK_SIZE bytes total)
	uint8_t data[CW_MACRO_BLOCK_SIZE];
	memset(data, 0xFF, sizeof(data));
	
	// First byte is the character count with signature bit (or 0xFF for empty)
	data[0] = (length == 0) ? 0xFF : ((length & ~CW_MACRO_SIG) | CW_MACRO_SIG);
	
	// Copy encoded characters from buffer (payload up to CW_MACRO_MAX_LEN)
	for (uint8_t i = 0; i < length; i++) {
		data[i + 1] = buffer[i];
	}

	// Compute and store checksum in the extra byte after payload
	data[CW_MACRO_CHECKSUM_OFFSET] = compute_macro_checksum(data, length);

#if CW_MACRO_DEBUG
	{
		char buf[64];
		sprintf_(buf, "CW_SaveMacro: prepared %u-byte EEPROM block\r\n", (uint8_t)sizeof(data));
		UART_Send(buf, strlen(buf));
		sprintf_(buf, "  data[0]=%u (length byte)\r\n", data[0]);
		UART_Send(buf, strlen(buf));
		for (uint8_t i = 1; i <= length && i <= 20; i++) {
			sprintf_(buf, "  data[%u]=%u\r\n", i, data[i]);
			UART_Send(buf, strlen(buf));
		}
	}
#endif

	if (!CW_MacroHeaderValid())
		CW_WriteMacroHeader();

	// Write block to EEPROM in 8-byte chunks (aligned)
	// EEPROM_WriteBuffer only writes 8 bytes at a time
	for (uint8_t i = 0; i < CW_MACRO_BLOCK_SIZE; i += 8) {
		EEPROM_WriteBuffer(MACRO_ADDRS[macroIndex] + i, data + i, 8);
	}
}

uint8_t CW_FormatMacroDisplay(uint8_t macroIndex, char *display, uint8_t maxChars)
{
	if (display == NULL || maxChars == 0)
		return 0;
	
	// Use CW_GetMacroLength to validate signature + checksum
	uint8_t length = CW_GetMacroLength(macroIndex);
	if (length == 0) {
		strcpy(display, "empty");
		return 0;
	}

	// Read first up to 9 encoded bytes (start after length byte)
	uint8_t data[10];
	EEPROM_ReadBuffer(MACRO_ADDRS[macroIndex] + 1, data, (length < 9) ? (length) : 9);

	// Copy up to 9 display positions (spaces + chars)
	uint8_t outPos = 0;
	uint8_t dispCount = 0;

	for (uint8_t i = 0; i < length && i < 9 && dispCount < 9; i++) {
		// Add space if present
		if (CW_MACRO_HAS_SPACE(data[i]) && dispCount < 9) {
			display[outPos++] = ' ';
			dispCount++;
		}
		// Add character if room; a prosign takes a place per letter
		const char ch = CW_MACRO_GET_CHAR(data[i]);
		if (CW_IS_PROSIGN(ch)) {
			const char   *letters = PROSIGN_LETTERS[ch - CW_PROSIGN_SK];
			const uint8_t n       = (uint8_t)strlen(letters);
			if (dispCount + n > 9)
				break;
			memcpy(display + outPos, letters, n);
			outPos += n;
			dispCount += n;
		} else if (dispCount < 9) {
			display[outPos++] = ch;
			dispCount++;
		}
	}

	// Add newline and char count
	outPos += sprintf_(display + outPos, "\n%u chars", length);

	return outPos;
}

// Decode accumulated pattern into a character
static char CW_DecodePattern(uint16_t pattern, uint8_t length)
{
	if (length == 0 || length > 9)
		return 0;
	
	for (unsigned int i = 0; i < MORSE_TABLE_SIZE; i++) {
		if (MORSE_TABLE[i].length == length && MORSE_TABLE[i].pattern == pattern) {
			return MORSE_TABLE[i].ch;
		}
	}
	
	return 0;  // Unknown pattern
}

// Playback helper: lookup pattern/length for a character
bool CW_GetMorseForChar(char ch, uint8_t *pattern, uint8_t *length)
{
	if (pattern == NULL || length == NULL)
		return false;

	for (unsigned int i = 0; i < MORSE_TABLE_SIZE; i++) {
		if (MORSE_TABLE[i].ch == ch) {
			*pattern = MORSE_TABLE[i].pattern;
			*length = MORSE_TABLE[i].length;
			return true;
		}
	}
	return false;
}

void CW_EncoderProcessElement(CW_ElementType_t element)
{
#if CW_ENCODER_DEBUG
	const char* elem_names[] = {"DIT", "DAH", "INTER_CHAR", "INTER_WORD"};
	if (element <= CW_ELEMENT_INTER_WORD_SPACE) {
		char buf[32];
		sprintf_(buf, "ENC: %s\r\n", elem_names[element]);
		UART_Send(buf, strlen(buf));
	}
#endif

	switch (element) {
	case CW_ELEMENT_DIT:
		// Add a dit (0) to the pattern; 9 elements is the longest character (SOS)
		if (s_encoder_length < 9) {
			// Pattern LSB first, so no shift needed for new bit
			s_encoder_length++;
		} else {
			s_encoder_overflow = true;
		}
		break;

	case CW_ELEMENT_DAH:
		// Add a dah (1) to the pattern
		if (s_encoder_length < 9) {
			s_encoder_pattern |= (uint16_t)(1u << s_encoder_length);
			s_encoder_length++;
		} else {
			s_encoder_overflow = true;
		}
		break;
		
	case CW_ELEMENT_INTER_CHAR_SPACE:
		// End of character - decode and emit
#if CW_ENCODER_DEBUG
		{
			char buf[64];
			sprintf_(buf, "DECODE: pattern=%u len=%d\r\n", s_encoder_pattern, s_encoder_length);
			UART_Send(buf, strlen(buf));
		}
#endif
		if (s_encoder_length > 0) {
			char ch = s_encoder_overflow ? 0 : CW_DecodePattern(s_encoder_pattern, s_encoder_length);
#if CW_ENCODER_DEBUG
			{
				char buf[64];
				sprintf_(buf, "  Result: ch='%c' (%d) valid=%d\r\n", 
					ch ? ch : '?', ch, ch != 0 && CW_ValidateChar(ch));
				UART_Send(buf, strlen(buf));
			}
#endif
			if (ch != 0 && CW_ValidateChar(ch)) {
				const bool can_update_display = !gCW_Recording || (gCW_RecordLength < CW_MACRO_MAX_LEN);
#if CW_ENCODER_DEBUG
				// Print decoded character for debug
				char buf[32];
				if (s_encoder_space_pending) {
					sprintf_(buf, "CW: (space) [%c]\r\n", ch);
				} else {
					sprintf_(buf, "CW: [%c]\r\n", ch);
				}
				UART_Send(buf, strlen(buf));
#endif
				
				// Emit to recorder if recording
				if (gCW_Recording && gCW_RecordLength < CW_MACRO_MAX_LEN) {
					gCW_RecordBuffer[gCW_RecordLength++] = CW_MACRO_ENCODE(ch, s_encoder_space_pending);
					gCW_RecordNewChar = true;
				}
				// Add to TX display buffer unless recording is already full
				if (can_update_display) {
					CW_AddToTxDisplay(ch, s_encoder_space_pending);
				}
#ifdef ENABLE_CODE_PRACTICE
				CPO_Call_OnChar(ch);   // no-op unless the callsign drill is running
#endif
			}
#ifdef ENABLE_CODE_PRACTICE
			else if (gCW_CpoActive) {
				// An unknown pattern (such as the 8-dit error signal), or a run past 9
				// elements (a held paddle), is dropped everywhere else. Practice shows it so a
				// fumble can't pass unseen, and the callsign drill scores it as a miss.
				CW_AddToTxDisplay(CW_CHAR_UNKNOWN, s_encoder_space_pending);
				CPO_Call_OnChar(CW_CHAR_UNKNOWN);
			}
#endif
			
			// Reset for next character
			s_encoder_pattern = 0;
			s_encoder_length = 0;
			s_encoder_space_pending = false;
			s_encoder_overflow = false;
		}
		break;

	case CW_ELEMENT_INTER_WORD_SPACE:
		// Mark that next character should have space before it
		// Also reset encoder state for next character
		s_encoder_pattern = 0;
		s_encoder_length = 0;
		s_encoder_space_pending = true;
		s_encoder_overflow = false;
		break;
	}
}

// Recording functions
void CW_StartRecording(uint8_t macroIndex)
{
	if (macroIndex >= CW_MACRO_COUNT)
		return;
	
	gCW_RecordMacroIndex = macroIndex;
	gCW_RecordLength = 0;
	gCW_RecordNewChar = false;
	gCW_Recording = true;
	CW_ClearTxDisplay();
	
	// Reset encoder state
	s_encoder_pattern = 0;
	s_encoder_length = 0;
	s_encoder_space_pending = false;
	s_encoder_overflow = false;
}

void CW_StopRecording(void)
{
	if (!gCW_Recording)
		return;
	
#if CW_MACRO_DEBUG
	char buf[64];
	sprintf_(buf, "CW_StopRecording: saving %u chars to macro %u\r\n", gCW_RecordLength, gCW_RecordMacroIndex);
	UART_Send(buf, strlen(buf));
#endif
	
	// Save the recorded macro
	CW_SaveMacro(gCW_RecordMacroIndex, (const char *)gCW_RecordBuffer, gCW_RecordLength);
	
#if CW_MACRO_DEBUG
	sprintf_(buf, "CW_StopRecording: save complete\r\n");
	UART_Send(buf, strlen(buf));
#endif
	
	gCW_Recording = false;
	gCW_RecordNewChar = false;
	CW_ClearTxDisplay();
}

// When the buffer is full, drop the oldest character to make room for one more. A
// prosign at the start goes as a whole, so part of it is never left behind.
static void MakeRoom(void)
{
	if (gCW_TX_DisplayIndex < CW_TX_DISPLAY_SIZE - 1)
		return;

	const uint8_t prosign = ProsignLength(gCW_TX_Display);
	const uint8_t drop    = prosign ? prosign : 1;
	memmove(gCW_TX_Display, gCW_TX_Display + drop, CW_TX_DISPLAY_SIZE - 1 - drop);
	gCW_TX_DisplayIndex = CW_TX_DISPLAY_SIZE - 1 - drop;
}

void CW_AddToTxDisplay(char ch, bool hasSpace)
{
	if (CW_IS_PROSIGN(ch)) {
		// its letters in lowercase, which the CW text printers draw underlined
		const char *letters = PROSIGN_LETTERS[ch - CW_PROSIGN_SK];
		CW_AddToTxDisplay(letters[0], hasSpace);
		for (const char *p = letters + 1; *p; p++)
			CW_AddToTxDisplay(*p, false);
		return;
	}

#if CW_ENCODER_DEBUG
	char buf[64];
	sprintf_(buf, "AddToTx: ch='%c' space=%d idx=%d\r\n", ch, hasSpace, gCW_TX_DisplayIndex);
	UART_Send(buf, strlen(buf));
#endif
	
	// Add space first if needed (never as the first character on the line)
	if (hasSpace && gCW_TX_DisplayIndex > 0) {
		MakeRoom();
		gCW_TX_Display[gCW_TX_DisplayIndex++] = ' ';
		gCW_TX_Display[gCW_TX_DisplayIndex] = '\0';
	}
	
	// Add character
	MakeRoom();
	gCW_TX_Display[gCW_TX_DisplayIndex++] = ch;
	gCW_TX_Display[gCW_TX_DisplayIndex] = '\0';
	
	// Trigger main display update
	gUpdateDisplay = true;
	
#if CW_ENCODER_DEBUG
	sprintf_(buf, "  Buffer now: \"%s\" (len=%d)\r\n", gCW_TX_Display, strlen(gCW_TX_Display));
	UART_Send(buf, strlen(buf));
#endif
}

void CW_ClearTxDisplay(void)
{
#if CW_ENCODER_DEBUG
	char buf[64];
	sprintf_(buf, "ClearTxDisplay called\r\n");
	UART_Send(buf, strlen(buf));
#endif
	memset(gCW_TX_Display, 0, CW_TX_DISPLAY_SIZE);
	gCW_TX_DisplayIndex = 0;
}

uint8_t CW_GetTxDisplayTail(char *display, uint8_t maxLen)
{
	if (display == NULL || maxLen == 0)
		return 0;

	const size_t len = strlen(gCW_TX_Display);
	uint8_t copy_len = (len >= (size_t)(maxLen - 1)) ? (uint8_t)(maxLen - 1) : (uint8_t)len;
	size_t idx = (len > copy_len) ? (len - copy_len) : 0;

	// Don't start partway into a prosign. Walk the run of prosign letters the cut
	// falls in, a whole prosign at a time from the run's start, to the first
	// prosign boundary at or after the cut.
	if (IsProsignLetter(gCW_TX_Display[idx])) {
		size_t at = idx;
		while (at > 0 && IsProsignLetter(gCW_TX_Display[at - 1]))
			at--;
		while (at < idx) {
			const uint8_t n = ProsignLength(gCW_TX_Display + at);
			at += n ? n : 1;
		}
		copy_len -= (uint8_t)(at - idx);
		idx = at;
	}

	if (copy_len > 0) {
		memcpy(display, gCW_TX_Display + idx, copy_len);
	}
	display[copy_len] = '\0';
	return copy_len;
}

