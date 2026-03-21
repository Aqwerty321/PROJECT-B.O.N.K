import { useCallback } from 'react';
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
    body: JSON.stringify({ hours }),
  });
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  return res.json();
}

export default function SimControls({ disabled = false }: SimControlsProps) {
  const { play } = useSound();

  const handleStep = useCallback(async (hours: number) => {
    play('buttonPress');
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
        {STEP_OPTIONS.map(opt => (
          <button
            key={opt.label}
            disabled={disabled}
            onClick={() => handleStep(opt.hours)}
            onMouseEnter={() => play('hover')}
            style={styles.button}
          >
            {opt.label}
          </button>
        ))}
      </div>
    </div>
  );
}

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
    border: `1px solid ${theme.colors.border}`,
    borderRadius: '3px',
    background: 'rgba(58, 159, 232, 0.08)',
    color: theme.colors.primary,
    cursor: 'pointer',
    transition: 'all 0.15s ease',
  },
};
