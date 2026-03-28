// @ts-check
import { test, expect } from '@playwright/test';

const BASE_URL = 'http://localhost:8000';

async function seedOperationalData(page) {
  await page.request.post(`${BASE_URL}/api/simulate/step`, {
    data: { step_seconds: 60 },
  });
}

test('watch target reset action stays inside the watch target card', async ({ page }) => {
  test.setTimeout(120000);

  await page.goto(`${BASE_URL}/#/command`, { waitUntil: 'domcontentloaded' });
  await page.waitForFunction(
    () => document.querySelector('#operations-main') !== null,
    undefined,
    { timeout: 15000 },
  );

  await seedOperationalData(page);

  await page.evaluate(() => {
    window.location.hash = '#/fleet-status';
  });
  await page.waitForTimeout(400);

  const firstFuelRow = page.locator('[data-testid^="fuel-row-"]').first();
  await expect(firstFuelRow).toBeVisible();
  await firstFuelRow.click();
  await page.waitForTimeout(250);

  const watchCard = page.getByTestId('watch-target-card');
  const resetButton = page.getByRole('button', { name: 'Return to fleet view' });
  await expect(watchCard).toBeVisible();
  await expect(resetButton).toBeVisible();

  const [watchBox, resetBox] = await Promise.all([
    watchCard.boundingBox(),
    resetButton.boundingBox(),
  ]);

  expect(watchBox).not.toBeNull();
  expect(resetBox).not.toBeNull();

  if (!watchBox || !resetBox) {
    throw new Error('Missing watch target card bounds');
  }

  expect(resetBox.x).toBeGreaterThanOrEqual(watchBox.x);
  expect(resetBox.y).toBeGreaterThanOrEqual(watchBox.y);
  expect(resetBox.x + resetBox.width).toBeLessThanOrEqual(watchBox.x + watchBox.width + 1);
  expect(resetBox.y + resetBox.height).toBeLessThanOrEqual(watchBox.y + watchBox.height + 1);

  await page.screenshot({
    path: 'test-results/watch-target-card-action.png',
    fullPage: true,
  });
});
