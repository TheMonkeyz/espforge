// Configure over USB: the flasher page's card reads and changes a board's settings through Web Serial (Chrome / Edge
// on a computer, https or localhost) at 115200 baud, using the firmware's test console (docs/PROTOCOL.md §2,
// forge_core's cfgcon.h): `cfg schema` answers one line `test: cfg-schema {"app","version","fields":[...]}`, `cfg`
// one line `test: cfg {"app","version","<key>":<value>,...}`, and `cfg set <key> <value>` answers `test: ok cfg <key>`
// or `test: error cfg <key> <why>`. The form is built from the schema alone: no app-specific code here, so every
// project gets the card, with or without Wi-Fi (from esp32-s3-colormesh's card, October 31).
// The console's answers are ESP-IDF log lines (`I (1234) test: ...`): the parser tolerates that prefix, colour codes
// and CRLF. The pure functions are exported for web/flash/config.test.js (node --test, no dependencies); the DOM part
// runs only in a browser.
'use strict';

const LINE_MAX = 119;          // the console reads lines of 119 bytes at most (forge_core testcon.c)
const SCHEMA_WAIT_MS = 2000;   // an older firmware doesn't answer `cfg schema`
const TYPES = ['text', 'bool', 'int', 'select', 'color', 'info'];

// "I (1234) test: cfg {...}" -> {tag: "test", text: "cfg {...}"}; "test: ok" too; "plain" -> {tag: null, text}
const TAG_RE = /^(?:[IWEDV] \(\d+\) )?([a-z_]+): (.*)$/;

