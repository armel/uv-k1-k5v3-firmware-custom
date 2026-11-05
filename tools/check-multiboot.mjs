// Source-level differential checks; no firmware compilation or hardware access.
// Run from the repository root: rtk run "node tools/check-multiboot.mjs"
// The small C subset used by marker validation and gauge arithmetic is translated
// to JavaScript. This does not validate target alignment or RAM code placement:
// those still require the firmware's static assertions and the linked ELF.
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {readFileSync} from 'node:fs';

const baseline = '0333d2d4c6b5aa9a16ff880ad652382f2f34ba13';
const original = execFileSync('rtk', ['git', 'show', `${baseline}:App/driver/mb_flash.c`],
    {encoding: 'utf8', maxBuffer: 1024 * 1024});
const current = readFileSync('App/driver/mb_flash.c', 'utf8');
const header = readFileSync('App/driver/mb_flash.h', 'utf8');
const strip = text => text.replace(/\/\*[\s\S]*?\*\/|\/\/[^\n]*/g, '');
const constants = {
    MB_STATE_LEGACY_MAGIC: 0x31504d46, MB_STATE_V2_MAGIC: 0x32504d46,
    MB_STATE_MAGIC: 0x33504d46, MB_BANK_COUNT: 5, MB_SLOT_COUNT: 5,
    MB_INT_APP_SIZE: 0x1d800, MB_FLASH_PAGE: 256, MB_PROGRESS_COLS: 118,
    MB_PROGRESS_FIRST_COL: 5, MB_PROGRESS_FILLED: 0x2d,
    MB_MARK_VALID: 0, MB_MARK_LEGACY: 1, MB_MARK_MISSING: 2,
    MB_MARK_CORRUPT: 3, MB_MARK_IO: 4,
};

// Guard the assumptions used by this narrow source interpreter.
for (const [name, value] of Object.entries(constants)) {
    const match = strip(header + current).match(new RegExp(`^#define\\s+${name}\\s+(\\S+)`, 'm'));
    if (name.startsWith('MB_MARK_')) {
        assert.match(header, new RegExp(`${name}\\b`));
    } else {
        assert(match, `Missing constant ${name}`);
        assert.equal(constants[match[1]] ?? Number(match[1].replace(/u$/, '')), value, name);
    }
}
assert.match(strip(header), /MB_MARK_VALID\s*=\s*0,\s*MB_MARK_LEGACY,\s*MB_MARK_MISSING,\s*MB_MARK_CORRUPT,\s*MB_MARK_IO,/);

function block(source, anchor) {
    const start = source.indexOf(anchor);
    assert(start >= 0, `Missing ${anchor}`);
    const open = source.indexOf('{', start);
    let depth = 1;
    let end = open + 1;
    while (depth && end < source.length) {
        if (source[end] === '{') depth++;
        if (source[end] === '}') depth--;
        end++;
    }
    assert.equal(depth, 0);
    return source.slice(open + 1, end - 1);
}

function crc32(bytes, length = bytes.length) {
    let crc = 0xffffffff;
    for (let i = 0; i < length; i++) {
        crc ^= bytes[i];
        for (let bit = 0; bit < 8; bit++)
            crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1));
    }
    return (crc ^ 0xffffffff) >>> 0;
}
assert.equal(crc32(Buffer.from('123456789')), 0xcbf43926);

const stateFields = {
    magic: [0, 4], generation: [4, 4], image_size: [8, 4], image_crc32: [12, 4],
    firmware_slot: [16, 1], slot_inv: [17, 1], config_bank: [18, 1],
    bank_inv: [19, 1], state_crc32: [20, 4],
};
function stateView(bytes) {
    const st = {bytes};
    for (const [field, [offset, size]] of Object.entries(stateFields)) {
        Object.defineProperty(st, field, {
            get: () => size === 4 ? bytes.readUInt32LE(offset) : bytes[offset],
            set: value => size === 4 ? bytes.writeUInt32LE(value >>> 0, offset) : bytes[offset] = value & 255,
        });
    }
    return st;
}

function markerReader(source) {
    let body = block(strip(source), 'static mb_mark_status_t mb_read_state_copy(')
        .replace(/sizeof\(\*st\)/g, '24').replace(/sizeof\(st->state_crc32\)/g, '4')
        .replace(/\(\(const uint8_t \*\)st\)/g, 'st.bytes')
        .replace(/\((?:const )?uint8_t \*\)st/g, 'st.bytes')
        .replace(/st->/g, 'st.')
        .replace(/\(uint8_t\)~([\w.]+)/g, '($1 ^ 255)')
        .replace(/const (?:uint8_t|bool) /g, 'const ')
        .replace(/\b(0x[\da-fA-F]+|\d+)u\b/g, '$1');
    assert(!/->|sizeof|uint\d+_t/.test(body), 'Unsupported C construct');
    const execute = Function('input', 'readError', 'st', 'MB_Crc32Bytes', 'memset',
        ...Object.keys(constants), `
        let mb_spi_err = 0;
        const base = 0;
        function mb_ext_read(address, bytes, size) {
            bytes.set(input.subarray(0, size));
            mb_spi_err = Number(readError);
        }
        ${body}`);
    const st = stateView(Buffer.alloc(24));
    return (input, readError) => ({
        status: execute(input, readError, st, crc32,
            (target, value, size) => target.bytes.fill(value, 0, size), ...Object.values(constants)),
        bytes: st.bytes,
    });
}

