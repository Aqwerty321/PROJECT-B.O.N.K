import { useCallback, useEffect, useRef, useState, memo } from 'react';
import { theme } from '../styles/theme';
import { useSound } from '../hooks/useSound';
import { useDashboard } from '../dashboard/DashboardContext';

interface SimControlsProps {
  disabled?: boolean;
}

type StepResponse = {
  status: string;
  new_timestamp: string;
  collisions_detected: number;
  maneuvers_executed: number;
};

const STEP_OPTIONS = [
  { label: '1H', hours: 1 },
  { label: '6H', hours: 6 },
  { label: '24H', hours: 24 },
];

const SPINNER_FRAMES = ['|', '/', '-', '\\'] as const;

function formatStepTimestamp(timestamp: string): string {
  const date = new Date(timestamp);
  if (Number.isNaN(date.getTime())) return 'UTC pending';
  return `${new Intl.DateTimeFormat(undefined, {
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
    hour12: false,
    timeZone: 'UTC',
  }).format(date)} UTC`;
}

function statusToneColor(tone: 'idle' | 'busy' | 'ok' | 'warning' | 'error'): string {
  switch (tone) {
    case 'busy':
      return theme.colors.primary;
    case 'ok':
      return theme.colors.accent;
    case 'warning':
      return theme.colors.warning;
    case 'error':
      return theme.colors.critical;
    default:
      return theme.colors.textMuted;
  }
}

async function postStep(hours: number): Promise<StepResponse> {
  const res = await fetch('/api/simulate/step', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ step_seconds: hours * 3600 }),
  });
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  return res.json();
}

export default memo(function SimControls({ disabled = false }: SimControlsProps) {
  const { play } = useSound();
  const { stepStatus, setStepStatus } = useDashboard();
  const [hoveredBtn, setHoveredBtn] = useState<string | null>(null);
  const [flashBtn, setFlashBtn] = useState<string | null>(null);
  const [spinnerFrame, setSpinnerFrame] = useState(0);
  const flashTimeoutRef = useRef<number | null>(null);
  const isStepping = stepStatus.tone === 'busy';
  const activeStepLabel = stepStatus.activeLabel ?? null;

  useEffect(() => {
    return () => {
      if (flashTimeoutRef.current !== null) {
        window.clearTimeout(flashTimeoutRef.current);
      }
    };
  }, []);

  useEffect(() => {
    if (!isStepping) {
      setSpinnerFrame(0);
      return;
    }

    const intervalId = window.setInterval(() => {
      setSpinnerFrame(frame => (frame + 1) % SPINNER_FRAMES.length);
    }, 140);

    return () => window.clearInterval(intervalId);
  }, [isStepping]);

  const handleStep = useCallback(async (hours: number, label: string) => {
    if (disabled || isStepping) return;

    play('buttonPress');
    setHoveredBtn(null);
    setFlashBtn(label);
    setStepStatus({
      tone: 'busy',
      title: `STEPPING ${label}`,
      detail: 'Command in flight. Mission time, track, conjunction, and burn views will refresh on ACK.',
      activeLabel: label,
      updatedAt: Date.now(),
    });
    if (flashTimeoutRef.current !== null) {
      window.clearTimeout(flashTimeoutRef.current);
    }
    flashTimeoutRef.current = window.setTimeout(() => setFlashBtn(null), 200);

    try {
      const step = await postStep(hours);
      setStepStatus({
        tone: step.collisions_detected > 0 || step.maneuvers_executed > 0 ? 'warning' : 'ok',
        title: 'STEP COMPLETE',
        detail: `${label} committed / ${formatStepTimestamp(step.new_timestamp)} / ${step.maneuvers_executed} maneuvers / ${step.collisions_detected} collisions`,
        activeLabel: null,
        updatedAt: Date.now(),
      });
    } catch (error) {
      play('nullClick');
      const detail = error instanceof Error && error.message === 'HTTP 400'
        ? 'Load telemetry before stepping the simulation.'
        : `The step command was rejected${error instanceof Error ? ` (${error.message})` : ''}.`;
      setStepStatus({
        tone: 'error',
        title: 'STEP REJECTED',
        detail,
        activeLabel: null,
        updatedAt: Date.now(),
      });
    }
  }, [disabled, isStepping, play, setStepStatus]);

  const liveStatus = isStepping
    ? {
        ...stepStatus,
        title: `STEPPING ${activeStepLabel ?? '--'} ${SPINNER_FRAMES[spinnerFrame]}`,
      }
    : stepStatus;
  const liveStatusColor = statusToneColor(liveStatus.tone);

  return (
    <div style={styles.container}>
      <span style={styles.label}>SIM STEP</span>
      <div style={styles.buttons}>
        {STEP_OPTIONS.map(opt => {
          const isHovered = hoveredBtn === opt.label;
          const isFlash = flashBtn === opt.label;
          const isActive = isStepping && activeStepLabel === opt.label;
          return (
            <button
              key={opt.label}
              disabled={disabled || isStepping}
              onClick={() => handleStep(opt.hours, opt.label)}
              onMouseEnter={() => {
                if (disabled || isStepping) return;
                setHoveredBtn(opt.label);
                play('hover');
              }}
              onMouseLeave={() => setHoveredBtn(null)}
              style={{
                ...styles.button,
                background: isFlash
                  ? 'rgba(58, 159, 232, 0.3)'
                  : isActive
                    ? 'rgba(58, 159, 232, 0.18)'
                  : isHovered
                    ? 'rgba(58, 159, 232, 0.12)'
                    : 'rgba(58, 159, 232, 0.06)',
                borderColor: isActive
                  ? 'rgba(88, 184, 255, 0.72)'
                  : isHovered
                  ? 'rgba(58, 159, 232, 0.5)'
                  : 'rgba(58, 159, 232, 0.3)',
                boxShadow: isActive
                  ? '0 0 16px rgba(58, 159, 232, 0.22)'
                  : isHovered
                  ? '0 0 12px rgba(58, 159, 232, 0.25)'
                  : 'none',
                cursor: disabled || isStepping ? 'not-allowed' : styles.button.cursor,
                opacity: disabled || isStepping ? 0.58 : 1,
              }}
            >
              {isActive ? `${opt.label}...` : opt.label}
            </button>
          );
        })}
      </div>
      <div
        role="status"
        aria-live="polite"
        aria-busy={isStepping}
        style={{
          ...styles.statusChip,
          borderColor: `${liveStatusColor}55`,
          boxShadow: `0 0 18px ${liveStatusColor}18`,
          background: liveStatus.tone === 'busy'
            ? 'linear-gradient(180deg, rgba(11, 17, 24, 0.96), rgba(8, 12, 18, 0.92))'
            : liveStatus.tone === 'error'
              ? 'linear-gradient(180deg, rgba(24, 11, 14, 0.96), rgba(16, 8, 10, 0.92))'
              : 'rgba(10, 11, 14, 0.9)',
        }}
      >
        <div style={styles.statusHeader}>
          <span style={{ ...styles.statusEyebrow, color: liveStatusColor }}>
            {isStepping ? 'Command Uplink' : 'Step Status'}
          </span>
          <div style={styles.statusMeter} aria-hidden="true">
            {SPINNER_FRAMES.map((_, index) => {
              const isLit = isStepping
                ? index === spinnerFrame
                : liveStatus.tone === 'idle'
                  ? index < 2
                  : liveStatus.tone === 'error'
                    ? index === 0
                    : true;
              return (
                <span
                  key={index}
                  style={{
                    ...styles.statusMeterBar,
                    background: isLit ? liveStatusColor : 'rgba(255, 255, 255, 0.08)',
                    opacity: isLit ? 1 : 0.45,
                  }}
                />
              );
            })}
          </div>
        </div>
        <span style={{ ...styles.statusTitle, color: liveStatus.tone === 'idle' ? theme.colors.text : liveStatusColor }}>
          {liveStatus.title}
        </span>
        <span style={styles.statusDetail}>{liveStatus.detail}</span>
      </div>
    </div>
  );
});