function parseLine(raw) {
  const line = String(raw).replace(/\x1b\[[0-9;]*m/g, '').replace(/\r$/, '').trim();
  if (!line) return null;
  const m = TAG_RE.exec(line);
  return m ? { tag: m[1], text: m[2] } : { tag: null, text: line };
}

// Whole lines out of a stream: the partial last line comes back as `rest` for the next chunk
function splitLines(rest, chunk) {
  const parts = (rest + chunk).split(/\r?\n/);
  return { lines: parts.slice(0, -1), rest: parts[parts.length - 1] };
}

function jsonObject(s) {
  const start = s.indexOf('{'), end = s.lastIndexOf('}');
  if (start < 0 || end <= start) return null;
  try {
    const o = JSON.parse(s.slice(start, end + 1));
    return o && typeof o === 'object' && !Array.isArray(o) ? o : null;
  } catch (e) { return null; }
}

// What a "test:" line's text says about cfg:
//   {kind: "schema", obj} | {kind: "values", obj} | {kind: "ok", key} | {kind: "error", key|null, why}
//   {kind: "nocfg"} (a firmware without the command) | {kind: "broken", what} (a cut or garbled JSON line) | null
function parseAnswer(text) {
  let m;
  if ((m = /^cfg-schema\s+(.*)$/.exec(text))) {
    const o = jsonObject(m[1]);
    return o && Array.isArray(o.fields) ? { kind: 'schema', obj: o } : { kind: 'broken', what: 'schema' };
  }
  if ((m = /^cfg\s+(\{.*)$/.exec(text))) {
    const o = jsonObject(m[1]);
    return o ? { kind: 'values', obj: o } : { kind: 'broken', what: 'values' };
  }
  if ((m = /^ok cfg (\S+)/.exec(text))) return { kind: 'ok', key: m[1] };
  if ((m = /^error cfg (\S+) (.*)$/.exec(text))) return { kind: 'error', key: m[1], why: m[2] };
  if ((m = /^error cfg:\s*(.*)$/.exec(text))) return { kind: 'error', key: null, why: m[1] };
  if (/^error unknown command 'cfg'/.test(text)) return { kind: 'nocfg' };
  return null;
}

const num = (v, d) => (typeof v === 'number' && Number.isFinite(v) ? Math.trunc(v) : d);
const str = (v, d) => (typeof v === 'string' ? v : d);

// The schema's fields, checked and completed: what the page draws. A field without a usable key is dropped; an
// unknown type, a select without options or an int range upside down is shown read-only (a newer firmware's type).
function formModel(schema) {
  const out = [], seen = new Set();
  const list = schema && Array.isArray(schema.fields) ? schema.fields : [];
  for (const f of list) {
    if (!f || typeof f.key !== 'string' || !/^[a-z0-9_]{1,16}$/.test(f.key) || seen.has(f.key)) continue;
    seen.add(f.key);
    const m = { key: f.key, label: str(f.label, f.key), help: str(f.help, ''),
                type: TYPES.includes(f.type) ? f.type : 'info' };
    if (m.type === 'int') {
      m.min = num(f.min, 0); m.max = num(f.max, 100); m.step = Math.max(1, num(f.step, 1)); m.unit = str(f.unit, '');
      if (m.max < m.min) m.type = 'info';
    } else if (m.type === 'select') {
      m.options = (Array.isArray(f.options) ? f.options : [])
        .filter(o => o && typeof o.value === 'string')
        .map(o => ({ value: o.value, label: str(o.label, o.value) }));
      if (!m.options.length) m.type = 'info';
    } else if (m.type === 'text') {
      m.max = Math.max(0, num(f.max, 64));
    }
    out.push(m);
  }
  return out;
}

// "#FF8000" or "ff8000" -> "ff8000"; anything else null
function hexColor(v) {
  const m = /^#?([0-9a-fA-F]{6})$/.exec(String(v === undefined || v === null ? '' : v).trim());
  return m ? m[1].toLowerCase() : null;
}

// A value from the `cfg` line as the form shows it: bool true/false, int a number, color "#rrggbb", the others a
// string; null when the line has no usable value for the field (the control is then left as it is)
function shownValue(field, v) {
  switch (field.type) {
    case 'bool': return typeof v === 'boolean' ? v : null;
    case 'int': return typeof v === 'number' && Number.isFinite(v) ? v : null;
    case 'color': { const h = hexColor(v); return h ? '#' + h : null; }
    case 'select': case 'text': return typeof v === 'string' ? v : null;
    default:
      if (v === undefined || v === null) return null;
      return typeof v === 'object' ? JSON.stringify(v) : String(v);
  }
}

const utf8Len = s => new TextEncoder().encode(s).length;

// A text for the command line: the console splits at spaces and tabs, so they (and "%", control characters) go as
// %XX, which cfgcon decodes; other characters (UTF-8 included) as they are
function encodeText(s) {
  return String(s).replace(/[\x00-\x20%\x7f]/g, c => '%' + c.charCodeAt(0).toString(16).toUpperCase().padStart(2, '0'));
}

// The console command for a new value from the form: {cmd} or {error} (checked as the firmware does, so a bad value
// is said at once, and a command over the console's 119 bytes is never sent cut)
function setCommand(field, input) {
  let v;
  switch (field.type) {
    case 'bool': v = input === true || input === 'on' || input === 'true' ? 'on' : 'off'; break;
    case 'int': {
      const s = String(input).trim();
      if (!/^-?\d+$/.test(s)) return { error: 'expected a whole number' };
      const n = Number(s);
      if (n < field.min || n > field.max) return { error: `out of range (${field.min} to ${field.max})` };
      if ((n - field.min) % field.step) return { error: `not a step of ${field.step} from ${field.min}` };
      v = String(n);
      break;
    }
    case 'color': v = hexColor(input); if (!v) return { error: 'expected a colour RRGGBB' }; break;
    case 'select':
      if (!field.options.some(o => o.value === input)) return { error: 'not one of the options' };
      v = encodeText(input);
      break;
    case 'text':
      if (/[\x00-\x1f\x7f]/.test(String(input))) return { error: 'no control characters' };
      if (utf8Len(String(input)) > field.max) return { error: `too long (${field.max} bytes at most)` };
      v = encodeText(input);
      break;
    default: return { error: 'read-only' };
  }
  const cmd = `cfg set ${field.key} ${v}`.replace(/ $/, '');
  return utf8Len(cmd) > LINE_MAX ? { error: 'too long for the console line' } : { cmd };
}

if (typeof module !== 'undefined' && module.exports) {
  module.exports = { LINE_MAX, parseLine, splitLines, parseAnswer, formModel, hexColor, shownValue, encodeText,
                     setCommand };
}

if (typeof document !== 'undefined') initConfigCard();

function initConfigCard() {
  const el = id => document.getElementById(id);
  const card = el('cfg-card');
  if (!card) return;
  const status = el('cfg-status'), panel = el('cfg-panel'), form = el('cfg-form'), info = el('cfg-info');
  const logEl = el('cfg-log'), fwEl = el('cfg-firmware');
  const btnConnect = el('cfg-connect'), btnDisconnect = el('cfg-disconnect'), btnRefresh = el('cfg-refresh');
  let port = null, reader = null, readDone = null, closing = false, rest = '';
  let fields = [], controls = {}, schemaTimer = null, refreshTimer = null, schemaRetried = false, haveSchema = false;
  let shown = false;                         // the first values after the schema: say "Connected" once
  const log = [];

  function say(msg, warn) {
    status.textContent = msg;
    status.classList.toggle('warn', !!warn);
    status.classList.toggle('muted', !warn);
  }
  function addLog(line) {
    log.push(line);
    if (log.length > 30) log.shift();
    logEl.hidden = false;
    logEl.textContent = log.join('\n');
    logEl.scrollTop = logEl.scrollHeight;
  }

  if (!('serial' in navigator)) {
    say(window.isSecureContext
      ? "This browser can't talk to USB devices: use Chrome or Edge on a computer."
      : 'USB access needs a secure page (https or localhost).', true);
    btnConnect.disabled = true;
    return;
  }

  async function send(cmd) {
    if (!port || !port.writable) { say('Not connected.', true); return; }
    const w = port.writable.getWriter();
    try { await w.write(new TextEncoder().encode(cmd + '\n')); addLog('> ' + cmd); }
    catch (e) { say('Could not write to the board: ' + e.message, true); }
    finally { w.releaseLock(); }
  }
  function refreshSoon() { clearTimeout(refreshTimer); refreshTimer = setTimeout(() => send('cfg'), 300); }
  function fieldOf(key) { return fields.find(f => f.key === key); }

  function apply(field, input) {
    const r = setCommand(field, input);
    if (r.error) { say(field.label + ': ' + r.error, true); refreshSoon(); return; }
    send(r.cmd);
    refreshSoon();
  }

  // One row per field, from the schema; info fields go to the list under the form
  function build(schema) {
    fields = formModel(schema);
    controls = {};
    form.textContent = ''; info.textContent = '';
    for (const f of fields) {
      if (f.type === 'info') {
        const dt = document.createElement('dt'), dd = document.createElement('dd');
        dt.textContent = f.label; dd.textContent = '?';
        if (f.help) dt.title = f.help;
        info.append(dt, dd);
        controls[f.key] = { set: v => { dd.textContent = v === null ? '?' : v; } };
        continue;
      }
      const row = document.createElement('div'), label = document.createElement('label'), id = 'cfg-f-' + f.key;
      row.className = 'row';
      label.htmlFor = id; label.textContent = f.label;
      row.appendChild(label);
      let input, set;
      if (f.type === 'bool') {
        input = document.createElement('input');
        input.type = 'checkbox'; input.className = 'switch'; input.setAttribute('role', 'switch');
        input.onchange = () => apply(f, input.checked);
        set = v => { input.checked = v; };
        row.appendChild(input);
      } else if (f.type === 'int') {
        input = document.createElement('input');
        const out = document.createElement('span');
        const wide = (f.max - f.min) / f.step > 200;       // a long range: type the number
        input.type = wide ? 'number' : 'range';
        input.min = f.min; input.max = f.max; input.step = f.step;
        const show = () => { out.textContent = wide ? f.unit : input.value + (f.unit ? ' ' + f.unit : ''); };
        input.oninput = show;
        input.onchange = () => apply(f, input.value);
        set = v => { input.value = v; show(); };
        row.append(input, out);
      } else if (f.type === 'select') {
        input = document.createElement('select');
        for (const o of f.options) {
          const opt = document.createElement('option');
          opt.value = o.value; opt.textContent = o.label;
          input.appendChild(opt);
        }
        input.onchange = () => apply(f, input.value);
        set = v => { input.value = v; };
        row.appendChild(input);
      } else if (f.type === 'color') {
        input = document.createElement('input');
        input.type = 'color';
        input.onchange = () => apply(f, input.value);
        set = v => { input.value = v; };
        row.appendChild(input);
      } else {                                              // text: sent with its button or Enter
        input = document.createElement('input');
        input.type = 'text'; input.autocomplete = 'off'; input.spellcheck = false;
        if (f.max) input.placeholder = 'up to ' + f.max + ' characters';
        const b = document.createElement('button');
        b.type = 'button'; b.className = 'btn quiet'; b.textContent = 'Set';
        b.onclick = () => apply(f, input.value);
        input.onkeydown = ev => { if (ev.key === 'Enter') b.click(); };
        set = v => { if (document.activeElement !== input) input.value = v; };
        row.append(input, b);
      }
      input.id = id;
      if (f.help) {
        const h = document.createElement('span');
        h.className = 'muted help'; h.textContent = f.help;
        row.appendChild(h);
      }
      controls[f.key] = { set };
      form.appendChild(row);
    }
    form.hidden = !form.children.length;
    info.hidden = !info.children.length;
  }

  function head(o) {
    const app = typeof o.app === 'string' ? o.app : '', ver = typeof o.version === 'string' ? o.version : '';
    fwEl.textContent = (app + ' ' + ver).trim() || '?';
    return (app + ' ' + ver).trim();
  }

  function show(values) {
    for (const f of fields) {
      const v = shownValue(f, values[f.key]);
      if (v !== null || f.type === 'info') controls[f.key].set(v);
    }
    const h = head(values);
    if (!shown) say('Connected: ' + (h || 'board') + '.');   // later: "Saved" or the board's error stays
    shown = true;
  }

  function noSettings() {
    clearTimeout(schemaTimer);
    panel.hidden = true;
    say('This firmware has no USB settings (or it is still starting: press Refresh).', true);
  }

  function onLine(raw) {
    const p = parseLine(raw);
    if (!p) return;
    addLog(p.tag ? p.tag + ': ' + p.text : p.text);
    if (p.tag !== 'test') return;
    const a = parseAnswer(p.text);
    if (!a) return;
    if (a.kind === 'schema') {
      clearTimeout(schemaTimer);
      haveSchema = true;
      build(a.obj);
      head(a.obj);
      panel.hidden = false;
      if (!fields.length) say('Connected: ' + (head(a.obj) || 'board') + ', with no settings to change.');
      send('cfg');
    } else if (a.kind === 'values') {
      if (haveSchema) show(a.obj);            // without a schema (a firmware before cfgcon): the timer says so
    } else if (a.kind === 'broken') {
      if (a.what === 'schema' && !schemaRetried) { schemaRetried = true; send('cfg schema'); }
      else if (haveSchema) refreshSoon();
    } else if (a.kind === 'ok') {
      const f = fieldOf(a.key);
      say('Saved: ' + (f ? f.label : a.key) + '.');
    } else if (a.kind === 'error') {
      const f = a.key && fieldOf(a.key);
      say((f ? f.label : a.key || 'The board') + ': ' + a.why, true);
      if (haveSchema) refreshSoon();        // the control goes back to the board's value
    } else if (a.kind === 'nocfg') {
      noSettings();
    }
  }

  function askSchema() {
    haveSchema = false; schemaRetried = false; shown = false;
    clearTimeout(schemaTimer);
    schemaTimer = setTimeout(() => { if (port && !haveSchema) noSettings(); }, SCHEMA_WAIT_MS);
    say('Connected, reading the settings…');
    send('cfg schema');
  }

  async function readLoop() {
    const dec = new TextDecoder();
    while (port && port.readable && !closing) {
      reader = port.readable.getReader();
      try {
        for (;;) {
          const { value, done } = await reader.read();
          if (done) break;
          const r = splitLines(rest, dec.decode(value, { stream: true }));
          rest = r.rest;
          r.lines.forEach(onLine);
        }
      } catch (e) {
        if (!closing) addLog('(read error: ' + e.message + ')');
      } finally {
        reader.releaseLock();
        reader = null;
      }
    }
  }

  function uiDisconnected(msg, warn) {
    port = null; readDone = null; closing = false; rest = ''; haveSchema = false;
    clearTimeout(schemaTimer); clearTimeout(refreshTimer);
    btnConnect.hidden = false; btnDisconnect.hidden = true; btnRefresh.hidden = true; panel.hidden = true;
    say(msg || 'Not connected.', warn);
  }

  async function connect() {
    let p;
    try { p = await navigator.serial.requestPort(); }
    catch (e) { say('No port chosen.'); return; }
    try { await p.open({ baudRate: 115200 }); }
    catch (e) {
      if (e.name === 'InvalidStateError') say('That port is already open in this page.', true);
      else say('Could not open the port (' + e.message + '). If the Install dialog is open, or another program ' +
               'reads the port (a serial monitor, the flash helper), close it and try again.', true);
      return;
    }
    port = p; closing = false; rest = '';
    btnConnect.hidden = true; btnDisconnect.hidden = false; btnRefresh.hidden = false;
    readDone = readLoop().then(() => {
      if (port) uiDisconnected('The board went away (unplugged, or restarting).', true);
    });
    askSchema();
  }

  async function disconnect() {
    if (!port) return;
    closing = true;
    try { if (reader) await reader.cancel(); } catch (e) {}
    try { if (readDone) await readDone; } catch (e) {}
    try { await port.close(); } catch (e) {}
    uiDisconnected('Disconnected.');
  }

  btnConnect.onclick = connect;
  btnDisconnect.onclick = disconnect;
  btnRefresh.onclick = () => { if (haveSchema) send('cfg'); else askSchema(); };
  window.addEventListener('beforeunload', () => { if (port) { try { port.close(); } catch (e) {} } });
}
