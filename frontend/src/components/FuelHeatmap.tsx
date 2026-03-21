import type { SatelliteSnapshot } from '../types/api';
import { statusColor } from '../types/api';
import { fuelColor } from '../utils/geo';
import { useSound } from '../hooks/useSound';

interface Props {
  satellites: SatelliteSnapshot[];
  selectedSatId: string | null;
  onSelectSat: (id: string | null) => void;
  maxFuelKg?: number;
}

function hexColor(n: number): string {
  return `#${n.toString(16).padStart(6, '0')}`;
}

export function FuelHeatmap({ satellites, selectedSatId, onSelectSat, maxFuelKg = 100 }: Props) {
  const { play } = useSound();
  if (satellites.length === 0) {
    return (
      <div style={{ color: '#64748b', padding: '12px', fontSize: '13px' }}>
        No satellite data
      </div>
    );
  }

  // Sort by fuel ascending (most critical first)
  const sorted = [...satellites].sort((a, b) => a.fuel_kg - b.fuel_kg);

  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: '4px', padding: '4px', overflowY: 'auto', maxHeight: '100%' }}>
      {sorted.map((sat) => {
        const fraction = Math.min(1, Math.max(0, sat.fuel_kg / maxFuelKg));
        const isSelected = sat.id === selectedSatId;
        const statusHex = hexColor(statusColor(sat.status));

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
              borderRadius: '4px',
              cursor: 'pointer',
              background: isSelected ? 'rgba(255,255,255,0.06)' : 'transparent',
              border: isSelected ? '1px solid rgba(255,255,255,0.15)' : '1px solid transparent',
              transition: 'background 0.1s',
            }}
          >
            {/* Status indicator dot */}
            <div
              style={{
                width: '8px',
                height: '8px',
                borderRadius: '50%',
                flexShrink: 0,
                background: statusHex,
                boxShadow: `0 0 4px ${statusHex}`,
              }}
            />

            {/* Satellite ID */}
            <div style={{
              fontSize: '11px',
              fontFamily: 'monospace',
              color: '#cbd5e1',
              width: '80px',
              flexShrink: 0,
              overflow: 'hidden',
              textOverflow: 'ellipsis',
              whiteSpace: 'nowrap',
            }}>
              {sat.id}
            </div>

            {/* Fuel bar */}
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
                background: fuelColor(fraction),
                borderRadius: '4px',
                transition: 'width 0.3s ease',
              }} />
            </div>

            {/* Fuel value */}
            <div style={{
              fontSize: '10px',
              fontFamily: 'monospace',
              color: fuelColor(fraction),
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
}
