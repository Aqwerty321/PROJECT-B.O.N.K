// @ts-check
import { test, expect } from '@playwright/test';

test('threat page redesign stays understandable and actionable', async ({ page }) => {
  test.setTimeout(45000);

  await page.goto('http://localhost:8000/#/threat', {
    waitUntil: 'domcontentloaded',
  });

  await page.waitForFunction(
    () => document.querySelector('#operations-main section') !== null,
    undefined,
    { timeout: 15000 },
  );

  await expect(page.getByRole('heading', { name: 'Conjunction Watch' })).toBeVisible();
  await expect(page.getByText('How to read the radar')).toBeVisible();
  await expect(page.getByText('Center is the watched spacecraft. Radius tells you how soon the encounter happens.')).toBeVisible();
  await expect(page.getByText('ENCOUNTER QUEUE', { exact: true })).toBeVisible();
  await expect(page.getByText('SELECTED ENCOUNTER', { exact: true }).first()).toBeVisible();
  await expect(page.getByRole('group', { name: 'Threat severity filters' })).toBeVisible();

  const queueButtons = page.locator('button').filter({ hasText: 'vs DEB-' });
  const queueCount = await queueButtons.count();

  if (queueCount > 0) {
    await queueButtons.first().click();
    await expect(page.getByText('Selected Encounter', { exact: true }).first()).toBeVisible();
    const selectedEncounterPanel = page.getByText('SELECTED ENCOUNTER', { exact: true }).first().locator('..');
    await expect(selectedEncounterPanel.getByText('Severity', { exact: true })).toBeVisible();
    await expect(selectedEncounterPanel.getByText('Miss', { exact: true })).toBeVisible();
  } else {
    await expect(page.getByText(/NO ENCOUNTERS IN STREAM|FILTERS HIDE ALL ENCOUNTERS/)).toBeVisible();
  }

  const watchFilter = page.getByRole('button', { name: /Watch/i }).first();
  await watchFilter.click();
  await page.waitForTimeout(200);

  await page.screenshot({
    path: 'test-results/threat-page-redesign.png',
    fullPage: true,
  });
});
