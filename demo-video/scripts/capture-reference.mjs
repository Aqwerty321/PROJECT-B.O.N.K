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
  {hash: '#/command', file: 'command-overview.png', heading: 'Mission Theatre', probeText: '3D ORBITAL COMMAND VIEW'},
  {hash: '#/track', file: 'ground-track.png', heading: 'Ground Track Operations', probeText: 'GROUND TRACK OPERATIONS'},
  {hash: '#/threat', file: 'threat-view.png', heading: 'Conjunction Watch', probeText: 'SELECTED ENCOUNTER', prepare: prepareThreatCapture, skipInitialGoto: true},
  {hash: '#/burn-ops', file: 'burn-ops.png', heading: 'Predictive Maneuver Timeline', probeText: 'BURN DECISION EXPLAINER', prepare: prepareBurnOpsCapture, skipInitialGoto: true},
  {hash: '#/evasion', file: 'evasion-view.png', heading: 'Fuel-to-Mitigation Efficiency', probeText: 'EVASION EFFICIENCY'},
  {hash: '#/fleet-status', file: 'fleet-status.png', heading: 'System Health & Resource Posture', probeText: 'MISSION STATUS'},
];

async function triggerChecklistAction(page, label) {
  await page.goto(`${baseUrl}/#/command`, {waitUntil: 'domcontentloaded'});
  await page.waitForLoadState('networkidle').catch(() => {});
  await waitForRoute(page, 'Mission Theatre', '3D ORBITAL COMMAND VIEW');
  const clicked = await page.evaluate(expectedLabel => {
    const panel = document.querySelector('[data-testid="sidebar-operator-checklist-panel"]');
    if (!panel) return false;
    const buttons = Array.from(panel.querySelectorAll('button'));
    const target = buttons.find(button => (button.textContent || '').trim() === expectedLabel);
    if (!target) return false;
    target.click();
    return true;
  }, label);
  if (!clicked) {
    throw new Error(`Could not find checklist action: ${label}`);
  }
}

async function prepareThreatCapture(page) {
  await triggerChecklistAction(page, 'Open Threat');
  await waitForRoute(page, 'Conjunction Watch', 'SELECTED ENCOUNTER');
  await page.waitForFunction(
    () => {
      const root = document.querySelector('#operations-main');
      return Boolean(root && (root.textContent ?? '').includes('Satellite Focus Required') === false && (root.textContent ?? '').includes('SELECTED ENCOUNTER'));
    },
    undefined,
    {timeout: 10000},
  );
}

async function prepareBurnOpsCapture(page) {
  await triggerChecklistAction(page, 'Open Burn Ops');
  await waitForRoute(page, 'Predictive Maneuver Timeline', 'BURN DECISION EXPLAINER');
  await page.waitForFunction(
    () => {
      const root = document.querySelector('#operations-main');
      return Boolean(root && (root.textContent ?? '').includes('Selected Burn') && (root.textContent ?? '').includes('AUTO-COLA-4-1'));
    },
    undefined,
    {timeout: 10000},
  );
}

async function waitForRoute(page, heading, probeText) {
  await page.locator('#operations-main').waitFor({state: 'visible', timeout: 20000});
  await page.getByRole('heading', {name: heading, exact: true}).waitFor({state: 'visible', timeout: 20000});
  if (probeText) {
    await page.waitForFunction(
      text => {
        const root = document.querySelector('#operations-main');
        return Boolean(root && (root.textContent ?? '').includes(text));
      },
      probeText,
      {timeout: 20000},
    );
  }
  await page.waitForTimeout(800);
}

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
      if (!route.skipInitialGoto) {
        await page.goto(url, {waitUntil: 'domcontentloaded'});
        await page.waitForLoadState('networkidle').catch(() => {});
        await waitForRoute(page, route.heading, route.probeText);
      }
      if (route.prepare) {
        await route.prepare(page);
      } else {
        await waitForRoute(page, route.heading, route.probeText);
      }
      await page.waitForFunction(
        () => document.fonts?.status !== 'loading',
        undefined,
        {timeout: 10000},
      ).catch(() => {});
      await page.waitForTimeout(600);
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
