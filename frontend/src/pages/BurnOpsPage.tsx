import type { ExecutedBurn, PendingBurn } from '../types/api';
import { GlassPanel } from '../components/GlassPanel';
import ManeuverGantt from '../components/ManeuverGantt';
import { SatelliteFocusDropdown, SatelliteSelectionPlaceholder } from '../components/dashboard/SatelliteFocusControls';
import { DetailList, EmptyStatePanel, InfoChip, SectionHeader, SummaryCard, type Tone } from '../components/dashboard/UiPrimitives';
import { useDashboard } from '../dashboard/DashboardContext';
import { theme } from '../styles/theme';

type TimelineBurn = ExecutedBurn | PendingBurn;

function formatUtcTime(value?: string | null): string {
  if (!value) return '--';
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return '--';
  return `${date.toISOString().slice(11, 19)} UTC`;
}

function formatDistanceKm(value?: number | null): string {
  if (value == null || !Number.isFinite(value)) return '--';
  if (value < 1) return `${Math.round(value * 1000)} m`;
  return `${value.toFixed(2)} km`;
}

function formatSpeedKmS(value?: number | null): string {
  if (value == null || !Number.isFinite(value)) return '--';
  return `${value.toFixed(2)} km/s`;
}

function formatDeltaVMs(value?: number | null): string {
  if (value == null || !Number.isFinite(value)) return '--';
  return `${(value * 1000).toFixed(2)} m/s`;
}

function formatLeadTime(seconds?: number | null): string {
  if (seconds == null || !Number.isFinite(seconds)) return '--';
  if (seconds < 60) return `${Math.round(seconds)} s`;
  if (seconds < 3600) return `${Math.round(seconds / 60)} min`;
  return `${(seconds / 3600).toFixed(2)} h`;
}

function burnKindLabel(burn: TimelineBurn): string {
  if (burn.graveyard_burn) return 'Graveyard';
  if (burn.recovery_burn) return 'Recovery';
  if (burn.scheduled_from_predictive_cdm) return 'Auto-COLA';
  if (burn.auto_generated) return 'Auto';
  return 'Manual';
}

function pickDecisionFocus(pending: PendingBurn[], executed: ExecutedBurn[]): TimelineBurn | null {
  const latestPredictiveExecuted = [...executed]
    .filter(burn => burn.scheduled_from_predictive_cdm)
    .sort((lhs, rhs) => new Date(rhs.burn_epoch).getTime() - new Date(lhs.burn_epoch).getTime())[0];
  if (latestPredictiveExecuted) return latestPredictiveExecuted;

  const nextPredictivePending = [...pending]
    .filter(burn => burn.scheduled_from_predictive_cdm)
    .sort((lhs, rhs) => new Date(lhs.burn_epoch).getTime() - new Date(rhs.burn_epoch).getTime())[0];
  if (nextPredictivePending) return nextPredictivePending;

  const latestExecuted = [...executed]
    .sort((lhs, rhs) => new Date(rhs.burn_epoch).getTime() - new Date(lhs.burn_epoch).getTime())[0];
  if (latestExecuted) return latestExecuted;

  return [...pending].sort((lhs, rhs) => new Date(lhs.burn_epoch).getTime() - new Date(rhs.burn_epoch).getTime())[0] ?? null;
}

function outcomeTone(burn: TimelineBurn | null): Tone {
  if (!burn) return 'neutral';
  if ('collision_avoided' in burn && burn.collision_avoided) return 'accent';
  if (burn.scheduled_from_predictive_cdm) return 'warning';
  return 'primary';
}

