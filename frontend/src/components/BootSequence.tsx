import { useEffect, useRef, useState, useCallback } from 'react';
import { theme } from '../styles/theme';
import { useSound } from '../hooks/useSound';

interface BootSequenceProps {
  onComplete: () => void;
}

// ---- phosphor green palette ----
const PHOSPHOR = {
  text: '#00ff41',
  textDim: '#00cc33',
  bg: '#001a00',
  bgMid: '#000d00',
  cursor: '#00ff41',
  glow: '0 0 8px rgba(0,255,65,0.6)',
};

// ---- boot log lines (creative mix) ----
const BOOT_LINES = [
  '[  0.000] BIOS POST ................................. OK',
  '[  0.003] CPU: 16-core x86_64 @ 3.8 GHz ............ ONLINE',
  '[  0.011] Memory: 65536 MiB DDR5 .................... MAPPED',
  '[  0.015] GPU: RTX 4090 (CUDA 12.4) ................. READY',
  '[  0.024] Loading kernel modules .....................',
  '[  0.031]   ext4 tmpfs overlay aufs .................. OK',
  '[  0.042]   nvidia_uvm nvidia_drm ................... OK',
  '[  0.055] Mounting /dev/sda1 on / .................... OK',
  '[  0.063] Network: eth0 10.0.1.42/24 ................ UP',
  '[  0.078] Initializing CASCADE runtime v3.2.1 .......',
  '[  0.091]   SGP4/SDP4 propagator .................... LOADED',
  '[  0.104]   RK4/J2 numerical integrator ............. LOADED',
  '[  0.112]   rk4_j2_substep(r, v, dt=60s, max_step=10s)',
  '[  0.118]   Broad-phase spatial grid (200x200x100) .. INIT',
  '[  0.131]   Narrow-phase conjunction screening ...... INIT',
  '[  0.140]   CW relative motion solver ............... INIT',
  '[  0.155]   NSGA-II multi-objective tuner ........... INIT',
  '[  0.168] Loading TLE catalog ........................',
  '  1 25544U 98067A   25080.53661689  .00016717  00000-0  10270-3 0  9025',
  '  2 25544  51.6362 208.5684 0004209 350.7582   9.3285 15.48919755252741',
  '  1 41335U 16011A   25079.91667824  .00000843  00000-0  37461-4 0  9993',
  '  2 41335  97.3215 142.6543 0012078 235.1647 124.8412 15.22174312497610',
  '[  0.201] Catalog ingested: 27,191 LEO objects',
  '[  0.218] Orbital database mounted at /var/cascade/tle',
  '[  0.234] Fuel budget allocator ...................... READY',
  '[  0.248] Maneuver planner (Hill/CW frame) .......... READY',
  '[  0.263] Recovery slot scheduler .................... READY',
  '[  0.281] REST API server binding 0.0.0.0:8000 ...... OK',
  '[  0.295] CORS: Allow-Origin http://localhost:5173',
  '[  0.310] Constellation status ....................... NOMINAL',
  '[  0.318] All subsystems initialized.',
  '',
  'PROJECTBONK_CORS_ENABLE=true ./build/ProjectBONK',
  '',
  'CASCADE ORBITAL ENGINE READY',
];

const BOOT_SCROLL_INTERVAL_MS = 55;    // ms per line (keep)
const BOOT_CURSOR_BLINK_MS = 125;      // cursor blink after scroll ends (was 500)
const BOOT_CURSOR_BLINKS = 3;          // number of blink cycles
const BOOT_FADE_MS = 150;              // green->black fade duration (was 600)

// ---- CASCADE title ----
const TITLE_TEXT = 'CASCADE';

