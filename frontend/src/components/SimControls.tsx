import { useCallback, useState, memo } from 'react';
import { theme } from '../styles/theme';
import { useSound } from '../hooks/useSound';

interface SimControlsProps {
  disabled?: boolean;
}

const STEP_OPTIONS = [
  { label: '1H', hours: 1 },
  { label: '6H', hours: 6 },
  { label: '24H', hours: 24 },
];

async function postStep(hours: number): Promise<{ status: string; new_timestamp: string; collisions_detected: number; maneuvers_executed: number }> {
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
  const [hoveredBtn, setHoveredBtn] = useState<string | null>(null);
  const [flashBtn, setFlashBtn] = useState<string | null>(null);

  const handleStep = useCallback(async (hours: number, label: string) => {
    play('buttonPress');
    setFlashBtn(label);
    setTimeout(() => setFlashBtn(null), 200);
    try {
      await postStep(hours);
    } catch {
      play('nullClick');
    }
  }, [play]);

  return (
    <div style={styles.container}>
      <span style={styles.label}>SIM STEP</span>
      <div style={styles.buttons}>
        {STEP_OPTIONS.map(opt => {
          const isHovered = hoveredBtn === opt.label;
          const isFlash = flashBtn === opt.label;
          return (
            <button
              key={opt.label}
              disabled={disabled}
              onClick={() => handleStep(opt.hours, opt.label)}
              onMouseEnter={() => { setHoveredBtn(opt.label); play('hover'); }}
              onMouseLeave={() => setHoveredBtn(null)}
              style={{
                ...styles.button,
                background: isFlash
                  ? 'rgba(58, 159, 232, 0.3)'
                  : isHovered
                    ? 'rgba(58, 159, 232, 0.12)'
                    : 'rgba(58, 159, 232, 0.06)',
                borderColor: isHovered
                  ? 'rgba(58, 159, 232, 0.5)'
                  : 'rgba(58, 159, 232, 0.3)',
                boxShadow: isHovered
                  ? '0 0 12px rgba(58, 159, 232, 0.25)'
                  : 'none',
              }}
            >
              {opt.label}
            </button>
          );
        })}
      </div>
    </div>
  );
});

const styles: Record<string, React.CSSProperties> = {
  container: {
    display: 'flex',
    alignItems: 'center',
    gap: '10px',
    padding: '6px 0',
  },
  label: {
    fontSize: '10px',
    letterSpacing: '0.14em',
    color: theme.colors.textDim,
    fontFamily: theme.font.mono,
    flexShrink: 0,
  },
  buttons: {
    display: 'flex',
    gap: '6px',
  },
  button: {
    fontFamily: theme.font.mono,
    fontSize: '11px',
    fontWeight: 600,
    letterSpacing: '0.05em',
    padding: '4px 12px',
    border: '1px solid rgba(58, 159, 232, 0.3)',
    background: 'rgba(58, 159, 232, 0.06)',
    color: theme.colors.primary,
    cursor: 'pointer',
    transition: 'all 0.15s ease',
    clipPath: theme.chamfer.buttonClipPath,
  },
};
