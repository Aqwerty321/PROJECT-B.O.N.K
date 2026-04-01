import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {chromium} from '@playwright/test';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const workspaceRoot = path.resolve(__dirname, '..');
const outputDir = path.join(workspaceRoot, 'output');
const baseUrl = process.env.MOTION_CANVAS_URL || 'http://127.0.0.1:9100';
const timeoutMs = Number(process.env.MOTION_CANVAS_RENDER_TIMEOUT_MS || 900000);

function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

function listMp4Files() {
  if (!fs.existsSync(outputDir)) return [];
  return fs
    .readdirSync(outputDir)
    .filter(file => file.toLowerCase().endsWith('.mp4'))
    .map(file => path.join(outputDir, file));
}

async function waitForRender(page) {
  const start = Date.now();
  let candidate = null;
  let stableHits = 0;
  let lastSize = -1;

  while (Date.now() - start < timeoutMs) {
    const mp4Files = listMp4Files();
    if (mp4Files.length > 0) {
      candidate = mp4Files[0];
      const size = fs.statSync(candidate).size;
      if (size > 0 && size === lastSize) {
        stableHits += 1;
      } else {
        stableHits = 0;
        lastSize = size;
      }
    }

    const hasAbort = await page.getByRole('button', {name: 'ABORT'}).count();
    const hasRender = await page.getByRole('button', {name: 'RENDER'}).count();

    if (candidate && stableHits >= 3 && !hasAbort && hasRender) {
      return candidate;
    }

    await sleep(2000);
  }

  throw new Error('Timed out waiting for Motion Canvas render to finish.');
}

async function main() {
  fs.mkdirSync(outputDir, {recursive: true});
  for (const file of listMp4Files()) {
    fs.rmSync(file, {force: true});
  }

  const browser = await chromium.launch({headless: true});
  const page = await browser.newPage({viewport: {width: 1600, height: 1000}});

  try {
    await page.goto(baseUrl, {waitUntil: 'domcontentloaded'});
    await page.waitForTimeout(2500);

    const selects = page.locator('select');
    await selects.nth(2).selectOption({index: 1});
    await selects.nth(3).selectOption({index: 2});
    await selects.nth(5).selectOption({index: 1});
    await page.waitForTimeout(400);

    await page.getByRole('button', {name: 'RENDER'}).click();
    const output = await waitForRender(page);
    console.log(output);
  } finally {
    await browser.close();
  }
}

main().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