// ---- init log lines (after CASCADE) ----
const INIT_LINES = [
  { text: '[INIT]  Propagator RK4/J2 ...........', status: 'OK' },
  { text: '[INIT]  Broad-phase grid ............', status: 'OK' },
  { text: '[INIT]  Conjunction screening .......', status: 'OK' },
  { text: '[INIT]  Maneuver planner (CW) .......', status: 'OK' },
  { text: '[INIT]  Fuel budget allocator .......', status: 'OK' },
  { text: '[INIT]  NSGA-II tuner ...............', status: 'OK' },
  { text: '[INIT]  REST API server .............', status: 'BOUND :8000' },
  { text: '[LOAD]  TLE catalog 27,191 objects ..', status: 'OK' },
  { text: '[LOAD]  Constellation ...............', status: 'NOMINAL' },
];
const INIT_LINE_INTERVAL_MS = 200;
const ASCII_SPINNER_MS = 100;          // ASCII spinner cycle speed
const SPINNER_CHARS = ['-', '\\', '|', '/'];

// ---- component ----

export default function BootSequence({ onComplete }: BootSequenceProps) {
  const { play } = useSound();

  // Phase state
  const [phase, setPhase] = useState<'boot' | 'fade' | 'cascade' | 'logs' | 'ready' | 'done'>('boot');

  // Boot terminal state
  const [bootLineIndex, setBootLineIndex] = useState(0);
  const [cursorVisible, setCursorVisible] = useState(true);
  const [bootFading, setBootFading] = useState(false);

  // CASCADE title state
  const [titleVisible, setTitleVisible] = useState(false);
  const [titleExpanded, setTitleExpanded] = useState(false);

  // Log lines state
  const [visibleLogs, setVisibleLogs] = useState(0);
  const [completedLogs, setCompletedLogs] = useState(0);
  const [borderProgress, setBorderProgress] = useState(0);

  // SYSTEM READY
  const [systemReady, setSystemReady] = useState(false);

  const bootSoundPlayed = useRef(false);
  const scrollRef = useRef<HTMLDivElement>(null);

  // ---- main timeline ----
  useEffect(() => {
    // Play boot sound via useSound hook
    if (!bootSoundPlayed.current) {
      bootSoundPlayed.current = true;
      play('boot');
    }

    const timers: ReturnType<typeof setTimeout>[] = [];
    const intervals: ReturnType<typeof setInterval>[] = [];
    let t = 0; // running time cursor

    // == Phase A: Boot terminal scroll ==
    const scrollInterval = setInterval(() => {
      setBootLineIndex(prev => {
        if (prev < BOOT_LINES.length) return prev + 1;
        return prev;
      });
    }, BOOT_SCROLL_INTERVAL_MS);
    intervals.push(scrollInterval);

    const scrollDuration = BOOT_LINES.length * BOOT_SCROLL_INTERVAL_MS;
    t += scrollDuration;

    // Stop scrolling, start cursor blink
    timers.push(setTimeout(() => {
      clearInterval(scrollInterval);
    }, t));

    // Cursor blink phase
    const blinkDuration = BOOT_CURSOR_BLINKS * 2 * BOOT_CURSOR_BLINK_MS;
    const blinkInterval = setInterval(() => {
      setCursorVisible(prev => !prev);
    }, BOOT_CURSOR_BLINK_MS);
    timers.push(setTimeout(() => {
      intervals.push(blinkInterval);
    }, t));

    t += blinkDuration;

    // Stop blink, start fade
    timers.push(setTimeout(() => {
      clearInterval(blinkInterval);
      setCursorVisible(false);
      setBootFading(true);
      setPhase('fade');
    }, t));

    t += BOOT_FADE_MS;

    // == Phase B: CASCADE title ==
    timers.push(setTimeout(() => {
      setPhase('cascade');
      setTitleVisible(true);
    }, t));

    t += 200;
    timers.push(setTimeout(() => {
      setTitleExpanded(true);
    }, t));

    t += 800; // wait for expand animation

    // == Phase C: Border trace completes, THEN log lines ==
    const borderStart = t;
    const borderSteps = 40;
    const borderDuration = 800;
    const borderStepMs = borderDuration / borderSteps;
    for (let i = 1; i <= borderSteps; i++) {
      timers.push(setTimeout(() => {
        setBorderProgress(i / borderSteps);
      }, borderStart + i * borderStepMs));
    }

    // Log lines start AFTER border completes (not overlapping)
    const logStart = borderStart + borderDuration;
    timers.push(setTimeout(() => {
      setPhase('logs');
    }, logStart));

    for (let i = 0; i < INIT_LINES.length; i++) {
      // Show line (with active spinner)
      timers.push(setTimeout(() => {
        setVisibleLogs(i + 1);
      }, logStart + i * INIT_LINE_INTERVAL_MS));

      // Complete line (spinner resolves to status text) after a short spin
      timers.push(setTimeout(() => {
        setCompletedLogs(i + 1);
      }, logStart + i * INIT_LINE_INTERVAL_MS + INIT_LINE_INTERVAL_MS - 40));
    }

    t = logStart + INIT_LINES.length * INIT_LINE_INTERVAL_MS;

    // == Phase D: SYSTEM READY ==
    t += 200; // 0.2s pause
    timers.push(setTimeout(() => {
      setPhase('ready');
      setSystemReady(true);
    }, t));

    t += 800;
    timers.push(setTimeout(() => {
      setPhase('done');
      onComplete();
    }, t));

    return () => {
      timers.forEach(clearTimeout);
      intervals.forEach(clearInterval);
    };
  }, [onComplete, play]);

  // Auto-scroll boot terminal
  useEffect(() => {
    if (scrollRef.current) {
      scrollRef.current.scrollTop = scrollRef.current.scrollHeight;
    }
  }, [bootLineIndex]);

  // ---- render ----

  const showBootTerminal = phase === 'boot' || phase === 'fade';
  const showCascade = phase === 'cascade' || phase === 'logs' || phase === 'ready' || phase === 'done';
  const showLogs = phase === 'cascade' || phase === 'logs' || phase === 'ready' || phase === 'done';

  return (
    <div style={{
      position: 'fixed',
      inset: 0,
      zIndex: 9999,
      overflow: 'hidden',
      fontFamily: theme.font.mono,
    }}>
      {/* ============ BOOT TERMINAL (Phase A) ============ */}
      {showBootTerminal && (
        <div style={{
          position: 'absolute',
          inset: 0,
          backgroundColor: bootFading ? '#000000' : PHOSPHOR.bg,
          transition: `background-color ${BOOT_FADE_MS}ms ease-out`,
          display: 'flex',
          flexDirection: 'column',
          zIndex: 10,
        }}>
          {/* Scanline overlay for CRT feel */}
          <div style={{
            position: 'absolute',
            inset: 0,
            background: `repeating-linear-gradient(
              0deg,
              transparent,
              transparent 2px,
              rgba(0,255,65,0.03) 2px,
              rgba(0,255,65,0.03) 4px
            )`,
            pointerEvents: 'none',
            zIndex: 2,
          }} />

          {/* Phosphor glow overlay */}
          <div style={{
            position: 'absolute',
            inset: 0,
            background: 'radial-gradient(ellipse at center, rgba(0,255,65,0.06) 0%, transparent 70%)',
            pointerEvents: 'none',
            zIndex: 1,
            opacity: bootFading ? 0 : 1,
            transition: `opacity ${BOOT_FADE_MS}ms ease-out`,
          }} />

          {/* Boot text */}
          <div
            ref={scrollRef}
            style={{
              flex: 1,
              padding: '16px 24px',
              overflowY: 'hidden',
              zIndex: 3,
              opacity: bootFading ? 0 : 1,
              transition: `opacity ${BOOT_FADE_MS}ms ease-out`,
            }}
          >
            {BOOT_LINES.slice(0, bootLineIndex).map((line, i) => (
              <div key={i} style={{
                fontSize: 'clamp(11px, 1.3vw, 14px)',
                lineHeight: '1.5',
                color: PHOSPHOR.text,
                textShadow: PHOSPHOR.glow,
                whiteSpace: 'pre',
                fontFamily: theme.font.mono,
              }}>
                {line || '\u00A0'}
              </div>
            ))}
            {/* Blinking cursor */}
            {!bootFading && bootLineIndex >= BOOT_LINES.length && (
              <span style={{
                fontSize: 'clamp(11px, 1.3vw, 14px)',
                color: cursorVisible ? PHOSPHOR.cursor : 'transparent',
                textShadow: cursorVisible ? PHOSPHOR.glow : 'none',
                fontFamily: theme.font.mono,
              }}>_</span>
            )}
          </div>
        </div>
      )}

      {/* ============ CASCADE + LOGS (Phase B-D) ============ */}
      {showCascade && (
        <div style={{
          position: 'absolute',
          inset: 0,
          backgroundColor: theme.colors.bg,
          display: 'flex',
          flexDirection: 'column',
          alignItems: 'center',
          justifyContent: 'center',
          zIndex: 5,
        }}>
          {/* CRT scanlines */}
          <div style={{
            position: 'absolute',
            inset: 0,
            background: `repeating-linear-gradient(
              0deg,
              transparent,
              transparent 2px,
              ${theme.colors.scanline} 2px,
              ${theme.colors.scanline} 4px
            )`,
            pointerEvents: 'none',
            zIndex: 1,
          }} />

          {/* CASCADE Title */}
          <div style={{
            position: 'absolute',
            top: '16%',
            left: 0,
            right: 0,
            textAlign: 'center',
            zIndex: 3,
            opacity: titleVisible ? 1 : 0,
            transition: 'opacity 0.4s ease',
          }}>
            <div style={{
              fontSize: 'clamp(28px, 5vw, 56px)',
              fontWeight: 700,
              color: theme.colors.primary,
              letterSpacing: titleExpanded ? '0.4em' : '0em',
              opacity: titleExpanded ? 1 : 0.6,
              transform: titleExpanded ? 'scale(1)' : 'scale(0.85)',
              transition: 'letter-spacing 0.8s ease, opacity 0.6s ease, transform 0.8s ease',
              textShadow: `0 0 20px ${theme.colors.primaryDim}, 0 0 40px ${theme.colors.primaryDim}`,
              fontFamily: theme.font.mono,
            }}>
              {TITLE_TEXT}
            </div>
            <div style={{
              fontSize: 'clamp(8px, 1vw, 12px)',
              fontWeight: 300,
              color: theme.colors.textDim,
              marginTop: '12px',
              letterSpacing: '0.18em',
              opacity: titleExpanded ? 1 : 0,
              transition: 'opacity 0.5s ease',
              transitionDelay: '0.3s',
              fontFamily: theme.font.mono,
            }}>
              CONSTELLATION AUTONOMOUS SAFETY &amp; COLLISION AVOIDANCE DECISION ENGINE
            </div>
          </div>

          {/* Log area with SVG border trace -- fixed size box */}
          {showLogs && (
            <div style={{
              position: 'absolute',
              top: '36%',
              left: '50%',
              transform: 'translateX(-50%)',
              zIndex: 3,
              width: 'min(580px, 85vw)',
              minHeight: '220px',
            }}>
              {/* SVG border -- traces from mid-top, thicker stroke */}
              <svg
                style={{
                  position: 'absolute',
                  top: '-10px',
                  left: '-12px',
                  width: 'calc(100% + 24px)',
                  height: 'calc(100% + 20px)',
                  pointerEvents: 'none',
                  overflow: 'visible',
                }}
                viewBox="0 0 200 100"
                preserveAspectRatio="none"
              >
                <BorderTrace progress={borderProgress} />
              </svg>

              {/* Log lines */}
              <div style={{ padding: '8px 4px' }}>
                {INIT_LINES.slice(0, visibleLogs).map((line, i) => {
                  const isCompleted = i < completedLogs;
                  return (
                    <div key={i} style={{
                      display: 'flex',
                      alignItems: 'center',
                      gap: '8px',
                      fontSize: 'clamp(10px, 1.15vw, 13px)',
                      lineHeight: '1.9',
                      fontFamily: theme.font.mono,
                      color: theme.colors.textDim,
                      whiteSpace: 'pre',
                    }}>
                      <span style={{ flex: 1 }}>{line.text}</span>
                      {isCompleted ? (
                        <span style={{
                          color: theme.colors.textDim,
                          fontWeight: 400,
                          minWidth: '90px',
                          textAlign: 'right',
                        }}>{line.status}</span>
                      ) : (
                        <AsciiSpinner />
                      )}
                    </div>
                  );
                })}
              </div>
            </div>
          )}

          {/* SYSTEM READY */}
          {systemReady && (
            <div style={{
              position: 'absolute',
              bottom: '16%',
              left: '50%',
              transform: 'translateX(-50%)',
              textAlign: 'center',
              zIndex: 3,
            }}>
              <div style={{
                width: '200px',
                height: '1px',
                background: `linear-gradient(90deg, transparent, ${theme.colors.primary}, transparent)`,
                margin: '0 auto 14px',
              }} />
              <div style={{
                fontSize: 'clamp(14px, 2vw, 22px)',
                fontWeight: 600,
                color: theme.colors.primary,
                letterSpacing: '0.4em',
                animation: 'readyPulse 1.5s ease-in-out infinite',
                textShadow: `0 0 16px ${theme.colors.primaryDim}, 0 0 32px ${theme.colors.primaryDim}`,
                fontFamily: theme.font.mono,
              }}>
                SYSTEM READY
              </div>
            </div>
          )}
        </div>
      )}

      <style>{keyframes}</style>
    </div>
  );
}