export function BurnOpsPage({ isNarrow, isCompact: _isCompact }: { isNarrow: boolean; isCompact: boolean }) {
  const { model, selectedSatId, selectSat } = useDashboard();

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
  const decisionFocus = pickDecisionFocus(focusedPending, focusedExecuted);
  const decisionLeadSeconds = decisionFocus?.trigger_tca_epoch_s
    ? decisionFocus.trigger_tca_epoch_s - (new Date(decisionFocus.burn_epoch).getTime() / 1000)
    : null;
  const decisionConstraintCount = Number(Boolean(decisionFocus?.blackout_overlap))
    + Number(Boolean(decisionFocus?.cooldown_conflict))
    + Number(Boolean(decisionFocus?.command_conflict));
  const executedDecision = decisionFocus && 'fuel_before_kg' in decisionFocus ? decisionFocus : null;
  const mitigationGainKm = executedDecision?.mitigation_miss_distance_km != null && executedDecision.trigger_miss_distance_km != null
    ? executedDecision.mitigation_miss_distance_km - executedDecision.trigger_miss_distance_km
    : null;
  const explainCards = decisionFocus ? [
    <SummaryCard
      key="focus-burn"
      label="Decision Focus"
      value={decisionFocus.id}
      detail={`${burnKindLabel(decisionFocus)} on ${decisionFocus.satellite_id}`}
      tone={decisionFocus.scheduled_from_predictive_cdm ? 'accent' : 'primary'}
    />,
    <SummaryCard
      key="trigger"
      label="Threat Geometry"
      value={formatDistanceKm(decisionFocus.trigger_miss_distance_km)}
      detail={`${formatSpeedKmS(decisionFocus.trigger_approach_speed_km_s)} approach / TCA ${formatUtcTime(decisionFocus.trigger_tca)}`}
      tone={decisionFocus.trigger_fail_open ? 'warning' : 'critical'}
    />,
    <SummaryCard
      key="command"
      label="Command Choice"
      value={formatDeltaVMs(decisionFocus.delta_v_norm_km_s)}
      detail={`${decisionFocus.upload_station || '--'} upload / lead ${formatLeadTime(decisionLeadSeconds)}`}
      tone={decisionConstraintCount > 0 ? 'warning' : 'primary'}
    />,
    <SummaryCard
      key="outcome"
      label="Outcome"
      value={executedDecision?.collision_avoided ? 'Avoided' : executedDecision?.mitigation_evaluated ? 'Tracked' : decisionFocus.scheduled_from_predictive_cdm ? 'Pending' : 'Monitor'}
      detail={executedDecision?.mitigation_miss_distance_km != null
        ? `${formatDistanceKm(executedDecision.mitigation_miss_distance_km)} post-burn miss`
        : 'Awaiting execution or mitigation evaluation'}
      tone={outcomeTone(decisionFocus)}
    />,
  ] : [];

  const detailEntries: Array<{ label: string; value: string; tone?: Tone }> = decisionFocus ? [
    { label: 'Burn Type', value: burnKindLabel(decisionFocus), tone: decisionFocus.scheduled_from_predictive_cdm ? 'accent' : 'primary' as const },
    { label: 'Satellite', value: decisionFocus.satellite_id, tone: 'primary' as const },
    { label: 'Trigger Debris', value: decisionFocus.trigger_debris_id ?? '--', tone: decisionFocus.trigger_debris_id ? 'critical' as const : 'neutral' as const },
    { label: 'Predicted Miss', value: formatDistanceKm(decisionFocus.trigger_miss_distance_km), tone: 'critical' as const },
    { label: 'Approach Speed', value: formatSpeedKmS(decisionFocus.trigger_approach_speed_km_s), tone: 'warning' as const },
    { label: 'Burn Epoch', value: formatUtcTime(decisionFocus.burn_epoch), tone: 'primary' as const },
    { label: 'Upload Station', value: decisionFocus.upload_station || '--', tone: decisionFocus.upload_station ? 'accent' as const : 'neutral' as const },
    { label: 'Lead To TCA', value: formatLeadTime(decisionLeadSeconds), tone: decisionLeadSeconds != null && decisionLeadSeconds >= 120 ? 'accent' as const : 'warning' as const },
    { label: 'Delta-V', value: formatDeltaVMs(decisionFocus.delta_v_norm_km_s), tone: 'primary' as const },
    { label: 'Constraint Flags', value: decisionConstraintCount > 0 ? `${decisionConstraintCount} active` : 'Clear', tone: decisionConstraintCount > 0 ? 'warning' as const : 'accent' as const },
    { label: 'Fail-Open', value: decisionFocus.trigger_fail_open ? 'Yes' : 'No', tone: decisionFocus.trigger_fail_open ? 'warning' as const : 'neutral' as const },
    { label: 'Mitigation Miss', value: executedDecision ? formatDistanceKm(executedDecision.mitigation_miss_distance_km) : 'Pending', tone: executedDecision?.collision_avoided ? 'accent' as const : executedDecision?.mitigation_evaluated ? 'warning' as const : 'neutral' as const },
    { label: 'Clearance Gain', value: mitigationGainKm != null ? formatDistanceKm(mitigationGainKm) : 'Pending', tone: mitigationGainKm != null && mitigationGainKm > 0 ? 'accent' as const : mitigationGainKm != null ? 'warning' as const : 'neutral' as const },
  ] : [];

  const headerCards = [
    <SummaryCard
      key="focus"
      label="Timeline Focus"
      value={selectedSatId ?? 'Fleet'}
      detail={selectedSatId ? 'Selected spacecraft stays highlighted while the full fleet command queue remains visible.' : 'Fleet-wide command queue, mitigation, and drop visibility.'}
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
            <SatelliteFocusDropdown label="View" satellites={model.satellites} selectedSatId={selectedSatId} onSelectSat={selectSat} fleetLabel="Fleet" tone="primary" variant="chip" />
            <InfoChip label="Pending" value={model.watchedPendingBurns.length.toString()} tone={model.watchedPendingBurns.length > 0 ? 'warning' : 'accent'} />
            <InfoChip label="Executed" value={model.watchedExecutedBurns.length.toString()} tone="primary" />
            <InfoChip label="Dropped" value={droppedCount.toString()} tone={droppedCount > 0 ? 'critical' : 'neutral'} />
          </div>
        }
      />

      <div style={{ display: 'grid', gridTemplateColumns: isNarrow ? 'repeat(2, minmax(0, 1fr))' : 'repeat(4, minmax(0, 1fr))', gap: '10px' }}>
        {headerCards}
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: isNarrow ? '1fr' : 'minmax(0, 1.45fr) minmax(320px, 0.9fr)', gap: '14px', flex: 1, minHeight: 0 }}>
        <GlassPanel
          title="PREDICTIVE MANEUVER TIMELINE"
          noPadding
          priority="primary"
          accentColor={theme.colors.warning}
          style={{ minHeight: 0 }}
        >
          <div style={{ display: 'flex', flexDirection: 'column', gap: '10px', flex: 1, minHeight: 0, padding: '10px 14px 14px' }}>
            <div style={{ display: 'flex', flexDirection: 'column', gap: '4px', maxWidth: '74ch', flexShrink: 0 }}>
              <span style={{ color: theme.colors.warning, fontSize: '9px', letterSpacing: '0.16em', textTransform: 'uppercase', opacity: 0.95 }}>Burn scheduler</span>
              <p id="burn-ops-heading" style={{ color: theme.colors.textDim, fontSize: '12px', lineHeight: 1.55 }}>
                Executed burns, queued maneuvers, dropped uploads, and blackout/conflict markers aligned on a single command clock with optional selected-vehicle emphasis.
              </p>
            </div>
            <div style={{ width: '100%', flex: 1, minHeight: 0, overflow: 'hidden', clipPath: theme.chamfer.clipPath, border: `1px solid ${selectedSatId ? 'rgba(88, 184, 255, 0.32)' : 'rgba(255, 194, 71, 0.28)'}`, background: 'linear-gradient(180deg, rgba(11, 13, 17, 0.92), rgba(7, 8, 10, 0.98))', boxShadow: selectedSatId ? 'inset 0 0 32px rgba(0, 0, 0, 0.62), 0 0 22px rgba(88, 184, 255, 0.08)' : 'inset 0 0 32px rgba(0, 0, 0, 0.62), 0 0 22px rgba(255, 194, 71, 0.05)' }}>
              <ManeuverGantt burns={model.burns} selectedSatId={selectedSatId} nowEpochS={model.nowEpochS} />
            </div>
          </div>
        </GlassPanel>

        <GlassPanel
          title="BURN DECISION EXPLAINER"
          noPadding
          priority="secondary"
          accentColor={theme.colors.accent}
          style={{ minHeight: 0 }}
        >
          <div style={{ display: 'flex', flexDirection: 'column', gap: '10px', flex: 1, minHeight: 0, padding: '10px 14px 14px', overflow: 'auto' }}>
            <div style={{ display: 'flex', flexDirection: 'column', gap: '4px' }}>
              <span style={{ color: theme.colors.accent, fontSize: '9px', letterSpacing: '0.16em', textTransform: 'uppercase' }}>Judge-facing rationale</span>
              <p style={{ color: theme.colors.textDim, fontSize: '12px', lineHeight: 1.55 }}>
                This rail turns the current burn focus into a narrated chain: triggering debris, predicted miss, chosen delta-v, upload path, and post-mitigation outcome.
              </p>
            </div>

            {selectedSatId ? decisionFocus ? (
              <>
                <div style={{ display: 'grid', gridTemplateColumns: 'repeat(2, minmax(0, 1fr))', gap: '8px' }}>
                  {explainCards}
                </div>
                <DetailList entries={detailEntries} />
              </>
            ) : (
              <EmptyStatePanel
                title="Awaiting Burn Evidence"
                detail="No burn has been queued or executed in the current focus lane yet. Run the ready-demo path or select a satellite with activity to populate the explanation rail."
              />
            ) : (
              <SatelliteSelectionPlaceholder
                title="Satellite Focus Required"
                detail="Select a satellite to inspect the burn decision explainer, including trigger debris, delta-v choice, upload path, and mitigation outcome."
                tone="accent"
              />
            )}
          </div>
        </GlassPanel>
      </div>
    </section>
  );
}
