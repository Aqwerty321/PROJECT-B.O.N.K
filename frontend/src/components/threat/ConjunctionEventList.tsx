import type { ConjunctionEvent } from '../../types/api';
import { riskLevelForEvent } from '../../types/api';
import { theme } from '../../styles/theme';
import { toneColor, type Tone } from '../dashboard/UiPrimitives';

function toneForEvent(event: ConjunctionEvent): Tone {
  const level = riskLevelForEvent(event);
  if (level === 'red') return 'critical';
  if (level === 'yellow') return 'warning';
  return 'accent';
}

function severityLabel(event: ConjunctionEvent): string {
  if (event.severity) {
    switch (event.severity) {
      case 'critical': return 'Critical';
      case 'warning': return 'Warning';
      case 'watch': return 'Watch';
    }
  }
  const tone = toneForEvent(event);
  return tone === 'critical' ? 'Critical' : tone === 'warning' ? 'Warning' : 'Watch';
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

function formatCountdown(tcaEpochS: number, nowEpochS?: number): string {
  if (typeof nowEpochS !== 'number') return '--';
  const deltaSeconds = Math.round(tcaEpochS - nowEpochS);
  if (Math.abs(deltaSeconds) < 30) return 'NOW';

  const absSeconds = Math.abs(deltaSeconds);
  const hours = Math.floor(absSeconds / 3600);
  const minutes = Math.floor((absSeconds % 3600) / 60);

  if (hours > 0) return `${deltaSeconds >= 0 ? 'T+' : 'T-'}${hours}h ${minutes}m`;
  return `${deltaSeconds >= 0 ? 'T+' : 'T-'}${minutes}m`;
}

export function ConjunctionEventList({
  events,
  activeEvent,
  highlightSatelliteId,
  nowEpochS,
  onSelectEvent,
}: {
  events: ConjunctionEvent[];
  activeEvent: ConjunctionEvent | null;
  highlightSatelliteId?: string | null;
  nowEpochS?: number;
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
        const isHighlightedSatellite = highlightSatelliteId === event.satellite_id;

        return (
          <button
            key={`${event.satellite_id}-${event.debris_id}-${event.tca_epoch_s}`}
            type="button"
            onClick={() => onSelectEvent(event)}
            style={{
              display: 'flex',
              flexDirection: 'column',
              gap: '8px',
              padding: '10px 12px 11px',
              border: `1px solid ${isActive ? `${color}66` : isHighlightedSatellite ? `${theme.colors.primary}3f` : 'rgba(255,255,255,0.06)'}`,
              background: isActive ? 'rgba(255,255,255,0.06)' : isHighlightedSatellite ? 'rgba(88, 184, 255, 0.05)' : 'rgba(255,255,255,0.02)',
              color: theme.colors.text,
              cursor: 'pointer',
              textAlign: 'left',
              clipPath: theme.chamfer.buttonClipPath,
              boxShadow: isHighlightedSatellite && !isActive ? 'inset 0 0 0 1px rgba(88, 184, 255, 0.08)' : 'none',
            }}
          >
            <div style={{ display: 'flex', justifyContent: 'space-between', gap: '12px', alignItems: 'center' }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: '8px', minWidth: 0 }}>
                <span style={{ color, fontSize: '10px', letterSpacing: '0.16em', textTransform: 'uppercase' }}>
                  {severityLabel(event)}
                </span>
                {isHighlightedSatellite && (
                  <span style={{ color: theme.colors.primary, fontSize: '9px', letterSpacing: '0.14em', textTransform: 'uppercase' }}>
                    Focused Vehicle
                  </span>
                )}
              </div>
              <span style={{ color, fontSize: '10px', fontWeight: 700, letterSpacing: '0.04em' }}>
                {formatCountdown(event.tca_epoch_s, nowEpochS)}
              </span>
            </div>

            <div style={{ display: 'flex', justifyContent: 'space-between', gap: '12px', alignItems: 'baseline' }}>
              <span style={{ fontSize: '13px', fontWeight: 700, letterSpacing: '0.03em', color: theme.colors.text }}>
                {event.satellite_id} vs {event.debris_id}
              </span>
              <span style={{ color: theme.colors.textDim, fontSize: '10px' }}>{formatTca(event.tca)} UTC</span>
            </div>

            <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, minmax(0, 1fr))', gap: '6px 10px' }}>
              <span style={{ color, fontSize: '11px', fontWeight: 700 }}>{event.miss_distance_km.toFixed(2)} km miss</span>
              <span style={{ color: theme.colors.textDim, fontSize: '11px' }}>{event.approach_speed_km_s.toFixed(3)} km/s</span>
              <span style={{ color: event.fail_open ? theme.colors.warning : event.predictive ? theme.colors.textMuted : event.collision ? theme.colors.critical : theme.colors.textMuted, fontSize: '11px', textAlign: 'right' }}>
                {event.fail_open ? 'Fail-open' : event.predictive ? 'Predictive 24h' : event.collision ? 'Collision flagged' : 'Historical'}
              </span>
            </div>
          </button>
        );
      })}
    </div>
  );
}
