import { GlassPanel } from '../components/GlassPanel';
import ManeuverGantt from '../components/ManeuverGantt';
import { FuelHeatmap } from '../components/FuelHeatmap';
import { StatusPanel } from '../components/StatusPanel';
import { EmptyStatePanel, InfoChip, SectionHeader } from '../components/dashboard/UiPrimitives';
import { useDashboard } from '../dashboard/DashboardContext';
import { theme } from '../styles/theme';

export function BurnOpsPage({ isNarrow, isCompact: _isCompact }: { isNarrow: boolean; isCompact: boolean }) {
  const { model, selectedSatId, selectSat } = useDashboard();

  return (
    <section aria-labelledby="burn-ops-heading" style={{ display: 'flex', flexDirection: 'column', gap: '12px', height: '100%' }}>
      <SectionHeader
        kicker="Burn Deck"
        title="Predictive Maneuver Timeline"
        description="Burn schedule, fleet status, and resource posture in one page."
        aside={
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: '6px', justifyContent: 'flex-end' }}>
            <InfoChip label="View" value={selectedSatId ?? 'Fleet'} tone={selectedSatId ? 'accent' : 'primary'} />
            <InfoChip label="Pending" value={model.watchedPendingBurns.length.toString()} tone={model.watchedPendingBurns.length > 0 ? 'warning' : 'accent'} />
            <InfoChip label="Executed" value={model.watchedExecutedBurns.length.toString()} tone="primary" />
          </div>
        }
      />

      <div style={{ display: 'grid', gridTemplateColumns: isNarrow ? '1fr' : 'minmax(0, 1.55fr) minmax(300px, 0.85fr)', gap: '14px', flex: 1, minHeight: 0 }}>
        <div style={{ display: 'flex', flexDirection: 'column', gap: '14px', minHeight: 0 }}>
          <GlassPanel
            title="PREDICTIVE MANEUVER TIMELINE"
            noPadding
            priority="primary"
            accentColor={theme.colors.warning}
            style={{ flex: 2, minHeight: 0 }}
          >
            <div style={{ display: 'flex', flexDirection: 'column', gap: '10px', flex: 1, minHeight: 0, padding: '10px 14px 14px' }}>
              <div style={{ display: 'flex', flexDirection: 'column', gap: '4px', maxWidth: '74ch', flexShrink: 0 }}>
                <span style={{ color: theme.colors.warning, fontSize: '9px', letterSpacing: '0.16em', textTransform: 'uppercase', opacity: 0.95 }}>Burn scheduler</span>
                <p id="burn-ops-heading" style={{ color: theme.colors.textDim, fontSize: '12px', lineHeight: 1.55 }}>
                  Executed burns, queued maneuvers, and cooldown reservations aligned on a single command clock.
                </p>
              </div>
              <div style={{ width: '100%', flex: 1, minHeight: 0, overflow: 'hidden', clipPath: theme.chamfer.clipPath, border: '1px solid rgba(255, 194, 71, 0.28)', background: 'linear-gradient(180deg, rgba(11, 13, 17, 0.92), rgba(7, 8, 10, 0.98))', boxShadow: 'inset 0 0 32px rgba(0, 0, 0, 0.62), 0 0 22px rgba(255, 194, 71, 0.05)' }}>
                <ManeuverGantt burns={model.burns} selectedSatId={selectedSatId} nowEpochS={model.nowEpochS} />
              </div>
            </div>
          </GlassPanel>

          <GlassPanel
            title="EVASION EFFICIENCY"
            noPadding
            priority="secondary"
            accentColor={theme.colors.primary}
            style={{ flex: 1, minHeight: 0 }}
          >
            <div style={{ padding: '10px 14px 14px', overflow: 'auto', flex: 1, minHeight: 0 }}>
              <EmptyStatePanel
                title="GRAPH PLACEHOLDER"
                detail="Reserved for the PS-required Fuel Consumed vs Collisions Avoided graph."
              />
            </div>
          </GlassPanel>
        </div>

        <div style={{ display: 'flex', flexDirection: 'column', gap: '14px', minHeight: 0 }}>
          <GlassPanel
            title="MISSION STATUS"
            noPadding
            priority="secondary"
            accentColor={theme.colors.accent}
            style={{ flex: 1, minHeight: 0 }}
          >
            <div style={{ flex: 1, overflow: 'auto', clipPath: theme.chamfer.clipPath, border: `1px solid ${theme.colors.border}`, background: 'rgba(255, 255, 255, 0.02)', minHeight: 0 }}>
              <StatusPanel
                status={model.status}
                apiError={model.statusError || model.snapError || model.burnsError}
                snapshotTimestamp={model.snapshot?.timestamp}
                debrisCount={model.debris.length}
                satCount={model.satellites.length}
              />
            </div>
          </GlassPanel>

          <GlassPanel
            title="FUEL WATCHLIST"
            noPadding
            priority="secondary"
            accentColor={theme.colors.warning}
            style={{ flex: 1, minHeight: 0 }}
          >
            <div style={{ display: 'flex', flexDirection: 'column', gap: '8px', flex: 1, minHeight: 0, padding: '10px 14px 14px' }}>
              <div style={{ display: 'flex', flexWrap: 'wrap', gap: '6px', justifyContent: 'flex-start', flexShrink: 0 }}>
                <InfoChip label="Average Fuel" value={model.satellites.length > 0 ? `${model.avgFuelKg.toFixed(1)} kg` : '--'} tone="warning" />
                <InfoChip label="Nominal" value={model.statusCounts.nominal.toString()} tone="accent" />
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
      </div>
    </section>
  );
}