const before = markerReader(original);
const after = markerReader(current);
let markerCases = 0;
function compare(bytes, readError = false) {
    const a = before(bytes, readError);
    const b = after(bytes, readError);
    assert.equal(b.status, a.status, `Status mismatch: ${bytes.toString('hex')}`);
    assert(a.bytes.equals(b.bytes), `Record mismatch: ${bytes.toString('hex')}`);
    markerCases++;
}
function makeRecord(magic) {
    const bytes = Buffer.alloc(24);
    const st = stateView(bytes);
    Object.assign(st, {magic, generation: 123, image_size: 65536, image_crc32: 0x12345678,
        firmware_slot: 2, slot_inv: 253, config_bank: 3, bank_inv: 252});
    if (magic === constants.MB_STATE_LEGACY_MAGIC) {
        bytes[4] = 2;
        bytes[5] = 253;
    }
    return bytes;
}
function checksum(bytes) { bytes.writeUInt32LE(crc32(bytes, 20), 20); }

// Exhaust all index/complement pairs, including every FMP2 reserved-byte pair.
for (const magic of [constants.MB_STATE_LEGACY_MAGIC, constants.MB_STATE_V2_MAGIC, constants.MB_STATE_MAGIC]) {
    const bytes = makeRecord(magic);
    for (const offset of magic === constants.MB_STATE_LEGACY_MAGIC ? [4] : [16, 18]) {
        for (let index = 0; index < 256; index++) for (let inverse = 0; inverse < 256; inverse++) {
            bytes[offset] = index;
            bytes[offset + 1] = inverse;
            checksum(bytes);
            compare(bytes);
        }
        // Restore valid fields before exercising a different pair.
        makeRecord(magic).copy(bytes);
    }
}

for (const magic of [0, 0xffffffff, constants.MB_STATE_LEGACY_MAGIC, constants.MB_STATE_V2_MAGIC, constants.MB_STATE_MAGIC]) {
    for (const size of [0, 1, 255, 256, 257, 0x1d7ff, 0x1d800, 0x1d801, 0xffffffff]) {
        const bytes = makeRecord(magic);
        bytes.writeUInt32LE(size, 8);
        checksum(bytes);
        compare(bytes);
        compare(bytes, true);
        for (let byte = 0; byte < 24; byte++) for (let bit = 0; bit < 8; bit++) {
            bytes[byte] ^= 1 << bit;
            compare(bytes);
            bytes[byte] ^= 1 << bit;
        }
    }
}

// Execute the actual old/new gauge blocks with RAM arrays and a mocked LCD.
function gaugeStep(source) {
    const body = block(block(strip(source), 'static void MB_RamReflash('), 'if (lcdEnabled)')
        .replace(/\b(\d+)u\b/g, '$1');
    return Function('state', 'mb_ram_progress_blit', ...Object.keys(constants), `
        let {pagesDone, progressAccumulator, progressFilled, lcdEnabled, regionRemaining, progressLine} = state;
        if (lcdEnabled) { ${body} }
        Object.assign(state, {pagesDone, progressAccumulator, progressFilled, lcdEnabled});`);
}
const oldGauge = gaugeStep(original);
const newGauge = gaugeStep(current);
let gaugeCases = 0;
for (const enabled of [false, true]) for (let failPage = 0; failPage <= 472; failPage++) {
    const init = () => ({pagesDone: 0, progressAccumulator: 0, progressFilled: 0,
        lcdEnabled: enabled, progressLine: Buffer.alloc(128, 0x21)});
    const a = init(), b = init();
    for (let page = 1; page <= 472; page++) {
        a.regionRemaining = b.regionRemaining = (473 - page) * 256;
        let oldCalls = 0, newCalls = 0;
        oldGauge(a, () => { oldCalls++; return page !== failPage; }, ...Object.values(constants));
        newGauge(b, () => { newCalls++; return page !== failPage; }, ...Object.values(constants));
        assert.equal(newCalls, oldCalls);
        assert.equal(b.lcdEnabled, a.lcdEnabled);
        assert.equal(b.pagesDone, a.pagesDone);
        assert(a.progressLine.equals(b.progressLine), `Gauge mismatch at page ${page}`);
        assert.equal(b.progressLine[4], 0x21);
        assert.equal(b.progressLine[123], 0x21);
        gaugeCases++;
    }
}

console.log(`Marker status and all 24 output bytes: ${markerCases} equivalent cases.`);
console.log(`Gauge bytes, LCD calls and failure handling: ${gaugeCases} equivalent steps.`);
console.log('No build performed. Check layout assertions and RAM-only call targets in the next Fusion ELF.');
