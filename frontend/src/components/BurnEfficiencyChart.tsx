import React, { useMemo } from 'react';
import type { BurnSummary, BurnsResponse, PerSatBurnStats } from '../types/api';
import { theme } from '../styles/theme';

interface Props {
  burns: BurnsResponse | null;
  selectedSatId: string | null;
  onSelectSat?: (satelliteId: string) => void;
}

interface BurnPoint {
  satelliteId: string;
  fuelConsumedKg: number;
  avoidanceFuelConsumedKg: number;
  collisionsAvoided: number;
  burnsExecuted: number;
  avoidanceBurns: number;
  recoveryBurns: number;
  graveyardBurns: number;
}

function compactSatLabel(id: string): string {
  if (id.length <= 14) return id;
  const parts = id.split('-');
  if (parts.length >= 2) {
    return `${parts[0]}-${parts[parts.length - 1]}`;
  }
  return `${id.slice(0, 8)}...${id.slice(-3)}`;
}

function asPoint(satelliteId: string, stats: PerSatBurnStats): BurnPoint {
  return {
    satelliteId,
    fuelConsumedKg: stats.fuel_consumed_kg,
    avoidanceFuelConsumedKg: stats.avoidance_fuel_consumed_kg ?? 0,
    collisionsAvoided: stats.collisions_avoided ?? 0,
    burnsExecuted: stats.burns_executed,
    avoidanceBurns: stats.avoidance_burns_executed ?? 0,
    recoveryBurns: stats.recovery_burns_executed ?? 0,
    graveyardBurns: stats.graveyard_burns_executed ?? 0,
  };
}

function fallbackSummary(burns: BurnsResponse | null): BurnSummary {
  const points = Object.entries(burns?.per_satellite ?? {}).map(([satelliteId, stats]) => asPoint(satelliteId, stats));
  return {
    burns_executed: burns?.executed.length ?? 0,
    burns_pending: burns?.pending.length ?? 0,
    burns_dropped: burns?.dropped?.length ?? 0,
    fuel_consumed_kg: points.reduce((sum, point) => sum + point.fuelConsumedKg, 0),
    avoidance_fuel_consumed_kg: points.reduce((sum, point) => sum + point.avoidanceFuelConsumedKg, 0),
    collisions_avoided: points.reduce((sum, point) => sum + point.collisionsAvoided, 0),
    avoidance_burns_executed: points.reduce((sum, point) => sum + point.avoidanceBurns, 0),
    recovery_burns_executed: points.reduce((sum, point) => sum + point.recoveryBurns, 0),
    graveyard_burns_executed: points.reduce((sum, point) => sum + point.graveyardBurns, 0),
  };
}

function tickValues(maxValue: number, steps = 4): number[] {
  const safeMax = Math.max(maxValue, 1);
  return Array.from({ length: steps + 1 }, (_, index) => (safeMax / steps) * index);
}

function chartColor(point: BurnPoint, selectedSatId: string | null): string {
  if (point.satelliteId === selectedSatId) return theme.colors.primary;
  if (point.collisionsAvoided > 0) return theme.colors.accent;
  if (point.avoidanceFuelConsumedKg > 0) return theme.colors.warning;
  return 'rgba(153, 169, 188, 0.72)';
}

function pointRadius(point: BurnPoint, selectedSatId: string | null): number {
  const emphasis = point.satelliteId === selectedSatId ? 2 : 0;
  return Math.min(9, 4 + point.avoidanceBurns + emphasis);
}

