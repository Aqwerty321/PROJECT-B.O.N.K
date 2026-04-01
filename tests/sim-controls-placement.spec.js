// @ts-check
import { test, expect } from '@playwright/test';

const BASE_URL = 'http://localhost:8000';

test('command page exposes sim step controls in the global operations rail', async ({ page }) => {
  test.setTimeout(120000);

  await page.setViewportSize({ width: 1440, height: 900 });
  await page.goto(`${BASE_URL}/#/command`, { waitUntil: 'domcontentloaded' });
  await page.waitForFunction(
    () => document.querySelector('#operations-main section') !== null,
    undefined,
    { timeout: 15000 },
  );
  await page.waitForTimeout(1200);

  const simControls = page.getByTestId('global-sim-controls');
  const simStatus = page.getByTestId('sim-step-status');
  const missionTitle = page.getByRole('heading', { name: 'Mission Theatre' });

  await expect(simControls).toBeVisible();
  await expect(simStatus).toBeVisible();
  await expect(page.getByText('Simulate Mission Time')).toBeVisible();
  await expect(page.getByText('Click To Simulate')).toBeVisible();
  await expect(page.getByRole('button', { name: '1H' })).toBeVisible();
  await expect(page.getByRole('button', { name: '6H' })).toBeVisible();
  await expect(page.getByRole('button', { name: '24H' })).toBeVisible();
  await expect(page.getByRole('button', { name: 'AUTO PLAY' })).toBeVisible();

  const positions = await Promise.all([
    missionTitle.boundingBox(),
    simControls.boundingBox(),
  ]);

  const [titleBox, controlsBox] = positions;
  expect(titleBox).not.toBeNull();
  expect(controlsBox).not.toBeNull();

  if (!titleBox || !controlsBox) {
    throw new Error('Missing command header or sim controls bounds');
  }

  expect(controlsBox.y).toBeLessThan(titleBox.y);
  expect(controlsBox.height).toBeGreaterThan(70);
  expect(controlsBox.y).toBeLessThan(220);

  await page.getByRole('button', { name: '1H' }).click();
  await expect(simStatus).toContainText(/STEP COMPLETE|STEPPING 1H|STEP REJECTED/);

  await page.screenshot({
    path: 'test-results/sim-controls-command.png',
    fullPage: true,
  });
});
