// Optional real-browser smoke test for a machine with Playwright + Chromium already installed.
// Not a dependency installer. Passing DOM/API tests do not imply this script was executed.
const {chromium}=require('playwright');
const assert=require('node:assert/strict'),path=require('node:path');
(async()=>{
 const base=process.argv[2]||'http://127.0.0.1:8766';
 const browser=await chromium.launch({headless:true,executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',args:['--no-sandbox']});
 try{
  const page=await browser.newPage({viewport:{width:1440,height:1100},deviceScaleFactor:1}),errors=[];
  page.on('pageerror',error=>errors.push(error.message));
  const state=async(objective=false)=>await(await page.request.get(`${base}/api/${objective?'objective/':''}state`)).json();
  const waitTick=tick=>page.waitForFunction(value=>document.querySelector('#tick').textContent===String(value),tick);
  await page.goto(base);await page.getByText('C++ 引擎在线').waitFor();
  await page.locator('#reset').click();await waitTick(0);const sandboxBefore=await state();
  const sandboxReplay=await(await page.request.get(base+'/api/replay')).json();
  await page.locator('#scenario').selectOption('objective');await page.locator('#objective-controls').waitFor({state:'visible'});
  // Establish this test's configuration without assuming the server has never been used.
  await page.locator('#boundary-policy').selectOption('strict-reject-unadjudicated-v1');
  await page.locator('#credit-strategy').selectOption('disabled');await page.locator('#objective-apply').click();await waitTick(0);
  assert.equal(await page.locator('#credit-strategy').inputValue(),'disabled');
  assert.equal(await page.locator('#red-robot-card').isVisible(),false);assert.match(await page.locator('#fire-help').innerText(),/禁用/);
  await page.locator('#mode').selectOption('1');await page.locator('#advance-ten').click();await waitTick(600);
  const capture=await state(true);assert.equal(capture.objective.zones.filter(zone=>zone.completed).length,2);assert.equal(capture.objective.score,null);
  assert.match(await page.locator('#objective-zones').innerText(),/临时控制锁存/);assert.match(await page.locator('#events').innerText(),/7\.050s/);
  await page.screenshot({path:path.resolve(__dirname,'../docs/objective-desktop.png'),fullPage:true});
  // Actual download exports the objective identity.
  const downloadReady=page.waitForEvent('download');await page.locator('#export').click();const download=await downloadReady;
  assert.match(download.suggestedFilename(),/tactics-objective-tick-600/);
  await page.locator('#boundary-policy').selectOption('ENGINEERING-NOT-OFFICIAL-timer-before-input-v1');
  await page.locator('#credit-strategy').selectOption('EXPERIMENTAL-end-of-second-v1');await page.locator('#objective-apply').click();await waitTick(0);
  await page.locator('#advance-ten').click();await waitTick(600);await page.locator('#advance-ten').click();await waitTick(1200);
  assert.deepEqual((await state(true)).objective.score,[13,33]);assert.equal(await page.locator('#red-score').innerText(),'13');assert.equal(await page.locator('#blue-score').innerText(),'33');
  const objectiveHash=(await state(true)).hash;
  await page.locator('#replay-file').setInputFiles({name:'wrong-mode.json',mimeType:'application/json',buffer:Buffer.from(JSON.stringify(sandboxReplay))});
  await page.waitForFunction(()=>document.querySelector('#toast').textContent.includes('其他模式'));assert.equal((await state(true)).hash,objectiveHash);
  await page.setViewportSize({width:390,height:844});await page.screenshot({path:path.resolve(__dirname,'../docs/objective-mobile.png'),fullPage:true});
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true,'mobile layout does not overflow');
  await page.locator('#scenario').selectOption('sandbox');await page.locator('#objective-controls').waitFor({state:'hidden'});
  assert.equal((await state()).hash,sandboxBefore.hash);assert.equal(await page.locator('#red-robot-card').isVisible(),true);
  assert.deepEqual(errors,[],'no browser page exceptions');
  console.log('PASS real-browser objective smoke: mode selection, capture in10s, timestamp display, weapon suppression, objective download, explicit scoring, wrong-profile import isolation, mode return, mobile width, screenshots and no page exceptions.');
 }finally{await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
