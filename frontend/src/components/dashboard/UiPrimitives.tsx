import type { CSSProperties, ReactNode } from 'react';
import { theme } from '../../styles/theme';

export type Tone = 'primary' | 'accent' | 'warning' | 'critical' | 'neutral';

export function toneColor(tone: Tone): string {
  switch (tone) {
    case 'primary':
      return theme.colors.primary;
    case 'accent':
      return theme.colors.accent;
    case 'warning':
      return theme.colors.warning;
    case 'critical':
      return theme.colors.critical;
    default:
      return theme.colors.textDim;
  }
}

export function SummaryCard({
  label,
  value,
  detail,
  tone = 'neutral',
  action,
  actionWidth = 0,
  testId,
}: {
  label: string;
  value: string;
  detail: string;
  tone?: Tone;
  action?: ReactNode;
  actionWidth?: number;
  testId?: string;
}) {
  const color = toneColor(tone);

  return (
    <div
      data-testid={testId}
      style={{
        background: 'linear-gradient(180deg, rgba(17, 19, 23, 0.94), rgba(8, 9, 12, 0.96))',
        border: `1px solid ${tone === 'neutral' ? theme.colors.border : `${color}55`}`,
        boxShadow: `0 0 14px ${tone === 'neutral' ? 'rgba(88, 184, 255, 0.06)' : `${color}12`}`,
        clipPath: theme.chamfer.clipPath,
        padding: '8px 12px',
        paddingRight: actionWidth > 0 ? `${12 + actionWidth}px` : '12px',
        minHeight: '62px',
        display: 'flex',
        flexDirection: 'column',
        justifyContent: 'space-between',
        gap: '4px',
        minWidth: 0,
        position: 'relative',
      }}
    >
      {action && (
        <div
          style={{
            position: 'absolute',
            top: '8px',
            right: '10px',
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'flex-end',
            maxWidth: `${actionWidth || 96}px`,
          }}
        >
          {action}
        </div>
      )}
      <span
        style={{
          color: theme.colors.textMuted,
          fontSize: '8px',
          letterSpacing: '0.16em',
          textTransform: 'uppercase',
        }}
      >
        {label}
      </span>
      <span
        style={{
          color,
          fontSize: 'clamp(16px, 1.15vw, 20px)',
          fontWeight: 700,
          letterSpacing: '-0.02em',
          fontVariantNumeric: 'tabular-nums',
          lineHeight: 1.05,
          overflow: 'hidden',
        }}
      >
        {value}
      </span>
      <span
        style={{
          color: theme.colors.textDim,
          fontSize: '8px',
          lineHeight: 1.25,
          display: '-webkit-box',
          WebkitBoxOrient: 'vertical',
          WebkitLineClamp: 2,
          overflow: 'hidden',
        }}
      >
        {detail}
      </span>
    </div>
  );
}

export function InfoChip({
  label,
  value,
  tone = 'neutral',
  style,
  onClick,
  active = false,
}: {
  label: string;
  value: string;
  tone?: Tone;
  style?: CSSProperties;
  onClick?: () => void;
  active?: boolean;
}) {
  const color = toneColor(tone);
  const isInteractive = Boolean(onClick);

  return (
    <div
      role={isInteractive ? 'button' : undefined}
      tabIndex={isInteractive ? 0 : undefined}
      aria-pressed={isInteractive ? active : undefined}
      onClick={onClick}
      onKeyDown={event => {
        if (!isInteractive) return;
        if (event.key === 'Enter' || event.key === ' ') {
          event.preventDefault();
          onClick?.();
        }
      }}
      style={{
        display: 'inline-flex',
        flexDirection: 'column',
        gap: '2px',
        minWidth: '82px',
        padding: '6px 9px',
        border: `1px solid ${tone === 'neutral' ? theme.colors.border : `${color}${active ? '66' : '55'}`}`,
        background: active
          ? `linear-gradient(180deg, ${color}18, rgba(10, 11, 14, 0.92))`
          : 'rgba(10, 11, 14, 0.88)',
        clipPath: theme.chamfer.buttonClipPath,
        cursor: isInteractive ? 'pointer' : 'default',
        opacity: isInteractive && !active ? 0.6 : 1,
        boxShadow: active ? `0 0 12px ${color}18` : 'none',
        transition: 'opacity 0.16s ease, box-shadow 0.16s ease, border-color 0.16s ease, background 0.16s ease',
        userSelect: 'none',
        ...style,
      }}
    >
      <span
        style={{
          color: active ? color : theme.colors.textMuted,
          fontSize: '8px',
          letterSpacing: '0.12em',
          textTransform: 'uppercase',
        }}
      >
        {label}
      </span>
      <span
        style={{
          color: active ? color : tone === 'neutral' ? theme.colors.text : color,
          fontSize: '12px',
          fontWeight: 600,
          fontVariantNumeric: 'tabular-nums',
        }}
      >
        {value}
      </span>
    </div>
  );
}

