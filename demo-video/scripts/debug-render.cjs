const {chromium} = require('@playwright/test');
const fs = require('fs');
const path = require('path');

(async () => {
  const outputDir = path.join(__dirname, '..', 'output');
  fs.mkdirSync(outputDir, {recursive: true});
  
  const browser = await chromium.launch({headless: true});
  const page = await browser.newPage({viewport: {width: 1600, height: 1000}});
  
  page.on('console', msg => {
    if (msg.type() === 'error') console.log('[BROWSER ERROR]', msg.text());
  });
  
  await page.goto('http://127.0.0.1:9100/', {waitUntil: 'networkidle', timeout: 30000});
  await page.waitForTimeout(2000);
  
  // Find Render button
  const renderBtn = page.getByRole('button', {name: 'Render'});
  const count = await renderBtn.count();
  console.log('Render button count:', count);
  
  if (count > 0) {
    console.log('Clicking Render...');
    await renderBtn.click();
    console.log('Clicked! Waiting for activity...');
    await page.waitForTimeout(5000);
    
    // Check for any new buttons (ABORT appears during render)
    const buttons = await page.evaluate(() => {
      return Array.from(document.querySelectorAll('button')).map(el => ({
        text: el.textContent?.trim(),
      })).filter(b => b.text);
    });
    console.log('Buttons after click:', JSON.stringify(buttons));
    
    // Check if output files appeared
    const files = fs.existsSync(outputDir) ? fs.readdirSync(outputDir) : [];
    console.log('Output files:', files);
    
    // Wait longer and check again
    await page.waitForTimeout(15000);
    const files2 = fs.existsSync(outputDir) ? fs.readdirSync(outputDir) : [];
    console.log('Output files after 20s:', files2);
  }
  
  await browser.close();
})();
