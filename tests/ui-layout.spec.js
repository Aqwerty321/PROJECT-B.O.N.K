// @ts-check
import { test } from "@playwright/test";

/** @import { Page } from '@playwright/test' */

test.describe.configure({ mode: "serial" });

const pages = [
  { id: "command", path: "#/command" },
  { id: "track", path: "#/track" },
  { id: "threat", path: "#/threat" },
  { id: "burn-ops", path: "#/burn-ops" },
];

const viewports = [
  { name: "desktop", width: 1440, height: 900 },
  { name: "laptop", width: 1280, height: 800 },
];

/** @param {Page} page */
async function waitForDashboard(page) {
  await page.goto("http://localhost:8000/#/command", {
    waitUntil: "domcontentloaded",
  });
  await page.waitForFunction(
    () => document.querySelector("#operations-main") !== null,
    undefined,
    { timeout: 15000 },
  );
}

test.describe("ui layout audit", () => {
  for (const viewport of viewports) {
    for (const entry of pages) {
      test(`${viewport.name} ${entry.id}`, async ({ page }) => {
        test.setTimeout(45000);
        await page.setViewportSize({
          width: viewport.width,
          height: viewport.height,
        });
        await waitForDashboard(page);
        await page.goto(`http://localhost:8000/${entry.path}`, {
          waitUntil: "domcontentloaded",
        });
        await page.waitForSelector("#operations-main");
        await page.waitForTimeout(250);

        const metrics = await page.evaluate(() => {
          const root = document.getElementById("root");
          const main = document.getElementById("operations-main");
          const summaryRail = main?.previousElementSibling;
          const topBar = summaryRail?.previousElementSibling;
          const section = main?.firstElementChild;
          const grid = section?.lastElementChild;
          const glassPanels = Array.from(
            document.querySelectorAll(
              '[aria-label="Primary navigation sidebar"] ~ div main section div',
            ),
          ).slice(0, 6);

          return {
            viewport: { width: window.innerWidth, height: window.innerHeight },
            doc: {
              scrollHeight: document.documentElement.scrollHeight,
              clientHeight: document.documentElement.clientHeight,
            },
            root: root
              ? {
                  clientHeight: root.clientHeight,
                  scrollHeight: root.scrollHeight,
                }
              : null,
            topBar:
              topBar instanceof HTMLElement
                ? {
                    clientHeight: topBar.clientHeight,
                    scrollHeight: topBar.scrollHeight,
                  }
                : null,
            summaryRail:
              summaryRail instanceof HTMLElement
                ? {
                    clientHeight: summaryRail.clientHeight,
                    scrollHeight: summaryRail.scrollHeight,
                  }
                : null,
            main:
              main instanceof HTMLElement
                ? {
                    clientHeight: main.clientHeight,
                    scrollHeight: main.scrollHeight,
                  }
                : null,
            section:
              section instanceof HTMLElement
                ? {
                    clientHeight: section.clientHeight,
                    scrollHeight: section.scrollHeight,
                  }
                : null,
            grid:
              grid instanceof HTMLElement
                ? {
                    clientHeight: grid.clientHeight,
                    scrollHeight: grid.scrollHeight,
                  }
                : null,
            bodyOverflow: getComputedStyle(document.body).overflow,
            htmlOverflow: getComputedStyle(document.documentElement).overflow,
            panelCount: glassPanels.length,
          };
        });

        console.log(
          `${viewport.name} ${entry.id}`,
          JSON.stringify(metrics, null, 2),
        );
        await page.screenshot({
          path: `test-results/${viewport.name}-${entry.id}.png`,
          fullPage: true,
        });
      });
    }
  }
});
