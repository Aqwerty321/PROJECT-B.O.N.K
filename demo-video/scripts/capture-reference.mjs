import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {chromium} from '@playwright/test';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const workspaceRoot = path.resolve(__dirname, '..');
const outputDir = path.join(workspaceRoot, 'public', 'reference');
const baseUrl = process.env.CAPTURE_BASE_URL || 'http://localhost:8000';

const routes = [
  {hash: '#/command', file: 'command-overview.png'},
  {hash: '#/track', file: 'ground-track.png'},
  {hash: '#/threat', file: 'threat-view.png'},
  {hash: '#/burn-ops', file: 'burn-ops.png'},
  {hash: '#/evasion', file: 'evasion-view.png'},
  {hash: '#/fleet-status', file: 'fleet-status.png'},
];

async function main() {
  fs.mkdirSync(outputDir, {recursive: true});

  const browser = await chromium.launch({headless: true});
  const page = await browser.newPage({
    viewport: {width: 1440, height: 900},
    deviceScaleFactor: 1,
  });

  try {
    for (const route of routes) {
      const url = `${baseUrl}/${route.hash}`;
      console.log(`[capture] ${url}`);
      await page.goto(url, {waitUntil: 'domcontentloaded'});
      await page.waitForFunction(
        () => document.querySelector('#operations-main section') !== null,
        undefined,
        {timeout: 20000},
      );
      await page.waitForTimeout(1600);
      await page.screenshot({
        path: path.join(outputDir, route.file),
        fullPage: false,
      });
    }
  } finally {
    await browser.close();
  }
}

main().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
