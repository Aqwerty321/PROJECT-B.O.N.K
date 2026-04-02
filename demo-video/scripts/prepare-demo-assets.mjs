import fs from 'node:fs';
import path from 'node:path';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const workspaceRoot = path.resolve(__dirname, '..');
const repoRoot = path.resolve(workspaceRoot, '..');
const referenceDir = path.join(workspaceRoot, 'public', 'reference');
const apiBase = process.env.API_BASE || process.env.DEMO_API_BASE || 'http://localhost:8000';
const captureBaseUrl = process.env.CAPTURE_BASE_URL || apiBase;
const skipReadyDemo = process.argv.includes('--skip-ready-demo');

function run(command, args, {cwd, env = process.env, captureStdout = false, label}) {
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, {
      cwd,
      env,
      stdio: captureStdout ? ['ignore', 'pipe', 'inherit'] : 'inherit',
    });

    let stdout = '';
    if (captureStdout && child.stdout) {
      child.stdout.on('data', chunk => {
        stdout += chunk;
        process.stdout.write(chunk);
      });
    }

    child.on('error', reject);
    child.on('close', code => {
      if (code === 0) {
        resolve(stdout);
        return;
      }
      reject(new Error(`${label ?? command} exited with code ${code}`));
    });
  });
}

async function fetchJson(url) {
  const response = await fetch(url, {headers: {Accept: 'application/json'}});
  if (!response.ok) {
    throw new Error(`Request failed for ${url}: HTTP ${response.status}`);
  }
  return response.json();
}

async function main() {
  fs.mkdirSync(referenceDir, {recursive: true});

  if (!skipReadyDemo) {
    console.log('[prepare] running ready demo scenario');
    await run('bash', [path.join(repoRoot, 'scripts', 'run_ready_demo.sh')], {
      cwd: repoRoot,
      env: {...process.env, API_BASE: apiBase},
      label: 'run_ready_demo.sh',
    });
  }

  console.log('[prepare] writing readiness and burn-proof artifacts');
  const readinessOutput = await run('python3', [path.join(repoRoot, 'scripts', 'check_demo_readiness.py'), '--api-base', apiBase], {
    cwd: repoRoot,
    captureStdout: true,
    label: 'check_demo_readiness.py',
  });
  fs.writeFileSync(path.join(referenceDir, 'readiness-output.txt'), readinessOutput, 'utf8');

  const burnDebug = await fetchJson(`${apiBase}/api/debug/burns`);
  fs.writeFileSync(path.join(referenceDir, 'burn-debug-output.json'), `${JSON.stringify(burnDebug, null, 2)}\n`, 'utf8');

  console.log('[prepare] syncing Motion Canvas source data');
  await run('node', [path.join(workspaceRoot, 'scripts', 'sync-reference-data.mjs')], {
    cwd: workspaceRoot,
    label: 'sync-reference-data.mjs',
  });

  console.log('[prepare] capturing dashboard references');
  await run('node', [path.join(workspaceRoot, 'scripts', 'capture-reference.mjs')], {
    cwd: workspaceRoot,
    env: {...process.env, CAPTURE_BASE_URL: captureBaseUrl},
    label: 'capture-reference.mjs',
  });

  console.log('[prepare] demo video references refreshed');
}

main().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
