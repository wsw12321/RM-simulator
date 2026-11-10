// Optional real-browser smoke test (needs an installed Playwright + Chromium):
//   python3 server.py --port 8766 &   node tests/lab_browser_smoke.cjs http://127.0.0.1:8766 build
const { chromium, devices } = require('playwright');
(async () => {
  const base = process.argv[2] || 'http://127.0.0.1:8766', out = process.argv[3] || 'build';
  const browser = await chromium.launch();
  const errors = [];
  for (const [name, opts] of [['desktop', { viewport: { width: 1440, height: 1000 }, deviceScaleFactor: 1 }], ['phone', { ...devices['iPhone 13'] }]]) {
    const page = await (await browser.newContext(opts)).newPage();
    page.on('console', (m) => { if (m.type() === 'error') errors.push(`${name}: ${m.text()}`); });
    page.on('pageerror', (e) => errors.push(`${name}: ${e.message}`));
    await page.goto(`${base}/lab.html`);
    await page.fill('input[name=duration]', '90');
    await page.selectOption('select[name=blue-sentry]', 'semi');
    await page.selectOption('select[name=red-dart]', 'base-random-moving');
    await page.fill('input[name=red-dart-acc]', '100');
    await page.fill('input[name=seed]', '7');
    await page.click('#run');
    await page.waitForFunction(() => !document.getElementById('result').hidden, null, { timeout: 60000 });
    const status = await page.textContent('#status');
    const frames = await page.$eval('#scrub', (el) => Number(el.max));
    await page.selectOption('#speed', '60');
    await page.waitForTimeout(1500);
    await page.fill('#scrub', String(Math.floor(frames * 0.75)));
    await page.dispatchEvent('#scrub', 'input');
    await page.click('#play'); // pause
    const clock = await page.textContent('#clock');
    const robots = await page.$$eval('.robot', (rows) => rows.length);
    const events = await page.$$eval('#events li', (rows) => rows.length);
    const systems = await page.$$eval('#systems > div', (rows) => rows.map((r) => r.textContent));
    const feed = await page.$$eval('#events li', (rows) => rows.map((r) => r.textContent).join('\n'));
    // Same setup over 20 seeds: only the tallies are shown.
    await page.click('#sweep');
    await page.waitForFunction(() => !document.getElementById('sweep-result').hidden, null, { timeout: 120000 });
    const sweep = await page.textContent('#sweep-text');
    const range = await page.textContent('#sweep-range');
    const noScroll = await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth);
    console.log(JSON.stringify({ name, status, frames, clock, robots, events, systems, sweep, range, noHorizontalScroll: noScroll }));
    if (!status.startsWith('完成') || frames !== 899 || robots !== 8 || !noScroll) throw new Error(`${name} lab page check failed`);
    if (systems.length !== 2 || !systems[0].includes('能量机关') || !systems[1].includes('飞镖')) throw new Error(`${name} systems card missing`);
    if (!feed.includes('飞镖命中') || !feed.includes('基地护甲展开') || !feed.includes('激活能量机关')) throw new Error(`${name} event feed lacks the new mechanisms`);
    if (!/红方胜 \d+ 局/.test(sweep) || !range.includes('seed 7–26')) throw new Error(`${name} sweep summary missing`);
    await page.screenshot({ path: `${out}/lab-${name}.png`, fullPage: name === 'phone' });
  }
  // Also load the index page and the existing roster page to make sure nothing regressed.
  const page = await (await browser.newContext()).newPage();
  page.on('pageerror', (e) => errors.push(`index: ${e.message}`));
  await page.goto(`${base}/`);
  const link = await page.$eval('footer a[href="lab.html"]', (a) => a.textContent);
  await page.goto(`${base}/roster.html`);
  await page.waitForTimeout(1500);
  console.log(JSON.stringify({ indexLink: link, errors }));
  if (errors.length) throw new Error('browser console errors');
  console.log('PASS lab page in real Chromium: run, playback, scrub, phone layout, index link, no console errors.');
  await browser.close();
})().catch((e) => { console.error(e); process.exit(1); });
