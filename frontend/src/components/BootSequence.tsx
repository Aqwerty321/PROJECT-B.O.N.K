import { useEffect, useRef, useState, useCallback } from 'react';
import { theme } from '../styles/theme';

interface BootSequenceProps {
  onComplete: () => void;
}

// ---- constants ----

const CHAR_POOL = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*()_+-=[]{}|;:,.<>?/~`';
const FLURRY_ROWS = 24;
const FLURRY_COLS = 80;
const FLURRY_DURATION_MS = 1200;
const FLURRY_TICK_MS = 40;

const TITLE_TEXT = 'C . A . S . C . A . D . E';
const TITLE_DELAY_MS = 1200;      // starts after flurry
const TITLE_EXPAND_MS = 800;      // expand animation duration

const BORDER_DELAY_MS = 2000;     // starts after flurry start
const BORDER_DRAW_MS = 1000;

const LOG_START_MS = 2800;        // when log lines begin
const LOG_LINES = [
  '[INIT]  Propagator RK4/J2 ........... OK',
  '[INIT]  Broad-phase grid ............ OK',
  '[INIT]  Conjunction screening ....... OK',
  '[INIT]  Maneuver planner (CW) ....... OK',
  '[INIT]  Fuel budget allocator ....... OK',
  '[INIT]  NSGA-II tuner ............... OK',
  '[INIT]  REST API server ............. BOUND :8000',
  '[LOAD]  TLE catalog ................. 27,191 objects',
  '[LOAD]  Satellite constellation ..... NOMINAL',
];
const LOG_LINE_INTERVAL_MS = 220;

const READY_DELAY_MS = 300;       // after last log line
const TOTAL_DURATION_MS = 6200;   // total before onComplete fires

// ---- helper: random string ----
function randomLine(cols: number): string {
  let s = '';
  for (let i = 0; i < cols; i++) {
    s += CHAR_POOL[Math.floor(Math.random() * CHAR_POOL.length)];
  }
  return s;
}

// ---- component ----

export default function BootSequence({ onComplete }: BootSequenceProps) {
  // phases
  const [phase, setPhase] = useState<'flurry' | 'title' | 'logs' | 'ready'>('flurry');
  const [flurryLines, setFlurryLines] = useState<string[]>([]);
  const [titleVisible, setTitleVisible] = useState(false);
  const [titleExpanded, setTitleExpanded] = useState(false);
  const [borderProgress, setBorderProgress] = useState(0);
  const [visibleLogs, setVisibleLogs] = useState<number>(0);
  const [systemReady, setSystemReady] = useState(false);

  const bootSoundPlayed = useRef(false);
  const containerRef = useRef<HTMLDivElement>(null);

  // play boot sound once
  const playBootSound = useCallback(() => {
    if (bootSoundPlayed.current) return;
    bootSoundPlayed.current = true;
    const audio = new Audio('/soundfx/short_terminal_dashui_load.mp3');
    audio.volume = 0.5;
    audio.play().catch(() => {});
  }, []);

  useEffect(() => {
    playBootSound();

    const timers: ReturnType<typeof setTimeout>[] = [];
    const intervals: ReturnType<typeof setInterval>[] = [];

    // ---- Phase 1: CRT flurry ----
    const flurryInterval = setInterval(() => {
      const lines: string[] = [];
      for (let r = 0; r < FLURRY_ROWS; r++) {
        lines.push(randomLine(FLURRY_COLS));
      }
      setFlurryLines(lines);
    }, FLURRY_TICK_MS);
    intervals.push(flurryInterval);

    // stop flurry, show title
    timers.push(setTimeout(() => {
      clearInterval(flurryInterval);
      setFlurryLines([]);
      setPhase('title');
      setTitleVisible(true);
    }, FLURRY_DURATION_MS));

    // expand title
    timers.push(setTimeout(() => {
      setTitleExpanded(true);
    }, TITLE_DELAY_MS + 200));

    // ---- Phase 2: SVG border trace ----
    const borderStart = BORDER_DELAY_MS;
    const borderSteps = 30;
    const borderStepMs = BORDER_DRAW_MS / borderSteps;
    for (let i = 1; i <= borderSteps; i++) {
      timers.push(setTimeout(() => {
        setBorderProgress(i / borderSteps);
      }, borderStart + i * borderStepMs));
    }

    // ---- Phase 3: Log lines ----
    timers.push(setTimeout(() => {
      setPhase('logs');
    }, LOG_START_MS));

    for (let i = 0; i < LOG_LINES.length; i++) {
      timers.push(setTimeout(() => {
        setVisibleLogs(i + 1);
      }, LOG_START_MS + i * LOG_LINE_INTERVAL_MS));
    }

    // ---- Phase 4: SYSTEM READY ----
    const readyTime = LOG_START_MS + LOG_LINES.length * LOG_LINE_INTERVAL_MS + READY_DELAY_MS;
    timers.push(setTimeout(() => {
      setPhase('ready');
      setSystemReady(true);
    }, readyTime));

    // ---- Complete ----
    timers.push(setTimeout(() => {
      onComplete();
    }, TOTAL_DURATION_MS));

    return () => {
      timers.forEach(clearTimeout);
      intervals.forEach(clearInterval);
    };
  }, [onComplete, playBootSound]);

  // SVG border perimeter (approx for a rect)
  const perim = 2 * (100 + 100); // viewBox percentage units

  return (
    <div ref={containerRef} style={styles.container}>
      {/* CRT scanline overlay */}
      <div style={styles.scanlines} />

      {/* SVG border trace */}
      <svg
        style={styles.borderSvg}
        viewBox="0 0 100 100"
        preserveAspectRatio="none"
      >
        <rect
          x="0.5"
          y="0.5"
          width="99"
          height="99"
          fill="none"
          stroke={theme.colors.primary}
          strokeWidth="0.3"
          strokeDasharray={perim}
          strokeDashoffset={perim * (1 - borderProgress)}
          style={{ transition: 'stroke-dashoffset 0.03s linear' }}
        />
      </svg>

      {/* Flurry */}
      {phase === 'flurry' && (
        <div style={styles.flurryContainer}>
          {flurryLines.map((line, i) => (
            <div key={i} style={styles.flurryLine}>
              {line}
            </div>
          ))}
        </div>
      )}

      {/* Title */}
      {titleVisible && (
        <div
          style={{
            ...styles.titleContainer,
            opacity: titleVisible ? 1 : 0,
          }}
        >
          <div
            style={{
              ...styles.titleText,
              letterSpacing: titleExpanded ? '0.35em' : '0em',
              opacity: titleExpanded ? 1 : 0.6,
              transform: titleExpanded ? 'scale(1)' : 'scale(0.85)',
            }}
          >
            {TITLE_TEXT}
          </div>
          <div
            style={{
              ...styles.subtitle,
              opacity: titleExpanded ? 1 : 0,
              transitionDelay: '0.3s',
            }}
          >
            CONSTELLATION AUTONOMOUS SAFETY &amp; COLLISION AVOIDANCE DECISION ENGINE
          </div>
        </div>
      )}

      {/* Log lines */}
      {(phase === 'logs' || phase === 'ready') && (
        <div style={styles.logContainer}>
          {LOG_LINES.slice(0, visibleLogs).map((line, i) => (
            <div
              key={i}
              style={{
                ...styles.logLine,
                animation: 'logFlipIn 0.25s ease-out forwards',
                animationDelay: '0s',
              }}
            >
              <span style={styles.logOk}>
                {line.includes('OK') || line.includes('BOUND') || line.includes('NOMINAL')
                  ? line.replace(/(OK|BOUND :8000|27,191 objects|NOMINAL)/, '')
                  : line}
              </span>
              {line.includes('OK') && <span style={styles.logStatusOk}>OK</span>}
              {line.includes('BOUND :8000') && <span style={styles.logStatusOk}>BOUND :8000</span>}
              {line.includes('27,191 objects') && <span style={styles.logStatusOk}>27,191 objects</span>}
              {line.includes('NOMINAL') && <span style={styles.logStatusOk}>NOMINAL</span>}
            </div>
          ))}
        </div>
      )}

      {/* SYSTEM READY */}
      {systemReady && (
        <div style={styles.readyContainer}>
          <div style={styles.readyDivider} />
          <div style={styles.readyText}>SYSTEM READY</div>
        </div>
      )}

      {/* CSS keyframes injected via style tag */}
      <style>{keyframes}</style>
    </div>
  );
}

// ---- keyframes ----

const keyframes = `
@keyframes logFlipIn {
  0% {
    transform: rotateX(90deg);
    opacity: 0;
  }
  100% {
    transform: rotateX(0deg);
    opacity: 1;
  }
}

