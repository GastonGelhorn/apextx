// SPDX-License-Identifier: GPL-2.0-only
// Execute the shipped library and canvas generators without a browser dependency.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const {createHash} = require('node:crypto');
const html = fs.readFileSync(path.join(__dirname, '../apextx-theme-editor.html'), 'utf8');
function section(start, end) {
  const at = html.indexOf(start), until = html.indexOf(end, at);
  assert(at >= 0 && until > at, `Missing source boundary: ${start}`);
  return html.slice(at, until);
}
const context = vm.createContext({});
vm.runInContext([
  section('const LIBRARY_WORDS =', 'let lang ='),
  section('const COLOR_KEYS =', 'let selectedPreset'),
  section('const col =', 'let bgCache = null'),
  'this.library = {LIBRARY_WORDS, COLOR_KEYS, INSTR_KEYS, PRESETS, BG_GROUPS, PATTERNS, state, paintBackground, contrast};',
].join('\n'), context);
const {LIBRARY_WORDS, COLOR_KEYS, INSTR_KEYS, PRESETS, BG_GROUPS, PATTERNS, state, paintBackground, contrast} = context.library;
const newGroups = ['grpMinimal', 'grpModern', 'grpRetro', 'grpDaylight'];
const newPatterns = ['aurora', 'mesh', 'waves', 'diagonal', 'blueprint', 'dots', 'sunset', 'metal', 'stripes', 'orbital', 'dunes', 'opart'];
const paletteKeys = Array.from(COLOR_KEYS, ([key]) => key).sort();
const instrumentKeys = Array.from(INSTR_KEYS, ([key]) => key).sort();
const newThemes = newGroups.flatMap(group => Object.entries(PRESETS[group]));
assert.equal(newThemes.length, 12);
for (const locale of ['en', 'es', 'de', 'cn']) {
  for (const key of [...newGroups, ...newPatterns.map(pattern => `bg_${pattern}`)]) {
    assert.equal(typeof LIBRARY_WORDS[locale][key], 'string', `${locale}: ${key}`);
    assert(LIBRARY_WORDS[locale][key].length > 0);
  }
}
const listedPatterns = BG_GROUPS.flatMap(([, patterns]) => Array.from(patterns));
assert.equal(new Set(listedPatterns).size, listedPatterns.length, 'Background choices must not be duplicated');
for (const pattern of listedPatterns) {
  assert(pattern === 'none' || pattern === 'image' || typeof PATTERNS[pattern] === 'function', `Unknown background: ${pattern}`);
}
for (const group of Object.values(PRESETS)) for (const [name, preset] of Object.entries(group)) {
  assert.deepEqual(Object.keys(preset.colors).sort(), paletteKeys, `${name}: firmware palette keys`);
  assert.deepEqual(Object.keys(preset.nb4).sort(), instrumentKeys, `${name}: firmware instrument keys`);
  for (const color of [...Object.values(preset.colors), ...Object.values(preset.nb4)]) assert.match(color, /^[0-9A-F]{6}$/, name);
  if (preset.look.bg) assert(listedPatterns.includes(preset.look.bg), `${name}: unlisted background`);
}
for (const [name, preset] of newThemes) {
  assert(Buffer.byteLength(name, 'utf8') <= 26, `${name}: firmware theme name limit`);
  assert.equal(preset.look.opa, 100, `${name}: animated instruments use opaque cards`);
  assert.equal(new Set(Object.values(preset.nb4)).size, 4, `${name}: distinguish the instruments`);
  for (const [foreground, background, minimum] of [
    ['PRIMARY1', 'PRIMARY2', 7], ['PRIMARY3', 'PRIMARY2', 4.5], ['QM_FG', 'QM_BG', 7],
  ]) assert(contrast(preset.colors[foreground], preset.colors[background]) >= minimum, `${name}: ${foreground} contrast`);
  for (const [instrument, color] of Object.entries(preset.nb4)) {
    assert(contrast(color, preset.colors.PRIMARY2) >= 3, `${name}: ${instrument} contrast`);
  }
}

