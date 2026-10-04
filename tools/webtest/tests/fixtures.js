// Shared test setup: fresh mock state, no internet (the page must not need it: on the setup network there is none),
// page errors collected, a screenshot of each test in shots/.
// The page's contract (main/web/index.html): element ids #wifi-list #wifi-ssid #wifi-pass #wifi-save #lang
// #update-state #update-check #update-install #update-channel #update-notes #msg #version; texts through t(key) and
// I18N = {en: {...}, fr: {...}}, elements with data-i18n="key"; the key from location.hash "#k=<key>", kept in
// sessionStorage.
const base = require('@playwright/test');
const fs = require('fs');
const path = require('path');

const KEY = process.env.MOCK_KEY || '0123456789abcdef';   // the same as mock-server.js

exports.test = base.test.extend({
  page: async ({ page, request }, use, testInfo) => {
    await request.post('/__reset');
    const errors = [];
    page.on('pageerror', e => errors.push(e.message));
    // (a 401 is the display refusing a change without its key: the page handles it, the browser still logs it)
    page.on('console', m => { if (m.type() === 'error' && !/status of 40[01]/.test(m.text())) errors.push(m.text()); });
    page.on('dialog', d => d.accept());           // confirm() before an install or a Wi-Fi change: yes
    await page.route(/^https?:\/\/(?!localhost)/, r => r.abort('internetdisconnected'));
    await use(page);
    fs.mkdirSync(path.join(__dirname, '..', 'shots'), { recursive: true });
    await page.screenshot({ path: path.join(__dirname, '..', 'shots', testInfo.title.replace(/[^a-z0-9]+/gi, '_') + '.png'),
                            fullPage: true });
    base.expect(errors, 'errors in the page').toEqual([]);
  },
});
exports.expect = base.expect;
exports.KEY = KEY;

// The mock's state (what the page sent to the "display")
exports.state = async request => (await request.get('/__state')).json();
// The page with the display's key, as the settings QR code opens it
exports.open = (page, key = KEY) => page.goto(key ? '/#k=' + key : '/');
// The page's own translation table (a global script binding, not necessarily a window property)
exports.table = page => page.evaluate(() => I18N);
// What the page shows for each data-i18n element (inputs and selects left out: their text is not the label)
exports.labels = page => page.evaluate(() => [...document.querySelectorAll('[data-i18n]')]
  .filter(e => !['INPUT', 'TEXTAREA', 'SELECT'].includes(e.tagName))
  .map(e => ({ key: e.getAttribute('data-i18n'), text: e.textContent.trim() })));
exports.posts = async (request, url) =>
  (await exports.state(request)).log.filter(e => e.method === 'POST' && e.url === url);
