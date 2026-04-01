import { BurnEfficiencyChart } from '../components/BurnEfficiencyChart';
import { GlassPanel } from '../components/GlassPanel';
import { SatelliteSelectionPlaceholder } from '../components/dashboard/SatelliteFocusControls';
import { DetailList, EmptyStatePanel, ImpactCaption, InfoChip, SectionHeader, SummaryCard } from '../components/dashboard/UiPrimitives';
import { useDashboard } from '../dashboard/DashboardContext';
import { theme } from '../styles/theme';

function formatMassKg(value: number): string {
  return `${value.toFixed(2)} kg`;
}

function formatDeltaVMs(value: number): string {
  return `${(value * 1000).toFixed(2)} m/s`;
}

export function EvasionPage({ isNarrow, isCompact: _isCompact }: { isNarrow: boolean; isCompact: boolean }) {
  const { model, focusSatFrom, reasoningLevel, selectedSatId, spotlightMode } = useDashboard();

  const selectedStats = selectedSatId ? model.burns?.per_satellite?.[selectedSatId] : null;
  const summary = model.burnSummary;
  const activeSatellite = model.activeSatellite;

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
  const selectedEfficiencyRatio = selectedStats
    ? (selectedStats.collisions_avoided ?? 0) / Math.max((selectedStats.avoidance_fuel_consumed_kg ?? 0) || selectedStats.fuel_consumed_kg || 1, 0.1)
    : 0;

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

  const focusCards = selectedSatId && selectedStats ? [
    <SummaryCard
      key="vehicle"
      label="Focused Vehicle"
      value={selectedSatId}
      detail={activeSatellite ? `${activeSatellite.status} / ${activeSatellite.fuel_kg.toFixed(1)} kg remaining` : 'Selected vehicle is no longer present in the live snapshot'}
      tone="primary"
    />,
    <SummaryCard
      key="focus-avoided"
      label="Avoided Outcomes"
      value={`${selectedStats.collisions_avoided ?? 0}`}
      detail={`${selectedStats.avoidance_burns_executed ?? 0} avoidance burns executed`}
      tone={(selectedStats.collisions_avoided ?? 0) > 0 ? 'accent' : 'neutral'}
    />,
    <SummaryCard
      key="focus-fuel"
      label="Fuel Draw"
      value={formatMassKg(selectedStats.fuel_consumed_kg)}
      detail={`${formatMassKg(selectedStats.avoidance_fuel_consumed_kg ?? 0)} tied to avoidance maneuvers`}
      tone={selectedStats.fuel_consumed_kg > 0 ? 'warning' : 'neutral'}
    />,
    <SummaryCard
      key="focus-ratio"
      label="Vehicle Ratio"
      value={`${selectedEfficiencyRatio.toFixed(2)} avoided/kg`}
      detail={`${selectedStats.burns_executed} burns executed in this lane`}
      tone={selectedEfficiencyRatio > 0 ? 'accent' : 'primary'}
    />,
  ] : [];

  const focusDetails = selectedSatId && selectedStats ? [
    { label: 'Vehicle Status', value: activeSatellite?.status ?? '--', tone: 'primary' as const },
    { label: 'Provenance', value: 'Executed burn history', tone: 'neutral' as const },
    { label: 'Fuel Remaining', value: activeSatellite ? `${activeSatellite.fuel_kg.toFixed(1)} kg` : '--', tone: 'primary' as const },
    { label: 'Burns Executed', value: `${selectedStats.burns_executed}`, tone: 'primary' as const },
    { label: 'Avoidance Burns', value: `${selectedStats.avoidance_burns_executed ?? 0}`, tone: (selectedStats.avoidance_burns_executed ?? 0) > 0 ? 'accent' as const : 'neutral' as const },
    { label: 'Recovery Burns', value: `${selectedStats.recovery_burns_executed ?? 0}`, tone: (selectedStats.recovery_burns_executed ?? 0) > 0 ? 'warning' as const : 'neutral' as const },
    { label: 'Graveyard Burns', value: `${selectedStats.graveyard_burns_executed ?? 0}`, tone: (selectedStats.graveyard_burns_executed ?? 0) > 0 ? 'critical' as const : 'neutral' as const },
    { label: 'Avoided Collisions', value: `${selectedStats.collisions_avoided ?? 0}`, tone: (selectedStats.collisions_avoided ?? 0) > 0 ? 'accent' as const : 'neutral' as const },
    { label: 'Total Delta-V', value: formatDeltaVMs(selectedStats.delta_v_total_km_s), tone: 'warning' as const },
    { label: 'Fuel Consumed', value: formatMassKg(selectedStats.fuel_consumed_kg), tone: 'warning' as const },
    { label: 'Avoidance Fuel', value: formatMassKg(selectedStats.avoidance_fuel_consumed_kg ?? 0), tone: (selectedStats.avoidance_fuel_consumed_kg ?? 0) > 0 ? 'warning' as const : 'neutral' as const },
  ] : [];
  const minimalImpactCaption = selectedSatId && selectedStats
    ? `${selectedSatId} has avoided ${selectedStats.collisions_avoided ?? 0} collision${(selectedStats.collisions_avoided ?? 0) === 1 ? '' : 's'} while spending ${formatMassKg(selectedStats.avoidance_fuel_consumed_kg ?? 0)} on avoidance burns.`
    : `Fleet-wide avoidance performance is running at ${efficiencyRatio.toFixed(2)} avoided collisions per kilogram with ${droppedCount} dropped command${droppedCount === 1 ? '' : 's'} still counted.`;

  return (
    <section aria-labelledby="evasion-heading" style={{ display: 'flex', flexDirection: 'column', gap: '12px', minHeight: '100%', overflow: 'auto' }}>
      <SectionHeader
        kicker="Evasion Deck"
        title="Fuel-to-Mitigation Efficiency"
        description="Dedicated efficiency view for avoided collisions versus fuel draw, with dropped-command context retained."
        aside={
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: '6px', justifyContent: 'flex-end' }}>
            <InfoChip label="Focus" value={selectedSatId ?? 'Fleet'} tone={selectedSatId ? 'primary' : 'neutral'} />
            <InfoChip label="Avoided" value={avoidedCount.toString()} tone={avoidedCount > 0 ? 'accent' : 'neutral'} />
            <InfoChip label="Fuel" value={`${fuelConsumedKg.toFixed(2)} kg`} tone={fuelConsumedKg > 0 ? 'warning' : 'neutral'} />
            <InfoChip label="Ratio" value={`${efficiencyRatio.toFixed(2)} /kg`} tone="primary" />
          </div>
        }
      />

      <div style={{ display: 'grid', gridTemplateColumns: isNarrow ? 'repeat(2, minmax(0, 1fr))' : 'repeat(5, minmax(0, 1fr))', gap: '10px', opacity: spotlightMode ? 0.92 : 1 }}>
        {cards}
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: isNarrow ? '1fr' : 'minmax(0, 1.45fr) minmax(320px, 0.9fr)', gap: '14px', flex: 1, minHeight: 0 }}>
        <GlassPanel
          title="EVASION EFFICIENCY"
          noPadding
          priority="primary"
          accentColor={theme.colors.primary}
          style={{ minHeight: '340px', boxShadow: spotlightMode ? `0 0 0 1px rgba(88, 184, 255, 0.18), 0 0 24px rgba(88, 184, 255, 0.08)` : undefined }}
        >
          <div style={{ display: 'flex', flexDirection: 'column', gap: '10px', padding: '10px 14px 14px', flex: 1, minHeight: 0 }}>
            <div style={{ display: 'flex', flexDirection: 'column', gap: '4px', maxWidth: '72ch', flexShrink: 0 }}>
              <span style={{ color: theme.colors.primary, fontSize: '9px', letterSpacing: '0.16em', textTransform: 'uppercase' }}>Avoidance effectiveness</span>
              <p id="evasion-heading" style={{ color: theme.colors.textDim, fontSize: '12px', lineHeight: 1.55 }}>
                Fuel draw versus backend-tracked avoided collisions, with dropped-command visibility so the chart reflects operational friction rather than idealized outcomes.
              </p>
            </div>
            <div style={{ flex: '1 1 auto', minHeight: '280px', overflow: 'visible', border: '1px solid rgba(88, 184, 255, 0.24)', background: 'linear-gradient(180deg, rgba(9, 12, 17, 0.96), rgba(5, 7, 10, 0.98))' }}>
              <BurnEfficiencyChart burns={model.burns} selectedSatId={selectedSatId} onSelectSat={id => focusSatFrom(id, id ? {
                source: 'Evasion Chart',
                detail: `Pinned ${id} from the fuel-to-mitigation efficiency chart.`,
              } : null)} />
            </div>
          </div>
        </GlassPanel>

        <GlassPanel
          title="SATELLITE EFFICIENCY FOCUS"
          noPadding
          priority="secondary"
          accentColor={theme.colors.accent}
          style={{ minHeight: '340px', boxShadow: spotlightMode && selectedSatId ? `0 0 0 1px rgba(57, 217, 138, 0.16), 0 0 24px rgba(57, 217, 138, 0.08)` : undefined }}
        >
          <div style={{ display: 'flex', flexDirection: 'column', gap: '10px', padding: '10px 14px 14px', flex: 1, minHeight: 0, overflow: 'auto' }}>
            <div style={{ display: 'flex', flexDirection: 'column', gap: '4px' }}>
              <span style={{ color: theme.colors.accent, fontSize: '9px', letterSpacing: '0.16em', textTransform: 'uppercase' }}>Vehicle detail rail</span>
              <p style={{ color: theme.colors.textDim, fontSize: '12px', lineHeight: 1.55 }}>
                Select any point on the chart to inspect vehicle-specific avoidance outcomes, burn mix, and fuel efficiency without hiding fleet context.
              </p>
            </div>

            {selectedSatId ? selectedStats ? (
              <>
                <div style={{ display: 'grid', gridTemplateColumns: 'repeat(2, minmax(0, 1fr))', gap: '8px' }}>
                  {focusCards}
                </div>
                {reasoningLevel === 'minimal' ? (
                  <ImpactCaption detail={minimalImpactCaption} tone={selectedStats.collisions_avoided ? 'accent' : selectedStats.fuel_consumed_kg > 0 ? 'warning' : 'neutral'} />
                ) : null}
                {reasoningLevel === 'detailed' ? <DetailList entries={focusDetails} /> : null}
              </>
            ) : (
              <EmptyStatePanel
                title="Awaiting Vehicle Burn Evidence"
                detail="The selected satellite has no logged burn-efficiency record yet. Pick another chart point or wait for executed burn outcomes to arrive from the backend."
              />
            ) : (
              <SatelliteSelectionPlaceholder
                title="Satellite Focus Required"
                detail="Select a chart point or use the dropdown to inspect vehicle-specific efficiency, burn mix, and fuel-to-mitigation detail."
                tone="accent"
              />
            )}
            {!selectedSatId && reasoningLevel === 'minimal' ? <ImpactCaption detail={minimalImpactCaption} tone={droppedCount > 0 ? 'warning' : 'accent'} /> : null}
          </div>
        </GlassPanel>
      </div>
    </section>
  );
}
