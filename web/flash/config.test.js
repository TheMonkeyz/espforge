// Unit tests of the pure functions in web/flash/config.js, the "Configure over USB" card: the console line parser,
// the cfg answers (schema, values, ok, error, a firmware without them), the form model built from a schema, and the
// `cfg set` commands (checks, %XX escapes, the console's 119-byte line). No browser, no dependencies:
//   node --test web/flash/config.test.js
'use strict';
const test = require('node:test');
const assert = require('node:assert');
const path = require('node:path');
const c = require(path.join(__dirname, 'config.js'));

// What the starter (main/main.c) answers, as forge_core's cfgcon.c writes it (tests/host/test_cfgcon.c checks that side)
const SCHEMA = '{"app":"espforge","version":"v0.7.0-rc.1","fields":[' +
  '{"key":"lang","label":"Language","type":"select","help":"on the display","options":[{"value":"en","label":"English"},' +
  '{"value":"fr","label":"Français"}]},' +
  '{"key":"channel","label":"Update channel","type":"select","options":[{"value":"stable","label":"Stable"},' +
  '{"value":"beta","label":"Beta (release candidates)"}]},' +
  '{"key":"dim","label":"Screen dimming","type":"bool"},' +
  '{"key":"brightness","label":"Brightness","type":"int","min":5,"max":100,"step":5,"unit":"%"},' +
  '{"key":"name","label":"Name","type":"text","max":16},' +
  '{"key":"color","label":"Colour","type":"color"},' +
  '{"key":"ip","label":"Address","type":"info"}]}';
const VALUES = '{"app":"espforge","version":"v0.7.0-rc.1","lang":"fr","channel":"beta","dim":true,"brightness":80,' +
  '"name":"Salle \\"A\\"","color":"#ff8000","ip":"no Wi-Fi"}';

test('console lines: the ESP-IDF prefix, colour codes and CR', () => {
  assert.deepStrictEqual(c.parseLine('I (1234) test: cfg-schema ' + SCHEMA), { tag: 'test', text: 'cfg-schema ' + SCHEMA });
  assert.deepStrictEqual(c.parseLine('W (99) test: error cfg brightness out of range'),
    { tag: 'test', text: 'error cfg brightness out of range' });
  assert.deepStrictEqual(c.parseLine('\x1b[0;32mI (7) test: ok cfg dim\x1b[0m\r'), { tag: 'test', text: 'ok cfg dim' });
  assert.deepStrictEqual(c.parseLine('ESP-ROM:esp32s3-20210327'), { tag: null, text: 'ESP-ROM:esp32s3-20210327' });
  assert.strictEqual(c.parseLine('  \r'), null);
});

test('lines out of a stream keep the partial last one', () => {
  assert.deepStrictEqual(c.splitLines('I (1) te', 'st: ok cfg dim\r\nI (2) test: cfg {"a"'),
    { lines: ['I (1) test: ok cfg dim'], rest: 'I (2) test: cfg {"a"' });
  assert.deepStrictEqual(c.splitLines('', 'a\n'), { lines: ['a'], rest: '' });
});

test('answers: schema, values, ok, error', () => {
  const s = c.parseAnswer('cfg-schema ' + SCHEMA);
  assert.strictEqual(s.kind, 'schema'); assert.strictEqual(s.obj.app, 'espforge'); assert.strictEqual(s.obj.fields.length, 7);
  const v = c.parseAnswer('cfg ' + VALUES);
  assert.strictEqual(v.kind, 'values'); assert.strictEqual(v.obj.name, 'Salle "A"'); assert.strictEqual(v.obj.dim, true);
  assert.deepStrictEqual(c.parseAnswer('ok cfg dim'), { kind: 'ok', key: 'dim' });
  assert.deepStrictEqual(c.parseAnswer('error cfg brightness not a step of the range'),
    { kind: 'error', key: 'brightness', why: 'not a step of the range' });
  assert.deepStrictEqual(c.parseAnswer('error cfg: expected cfg, cfg schema or cfg set KEY VALUE'),
    { kind: 'error', key: null, why: 'expected cfg, cfg schema or cfg set KEY VALUE' });
});

test('answers of an older firmware', () => {
  // no cfg command at all
  assert.deepStrictEqual(c.parseAnswer("error unknown command 'cfg' (help lists them)"), { kind: 'nocfg' });
  // ColorMesh v0.1-v0.3's cfg (no schema): it answers "cfg schema" with its values, which are not a schema
  assert.strictEqual(c.parseAnswer('cfg {"color":"#ff8000","dev":false}').kind, 'values');
  // a cut line (a log line from another task in the middle, or a lost byte)
  assert.deepStrictEqual(c.parseAnswer('cfg-schema {"app":"x","fields":[{"key"'), { kind: 'broken', what: 'schema' });
  assert.deepStrictEqual(c.parseAnswer('cfg-schema {"app":"x"}'), { kind: 'broken', what: 'schema' });
  assert.deepStrictEqual(c.parseAnswer('cfg {"a":'), { kind: 'broken', what: 'values' });
  assert.strictEqual(c.parseAnswer('pong v0.7.0'), null);
  assert.strictEqual(c.parseAnswer('cfg'), null);
});

test('form model from the schema', () => {
  const m = c.formModel(c.parseAnswer('cfg-schema ' + SCHEMA).obj);
  assert.deepStrictEqual(m.map(f => f.type), ['select', 'select', 'bool', 'int', 'text', 'color', 'info']);
  assert.deepStrictEqual(m[0], { key: 'lang', label: 'Language', help: 'on the display', type: 'select',
    options: [{ value: 'en', label: 'English' }, { value: 'fr', label: 'Français' }] });
  assert.deepStrictEqual(m[3], { key: 'brightness', label: 'Brightness', help: '', type: 'int', min: 5, max: 100, step: 5,
    unit: '%' });
  assert.strictEqual(m[4].max, 16);
});