function canvas() {
  const commands = [];
  let saves = 0;
  const finite = value => {
    if (Array.isArray(value)) value.forEach(finite);
    else if (typeof value === 'number') assert(Number.isFinite(value), `Non-finite canvas argument: ${value}`);
  };
  const color = value => {
    if (value && value.gradient) return;
    assert.equal(typeof value, 'string');
    if (/^#[0-9A-Fa-f]{6}$/.test(value)) return;
    const match = /^rgba\(([^)]+)\)$/.exec(value);
    assert(match, `Invalid canvas colour: ${value}`);
    const channels = match[1].split(',').map(Number);
    assert.equal(channels.length, 4);
    assert(channels.every(Number.isFinite));
    assert(channels.slice(0, 3).every(channel => channel >= 0 && channel <= 255));
    assert(channels[3] >= 0 && channels[3] <= 1);
  };
  const methods = new Set(['beginPath', 'closePath', 'moveTo', 'lineTo', 'bezierCurveTo', 'quadraticCurveTo', 'fill', 'stroke', 'fillRect', 'strokeRect', 'arc', 'ellipse', 'setLineDash', 'translate', 'rotate', 'save', 'restore']);
  const graphics = new Proxy({}, {
    set(target, key, value) {
      finite(value);
      if (key === 'fillStyle' || key === 'strokeStyle') color(value);
      if (key === 'lineWidth') assert(value > 0);
      target[key] = value; commands.push(['set', key, value]); return true;
    },
    get(target, key) {
      if (key === 'createLinearGradient' || key === 'createRadialGradient') return (...args) => {
        args.forEach(finite);
        if (key === 'createRadialGradient') assert(args[2] >= 0 && args[5] >= 0);
        const gradient = {gradient: key, args, stops: [], addColorStop(offset, value) {
          assert(Number.isFinite(offset) && offset >= 0 && offset <= 1); color(value);
          this.stops.push([offset, value]);
        }};
        commands.push([key, args]); return gradient;
      };
      if (methods.has(key)) return (...args) => {
        args.forEach(finite);
        if (key === 'arc') assert(args[2] >= 0);
        if (key === 'ellipse') assert(args[2] >= 0 && args[3] >= 0);
        if (key === 'save') saves++;
        if (key === 'restore') { saves--; assert(saves >= 0, 'Unbalanced canvas restore'); }
        commands.push([key, ...args]);
      };
      if (Object.hasOwn(target, key)) return target[key];
      throw new Error(`Unexpected canvas method or property: ${String(key)}`);
    },
  });
  return {graphics, commands, verify() { assert.equal(saves, 0, 'Canvas state must be restored'); }};
}
const signatures = new Set();
for (const [name, preset] of newThemes) {
  Object.assign(state, {colors: preset.colors, nb4: preset.nb4});
  // Full radio dimensions and thumbnail dimensions, in both orientations.
  for (const [width, height] of [[320, 480], [480, 320], [32, 48], [48, 32]]) {
    for (const pattern of newPatterns) {
      const first = canvas(), second = canvas();
      paintBackground(first.graphics, width, height, pattern);
      paintBackground(second.graphics, width, height, pattern);
      first.verify(); second.verify();
      assert(first.commands.length > 4, `${name}/${pattern}: background must draw`);
      const fingerprint = commands => createHash('sha256').update(JSON.stringify(commands)).digest('hex');
      assert.equal(fingerprint(first.commands), fingerprint(second.commands), `${name}/${pattern}: deterministic export`);
      if (name === newThemes[0][0] && width === 320) {
        signatures.add(fingerprint(first.commands));
      }
    }
  }
}
assert.equal(signatures.size, newPatterns.length, 'New backgrounds must have distinct geometry');
for (const unknown of ['future-background', '', '__proto__', 'constructor', 'toString']) {
  const fallback = canvas(); paintBackground(fallback.graphics, 320, 480, unknown); fallback.verify();
  assert.deepEqual(fallback.commands, [['set', 'fillStyle', `#${state.colors.SECONDARY3}`], ['fillRect', 0, 0, 320, 480]], 'Unknown backgrounds fall back to the solid screen colour');
}
console.log(`Editor library: ${newThemes.length} new themes, ${newPatterns.length} new backgrounds, contrast, firmware keys, 4 languages, deterministic drawing and both orientations passed.`);
