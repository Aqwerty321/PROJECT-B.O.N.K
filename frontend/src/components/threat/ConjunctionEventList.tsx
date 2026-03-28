import type { ConjunctionEvent } from '../../types/api';
import { riskLevelForEvent } from '../../types/api';
import { theme } from '../../styles/theme';
import { toneColor, type Tone } from '../dashboard/UiPrimitives';
import type { ConjunctionQueueEntry, ConjunctionQueueGroup } from './queueModel';

function toneForEvent(event: ConjunctionEvent): Tone {
  const level = riskLevelForEvent(event);
  if (level === 'red') return 'critical';
  if (level === 'yellow') return 'warning';
  return 'accent';
}

function toneForGroup(group: ConjunctionQueueGroup): Tone {
  const level = group.entries.reduce<Tone>((current, entry) => {
    const tone = toneForEvent(entry.event);
    if (tone === 'critical') return 'critical';
    if (tone === 'warning' && current !== 'critical') return 'warning';
    return current;
  }, 'accent');
  return level;
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
  groups,
  activeEventKey,
  nowEpochS,
  onSelectEvent,
}: {
  groups: ConjunctionQueueGroup[];
  activeEventKey: string | null;
  nowEpochS?: number;
  onSelectEvent: (entry: ConjunctionQueueEntry) => void;
}) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: '8px', minHeight: 0, overflowY: 'auto', paddingRight: '4px' }}>
      {groups.map(group => {
        const groupTone = toneForGroup(group);
        const groupColor = toneColor(groupTone);

        return (
          <section
            key={group.satelliteId}
            style={{ display: 'flex', flexDirection: 'column', gap: '8px' }}
          >
            <div
              style={{
                display: 'flex',
                justifyContent: 'space-between',
                gap: '12px',
                alignItems: 'flex-start',
                padding: '9px 10px',
                border: `1px solid ${group.isFocused ? `${theme.colors.primary}44` : `${groupColor}22`}`,
                background: group.isFocused ? 'rgba(88, 184, 255, 0.08)' : 'rgba(255,255,255,0.03)',
                clipPath: theme.chamfer.buttonClipPath,
              }}
            >
              <div style={{ display: 'flex', flexDirection: 'column', gap: '4px', minWidth: 0 }}>
                <span style={{ color: group.isFocused ? theme.colors.primary : theme.colors.textMuted, fontSize: '9px', letterSpacing: '0.14em', textTransform: 'uppercase' }}>
                  {group.isFocused ? 'Focused Vehicle' : 'Vehicle Lane'}
                </span>
                <span style={{ color: theme.colors.text, fontSize: '13px', fontWeight: 700, letterSpacing: '0.04em' }}>
                  {group.satelliteId}
                </span>
              </div>

              <div style={{ display: 'flex', flexDirection: 'column', gap: '3px', textAlign: 'right', flexShrink: 0 }}>
                <span style={{ color: groupColor, fontSize: '11px', fontWeight: 700 }}>
                  {group.entries.length} encounters
                </span>
                <span style={{ color: theme.colors.textDim, fontSize: '10px' }}>
                  {group.distinctDebrisCount} debris / {group.nextTcaEpochS !== null ? `Next ${formatCountdown(group.nextTcaEpochS, nowEpochS)}` : 'No next TCA'}
                </span>
                {group.collapsedSamples > 0 && (
                  <span style={{ color: theme.colors.textMuted, fontSize: '10px' }}>
                    {group.collapsedSamples} repeated samples collapsed
                  </span>
                )}
              </div>
            </div>

            <div
              style={{
                display: 'flex',
                flexDirection: 'column',
                gap: '8px',
                paddingLeft: '10px',
                borderLeft: `1px solid ${group.isFocused ? `${theme.colors.primary}33` : 'rgba(255,255,255,0.08)'}`,
              }}
            >
              {group.entries.map(entry => {
                const { event } = entry;
                const tone = toneForEvent(event);
                const color = toneColor(tone);
                const isActive = activeEventKey === entry.key;
                const streamLabel = event.fail_open
                  ? 'Fail-open'
                  : event.predictive
                    ? 'Predictive 24h'
                    : entry.sampleCount > 1
                      ? `History x${entry.sampleCount}`
                      : event.collision
                        ? 'Collision flagged'
                        : 'Historical';

                return (
                  <button
                    key={entry.key}
                    type="button"
                    onClick={() => onSelectEvent(entry)}
                    style={{
                      display: 'flex',
                      flexDirection: 'column',
                      gap: '8px',
                      padding: '10px 12px 11px',
                      border: `1px solid ${isActive ? `${color}66` : group.isFocused ? `${theme.colors.primary}33` : 'rgba(255,255,255,0.06)'}`,
                      background: isActive ? 'rgba(255,255,255,0.06)' : group.isFocused ? 'rgba(88, 184, 255, 0.04)' : 'rgba(255,255,255,0.02)',
                      color: theme.colors.text,
                      cursor: 'pointer',
                      textAlign: 'left',
                      clipPath: theme.chamfer.buttonClipPath,
                      boxShadow: group.isFocused && !isActive ? 'inset 0 0 0 1px rgba(88, 184, 255, 0.06)' : 'none',
                    }}
                  >
                    <div style={{ display: 'flex', justifyContent: 'space-between', gap: '12px', alignItems: 'center' }}>
                      <div style={{ display: 'flex', alignItems: 'center', gap: '8px', minWidth: 0, flexWrap: 'wrap' }}>
                        <span style={{ color, fontSize: '10px', letterSpacing: '0.16em', textTransform: 'uppercase' }}>
                          {severityLabel(event)}
                        </span>
                        {entry.sampleCount > 1 && !event.predictive && (
                          <span style={{ color: theme.colors.textMuted, fontSize: '9px', letterSpacing: '0.12em', textTransform: 'uppercase' }}>
                            {entry.sampleCount} samples
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
                        {streamLabel}
                      </span>
                    </div>
                  </button>
                );
              })}
            </div>
          </section>
        );
      })}
    </div>
  );
}
