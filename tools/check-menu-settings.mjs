// Source-level regression check only: does not compile or execute firmware.
// Run from the repository root: rtk run "node tools/check-menu-settings.mjs"
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {execFileSync} from 'node:child_process';

const baseline = 'f7004b871abbe296981c5a8b990718e8243262d6';
const original = execFileSync('rtk', ['run', `git show ${baseline}:App/app/menu.c`], {encoding: 'utf8'});
const current = readFileSync('App/app/menu.c', 'utf8');
const definitions = readFileSync('App/app/menu_settings.def', 'utf8');
const presets = JSON.parse(readFileSync('CMakePresets.json', 'utf8')).configurePresets;
function preset(name) {
    const p = presets.find(p => p.name === name);
    assert(p, `Missing preset ${name}`);
    const parents = p.inherits ? [p.inherits].flat() : [];
    return Object.assign({}, ...parents.map(preset), p.cacheVariables);
}
const flags = preset('Fusion');
const strip = text => text.replace(/"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|\/\*[\s\S]*?\*\/|\/\/[^\n]*/g,
    token => token.startsWith('/') ? '' : token);
const compact = text => strip(text).replace(/\s+/g, '');

// Evaluate feature guards, not C code. Unsupported expressions fail closed.
function condition(expression) {
    const value = expression.replace(/defined\s*\(?\s*(\w+)\s*\)?/g,
        (_, name) => flags[name] === true ? '1' : '0')
        .replace(/\b[A-Za-z_]\w*\b/g, name => flags[name] === true ? '1' : '0');
    assert(/^[\d\s!&|()=<>+*-]+$/.test(value), `Unsupported guard: ${expression}`);
    return Boolean(Function(`return (${value});`)());
}
function enabled(text) {
    const stack = [];
    let active = true;
    const output = [];
    for (const line of strip(text).split('\n')) {
        const directive = /^\s*#(ifdef|ifndef|if|elif|else|endif)\b\s*(.*)/.exec(line);
        if (!directive) { if (active) output.push(line); continue; }
        const [, kind, expression] = directive;
        if (kind === 'if' || kind === 'ifdef' || kind === 'ifndef') {
            const test = kind === 'if' ? condition(expression) :
                (flags[expression.trim()] === true) !== (kind === 'ifndef');
            stack.push({parent: active, taken: test});
            active = active && test;
        } else {
            const frame = stack.at(-1);
            assert(frame, 'Unbalanced feature guards');
            if (kind === 'endif') { active = frame.parent; stack.pop(); }
            else {
                const test = kind === 'else' || condition(expression);
                active = frame.parent && !frame.taken && test;
                frame.taken ||= test;
            }
        }
    }
    assert.equal(stack.length, 0, 'Unclosed feature guard');
    return output.join('\n');
}
function section(text, from, to) {
    const start = text.indexOf(from);
    const end = text.indexOf(to, start + from.length);
    assert(start >= 0 && end > start, `Missing section ${from}: start=${start}, end=${end}`);
    return text.slice(start, end);
}
function cases(text) {
    const result = new Map();
    const labels = [...text.matchAll(/case\s+(MENU_\w+)\s*:/g)];
    for (let i = 0; i < labels.length; i++) {
        let j = i;
        let body;
        do {
            body = text.slice(labels[j].index + labels[j][0].length,
                j + 1 < labels.length ? labels[j + 1].index : text.length);
            j++;
        } while (!body.trim() && j < labels.length);
        result.set(labels[i][1], body.trim());
    }
    return result;
}
const ranges = [
    ['int MENU_GetLimits', 'void MENU_AcceptSetting'],
    ['void MENU_AcceptSetting', 'static void MENU_ClampSelection'],
    ['void MENU_ShowCurrentSetting', 'static KEY_Code_t edit_last_key']
];
const before = ranges.map(([a,b]) => cases(section(enabled(original), a, b)));
const after = ranges.map(([a,b]) => cases(section(enabled(current), a, b)));
const entries = [...enabled(definitions).matchAll(/^MENU_SETTING\((MENU_\w+),\s*([^,]+),\s*([^,]+),\s*(.+)\)$/gm)];
const migrated = new Set(entries.map(e => e[1]));
assert.equal(migrated.size, entries.length, 'Duplicate descriptor');

for (const [, id, value, minimum, maximum] of entries) {
    const limits = before[0].get(id);
    assert(limits, `Missing baseline limits for ${id}`);
    const min = /\*pMin\s*=\s*([^;]+);/.exec(limits)?.[1] ?? '0';
    const max = /\*pMax\s*=\s*([^;]+);/.exec(limits)?.[1];
    assert.equal(compact(minimum), compact(min), `${id}: minimum changed`);
    assert.equal(compact(maximum), compact(max), `${id}: maximum changed`);
    assert.equal(compact(before[1].get(id)), compact(`${value} = gSubMenuSelection; break;`),
        `${id}: original write has additional behavior`);
    assert.match(compact(before[2].get(id)),
        new RegExp(`^gSubMenuSelection=${value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')};(?:break|return);$`),
        `${id}: original read has additional behavior`);
    for (const map of after) assert(!map.has(id), `${id}: stale switch entry`);
}

let preserved = 0;
for (let i = 0; i < before.length; i++) {
    for (const [id, body] of before[i]) {
        if (migrated.has(id)) continue;
        assert(after[i].has(id), `${id}: non-migrated case removed`);
        assert.equal(compact(after[i].get(id)), compact(body), `${id}: special behavior changed`);
        preserved++;
    }
}
assert(current.includes('gRequestSaveSettings = true;\n        return;'), 'Missing generic save request');
assert(current.includes('*setting->value = (unsigned char)gSubMenuSelection;'), 'Missing byte write');
assert(compact(current).includes(compact(`
    if (gSubMenuSelection < setting->minimum)
        gSubMenuSelection = setting->minimum;
    else if (gSubMenuSelection > setting->maximum)
        gSubMenuSelection = setting->maximum;
    *setting->value = (unsigned char)gSubMenuSelection;
    gRequestSaveSettings = true;
    return;
`)), 'Generic write must clamp before saving and returning');
assert(compact(current).includes(compact(`
    *pMin = setting->minimum;
    *pMax = setting->maximum;
    return 0;
`)), 'Generic limits contract changed');
assert(compact(current).includes(compact(`
    gSubMenuSelection = *setting->value;
    return;
`)), 'Generic read contract changed');
console.log(`PASS: ${entries.length} Fusion descriptors match the reference; ${preserved} special cases unchanged.`);
console.log('Source equivalence check only; firmware compilation and device validation are still required.');
