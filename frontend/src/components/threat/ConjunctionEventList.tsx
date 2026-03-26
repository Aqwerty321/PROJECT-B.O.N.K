import type { ConjunctionEvent } from '../../types/api';
import { riskLevelFromDistance } from '../../types/api';
import { theme } from '../../styles/theme';
import { toneColor, type Tone } from '../dashboard/UiPrimitives';

function toneForEvent(event: ConjunctionEvent): Tone {
  const level = riskLevelFromDistance(event.miss_distance_km);
  if (level === 'red') return 'critical';
  if (level === 'yellow') return 'warning';
  return 'accent';
}

function formatTca(tca: string): string {
  const date = new Date(tca);
  if (Number.isNaN(date.getTime())) return '--';
  return new Intl.DateTimeFormat(undefined, {
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
    hour12: false,
    timeZone: 'UTC',
  }).format(date);
}

export function ConjunctionEventList({
  events,
  activeEvent,
  onSelectEvent,
}: {
  events: ConjunctionEvent[];
  activeEvent: ConjunctionEvent | null;
  onSelectEvent: (event: ConjunctionEvent) => void;
}) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: '8px', minHeight: 0, overflowY: 'auto', paddingRight: '4px' }}>
      {events.map(event => {
        const tone = toneForEvent(event);
        const color = toneColor(tone);
        const isActive = activeEvent?.satellite_id === event.satellite_id
          && activeEvent?.debris_id === event.debris_id
          && activeEvent?.tca_epoch_s === event.tca_epoch_s;

        return (
          <button
            key={`${event.satellite_id}-${event.debris_id}-${event.tca_epoch_s}`}
            type="button"
            onClick={() => onSelectEvent(event)}
            style={{
              display: 'flex',
              flexDirection: 'column',
              gap: '7px',
              padding: '11px 12px',
              border: `1px solid ${isActive ? `${color}66` : 'rgba(255,255,255,0.06)'}`,
              background: isActive ? 'rgba(255,255,255,0.06)' : 'rgba(255,255,255,0.02)',
              color: theme.colors.text,
              cursor: 'pointer',
              textAlign: 'left',
              clipPath: theme.chamfer.buttonClipPath,
            }}
          >
            <div style={{ display: 'flex', justifyContent: 'space-between', gap: '12px', alignItems: 'center' }}>
              <span style={{ color, fontSize: '10px', letterSpacing: '0.16em', textTransform: 'uppercase' }}>
                {tone === 'critical' ? 'Critical' : tone === 'warning' ? 'Warning' : 'Nominal'}
              </span>
              <span style={{ color: theme.colors.textDim, fontSize: '10px' }}>{formatTca(event.tca)} UTC</span>
            </div>
            <span style={{ fontSize: '13px', fontWeight: 700, letterSpacing: '0.03em' }}>
              {event.satellite_id}
            </span>
            <div style={{ display: 'grid', gridTemplateColumns: 'repeat(2, minmax(0, 1fr))', gap: '6px 12px' }}>
              <span style={{ color: theme.colors.textDim, fontSize: '11px' }}>Debris {event.debris_id}</span>
              <span style={{ color, fontSize: '11px', textAlign: 'right' }}>{event.miss_distance_km.toFixed(2)} km miss</span>
              <span style={{ color: theme.colors.textDim, fontSize: '11px' }}>{event.approach_speed_km_s.toFixed(3)} km/s</span>
              <span style={{ color: event.collision ? theme.colors.critical : theme.colors.textMuted, fontSize: '11px', textAlign: 'right' }}>
                {event.fail_open ? 'Fail-open warning' : event.predictive ? 'Predictive 24h' : event.collision ? 'Collision flagged' : 'Predicted approach'}
              </span>
            </div>
          </button>
        );
      })}
    </div>
  );
}
