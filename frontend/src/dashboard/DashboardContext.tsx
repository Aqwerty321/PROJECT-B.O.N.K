import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useRef,
  useState,
  type ReactNode,
} from 'react';
import {
  useBurns,
  useConjunctions,
  useSnapshot,
  useStatus,
  useTrajectory,
} from '../hooks/useApi';
import { riskLevelForEvent } from '../types/api';
import type {
  AttentionTarget,
  DashboardContextValue,
  FocusOrigin,
  ReasoningLevel,
  SoundMode,
  DashboardViewModel,
  StepStatusSummary,
  StatusCounts,
  ThreatSeverityFilter,
  ThreatCounts,
  TrackHistoryPoint,
} from '../types/dashboard';
import {
  DEFAULT_STEP_STATUS,
  DEFAULT_THREAT_SEVERITY_FILTER,
} from '../types/dashboard';

const TRACK_HISTORY_WINDOW_S = 90 * 60;
const SOUND_MODE_STORAGE_KEY = 'cascade.sound-mode';

const DashboardContext = createContext<DashboardContextValue | null>(null);

function formatWithFormatter(formatter: Intl.DateTimeFormat, value: string): string {
  const date = new Date(value);
  return Number.isNaN(date.getTime()) ? '--' : formatter.format(date);
}

function findNextThreat(
  events: DashboardViewModel['conjList'],
  nowEpochS: number,
) {
  const future = [...events]
    .filter(event => event.tca_epoch_s >= nowEpochS - 300)
    .sort((a, b) => a.tca_epoch_s - b.tca_epoch_s);
  return future[0] ?? null;
}

function findNextPendingBurn(
  burns: DashboardViewModel['pendingBurns'],
) {
  const upcoming = [...burns].sort(
    (a, b) => new Date(a.burn_epoch).getTime() - new Date(b.burn_epoch).getTime(),
  );
  return upcoming[0] ?? null;
}

function findLowestFuelSatellite(satellites: DashboardViewModel['satellites']) {
  return satellites.reduce<typeof satellites[number] | null>((lowest, satellite) => {
    if (!lowest || satellite.fuel_kg < lowest.fuel_kg) {
      return satellite;
    }
    return lowest;
  }, null);
}

function formatSnapshotAgeLabel(updatedAtMs: number | null): string {
  if (!updatedAtMs) return 'Awaiting snapshot';
  const seconds = Math.max(0, Math.round((Date.now() - updatedAtMs) / 1000));
  if (seconds < 60) return `${seconds}s old`;
  const minutes = Math.round(seconds / 60);
  if (minutes < 60) return `${minutes}m old`;
  return `${(minutes / 60).toFixed(1)}h old`;
}