// ---- ASCII Spinner: cycles through - \ | / ----

function AsciiSpinner() {
  const [index, setIndex] = useState(0);

  useEffect(() => {
    const interval = setInterval(() => {
      setIndex(prev => (prev + 1) % SPINNER_CHARS.length);
    }, ASCII_SPINNER_MS);
    return () => clearInterval(interval);
  }, []);

  return (
    <span style={{
      display: 'inline-block',
      minWidth: '90px',
      textAlign: 'right',
      fontFamily: theme.font.mono,
      color: theme.colors.primary,
      fontWeight: 600,
      fontSize: 'inherit',
    }}>
      {SPINNER_CHARS[index]}
    </span>
  );
}

// ---- SVG border trace starting from mid-top ----

function BorderTrace({ progress }: { progress: number }) {
  // Rectangle: top-left(0,0) -> top-right(200,0) -> bottom-right(200,100) -> bottom-left(0,100) -> close
  // We start from mid-top (100,0) and trace clockwise and counter-clockwise simultaneously.
  // Full perimeter = 2*200 + 2*100 = 600 units
  // Half perimeter = 300 (each direction traces half)

  // Path from mid-top clockwise: (100,0) -> (200,0) -> (200,100) -> (0,100) -> (0,0) -> (100,0)
  const path = 'M 100,0 L 200,0 L 200,100 L 0,100 L 0,0 L 100,0';
  const totalLength = 600; // 100 + 200 + 200 + 100 + 100

  return (
    <path
      d={path}
      fill="none"
      stroke={theme.colors.primary}
      strokeWidth="1.5"
      strokeDasharray={totalLength}
      strokeDashoffset={totalLength * (1 - progress)}
      opacity={0.7}
      style={{ transition: 'stroke-dashoffset 0.02s linear' }}
    />
  );
}

// ---- keyframes ----

const keyframes = `
@keyframes readyPulse {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.5; }
}
`;