@keyframes readyPulse {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.5; }
}

@keyframes scanlineMove {
  0% { transform: translateY(-100%); }
  100% { transform: translateY(100vh); }
}
`;

// ---- styles ----

const styles: Record<string, React.CSSProperties> = {
  container: {
    position: 'fixed',
    inset: 0,
    backgroundColor: theme.colors.bg,
    display: 'flex',
    flexDirection: 'column',
    alignItems: 'center',
    justifyContent: 'center',
    fontFamily: theme.font.mono,
    color: theme.colors.primary,
    zIndex: 9999,
    overflow: 'hidden',
  },
  scanlines: {
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
  },
  borderSvg: {
    position: 'absolute',
    inset: '8px',
    width: 'calc(100% - 16px)',
    height: 'calc(100% - 16px)',
    zIndex: 2,
    pointerEvents: 'none',
  },
  flurryContainer: {
    position: 'absolute',
    inset: 0,
    display: 'flex',
    flexDirection: 'column',
    justifyContent: 'center',
    alignItems: 'center',
    zIndex: 3,
    padding: '20px',
  },
  flurryLine: {
    fontSize: '11px',
    lineHeight: '1.3',
    color: theme.colors.primaryDim,
    whiteSpace: 'pre',
    letterSpacing: '0.05em',
    fontFamily: theme.font.mono,
  },
  titleContainer: {
    position: 'absolute',
    top: '18%',
    left: 0,
    right: 0,
    textAlign: 'center',
    zIndex: 3,
    transition: 'opacity 0.4s ease',
  },
  titleText: {
    fontSize: 'clamp(24px, 4vw, 48px)',
    fontWeight: 700,
    color: theme.colors.primary,
    transition: 'letter-spacing 0.8s ease, opacity 0.6s ease, transform 0.8s ease',
    textShadow: `0 0 20px ${theme.colors.primaryDim}, 0 0 40px ${theme.colors.primaryDim}`,
    fontFamily: theme.font.mono,
  },
  subtitle: {
    fontSize: 'clamp(8px, 1.1vw, 13px)',
    fontWeight: 300,
    color: theme.colors.textDim,
    marginTop: '12px',
    letterSpacing: '0.2em',
    transition: 'opacity 0.5s ease',
    fontFamily: theme.font.mono,
  },
  logContainer: {
    position: 'absolute',
    top: '38%',
    left: '50%',
    transform: 'translateX(-50%)',
    zIndex: 3,
    perspective: '800px',
    width: 'min(600px, 85vw)',
  },
  logLine: {
    fontSize: 'clamp(10px, 1.2vw, 14px)',
    lineHeight: '1.8',
    fontFamily: theme.font.mono,
    color: theme.colors.textDim,
    whiteSpace: 'pre',
    transformOrigin: 'center left',
  },
  logOk: {
    color: theme.colors.textDim,
  },
  logStatusOk: {
    color: theme.colors.accent,
    fontWeight: 600,
  },
  readyContainer: {
    position: 'absolute',
    bottom: '18%',
    left: '50%',
    transform: 'translateX(-50%)',
    textAlign: 'center',
    zIndex: 3,
  },
  readyDivider: {
    width: '200px',
    height: '1px',
    background: `linear-gradient(90deg, transparent, ${theme.colors.primary}, transparent)`,
    margin: '0 auto 16px',
  },
  readyText: {
    fontSize: 'clamp(14px, 2vw, 22px)',
    fontWeight: 600,
    color: theme.colors.accent,
    letterSpacing: '0.4em',
    animation: 'readyPulse 1.5s ease-in-out infinite',
    textShadow: `0 0 12px ${theme.colors.accent}`,
    fontFamily: theme.font.mono,
  },
};
