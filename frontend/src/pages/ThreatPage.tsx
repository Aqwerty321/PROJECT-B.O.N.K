import { useEffect, useMemo, useState } from 'react';
import { ConjunctionBullseye } from '../components/ConjunctionBullseye';
import { ThreatSeverityFilters, filterConjunctionsBySeverity } from '../components/dashboard/ThreatSeverityFilters';
import { GlassPanel } from '../components/GlassPanel';
import { EmptyStatePanel, InfoChip, SectionHeader, SummaryCard } from '../components/dashboard/UiPrimitives';
import { ConjunctionDetailCard } from '../components/threat/ConjunctionDetailCard';
import { ConjunctionEventList } from '../components/threat/ConjunctionEventList';
import {
  buildEncounterQueueGroups,
  countCollapsedEncounterSamples,
  flattenEncounterQueueGroups,
} from '../components/threat/queueModel';
import { useDashboard } from '../dashboard/DashboardContext';
import { useConjunctions } from '../hooks/useApi';
import { theme } from '../styles/theme';
import { riskLevelForEvent } from '../types/api';
import { hasActiveThreatSeverityFilter } from '../types/dashboard';

function formatUtcTime(value: string): string {
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return '--';
  return date.toISOString().slice(11, 19);
}

function formatOffsetLabel(deltaSeconds: number): string {
  if (Math.abs(deltaSeconds) < 30) return 'NOW';
  const minutes = Math.round(Math.abs(deltaSeconds) / 60);
  return `${deltaSeconds >= 0 ? 'T+' : 'T-'}${minutes}m`;
}

