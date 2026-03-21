// API response types matching backend JSON shapes exactly

export interface SatelliteSnapshot {
  id: string;
  lat: number;   // degrees
  lon: number;   // degrees
  fuel_kg: number;
  status: string; // "NOMINAL" | "MANEUVERING" | "DEGRADED" | "GRAVEYARD"
}

// Debris compact tuple: [id, lat, lon, alt_km]
export type DebrisTuple = [string, number, number, number];

export interface VisualizationSnapshot {
  timestamp: string;
  satellites: SatelliteSnapshot[];
  debris_cloud: DebrisTuple[];
}

export interface StatusResponse {
  status: string;
  uptime_s: number;
  tick_count: number;
  object_count: number;
  internal_metrics?: {
    satellite_count: number;
    debris_count: number;
    pending_burn_queue: number;
    pending_recovery_requests: number;
    pending_graveyard_requests: number;
    command_queue_depth: number;
    command_queue_depth_max: number;
    command_queue_depth_limit: number;
    failed_objects_last_tick: number;
    failed_objects_total: number;
    command_queue_enqueued_total: number;
    command_queue_completed_total: number;
    command_queue_rejected_total: number;
    command_queue_timeout_total: number;
    command_latency_us: Record<string, LatencyStats>;
  };
}

export interface LatencyStats {
  count: number;
  queue_wait_us_total: number;
  queue_wait_us_max: number;
  queue_wait_us_last: number;
  queue_wait_us_mean: number;
  execution_us_total: number;
  execution_us_max: number;
  execution_us_last: number;
  execution_us_mean: number;
}

export interface ConflictsResponse {
  status: string;
  total_conflicts: number;
  ring_size: number;
  conflicts_by_source: Record<string, number>;
  recent: ConflictRecord[];
}

export interface ConflictRecord {
  object_id: string;
  stored_type: string;
  incoming_type: string;
  telemetry_timestamp: string;
  ingestion_timestamp: string;
  source_id: string;
  reason: string;
}

// Ground stations from docs/groundstations.csv
export interface GroundStation {
  name: string;
  lat: number;
  lon: number;
}

export const GROUND_STATIONS: GroundStation[] = [
  { name: "Svalbard",      lat:  78.23, lon:  15.40 },
  { name: "McMurdo",       lat: -77.85, lon: 166.67 },
  { name: "Hartebeesthoek",lat: -25.89, lon:  27.68 },
  { name: "Fucino",        lat:  41.97, lon:  13.60 },
  { name: "Inuvik",        lat:  68.36, lon:-133.72 },
  { name: "Singapore",     lat:   1.29, lon: 103.85 },
];

// Risk color thresholds (from PS.md conjunction warning levels)
export type RiskLevel = 'green' | 'yellow' | 'red';

export function riskColor(level: RiskLevel): string {
  switch (level) {
    case 'green':  return '#22c55e';
    case 'yellow': return '#eab308';
    case 'red':    return '#ef4444';
  }
}

export function statusColor(status: string): number {
  switch (status) {
    case 'NOMINAL':      return 0x22c55e;
    case 'MANEUVERING':  return 0xeab308;
    case 'DEGRADED':     return 0xf97316;
    case 'GRAVEYARD':    return 0x6b7280;
    default:             return 0x60a5fa;
  }
}

// --- New endpoint types (Phase 0B-E) ---

export interface ExecutedBurn {
  id: string;
  satellite_id: string;
  burn_epoch: string;      // ISO-8601
  upload_epoch: string;
  upload_station: string;
  delta_v_km_s: [number, number, number];
  delta_v_norm_km_s: number;
  fuel_before_kg: number;
  fuel_after_kg: number;
  auto_generated: boolean;
  recovery_burn: boolean;
  graveyard_burn: boolean;
}

export interface PendingBurn {
  id: string;
  satellite_id: string;
  burn_epoch: string;
  upload_epoch: string;
  upload_station: string;
  delta_v_km_s: [number, number, number];
  delta_v_norm_km_s: number;
  auto_generated: boolean;
  recovery_burn: boolean;
  graveyard_burn: boolean;
}

export interface PerSatBurnStats {
  burns_executed: number;
  delta_v_total_km_s: number;
  fuel_consumed_kg: number;
}

export interface BurnsResponse {
  executed: ExecutedBurn[];
  pending: PendingBurn[];
  per_satellite: Record<string, PerSatBurnStats>;
}

export interface ConjunctionEvent {
  satellite_id: string;
  debris_id: string;
  tca: string;             // ISO-8601
  tca_epoch_s: number;
  miss_distance_km: number;
  approach_speed_km_s: number;
  sat_pos_eci_km: [number, number, number];
  deb_pos_eci_km: [number, number, number];
  collision: boolean;
  tick_id: number;
}

export interface ConjunctionsResponse {
  conjunctions: ConjunctionEvent[];
  count: number;
}

export interface TrajectoryPoint {
  epoch_s: number;
  lat: number;
  lon: number;
  alt_km: number;
  eci: [number, number, number];
}

export interface TrajectoryResponse {
  satellite_id: string;
  trail: TrajectoryPoint[];
  predicted: TrajectoryPoint[];
}

export function riskLevelFromDistance(km: number): RiskLevel {
  if (km < 1) return 'red';
  if (km < 5) return 'yellow';
  return 'green';
}
