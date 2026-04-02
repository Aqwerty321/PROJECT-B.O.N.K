#!/usr/bin/env node
/**
 * render-ffmpeg.js
 *
 * Generates the CASCADE demo video using pure FFmpeg.
 * Ken Burns zoom/pan on real app screenshots + text interstitial cards,
 * assembled with crossfade transitions.
 *
 * Usage: node demo-video/scripts/render-ffmpeg.js
 */

const { execSync } = require('child_process');
const path = require('path');
const fs = require('fs');

// ─── Config ──────────────────────────────────────────────────────────
const FPS = 30;
const OUT_W = 1920;
const OUT_H = 1080;
const CRF_SEG = 14;       // near-lossless for temp segments
const CRF_FINAL = 18;     // high quality final output
const XFADE_DUR = 0.8;    // crossfade duration (seconds)
const HOLD_SEC = 2.5;     // hold at full view before zooming (seconds)

// Source screenshots are 1440x900. Upscale 2x for zoom headroom.
const UPSCALE = 2;
const UP_W = 1440 * UPSCALE;  // 2880
const UP_H = 900 * UPSCALE;   // 1800

// Paths
const ROOT = path.resolve(__dirname, '..');
const REF = path.join(ROOT, 'public', 'reference');
const OUT = path.join(ROOT, 'output');
const TMP = path.join(OUT, 'tmp');

// Fonts
const FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf';
const FONT_BOLD = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf';

// Colors
const BG = '0x0a0e17';
const ACCENT = '0x00e5ff';
const DIM = '0x7799aa';

// ─── Segment definitions ─────────────────────────────────────────────