export function ThreatPage({ isNarrow, isCompact: _isCompact }: { isNarrow: boolean; isCompact: boolean }) {
  const { model, selectedSatId, selectSat, threatSeverityFilter } = useDashboard();
  const [activeEventKey, setActiveEventKey] = useState<string | null>(null);
  const { conjunctions: predictiveConjunctions } = useConjunctions(2000, undefined, 'predicted');

  const streamEvents = predictiveConjunctions && predictiveConjunctions.count > 0
    ? predictiveConjunctions.conjunctions
    : model.conjList;
  const streamLabel = predictiveConjunctions && predictiveConjunctions.count > 0
    ? 'Predictive 24h'
    : 'Historical';

  const sortedEvents = useMemo(
    () => [...streamEvents].sort((a, b) => a.tca_epoch_s - b.tca_epoch_s),
    [streamEvents],
  );

  const nextThreat = useMemo(
    () => sortedEvents.find(event => event.tca_epoch_s >= model.nowEpochS - 300) ?? sortedEvents[0] ?? null,
    [model.nowEpochS, sortedEvents],
  );

  const focusSatelliteId = selectedSatId
    ?? nextThreat?.satellite_id
    ?? sortedEvents[0]?.satellite_id
    ?? null;

  const focusedEvents = useMemo(
    () => focusSatelliteId ? sortedEvents.filter(event => event.satellite_id === focusSatelliteId) : sortedEvents,
    [focusSatelliteId, sortedEvents],
  );
  const filteredEvents = useMemo(
    () => filterConjunctionsBySeverity(sortedEvents, threatSeverityFilter),
    [sortedEvents, threatSeverityFilter],
  );
  const filteredFocusedEvents = useMemo(
    () => filterConjunctionsBySeverity(focusedEvents, threatSeverityFilter),
    [focusedEvents, threatSeverityFilter],
  );
  const queueGroups = useMemo(
    () => buildEncounterQueueGroups(filteredEvents, focusSatelliteId),
    [filteredEvents, focusSatelliteId],
  );
  const flatQueueEntries = useMemo(
    () => flattenEncounterQueueGroups(queueGroups),
    [queueGroups],
  );
  const activeQueueEntry = flatQueueEntries.find(entry => entry.key === activeEventKey) ?? null;
  const activeEvent = activeQueueEntry?.event ?? null;
  const focusedQueueEntries = useMemo(
    () => focusSatelliteId ? flatQueueEntries.filter(entry => entry.event.satellite_id === focusSatelliteId) : flatQueueEntries,
    [flatQueueEntries, focusSatelliteId],
  );
  const collapsedQueueSamples = useMemo(
    () => countCollapsedEncounterSamples(queueGroups),
    [queueGroups],
  );
  const filteredStreamThreatCounts = useMemo(
    () => filteredEvents.reduce(
      (counts, event) => {
        const level = riskLevelForEvent(event);
        counts[level] += 1;
        return counts;
      },
      { red: 0, yellow: 0, green: 0 },
    ),
    [filteredEvents],
  );
  const filteredFocusedThreatCounts = useMemo(
    () => filteredFocusedEvents.reduce(
      (counts, event) => {
        const level = riskLevelForEvent(event);
        counts[level] += 1;
        return counts;
      },
      { red: 0, yellow: 0, green: 0 },
    ),
    [filteredFocusedEvents],
  );

  const streamThreatCounts = useMemo(
    () => sortedEvents.reduce(
      (counts, event) => {
        const level = riskLevelForEvent(event);
        counts[level] += 1;
        return counts;
      },
      { red: 0, yellow: 0, green: 0 },
    ),
    [sortedEvents],
  );

  const focusNextThreat = useMemo(
    () => filteredFocusedEvents.find(event => event.tca_epoch_s >= model.nowEpochS - 300) ?? filteredFocusedEvents[0] ?? null,
    [filteredFocusedEvents, model.nowEpochS],
  );

  const closestFocusedEvent = useMemo(
    () => filteredFocusedEvents.reduce<typeof filteredFocusedEvents[number] | null>((closest, event) => {
      if (!closest || event.miss_distance_km < closest.miss_distance_km) {
        return event;
      }
      return closest;
    }, null),
    [filteredFocusedEvents],
  );

  const focusFailOpenCount = useMemo(
    () => filteredFocusedEvents.filter(event => event.fail_open).length,
    [filteredFocusedEvents],
  );

  const focusDistinctDebrisCount = useMemo(
    () => new Set(filteredFocusedEvents.map(event => event.debris_id)).size,
    [filteredFocusedEvents],
  );

  const bullseyeMaxTcaSeconds = useMemo(() => {
    const maxFutureDt = filteredFocusedEvents.reduce((acc, event) => {
      const dt = Math.max(0, event.tca_epoch_s - model.nowEpochS);
      return Math.max(acc, dt);
    }, 0);
    if (maxFutureDt <= 5400) return 5400;
    if (maxFutureDt <= 21600) return 21600;
    return 86400;
  }, [filteredFocusedEvents, model.nowEpochS]);

  useEffect(() => {
    if (filteredEvents.length === 0) {
      setActiveEventKey(null);
      return;
    }

    const hasActive = activeEventKey && flatQueueEntries.some(entry => entry.key === activeEventKey);
    if (!hasActive) {
      const preferred = queueGroups.find(group => group.satelliteId === focusSatelliteId)?.entries[0]
        ?? queueGroups[0]?.entries[0]
        ?? null;
      setActiveEventKey(preferred?.key ?? null);
    }
  }, [activeEventKey, flatQueueEntries, focusSatelliteId, queueGroups, filteredEvents.length]);

  const displayThreatCounts = focusSatelliteId ? filteredFocusedThreatCounts : filteredStreamThreatCounts;
  const filterSummary = hasActiveThreatSeverityFilter(threatSeverityFilter)
    ? [
        threatSeverityFilter.critical ? 'Critical' : null,
        threatSeverityFilter.warning ? 'Warning' : null,
        threatSeverityFilter.watch ? 'Watch' : null,
      ].filter(Boolean).join(' / ')
    : 'None';
  const focusSummaryCards = [
    <SummaryCard
      key="focus"
      label="Focus Vehicle"
      value={focusSatelliteId ?? 'AUTO'}
      detail={selectedSatId
        ? 'Pinned from the encounter queue. Select another encounter to retarget, or clear to return to auto-follow.'
        : focusSatelliteId
          ? 'Auto-focused on the next active spacecraft. Select an encounter from the queue to pin it.'
          : 'Awaiting predictive or historical events.'}
      tone={focusSatelliteId ? 'primary' : 'neutral'}
      testId="threat-focus-card"
      action={selectedSatId ? (
        <button
          type="button"
          aria-label="Return threat page to auto focus"
          onClick={() => {
            setActiveEventKey(null);
            selectSat(null);
          }}
          style={{
            display: 'inline-flex',
            alignItems: 'center',
            gap: '5px',
            minHeight: '22px',
            padding: '0 8px',
            border: `1px solid ${theme.colors.primary}44`,
            background: 'rgba(88, 184, 255, 0.08)',
            color: theme.colors.primary,
            fontFamily: theme.font.mono,
            fontSize: '8px',
            fontWeight: 700,
            letterSpacing: '0.12em',
            textTransform: 'uppercase',
            cursor: 'pointer',
            clipPath: theme.chamfer.buttonClipPath,
            boxShadow: '0 0 10px rgba(88, 184, 255, 0.08)',
          }}
        >
          <span style={{ width: '5px', height: '5px', borderRadius: '50%', background: theme.colors.accent, boxShadow: `0 0 5px ${theme.colors.accent}` }} aria-hidden="true" />
          Auto
        </button>
      ) : null}
      actionWidth={selectedSatId ? 72 : 0}
    />,
    <SummaryCard
      key="closest"
      label="Closest Miss"
      value={closestFocusedEvent ? `${closestFocusedEvent.miss_distance_km.toFixed(2)} km` : '--'}
      detail={closestFocusedEvent
        ? `${closestFocusedEvent.debris_id} at ${formatUtcTime(closestFocusedEvent.tca)} UTC`
        : focusedEvents.length > 0
          ? 'Filters currently hide the closest encounter.'
          : 'No conjunctions currently in the focus lane.'}
      tone={closestFocusedEvent ? (closestFocusedEvent.miss_distance_km < 1 ? 'critical' : closestFocusedEvent.miss_distance_km < 5 ? 'warning' : 'accent') : 'neutral'}
    />,
    <SummaryCard
      key="next"
      label="Next TCA"
      value={focusNextThreat ? formatUtcTime(focusNextThreat.tca) : '--'}
      detail={focusNextThreat
        ? `${formatOffsetLabel(focusNextThreat.tca_epoch_s - model.nowEpochS)} on ${focusDistinctDebrisCount} tracked debris objects`
        : focusedEvents.length > 0
          ? 'Filters currently hide the soonest encounter.'
          : 'The watch list will populate after the next backend conjunction update.'}
      tone={focusNextThreat ? 'warning' : 'neutral'}
    />,
    <SummaryCard
      key="events"
      label="Events In Scope"
      value={focusedQueueEntries.length.toString()}
      detail={focusedEvents.length > 0
        ? `${displayThreatCounts.red} critical / ${displayThreatCounts.yellow} warning / ${displayThreatCounts.green} watch after filtering`
        : streamEvents.length > 0
          ? 'No encounters currently visible for the focused spacecraft.'
          : 'Waiting for predictive or historical conjunction data.'}
      tone={filteredFocusedEvents.length > 0 ? 'accent' : 'neutral'}
    />,
  ];
  return (
    <section aria-labelledby="threat-heading" style={{ display: 'flex', flexDirection: 'column', gap: '12px', height: '100%' }}>
      <SectionHeader
        kicker="Threat Deck"
        title="Conjunction Watch"
        description="Use the queue on the right to choose an encounter, the bullseye on the left to understand when and from where it approaches, and the detail card below to confirm why it matters."
        aside={
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: '6px', justifyContent: 'flex-end' }}>
            <InfoChip label="Focus" value={focusSatelliteId ?? 'Auto'} tone={focusSatelliteId ? 'primary' : 'neutral'} />
            <InfoChip label="Stream" value={streamLabel} tone={streamLabel === 'Predictive 24h' ? 'warning' : 'accent'} />
            <InfoChip label="Filters" value={filterSummary} tone={hasActiveThreatSeverityFilter(threatSeverityFilter) ? 'accent' : 'warning'} />
          </div>
        }
      />

      <div style={{ display: 'grid', gridTemplateColumns: isNarrow ? 'repeat(2, minmax(0, 1fr))' : 'repeat(4, minmax(0, 1fr))', gap: '10px', flexShrink: 0 }}>
        {focusSummaryCards}
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: isNarrow ? '1fr' : 'minmax(0, 1.32fr) minmax(320px, 0.74fr)', gap: '14px', flex: 1, minHeight: 0 }}>
        <GlassPanel
          title="CONJUNCTION BULLSEYE"
          noPadding
          priority="primary"
          accentColor={theme.colors.primary}
          style={{ minHeight: 0 }}
        >
          <div style={{ display: 'flex', flexDirection: 'column', gap: '10px', flex: 1, minHeight: 0, padding: '10px 14px 14px' }}>
            <div style={{ display: 'grid', gridTemplateColumns: isNarrow ? '1fr' : 'minmax(0, 1fr) auto', gap: '10px', alignItems: 'flex-start', flexShrink: 0 }}>
              <div style={{ display: 'flex', flexDirection: 'column', gap: '4px', maxWidth: '60ch' }}>
                <span style={{ color: theme.colors.primary, fontSize: '9px', letterSpacing: '0.16em', textTransform: 'uppercase' }}>How to read the radar</span>
                <p id="threat-heading" style={{ color: theme.colors.textDim, fontSize: '12px', lineHeight: 1.55 }}>
                  Center is the watched spacecraft. Radius tells you how soon the encounter happens. Angle shows approach direction. Pick an event on the right to inspect it here.
                </p>
              </div>
              <div style={{ display: 'flex', flexWrap: 'wrap', gap: '6px' }}>
                <InfoChip label="Queue" value={`${flatQueueEntries.length} shown`} tone={flatQueueEntries.length > 0 ? 'accent' : 'warning'} />
                <InfoChip label="Collapsed" value={collapsedQueueSamples.toString()} tone={collapsedQueueSamples > 0 ? 'accent' : 'neutral'} />
                <InfoChip label="Horizon" value={bullseyeMaxTcaSeconds >= 86400 ? '24h' : bullseyeMaxTcaSeconds >= 21600 ? '6h' : '90m'} tone="accent" />
              </div>
            </div>

            <ThreatSeverityFilters counts={streamThreatCounts} />

            <div style={{ flex: 1, minHeight: 0, overflow: 'hidden', clipPath: theme.chamfer.clipPath, border: '1px solid rgba(88, 184, 255, 0.34)', background: 'linear-gradient(180deg, rgba(10, 11, 14, 0.92), rgba(7, 8, 10, 0.98))' }}>
              <ConjunctionBullseye conjunctions={sortedEvents} selectedSatId={focusSatelliteId} nowEpochS={model.nowEpochS} maxTcaSeconds={bullseyeMaxTcaSeconds} severityFilter={threatSeverityFilter} />
            </div>
          </div>
        </GlassPanel>

        <div style={{ display: 'grid', gridTemplateRows: 'minmax(0, 0.56fr) minmax(0, 0.44fr)', gap: '14px', minHeight: 0 }}>
          <GlassPanel
            title="ENCOUNTER QUEUE"
            noPadding
            priority="secondary"
            accentColor={theme.colors.warning}
            style={{ minHeight: 0 }}
          >
            <div style={{ display: 'flex', flexDirection: 'column', gap: '8px', flex: 1, minHeight: 0, padding: '10px 12px 12px', overflow: 'auto' }}>
              <div style={{ display: 'flex', flexWrap: 'wrap', alignItems: 'center', justifyContent: 'space-between', gap: '8px', flexShrink: 0 }}>
                <p style={{ color: theme.colors.textDim, fontSize: '10px', lineHeight: 1.5, maxWidth: '42ch' }}>
                  Encounters are grouped by spacecraft and repeated history samples are collapsed into a single row. Choose one to pin this page to a vehicle, or clear the pin from the Focus Vehicle card to return to auto-follow.
                </p>
                <div style={{ display: 'flex', flexWrap: 'wrap', gap: '6px' }}>
                  <InfoChip label="Focused" value={focusSatelliteId ?? 'Auto'} tone={focusSatelliteId ? 'primary' : 'neutral'} />
                  <InfoChip label="Fail-open" value={focusFailOpenCount.toString()} tone={focusFailOpenCount > 0 ? 'warning' : 'neutral'} />
                  <InfoChip label="Vehicle Lanes" value={queueGroups.length.toString()} tone={queueGroups.length > 0 ? 'accent' : 'neutral'} />
                </div>
              </div>
              {flatQueueEntries.length > 0 ? (
                <ConjunctionEventList
                  groups={queueGroups}
                  activeEventKey={activeEventKey}
                  nowEpochS={model.nowEpochS}
                  onSelectEvent={entry => {
                    setActiveEventKey(entry.key);
                    selectSat(entry.event.satellite_id);
                  }}
                />
              ) : (
                <EmptyStatePanel
                  title={streamEvents.length > 0 ? 'FILTERS HIDE ALL ENCOUNTERS' : 'NO ENCOUNTERS IN STREAM'}
                  detail={streamEvents.length > 0
                    ? 'Re-enable one or more severity filters to repopulate the queue and the bullseye.'
                    : 'The predictive or historical conjunction stream has not produced events for the current focus yet.'}
                />
              )}
            </div>
          </GlassPanel>

          <GlassPanel
            title="SELECTED ENCOUNTER"
            noPadding
            priority="secondary"
            accentColor={theme.colors.accent}
            style={{ minHeight: 0 }}
          >
            <div style={{ padding: '10px 12px 12px', overflow: 'auto', flex: 1, minHeight: 0 }}>
              <ConjunctionDetailCard event={activeEvent} nowEpochS={model.nowEpochS} />
            </div>
          </GlassPanel>
        </div>
      </div>
    </section>
  );
}
