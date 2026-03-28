import type { CSSProperties } from 'react';
import { useMemo } from 'react';
import type { SatelliteSnapshot } from '../../types/api';
import { theme } from '../../styles/theme';
import { toneColor, type Tone } from './UiPrimitives';

type SatelliteFocusDropdownVariant = 'chip' | 'panel';

export function SatelliteFocusDropdown({
  label,
  satellites,
  selectedSatId,
  onSelectSat,
  fleetLabel = 'Fleet Overview',
  tone = 'primary',
  variant = 'chip',
  style,
}: {
  label: string;
  satellites: SatelliteSnapshot[];
  selectedSatId: string | null;
  onSelectSat: (id: string | null) => void;
  fleetLabel?: string;
  tone?: Tone;
  variant?: SatelliteFocusDropdownVariant;
  style?: CSSProperties;
}) {
  const sortedSatellites = useMemo(
    () => [...satellites].sort((lhs, rhs) => lhs.id.localeCompare(rhs.id)),
    [satellites],
  );

  const color = toneColor(selectedSatId ? tone : 'neutral');
  const compact = variant === 'chip';

  return (
    <label
      style={{
        display: 'inline-flex',
        flexDirection: 'column',
        gap: compact ? '2px' : '4px',
        minWidth: compact ? '128px' : '100%',
        padding: compact ? '6px 9px' : '10px 12px',
        border: `1px solid ${selectedSatId ? `${color}66` : theme.colors.border}`,
        background: selectedSatId
          ? `linear-gradient(180deg, ${color}14, rgba(10, 11, 14, 0.92))`
          : 'linear-gradient(180deg, rgba(16, 18, 24, 0.88), rgba(8, 9, 12, 0.94))',
        clipPath: theme.chamfer.buttonClipPath,
        boxShadow: selectedSatId ? `0 0 12px ${color}16` : 'inset 0 0 0 1px rgba(255,255,255,0.03)',
        position: 'relative',
        ...style,
      }}
    >
      <span
        style={{
          color: selectedSatId ? color : theme.colors.textMuted,
          fontSize: compact ? '8px' : '9px',
          letterSpacing: '0.14em',
          textTransform: 'uppercase',
        }}
      >
        {label}
      </span>

      <div style={{ position: 'relative' }}>
        <select
          value={selectedSatId ?? ''}
          onChange={event => onSelectSat(event.target.value || null)}
          style={{
            width: '100%',
            border: 'none',
            outline: 'none',
            background: 'transparent',
            color: selectedSatId ? color : theme.colors.text,
            fontFamily: theme.font.mono,
            fontSize: compact ? '12px' : '15px',
            fontWeight: compact ? 600 : 700,
            paddingRight: '22px',
            appearance: 'none',
            WebkitAppearance: 'none',
            MozAppearance: 'none',
            cursor: 'pointer',
            lineHeight: 1.15,
            fontVariantNumeric: 'tabular-nums',
          }}
        >
          <option value="">{fleetLabel}</option>
          {sortedSatellites.map(satellite => (
            <option key={satellite.id} value={satellite.id}>
              {`${satellite.id} - ${satellite.status}`}
            </option>
          ))}
        </select>
        <span
          aria-hidden="true"
          style={{
            position: 'absolute',
            right: 0,
            top: '50%',
            transform: 'translateY(-50%)',
            color: selectedSatId ? color : theme.colors.textMuted,
            fontSize: compact ? '11px' : '12px',
            pointerEvents: 'none',
          }}
        >
          v
        </span>
      </div>
    </label>
  );
}

export function SatelliteSelectionPlaceholder({
  title = 'Satellite Focus Required',
  detail,
  tone = 'primary',
}: {
  title?: string;
  detail: string;
  tone?: Tone;
}) {
  const color = toneColor(tone);

  return (
    <div
      style={{
        display: 'flex',
        flexDirection: 'column',
        gap: '8px',
        padding: '14px 16px',
        border: `1px solid ${color}44`,
        background: `linear-gradient(180deg, ${color}10, rgba(8, 9, 12, 0.94))`,
        clipPath: theme.chamfer.buttonClipPath,
        boxShadow: `0 0 16px ${color}10`,
      }}
    >
      <span
        style={{
          color,
          fontSize: '9px',
          letterSpacing: '0.16em',
          textTransform: 'uppercase',
        }}
      >
        Fleet Mode Active
      </span>
      <span style={{ color: theme.colors.text, fontSize: '13px', fontWeight: 700, letterSpacing: '0.05em' }}>
        {title}
      </span>
      <span style={{ color: theme.colors.textDim, fontSize: '12px', lineHeight: 1.6 }}>
        {detail}
      </span>
    </div>
  );
}