const segments = [
  // 1. TITLE CARD
  {
    type: 'card',
    duration: 12,
    texts: [
      { text: 'C A S C A D E', font: FONT_BOLD, size: 64, color: 'white', yExpr: 'h/2-80' },
      { text: 'Collision Avoidance System for Constellation', font: FONT, size: 24, color: DIM, yExpr: 'h/2+10' },
      { text: 'Automation, Detection & Evasion', font: FONT, size: 24, color: DIM, yExpr: 'h/2+50' },
    ],
    fadeIn: 2.0,
    fadeOut: 0.5,
  },

  // 2. COMMAND OVERVIEW
  {
    type: 'screenshot',
    duration: 35,
    image: 'command-overview.png',
    startCenter: [720, 450],   // image center (full view)
    endCenter:   [440, 480],   // globe with debris
    startZoom: 1.05,
    endZoom: 1.55,
    bottomText: 'Command Overview  \u2014  Operational fleet picture from real orbital data',
  },

  // 3. INTERSTITIAL: CASCADE LOOP
  {
    type: 'card',
    duration: 6,
    texts: [
      { text: 'THE CASCADE LOOP', font: FONT, size: 20, color: ACCENT, yExpr: 'h/2-50' },
      { text: 'Detect  \u2192  Decide  \u2192  Evade  \u2192  Recover', font: FONT_BOLD, size: 38, color: 'white', yExpr: 'h/2+10' },
    ],
    fadeIn: 0.5,
    fadeOut: 0.5,
  },

  // 4. GROUND TRACK
  {
    type: 'screenshot',
    duration: 35,
    image: 'ground-track.png',
    startCenter: [720, 450],
    endCenter:   [480, 500],   // Mercator map center
    startZoom: 1.05,
    endZoom: 1.5,
    bottomText: 'Ground Track  \u2014  Mercator projection with coastlines and real orbital traffic',
  },

  // 5. INTERSTITIAL: THREAT
  {
    type: 'card',
    duration: 6,
    texts: [
      { text: 'THREAT DETECTION', font: FONT, size: 20, color: ACCENT, yExpr: 'h/2-50' },
      { text: 'Conservative screening', font: FONT_BOLD, size: 38, color: 'white', yExpr: 'h/2+10' },
      { text: 'Zero missed collisions by design', font: FONT, size: 22, color: DIM, yExpr: 'h/2+60' },
    ],
    fadeIn: 0.5,
    fadeOut: 0.5,
  },

  // 6. THREAT VIEW
  {
    type: 'screenshot',
    duration: 35,
    image: 'threat-view.png',
    startCenter: [720, 450],
    endCenter:   [400, 440],   // bullseye plot
    startZoom: 1.05,
    endZoom: 1.6,
    bottomText: 'Conjunction Watch  \u2014  Severity analysis and time-to-closest-approach',
  },

  // 7. INTERSTITIAL: AUTONOMOUS
  {
    type: 'card',
    duration: 6,
    texts: [
      { text: 'AUTONOMOUS RESPONSE', font: FONT, size: 20, color: ACCENT, yExpr: 'h/2-50' },
      { text: 'From detection to evasion', font: FONT_BOLD, size: 38, color: 'white', yExpr: 'h/2+10' },
      { text: 'in 13 milliseconds', font: FONT_BOLD, size: 38, color: 'white', yExpr: 'h/2+55' },
    ],
    fadeIn: 0.5,
    fadeOut: 0.5,
  },

  // 8. BURN OPS — THE MONEY SHOT
  {
    type: 'screenshot',
    duration: 45,
    image: 'burn-ops.png',
    startCenter: [720, 450],
    endCenter:   [500, 470],   // timeline + decision area
    startZoom: 1.05,
    endZoom: 1.5,
    bottomText: 'Burn Operations  \u2014  SAT-67060  \u00b7  0.50 m/s \u0394v  \u00b7  8.0 m \u2192 134.4 m miss distance',
  },

  // 9. EVASION VIEW
  {
    type: 'screenshot',
    duration: 30,
    image: 'evasion-view.png',
    startCenter: [720, 450],
    endCenter:   [500, 460],   // efficiency chart
    startZoom: 1.05,
    endZoom: 1.5,
    bottomText: 'Evasion Efficiency  \u2014  2 collisions avoided  \u00b7  0.09 kg fuel per avoidance',
  },

  // 10. FLEET STATUS
  {
    type: 'screenshot',
    duration: 30,
    image: 'fleet-status.png',
    startCenter: [720, 450],
    endCenter:   [1050, 400],  // fuel bars on right side
    startZoom: 1.05,
    endZoom: 1.55,
    bottomText: 'Fleet Status  \u2014  System health and resource posture across the constellation',
  },

  // 11. CLOSING CARD
  {
    type: 'card',
    duration: 18,
    texts: [
      { text: 'C A S C A D E', font: FONT_BOLD, size: 56, color: 'white', yExpr: 'h/2-100' },
      { text: 'Collision awareness', font: FONT, size: 34, color: 'white', yExpr: 'h/2-20' },
      { text: 'into collision avoidance', font: FONT, size: 34, color: ACCENT, yExpr: 'h/2+25' },
      { text: '2 collisions avoided  \u00b7  0.19 kg fuel  \u00b7  13 ms tick time', font: FONT, size: 20, color: DIM, yExpr: 'h/2+90' },
    ],
    fadeIn: 1.5,
    fadeOut: 2.0,
  },
];

// ─── Helpers ─────────────────────────────────────────────────────────

function run(cmd, label) {
  console.log(`\n\u2192 ${label}`);
  try {
    execSync(cmd, { stdio: ['pipe', 'pipe', 'pipe'], timeout: 300_000 });
    console.log(`  \u2713 done`);
  } catch (err) {
    console.error(`  \u2717 FAILED: ${label}`);
    if (err.stderr) console.error(err.stderr.toString().slice(-500));
    process.exit(1);
  }
}

/** Write text to a temp file for drawtext textfile= (avoids escaping hell) */
function writeText(segIdx, lineIdx, text) {
  const file = path.join(TMP, `text_${segIdx}_${lineIdx}.txt`);
  fs.writeFileSync(file, text, 'utf-8');
  return file;
}

/**
 * Build zoompan expressions for a screenshot segment.
 * Uses abs() trick to avoid commas: max(0, x) = (x + abs(x)) / 2
 */
