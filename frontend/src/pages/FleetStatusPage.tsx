import { GlassPanel } from '../components/GlassPanel';
import { FuelHeatmap } from '../components/FuelHeatmap';
import { StatusPanel } from '../components/StatusPanel';
import { InfoChip, SectionHeader, HeroMetric } from '../components/dashboard/UiPrimitives';
import { useDashboard } from '../dashboard/DashboardContext';
import { theme } from '../styles/theme';

export function FleetStatusPage({ isNarrow, isCompact: _isCompact }: { isNarrow: boolean; isCompact: boolean }) {
  const { model, selectedSatId, selectSat } = useDashboard();

  return (
    <section aria-labelledby="fleet-status-heading" style={{ display: 'flex', flexDirection: 'column', gap: '12px', height: '100%' }}>
      <SectionHeader
        kicker="Fleet Status"
        title="System Health & Resource Posture"
        description="Engine diagnostics, satellite fuel levels, and fleet readiness at a glance."
        aside={
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: '6px', justifyContent: 'flex-end' }}>
            <InfoChip label="Satellites" value={model.satellites.length.toString()} tone="primary" />
            <InfoChip label="Nominal" value={model.statusCounts.nominal.toString()} tone="accent" />
            <InfoChip label="Degraded" value={model.statusCounts.degraded.toString()} tone={model.statusCounts.degraded > 0 ? 'critical' : 'neutral'} />
            <InfoChip label="Avg Fuel" value={model.satellites.length > 0 ? `${model.avgFuelKg.toFixed(1)} kg` : '--'} tone="warning" />
          </div>
        }
      />

      <div style={{ display: 'grid', gridTemplateColumns: isNarrow ? '1fr' : '1fr 1fr', gap: '14px', flex: 1, minHeight: 0 }}>
        {/* Mission Status panel — full height on the left */}
        <GlassPanel
          title="MISSION STATUS"
          noPadding
          priority="primary"
          accentColor={theme.colors.accent}
          style={{ minHeight: 0 }}
        >
          <div style={{ display: 'flex', flexDirection: 'column', gap: '12px', flex: 1, minHeight: 0, padding: '10px 14px 14px' }}>
            <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '10px', flexShrink: 0 }}>
              <HeroMetric
                label="System Status"
                value={model.status?.status ?? '--'}
                detail={model.statusError ? `Error: ${model.statusError}` : 'Engine operational status'}
                tone={model.status?.status === 'NOMINAL' ? 'accent' : 'critical'}
              />
              <HeroMetric
                label="Resource Posture"
                value={model.resourceValue}
                detail={model.resourceDetail}
                tone={model.lowestFuelSatellite && model.lowestFuelSatellite.fuel_kg < 10 ? 'critical' : 'warning'}
              />
            </div>

            <div style={{ flex: 1, overflow: 'auto', clipPath: theme.chamfer.clipPath, border: `1px solid ${theme.colors.border}`, background: 'rgba(255, 255, 255, 0.02)', minHeight: 0 }}>
              <StatusPanel
                status={model.status}
                apiError={model.statusError || model.snapError || model.burnsError}
                snapshotTimestamp={model.snapshot?.timestamp}
                debrisCount={model.debris.length}
                satCount={model.satellites.length}
              />
            </div>
          </div>
        </GlassPanel>

        {/* Fuel Watchlist — full height on the right */}
        <GlassPanel
          title="FUEL WATCHLIST"
          noPadding
          priority="primary"
          accentColor={theme.colors.warning}
          style={{ minHeight: 0 }}
        >
          <div style={{ display: 'flex', flexDirection: 'column', gap: '10px', flex: 1, minHeight: 0, padding: '10px 14px 14px' }}>
            <div style={{ display: 'flex', flexWrap: 'wrap', gap: '6px', justifyContent: 'flex-start', flexShrink: 0 }}>
              <InfoChip label="Average Fuel" value={model.satellites.length > 0 ? `${model.avgFuelKg.toFixed(1)} kg` : '--'} tone="warning" />
              <InfoChip label="Nominal" value={model.statusCounts.nominal.toString()} tone="accent" />
              <InfoChip label="Maneuvering" value={model.statusCounts.maneuvering.toString()} tone={model.statusCounts.maneuvering > 0 ? 'primary' : 'neutral'} />
              <InfoChip label="Degraded" value={model.statusCounts.degraded.toString()} tone={model.statusCounts.degraded > 0 ? 'critical' : 'neutral'} />
            </div>

            <div style={{ flex: 1, minHeight: 0, overflow: 'hidden', clipPath: theme.chamfer.clipPath, border: `1px solid ${theme.colors.border}`, background: 'rgba(255, 255, 255, 0.02)' }}>
              <FuelHeatmap
                satellites={model.satellites}
                selectedSatId={selectedSatId}
                onSelectSat={selectSat}
              />
            </div>
          </div>
        </GlassPanel>
      </div>
    </section>
  );
}
