// Run the real phone interface in Chromium with an isolated mock server.
const {chromium} = require('playwright');
const fs = require('fs');
const assert = require('assert');

(async () => {
  const browser = await chromium.launch({headless:true});
  try {
    const page = await browser.newPage();
    await page.route('https://phone.test/**', async route => {
      const path = new URL(route.request().url()).pathname;
      if (path === '/' || path === '/phone.js' || path === '/phone.css') {
        const file = path === '/' ? 'index.html' : path.slice(1);
        const type = file.endsWith('.js') ? 'text/javascript' : file.endsWith('.css') ? 'text/css' : 'text/html';
        await route.fulfill({status:200, contentType:type, body:fs.readFileSync('assets/phone/' + file)});
      } else if (path === '/api/config') await route.fulfill({status:200, contentType:'application/json', body:'{"language":"en"}'});
      else await route.fulfill({status:401, contentType:'application/json', body:'{}'});
    });
    await page.goto('https://phone.test/');
    await page.evaluate(() => {
      const text = '😀 link... @Bob';
      document.body.append(messageRow({seq:42, name:'Viewer', time:'12:00', text,
        links:[{start:3,end:10,url:'https://example.org/full?x=1&y=2'}],
        mentions:[{start:11,end:15,name:'Bob'}]}));
      document.body.append(messageRow({seq:43, name:'Viewer', time:'12:01', text:'bad',
        links:[{start:0,end:3,url:'javascript:alert(1)'}]}));
    });
    const link = page.locator('[data-seq="42"] a');
    assert.equal(await link.innerText(), 'link...');
    assert.equal(await link.getAttribute('href'), 'https://example.org/full?x=1&y=2');
    assert.equal(await link.getAttribute('rel'), 'noopener noreferrer');
    assert.equal(await page.locator('[data-seq="42"] .mention-link').innerText(), '@Bob');
    assert.equal(await page.locator('[data-seq="43"] a').count(), 0);
    console.log('PASS: phone interface keeps full link targets and blocks unsafe schemes');
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
