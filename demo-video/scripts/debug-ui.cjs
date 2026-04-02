const {chromium} = require('@playwright/test');

(async () => {
  const browser = await chromium.launch({headless: true});
  const page = await browser.newPage({viewport: {width: 1600, height: 1000}});
  await page.goto('http://127.0.0.1:9100/', {waitUntil: 'networkidle', timeout: 30000});
  await page.waitForTimeout(3000);
  
  // Get all buttons
  const buttons = await page.evaluate(() => {
    return Array.from(document.querySelectorAll('button')).map(el => ({
      text: el.textContent?.trim(),
      ariaLabel: el.getAttribute('aria-label'),
      title: el.getAttribute('title'),
    }));
  });
  console.log('Buttons found:', JSON.stringify(buttons, null, 2));
  
  // Check for render text
  const renderEls = await page.evaluate(() => {
    return Array.from(document.querySelectorAll('*')).filter(el => 
      el.textContent?.toLowerCase().includes('render') && el.children.length === 0
    ).slice(0, 20).map(el => ({
      tag: el.tagName,
      text: el.textContent?.trim()?.substring(0, 100),
    }));
  });
  console.log('Render text:', JSON.stringify(renderEls, null, 2));
  
  // Get page title
  console.log('Title:', await page.title());
  
  await browser.close();
})();
