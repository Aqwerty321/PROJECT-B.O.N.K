// @ts-check
import { test, expect } from '@playwright/test';

const BASE_URL = 'http://localhost:8000';

test('sidebar utilities hold global focus and operation checklist without pushing down main content', async ({ page }) => {
  test.setTimeout(120000);

  await page.setViewportSize({ width: 1440, height: 900 });
  await page.goto(`${BASE_URL}/#/threat`, { waitUntil: 'domcontentloaded' });
  await page.waitForFunction(
    () => document.querySelector('#operations-main') !== null,
    undefined,
    { timeout: 15000 },
  );
  await page.waitForTimeout(1200);

  const topBarHeight = await page.evaluate(() => {
    const main = document.getElementById('operations-main');
    const topBar = main?.previousElementSibling;
    return topBar instanceof HTMLElement ? topBar.clientHeight : -1;
  });

  const mainHeight = await page.evaluate(() => {
    const main = document.getElementById('operations-main');
    return main instanceof HTMLElement ? main.clientHeight : -1;
  });

  expect(topBarHeight).toBeGreaterThan(0);
  expect(topBarHeight).toBeLessThan(165);
  expect(mainHeight).toBeGreaterThan(620);

  const checklistRail = page.getByTestId('sidebar-checklist-rail-button');
  const focusRail = page.getByTestId('sidebar-focus-rail-button');
  await expect(checklistRail).toBeVisible();
  await expect(focusRail).toBeVisible();

  await checklistRail.click();

  const checklistPanel = page.getByTestId('sidebar-operator-checklist-panel');
  const focusPanel = page.getByTestId('sidebar-global-focus-panel');
  await expect(checklistPanel).toBeVisible();
  await expect(focusPanel).toBeVisible();

  const checklistCount = Number(await page.getByTestId('sidebar-operator-checklist-count').innerText());
  expect(checklistCount).toBeGreaterThan(0);

  const globalFocusValue = page.getByTestId('sidebar-global-focus-value');
  await expect(globalFocusValue).toContainText('Fleet Overview');

  const watchSummary = page.getByTestId('watch-target-summary-card');
  await expect(watchSummary).toContainText('Fleet Overview');

  await focusPanel.locator('button[aria-haspopup="listbox"]').click();
  await page.locator('[role="option"]').nth(1).click();
  await page.waitForTimeout(250);

  const pinnedFocusText = await globalFocusValue.innerText();
  await expect(watchSummary).toContainText(pinnedFocusText);

  const autoButton = page.getByRole('button', { name: 'Return global focus to fleet overview' });
  await expect(autoButton).toBeVisible();
  await autoButton.click();
  await page.waitForTimeout(200);

  await expect(globalFocusValue).toContainText('Fleet Overview');
  await expect(watchSummary).toContainText('Fleet Overview');

  await page.screenshot({
    path: 'test-results/sidebar-utilities-threat.png',
    fullPage: true,
  });
});
