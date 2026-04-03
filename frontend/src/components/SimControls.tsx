import { useCallback, useEffect, useRef, useState, memo } from 'react';
import { theme } from '../styles/theme';
import { useSound } from '../hooks/useSound';
import { useDashboard } from '../dashboard/DashboardContext';

interface SimControlsProps {
  disabled?: boolean;
  compact?: boolean;
  layout?: 'panel' | 'rail' | 'cluster';
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

/** Delay between auto-play steps (ms). */
const AUTO_STEP_DELAY_MS = 3000;
/** Auto-play advances 1 hour per tick. */
const AUTO_STEP_HOURS = 1;

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

export default memo(function SimControls({ disabled = false, compact = false, layout = 'panel' }: SimControlsProps) {
  const { play } = useSound();
  const { stepStatus, setStepStatus, model } = useDashboard();
  const [hoveredBtn, setHoveredBtn] = useState<string | null>(null);
  const [flashBtn, setFlashBtn] = useState<string | null>(null);
  const [spinnerFrame, setSpinnerFrame] = useState(0);
  const [autoPlay, setAutoPlay] = useState(false);
  const flashTimeoutRef = useRef<number | null>(null);
  const autoPlayRef = useRef(false);
  const autoTimerRef = useRef<number | null>(null);
  const isStepping = stepStatus.tone === 'busy';
  const activeStepLabel = stepStatus.activeLabel ?? null;

  // Keep ref in sync so the async loop can read current state
  autoPlayRef.current = autoPlay;

  useEffect(() => {
    return () => {
      if (flashTimeoutRef.current !== null) {
        window.clearTimeout(flashTimeoutRef.current);
      }
      if (autoTimerRef.current !== null) {
        window.clearTimeout(autoTimerRef.current);
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

  // ---------- Auto-play loop ----------
  const runAutoStep = useCallback(async () => {
    if (!autoPlayRef.current) return;

    setStepStatus({
      tone: 'busy',
      title: 'AUTO-STEPPING 1H',
      detail: 'Auto-play active. Advancing simulation 1H per cycle.',
      activeLabel: 'AUTO',
      updatedAt: Date.now(),
    });

    try {
      const step = await postStep(AUTO_STEP_HOURS);
      if (!autoPlayRef.current) return; // stopped while in-flight

      setStepStatus({
        tone: step.collisions_detected > 0 || step.maneuvers_executed > 0 ? 'warning' : 'ok',
        title: 'AUTO STEP OK',
        detail: `1H committed / ${formatStepTimestamp(step.new_timestamp)} / ${step.maneuvers_executed} maneuvers / ${step.collisions_detected} collisions`,
        activeLabel: null,
        updatedAt: Date.now(),
      });

      // Schedule next tick
      if (autoPlayRef.current) {
        autoTimerRef.current = window.setTimeout(runAutoStep, AUTO_STEP_DELAY_MS);
      }
    } catch {
      // Stop auto-play on error
      autoPlayRef.current = false;
      setAutoPlay(false);
      setStepStatus({
        tone: 'error',
        title: 'AUTO-PLAY STOPPED',
        detail: 'A step failed during auto-play. Halted to prevent cascading errors.',
        activeLabel: null,
        updatedAt: Date.now(),
      });
    }
  }, [setStepStatus]);

  const toggleAutoPlay = useCallback(() => {
    if (disabled) return;

    if (autoPlay) {
      // Stop
      autoPlayRef.current = false;
      setAutoPlay(false);
      if (autoTimerRef.current !== null) {
        window.clearTimeout(autoTimerRef.current);
        autoTimerRef.current = null;
      }
      play('buttonPress');
      setStepStatus({
        tone: 'ok',
        title: 'AUTO-PLAY OFF',
        detail: 'Simulation auto-advance stopped. Use manual step buttons to continue.',
        activeLabel: null,
        updatedAt: Date.now(),
      });
    } else {
      // Start
      if (isStepping) return; // don't start while a manual step is in-flight
      autoPlayRef.current = true;
      setAutoPlay(true);
      play('buttonPress');
      // Kick off first step immediately
      runAutoStep();
    }
  }, [disabled, autoPlay, isStepping, play, setStepStatus, runAutoStep]);

  // Stop auto-play if component is disabled externally
  useEffect(() => {
    if (disabled && autoPlay) {
      autoPlayRef.current = false;
      setAutoPlay(false);
      if (autoTimerRef.current !== null) {
        window.clearTimeout(autoTimerRef.current);
        autoTimerRef.current = null;
      }
    }
  }, [disabled, autoPlay]);

  const handleStep = useCallback(async (hours: number, label: string) => {
    if (disabled || isStepping || autoPlay) return;

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
  }, [disabled, isStepping, autoPlay, play, setStepStatus]);

  const liveStatus = isStepping
    ? {
        ...stepStatus,
        title: `STEPPING ${activeStepLabel ?? '--'} ${SPINNER_FRAMES[spinnerFrame]}`,
      }
    : stepStatus;
  const liveStatusColor = statusToneColor(liveStatus.tone);
  const staleActionFreeze = model.truthBanner.snapshotSeverity === 'critical';
  const isCluster = layout === 'cluster';

  const manualDisabled = disabled || staleActionFreeze || isStepping || autoPlay;
  const headingEyebrow = layout === 'rail' ? 'Global Simulation' : 'Simulation Controls';
  const headingTitle = layout === 'rail' ? 'Simulate Mission Time' : 'Advance Mission Time';
  const headingDetail = staleActionFreeze
    ? 'Snapshot freshness is critical. Restore the feed before issuing another simulation step.'
    : layout === 'rail'
      ? `Current clock ${model.missionValue}. Step the simulation from any route and refresh the shared track, threat, and burn picture.`
      : 'Click a step size to move mission time forward and refresh the shared track, threat, and burn picture.';
  const statusTitle = staleActionFreeze ? 'STEP FREEZE ACTIVE' : liveStatus.title;
  const statusDetail = staleActionFreeze
    ? 'Simulation stepping is paused because the snapshot feed is critically stale. Restore freshness before issuing a risky command.'
    : liveStatus.detail;
  const statusEyebrow = autoPlay ? 'Auto-Play Active' : isStepping ? 'Command Uplink' : 'Simulation Status';
  const controlsGridTemplate = compact
    ? '1fr'
    : layout === 'rail'
      ? 'minmax(220px, 0.92fr) auto minmax(260px, 1.12fr)'
      : 'minmax(200px, 1.05fr) auto minmax(260px, 1.15fr)';
  const buttonBaseStyle = isCluster ? styles.clusterButton : styles.button;
  const autoButtonLabel = isCluster
    ? (autoPlay ? 'STOP' : 'AUTO')
    : (autoPlay ? 'AUTO STOP' : 'AUTO PLAY');
  const clusterTone = staleActionFreeze
    ? theme.colors.warning
    : liveStatus.tone === 'idle'
      ? theme.colors.textMuted
      : liveStatusColor;

  const actionButtons = (
    <>
      {STEP_OPTIONS.map(opt => {
        const isHovered = hoveredBtn === opt.label;
        const isFlash = flashBtn === opt.label;
        const isActive = isStepping && activeStepLabel === opt.label;
        return (
          <button
            key={opt.label}
            disabled={manualDisabled}
            onClick={() => handleStep(opt.hours, opt.label)}
            onMouseEnter={() => {
              if (manualDisabled) return;
              setHoveredBtn(opt.label);
              play('hover');
            }}
            onMouseLeave={() => setHoveredBtn(null)}
            style={{
              ...buttonBaseStyle,
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
              cursor: manualDisabled ? 'not-allowed' : buttonBaseStyle.cursor,
              opacity: manualDisabled ? 0.58 : 1,
            }}
          >
            {isActive ? `${opt.label}...` : opt.label}
          </button>
        );
      })}

      <button
        disabled={disabled || staleActionFreeze || (isStepping && !autoPlay)}
        onClick={toggleAutoPlay}
        onMouseEnter={() => {
          if (!disabled && !staleActionFreeze && !isStepping) {
            setHoveredBtn('AUTO');
            play('hover');
          }
        }}
        onMouseLeave={() => setHoveredBtn(null)}
        style={{
          ...buttonBaseStyle,
          background: autoPlay
            ? 'rgba(57, 217, 138, 0.18)'
            : hoveredBtn === 'AUTO'
              ? 'rgba(57, 217, 138, 0.10)'
              : 'rgba(57, 217, 138, 0.04)',
          borderColor: autoPlay
            ? 'rgba(57, 217, 138, 0.72)'
            : hoveredBtn === 'AUTO'
              ? 'rgba(57, 217, 138, 0.45)'
              : 'rgba(57, 217, 138, 0.28)',
          color: autoPlay ? theme.colors.accent : theme.colors.textDim,
          boxShadow: autoPlay
            ? '0 0 16px rgba(57, 217, 138, 0.22), inset 0 0 12px rgba(57, 217, 138, 0.08)'
            : hoveredBtn === 'AUTO'
              ? '0 0 12px rgba(57, 217, 138, 0.18)'
              : 'none',
          cursor: disabled || staleActionFreeze || (isStepping && !autoPlay) ? 'not-allowed' : 'pointer',
          opacity: disabled || staleActionFreeze ? 0.58 : 1,
        }}
      >
        {autoButtonLabel}
      </button>
    </>
  );

  if (isCluster) {
    return (
      <div
        data-testid="global-sim-controls"
        aria-label={`Simulation step controls. ${statusTitle}`}
        title={`${statusTitle} - ${statusDetail}`}
        style={{
          ...styles.clusterContainer,
          borderColor: liveStatus.tone === 'idle' && !staleActionFreeze ? theme.colors.border : `${clusterTone}55`,
          boxShadow: liveStatus.tone === 'idle' && !staleActionFreeze ? 'none' : `0 0 14px ${clusterTone}12`,
        }}
      >
        <span style={{ ...styles.clusterLabel, color: liveStatus.tone === 'idle' && !staleActionFreeze ? theme.colors.textMuted : clusterTone }}>
          Sim Step
        </span>
        <div style={styles.clusterButtons}>{actionButtons}</div>
      </div>
    );
  }

  const statusContent = (
    <>
      <div style={styles.statusHeader}>
        <span style={{ ...styles.statusEyebrow, color: liveStatusColor }}>
          {statusEyebrow}
        </span>
        <div style={styles.statusMeter} aria-hidden="true">
          {SPINNER_FRAMES.map((_, index) => {
            const isLit = isStepping
              ? index === spinnerFrame
              : autoPlay
                ? true
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
                  background: isLit
                    ? (autoPlay && !isStepping ? theme.colors.accent : liveStatusColor)
                    : 'rgba(255, 255, 255, 0.08)',
                  opacity: isLit ? 1 : 0.45,
                }}
              />
            );
          })}
        </div>
      </div>
      <span style={{ ...styles.statusTitle, color: staleActionFreeze ? theme.colors.warning : liveStatus.tone === 'idle' ? theme.colors.text : liveStatusColor }}>
        {statusTitle}
      </span>
      <span style={layout === 'rail' ? styles.statusDetailClamp : styles.statusDetail}>
        {statusDetail}
      </span>
    </>
  );

  if (layout === 'rail') {
    return (
      <div
        data-testid="global-sim-controls"
        style={{
          ...styles.railContainer,
          gridTemplateColumns: controlsGridTemplate,
          alignItems: compact ? 'stretch' : 'center',
        }}
      >
        <div style={styles.commandCopy}>
          <span style={styles.commandEyebrow}>{headingEyebrow}</span>
          <strong style={styles.commandTitle}>{headingTitle}</strong>
          <span style={styles.commandDetail}>{headingDetail}</span>
        </div>

        <div style={styles.actionPanel}>
          <span style={styles.actionLabel}>Click To Simulate</span>
          <div style={styles.buttons}>{actionButtons}</div>
        </div>

        <div
          data-testid="sim-step-status"
          role="status"
          aria-live="polite"
          aria-busy={isStepping}
          style={{
            ...styles.railStatusChip,
            borderColor: `${liveStatusColor}55`,
            boxShadow: `0 0 18px ${liveStatusColor}18`,
            background: liveStatus.tone === 'busy'
              ? 'linear-gradient(180deg, rgba(11, 17, 24, 0.96), rgba(8, 12, 18, 0.92))'
              : liveStatus.tone === 'error'
                ? 'linear-gradient(180deg, rgba(24, 11, 14, 0.96), rgba(16, 8, 10, 0.92))'
                : 'rgba(10, 11, 14, 0.92)',
          }}
        >
          {statusContent}
        </div>
      </div>
    );
  }

  return (
    <div
      data-testid="global-sim-controls"
      style={{
        ...styles.container,
        gridTemplateColumns: controlsGridTemplate,
        alignItems: compact ? 'stretch' : 'center',
      }}
    >
      <div style={styles.commandCopy}>
        <span style={styles.commandEyebrow}>{headingEyebrow}</span>
        <strong style={styles.commandTitle}>{headingTitle}</strong>
        <span style={styles.commandDetail}>{headingDetail}</span>
      </div>

      <div style={styles.actionPanel}>
        <span style={styles.actionLabel}>Click To Simulate</span>
        <div style={styles.buttons}>{actionButtons}</div>
      </div>

      <div
        data-testid="sim-step-status"
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
        {statusContent}
      </div>
    </div>
  );
});

const styles: Record<string, React.CSSProperties> = {
  container: {
    display: 'grid',
    gap: '10px',
    padding: '10px 12px',
    border: `1px solid ${theme.colors.border}`,
    background: 'linear-gradient(180deg, rgba(11, 15, 22, 0.92), rgba(7, 10, 15, 0.97))',
    clipPath: theme.chamfer.buttonClipPath,
    boxShadow: 'inset 0 0 0 1px rgba(255,255,255,0.02), 0 0 18px rgba(88, 184, 255, 0.08)',
  },
  railContainer: {
    display: 'grid',
    gap: '10px',
    padding: '8px 12px',
    border: `1px solid ${theme.colors.border}`,
    background: 'linear-gradient(90deg, rgba(12, 16, 24, 0.96), rgba(8, 12, 19, 0.96) 42%, rgba(7, 10, 15, 0.98))',
    clipPath: theme.chamfer.buttonClipPath,
    boxShadow: 'inset 0 0 0 1px rgba(255,255,255,0.02), 0 0 20px rgba(88, 184, 255, 0.08)',
  },
  commandCopy: {
    display: 'flex',
    flexDirection: 'column',
    gap: '4px',
    minWidth: 0,
  },
  commandEyebrow: {
    fontSize: '8px',
    letterSpacing: '0.16em',
    color: theme.colors.textDim,
    textTransform: 'uppercase',
    fontFamily: theme.font.mono,
  },
  commandTitle: {
    color: theme.colors.text,
    fontSize: '14px',
    lineHeight: 1.15,
    fontWeight: 700,
    letterSpacing: '0.04em',
  },
  commandDetail: {
    color: theme.colors.textDim,
    fontSize: '10px',
    lineHeight: 1.5,
  },
  actionPanel: {
    display: 'flex',
    flexDirection: 'column',
    gap: '6px',
    minWidth: 0,
  },
  actionLabel: {
    color: theme.colors.textMuted,
    fontSize: '8px',
    letterSpacing: '0.16em',
    textTransform: 'uppercase',
    fontFamily: theme.font.mono,
  },
  buttons: {
    display: 'flex',
    gap: '8px',
    flexWrap: 'wrap',
  },
  clusterContainer: {
    display: 'inline-flex',
    alignItems: 'center',
    gap: '6px',
    padding: '6px 8px',
    border: `1px solid ${theme.colors.border}`,
    background: 'rgba(8, 10, 14, 0.72)',
    clipPath: theme.chamfer.buttonClipPath,
    maxWidth: '100%',
    flexWrap: 'wrap',
  },
  clusterLabel: {
    color: theme.colors.textMuted,
    fontSize: '8px',
    letterSpacing: '0.16em',
    textTransform: 'uppercase',
    fontFamily: theme.font.mono,
  },
  clusterButtons: {
    display: 'inline-flex',
    alignItems: 'center',
    gap: '6px',
    flexWrap: 'wrap',
  },
  statusChip: {
    display: 'flex',
    flexDirection: 'column',
    gap: '5px',
    minWidth: 0,
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
  statusDetailClamp: {
    color: theme.colors.textDim,
    fontSize: '10px',
    lineHeight: 1.45,
    display: '-webkit-box',
    WebkitBoxOrient: 'vertical',
    WebkitLineClamp: 2,
    overflow: 'hidden',
  },
  railStatusChip: {
    display: 'flex',
    flexDirection: 'column',
    gap: '4px',
    minWidth: 0,
    padding: '8px 12px',
    border: '1px solid rgba(88, 184, 255, 0.22)',
    clipPath: theme.chamfer.buttonClipPath,
    justifyContent: 'center',
  },
  button: {
    fontFamily: theme.font.mono,
    fontSize: '12px',
    fontWeight: 600,
    letterSpacing: '0.08em',
    padding: '8px 12px',
    border: '1px solid rgba(88, 184, 255, 0.32)',
    background: 'rgba(88, 184, 255, 0.07)',
    color: theme.colors.primary,
    cursor: 'pointer',
    transition: 'all 0.15s ease',
    clipPath: theme.chamfer.buttonClipPath,
  },
  clusterButton: {
    fontFamily: theme.font.mono,
    fontSize: '9px',
    fontWeight: 600,
    letterSpacing: '0.12em',
    padding: '5px 8px',
    border: `1px solid ${theme.colors.border}`,
    background: 'rgba(255,255,255,0.03)',
    color: theme.colors.textDim,
    cursor: 'pointer',
    transition: 'all 0.15s ease',
    clipPath: theme.chamfer.buttonClipPath,
    textTransform: 'uppercase',
  },
};
