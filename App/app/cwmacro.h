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

 // CW Macro system
// Storage format: first byte = length (0xff = empty), remaining bytes = characters
// Each character byte: bits 0-6 = character, bit 7 = space before character
// Supported characters: A-Z, 0-9, '/', '?', '.', ',', '=', '-'

#ifndef APP_CWMACRO_H
#define APP_CWMACRO_H

#include <stdint.h>
#include <stdbool.h>

// Keep full 46-character payload and store a checksum byte after the payload
#define CW_MACRO_MAX_LEN 46  // payload bytes
#define CW_MACRO_COUNT 4
// Block layout: [0]=len|SIG, [1..46]=payload bytes (encoded), [47]=checksum
#define CW_MACRO_BLOCK_SIZE 48

// Signature is stored in the length byte (high bit). Checksum is stored in the
// last byte of the block.
#define CW_MACRO_SIG 0x80
#define CW_MACRO_CHECKSUM_OFFSET (CW_MACRO_BLOCK_SIZE - 1)

// Logical EEPROM addresses (eeprom_compat.c maps 0xD000-0xD0FF to flash
// 0x00B000, a sector of its own). CHIRP uses the same addresses.
//   0xD000  16-byte header: 'C','W','M', version, macro count, block size, 0xFF...
//   0xD010  CW_MACRO_COUNT blocks of CW_MACRO_BLOCK_SIZE
//   0xD0D0  spare, room for a 5th macro
#define CW_MACRO_HDR_EEPROM_ADDR 0xD000
#define CW_MACRO_HDR_SIZE        16
#define CW_MACRO_HDR_VERSION     1
#define CW_MACRO_HDR_SIG         "CWM"   // first 3 bytes, no terminator stored
#define CW_MACRO_FLASH_SECTOR    0x00B000

#define CW_MACRO1_EEPROM_ADDR 0xD010
#define CW_MACRO2_EEPROM_ADDR (CW_MACRO1_EEPROM_ADDR + CW_MACRO_BLOCK_SIZE)  /* 0xD040 */
#define CW_MACRO3_EEPROM_ADDR (CW_MACRO2_EEPROM_ADDR + CW_MACRO_BLOCK_SIZE)  /* 0xD070 */
#define CW_MACRO4_EEPROM_ADDR (CW_MACRO3_EEPROM_ADDR + CW_MACRO_BLOCK_SIZE)  /* 0xD0A0 */

// Encoding/Decoding helper macros
#define CW_MACRO_ENCODE(ch, hasSpace) ((hasSpace) ? ((ch) | 0x80) : (ch))
#define CW_MACRO_HAS_SPACE(byte) (((byte) & 0x80) != 0)
#define CW_MACRO_GET_CHAR(byte) ((byte) & 0x7F)

// Prosigns that have no punctuation of their own (BT, AR, KN and AS decode as
// = + ( &). Each is one byte wherever a character goes: the decoder, macros and
// playback, which sends it run together. In the TX display buffer and the macro
// preview it becomes its letters in lowercase, which the CW text printers draw
// as underlined capitals. CHIRP shows it as <SK>.
// New prosigns go on the end: these bytes are stored in macros.
#define CW_PROSIGN_SK  '\x01'  // ...-.-   end of contact
#define CW_PROSIGN_BK  '\x02'  // -...-.-  break
#define CW_PROSIGN_KA  '\x03'  // -.-.-    attention (also CT)
#define CW_PROSIGN_SN  '\x04'  // ...-.    understood (also VE)
#define CW_PROSIGN_UR  '\x05'  // ..-.-.   U and R run together
#define CW_PROSIGN_SOS '\x06'  // ...---... distress, 9 elements
#define CW_IS_PROSIGN(ch) ((ch) >= CW_PROSIGN_SK && (ch) <= CW_PROSIGN_SOS)

// Load a macro from EEPROM into buffer
// Returns actual character count (without considering spaces)
uint8_t CW_LoadMacro(uint8_t macroIndex, char *buffer, uint8_t bufferSize);

// Save a macro to EEPROM from buffer
void CW_SaveMacro(uint8_t macroIndex, const char *buffer, uint8_t length);

// Get the character count for a macro (reads length byte from EEPROM)
uint8_t CW_GetMacroLength(uint8_t macroIndex);

// Payload length of a raw CW_MACRO_BLOCK_SIZE block, or 0 if it is empty or
// fails the signature, length or checksum check
uint8_t CW_MacroBlockLength(const uint8_t *block);

// Macro area header at CW_MACRO_HDR_EEPROM_ADDR
bool CW_MacroHeaderValid(void);
void CW_WriteMacroHeader(void);

// Validate if a character is allowed in CW macros (A-Z, 0-9, + , - . / = ? ( & and the prosigns)
bool CW_ValidateChar(char ch);

// Format macro for display (first N chars)
// Returns number of characters copied
uint8_t CW_FormatMacroDisplay(uint8_t macroIndex, char *display, uint8_t maxChars);

// Playback helper: given a decoded character, return pattern (LSB-first) and element length
// Playback helper: given a decoded character, return pattern (LSB-first) and element length
bool CW_GetMorseForChar(char ch, uint8_t *pattern, uint8_t *length);

// Morse code encoder - receives element events from keyer FSM
typedef enum {
	CW_ELEMENT_DIT = 0,
	CW_ELEMENT_DAH = 1,
	CW_ELEMENT_INTER_CHAR_SPACE = 2,  // End of character
	CW_ELEMENT_INTER_WORD_SPACE = 3   // End of word (space before next char)
} CW_ElementType_t;

// Process a morse code element from the keyer
void CW_EncoderProcessElement(CW_ElementType_t element);

// Recording state management
extern bool gCW_Recording;
extern uint8_t gCW_RecordMacroIndex;  // Which macro (0-3)
extern uint8_t gCW_RecordBuffer[CW_MACRO_MAX_LEN];  // Encoded characters
extern uint8_t gCW_RecordLength;  // Current length
extern bool gCW_RecordNewChar;  // Flag: new character ready for display update

// Playback state (moved from cwkeyer.c static)
extern bool gCW_PlaybackActive;         // true while macro playback is in progress
extern bool gCW_PlaybackRepeat;         // true if playback should repeat after completion
extern uint8_t gCW_PlaybackMacroIndex;  // macro being played (0-3)
extern uint16_t gCW_MessageRepeatCountdown_500ms;  // countdown timer for repeat delay

// Start recording a macro
void CW_StartRecording(uint8_t macroIndex);

// Stop recording and save
void CW_StopRecording(void);

// TX character display buffer (for showing what's being transmitted)
#define CW_TX_DISPLAY_SIZE 16

// Stands in during code practice for keying that decodes to no character. It is
// not a CW character itself, so it never matches anything the operator is copying.
#define CW_CHAR_UNKNOWN '*'
extern char gCW_TX_Display[CW_TX_DISPLAY_SIZE];
extern uint8_t gCW_TX_DisplayIndex;
extern bool gCW_TX_DisplayUpdated;  // Flag: new data needs to be displayed

// Add a decoded character to the TX display buffer
void CW_AddToTxDisplay(char ch, bool hasSpace);

// Clear the TX display buffer
void CW_ClearTxDisplay(void);

// Get last N characters from TX display buffer
// Returns number of characters copied
uint8_t CW_GetTxDisplayTail(char *display, uint8_t maxLen);

#endif
