import { riskLevelForEvent, type ConjunctionEvent } from '../../types/api';

export interface ConjunctionQueueEntry {
  key: string;
  event: ConjunctionEvent;
  sampleCount: number;
}

export interface ConjunctionQueueGroup {
  satelliteId: string;
  entries: ConjunctionQueueEntry[];
  distinctDebrisCount: number;
  collapsedSamples: number;
  nextTcaEpochS: number | null;
  isFocused: boolean;
}

export function eventKey(event: ConjunctionEvent): string {
  return `${event.satellite_id}-${event.debris_id}-${event.tca_epoch_s}`;
}

function severityRank(event: ConjunctionEvent): number {
  const level = riskLevelForEvent(event);
  if (level === 'red') return 3;
  if (level === 'yellow') return 2;
  return 1;
}

function eventDedupKey(event: ConjunctionEvent): string {
  const tcaBucket = Math.round(event.tca_epoch_s / (event.predictive ? 60 : 300));
  const missBucket = Math.round(event.miss_distance_km * (event.predictive ? 100 : 20));
  const stream = event.predictive ? 'predictive' : 'history';
  const severity = event.severity ?? riskLevelForEvent(event);
  return `${event.satellite_id}|${event.debris_id}|${stream}|${severity}|${event.fail_open ? 'fail-open' : 'nominal'}|${tcaBucket}|${missBucket}`;
}

export function buildEncounterQueueGroups(
  events: ConjunctionEvent[],
  focusSatelliteId: string | null,
): ConjunctionQueueGroup[] {
  const deduped = new Map<string, ConjunctionQueueEntry>();

  for (const event of events) {
    const dedupKey = eventDedupKey(event);
    const current = deduped.get(dedupKey);
    if (!current) {
      deduped.set(dedupKey, {
        key: eventKey(event),
        event,
        sampleCount: 1,
      });
      continue;
    }

    current.sampleCount += 1;
    const replace = event.predictive
      || event.tca_epoch_s < current.event.tca_epoch_s
      || event.miss_distance_km < current.event.miss_distance_km;
    if (replace) {
      current.event = event;
      current.key = eventKey(event);
    }
  }

  const groupsBySat = new Map<string, ConjunctionQueueEntry[]>();
  for (const entry of deduped.values()) {
    const entries = groupsBySat.get(entry.event.satellite_id) ?? [];
    entries.push(entry);
    groupsBySat.set(entry.event.satellite_id, entries);
  }

  const groups: ConjunctionQueueGroup[] = [];
  for (const [satelliteId, entries] of groupsBySat) {
    entries.sort((a, b) => {
      if (a.event.predictive !== b.event.predictive) return a.event.predictive ? -1 : 1;
      const severityDelta = severityRank(b.event) - severityRank(a.event);
      if (severityDelta !== 0) return severityDelta;
      if (a.event.tca_epoch_s !== b.event.tca_epoch_s) return a.event.tca_epoch_s - b.event.tca_epoch_s;
      if (a.event.miss_distance_km !== b.event.miss_distance_km) return a.event.miss_distance_km - b.event.miss_distance_km;
      return a.event.debris_id.localeCompare(b.event.debris_id);
    });

    groups.push({
      satelliteId,
      entries,
      distinctDebrisCount: new Set(entries.map(entry => entry.event.debris_id)).size,
      collapsedSamples: entries.reduce((sum, entry) => sum + Math.max(0, entry.sampleCount - 1), 0),
      nextTcaEpochS: entries[0]?.event.tca_epoch_s ?? null,
      isFocused: focusSatelliteId === satelliteId,
    });
  }

  groups.sort((a, b) => {
    if (a.isFocused !== b.isFocused) return a.isFocused ? -1 : 1;
    const aTop = a.entries[0]?.event;
    const bTop = b.entries[0]?.event;
    if (aTop && bTop) {
      const severityDelta = severityRank(bTop) - severityRank(aTop);
      if (severityDelta !== 0) return severityDelta;
      if (aTop.tca_epoch_s !== bTop.tca_epoch_s) return aTop.tca_epoch_s - bTop.tca_epoch_s;
      if (aTop.miss_distance_km !== bTop.miss_distance_km) return aTop.miss_distance_km - bTop.miss_distance_km;
    }
    return a.satelliteId.localeCompare(b.satelliteId);
  });

  return groups;
}

export function flattenEncounterQueueGroups(groups: ConjunctionQueueGroup[]): ConjunctionQueueEntry[] {
  return groups.flatMap(group => group.entries);
}

export function countCollapsedEncounterSamples(groups: ConjunctionQueueGroup[]): number {
  return groups.reduce((sum, group) => sum + group.collapsedSamples, 0);
}
