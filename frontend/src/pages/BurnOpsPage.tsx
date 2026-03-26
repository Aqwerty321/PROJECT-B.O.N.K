import { GlassPanel } from '../components/GlassPanel';
import ManeuverGantt from '../components/ManeuverGantt';
import { InfoChip, SectionHeader, SummaryCard } from '../components/dashboard/UiPrimitives';
import { useDashboard } from '../dashboard/DashboardContext';
import { theme } from '../styles/theme';

function formatUtcTime(value?: string | null): string {
  if (!value) return '--';
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return '--';
  return `${date.toISOString().slice(11, 19)} UTC`;
}

export function BurnOpsPage({ isNarrow, isCompact: _isCompact }: { isNarrow: boolean; isCompact: boolean }) {
  const { model, selectedSatId } = useDashboard();

  const droppedCount = model.burnSummary?.burns_dropped ?? model.droppedBurns.length;
  const focusedPending = selectedSatId ? model.pendingBurns.filter(burn => burn.satellite_id === selectedSatId) : model.pendingBurns;
  const focusedExecuted = selectedSatId ? model.executedBurns.filter(burn => burn.satellite_id === selectedSatId) : model.executedBurns;
  const focusedDropped = selectedSatId ? model.droppedBurns.filter(burn => burn.satellite_id === selectedSatId) : model.droppedBurns;
  const focusedPredictiveBurns = focusedPending.filter(burn => burn.scheduled_from_predictive_cdm).length
    + focusedExecuted.filter(burn => burn.scheduled_from_predictive_cdm).length;
  const focusedBlackoutFlags = [...focusedPending, ...focusedExecuted, ...focusedDropped].filter(burn => burn.blackout_overlap).length;
  const focusedConflictFlags = [...focusedPending, ...focusedExecuted, ...focusedDropped].filter(burn => burn.command_conflict || burn.cooldown_conflict).length;

  const nextFocusedBurn = [...focusedPending].sort((a, b) => new Date(a.burn_epoch).getTime() - new Date(b.burn_epoch).getTime())[0] ?? null;
  const latestFocusedExecution = [...focusedExecuted].sort((a, b) => new Date(b.burn_epoch).getTime() - new Date(a.burn_epoch).getTime())[0] ?? null;

  const headerCards = [
    <SummaryCard
      key="focus"
      label="Timeline Focus"
      value={selectedSatId ?? 'Fleet'}
      detail={selectedSatId ? 'Burn lane narrowed to the selected spacecraft command rail.' : 'Fleet-wide command queue, mitigation, and drop visibility.'}
      tone={selectedSatId ? 'primary' : 'accent'}
    />,
    <SummaryCard
      key="next"
      label="Next Command"
      value={nextFocusedBurn ? formatUtcTime(nextFocusedBurn.burn_epoch) : '--'}
      detail={nextFocusedBurn
        ? `${nextFocusedBurn.scheduled_from_predictive_cdm ? 'Predictive' : nextFocusedBurn.recovery_burn ? 'Recovery' : nextFocusedBurn.graveyard_burn ? 'Graveyard' : 'Manual'} burn queued`
        : 'No pending burns in the current focus lane.'}
      tone={nextFocusedBurn ? 'warning' : 'neutral'}
    />,
    <SummaryCard
      key="predictive"
      label="Predictive Triggers"
      value={focusedPredictiveBurns.toString()}
      detail={latestFocusedExecution
        ? `Last execution ${formatUtcTime(latestFocusedExecution.burn_epoch)}`
        : 'No executed burns yet in this focus lane'}
      tone={focusedPredictiveBurns > 0 ? 'accent' : 'neutral'}
    />,
    <SummaryCard
      key="friction"
      label="Operational Friction"
      value={`${focusedConflictFlags + focusedBlackoutFlags + focusedDropped.length}`}
      detail={`${focusedBlackoutFlags} blackout, ${focusedConflictFlags} conflicts, ${focusedDropped.length} dropped`}
      tone={focusedDropped.length > 0 ? 'critical' : focusedConflictFlags + focusedBlackoutFlags > 0 ? 'warning' : 'neutral'}
    />,
  ];

  return (
    <section aria-labelledby="burn-ops-heading" style={{ display: 'flex', flexDirection: 'column', gap: '12px', height: '100%' }}>
      <SectionHeader
        kicker="Burn Deck"
        title="Predictive Maneuver Timeline"
        description="Command timeline and upload friction centered on the live burn queue; horizontal scroll keeps dense schedules readable."
        aside={
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: '6px', justifyContent: 'flex-end' }}>
            <InfoChip label="View" value={selectedSatId ?? 'Fleet'} tone={selectedSatId ? 'accent' : 'primary'} />
            <InfoChip label="Pending" value={model.watchedPendingBurns.length.toString()} tone={model.watchedPendingBurns.length > 0 ? 'warning' : 'accent'} />
            <InfoChip label="Executed" value={model.watchedExecutedBurns.length.toString()} tone="primary" />
            <InfoChip label="Dropped" value={droppedCount.toString()} tone={droppedCount > 0 ? 'critical' : 'neutral'} />
          </div>
        }
      />

      <div style={{ display: 'grid', gridTemplateColumns: isNarrow ? 'repeat(2, minmax(0, 1fr))' : 'repeat(4, minmax(0, 1fr))', gap: '10px' }}>
        {headerCards}
      </div>

      <GlassPanel
        title="PREDICTIVE MANEUVER TIMELINE"
        noPadding
        priority="primary"
        accentColor={theme.colors.warning}
        style={{ flex: 1, minHeight: 0 }}
      >
        <div style={{ display: 'flex', flexDirection: 'column', gap: '10px', flex: 1, minHeight: 0, padding: '10px 14px 14px' }}>
          <div style={{ display: 'flex', flexDirection: 'column', gap: '4px', maxWidth: '74ch', flexShrink: 0 }}>
            <span style={{ color: theme.colors.warning, fontSize: '9px', letterSpacing: '0.16em', textTransform: 'uppercase', opacity: 0.95 }}>Burn scheduler</span>
            <p id="burn-ops-heading" style={{ color: theme.colors.textDim, fontSize: '12px', lineHeight: 1.55 }}>
              Executed burns, queued maneuvers, dropped uploads, and blackout/conflict markers aligned on a single command clock with selected-vehicle focus.
            </p>
          </div>
          <div style={{ width: '100%', flex: 1, minHeight: 0, overflow: 'hidden', clipPath: theme.chamfer.clipPath, border: '1px solid rgba(255, 194, 71, 0.28)', background: 'linear-gradient(180deg, rgba(11, 13, 17, 0.92), rgba(7, 8, 10, 0.98))', boxShadow: 'inset 0 0 32px rgba(0, 0, 0, 0.62), 0 0 22px rgba(255, 194, 71, 0.05)' }}>
            <ManeuverGantt burns={model.burns} selectedSatId={selectedSatId} nowEpochS={model.nowEpochS} />
          </div>
        </div>
      </GlassPanel>
    </section>
  );
}
