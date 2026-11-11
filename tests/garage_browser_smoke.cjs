// Real-browser smoke test for web/garage.html and the line-up panel of web/field.html
// (optional: needs Playwright + Chromium). Run against a server started with a
// throw-away data directory:  RMUC_DATA_DIR=$(mktemp -d) python3 server.py --port 18766
//   node tests/garage_browser_smoke.cjs http://127.0.0.1:18766 [screenshot.png]
const { chromium } = require('playwright');

(async () => {
  const base = process.argv[2] || 'http://127.0.0.1:8765';
  const shot = process.argv[3];
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1500, height: 1100 }, deviceScaleFactor: shot ? 2 : 1 });
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e)));
  page.on('console', (m) => { if (m.type() === 'error' && !m.text().includes('400')) errors.push(m.text()); });
  const check = (ok, what) => { if (!ok) throw new Error(`garage: ${what}`); };
  const status = () => page.textContent('#status');
  const name = `测试车 ${Date.now() % 100000}`;

  await page.goto(`${base}/garage.html`);
  await page.waitForFunction(() => document.getElementById('status').textContent.includes('内置车辆'), null, { timeout: 15000 });
  check((await page.locator('#vehicles li').count()) >= 7, 'built-in vehicles listed');
  check(await page.locator('#save').isDisabled(), 'built-in vehicle cannot be overwritten');
  check((await page.locator('#form input, #form select').count()) >= 60 + 16, 'form generated from the schema');
  // an out-of-range value is reported and blocks saving
  const field = (label, tag) => page.locator('#form label', { has: page.locator('span', { hasText: new RegExp(`^${label}$`) }) }).locator(tag);
  const speed = field('最高前进速度', 'input');
  await speed.fill('99');
  await page.waitForFunction(() => document.getElementById('status').textContent.startsWith('参数无效'));
  check((await status()).includes('chassis.max_speed_mps'), `error names the field (${await status()})`);
  check(await page.locator('#save-as').isDisabled(), 'invalid vehicle cannot be saved');
  // a valid edit: slower chassis, power-first; the rule table follows the choice
  await speed.fill('1.2');
  await field('最高横移速度', 'input').fill('1.0');
  await field('底盘类型（步兵）', 'select').selectOption('power_first');
  await page.waitForFunction(() => document.getElementById('status').textContent.includes('未保存'));
  check((await page.textContent('#attributes tbody tr:first-child')).includes('60 W'), 'power-first gives 60 W at level 1');
  await page.fill('#name', name);
  await page.waitForFunction(() => document.getElementById('status').textContent.includes('未保存'));
  await page.click('#save-as');
  await page.waitForFunction(() => document.getElementById('status').textContent.startsWith('已保存'));
  check((await page.locator('#vehicles li button.selected').textContent()).includes(name), 'saved vehicle is listed and selected');
  check(!(await page.locator('#save').isDisabled()), 'saved vehicle can be overwritten');
  if (shot) await page.screenshot({ path: shot, fullPage: true });

  // the saved vehicle survives a reload and can be chosen for a start position
  await page.goto(`${base}/field.html`);
  await page.waitForFunction(() => document.getElementById('status').textContent.startsWith('已连接'), null, { timeout: 15000 });
  const first = page.locator('#lineup tr').first().locator('select');
  const options = await first.locator('option').allTextContents();
  check(options.some((o) => o.includes(name)), 'saved vehicle offered in the line-up');
  await first.selectOption({ label: `${name} ★` });
  await page.click('#apply');
  await page.waitForFunction(() => document.getElementById('status').textContent.startsWith('已重置'));
  check((await page.textContent('#sel-id')).includes(name), `robot uses the saved vehicle (${await page.textContent('#sel-id')})`);
  check((await page.textContent('#t-power')).includes('/ 60 W'), 'its power limit follows the rule choice');
  await page.click('#play');
  await page.focus('#field');
  await page.keyboard.down('KeyW');
  await page.waitForTimeout(2500);
  const v = parseFloat(await page.textContent('#t-speed'));
  await page.keyboard.up('KeyW');
  check(v > 1.0 && v <= 1.2 + 1e-6, `top speed follows the saved parameter (${v} m/s)`);
  await page.click('#play');

  // clean up: delete the vehicle again
  await page.goto(`${base}/garage.html`);
  await page.waitForFunction(() => document.getElementById('status').textContent.includes('内置车辆'));
  await page.locator('#vehicles li button', { hasText: name }).click();
  await page.waitForFunction(() => document.getElementById('status').textContent.startsWith('已保存的车辆'));
  page.once('dialog', (d) => d.accept());
  await page.click('#delete');
  await page.waitForFunction(() => document.getElementById('status').textContent.startsWith('已删除'));
  check((await page.locator('#vehicles li button', { hasText: name }).count()) === 0, 'vehicle deleted');
  check(errors.length === 0, `no page errors: ${errors.join(' | ')}`);
  await browser.close();
  console.log('PASS garage browser smoke');
})().catch((e) => { console.error(String(e)); process.exit(1); });
