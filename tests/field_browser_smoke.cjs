// Real-browser smoke test for web/field.html (optional: needs Playwright + Chromium).
//   node tests/field_browser_smoke.cjs http://127.0.0.1:8765 [screenshot.png]
const { chromium } = require('playwright');

(async () => {
  const base = process.argv[2] || 'http://127.0.0.1:8765';
  const shot = process.argv[3];
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1500, height: 1000 }, deviceScaleFactor: shot ? 2 : 1 });
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e)));
  page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
  await page.goto(`${base}/field.html`);
  await page.waitForFunction(() => document.getElementById('status').textContent.startsWith('已连接'), null, { timeout: 15000 });
  const check = (ok, what) => { if (!ok) throw new Error(`field page: ${what}`); };
  check((await page.locator('#robot option').count()) === 4, 'four robots listed');
  check((await page.textContent('#f-size')).includes('28 × 15'), 'field size shown');
  // the canvas is painted (not a single flat colour)
  const colours = await page.evaluate(() => {
    const c = document.getElementById('field'), d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data, seen = new Set();
    for (let i = 0; i < d.length; i += 4 * 997) seen.add(`${d[i] >> 4},${d[i + 1] >> 4},${d[i + 2] >> 4}`);
    return seen.size;
  });
  check(colours > 20, `terrain rendered (${colours} colour buckets)`);
  await page.click('#reset');
  await page.waitForFunction(() => document.getElementById('status').textContent.startsWith('已重置'));
  const before = await page.textContent('#t-pos');
  await page.click('#play');
  await page.focus('#field');
  await page.keyboard.down('KeyW');
  await page.waitForTimeout(1500);
  await page.keyboard.up('KeyW');
  await page.waitForTimeout(300);
  const after = await page.textContent('#t-pos');
  check(before !== after, `robot moved (${before} -> ${after})`);
  const x0 = parseFloat(before), x1 = parseFloat(after);
  check(x1 > x0 + 0.5, `robot drove forward (${x0} -> ${x1})`);
  // pointer probe over the red high point: 800 mm above the floor
  const box = await page.locator('#field').boundingBox();
  await page.mouse.move(box.x + box.width * (1.4 / 28), box.y + box.height * (1 - 12.2 / 15));
  const probed = parseFloat(await page.textContent('#p-ground'));
  check(Math.abs(probed - 800) <= 3, `probe shows the 800 mm high point (${probed})`);
  check((await page.textContent('#p-zones')).includes('red_high_point'), 'probe shows the zone');
  await page.click('#play');
  check(errors.length === 0, `no page errors: ${errors.join(' | ')}`);
  if (shot) await page.screenshot({ path: shot, fullPage: true });
  await browser.close();
  console.log('PASS field browser smoke');
})().catch((e) => { console.error(String(e)); process.exit(1); });
