#!/usr/bin/env python3
"""Small AIS/HDLC reference used by the AIS RX host and Flipper tests.

The bit arrays in this file follow ITU-R M.1371 field numbering: bit zero is
the message-type MSB.  Message bits are packed into bytes most-significant bit
first (bit zero is the MSB of byte zero), then each byte goes on the wire
least-significant bit first, as every HDLC byte, followed by the complemented
CRC-16.  On the air the first data bit after the flag is therefore message bit
7, not bit 0; a receiver that shifts bits in LSB first gets the message bytes
back and reads the fields MSB first (AIS-catcher, gr-ais).  The gpsd AIVDM
example below checks the field layout against a real report.
"""

from __future__ import annotations

FLAG = 0x7E
CRC_GOOD = 0xF0B8
MAX_PAYLOAD_BITS = 424


def put(bits: list[int], start: int, width: int, value: int) -> None:
    if value < 0:
        value += 1 << width
    if value < 0 or value >= 1 << width:
        raise ValueError((start, width, value))
    for offset in range(width):
        bits[start + offset] = (value >> (width - 1 - offset)) & 1


def get(bits: list[int], start: int, width: int, signed: bool = False) -> int:
    value = 0
    for bit in bits[start:start + width]:
        value = (value << 1) | bit
    if signed and value & (1 << (width - 1)):
        value -= 1 << width
    return value


def text_value(char: str) -> int:
    char = char.upper()
    code = ord(char)
    if 64 <= code <= 95:
        return code - 64
    if 32 <= code <= 63:
        return code
    return 32


def put_text(bits: list[int], start: int, chars: int, value: str) -> None:
    value = value.upper()[:chars].ljust(chars, "@")
    for index, char in enumerate(value):
        put(bits, start + 6 * index, 6, text_value(char))


def decode_text(bits: list[int], start: int, chars: int) -> str:
    out = []
    for index in range(chars):
        value = get(bits, start + 6 * index, 6)
        char = chr(value + 64) if value < 32 else chr(value)
        out.append(" " if char == "@" else char)
    return "".join(out).rstrip()