function zoomExprs(seg) {
  const totalFrames = seg.duration * FPS;
  const holdFrames = Math.round(HOLD_SEC * FPS);
  const zoomFrames = totalFrames - holdFrames;

  const scx = seg.startCenter[0] * UPSCALE;
  const scy = seg.startCenter[1] * UPSCALE;
  const ecx = seg.endCenter[0] * UPSCALE;
  const ecy = seg.endCenter[1] * UPSCALE;
  const sz = seg.startZoom;
  const dz = seg.endZoom - seg.startZoom;
  const dCx = ecx - scx;
  const dCy = ecy - scy;

  // t = max(0, on - holdFrames) without commas
  const t = `((on-${holdFrames})+abs(on-${holdFrames}))/2`;

  // Zoom: hold at startZoom, then linear ramp
  const z = `${sz}+${dz.toFixed(4)}*${t}/${zoomFrames}`;

  // Center interpolation (same hold logic)
  const cx = `${scx}+${dCx}*${t}/${zoomFrames}`;
  const cy = `${scy}+${dCy}*${t}/${zoomFrames}`;

  // Convert center to top-left: x = cx - OUT_W/(2*z)
  const x = `(${cx})-${OUT_W / 2}/(${z})`;
  const y = `(${cy})-${OUT_H / 2}/(${z})`;

  return { z, x, y, d: totalFrames };
}

function buildScreenshotCmd(seg, segIdx, outFile) {
  const imgPath = path.join(REF, seg.image);
  const { z, x, y, d } = zoomExprs(seg);

  // Write bottom bar text to file
  const textFile = writeText(segIdx, 0, seg.bottomText);

  const filters = [
    `scale=${UP_W}:${UP_H}:flags=lanczos`,
    `zoompan=z='${z}':x='${x}':y='${y}':d=${d}:s=${OUT_W}x${OUT_H}:fps=${FPS}`,
    // Semi-transparent bottom bar
    `drawbox=x=0:y=ih-60:w=iw:h=60:color=black@0.7:t=fill`,
    // Thin accent line at top of bar
    `drawbox=x=0:y=ih-60:w=iw:h=1:color=${ACCENT}@0.4:t=fill`,
    // Bottom text
    `drawtext=textfile='${textFile}':fontfile='${FONT}':fontsize=22:fontcolor=white@0.9:x=(w-tw)/2:y=h-38`,
  ].join(',');

  return [
    'ffmpeg -y -loop 1',
    `-i "${imgPath}"`,
    `-vf "${filters}"`,
    `-t ${seg.duration}`,
    `-c:v libx264 -crf ${CRF_SEG} -pix_fmt yuv420p -r ${FPS}`,
    `"${outFile}"`,
  ].join(' ');
}

function buildCardCmd(seg, segIdx, outFile) {
  const filters = [];

  // Add drawtext for each text line
  seg.texts.forEach((t, li) => {
    const textFile = writeText(segIdx, li, t.text);
    filters.push(
      `drawtext=textfile='${textFile}':fontfile='${t.font}':fontsize=${t.size}:fontcolor=${t.color}:x=(w-tw)/2:y=${t.yExpr}`
    );
  });

  // Fade in/out
  if (seg.fadeIn) {
    filters.push(`fade=t=in:st=0:d=${seg.fadeIn}`);
  }
  if (seg.fadeOut) {
    filters.push(`fade=t=out:st=${(seg.duration - seg.fadeOut).toFixed(2)}:d=${seg.fadeOut}`);
  }

  return [
    'ffmpeg -y',
    `-f lavfi -i "color=c=${BG}:s=${OUT_W}x${OUT_H}:d=${seg.duration}:r=${FPS}"`,
    `-vf "${filters.join(',')}"`,
    `-c:v libx264 -crf ${CRF_SEG} -pix_fmt yuv420p`,
    `"${outFile}"`,
  ].join(' ');
}

// ─── Main ────────────────────────────────────────────────────────────

console.log('CASCADE Demo Video \u2014 FFmpeg Pipeline');
console.log('======================================\n');

