import { BurnEfficiencyChart } from '../components/BurnEfficiencyChart';
import { GlassPanel } from '../components/GlassPanel';
import { InfoChip, SectionHeader, SummaryCard } from '../components/dashboard/UiPrimitives';
import { useDashboard } from '../dashboard/DashboardContext';
import { theme } from '../styles/theme';

export function EvasionPage({ isNarrow, isCompact: _isCompact }: { isNarrow: boolean; isCompact: boolean }) {
  const { model, selectedSatId } = useDashboard();

  const selectedStats = selectedSatId ? model.burns?.per_satellite?.[selectedSatId] : null;
  const summary = model.burnSummary;

  const focusedExecuted = selectedSatId
    ? model.executedBurns.filter(burn => burn.satellite_id === selectedSatId)
    : model.executedBurns;
  const focusedPending = selectedSatId
    ? model.pendingBurns.filter(burn => burn.satellite_id === selectedSatId)
    : model.pendingBurns;

  const avoidedCount = selectedStats?.collisions_avoided ?? summary?.collisions_avoided ?? 0;
  const fuelConsumedKg = selectedStats?.fuel_consumed_kg ?? summary?.fuel_consumed_kg ?? 0;
  const avoidanceFuelKg = selectedStats?.avoidance_fuel_consumed_kg ?? summary?.avoidance_fuel_consumed_kg ?? 0;
  const droppedCount = summary?.burns_dropped ?? model.droppedBurns.length;

  const efficiencyRatio = avoidedCount / Math.max(avoidanceFuelKg || fuelConsumedKg || 1, 0.1);

  const cards = [
    <SummaryCard
      key="focus"
      label="Efficiency Focus"
      value={selectedSatId ?? 'Fleet'}
      detail={selectedSatId
        ? `${focusedExecuted.length} executed / ${focusedPending.length} pending in selected lane`
        : `${model.executedBurns.length} executed / ${model.pendingBurns.length} pending across fleet`}
      tone={selectedSatId ? 'primary' : 'accent'}
    />,
    <SummaryCard
      key="avoided"
      label="Collisions Avoided"
      value={avoidedCount.toString()}
      detail={selectedSatId ? 'Avoidance outcomes tied to selected vehicle' : 'Aggregate backend-tracked mitigation outcomes'}
      tone={avoidedCount > 0 ? 'accent' : 'neutral'}
    />,
    <SummaryCard
      key="fuel"
      label="Fuel Consumed"
      value={`${fuelConsumedKg.toFixed(2)} kg`}
      detail={`${avoidanceFuelKg.toFixed(2)} kg attributed to avoidance burns`}
      tone={fuelConsumedKg > 0 ? 'warning' : 'neutral'}
    />,
    <SummaryCard
      key="ratio"
      label="Evasion Ratio"
      value={`${efficiencyRatio.toFixed(2)} avoided/kg`}
      detail={`${droppedCount} dropped commands included in context`}
      tone={droppedCount > 0 ? 'critical' : 'primary'}
    />,
    <SummaryCard
      key="ops"
      label="Slot Integrity"
      value={model.opsHealthValue}
      detail={model.opsHealthDetail}
      tone={model.opsHealthWarn ? 'critical' : 'accent'}
    />,
  ];

  return (
    <section aria-labelledby="evasion-heading" style={{ display: 'flex', flexDirection: 'column', gap: '12px', minHeight: '100%', overflow: 'auto' }}>
      <SectionHeader
        kicker="Evasion Deck"
        title="Fuel-to-Mitigation Efficiency"
        description="Dedicated efficiency view for avoided collisions versus fuel draw, with dropped-command context retained."
        aside={
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: '6px', justifyContent: 'flex-end' }}>
            <InfoChip label="View" value={selectedSatId ?? 'Fleet'} tone={selectedSatId ? 'accent' : 'primary'} />
            <InfoChip label="Avoided" value={avoidedCount.toString()} tone={avoidedCount > 0 ? 'accent' : 'neutral'} />
            <InfoChip label="Fuel" value={`${fuelConsumedKg.toFixed(2)} kg`} tone={fuelConsumedKg > 0 ? 'warning' : 'neutral'} />
            <InfoChip label="Ratio" value={`${efficiencyRatio.toFixed(2)} /kg`} tone="primary" />
          </div>
        }
      />

      <div style={{ display: 'grid', gridTemplateColumns: isNarrow ? 'repeat(2, minmax(0, 1fr))' : 'repeat(5, minmax(0, 1fr))', gap: '10px' }}>
        {cards}
      </div>

      <GlassPanel
        title="EVASION EFFICIENCY"
        noPadding
        priority="primary"
        accentColor={theme.colors.primary}
        style={{ flex: 1, minHeight: '340px' }}
      >
        <div style={{ display: 'flex', flexDirection: 'column', gap: '10px', padding: '10px 14px 14px', flex: 1, minHeight: 0 }}>
          <div style={{ display: 'flex', flexDirection: 'column', gap: '4px', maxWidth: '72ch', flexShrink: 0 }}>
            <span style={{ color: theme.colors.primary, fontSize: '9px', letterSpacing: '0.16em', textTransform: 'uppercase' }}>Avoidance effectiveness</span>
            <p id="evasion-heading" style={{ color: theme.colors.textDim, fontSize: '12px', lineHeight: 1.55 }}>
              Fuel draw versus backend-tracked avoided collisions, with dropped-command visibility so the chart reflects operational friction rather than idealized outcomes.
            </p>
          </div>
          <div style={{ flex: '1 1 auto', minHeight: '280px', overflow: 'visible', border: '1px solid rgba(88, 184, 255, 0.24)', background: 'linear-gradient(180deg, rgba(9, 12, 17, 0.96), rgba(5, 7, 10, 0.98))' }}>
            <BurnEfficiencyChart burns={model.burns} selectedSatId={selectedSatId} />
          </div>
        </div>
      </GlassPanel>
    </section>
  );
}
