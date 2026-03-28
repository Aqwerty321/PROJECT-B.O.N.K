// @ts-check
import fs from 'node:fs';
import path from 'node:path';
import { test, expect } from '@playwright/test';

const BASE_URL = process.env.FPS_EVIDENCE_BASE_URL || 'http://localhost:8000';
const OUTPUT_DIR = process.env.FPS_EVIDENCE_OUTPUT_DIR || 'test-results/fps-evidence';

function classifyFps(fps) {
  if (!Number.isFinite(fps) || fps <= 0) return 'invalid';
  if (fps >= 45) return 'strong';
  if (fps >= 20) return 'usable';
  if (fps >= 10) return 'constrained';
  return 'low';
}

async function measureFps(page, durationMs) {
  return page.evaluate(async ({ durationMs }) => {
    return await new Promise(resolve => {
      let frames = 0;
      const start = performance.now();

      const tick = now => {
        frames += 1;
        const elapsedMs = now - start;
        if (elapsedMs >= durationMs) {
          resolve({
            elapsedMs,
            frames,
            fps: (frames * 1000) / elapsedMs,
          });
          return;
        }
        requestAnimationFrame(tick);
      };

      requestAnimationFrame(tick);
    });
  }, { durationMs });
}

test('captures dense-payload FPS evidence for the judge-facing dashboard', async ({ page }) => {
  test.setTimeout(90000);
  fs.mkdirSync(OUTPUT_DIR, { recursive: true });

  await page.setViewportSize({ width: 1440, height: 900 });
  await page.goto(`${BASE_URL}/#/command`, { waitUntil: 'domcontentloaded' });
  await page.waitForFunction(
    () => document.querySelector('#operations-main section') !== null,
    undefined,
    { timeout: 20000 },
  );
  await page.waitForTimeout(1200);

  const commandFps = await measureFps(page, 3200);
  const runtimeEvidence = await page.evaluate(async () => {
    const [status, snapshot] = await Promise.all([
      fetch('/api/status?details=1').then(resp => resp.json()),
      fetch('/api/visualization/snapshot').then(resp => resp.json()),
    ]);

    return {
      objectCount: status.object_count ?? 0,
      satellites: Array.isArray(snapshot.satellites) ? snapshot.satellites.length : 0,
      debris: Array.isArray(snapshot.debris_cloud) ? snapshot.debris_cloud.length : 0,
      snapshotTimestamp: snapshot.timestamp ?? null,
      stepExecutionUsMean: status.internal_metrics?.command_latency_us?.step?.execution_us_mean ?? null,
    };
  });

  await page.screenshot({
    path: path.join(OUTPUT_DIR, 'command-fps-evidence.png'),
    fullPage: true,
  });

  await page.goto(`${BASE_URL}/#/scorecard`, { waitUntil: 'domcontentloaded' });
  await page.waitForSelector('#operations-main');
  await page.waitForTimeout(800);
  const scorecardFps = await measureFps(page, 2200);

  await page.screenshot({
    path: path.join(OUTPUT_DIR, 'scorecard-fps-evidence.png'),
    fullPage: true,
  });

  const report = {
    baseUrl: BASE_URL,
    commandView: commandFps,
    scorecardView: scorecardFps,
    commandViewClass: classifyFps(commandFps.fps),
    scorecardViewClass: classifyFps(scorecardFps.fps),
    runtimeEvidence,
    generatedAt: new Date().toISOString(),
  };

  fs.writeFileSync(
    path.join(OUTPUT_DIR, 'fps-evidence.json'),
    `${JSON.stringify(report, null, 2)}\n`,
    'utf8',
  );

  expect(runtimeEvidence.satellites).toBeGreaterThan(0);
  expect(runtimeEvidence.debris).toBeGreaterThan(1000);
  expect(commandFps.fps).toBeGreaterThan(5);
  expect(scorecardFps.fps).toBeGreaterThan(5);
});