// @ts-check
import { test, expect } from '@playwright/test';

test('track page sidebar closed and open states', async ({ page }) => {
  await page.setViewportSize({ width: 1280, height: 800 });
  await page.goto('http://localhost:8000/#/track', { waitUntil: 'domcontentloaded' });
  await page.waitForSelector('#operations-main');
  await page.waitForTimeout(400);

  await page.screenshot({ path: 'test-results/sidebar-closed-track.png', fullPage: true });

  const toggle = page.getByRole('button', { name: 'Open navigation' }).first();
  await expect(toggle).toBeVisible();
  await toggle.click();
  await page.waitForTimeout(350);

  await page.screenshot({ path: 'test-results/sidebar-open-track.png', fullPage: true });
});
