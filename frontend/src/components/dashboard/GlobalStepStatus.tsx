import { theme } from '../../styles/theme';
import { useDashboard } from '../../dashboard/DashboardContext';

function toneColor(tone: 'idle' | 'busy' | 'ok' | 'warning' | 'error') {
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

export function GlobalStepStatus({ compact = false }: { compact?: boolean }) {
  const { stepStatus } = useDashboard();
  const color = toneColor(stepStatus.tone);

  if (compact) {
    return (
      <div
        aria-label={`Command state: ${stepStatus.title}`}
        aria-live="polite"
        title={`${stepStatus.title} - ${stepStatus.detail}`}
        style={{
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          width: '100%',
          minHeight: '32px',
          border: `1px solid ${color}33`,
          background: 'rgba(9, 11, 15, 0.8)',
          clipPath: theme.chamfer.buttonClipPath,
          boxShadow: `0 0 12px ${color}12`,
        }}
      >
        <span
          aria-hidden="true"
          style={{
            width: '8px',
            height: '8px',
            borderRadius: '50%',
            background: color,
            boxShadow: `0 0 8px ${color}`,
          }}
        />
      </div>
    );
  }

  return (
    <div
      aria-live="polite"
      style={{
        display: 'flex',
        flexDirection: 'column',
        gap: '2px',
        minWidth: '100%',
        maxWidth: '100%',
        padding: '8px 10px',
        border: `1px solid ${color}44`,
        background: 'rgba(9, 11, 15, 0.88)',
        clipPath: theme.chamfer.buttonClipPath,
        boxShadow: `0 0 12px ${color}10`,
      }}
    >
      <span style={{ color, fontSize: '8px', letterSpacing: '0.14em', textTransform: 'uppercase' }}>
        Command State
      </span>
      <span style={{ color: stepStatus.tone === 'idle' ? theme.colors.text : color, fontSize: '11px', fontWeight: 700, letterSpacing: '0.06em' }}>
        {stepStatus.title}
      </span>
      <span style={{ color: theme.colors.textDim, fontSize: '9px', lineHeight: 1.4 }}>
        {stepStatus.detail}
      </span>
    </div>
  );
}
