// Browser tests of the settings page (main/web/index.html) against a mock display (mock-server.js).
//   cd tools/webtest && npm test            (first time: npm install && npx playwright install chromium)
// Screenshots of each test land in tools/webtest/shots/ for review.
const { defineConfig, devices } = require('@playwright/test');

module.exports = defineConfig({
  testDir: './tests',
  workers: 1,                                   // one mock server, shared state
  reporter: [['list']],
  use: {
    baseURL: 'http://localhost:8099',
    ...devices['Pixel 7'],                       // the page is used on a phone
    browserName: 'chromium',
  },
  webServer: {
    command: 'node mock-server.js 8099',
    url: 'http://localhost:8099/api/info',
    reuseExistingServer: true,
  },
});
