import type { CSSProperties } from 'react';
import { useEffect, useMemo, useRef, useState } from 'react';
import { createPortal } from 'react-dom';
import type { SatelliteSnapshot } from '../../types/api';
import { theme } from '../../styles/theme';
import { toneColor, type Tone } from './UiPrimitives';

type SatelliteFocusDropdownVariant = 'chip' | 'panel' | 'hero';

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
  const triggerRef = useRef<HTMLButtonElement>(null);
  const menuRef = useRef<HTMLDivElement>(null);
  const [open, setOpen] = useState(false);
  const [menuStyle, setMenuStyle] = useState<CSSProperties>({});

  const emphasized = Boolean(selectedSatId) || variant === 'hero';
  const color = toneColor(emphasized ? tone : 'neutral');
  const compact = variant === 'chip';
  const hero = variant === 'hero';
  const activeSatellite = sortedSatellites.find(satellite => satellite.id === selectedSatId) ?? null;

  useEffect(() => {
    if (!open) return;

    const updatePosition = () => {
      const trigger = triggerRef.current;
      if (!trigger) return;
      const rect = trigger.getBoundingClientRect();
      const desiredWidth = Math.max(rect.width, hero ? 340 : compact ? 220 : 280);
      const estimatedHeight = Math.min(360, 56 + (sortedSatellites.length + 1) * 40);
      const canOpenBelow = rect.bottom + estimatedHeight + 12 <= window.innerHeight;
      const left = Math.min(Math.max(12, rect.left), Math.max(12, window.innerWidth - desiredWidth - 12));
      const top = canOpenBelow
        ? rect.bottom + 6
        : Math.max(12, rect.top - estimatedHeight - 6);
      const availableHeight = canOpenBelow
        ? Math.max(120, window.innerHeight - top - 12)
        : Math.max(120, rect.top - 18);

      setMenuStyle({
        position: 'fixed',
        top,
        left,
        width: desiredWidth,
        maxHeight: Math.min(360, availableHeight),
        zIndex: 1400,
      });
    };

    const closeOnOutside = (event: MouseEvent) => {
      const target = event.target as Node | null;
      if (triggerRef.current?.contains(target)) return;
      if (menuRef.current?.contains(target)) return;
      setOpen(false);
    };

    const closeOnEscape = (event: KeyboardEvent) => {
      if (event.key === 'Escape') {
        setOpen(false);
        triggerRef.current?.focus();
      }
    };

    updatePosition();
    window.addEventListener('resize', updatePosition);
    window.addEventListener('scroll', updatePosition, true);
    document.addEventListener('mousedown', closeOnOutside);
    document.addEventListener('keydown', closeOnEscape);
    return () => {
      window.removeEventListener('resize', updatePosition);
      window.removeEventListener('scroll', updatePosition, true);
      document.removeEventListener('mousedown', closeOnOutside);
      document.removeEventListener('keydown', closeOnEscape);
    };
  }, [compact, open, sortedSatellites.length]);

  const menu = open ? createPortal(
    <div
      ref={menuRef}
      role="listbox"
      aria-label={`${label} satellite selector`}
      style={{
        ...menuStyle,
        overflow: 'auto',
        padding: '8px',
        border: `1px solid ${selectedSatId ? `${color}55` : theme.colors.border}`,
        background: 'linear-gradient(180deg, rgba(10, 14, 20, 0.98), rgba(5, 7, 11, 0.99))',
        boxShadow: `0 16px 42px rgba(0,0,0,0.46), 0 0 22px ${selectedSatId ? `${color}18` : 'rgba(88,184,255,0.08)'}`,
        clipPath: theme.chamfer.buttonClipPath,
        backdropFilter: 'blur(16px)',
      }}
    >
      <div style={{ display: 'flex', flexDirection: 'column', gap: '2px', padding: '4px 6px 8px', borderBottom: `1px solid ${theme.colors.border}` }}>
        <span style={{ color: selectedSatId ? color : theme.colors.textMuted, fontSize: '8px', letterSpacing: '0.16em', textTransform: 'uppercase' }}>{label}</span>
        <span style={{ color: theme.colors.textDim, fontSize: '11px', lineHeight: 1.5 }}>
          {selectedSatId ? 'Selected satellite stays highlighted across focus-aware panels.' : 'Choose fleet overview or a single spacecraft focus.'}
        </span>
      </div>

      <div style={{ display: 'flex', flexDirection: 'column', gap: '6px', paddingTop: '8px' }}>
        <button
          type="button"
          role="option"
          aria-selected={!selectedSatId}
          onClick={() => {
            onSelectSat(null);
            setOpen(false);
          }}
          style={{
            display: 'flex',
            flexDirection: 'column',
            alignItems: 'flex-start',
            gap: '3px',
            width: '100%',
            padding: '10px 12px',
            border: `1px solid ${!selectedSatId ? `${theme.colors.primary}55` : theme.colors.border}`,
            background: !selectedSatId ? 'rgba(88, 184, 255, 0.12)' : 'rgba(255,255,255,0.02)',
            color: !selectedSatId ? theme.colors.primary : theme.colors.text,
            cursor: 'pointer',
            textAlign: 'left',
            clipPath: theme.chamfer.buttonClipPath,
          }}
        >
          <span style={{ fontSize: '11px', fontWeight: 700, letterSpacing: '0.04em' }}>{fleetLabel}</span>
          <span style={{ color: theme.colors.textDim, fontSize: '10px', lineHeight: 1.45 }}>Keep fleet-wide context and aggregate-capable panels active.</span>
        </button>

        {sortedSatellites.map(satellite => {
          const selected = satellite.id === selectedSatId;
          return (
            <button
              key={satellite.id}
              type="button"
              role="option"
              aria-selected={selected}
              onClick={() => {
                onSelectSat(satellite.id);
                setOpen(false);
              }}
              style={{
                display: 'flex',
                flexDirection: 'column',
                alignItems: 'flex-start',
                gap: '3px',
                width: '100%',
                padding: '10px 12px',
                border: `1px solid ${selected ? `${color}66` : theme.colors.border}`,
                background: selected ? `linear-gradient(180deg, ${color}18, rgba(10, 11, 14, 0.92))` : 'rgba(255,255,255,0.02)',
                color: selected ? color : theme.colors.text,
                cursor: 'pointer',
                textAlign: 'left',
                clipPath: theme.chamfer.buttonClipPath,
                boxShadow: selected ? `0 0 12px ${color}16` : 'none',
              }}
            >
              <div style={{ display: 'flex', alignItems: 'center', gap: '8px', width: '100%', justifyContent: 'space-between' }}>
                <span style={{ fontSize: '11px', fontWeight: 700, letterSpacing: '0.04em' }}>{satellite.id}</span>
                {selected ? <span style={{ color, fontSize: '9px', letterSpacing: '0.14em', textTransform: 'uppercase' }}>Selected</span> : null}
              </div>
              <span style={{ color: selected ? theme.colors.text : theme.colors.textDim, fontSize: '10px', lineHeight: 1.45 }}>
                {`${satellite.status} / ${satellite.fuel_kg.toFixed(1)} kg fuel`}
              </span>
            </button>
          );
        })}
      </div>
    </div>,
    document.body,
  ) : null;

  return (
    <div
      style={{
        display: 'inline-flex',
        flexDirection: 'column',
        gap: compact ? '2px' : hero ? '6px' : '4px',
        minWidth: compact ? '128px' : '100%',
        padding: compact ? '6px 9px' : hero ? '14px 16px' : '10px 12px',
        border: `1px solid ${emphasized ? `${color}66` : theme.colors.border}`,
        background: hero
          ? `linear-gradient(135deg, ${color}18, rgba(10, 12, 18, 0.96) 46%, rgba(6, 8, 12, 0.98))`
          : selectedSatId
            ? `linear-gradient(180deg, ${color}14, rgba(10, 11, 14, 0.92))`
            : 'linear-gradient(180deg, rgba(16, 18, 24, 0.88), rgba(8, 9, 12, 0.94))',
        clipPath: theme.chamfer.buttonClipPath,
        boxShadow: hero
          ? `0 0 28px ${color}18, inset 0 0 0 1px rgba(255,255,255,0.03)`
          : selectedSatId ? `0 0 12px ${color}16` : 'inset 0 0 0 1px rgba(255,255,255,0.03)',
        position: 'relative',
        ...style,
      }}
    >
      <span
        style={{
          color: selectedSatId ? color : theme.colors.textMuted,
          fontSize: compact ? '8px' : hero ? '9px' : '9px',
          letterSpacing: '0.14em',
          textTransform: 'uppercase',
        }}
      >
        {label}
      </span>

      <button
        ref={triggerRef}
        type="button"
        aria-haspopup="listbox"
        aria-expanded={open}
        onClick={() => setOpen(prev => !prev)}
        onKeyDown={event => {
          if (event.key === 'ArrowDown' || event.key === 'Enter' || event.key === ' ') {
            event.preventDefault();
            setOpen(true);
          }
        }}
        style={{
          display: 'flex',
          alignItems: compact ? 'center' : 'flex-start',
          justifyContent: 'space-between',
          gap: '10px',
          width: '100%',
          border: 'none',
          outline: 'none',
          background: 'transparent',
          color: selectedSatId ? color : theme.colors.text,
          fontFamily: theme.font.mono,
          cursor: 'pointer',
          padding: 0,
          textAlign: 'left',
        }}
      >
        <div style={{ display: 'flex', flexDirection: 'column', gap: compact ? '0px' : hero ? '4px' : '2px', minWidth: 0 }}>
          <span
            style={{
              color: emphasized ? color : theme.colors.text,
              fontSize: compact ? '12px' : hero ? '18px' : '15px',
              fontWeight: compact ? 600 : 700,
              lineHeight: 1.15,
              fontVariantNumeric: 'tabular-nums',
              whiteSpace: 'nowrap',
              overflow: 'hidden',
              textOverflow: 'ellipsis',
            }}
          >
            {activeSatellite?.id ?? fleetLabel}
          </span>
          {!compact ? (
            <span style={{ color: theme.colors.textDim, fontSize: hero ? '11px' : '10px', lineHeight: 1.4, maxWidth: hero ? '44ch' : 'none' }}>
              {activeSatellite
                ? `${activeSatellite.status} / ${activeSatellite.fuel_kg.toFixed(1)} kg fuel remaining`
                : hero
                  ? 'Fleet-wide mode stays active until a spacecraft is pinned from this console.'
                  : 'Fleet mode active'}
            </span>
          ) : null}
          {hero ? (
            <span style={{ color: theme.colors.textMuted, fontSize: '9px', letterSpacing: '0.12em', textTransform: 'uppercase' }}>
              {selectedSatId ? 'Pinned across focus-aware views' : 'Shared page focus controller'}
            </span>
          ) : null}
        </div>

        <div style={{ display: 'flex', alignItems: hero ? 'center' : 'flex-start', gap: '8px', flexShrink: 0 }}>
          {hero && selectedSatId ? (
            <button
              type="button"
              onClick={event => {
                event.preventDefault();
                event.stopPropagation();
                onSelectSat(null);
                setOpen(false);
              }}
              style={{
                padding: '6px 8px',
                border: `1px solid ${theme.colors.border}`,
                background: 'rgba(255,255,255,0.03)',
                color: theme.colors.textDim,
                clipPath: theme.chamfer.buttonClipPath,
                cursor: 'pointer',
                fontFamily: theme.font.mono,
                fontSize: '9px',
                letterSpacing: '0.12em',
                textTransform: 'uppercase',
              }}
            >
              Clear
            </button>
          ) : null}
          <span
            aria-hidden="true"
            style={{
              color: open || emphasized ? color : theme.colors.textMuted,
              fontSize: compact ? '11px' : hero ? '14px' : '12px',
              transform: open ? 'rotate(180deg)' : 'rotate(0deg)',
              transition: 'transform 0.18s ease',
            }}
          >
            v
          </span>
        </div>
      </button>
      {menu}
    </div>
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
