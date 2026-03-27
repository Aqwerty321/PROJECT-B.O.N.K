import type {
  BurnsResponse,
  BurnSummary,
  ConjunctionEvent,
  DebrisTuple,
  ExecutedBurn,
  PendingBurn,
  RiskLevel,
  SatelliteSnapshot,
  StatusResponse,
  TrajectoryResponse,
  VisualizationSnapshot,
} from './api';

export type StepStatusTone = 'idle' | 'busy' | 'ok' | 'warning' | 'error';

export interface StepStatusSummary {
  tone: StepStatusTone;
  title: string;
  detail: string;
  activeLabel?: string | null;
  updatedAt?: number;
}

export const DEFAULT_STEP_STATUS: StepStatusSummary = {
  tone: 'idle',
  title: 'COMMAND READY',
  detail: 'Advance the simulation clock and sync the mission views.',
  activeLabel: null,
};

export interface StatusCounts {
  nominal: number;
  maneuvering: number;
  degraded: number;
  graveyard: number;
}

export interface ThreatCounts {
  red: number;
  yellow: number;
  green: number;
}

export interface ThreatSeverityFilter {
  critical: boolean;
  warning: boolean;
  watch: boolean;
}

export const DEFAULT_THREAT_SEVERITY_FILTER: ThreatSeverityFilter = {
  critical: true,
  warning: true,
  watch: true,
};

export function threatFilterKeyForRiskLevel(level: RiskLevel): keyof ThreatSeverityFilter {
  if (level === 'red') return 'critical';
  if (level === 'yellow') return 'warning';
  return 'watch';
}

export function threatFilterAllowsRiskLevel(level: RiskLevel, filter: ThreatSeverityFilter): boolean {
  return filter[threatFilterKeyForRiskLevel(level)];
}

export function hasActiveThreatSeverityFilter(filter: ThreatSeverityFilter): boolean {
  return filter.critical || filter.warning || filter.watch;
}

export type TrackHistoryPoint = [number, number, number];

export interface DashboardViewModel {
  snapshot: VisualizationSnapshot | null;
  status: StatusResponse | null;
  burns: BurnsResponse | null;
  trajectory: TrajectoryResponse | null;
  selectedTrajectory: TrajectoryResponse | null;
  snapError: string | null;
  statusError: string | null;
  burnsError: string | null;
  conjunctionsError: string | null;
  trajectoryError: string | null;
  satellites: SatelliteSnapshot[];
  debris: DebrisTuple[];
  conjList: ConjunctionEvent[];
  executedBurns: ExecutedBurn[];
  pendingBurns: PendingBurn[];
  droppedBurns: PendingBurn[];
  burnSummary: BurnSummary | null;
  nowEpochS: number;
  snapshotUpdatedAtMs: number | null;
  trackHistory: Map<string, TrackHistoryPoint[]>;
  trackVersion: number;
  activeSatellite: SatelliteSnapshot | null;
  watchedPendingBurns: PendingBurn[];
  watchedExecutedBurns: ExecutedBurn[];
  avgFuelKg: number;
  statusCounts: StatusCounts;
  lowestFuelSatellite: SatelliteSnapshot | null;
  threatCounts: ThreatCounts;
  nextThreat: ConjunctionEvent | null;
  nextPendingBurn: PendingBurn | null;
  missionValue: string;
  missionDetail: string;
  watchTargetValue: string;
  watchTargetDetail: string;
  threatValue: string;
  threatDetail: string;
  burnValue: string;
  burnDetail: string;
  resourceValue: string;
  resourceDetail: string;
  heroModeValue: string;
  heroPathValue: string;
  operationsLiveSummary: string;
}

export interface DashboardContextValue {
  booted: boolean;
  setBooted: (value: boolean) => void;
  selectedSatId: string | null;
  selectSat: (id: string | null) => void;
  threatSeverityFilter: ThreatSeverityFilter;
  toggleThreatSeverity: (severity: keyof ThreatSeverityFilter) => void;
  setThreatSeverityFilter: (filter: ThreatSeverityFilter) => void;
  stepStatus: StepStatusSummary;
  setStepStatus: (status: StepStatusSummary) => void;
  model: DashboardViewModel;
}
