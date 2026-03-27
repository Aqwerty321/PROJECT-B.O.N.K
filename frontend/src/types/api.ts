// API response types matching backend JSON shapes exactly

export interface SatelliteSnapshot {
  id: string;
  lat: number;   // degrees
  lon: number;   // degrees
  alt_km: number; // altitude above WGS-84 ellipsoid
  eci_r?: { x: number; y: number; z: number }; // ECI position in km
  eci_v?: { x: number; y: number; z: number }; // ECI velocity in km/s
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
    active_cdm_warnings?: number;
    predictive_conjunction_count?: number;
    history_conjunction_count?: number;
    dropped_burn_count?: number;
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
    propagation_last_tick?: {
      collisions_detected: number;
      maneuvers_executed: number;
      auto_planned_maneuvers: number;
      recovery_planned: number;
      recovery_deferred: number;
      recovery_completed: number;
      graveyard_planned: number;
      graveyard_deferred: number;
      graveyard_completed: number;
      upload_window_missed: number;
      stationkeeping_outside_box: number;
      stationkeeping_uptime_penalty_mean: number;
      stationkeeping_slot_radius_error_mean_km: number;
      stationkeeping_slot_radius_error_max_km: number;
      recovery_pending_marked: number;
      recovery_slot_error_mean: number;
      recovery_slot_error_max: number;
    };
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

// Ground stations from docs/groundstations.csv
export interface GroundStation {
  name: string;
  lat: number;
  lon: number;
}

export const GROUND_STATIONS: GroundStation[] = [
  { name: "ISTRAC Bengaluru",  lat:  13.0333, lon:  77.5167 },
  { name: "Svalbard",          lat:  78.2297, lon:  15.4077 },
  { name: "Goldstone",         lat:  35.4266, lon:-116.89   },
  { name: "Punta Arenas",      lat: -53.15,   lon: -70.9167 },
  { name: "IIT Delhi",         lat:  28.545,  lon:  77.1926 },
  { name: "McMurdo",           lat: -77.8463, lon: 166.6682 },
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
    case 'FUEL_LOW':     return 0xf97316;
    case 'OFFLINE':      return 0x6b7280;
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
  blackout_overlap?: boolean;
  cooldown_conflict?: boolean;
  command_conflict?: boolean;
  scheduled_from_predictive_cdm?: boolean;
  trigger_debris_id?: string;
  trigger_tca?: string;
  trigger_tca_epoch_s?: number;
  trigger_miss_distance_km?: number;
  trigger_approach_speed_km_s?: number;
  trigger_fail_open?: boolean;
  mitigation_tracked?: boolean;
  mitigation_evaluated?: boolean;
  collision_avoided?: boolean;
  mitigation_eval_epoch?: string;
  mitigation_eval_epoch_s?: number;
  mitigation_miss_distance_km?: number;
  mitigation_fail_open?: boolean;
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
  blackout_overlap?: boolean;
  cooldown_conflict?: boolean;
  command_conflict?: boolean;
  scheduled_from_predictive_cdm?: boolean;
  trigger_debris_id?: string;
  trigger_tca?: string;
  trigger_tca_epoch_s?: number;
  trigger_miss_distance_km?: number;
  trigger_approach_speed_km_s?: number;
  trigger_fail_open?: boolean;
  upload_window_missed?: boolean;
  dropped_epoch?: string;
  dropped_epoch_s?: number;
}

export interface PerSatBurnStats {
  burns_executed: number;
  delta_v_total_km_s: number;
  fuel_consumed_kg: number;
  collisions_avoided?: number;
  avoidance_burns_executed?: number;
  recovery_burns_executed?: number;
  graveyard_burns_executed?: number;
  avoidance_fuel_consumed_kg?: number;
}

export interface BurnSummary {
  burns_executed: number;
  burns_pending: number;
  burns_dropped: number;
  fuel_consumed_kg: number;
  avoidance_fuel_consumed_kg: number;
  collisions_avoided: number;
  avoidance_burns_executed: number;
  recovery_burns_executed: number;
  graveyard_burns_executed: number;
}

export interface BurnsResponse {
  executed: ExecutedBurn[];
  dropped?: PendingBurn[];
  pending: PendingBurn[];
  per_satellite: Record<string, PerSatBurnStats>;
  summary?: BurnSummary;
}

export type CdmSeverity = 'critical' | 'warning' | 'watch';

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
   predictive?: boolean;
   fail_open?: boolean;
  severity?: CdmSeverity;  // tiered severity from backend
  tick_id: number;
}

export interface ConjunctionsResponse {
  conjunctions: ConjunctionEvent[];
  count: number;
  source?: string;
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

/** Derive RiskLevel from a ConjunctionEvent, preferring server severity. */
export function riskLevelForEvent(e: ConjunctionEvent): RiskLevel {
  if (e.severity) {
    switch (e.severity) {
      case 'critical': return 'red';
      case 'warning':  return 'yellow';
      case 'watch':    return 'green';
    }
  }
  return riskLevelFromDistance(e.miss_distance_km);
}

/**
 * Simple probability-of-collision proxy.
 * Based on miss distance and approach speed — higher speed + lower distance
 * means higher risk.  Returns a value in [0, 1].
 */
export function pcProxy(miss_km: number, speed_km_s: number): number {
  // Hard-body collision sphere ~100m = 0.1 km.
  // Pc ~ exp(-0.5 * (miss / sigma)^2)  where sigma scales with miss geometry.
  // We use a simplified model: sigma = 0.1 km (hard-body) + speed-dependent term.
  const sigma = 0.1 + Math.min(speed_km_s * 0.02, 0.5);
  const x = miss_km / sigma;
  return Math.exp(-0.5 * x * x);
}
