import type { ConjunctionEvent } from '../../types/api';
import { riskLevelFromDistance } from '../../types/api';
import { theme } from '../../styles/theme';
import { DetailList, EmptyStatePanel, type Tone } from '../dashboard/UiPrimitives';

function toneForEvent(event: ConjunctionEvent): Tone {
  const level = riskLevelFromDistance(event.miss_distance_km);
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
  const timingLabel = secondsToTca >= 0
    ? `T+${secondsToTca.toLocaleString()} s`
    : `T${secondsToTca.toLocaleString()} s`;

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
          Encounter Detail
        </span>
        <span style={{ color: theme.colors.text, fontSize: '18px', fontWeight: 700 }}>
          {event.satellite_id} vs {event.debris_id}
        </span>
        <span style={{ color: theme.colors.textDim, fontSize: '12px', lineHeight: 1.55 }}>
          Selected event locks the global watch target so the Track and Burn Ops pages stay in sync with this spacecraft.
        </span>
      </div>

      <DetailList
        entries={[
          { label: 'TCA', value: formatUtc(event.tca), tone },
          { label: 'Time Offset', value: timingLabel, tone: secondsToTca < 0 ? 'warning' : 'primary' },
          { label: 'Miss Distance', value: `${event.miss_distance_km.toFixed(3)} km`, tone },
          { label: 'Approach Speed', value: `${event.approach_speed_km_s.toFixed(4)} km/s`, tone: 'primary' },
          { label: 'Stream', value: event.predictive ? 'Predictive 24h' : 'Historical', tone: event.predictive ? 'warning' : 'neutral' },
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