export function HeroMetric({
  label,
  value,
  detail,
  tone = 'neutral',
  action,
}: {
  label: string;
  value: string;
  detail: string;
  tone?: Tone;
  action?: ReactNode;
}) {
  const color = toneColor(tone);

  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: '4px' }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', gap: '8px', alignItems: 'flex-start' }}>
        <span style={{ color: theme.colors.textMuted, fontSize: '9px', letterSpacing: '0.14em', textTransform: 'uppercase' }}>
          {label}
        </span>
        {action}
      </div>
      <span style={{ color, fontSize: '16px', fontWeight: 700, fontVariantNumeric: 'tabular-nums' }}>
        {value}
      </span>
      <span style={{ color: theme.colors.textDim, fontSize: '10px', lineHeight: 1.4 }}>
        {detail}
      </span>
    </div>
  );
}

export function SectionHeader({
  title,
  kicker,
  description,
  aside,
}: {
  title: string;
  kicker: string;
  description: string;
  aside?: ReactNode;
}) {
  return (
    <div style={{ display: 'flex', flexWrap: 'wrap', justifyContent: 'space-between', gap: '10px', alignItems: 'flex-end' }}>
      <div style={{ display: 'flex', flexDirection: 'column', gap: '6px' }}>
        <span style={{ color: theme.colors.primary, fontSize: '9px', letterSpacing: '0.16em', textTransform: 'uppercase' }}>
          {kicker}
        </span>
        <div style={{ display: 'flex', flexDirection: 'column', gap: '2px' }}>
          <h2 style={{ fontSize: 'clamp(20px, 1.7vw, 24px)', lineHeight: 1.05, color: theme.colors.text, fontWeight: 700 }}>
            {title}
          </h2>
          <p style={{ color: theme.colors.textDim, fontSize: '12px', lineHeight: 1.45, maxWidth: '60ch' }}>
            {description}
          </p>
        </div>
      </div>
      {aside && <div style={{ minWidth: 0 }}>{aside}</div>}
    </div>
  );
}

export function DetailList({
  entries,
}: {
  entries: Array<{ label: string; value: ReactNode; tone?: Tone }>;
}) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: '8px' }}>
      {entries.map(entry => {
        const color = entry.tone ? toneColor(entry.tone) : theme.colors.text;
        return (
          <div
            key={entry.label}
            style={{
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'space-between',
              gap: '12px',
              padding: '7px 0',
              borderBottom: '1px solid rgba(255,255,255,0.05)',
            }}
          >
            <span style={{ color: theme.colors.textMuted, fontSize: '10px', letterSpacing: '0.14em', textTransform: 'uppercase' }}>
              {entry.label}
            </span>
            <span style={{ color, fontSize: '12px', fontWeight: 600, textAlign: 'right' }}>
              {entry.value}
            </span>
          </div>
        );
      })}
    </div>
  );
}

export function EmptyStatePanel({
  title,
  detail,
}: {
  title: string;
  detail: string;
}) {
  return (
    <div
      style={{
        display: 'flex',
        flexDirection: 'column',
        gap: '8px',
        padding: '16px',
        border: `1px solid ${theme.colors.border}`,
        background: 'rgba(10, 11, 14, 0.54)',
        clipPath: theme.chamfer.buttonClipPath,
      }}
    >
      <span style={{ color: theme.colors.text, fontSize: '13px', fontWeight: 700, letterSpacing: '0.08em' }}>{title}</span>
      <span style={{ color: theme.colors.textDim, fontSize: '12px', lineHeight: 1.6 }}>{detail}</span>
    </div>
  );
}
