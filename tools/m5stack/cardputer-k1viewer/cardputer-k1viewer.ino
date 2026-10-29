/*
 * Standalone K1Viewer client for the M5Stack Cardputer.
 *
 * Copyright 2026 Armel F4HWN
 * https://github.com/armel
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

#include <Arduino.h>
#include <M5Cardputer.h>
#include <USBHost.h>
#include <USBHostSerial.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if ARDUINO_USB_MODE != 0
#error "Select USB Mode: USB-OTG (TinyUSB)"
#endif

#if ARDUINO_USB_CDC_ON_BOOT || ARDUINO_USB_MSC_ON_BOOT || ARDUINO_USB_DFU_ON_BOOT
#error "Disable USB CDC, MSC, and DFU on boot when using USB host mode"
#endif

namespace {

constexpr uint16_t kRadioWidth = 128;
constexpr uint16_t kRadioHeight = 64;
constexpr size_t kRadioFrameBytes = (kRadioWidth * kRadioHeight) / 8;
constexpr size_t kMaxPayload = 2048;

constexpr int16_t kViewWidth = 240;
constexpr int16_t kViewHeight = 120;
constexpr int16_t kViewX = 0;
constexpr int16_t kViewY = 0;
constexpr int16_t kStatusTop = 120;
constexpr int16_t kStatusHeight = 15;
constexpr int16_t kStatusTextY = 4;
constexpr int16_t kStatusLedY = 7;
constexpr int16_t kStatusLedRadius = 5;

constexpr uint32_t kKeepaliveMs = 200;
constexpr uint32_t kFpsSampleMs = 1000;
constexpr uint32_t kUsbTxTimeoutMs = 20;

constexpr uint8_t kTypeScreenshot = 0x01;
constexpr uint8_t kTypeDiff = 0x02;
constexpr uint8_t kTypeKey = 0x03;
constexpr uint8_t kTypeKeyLong = 0x04;
constexpr uint8_t kFlagLedRed = 1u << 1;
constexpr uint8_t kFlagLedGreen = 1u << 2;

constexpr uint8_t kKeyMenu = 0x0A;
constexpr uint8_t kKeyUp = 0x0B;
constexpr uint8_t kKeyDown = 0x0C;
constexpr uint8_t kKeyExit = 0x0D;
constexpr uint8_t kKeyStar = 0x0E;
constexpr uint8_t kKeyF = 0x0F;
constexpr uint8_t kKeySide2 = 0x11;
constexpr uint8_t kKeySide1 = 0x12;

constexpr uint32_t kUiBackground = 0x111411;
constexpr uint32_t kUiForeground = 0xF0F4EE;
constexpr uint32_t kLedRedOn = 0xFF3B30;
constexpr uint32_t kLedRedOff = 0x451311;
constexpr uint32_t kLedGreenOn = 0x34C759;
constexpr uint32_t kLedGreenOff = 0x103818;

constexpr uint8_t kKeepalive[] = {0x55, 0xAA, 0x00, 0x00};

enum class ParserState : uint8_t {
    Seek,
    HeaderAa,
    Header55,
    Type,
    LengthHigh,
    LengthLow,
    Payload,
    End,
};

enum PaletteId : uint8_t {
    PaletteGrey,
    PaletteOrange,
    PaletteBlue,
    PaletteCount,
};

struct Palette {
    uint32_t foreground;
    uint32_t background;
};

struct Parser {
    ParserState state = ParserState::Seek;
    uint8_t flags = 0;
    uint8_t type = 0;
    uint16_t length = 0;
    uint16_t position = 0;
    uint8_t payload[kMaxPayload] = {};
};

constexpr Palette kPalettes[PaletteCount] = {
    {0x2A213F, 0xE5E0EC},
    {0x41310A, 0xFFC125},
    {0x07223A, 0x1C86E4},
};

USBHostSerialClass kK1Serial;
M5Canvas kRadioCanvas(&M5Cardputer.Display);
M5Canvas kStatusCanvas(&M5Cardputer.Display);
Parser kParser;
uint8_t kFramebuffer[kRadioFrameBytes] = {};

bool kUsbHostStarted = false;
bool kConnected = false;
bool kFrameDirty = true;
bool kStatusDirty = true;
uint8_t kFlags = 0;
uint8_t kPalette = PaletteGrey;
uint32_t kFrameCount = 0;
uint32_t kFpsWindowFrameCount = 0;
uint32_t kFpsWindowStart = 0;
uint32_t kNextKeepalive = 0;
uint8_t kFps = 0;

uint16_t rgb565(uint32_t color)
{
    const uint8_t red = (color >> 16) & 0xFF;
    const uint8_t green = (color >> 8) & 0xFF;
    const uint8_t blue = color & 0xFF;

    return static_cast<uint16_t>(((red & 0xF8u) << 8) |
                                 ((green & 0xFCu) << 3) |
                                 (blue >> 3));
}

uint16_t inactiveColor(const Palette &palette)
{
    const uint8_t bgRed = (palette.background >> 16) & 0xFF;
    const uint8_t bgGreen = (palette.background >> 8) & 0xFF;
    const uint8_t bgBlue = palette.background & 0xFF;
    const uint8_t fgRed = (palette.foreground >> 16) & 0xFF;
    const uint8_t fgGreen = (palette.foreground >> 8) & 0xFF;
    const uint8_t fgBlue = palette.foreground & 0xFF;

    const uint8_t red = (bgRed * 95u + fgRed * 5u + 50u) / 100u;
    const uint8_t green = (bgGreen * 95u + fgGreen * 5u + 50u) / 100u;
    const uint8_t blue = (bgBlue * 95u + fgBlue * 5u + 50u) / 100u;
    return rgb565((static_cast<uint32_t>(red) << 16) |
                  (static_cast<uint32_t>(green) << 8) | blue);
}

void resetParser()
{
    kParser.state = ParserState::Seek;
    kParser.flags = 0;
    kParser.type = 0;
    kParser.length = 0;
    kParser.position = 0;
}

void applyFrame()
{
    bool valid = false;

    if (kParser.type == kTypeScreenshot &&
        kParser.length == kRadioFrameBytes) {
        memcpy(kFramebuffer, kParser.payload, kRadioFrameBytes);
        valid = true;
    } else if (kParser.type == kTypeDiff && kParser.length > 0 &&
               (kParser.length % 9u) == 0) {
        valid = true;
        for (uint16_t offset = 0; offset < kParser.length; offset += 9u) {
            if (kParser.payload[offset] >= 128u) {
                valid = false;
                break;
            }
        }

        for (uint16_t offset = 0; valid && offset < kParser.length;
             offset += 9u) {
            const uint8_t chunk = kParser.payload[offset];
            memcpy(&kFramebuffer[static_cast<uint16_t>(chunk) * 8u],
                   &kParser.payload[offset + 1u], 8u);
        }
    }

    if (valid) {
        const bool flagsChanged = kFlags != kParser.flags;
        kFlags = kParser.flags;
        ++kFrameCount;
        kFrameDirty = true;
        if (flagsChanged) {
            kStatusDirty = true;
        }
    }
}

void seekParser(uint8_t byte)
{
    if (byte == 0xFFu) {
        kParser.flags = 0;
        kParser.state = ParserState::HeaderAa;
    } else if ((byte & 0xF0u) == 0xF0u) {
        kParser.flags = byte & 0x0Fu;
        kParser.state = ParserState::HeaderAa;
    } else if (byte == 0xAAu) {
        kParser.flags = 0;
        kParser.state = ParserState::Header55;
    }
}

void pushParser(uint8_t byte)
{
    switch (kParser.state) {
    case ParserState::Seek:
        seekParser(byte);
        break;

    case ParserState::HeaderAa:
        if (byte == 0xAAu) {
            kParser.state = ParserState::Header55;
        } else {
            resetParser();
            seekParser(byte);
        }
        break;

    case ParserState::Header55:
        if (byte == 0x55u) {
            kParser.state = ParserState::Type;
        } else {
            resetParser();
            seekParser(byte);
        }
        break;

    case ParserState::Type:
        kParser.type = byte;
        kParser.state = ParserState::LengthHigh;
        break;

    case ParserState::LengthHigh:
        kParser.length = static_cast<uint16_t>(byte) << 8;
        kParser.state = ParserState::LengthLow;
        break;

    case ParserState::LengthLow:
        kParser.length |= byte;
        kParser.position = 0;
        if (kParser.length > kMaxPayload) {
            resetParser();
        } else if (kParser.length == 0) {
            kParser.state = ParserState::End;
        } else {
            kParser.state = ParserState::Payload;
        }
        break;

    case ParserState::Payload:
        kParser.payload[kParser.position++] = byte;
        if (kParser.position == kParser.length) {
            kParser.state = ParserState::End;
        }
        break;

    case ParserState::End:
        if (byte == 0x0Au) {
            applyFrame();
        }
        resetParser();
        break;
    }
}

void renderRadio()
{
    const Palette &palette = kPalettes[kPalette];
    const uint16_t foreground = rgb565(palette.foreground);
    const uint16_t inactive = inactiveColor(palette);

    kRadioCanvas.fillSprite(inactive);
    for (uint16_t y = 0; y < kRadioHeight; ++y) {
        const int16_t top = (y * kViewHeight) / kRadioHeight;
        const int16_t bottom = ((y + 1) * kViewHeight) / kRadioHeight;
        for (uint16_t x = 0; x < kRadioWidth; ++x) {
            const uint16_t bitIndex = y * kRadioWidth + x;
            if ((kFramebuffer[bitIndex >> 3] &
                 (1u << (bitIndex & 7u))) == 0) {
                continue;
            }

            const int16_t left = (x * kViewWidth) / kRadioWidth;
            const int16_t right = ((x + 1) * kViewWidth) / kRadioWidth;
            kRadioCanvas.fillRect(left, top, right - left, bottom - top,
                                  foreground);
        }
    }
    kRadioCanvas.pushSprite(kViewX, kViewY);
    kFrameDirty = false;
}

void drawLed(int16_t x, uint32_t onColor, uint32_t offColor, bool on)
{
    const uint16_t fill = rgb565(on ? onColor : offColor);
    const uint16_t border = rgb565(onColor);
    kStatusCanvas.fillCircle(x, kStatusLedY, kStatusLedRadius, fill);
    kStatusCanvas.drawCircle(x, kStatusLedY, kStatusLedRadius, border);
}

void renderStatus()
{
    kStatusCanvas.fillSprite(rgb565(kUiBackground));
    kStatusCanvas.setTextFont(1);
    kStatusCanvas.setTextSize(1);
    kStatusCanvas.setTextColor(rgb565(kUiForeground),
                               rgb565(kUiBackground));

    if (!kUsbHostStarted) {
        kStatusCanvas.drawString("USB HOST ERROR", 78, kStatusTextY);
    } else if (!kConnected) {
        kStatusCanvas.drawString("CONNECT K1 USB-C", 69, kStatusTextY);
    } else {
        char fpsText[7];
        snprintf(fpsText, sizeof(fpsText), "%02u FPS",
                 static_cast<unsigned int>(kFps));
        kStatusCanvas.drawString("K1 CONNECTED", 4, kStatusTextY);
        kStatusCanvas.drawString(fpsText, 88, kStatusTextY);
        drawLed(176, kLedRedOn, kLedRedOff,
                (kFlags & kFlagLedRed) != 0);
        drawLed(204, kLedGreenOn, kLedGreenOff,
                (kFlags & kFlagLedGreen) != 0);

    }
    kStatusCanvas.pushSprite(0, kStatusTop);
    kStatusDirty = false;
}

void updateConnection(uint32_t now)
{
    const bool mounted = kUsbHostStarted && kK1Serial.mounted();
    if (mounted == kConnected) {
        return;
    }

    kConnected = mounted;
    resetParser();
    kFlags = 0;
    kFps = 0;
    kFpsWindowStart = now;
    kFpsWindowFrameCount = kFrameCount;
    kStatusDirty = true;

    if (kConnected) {
        // DTR tells the K1 firmware that a USB K1Viewer client is present.
        kK1Serial.setControlLineState(0x0001u);
        kNextKeepalive = now;
    }
}

void serviceUsb(uint32_t now)
{
    if (!kUsbHostStarted) {
        return;
    }

    USBHost.task();
    updateConnection(now);
    if (!kConnected) {
        return;
    }

    uint8_t data[256];
    for (uint8_t pass = 0; pass < 8; ++pass) {
        const size_t count = kK1Serial.read(data, sizeof(data));
        if (count == 0) {
            break;
        }
        for (size_t i = 0; i < count; ++i) {
            pushParser(data[i]);
        }
    }

    if (static_cast<int32_t>(now - kNextKeepalive) >= 0) {
        kK1Serial.write(kKeepalive, sizeof(kKeepalive));
        kK1Serial.flush();
        kNextKeepalive = now + kKeepaliveMs;
    }
}

void updateFps(uint32_t now)
{
    if (!kConnected || now - kFpsWindowStart < kFpsSampleMs) {
        return;
    }

    const uint32_t elapsed = now - kFpsWindowStart;
    const uint32_t frames = kFrameCount - kFpsWindowFrameCount;
    uint32_t fps = (frames * 1000u + elapsed / 2u) / elapsed;
    if (fps > 99u) {
        fps = 99u;
    }

    kFps = static_cast<uint8_t>(fps);
    kFpsWindowStart = now;
    kFpsWindowFrameCount = kFrameCount;
    kStatusDirty = true;
}

void selectPalette(uint8_t palette)
{
    if (palette >= PaletteCount || palette == kPalette) {
        return;
    }

    kPalette = palette;
    kFrameDirty = true;
}

void sendRadioKey(uint8_t keyCode, bool longPress)
{
    if (!kConnected) {
        return;
    }

    const uint8_t packet[] = {
        0xAA, 0x55, longPress ? kTypeKeyLong : kTypeKey, keyCode,
    };
    kK1Serial.write(packet, sizeof(packet));
    kK1Serial.flush();
}

bool handleFnRadioKey(uint8_t rawKey, bool longPress)
{
    switch (rawKey) {
    case '`':
    case KEY_BACKSPACE:
        sendRadioKey(kKeyExit, longPress);
        return true;
    case '1':
        sendRadioKey(kKeySide1, longPress);
        return true;
    case '2':
        sendRadioKey(kKeySide2, longPress);
        return true;
    case '3':
        sendRadioKey(kKeyF, longPress);
        return true;
    case '8':
        sendRadioKey(kKeyStar, longPress);
        return true;
    case ';':
    case ',':
        sendRadioKey(kKeyUp, false);
        return true;
    case '.':
    case '/':
        sendRadioKey(kKeyDown, false);
        return true;
    default:
        return false;
    }
}

bool handleRadioKey(uint8_t rawKey, bool longPress)
{
    if (rawKey >= '0' && rawKey <= '9') {
        sendRadioKey(rawKey - '0', longPress);
        return true;
    }

    switch (rawKey) {
    case 'm':
    case KEY_ENTER:
        sendRadioKey(kKeyMenu, longPress);
        return true;
    case KEY_BACKSPACE:
        sendRadioKey(kKeyExit, longPress);
        return true;
    case 'f':
        sendRadioKey(kKeyF, longPress);
        return true;
    default:
        return false;
    }
}

void updateInput()
{
    M5Cardputer.update();

    if (M5Cardputer.BtnA.wasPressed()) {
        selectPalette((kPalette + 1u) % PaletteCount);
    }

    if (!M5Cardputer.Keyboard.isChange() ||
        !M5Cardputer.Keyboard.isPressed()) {
        return;
    }

    Keyboard_Class::KeysState &state = M5Cardputer.Keyboard.keysState();
    for (const Point2D_t &position : M5Cardputer.Keyboard.keyList()) {
        const uint8_t rawKey =
            M5Cardputer.Keyboard.getKeyValue(position).value_first;

        if (rawKey == KEY_FN || rawKey == KEY_OPT ||
            rawKey == KEY_LEFT_CTRL || rawKey == KEY_LEFT_SHIFT ||
            rawKey == KEY_LEFT_ALT) {
            continue;
        }

        if (state.fn && handleFnRadioKey(rawKey, state.shift)) {
            continue;
        }

        if (!state.fn && handleRadioKey(rawKey, state.shift)) {
            continue;
        }

        if (!state.fn && rawKey == 'g') {
            selectPalette(PaletteGrey);
        } else if (!state.fn && rawKey == 'o') {
            selectPalette(PaletteOrange);
        } else if (!state.fn && rawKey == 'b') {
            selectPalette(PaletteBlue);
        }
    }
}

} // namespace

void setup()
{
    auto config = M5.config();
    M5Cardputer.begin(config, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(128);
    M5Cardputer.Display.fillScreen(rgb565(kUiBackground));

    kRadioCanvas.setColorDepth(16);
    kStatusCanvas.setColorDepth(16);
    if (kRadioCanvas.createSprite(kViewWidth, kViewHeight) == nullptr ||
        kStatusCanvas.createSprite(M5Cardputer.Display.width(),
                                   kStatusHeight) == nullptr) {
        M5Cardputer.Display.setTextColor(TFT_RED, rgb565(kUiBackground));
        M5Cardputer.Display.drawString("DISPLAY BUFFER ERROR", 54, 60);
        while (true) {
            delay(1000);
        }
    }

    kUsbHostStarted = USBHost.begin();
    if (kUsbHostStarted) {
        kK1Serial.setTxTimeoutMs(kUsbTxTimeoutMs);
        kK1Serial.begin(38400);
    }

    resetParser();
    kFpsWindowStart = millis();
}

void loop()
{
    const uint32_t now = millis();
    serviceUsb(now);
    updateInput();
    updateFps(now);

    if (kFrameDirty) {
        renderRadio();
    }
    if (kStatusDirty) {
        renderStatus();
    }

    delay(1);
}

