const {chromium} = require('@playwright/test');
const fs = require('fs');
const path = require('path');

const outputDir = path.join(__dirname, '..', 'output');
const timeoutMs = 2700000; // 45 minutes

function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

function listMp4Files() {
  if (!fs.existsSync(outputDir)) return [];
  return fs.readdirSync(outputDir)
    .filter(f => f.toLowerCase().endsWith('.mp4'))
    .map(f => path.join(outputDir, f));
}

function getFileSize(filePath) {
  try {
    return fs.statSync(filePath).size;
  } catch {
    return 0;
  }
}

(async () => {
  fs.mkdirSync(outputDir, {recursive: true});
  // Remove old mp4 files
  for (const f of listMp4Files()) {
    console.log(`Removing old file: ${f}`);
    fs.rmSync(f, {force: true});
  }

  console.log('Launching browser...');
  const browser = await chromium.launch({headless: true});
  const page = await browser.newPage({viewport: {width: 1600, height: 1000}});

  // Listen for console messages from the MC app
  page.on('console', msg => {
    const text = msg.text();
    if (text.includes('render') || text.includes('Render') || text.includes('export') || text.includes('frame') || text.includes('error') || text.includes('Error')) {
      console.log(`[MC Console] ${text}`);
    }
  });

  console.log('Navigating to Motion Canvas...');
  await page.goto('http://127.0.0.1:9100/', {waitUntil: 'networkidle', timeout: 60000});
  await page.waitForTimeout(3000);

  // Take a screenshot to see current UI state
  await page.screenshot({path: path.join(outputDir, 'mc-ui-before-render.png')});
  console.log('Screenshot saved: mc-ui-before-render.png');

  // Try to find and click the Render button
  // Motion Canvas UI may use different button labeling — check all buttons
  const allButtons = await page.getByRole('button').allTextContents();
  console.log('Available buttons:', allButtons.join(', '));

  let renderClicked = false;

  // Try exact "Render" button first
  try {
    const renderBtn = page.getByRole('button', {name: 'Render'});
    const count = await renderBtn.count();
    if (count > 0) {
      await renderBtn.click();
      renderClicked = true;
      console.log('Clicked "Render" button');
    }
  } catch (e) {
    console.log('Could not find/click "Render" button:', e.message);
  }

  // If not found, try other patterns
  if (!renderClicked) {
    // Try clicking any button that contains 'render' text (case-insensitive)
    try {
      const btns = page.getByRole('button');
      const btnCount = await btns.count();
      for (let i = 0; i < btnCount; i++) {
        const text = await btns.nth(i).textContent();
        if (text && text.toLowerCase().includes('render')) {
          await btns.nth(i).click();
          renderClicked = true;
          console.log(`Clicked button with text: "${text}"`);
          break;
        }
      }
    } catch (e) {
      console.log('Error searching for render button:', e.message);
    }
  }

  if (!renderClicked) {
    console.error('FATAL: Could not find Render button. Buttons found:', allButtons.join(', '));
    await page.screenshot({path: path.join(outputDir, 'mc-ui-no-render-btn.png')});
    await browser.close();
    process.exit(1);
  }

  await page.waitForTimeout(2000);
  await page.screenshot({path: path.join(outputDir, 'mc-ui-after-click.png')});
  console.log('Render initiated. Monitoring progress...');

  const start = Date.now();
  let candidate = null;
  let stableHits = 0;
  let lastSize = -1;
  let maxSizeSeen = 0;
  let renderComplete = false;

  while (Date.now() - start < timeoutMs) {
    const mp4Files = listMp4Files();
    if (mp4Files.length > 0) {
      candidate = mp4Files[0];
      const size = getFileSize(candidate);
      if (size > maxSizeSeen) maxSizeSeen = size;

      if (size > 0 && size === lastSize) {
        stableHits++;
      } else {
        stableHits = 0;
        lastSize = size;
      }
    }

    // Check UI state
    let hasAbort = false;
    let hasRender = false;
    let progressText = '';
    try {
      hasAbort = (await page.getByRole('button', {name: 'Abort'}).count()) > 0;
      hasRender = (await page.getByRole('button', {name: 'Render'}).count()) > 0;
    } catch {
      // page busy
    }

    // Try to read progress from the UI
    try {
      const progressElem = await page.$('[class*="progress"], [class*="Progress"], [data-progress]');
      if (progressElem) {
        progressText = await progressElem.textContent() || '';
      }
    } catch { /* no progress element */ }

    const elapsed = ((Date.now() - start) / 1000).toFixed(0);
    const sizeMB = candidate ? (lastSize / 1024 / 1024).toFixed(1) : '0';
    const line = `[${elapsed}s] ${sizeMB} MB | stable=${stableHits} | abort=${hasAbort} render=${hasRender}${progressText ? ' | ' + progressText : ''}`;
    console.log(line);

    // Completion: file exists, file size stable for 10+ checks, no abort button, render button is back
    if (candidate && stableHits >= 10 && !hasAbort && hasRender) {
      renderComplete = true;
      console.log(`\n✓ Render complete: ${candidate}`);
      console.log(`  Size: ${(getFileSize(candidate) / 1024 / 1024).toFixed(1)} MB`);
      break;
    }

    // Alternative completion: file exists, file size stable for 20+ checks (UI check may be unreliable)
    if (candidate && stableHits >= 20) {
      renderComplete = true;
      console.log(`\n✓ Render likely complete (file stable for 60s): ${candidate}`);
      console.log(`  Size: ${(getFileSize(candidate) / 1024 / 1024).toFixed(1)} MB`);
      break;
    }

    // If the render is actively growing, reset poll interval to 3s; if idle, slow to 5s
    const pollInterval = (stableHits > 0) ? 5000 : 3000;
    await sleep(pollInterval);
  }

  if (!renderComplete) {
    console.log(`\n✗ Render did not complete within ${timeoutMs / 60000} minutes.`);
    if (candidate) {
      console.log(`  Partial file: ${candidate} (${(getFileSize(candidate) / 1024 / 1024).toFixed(1)} MB)`);
    }
  }

  // Final screenshot
  await page.screenshot({path: path.join(outputDir, 'mc-ui-final.png')}).catch(() => {});

  // Validate the mp4
  if (candidate && renderComplete) {
    const {execSync} = require('child_process');
    try {
      const probe = execSync(`ffprobe -v error -show_format -show_streams "${candidate}" 2>&1`, {encoding: 'utf-8'});
      if (probe.includes('duration=')) {
        const dur = probe.match(/duration=([\d.]+)/);
        console.log(`  Duration: ${dur ? dur[1] : 'unknown'}s`);
        console.log('  MP4 is valid!');
      } else {
        console.log('  WARNING: ffprobe could not read duration — file may be corrupt');
      }
    } catch (e) {
      console.log('  WARNING: ffprobe failed —', e.message);
    }
  }

  await browser.close();
  process.exit(renderComplete ? 0 : 1);
})().catch(err => {
  console.error('Fatal error:', err);
  process.exit(1);
});