const styles: Record<string, React.CSSProperties> = {
  container: {
    display: 'flex',
    alignItems: 'center',
    gap: '12px',
    padding: '8px 0',
    flexWrap: 'wrap',
  },
  label: {
    fontSize: '11px',
    letterSpacing: '0.16em',
    color: theme.colors.textDim,
    fontFamily: theme.font.mono,
    flexShrink: 0,
  },
  buttons: {
    display: 'flex',
    gap: '8px',
    flexWrap: 'wrap',
  },
  statusChip: {
    display: 'flex',
    flexDirection: 'column',
    gap: '5px',
    minWidth: '250px',
    maxWidth: '420px',
    padding: '8px 12px 9px',
    border: '1px solid rgba(88, 184, 255, 0.22)',
    clipPath: theme.chamfer.buttonClipPath,
  },
  statusHeader: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'space-between',
    gap: '12px',
  },
  statusEyebrow: {
    fontSize: '9px',
    letterSpacing: '0.15em',
    textTransform: 'uppercase',
    fontFamily: theme.font.mono,
  },
  statusMeter: {
    display: 'flex',
    alignItems: 'center',
    gap: '4px',
  },
  statusMeterBar: {
    width: '10px',
    height: '3px',
    borderRadius: '999px',
    transition: 'all 0.14s ease',
  },
  statusTitle: {
    fontSize: '12px',
    letterSpacing: '0.08em',
    fontWeight: 700,
    fontFamily: theme.font.mono,
  },
  statusDetail: {
    color: theme.colors.textDim,
    fontSize: '10px',
    lineHeight: 1.55,
  },
  button: {
    fontFamily: theme.font.mono,
    fontSize: '13px',
    fontWeight: 600,
    letterSpacing: '0.05em',
    padding: '8px 16px',
    border: '1px solid rgba(88, 184, 255, 0.32)',
    background: 'rgba(88, 184, 255, 0.07)',
    color: theme.colors.primary,
    cursor: 'pointer',
    transition: 'all 0.15s ease',
    clipPath: theme.chamfer.buttonClipPath,
  },
};