export const BurnEfficiencyChart = React.memo(function BurnEfficiencyChart({ burns, selectedSatId, onSelectSat }: Props) {
  const points = useMemo(
    () => Object.entries(burns?.per_satellite ?? {})
      .map(([satelliteId, stats]) => asPoint(satelliteId, stats))
      .sort((a, b) => {
        if (b.collisionsAvoided !== a.collisionsAvoided) return b.collisionsAvoided - a.collisionsAvoided;
        if (Math.abs(b.fuelConsumedKg - a.fuelConsumedKg) > 1e-9) return b.fuelConsumedKg - a.fuelConsumedKg;
        return a.satelliteId.localeCompare(b.satelliteId);
      }),
    [burns],
  );

  const summary = burns?.summary ?? fallbackSummary(burns);
  const selectedPoint = selectedSatId ? points.find(point => point.satelliteId === selectedSatId) ?? null : null;
  const bestPoint = points.reduce<BurnPoint | null>((best, point) => {
    if (!best) return point;
    if (point.collisionsAvoided !== best.collisionsAvoided) return point.collisionsAvoided > best.collisionsAvoided ? point : best;
    if (Math.abs(point.avoidanceFuelConsumedKg - best.avoidanceFuelConsumedKg) > 1e-9) {
      return point.avoidanceFuelConsumedKg < best.avoidanceFuelConsumedKg ? point : best;
    }
    return point.burnsExecuted > best.burnsExecuted ? point : best;
  }, null);
  const activePoint = selectedPoint ?? bestPoint;

  const xMax = Math.max(
    1,
    summary.fuel_consumed_kg,
    summary.avoidance_fuel_consumed_kg,
    ...points.map(point => Math.max(point.fuelConsumedKg, point.avoidanceFuelConsumedKg)),
  ) * 1.15;

  const yMax = Math.max(
    1,
    summary.collisions_avoided,
    ...points.map(point => point.collisionsAvoided),
  ) + 0.5;

  const chart = {
    width: 960,
    height: 450,
    left: 96,
    right: 34,
    top: 84,
    bottom: 62,
  };
  const plotWidth = chart.width - chart.left - chart.right;
  const plotHeight = chart.height - chart.top - chart.bottom;
  const toX = (value: number) => chart.left + (Math.max(0, value) / xMax) * plotWidth;
  const toY = (value: number) => chart.height - chart.bottom - (Math.max(0, value) / yMax) * plotHeight;
  const xTicks = tickValues(xMax);
  const yTicks = tickValues(yMax);

  const efficiencyRatio = activePoint
    ? activePoint.collisionsAvoided / Math.max(activePoint.avoidanceFuelConsumedKg || activePoint.fuelConsumedKg || 1, 0.1)
    : summary.collisions_avoided / Math.max(summary.avoidance_fuel_consumed_kg || summary.fuel_consumed_kg || 1, 0.1);
  const chartCaption = activePoint
    ? `${compactSatLabel(activePoint.satelliteId)} is the current reference point for avoided-collision efficiency.`
    : 'Fleet summary diamond shows aggregate avoided collisions versus fuel consumed.';

  return (
    <div
      style={{
        flex: 1,
        minWidth: 0,
        minHeight: 0,
        position: 'relative',
        overflow: 'visible',
      }}
    >
      <svg viewBox={`0 0 ${chart.width} ${chart.height}`} style={{ width: '100%', height: 'auto', display: 'block' }}>
          <defs>
            <linearGradient id="efficiency-fill" x1="0" y1="0" x2="0" y2="1">
              <stop offset="0%" stopColor="rgba(57, 217, 138, 0.16)" />
              <stop offset="100%" stopColor="rgba(57, 217, 138, 0.02)" />
            </linearGradient>
          </defs>

          <rect x={chart.left} y={chart.top} width={plotWidth} height={plotHeight} fill="url(#efficiency-fill)" opacity="0.55" />

          {xTicks.map((tick, index) => {
            const x = toX(tick);
            return (
              <g key={`x-${index}`}>
                <line x1={x} y1={chart.top} x2={x} y2={chart.height - chart.bottom} stroke="rgba(255,255,255,0.08)" strokeWidth="1" />
                <text x={x} y={chart.height - 22} fill={theme.colors.textMuted} fontSize="10" textAnchor="middle">
                  {tick.toFixed(tick >= 10 ? 0 : 1)}
                </text>
              </g>
            );
          })}

          {yTicks.map((tick, index) => {
            const y = toY(tick);
            return (
              <g key={`y-${index}`}>
                <line x1={chart.left} y1={y} x2={chart.width - chart.right} y2={y} stroke="rgba(255,255,255,0.08)" strokeWidth="1" />
                <text x={chart.left - 16} y={y + 5} fill={theme.colors.textMuted} fontSize="10" textAnchor="end">
                  {tick.toFixed(tick >= 2 ? 0 : 1)}
                </text>
              </g>
            );
          })}

          <line x1={chart.left} y1={chart.top} x2={chart.left} y2={chart.height - chart.bottom} stroke="rgba(255,255,255,0.28)" strokeWidth="1.2" />
          <line x1={chart.left} y1={chart.height - chart.bottom} x2={chart.width - chart.right} y2={chart.height - chart.bottom} stroke="rgba(255,255,255,0.28)" strokeWidth="1.2" />

          <text x={chart.left + 4} y={chart.top + 14} fill={theme.colors.textDim} fontSize="10" letterSpacing="1px" opacity="0.7">COLLISIONS AVOIDED</text>
          <text x={chart.width - chart.right} y={chart.height - 12} fill={theme.colors.textDim} fontSize="10" textAnchor="end" letterSpacing="1px">FUEL CONSUMED (KG)</text>

          <polyline
            fill="none"
            stroke="rgba(88, 184, 255, 0.25)"
            strokeWidth="1"
            points={`${chart.left},${chart.height - chart.bottom} ${toX(summary.fuel_consumed_kg)},${toY(summary.collisions_avoided)}`}
          />

          {points.map(point => {
            const x = toX(point.fuelConsumedKg);
            const y = toY(point.collisionsAvoided);
            const color = chartColor(point, selectedSatId);
            const radius = pointRadius(point, selectedSatId);
            return (
              <g
                key={point.satelliteId}
                role={onSelectSat ? 'button' : undefined}
                tabIndex={onSelectSat ? 0 : undefined}
                aria-label={onSelectSat ? `Focus ${point.satelliteId}` : undefined}
                onClick={onSelectSat ? () => onSelectSat(point.satelliteId) : undefined}
                onKeyDown={onSelectSat ? event => {
                  if (event.key === 'Enter' || event.key === ' ') {
                    event.preventDefault();
                    onSelectSat(point.satelliteId);
                  }
                } : undefined}
                style={{ cursor: onSelectSat ? 'pointer' : 'default' }}
              >
                <circle cx={x} cy={y} r={Math.max(radius + 10, 14)} fill="transparent" />
                <circle cx={x} cy={y} r={radius + 3} fill={color} opacity="0.12" />
                <circle cx={x} cy={y} r={radius} fill={color} stroke="rgba(6, 7, 10, 0.95)" strokeWidth="1.4" />
                {point.satelliteId === selectedSatId ? <circle cx={x} cy={y} r={radius + 4.5} fill="none" stroke={theme.colors.primary} strokeWidth="1.5" opacity="0.9" /> : null}
                {(point.collisionsAvoided > 0 || point.satelliteId === selectedSatId) ? (
                  <text x={x} y={y - radius - 12} fill={color} fontSize="10" textAnchor="middle">
                    {compactSatLabel(point.satelliteId)}
                  </text>
                ) : null}
              </g>
            );
          })}

          <polygon
            points={`${toX(summary.fuel_consumed_kg)},${toY(summary.collisions_avoided) - 10} ${toX(summary.fuel_consumed_kg) + 10},${toY(summary.collisions_avoided)} ${toX(summary.fuel_consumed_kg)},${toY(summary.collisions_avoided) + 10} ${toX(summary.fuel_consumed_kg) - 10},${toY(summary.collisions_avoided)}`}
            fill={theme.colors.warning}
            stroke="rgba(5, 7, 10, 0.98)"
            strokeWidth="1.2"
          />
          <text x={toX(summary.fuel_consumed_kg) + 16} y={toY(summary.collisions_avoided) - 12} fill={theme.colors.warning} fontSize="10">
            Fleet summary
          </text>

          {activePoint ? (
            <g pointerEvents="none">
              <line
                x1={chart.left}
                y1={toY(activePoint.collisionsAvoided)}
                x2={toX(activePoint.fuelConsumedKg)}
                y2={toY(activePoint.collisionsAvoided)}
                stroke="rgba(88, 184, 255, 0.16)"
                strokeWidth="1"
                strokeDasharray="4 4"
              />
              <line
                x1={toX(activePoint.fuelConsumedKg)}
                y1={chart.height - chart.bottom}
                x2={toX(activePoint.fuelConsumedKg)}
                y2={toY(activePoint.collisionsAvoided)}
                stroke="rgba(88, 184, 255, 0.16)"
                strokeWidth="1"
                strokeDasharray="4 4"
              />
            </g>
          ) : null}
      </svg>

      <div style={{ position: 'absolute', left: '14px', top: '4px', display: 'flex', flexDirection: 'column', gap: '4px', pointerEvents: 'none' }}>
        <span style={{ color: theme.colors.textDim, fontSize: '10px', letterSpacing: '0.14em', textTransform: 'uppercase' }}>
          Efficiency reference
        </span>
        <strong style={{ color: theme.colors.text, fontSize: '13px' }}>
          {efficiencyRatio.toFixed(2)} avoided/kg
        </strong>
        <span style={{ color: theme.colors.textMuted, fontSize: '10px', maxWidth: '36ch', lineHeight: 1.45 }}>
          {points.length > 0 ? `${chartCaption} Select any point to focus that spacecraft.` : chartCaption}
        </span>
      </div>

      {points.length === 0 ? (
        <div
          style={{
            position: 'absolute',
            inset: 0,
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center',
            pointerEvents: 'none',
          }}
        >
          <div style={{ maxWidth: '32ch', textAlign: 'center', padding: '0 18px' }}>
            <strong style={{ display: 'block', color: theme.colors.text, fontSize: '13px', marginBottom: '6px' }}>Awaiting first executed maneuver</strong>
            <span style={{ color: theme.colors.textDim, fontSize: '11px', lineHeight: 1.5 }}>
              The chart is live, but fuel-to-avoidance points appear only after the backend logs real burn outcomes.
            </span>
          </div>
        </div>
      ) : null}
    </div>
  );
});
