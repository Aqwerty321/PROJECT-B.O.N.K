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
  DashboardContextValue,
  DashboardViewModel,
  StepStatusSummary,
  StatusCounts,
  ThreatCounts,
  TrackHistoryPoint,
} from '../types/dashboard';
import { DEFAULT_STEP_STATUS } from '../types/dashboard';

const TRACK_HISTORY_WINDOW_S = 90 * 60;

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

export function DashboardProvider({ children }: { children: ReactNode }) {
  const [booted, setBooted] = useState(false);
  const [selectedSatId, setSelectedSatId] = useState<string | null>(null);
  const [stepStatus, setStepStatus] = useState<StepStatusSummary>(DEFAULT_STEP_STATUS);
  const [snapshotUpdatedAtMs, setSnapshotUpdatedAtMs] = useState<number | null>(null);

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
  }, [satellites, selectedSatId]);

  const selectSat = useCallback((id: string | null) => setSelectedSatId(id), []);

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

  const heroModeValue = selectedSatId ?? 'Fleet';
  const heroPathValue = trajectory?.satellite_id ?? 'Standby';

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
  }), [
    activeSatellite,
    avgFuelKg,
    burnDetail,
    burnValue,
    burns,
    burnsError,
    conjList,
    conjunctionsError,
    debris,
    droppedBurns,
    executedBurns,
    heroModeValue,
    heroPathValue,
    lowestFuelSatellite,
    missionDetail,
    missionValue,
    nextPendingBurn,
    nextThreat,
    nowEpochS,
    operationsLiveSummary,
    pendingBurns,
    burnSummary,
    resourceDetail,
    resourceValue,
    satellites,
    selectedTrajectory,
    snapError,
    snapshot,
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
    stepStatus,
    setStepStatus,
    model,
  }), [booted, model, selectSat, selectedSatId, stepStatus]);

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
