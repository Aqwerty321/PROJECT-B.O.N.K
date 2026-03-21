import React, { useState, useEffect } from 'react';
import type { SatelliteSnapshot } from '../types/api';
import { statusColor } from '../types/api';
import { fuelColor, hexColor } from '../utils/geo';
import { useSound } from '../hooks/useSound';
import { theme } from '../styles/theme';

interface Props {
  satellites: SatelliteSnapshot[];
  selectedSatId: string | null;
  onSelectSat: (id: string | null) => void;
  maxFuelKg?: number;
}

// Animated empty state placeholder bars
function EmptyState() {
  const [pulse, setPulse] = useState(0);
  useEffect(() => {
    let frame = 0;
    let running = true;
    const loop = () => {
      if (!running) return;
      frame++;
      setPulse(Math.sin(frame * 0.03) * 0.5 + 0.5);
      requestAnimationFrame(loop);
    };
    requestAnimationFrame(loop);
    return () => { running = false; };
  }, []);

  return (
    <div style={{ padding: '12px', display: 'flex', flexDirection: 'column', gap: '8px' }}>
      {[0.6, 0.4, 0.8, 0.3].map((w, i) => (
        <div key={i} style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
          <div style={{
            width: '8px',
            height: '8px',
            borderRadius: '50%',
            border: '1px solid rgba(58,159,232,0.2)',
            opacity: 0.3 + pulse * 0.3,
          }} />
          <div style={{
            flex: 1,
            height: '8px',
            background: 'rgba(255,255,255,0.04)',
            borderRadius: '4px',
            overflow: 'hidden',
          }}>
            <div style={{
              width: `${w * 100}%`,
              height: '100%',
              background: `rgba(58,159,232,${0.08 + pulse * 0.08})`,
              borderRadius: '4px',
              transition: 'opacity 0.3s',
            }} />
          </div>
        </div>
      ))}
      <div style={{
        textAlign: 'center',
        fontSize: '10px',
        color: theme.colors.textMuted,
        fontFamily: theme.font.mono,
        letterSpacing: '0.1em',
        opacity: 0.4 + pulse * 0.6,
        marginTop: '4px',
      }}>
        AWAITING SATELLITE DATA
      </div>
    </div>
  );
}

export const FuelHeatmap = React.memo(function FuelHeatmap({ satellites, selectedSatId, onSelectSat, maxFuelKg = 100 }: Props) {
  const { play } = useSound();
  if (satellites.length === 0) {
    return <EmptyState />;
  }

  // Sort by fuel ascending (most critical first)
  const sorted = [...satellites].sort((a, b) => a.fuel_kg - b.fuel_kg);

  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: '4px', padding: '4px', overflowY: 'auto', maxHeight: '100%' }}>
      {sorted.map((sat) => {
        const fraction = Math.min(1, Math.max(0, sat.fuel_kg / maxFuelKg));
        const isSelected = sat.id === selectedSatId;
        const statusHex = hexColor(statusColor(sat.status));
        const fuelCol = fuelColor(fraction);

        return (
          <div
            key={sat.id}
            onClick={() => { play('click'); onSelectSat(isSelected ? null : sat.id); }}
            onMouseEnter={() => play('hover')}
            style={{
              display: 'flex',
              alignItems: 'center',
              gap: '8px',
              padding: '5px 8px',
              cursor: 'pointer',
              background: isSelected ? 'rgba(255,255,255,0.06)' : 'transparent',
              border: isSelected ? '1px solid rgba(255,255,255,0.15)' : '1px solid transparent',
              transition: 'background 0.1s',
              clipPath: theme.chamfer.buttonClipPath,
            }}
          >
            {/* Status indicator dot with glow */}
            <div
              style={{
                width: '8px',
                height: '8px',
                borderRadius: '50%',
                flexShrink: 0,
                background: statusHex,
                boxShadow: `0 0 4px ${statusHex}, 0 0 8px ${statusHex}`,
              }}
            />

            {/* Satellite ID */}
            <div style={{
              fontSize: '11px',
              fontFamily: theme.font.mono,
              color: '#cbd5e1',
              width: '80px',
              flexShrink: 0,
              overflow: 'hidden',
              textOverflow: 'ellipsis',
              whiteSpace: 'nowrap',
            }}>
              {sat.id}
            </div>

            {/* Fuel bar with glow */}
            <div style={{
              flex: 1,
              height: '8px',
              background: 'rgba(255,255,255,0.08)',
              borderRadius: '4px',
              overflow: 'hidden',
            }}>
              <div style={{
                width: `${fraction * 100}%`,
                height: '100%',
                background: fuelCol,
                borderRadius: '4px',
                transition: 'width 0.3s ease',
                boxShadow: `0 0 6px ${fuelCol}, 0 0 2px ${fuelCol}`,
              }} />
            </div>

            {/* Fuel value */}
            <div style={{
              fontSize: '10px',
              fontFamily: theme.font.mono,
              color: fuelCol,
              width: '42px',
              textAlign: 'right',
              flexShrink: 0,
            }}>
              {sat.fuel_kg.toFixed(1)} kg
            </div>
          </div>
        );
      })}
    </div>
  );
});