export function DashboardProvider({ children }: { children: ReactNode }) {
  const [booted, setBooted] = useState(false);
  const [selectedSatId, setSelectedSatId] = useState<string | null>(null);
  const [focusOrigin, setFocusOrigin] = useState<FocusOrigin | null>(null);
  const [attentionTarget, setAttentionTarget] = useState<AttentionTarget | null>(null);
  const [soundMode, setSoundModeState] = useState<SoundMode>(() => {
    if (typeof window === 'undefined') return 'alerts';
    const stored = window.localStorage.getItem(SOUND_MODE_STORAGE_KEY);
    return stored === 'muted' || stored === 'alerts' || stored === 'full' ? stored : 'alerts';
  });
  const [reasoningLevel, setReasoningLevel] = useState<ReasoningLevel>('minimal');
  const [spotlightMode, setSpotlightMode] = useState(false);
  const [stepStatus, setStepStatus] = useState<StepStatusSummary>(DEFAULT_STEP_STATUS);
  const [snapshotUpdatedAtMs, setSnapshotUpdatedAtMs] = useState<number | null>(null);
  const [threatSeverityFilter, setThreatSeverityFilter] = useState<ThreatSeverityFilter>(DEFAULT_THREAT_SEVERITY_FILTER);

  const { snapshot, error: snapError } = useSnapshot(booted ? 1000 : 60000);
  const { status, error: statusError } = useStatus(booted ? 2000 : 60000);
  const { burns, error: burnsError } = useBurns(booted ? 2000 : 60000);
  const { conjunctions, error: conjunctionsError } = useConjunctions(booted ? 2000 : 60000, selectedSatId ?? undefined, 'combined');

  const satellites = snapshot?.satellites ?? [];
  const debris = snapshot?.debris_cloud ?? [];
  const conjList = conjunctions?.conjunctions ?? [];
  const executedBurns = burns?.executed ?? [];
  const pendingBurns = burns?.pending ?? [];
  const droppedBurns = burns?.dropped ?? [];
  const burnSummary = burns?.summary ?? null;
  const trajectoryFocusId = selectedSatId ?? satellites[0]?.id ?? null;
  const { trajectory, error: trajectoryError } = useTrajectory(trajectoryFocusId, booted ? 2000 : 60000);

  const selectedTrajectory = selectedSatId && trajectory?.satellite_id === selectedSatId
    ? trajectory
    : null;

  const nowEpochS = useMemo(() => {
    if (snapshot?.timestamp) {
      return new Date(snapshot.timestamp).getTime() / 1000;
    }
    return Date.now() / 1000;
  }, [snapshot?.timestamp]);

  useEffect(() => {
    if (!snapshot?.timestamp) return;
    setSnapshotUpdatedAtMs(Date.now());
  }, [snapshot?.timestamp]);

  const trackHistoryRef = useRef<Map<string, TrackHistoryPoint[]>>(new Map());
  const [trackVersion, setTrackVersion] = useState(0);

  useEffect(() => {
    if (!snapshot) return;

    const map = trackHistoryRef.current;
    const snapshotEpochS = new Date(snapshot.timestamp).getTime() / 1000;

    if (!Number.isFinite(snapshotEpochS)) return;

    for (const satellite of snapshot.satellites) {
      let history = map.get(satellite.id);
      if (!history) {
        history = [];
        map.set(satellite.id, history);
      }
      // Skip duplicate epoch (same sim tick polled multiple times)
      const last = history[history.length - 1];
      if (last && last[0] === snapshotEpochS) continue;
      history.push([snapshotEpochS, satellite.lat, satellite.lon]);
      while (history.length > 0 && snapshotEpochS - history[0][0] > TRACK_HISTORY_WINDOW_S) {
        history.shift();
      }
    }

    for (const [satelliteId, history] of map) {
      while (history.length > 0 && snapshotEpochS - history[0][0] > TRACK_HISTORY_WINDOW_S) {
        history.shift();
      }
      if (history.length === 0) {
        map.delete(satelliteId);
      }
    }

    setTrackVersion(version => version + 1);
  }, [snapshot]);

  useEffect(() => {
    if (!selectedSatId) return;
    if (satellites.some(satellite => satellite.id === selectedSatId)) return;
    setSelectedSatId(null);
    setFocusOrigin(null);
  }, [satellites, selectedSatId]);

  const selectSat = useCallback((id: string | null) => {
    setSelectedSatId(id);
    if (id == null) {
      setFocusOrigin(null);
    }
  }, []);
  const setSoundMode = useCallback((mode: SoundMode) => {
    setSoundModeState(mode);
    if (typeof window !== 'undefined') {
      window.localStorage.setItem(SOUND_MODE_STORAGE_KEY, mode);
    }
  }, []);
  const focusSatFrom = useCallback((id: string | null, origin: FocusOrigin | null) => {
    setSelectedSatId(id);
    setFocusOrigin(id == null ? null : origin);
  }, []);
  const toggleThreatSeverity = useCallback((severity: keyof ThreatSeverityFilter) => {
    setThreatSeverityFilter(current => ({
      ...current,
      [severity]: !current[severity],
    }));
  }, []);

  const activeSatellite = useMemo(
    () => satellites.find(satellite => satellite.id === selectedSatId) ?? null,
    [satellites, selectedSatId],
  );

  const watchedPendingBurns = useMemo(
    () => selectedSatId
      ? pendingBurns.filter(burn => burn.satellite_id === selectedSatId)
      : pendingBurns,
    [pendingBurns, selectedSatId],
  );

  const watchedExecutedBurns = useMemo(
    () => selectedSatId
      ? executedBurns.filter(burn => burn.satellite_id === selectedSatId)
      : executedBurns,
    [executedBurns, selectedSatId],
  );

  const avgFuelKg = useMemo(() => {
    if (satellites.length === 0) return 0;
    const totalFuel = satellites.reduce((sum, satellite) => sum + satellite.fuel_kg, 0);
    return totalFuel / satellites.length;
  }, [satellites]);

  const statusCounts = useMemo<StatusCounts>(() => {
    return satellites.reduce(
      (counts, satellite) => {
        const key = satellite.status.toUpperCase();
        if (key === 'NOMINAL') counts.nominal += 1;
        else if (key === 'MANEUVERING') counts.maneuvering += 1;
        else if (key === 'DEGRADED' || key === 'FUEL_LOW') counts.degraded += 1;
        else if (key === 'GRAVEYARD' || key === 'OFFLINE') counts.graveyard += 1;
        return counts;
      },
      { nominal: 0, maneuvering: 0, degraded: 0, graveyard: 0 },
    );
  }, [satellites]);

  const lowestFuelSatellite = useMemo(
    () => findLowestFuelSatellite(satellites),
    [satellites],
  );

  const threatCounts = useMemo<ThreatCounts>(() => {
    return conjList.reduce(
      (counts, event) => {
        const level = riskLevelForEvent(event);
        counts[level] += 1;
        return counts;
      },
      { red: 0, yellow: 0, green: 0 },
    );
  }, [conjList]);

  const nextThreat = useMemo(
    () => findNextThreat(conjList, nowEpochS),
    [conjList, nowEpochS],
  );

  const nextPendingBurn = useMemo(
    () => findNextPendingBurn(watchedPendingBurns),
    [watchedPendingBurns],
  );

  const missionFormatter = useMemo(() => new Intl.DateTimeFormat(undefined, {
    month: 'short',
    day: 'numeric',
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
    hour12: false,
    timeZone: 'UTC',
  }), []);

  const shortTimeFormatter = useMemo(() => new Intl.DateTimeFormat(undefined, {
    hour: '2-digit',
    minute: '2-digit',
    hour12: false,
    timeZone: 'UTC',
  }), []);

  const watchTargetValue = selectedSatId ?? 'Fleet Overview';
  const watchTargetDetail = activeSatellite
    ? `${activeSatellite.status} / ${activeSatellite.fuel_kg.toFixed(1)} kg fuel remaining`
    : `${statusCounts.nominal} nominal / ${statusCounts.maneuvering} maneuvering / ${statusCounts.degraded} degraded`;

  const threatValue = `${threatCounts.red} critical / ${threatCounts.yellow} watch`;
  const threatDetail = nextThreat
    ? `Next TCA ${formatWithFormatter(shortTimeFormatter, nextThreat.tca)} UTC at ${nextThreat.miss_distance_km.toFixed(2)} km`
    : 'No active conjunction warnings in the current watch list';

  const burnValue = `${watchedPendingBurns.length} pending / ${watchedExecutedBurns.length} logged`;
  const burnDetail = nextPendingBurn
    ? `Next burn ${formatWithFormatter(shortTimeFormatter, nextPendingBurn.burn_epoch)} UTC`
    : 'No pending burns scheduled for the current view';

  const resourceValue = satellites.length > 0 ? `${avgFuelKg.toFixed(1)} kg avg` : 'Awaiting telemetry';
  const resourceDetail = lowestFuelSatellite
    ? `Lowest fuel ${lowestFuelSatellite.id} at ${lowestFuelSatellite.fuel_kg.toFixed(1)} kg`
    : 'Fuel posture appears once fleet telemetry is live';

  const missionValue = snapshot?.timestamp
    ? `${formatWithFormatter(missionFormatter, snapshot.timestamp)} UTC`
    : 'Link Pending';
  const missionDetail = `Tick ${status?.tick_count.toLocaleString() ?? '--'} / ${satellites.length.toLocaleString()} sats / ${debris.length.toLocaleString()} debris`;

  const propagationLastTick = status?.internal_metrics?.propagation_last_tick;
  const slotOutsideBox = propagationLastTick?.stationkeeping_outside_box ?? 0;
  const uploadMissed = propagationLastTick?.upload_window_missed ?? 0;
  const slotErrorMaxKm = propagationLastTick?.stationkeeping_slot_radius_error_max_km ?? 0;
  const recoveryPlanned = propagationLastTick?.recovery_planned ?? 0;
  const opsHealthValue = slotOutsideBox > 0
    ? `${slotOutsideBox} outside box`
    : uploadMissed > 0
      ? `${uploadMissed} upload slips`
      : recoveryPlanned > 0
        ? `${recoveryPlanned} recoveries`
        : 'Within slot box';
  const opsHealthDetail = slotOutsideBox > 0
    ? `Worst slot drift ${slotErrorMaxKm.toFixed(2)} km on the last tick.`
    : uploadMissed > 0
      ? `${uploadMissed} maneuver uploads missed their transmit window on the last tick.`
      : recoveryPlanned > 0
        ? `${recoveryPlanned} recovery maneuvers were planned to regain nominal slot keeping.`
        : 'No slot-box violations or upload misses were reported on the last tick.';
  const opsHealthWarn = slotOutsideBox > 0 || uploadMissed > 0;
  const failOpenCount = conjList.filter(event => event.fail_open).length;
  const droppedCount = burnSummary?.burns_dropped ?? droppedBurns.length;
  const snapshotAgeLabel = formatSnapshotAgeLabel(snapshotUpdatedAtMs);
  const snapshotAgeMs = snapshotUpdatedAtMs == null ? Number.POSITIVE_INFINITY : Date.now() - snapshotUpdatedAtMs;
  const snapshotSeverity = snapshotAgeMs > 45_000
    ? 'critical'
    : snapshotAgeMs > 15_000
      ? 'warning'
      : 'fresh';
  const snapshotStale = snapshotSeverity !== 'fresh';
  const snapshotDetail = snapshotSeverity === 'critical'
    ? 'Snapshot feed is stale enough that operator decisions should pause until freshness recovers.'
    : snapshotSeverity === 'warning'
      ? 'Snapshot feed is aging; continue watching, but verify the feed is still advancing.'
      : 'Snapshot cadence is current.';
  const conjunctionSourceLabel = conjunctions?.source === 'predictive'
    ? 'Predictive 24h'
    : conjunctions?.source === 'history'
      ? 'Historical'
      : conjunctions?.source === 'combined'
        ? 'Combined'
        : 'Unknown';

  const heroModeValue = selectedSatId ?? 'Fleet';
  const heroPathValue = trajectory?.satellite_id ?? 'Standby';
  const focusedCriticalCount = selectedSatId
    ? conjList.filter(event => event.satellite_id === selectedSatId && riskLevelForEvent(event) === 'red').length
    : threatCounts.red;
  const counterfactualCandidate = [...watchedExecutedBurns]
    .filter(burn => burn.scheduled_from_predictive_cdm && burn.trigger_debris_id)
    .sort((lhs, rhs) => new Date(rhs.burn_epoch).getTime() - new Date(lhs.burn_epoch).getTime())[0] ?? null;
  const nextCriticalConjunction = [...conjList]
    .filter(event => riskLevelForEvent(event) === 'red')
    .sort((lhs, rhs) => lhs.tca_epoch_s - rhs.tca_epoch_s)[0] ?? null;
  const operatorChecklist: DashboardViewModel['operatorChecklist'] = [];
  if (snapshotSeverity === 'critical') {
    operatorChecklist.push({
      id: 'stale-critical',
      label: 'Pause Decisions',
      detail: 'Snapshot feed is stale; verify the backend stream before acting on the current picture.',
      tone: 'critical',
      actionLabel: 'Open Threat',
      actionPage: 'threat',
      actionTarget: nextCriticalConjunction ? { kind: 'conjunction', key: `${nextCriticalConjunction.satellite_id}-${nextCriticalConjunction.debris_id}-${nextCriticalConjunction.tca_epoch_s}` } : undefined,
    });
  } else if (snapshotSeverity === 'warning') {
    operatorChecklist.push({
      id: 'stale-warning',
      label: 'Verify Feed',
      detail: 'Snapshot cadence is aging; confirm the feed is still advancing before trusting timing-sensitive calls.',
      tone: 'warning',
      actionLabel: 'Open Threat',
      actionPage: 'threat',
      actionTarget: nextCriticalConjunction ? { kind: 'conjunction', key: `${nextCriticalConjunction.satellite_id}-${nextCriticalConjunction.debris_id}-${nextCriticalConjunction.tca_epoch_s}` } : undefined,
    });
  }
  if (focusedCriticalCount > 0) {
    operatorChecklist.push({
      id: 'critical-conjunction',
      label: `${focusedCriticalCount} Critical ${focusedCriticalCount === 1 ? 'Conjunction' : 'Conjunctions'}`,
      detail: selectedSatId
        ? `Review the focused threat lane for ${selectedSatId}.`
        : 'Review the live critical conjunction queue.',
      tone: 'critical',
      actionLabel: 'Open Threat',
      actionPage: 'threat',
      actionTarget: nextCriticalConjunction ? { kind: 'conjunction', key: `${nextCriticalConjunction.satellite_id}-${nextCriticalConjunction.debris_id}-${nextCriticalConjunction.tca_epoch_s}` } : undefined,
    });
  }
  if (failOpenCount > 0) {
    operatorChecklist.push({
      id: 'fail-open',
      label: `${failOpenCount} Fail-open`,
      detail: 'Inspect encounter geometry before relying on automated collision interpretation.',
      tone: 'warning',
    });
  }
  if (droppedCount > 0) {
    operatorChecklist.push({
      id: 'dropped-burns',
      label: `${droppedCount} Dropped ${droppedCount === 1 ? 'Command' : 'Commands'}`,
      detail: 'Review Burn Ops for upload-window or command-friction loss.',
      tone: 'critical',
      actionLabel: 'Open Burn Ops',
      actionPage: 'burn-ops',
    });
  }
  if (uploadMissed > 0) {
    operatorChecklist.push({
      id: 'upload-slips',
      label: `${uploadMissed} Upload ${uploadMissed === 1 ? 'Slip' : 'Slips'}`,
      detail: 'Verify ground-station timing before the next step.',
      tone: 'warning',
      actionLabel: 'Open Burn Ops',
      actionPage: 'burn-ops',
    });
  }
  if (counterfactualCandidate) {
    operatorChecklist.push({
      id: 'counterfactual-ready',
      label: 'Counterfactual Ready',
      detail: `Compare ${counterfactualCandidate.id} against the no-burn branch in Burn Ops.`,
      tone: 'accent',
      actionLabel: 'Open Burn Ops',
      actionPage: 'burn-ops',
      actionTarget: { kind: 'burn', key: counterfactualCandidate.id },
    });
  }

  const operationsLiveSummary = selectedSatId
    ? `Tracking ${selectedSatId}. ${threatCounts.red} critical conjunctions. ${watchedPendingBurns.length} pending burns in focus.`
    : `Fleet overview active. ${threatCounts.red} critical conjunctions. ${watchedPendingBurns.length} pending burns in queue.`;

  const model = useMemo<DashboardViewModel>(() => ({
    snapshot,
    status,
    burns,
    trajectory,
    selectedTrajectory,
    snapError,
    statusError,
    burnsError,
    conjunctionsError,
    trajectoryError,
    satellites,
    debris,
    conjList,
    executedBurns,
    pendingBurns,
    droppedBurns,
    burnSummary,
    nowEpochS,
    snapshotUpdatedAtMs,
    trackHistory: trackHistoryRef.current,
    trackVersion,
    activeSatellite,
    watchedPendingBurns,
    watchedExecutedBurns,
    avgFuelKg,
    statusCounts,
    lowestFuelSatellite,
    threatCounts,
    nextThreat,
    nextPendingBurn,
    missionValue,
    missionDetail,
    watchTargetValue,
    watchTargetDetail,
    threatValue,
    threatDetail,
    burnValue,
    burnDetail,
    resourceValue,
    resourceDetail,
    heroModeValue,
    heroPathValue,
    operationsLiveSummary,
    opsHealthValue,
    opsHealthDetail,
    opsHealthWarn,
    operatorChecklist,
    truthBanner: {
      snapshotAgeLabel,
      snapshotStale,
      snapshotSeverity,
      snapshotDetail,
      conjunctionSourceLabel,
      failOpenCount,
      droppedCount,
      uploadMissedCount: uploadMissed,
    },
  }), [
    activeSatellite,
    avgFuelKg,
    burnDetail,
    burnValue,
    burns,
    burnsError,
    conjList,
    conjunctionSourceLabel,
    conjunctionsError,
    debris,
    droppedCount,
    droppedBurns,
    executedBurns,
    failOpenCount,
    heroModeValue,
    heroPathValue,
    lowestFuelSatellite,
    missionDetail,
    missionValue,
    nextPendingBurn,
    nextThreat,
    nowEpochS,
    opsHealthDetail,
    opsHealthValue,
    opsHealthWarn,
    operatorChecklist,
    operationsLiveSummary,
    pendingBurns,
    burnSummary,
    resourceDetail,
    resourceValue,
    satellites,
    selectedTrajectory,
    snapError,
    snapshot,
    snapshotAgeLabel,
    snapshotDetail,
    snapshotSeverity,
    snapshotStale,
    snapshotUpdatedAtMs,
    status,
    statusCounts,
    statusError,
    threatCounts,
    threatDetail,
    threatValue,
    trackVersion,
    trajectory,
    trajectoryError,
    uploadMissed,
    watchTargetDetail,
    watchTargetValue,
    watchedExecutedBurns,
    watchedPendingBurns,
  ]);

  const value = useMemo<DashboardContextValue>(() => ({
    booted,
    setBooted,
    selectedSatId,
    selectSat,
    focusOrigin,
    setFocusOrigin,
    focusSatFrom,
    attentionTarget,
    setAttentionTarget,
    soundMode,
    setSoundMode,
    reasoningLevel,
    setReasoningLevel,
    spotlightMode,
    setSpotlightMode,
    threatSeverityFilter,
    toggleThreatSeverity,
    setThreatSeverityFilter,
    stepStatus,
    setStepStatus,
    model,
  }), [
    booted,
    attentionTarget,
    focusOrigin,
    focusSatFrom,
    model,
    reasoningLevel,
    selectSat,
    selectedSatId,
    setFocusOrigin,
    setAttentionTarget,
    setSoundMode,
    soundMode,
    spotlightMode,
    stepStatus,
    threatSeverityFilter,
    toggleThreatSeverity,
  ]);

  return (
    <DashboardContext.Provider value={value}>
      {children}
    </DashboardContext.Provider>
  );
}

export function useDashboard() {
  const value = useContext(DashboardContext);
  if (!value) {
    throw new Error('useDashboard must be used within DashboardProvider');
  }
  return value;
}
