import fs from 'node:fs';
import path from 'node:path';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {chromium} from '@playwright/test';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const workspaceRoot = path.resolve(__dirname, '..');
const outputDir = path.join(workspaceRoot, 'output');
const baseUrl = process.env.MOTION_CANVAS_URL || 'http://127.0.0.1:9100';
const timeoutMs = Number(process.env.MOTION_CANVAS_RENDER_TIMEOUT_MS || 900000);
const shouldStartServer = process.env.MOTION_CANVAS_START_SERVER !== '0';

function npmCommand() {
  return process.platform === 'win32' ? 'npm.cmd' : 'npm';
}

function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

async function waitForServer(url) {
  const startedAt = Date.now();
  while (Date.now() - startedAt < 120000) {
    if (await canReachServer(url)) return;
    await sleep(1000);
  }
  throw new Error(`Timed out waiting for Motion Canvas dev server at ${url}`);
}

async function canReachServer(url) {
  try {
    const response = await fetch(url, {redirect: 'manual'});
    return response.ok || response.status === 302 || response.status === 304;
  } catch {
    return false;
  }
}

function startServer() {
  const url = new URL(baseUrl);
  const port = url.port || '9100';
  const host = url.hostname || '127.0.0.1';
  const child = spawn(npmCommand(), ['run', 'start', '--', '--host', host, '--port', port], {
    cwd: workspaceRoot,
    env: process.env,
    stdio: ['ignore', 'pipe', 'pipe'],
  });

  child.stdout.on('data', chunk => {
    process.stdout.write(`[motion] ${chunk}`);
  });

  child.stderr.on('data', chunk => {
    process.stderr.write(`[motion] ${chunk}`);
  });

  return child;
}

async function stopServer(child) {
  if (!child || child.killed || child.exitCode !== null) return;

  const closed = new Promise(resolve => {
    child.once('close', resolve);
    child.once('exit', resolve);
  });

  child.kill('SIGTERM');

  const timeout = setTimeout(() => {
    if (child.exitCode === null && !child.killed) {
      child.kill('SIGKILL');
    }
  }, 5000);

  await closed;
  clearTimeout(timeout);
}

function listMp4Files() {
  if (!fs.existsSync(outputDir)) return [];
  return fs
    .readdirSync(outputDir)
    .filter(file => file.toLowerCase().endsWith('.mp4'))
    .map(file => path.join(outputDir, file));
}

async function waitForRender(page) {
  const start = Date.now();
  let candidate = null;
  let stableHits = 0;
  let lastSize = -1;

  while (Date.now() - start < timeoutMs) {
    const mp4Files = listMp4Files();
    if (mp4Files.length > 0) {
      candidate = mp4Files[0];
      const size = fs.statSync(candidate).size;
      if (size > 0 && size === lastSize) {
        stableHits += 1;
      } else {
        stableHits = 0;
        lastSize = size;
      }
    }

    const hasAbort = await page.getByRole('button', {name: 'ABORT'}).count();
    const hasRender = await page.getByRole('button', {name: 'RENDER'}).count();

    if (candidate && stableHits >= 3 && !hasAbort && hasRender) {
      return candidate;
    }

    await sleep(2000);
  }

  throw new Error('Timed out waiting for Motion Canvas render to finish.');
}

async function main() {
  fs.mkdirSync(outputDir, {recursive: true});
  for (const file of listMp4Files()) {
    fs.rmSync(file, {force: true});
  }

  let server = null;
  if (await canReachServer(baseUrl)) {
    console.log(`[motion] using existing server at ${baseUrl}`);
  } else if (shouldStartServer) {
    server = startServer();
    await waitForServer(baseUrl);
  } else {
    throw new Error(`Motion Canvas dev server is not reachable at ${baseUrl}`);
  }

  const browser = await chromium.launch({headless: true});
  const page = await browser.newPage({viewport: {width: 1600, height: 1000}});

  try {
    await page.goto(baseUrl, {waitUntil: 'domcontentloaded'});
    await page.waitForLoadState('networkidle').catch(() => {});
    await page.getByRole('button', {name: 'RENDER'}).waitFor({timeout: 30000});
    await page.waitForTimeout(600);

    await page.getByRole('button', {name: 'RENDER'}).click();
    const output = await waitForRender(page);
    console.log(output);
  } finally {
    await browser.close();
    if (server) {
      await stopServer(server);
    }
  }
}

main().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
