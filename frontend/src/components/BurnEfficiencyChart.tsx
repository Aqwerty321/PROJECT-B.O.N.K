import React, { useMemo } from 'react';
import type { BurnSummary, BurnsResponse, PerSatBurnStats } from '../types/api';
import { theme } from '../styles/theme';

interface Props {
  burns: BurnsResponse | null;
  selectedSatId: string | null;
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

function summaryCard(label: string, value: string, detail: string, tone: string) {
  return (
    <div
      key={label}
      style={{
        minHeight: '88px',
        padding: '10px 12px',
        border: `1px solid ${tone}`,
        background: 'linear-gradient(180deg, rgba(7, 10, 15, 0.92), rgba(5, 8, 12, 0.98))',
        clipPath: theme.chamfer.buttonClipPath,
        boxShadow: `inset 0 0 18px rgba(0, 0, 0, 0.28), 0 0 16px ${tone}18`,
        display: 'flex',
        flexDirection: 'column',
        gap: '6px',
      }}
    >
      <span style={{ color: tone, fontSize: '10px', letterSpacing: '0.14em', textTransform: 'uppercase' }}>{label}</span>
      <strong style={{ color: theme.colors.text, fontSize: '20px', lineHeight: 1 }}>{value}</strong>
      <span style={{ color: theme.colors.textDim, fontSize: '11px', lineHeight: 1.45 }}>{detail}</span>
    </div>
  );
}

export const BurnEfficiencyChart = React.memo(function BurnEfficiencyChart({ burns, selectedSatId }: Props) {
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
    width: 620,
    height: 270,
    left: 62,
    right: 22,
    top: 18,
    bottom: 40,
  };
  const plotWidth = chart.width - chart.left - chart.right;
  const plotHeight = chart.height - chart.top - chart.bottom;
  const toX = (value: number) => chart.left + (Math.max(0, value) / xMax) * plotWidth;
  const toY = (value: number) => chart.height - chart.bottom - (Math.max(0, value) / yMax) * plotHeight;
  const xTicks = tickValues(xMax);
  const yTicks = tickValues(yMax);

  const cards = [
    summaryCard(
      selectedPoint ? 'Focus' : 'Fleet',
      selectedPoint ? compactSatLabel(selectedPoint.satelliteId) : 'ALL SATS',
      selectedPoint
        ? `${selectedPoint.burnsExecuted} burns logged for selected vehicle`
        : `${summary.burns_executed} executed / ${summary.burns_pending} pending / ${summary.burns_dropped} dropped`,
      selectedPoint ? theme.colors.primary : theme.colors.accent,
    ),
    summaryCard(
      'Avoided',
      `${selectedPoint?.collisionsAvoided ?? summary.collisions_avoided}`,
      selectedPoint
        ? `${selectedPoint.avoidanceBurns} avoidance burns, ${selectedPoint.recoveryBurns} recovery burns`
        : `${summary.avoidance_burns_executed} avoidance burns tracked fleet-wide`,
      theme.colors.accent,
    ),
    summaryCard(
      'Fuel',
      `${(selectedPoint?.fuelConsumedKg ?? summary.fuel_consumed_kg).toFixed(2)} kg`,
      selectedPoint
        ? `${selectedPoint.avoidanceFuelConsumedKg.toFixed(2)} kg spent on avoidance maneuvers`
        : `${summary.avoidance_fuel_consumed_kg.toFixed(2)} kg tied to avoidance burns`,
      theme.colors.warning,
    ),
    summaryCard(
      'Dropped',
      `${summary.burns_dropped}`,
      'Commands lost to upload-window invalidation remain visible instead of disappearing.',
      summary.burns_dropped > 0 ? theme.colors.critical : 'rgba(153, 169, 188, 0.46)',
    ),
  ];

  return (
    <div style={{ display: 'flex', flexWrap: 'wrap', gap: '14px', minHeight: 0, flex: 1 }}>
      <div
        style={{
          flex: '1 1 380px',
          minWidth: 0,
          minHeight: '260px',
          position: 'relative',
          border: '1px solid rgba(88, 184, 255, 0.22)',
          clipPath: theme.chamfer.clipPath,
          background: 'radial-gradient(circle at top left, rgba(88, 184, 255, 0.14), transparent 38%), linear-gradient(180deg, rgba(8, 12, 18, 0.96), rgba(5, 7, 10, 0.98))',
          overflow: 'hidden',
        }}
      >
        <svg viewBox={`0 0 ${chart.width} ${chart.height}`} style={{ width: '100%', height: '100%', display: 'block' }}>
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
                <text x={x} y={chart.height - 14} fill={theme.colors.textMuted} fontSize="10" textAnchor="middle">
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
                <text x={chart.left - 10} y={y + 4} fill={theme.colors.textMuted} fontSize="10" textAnchor="end">
                  {tick.toFixed(tick >= 2 ? 0 : 1)}
                </text>
              </g>
            );
          })}

          <line x1={chart.left} y1={chart.top} x2={chart.left} y2={chart.height - chart.bottom} stroke="rgba(255,255,255,0.28)" strokeWidth="1.2" />
          <line x1={chart.left} y1={chart.height - chart.bottom} x2={chart.width - chart.right} y2={chart.height - chart.bottom} stroke="rgba(255,255,255,0.28)" strokeWidth="1.2" />

          <text x={chart.left} y={12} fill={theme.colors.textDim} fontSize="11" letterSpacing="1.8px">COLLISIONS AVOIDED</text>
          <text x={chart.width - chart.right} y={chart.height - 8} fill={theme.colors.textDim} fontSize="11" textAnchor="end" letterSpacing="1.8px">FUEL CONSUMED (KG)</text>

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
              <g key={point.satelliteId}>
                <circle cx={x} cy={y} r={radius + 3} fill={color} opacity="0.12" />
                <circle cx={x} cy={y} r={radius} fill={color} stroke="rgba(6, 7, 10, 0.95)" strokeWidth="1.4" />
                {point.satelliteId === selectedSatId ? <circle cx={x} cy={y} r={radius + 4.5} fill="none" stroke={theme.colors.primary} strokeWidth="1.5" opacity="0.9" /> : null}
                {(point.collisionsAvoided > 0 || point.satelliteId === selectedSatId) ? (
                  <text x={x} y={y - radius - 8} fill={color} fontSize="10" textAnchor="middle">
                    {compactSatLabel(point.satelliteId)}
                  </text>
                ) : null}
              </g>
            );
          })}

          <polygon
            points={`${toX(summary.fuel_consumed_kg)},${toY(summary.collisions_avoided) - 8} ${toX(summary.fuel_consumed_kg) + 8},${toY(summary.collisions_avoided)} ${toX(summary.fuel_consumed_kg)},${toY(summary.collisions_avoided) + 8} ${toX(summary.fuel_consumed_kg) - 8},${toY(summary.collisions_avoided)}`}
            fill={theme.colors.warning}
            stroke="rgba(5, 7, 10, 0.98)"
            strokeWidth="1.2"
          />
          <text x={toX(summary.fuel_consumed_kg) + 12} y={toY(summary.collisions_avoided) - 10} fill={theme.colors.warning} fontSize="10">
            Fleet summary
          </text>
        </svg>

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

      <div style={{ flex: '0 1 240px', display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(116px, 1fr))', gap: '10px', minWidth: '220px' }}>
        {cards}
      </div>
    </div>
  );
});
