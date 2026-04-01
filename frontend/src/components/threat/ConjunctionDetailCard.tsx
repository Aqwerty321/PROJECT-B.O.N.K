import type { ConjunctionEvent } from '../../types/api';
import { riskLevelForEvent, pcProxy } from '../../types/api';
import { theme } from '../../styles/theme';
import { DetailList, EmptyStatePanel, InfoChip, type Tone } from '../dashboard/UiPrimitives';

const COLLISION_THRESHOLD_KM = 0.1;

function toneForEvent(event: ConjunctionEvent): Tone {
  const level = riskLevelForEvent(event);
  if (level === 'red') return 'critical';
  if (level === 'yellow') return 'warning';
  return 'accent';
}

function formatUtc(tca: string): string {
  const date = new Date(tca);
  if (Number.isNaN(date.getTime())) return '--';
  return `${new Intl.DateTimeFormat(undefined, {
    month: 'short',
    day: 'numeric',
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
    hour12: false,
    timeZone: 'UTC',
  }).format(date)} UTC`;
}

function formatOffset(secondsToTca: number): string {
  const sign = secondsToTca >= 0 ? 'T+' : 'T-';
  const totalSeconds = Math.abs(secondsToTca);
  const hours = Math.floor(totalSeconds / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  const seconds = totalSeconds % 60;
  if (hours > 0) return `${sign}${hours}h ${minutes}m`;
  if (minutes > 0) return `${sign}${minutes}m ${seconds}s`;
  return `${sign}${seconds}s`;
}

export function ConjunctionDetailCard({
  event,
  nowEpochS,
}: {
  event: ConjunctionEvent | null;
  nowEpochS: number;
}) {
  if (!event) {
    return (
      <EmptyStatePanel
        title="NO EVENT SELECTED"
        detail="Choose a conjunction from the watch list to inspect TCA, miss distance, approach geometry, and collision state."
      />
    );
  }

  const tone = toneForEvent(event);
  const secondsToTca = Math.round(event.tca_epoch_s - nowEpochS);
  const timingLabel = formatOffset(secondsToTca);
  const streamLabel = event.predictive ? 'Predictive 24h' : 'Historical';
  const streamDetail = event.fail_open
    ? 'Escalated because the predictive pass could not safely clear the pair.'
    : event.predictive
      ? 'Persistent 24-hour CDM record selected from the live predictive stream.'
      : 'Historical conjunction from the current simulation watch history.';
  const pc = pcProxy(event.miss_distance_km, event.approach_speed_km_s);
  const severityStr = event.severity
    ? event.severity.charAt(0).toUpperCase() + event.severity.slice(1)
    : (tone === 'critical' ? 'Critical' : tone === 'warning' ? 'Warning' : 'Watch');

  return (
    <div
      style={{
        display: 'flex',
        flexDirection: 'column',
        gap: '12px',
        padding: '14px 16px 16px',
        border: `1px solid ${theme.colors.border}`,
        background: 'rgba(10, 11, 14, 0.88)',
        clipPath: theme.chamfer.clipPath,
      }}
    >
      <div style={{ display: 'flex', flexDirection: 'column', gap: '6px' }}>
        <span style={{ color: tone === 'critical' ? theme.colors.critical : tone === 'warning' ? theme.colors.warning : theme.colors.accent, fontSize: '10px', letterSpacing: '0.16em', textTransform: 'uppercase' }}>
          Selected Encounter
        </span>
        <span style={{ color: theme.colors.text, fontSize: '18px', fontWeight: 700 }}>
          {event.satellite_id} vs {event.debris_id}
        </span>
        <span style={{ color: theme.colors.textDim, fontSize: '12px', lineHeight: 1.55 }}>
          This is the encounter currently driving the global watch target. Confirm when it happens, how close it gets, and whether the stream classified it as predictive or fail-open. {streamDetail}
        </span>
      </div>

      <div style={{ display: 'flex', flexWrap: 'wrap', gap: '8px' }}>
        <InfoChip label="Severity" value={severityStr} tone={tone} active />
        <InfoChip label="TCA" value={formatUtc(event.tca)} tone="primary" active />
        <InfoChip label="Miss" value={`${event.miss_distance_km.toFixed(3)} km`} tone={tone} active />
        <InfoChip label="Stream" value={streamLabel} tone={event.predictive ? 'warning' : 'neutral'} active />
        <InfoChip label="Collision Threshold" value={`${COLLISION_THRESHOLD_KM.toFixed(3)} km`} tone="critical" active />
        <InfoChip label="Provenance" value={event.predictive ? 'Predictive CDM' : 'Historical record'} tone="neutral" active />
        {event.fail_open && <InfoChip label="Fail-open" value="Active" tone="warning" active />}
      </div>

      <DetailList
        entries={[
          { label: 'Time Offset', value: timingLabel, tone: secondsToTca < 0 ? 'warning' : 'primary' },
          { label: 'Approach Speed', value: `${event.approach_speed_km_s.toFixed(4)} km/s`, tone: 'primary' },
          { label: 'Pc (est.)', value: pc < 0.001 ? pc.toExponential(2) : pc.toFixed(4), tone: pc > 0.5 ? 'critical' : pc > 0.01 ? 'warning' : 'accent' },
          { label: 'Fail-open', value: event.fail_open ? 'TRUE' : 'FALSE', tone: event.fail_open ? 'warning' : 'accent' },
          { label: 'Collision Flag', value: event.collision ? 'TRUE' : 'FALSE', tone: event.collision ? 'critical' : 'accent' },
          { label: 'Tick', value: event.tick_id.toLocaleString(), tone: 'neutral' },
          { label: 'Sat ECI', value: event.sat_pos_eci_km.map(value => value.toFixed(2)).join(', ') },
          { label: 'Debris ECI', value: event.deb_pos_eci_km.map(value => value.toFixed(2)).join(', ') },
        ]}
      />
    </div>
  );
}