// Check inputs
for (const seg of segments) {
  if (seg.type === 'screenshot') {
    const p = path.join(REF, seg.image);
    if (!fs.existsSync(p)) {
      console.error(`Missing screenshot: ${p}`);
      process.exit(1);
    }
  }
}

// Prep directories
if (fs.existsSync(TMP)) fs.rmSync(TMP, { recursive: true });
fs.mkdirSync(TMP, { recursive: true });
fs.mkdirSync(OUT, { recursive: true });

// ── Step 1: Generate each segment ────────────────────────────────────
console.log(`Generating ${segments.length} segments...`);
const segFiles = [];

for (let i = 0; i < segments.length; i++) {
  const seg = segments[i];
  const outFile = path.join(TMP, `seg_${String(i + 1).padStart(2, '0')}.mp4`);
  segFiles.push(outFile);

  const label = `Segment ${i + 1}/${segments.length}: ${seg.type} (${seg.duration}s)` +
    (seg.image ? ` [${seg.image}]` : '');

  if (seg.type === 'screenshot') {
    run(buildScreenshotCmd(seg, i, outFile), label);
  } else {
    run(buildCardCmd(seg, i, outFile), label);
  }
}

// ── Step 2: Concatenate with xfade transitions ──────────────────────
console.log('\nBuilding crossfade chain...');

const inputs = segFiles.map(f => `-i "${f}"`).join(' ');
let filterParts = [];
let prevLabel = '0:v';
let accumDuration = segments[0].duration;

for (let i = 1; i < segments.length; i++) {
  const offset = (accumDuration - XFADE_DUR).toFixed(3);
  const outLabel = i < segments.length - 1 ? `v${i}` : 'vout';

  filterParts.push(
    `[${prevLabel}][${i}:v]xfade=transition=fade:duration=${XFADE_DUR}:offset=${offset}[${outLabel}]`
  );

  prevLabel = outLabel;
  accumDuration += segments[i].duration - XFADE_DUR;
}

const filterGraph = filterParts.join(';');
const finalOutput = path.join(OUT, 'project.mp4');

// Write filtergraph to file to avoid shell escaping issues
const fgFile = path.join(TMP, 'xfade.txt');
fs.writeFileSync(fgFile, filterGraph, 'utf-8');

const concatCmd = [
  `ffmpeg -y ${inputs}`,
  `-filter_complex_script "${fgFile}"`,
  `-map "[vout]"`,
  `-c:v libx264 -crf ${CRF_FINAL} -pix_fmt yuv420p -movflags +faststart`,
  `"${finalOutput}"`,
].join(' ');

run(concatCmd, 'Final concatenation with crossfades');

// ── Step 3: Verify output ───────────────────────────────────────────
console.log('\n======================================');
console.log('Verifying output...\n');

try {
  const probe = execSync(
    `ffprobe -v quiet -print_format json -show_format -show_streams "${finalOutput}"`
  ).toString();
  const info = JSON.parse(probe);
  const stream = info.streams[0];
  const fmt = info.format;

  const sizeMB = (parseInt(fmt.size) / 1024 / 1024).toFixed(1);
  const duration = parseFloat(fmt.duration).toFixed(1);
  const bitrate = (parseInt(fmt.bit_rate) / 1000).toFixed(0);

  console.log(`  File:       ${path.basename(finalOutput)}`);
  console.log(`  Size:       ${sizeMB} MB`);
  console.log(`  Duration:   ${duration}s (${(parseFloat(fmt.duration) / 60).toFixed(1)} min)`);
  console.log(`  Resolution: ${stream.width}x${stream.height}`);
  console.log(`  FPS:        ${stream.r_frame_rate}`);
  console.log(`  Bitrate:    ${bitrate} kbps`);
  console.log(`  Codec:      ${stream.codec_name} (${stream.profile})`);
} catch (e) {
  console.log('Could not probe output file.');
}

// Expected duration
const expectedDur = segments.reduce((s, seg) => s + seg.duration, 0) -
  (segments.length - 1) * XFADE_DUR;
console.log(`\n  Expected:   ~${expectedDur.toFixed(1)}s`);

console.log('\n\u2713 Done!');
