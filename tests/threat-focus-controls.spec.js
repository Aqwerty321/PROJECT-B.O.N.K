// @ts-check
import { test, expect } from '@playwright/test';

const BASE_URL = 'http://localhost:8000';

test('threat page can switch and clear focused object from encounter queue workflow', async ({ page }) => {
  test.setTimeout(120000);

  await page.goto(`${BASE_URL}/#/threat`, { waitUntil: 'domcontentloaded' });
  await page.waitForFunction(
    () => document.querySelector('#operations-main') !== null,
    undefined,
    { timeout: 15000 },
  );
  await page.waitForTimeout(1400);

  const focusCard = page.locator('[data-testid="threat-focus-card"]');
  await expect(focusCard).toBeVisible();

  const queueButtons = page.locator('button').filter({ hasText: 'vs DEB-' });
  const queueCount = await queueButtons.count();
  expect(queueCount).toBeGreaterThan(1);

  const firstEventLabel = await queueButtons.nth(0).innerText();
  const secondEventLabel = await queueButtons.nth(1).innerText();
  const firstSatMatch = firstEventLabel.match(/SAT-[0-9]+/);
  const secondSatMatch = secondEventLabel.match(/SAT-[0-9]+/);

  if (!firstSatMatch || !secondSatMatch) {
    throw new Error('Could not parse satellite ids from encounter queue');
  }

  const firstSat = firstSatMatch[0];
  const secondSat = secondSatMatch[0];

  await queueButtons.nth(0).click();
  await page.waitForTimeout(350);
  await expect(focusCard).toContainText(firstSat);

  await queueButtons.nth(1).click();
  await page.waitForTimeout(350);
  await expect(focusCard).toContainText(secondSat);

  const autoButton = page.getByRole('button', { name: 'Return threat page to auto focus' });
  await expect(autoButton).toBeVisible();
  await autoButton.click();
  await page.waitForTimeout(350);

  await expect(focusCard).not.toContainText(secondSat);

  await page.screenshot({
    path: 'test-results/threat-focus-queue-workflow.png',
    fullPage: true,
  });
});