test('form model: what a page cannot draw is read-only or dropped', () => {
  const m = c.formModel({ fields: [
    { key: 'Bad Key', type: 'bool' }, { type: 'bool' }, null, 'x',                     // dropped
    { key: 'a', type: 'slider' },                                                    // unknown type: read-only
    { key: 'b', type: 'select', options: [] },                                       // nothing to pick: read-only
    { key: 'c', type: 'int', min: 10, max: 5 },                                      // upside down: read-only
    { key: 'd', type: 'int' },                                                       // defaults
    { key: 'e', type: 'select', options: [{ value: 'x' }, { label: 'no value' }] },  // the label defaults to the value
    { key: 'a', type: 'bool' },                                                      // a key twice: the first kept
  ] });
  assert.deepStrictEqual(m.map(f => f.key + ':' + f.type), ['a:info', 'b:info', 'c:info', 'd:int', 'e:select']);
  assert.deepStrictEqual([m[3].min, m[3].max, m[3].step, m[3].unit, m[3].label], [0, 100, 1, '', 'd']);
  assert.deepStrictEqual(m[4].options, [{ value: 'x', label: 'x' }]);
  assert.deepStrictEqual(c.formModel(null), []);
  assert.deepStrictEqual(c.formModel({ fields: 'no' }), []);
});

test('values as the form shows them', () => {
  const m = c.formModel(c.parseAnswer('cfg-schema ' + SCHEMA).obj), v = c.parseAnswer('cfg ' + VALUES).obj;
  assert.deepStrictEqual(m.map(f => c.shownValue(f, v[f.key])), ['fr', 'beta', true, 80, 'Salle "A"', '#ff8000', 'no Wi-Fi']);
  const by = k => m.find(f => f.key === k);
  assert.strictEqual(c.shownValue(by('dim'), 'yes'), null);           // a wrong type: the control stays as it is
  assert.strictEqual(c.shownValue(by('brightness'), '80'), null);
  assert.strictEqual(c.shownValue(by('color'), 'FF8000'), '#ff8000');
  assert.strictEqual(c.shownValue(by('color'), '#fff'), null);
  assert.strictEqual(c.shownValue(by('lang'), undefined), null);
  assert.strictEqual(c.shownValue(by('ip'), 42), '42');
  assert.strictEqual(c.shownValue(by('ip'), undefined), null);
});

test('set commands, checked as the firmware does', () => {
  const m = c.formModel(c.parseAnswer('cfg-schema ' + SCHEMA).obj), by = k => m.find(f => f.key === k);
  assert.deepStrictEqual(c.setCommand(by('dim'), true), { cmd: 'cfg set dim on' });
  assert.deepStrictEqual(c.setCommand(by('dim'), false), { cmd: 'cfg set dim off' });
  assert.deepStrictEqual(c.setCommand(by('brightness'), '55'), { cmd: 'cfg set brightness 55' });
  assert.deepStrictEqual(c.setCommand(by('brightness'), 100), { cmd: 'cfg set brightness 100' });
  assert.ok(c.setCommand(by('brightness'), '52').error.startsWith('not a step'));
  assert.ok(c.setCommand(by('brightness'), '0').error.startsWith('out of range'));
  assert.ok(c.setCommand(by('brightness'), '105').error.startsWith('out of range'));
  assert.ok(c.setCommand(by('brightness'), '5.5').error.startsWith('expected'));
  assert.deepStrictEqual(c.setCommand(by('lang'), 'en'), { cmd: 'cfg set lang en' });
  assert.ok(c.setCommand(by('lang'), 'de').error);
  assert.deepStrictEqual(c.setCommand(by('color'), '#00FF80'), { cmd: 'cfg set color 00ff80' });
  assert.ok(c.setCommand(by('color'), 'orange').error);
  assert.deepStrictEqual(c.setCommand(by('ip'), 'x'), { error: 'read-only' });
});

test('text values: spaces and % escaped, UTF-8 counted in bytes, empty allowed', () => {
  const name = c.formModel(c.parseAnswer('cfg-schema ' + SCHEMA).obj).find(f => f.key === 'name');
  assert.deepStrictEqual(c.setCommand(name, 'Salle de  bain'), { cmd: 'cfg set name Salle%20de%20%20bain' });
  assert.deepStrictEqual(c.setCommand(name, '100%'), { cmd: 'cfg set name 100%25' });
  assert.deepStrictEqual(c.setCommand(name, 'Cuisine é'), { cmd: 'cfg set name Cuisine%20é' });
  assert.deepStrictEqual(c.setCommand(name, ''), { cmd: 'cfg set name' });      // cfgcon: an empty text
  assert.ok(c.setCommand(name, 'sixteen_chars_ab').cmd);
  assert.ok(c.setCommand(name, 'seventeen_chars_a').error.startsWith('too long'));
  assert.ok(c.setCommand(name, 'éééééééé' + 'x').error.startsWith('too long'));   // 17 bytes
  assert.ok(c.setCommand(name, 'a\tb').error);
  assert.strictEqual(c.encodeText('a b%c\x7f'), 'a%20b%25c%7F');
});

test('a command never goes over the console line (119 bytes)', () => {
  const long = { key: 'note', type: 'text', max: 64 };
  assert.ok(c.setCommand(long, 'x'.repeat(64)).cmd);                              // 13 + 64 bytes
  const r = c.setCommand(long, ' '.repeat(40));                                   // 40 spaces -> 120 bytes of %20
  assert.strictEqual(r.error, 'too long for the console line');
  assert.strictEqual(c.LINE_MAX, 119);
});
