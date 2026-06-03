const { chromium } = require('playwright');
const path = require('path');

(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage({
    viewport: { width: 1280, height: 720 },
    deviceScaleFactor: 2
  });
  const fileUrl = 'file://' + path.resolve('mockup.html');
  await page.goto(fileUrl, { waitUntil: 'networkidle' });
  await page.screenshot({ path: 'qgc_boat_mockup.png' });
  await browser.close();
})();