def payload_bytes(bits: list[int]) -> bytes:
    """Message bits (field order) to bytes, MSB first."""
    if len(bits) % 8:
        raise ValueError("AIS test messages must be byte aligned")
    out = bytearray(len(bits) // 8)
    for index, bit in enumerate(bits):
        out[index // 8] |= bit << (7 - (index & 7))
    return bytes(out)


def message_bits(data: bytes) -> list[int]:
    """Bytes back to message bits in field order."""
    return [(byte >> (7 - bit)) & 1 for byte in data for bit in range(8)]


def bytes_bits(data: bytes) -> list[int]:
    """Bytes to wire order: each byte least-significant bit first."""
    return [(byte >> bit) & 1 for byte in data for bit in range(8)]


def armored_bits(payload: str) -> list[int]:
    """NMEA AIVDM six-bit armoring to message bits."""
    bits = []
    for char in payload:
        value = ord(char) - 48
        if value > 40:
            value -= 8
        bits += [(value >> (5 - index)) & 1 for index in range(6)]
    return bits


# gpsd AIVDM reference: type 1, MMSI 477553000, 47.582833 N 122.345833 W,
# COG 51.0, heading 181, second 15.
GPSD_TYPE1 = "177KQJ5000G?tO`K>RA1wUbN0TKH"


def crc_update(crc: int, byte: int) -> int:
    crc ^= byte
    for _ in range(8):
        crc = (crc >> 1) ^ (0x8408 if crc & 1 else 0)
    return crc & 0xFFFF


def append_fcs(payload: bytes) -> bytes:
    crc = 0xFFFF
    for byte in payload:
        crc = crc_update(crc, byte)
    crc ^= 0xFFFF
    return payload + bytes((crc & 0xFF, crc >> 8))


def valid_fcs(frame: bytes) -> bool:
    crc = 0xFFFF
    for byte in frame:
        crc = crc_update(crc, byte)
    return crc == CRC_GOOD


def byte_bits(value: int) -> list[int]:
    return [(value >> bit) & 1 for bit in range(8)]


def stuffed(bits: list[int]) -> list[int]:
    out: list[int] = []
    ones = 0
    for bit in bits:
        out.append(bit)
        if bit:
            ones += 1
            if ones == 5:
                out.append(0)
                ones = 0
        else:
            ones = 0
    return out


def wire_bits(payload: bytes, bad_crc: bool = False) -> list[int]:
    frame = bytearray(append_fcs(payload))
    if bad_crc:
        frame[-1] ^= 0x01
    training = [0, 1] * 12
    flag = byte_bits(FLAG)
    return training + flag + stuffed(bytes_bits(frame)) + flag + [1] * 24


def nrzi_levels(bits: list[int], initial: int = 0) -> list[int]:
    level = initial
    out = []
    for bit in bits:
        if not bit:
            level ^= 1
        out.append(level)
    return out


def _base(message_type: int, mmsi: int, length: int) -> list[int]:
    bits = [0] * length
    put(bits, 0, 6, message_type)
    put(bits, 8, 30, mmsi)
    return bits


def class_a(message_type: int = 1, mmsi: int = 227123456,
            lat: float = 48.8566, lon: float = 2.3522,
            sog: float = 12.3, cog: float | None = 87.6,
            status: int = 0, heading: int | None = None) -> bytes:
    """cog None: not available (3600); heading None: the course, rounded."""
    if message_type not in (1, 2, 3):
        raise ValueError(message_type)
    bits = _base(message_type, mmsi, 168)
    put(bits, 38, 4, status)
    put(bits, 50, 10, round(sog * 10))
    put(bits, 60, 1, 1)
    put(bits, 61, 28, round(lon * 600000))
    put(bits, 89, 27, round(lat * 600000))
    put(bits, 116, 12, 3600 if cog is None else round(cog * 10))
    put(bits, 128, 9, round(cog or 0) if heading is None else heading)
    put(bits, 137, 6, 42)
    put(bits, 148, 1, 1)
    return payload_bytes(bits)


def class_b18(mmsi: int = 244987654, lat: float = 52.37, lon: float = 4.90,
              sog: float = 7.4, cog: float = 271.2) -> bytes:
    bits = _base(18, mmsi, 168)
    put(bits, 46, 10, round(sog * 10))
    put(bits, 56, 1, 1)
    put(bits, 57, 28, round(lon * 600000))
    put(bits, 85, 27, round(lat * 600000))
    put(bits, 112, 12, round(cog * 10))
    put(bits, 124, 9, round(cog))
    put(bits, 133, 6, 17)
    return payload_bytes(bits)


def class_b19(mmsi: int = 244987654, name: str = "FLIPPER CLASS B",
              lat: float = 52.37, lon: float = 4.90,
              sog: float = 7.4, cog: float = 271.2) -> bytes:
    bits = _base(19, mmsi, 312)
    put(bits, 46, 10, round(sog * 10))
    put(bits, 56, 1, 1)
    put(bits, 57, 28, round(lon * 600000))
    put(bits, 85, 27, round(lat * 600000))
    put(bits, 112, 12, round(cog * 10))
    put(bits, 124, 9, round(cog))
    put(bits, 133, 6, 17)
    put_text(bits, 143, 20, name)
    put(bits, 263, 8, 37)
    return payload_bytes(bits)


def static_5(mmsi: int = 227123456, name: str = "F4HWN TEST",
             destination: str = "LE HAVRE") -> bytes:
    bits = _base(5, mmsi, 424)
    put(bits, 40, 30, 1234567)
    put_text(bits, 70, 7, "F4HWN")
    put_text(bits, 112, 20, name)
    put(bits, 232, 8, 36)
    put_text(bits, 302, 20, destination)
    return payload_bytes(bits)


def static_24(mmsi: int = 244987654, name: str = "FLIPPER CLASS B") -> bytes:
    bits = _base(24, mmsi, 160)
    put(bits, 38, 2, 0)
    put_text(bits, 40, 20, name)
    return payload_bytes(bits)


def static_24b(mmsi: int = 244987654, call_sign: str = "F4AIS") -> bytes:
    bits = _base(24, mmsi, 168)
    put(bits, 38, 2, 1)
    put(bits, 40, 8, 37)
    put_text(bits, 48, 3, "OAI")
    put(bits, 66, 4, 1)
    put(bits, 70, 20, 12345)
    put_text(bits, 90, 7, call_sign)
    return payload_bytes(bits)


def inland_8(mmsi: int = 227123456, eni: str = "01823456",
             length: float = 110.0, beam: float = 11.4, draught: float = 2.5,
             hazard: int = 2, loaded: int = 2, shiptype: int = 8021) -> bytes:
    """Inland AIS ship static and voyage data: message 8, DAC 200, FI 10
    (gpsd AIVDM): length and beam in 0.1 m, draught in 0.01 m, hazard 0-3
    blue cones / 4 B-flag / 5 unknown, loaded 1 unloaded / 2 loaded. It
    carries no name: the default MMSI is the Class A fixture's, so message
    5 (static_5) names it."""
    bits = _base(8, mmsi, 168)
    put(bits, 40, 10, 200)
    put(bits, 50, 6, 10)
    put_text(bits, 56, 8, eni)
    put(bits, 104, 13, round(length * 10))
    put(bits, 117, 10, round(beam * 10))
    put(bits, 127, 14, shiptype)
    put(bits, 141, 3, hazard)
    put(bits, 144, 11, round(draught * 100))
    put(bits, 155, 2, loaded)
    return payload_bytes(bits)


def parse(payload: bytes) -> dict[str, object]:
    bits = message_bits(payload)
    message_type = get(bits, 0, 6)
    out: dict[str, object] = {
        "type": message_type,
        "mmsi": get(bits, 8, 30),
    }
    if message_type in (1, 2, 3):
        out.update(status=get(bits, 38, 4), sog=get(bits, 50, 10),
                   cog=get(bits, 116, 12), heading=get(bits, 128, 9),
                   lon=get(bits, 61, 28, True), lat=get(bits, 89, 27, True))
    elif message_type in (18, 19):
        out.update(sog=get(bits, 46, 10), cog=get(bits, 112, 12),
                   heading=get(bits, 124, 9),
                   lon=get(bits, 57, 28, True), lat=get(bits, 85, 27, True))
        if message_type == 19:
            out["name"] = decode_text(bits, 143, 20)
    elif message_type == 5:
        out["name"] = decode_text(bits, 112, 20)
        out["destination"] = decode_text(bits, 302, 20)
    elif message_type == 8 and get(bits, 40, 10) == 200 and get(bits, 50, 6) == 10:
        out.update(eni=decode_text(bits, 56, 8), length=get(bits, 104, 13),
                   beam=get(bits, 117, 10), hazard=get(bits, 141, 3),
                   draught=get(bits, 144, 11), loaded=get(bits, 155, 2))
    elif message_type == 24:
        if get(bits, 38, 2) == 0:
            out["name"] = decode_text(bits, 40, 20)
        else:
            out["call_sign"] = decode_text(bits, 90, 7)
    return out


def test_payloads() -> dict[str, bytes]:
    return {
        "classa": class_a(),
        "type2": class_a(message_type=2),
        "type3": class_a(message_type=3),
        "moored": class_a(mmsi=227555000, lat=48.8606, lon=2.2935, sog=0.0,
                          cog=None, status=5, heading=90),
        "classb": class_b18(),
        "classb19": class_b19(),
        "static5": static_5(),
        "static24": static_24(),
        "static24b": static_24b(),
        "inland": inland_8(),
    }


def self_test() -> None:
    frames = test_payloads()
    for name, payload in frames.items():
        frame = append_fcs(payload)
        assert valid_fcs(frame), name
        assert parse(payload)["type"] == (2 if name == "type2" else
                                           3 if name == "type3" else
                                           18 if name == "classb" else
                                           19 if name == "classb19" else
                                           5 if name == "static5" else
                                           24 if name in ("static24", "static24b") else
                                           8 if name == "inland" else 1)
    assert parse(frames["classa"])["lat"] == round(48.8566 * 600000)
    assert parse(frames["classb19"])["name"] == "FLIPPER CLASS B"
    assert parse(frames["static5"])["destination"] == "LE HAVRE"
    assert parse(frames["static24b"])["call_sign"] == "F4AIS"
    inland = parse(frames["inland"])
    assert (inland["eni"], inland["length"], inland["beam"], inland["draught"],
            inland["hazard"], inland["loaded"]) == ("01823456", 1100, 114, 250, 2, 2), inland
    moored = parse(frames["moored"])
    assert (moored["status"], moored["sog"], moored["cog"], moored["heading"]) == (5, 0, 3600, 90)
    assert len(wire_bits(frames["static5"])) < 600

    gpsd = payload_bytes(armored_bits(GPSD_TYPE1))
    fields = parse(gpsd)
    assert fields["type"] == 1 and fields["mmsi"] == 477553000, fields
    assert fields["lat"] == 28549700 and fields["lon"] == -73407500, fields
    assert fields["cog"] == 510, fields
    # On the air, message bits 7..0 of byte zero come first: the six type
    # bits (000001) are the last six of the first eight wire bits after the
    # two repeat-indicator bits.
    wire = bytes_bits(gpsd)
    assert wire[:8] == [0, 0, 1, 0, 0, 0, 0, 0], wire[:8]
    print("AIS reference: payload, CRC, stuffing and message fields OK")


if __name__ == "__main__":
    self_test()
