import { GlassPanel } from '../components/GlassPanel';
import { GroundTrackMap } from '../components/GroundTrackMap';
import { useDashboard } from '../dashboard/DashboardContext';
import { theme } from '../styles/theme';
import { DetailList, InfoChip, SectionHeader, SummaryCard } from '../components/dashboard/UiPrimitives';

function minutesSince(timestampMs: number | null): string {
  if (timestampMs === null) return '--';
  const deltaMs = Date.now() - timestampMs;
  const seconds = Math.max(0, Math.round(deltaMs / 1000));
  if (seconds < 60) return `${seconds}s ago`;
  return `${Math.round(seconds / 60)}m ago`;
}

function spanMinutesFromEpochs(points: Array<[number, number, number]>): number {
  if (points.length < 2) return 0;
  return Math.max(0, (points[points.length - 1][0] - points[0][0]) / 60);
}

function spanMinutesFromTrajectory(points: Array<{ epoch_s: number }>): number {
  if (points.length < 2) return 0;
  return Math.max(0, (points[points.length - 1].epoch_s - points[0].epoch_s) / 60);
}

function formatMinutesValue(minutes: number, fallback = 'Standby'): string {
  if (!Number.isFinite(minutes) || minutes <= 0) return fallback;
  return `${Math.round(minutes)} min`;
}

function formatUtcClock(value?: string | null): string {
  if (!value) return '--';
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return '--';
  return `${date.toISOString().slice(11, 19)} UTC`;
}

export function TrackPage({ isNarrow, isCompact }: { isNarrow: boolean; isCompact: boolean }) {
  const { model, selectedSatId, selectSat } = useDashboard();
  const focusTrackId = model.trajectory?.satellite_id ?? selectedSatId ?? model.activeSatellite?.id ?? null;
  const focusedHistory = focusTrackId ? (model.trackHistory.get(focusTrackId) ?? []) : [];
  const historyCoverageMinutes = spanMinutesFromEpochs(focusedHistory);
  const trailCoverageMinutes = spanMinutesFromTrajectory(model.trajectory?.trail ?? []);
  const forecastCoverageMinutes = spanMinutesFromTrajectory(model.trajectory?.predicted ?? []);

  const proofCards = [
    <SummaryCard
      key="trail"
      label="Historical Trail"
      value={formatMinutesValue(trailCoverageMinutes, 'Awaiting focus')}
      detail={`${model.trajectory?.trail.length ?? 0} plotted trail points with 90-minute retention intent`}
      tone={trailCoverageMinutes >= 80 ? 'accent' : model.trajectory?.trail.length ? 'warning' : 'neutral'}
    />,
    <SummaryCard
      key="forecast"
      label="Predicted Path"
      value={formatMinutesValue(forecastCoverageMinutes, 'Awaiting focus')}
      detail={`${model.trajectory?.predicted.length ?? 0} projected points rendered as dashed forecast track`}
      tone={forecastCoverageMinutes >= 80 ? 'warning' : model.trajectory?.predicted.length ? 'primary' : 'neutral'}
    />,
    <SummaryCard
      key="terminator"
      label="Terminator"
      value={model.snapshot ? 'LIVE' : 'Standby'}
      detail={model.snapshot ? `Solar day-night boundary from ${formatUtcClock(model.snapshot.timestamp)}` : 'Terminator overlay will lock to the next snapshot epoch'}
      tone={model.snapshot ? 'primary' : 'neutral'}
    />,
    <SummaryCard
      key="cache"
      label="Track Cache"
      value={`${model.trackHistory.size} lanes`}
      detail={`${focusedHistory.length} retained history points for ${focusTrackId ?? 'the active focus lane'}`}
      tone={focusedHistory.length > 1 ? 'accent' : model.trackHistory.size > 0 ? 'warning' : 'neutral'}
    />,
  ];

  return (
    <section aria-labelledby="track-heading" style={{ display: 'flex', flexDirection: 'column', gap: '12px', height: '100%' }}>
      <SectionHeader
        kicker="Track Deck"
        title="Ground Track Operations"
        description="Full tactical ground track on the left, focused spacecraft details and path state on the right."
        aside={
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: '6px', justifyContent: 'flex-end' }}>
            <InfoChip label="Mode" value={selectedSatId ?? 'Fleet'} tone={selectedSatId ? 'accent' : 'primary'} />
            <InfoChip label="Objects" value={model.satellites.length.toLocaleString()} tone="accent" />
            <InfoChip label="Debris" value={model.debris.length.toLocaleString()} tone="warning" />
          </div>
        }
      />

      <div style={{ display: 'grid', gridTemplateColumns: isNarrow ? '1fr' : 'minmax(0, 1.8fr) minmax(300px, 0.72fr)', gap: '14px', flex: 1, minHeight: 0 }}>
        <GlassPanel
          title="GROUND TRACK OPERATIONS"
          noPadding
          priority="primary"
          accentColor={theme.colors.primary}
          style={{ minHeight: 0 }}
        >
          <div style={{ display: 'flex', flexDirection: 'column', gap: '10px', flex: 1, minHeight: 0, padding: '10px 14px 14px' }}>
            <div style={{ display: 'flex', flexDirection: isCompact ? 'column' : 'row', justifyContent: 'space-between', alignItems: isCompact ? 'stretch' : 'flex-start', gap: '10px', flexShrink: 0 }}>
              <div style={{ display: 'flex', flexDirection: 'column', gap: '4px', maxWidth: '74ch' }}>
                <span style={{ color: theme.colors.primary, fontSize: '9px', letterSpacing: '0.16em', textTransform: 'uppercase', opacity: 0.95 }}>2D tactical track</span>
                <p id="track-heading" style={{ color: theme.colors.textDim, fontSize: '12px', lineHeight: 1.55 }}>
                  Real-time constellation markers, last 90 minutes of trail, next 90 minutes of predicted path, and the live terminator overlay.
                </p>
              </div>
              <div style={{ display: 'flex', flexWrap: 'wrap', gap: '6px', justifyContent: isCompact ? 'flex-start' : 'flex-end' }}>
                <InfoChip label="Freshness" value={minutesSince(model.snapshotUpdatedAtMs)} tone={model.snapshot ? 'accent' : 'warning'} />
                <InfoChip label="Watch" value={model.watchTargetValue} tone={selectedSatId ? 'accent' : 'neutral'} />
              </div>
            </div>

            <div style={{ width: '100%', flex: 1, minHeight: 0, overflow: 'hidden', clipPath: theme.chamfer.clipPath, border: '1px solid rgba(88, 184, 255, 0.34)', background: 'linear-gradient(180deg, rgba(11, 13, 17, 0.92), rgba(7, 8, 10, 0.98))', boxShadow: 'inset 0 0 32px rgba(0, 0, 0, 0.62), 0 0 24px rgba(88, 184, 255, 0.06)' }}>
              <GroundTrackMap
                snapshot={model.snapshot}
                selectedSatId={selectedSatId}
                onSelectSat={selectSat}
                trackHistory={model.trackHistory}
                trackVersion={model.trackVersion}
                trajectory={model.trajectory}
              />
            </div>
          </div>
        </GlassPanel>

        <GlassPanel
          title="TRACK DETAIL RAIL"
          noPadding
          priority="secondary"
          accentColor={theme.colors.accent}
          style={{ minHeight: 0 }}
        >
          <div style={{ display: 'flex', flexDirection: 'column', gap: '10px', flex: 1, minHeight: 0, padding: '10px 14px 14px', overflow: 'auto' }}>
            <div style={{ display: 'flex', flexDirection: 'column', gap: '4px' }}>
              <span style={{ color: theme.colors.textMuted, fontSize: '9px', letterSpacing: '0.16em', textTransform: 'uppercase' }}>Selection State</span>
              <div style={{ display: 'flex', flexWrap: 'wrap', alignItems: 'center', gap: '8px' }}>
                <span style={{ color: theme.colors.text, fontSize: '20px', fontWeight: 700, lineHeight: 1.05 }}>{selectedSatId ?? 'Fleet Overview'}</span>
                {selectedSatId && (
                  <button
                    type="button"
                    onClick={() => selectSat(null)}
                    style={{
                      fontSize: '9px',
                      fontFamily: theme.font.mono,
                      padding: '4px 8px',
                      border: `1px solid ${theme.colors.border}`,
                      background: 'rgba(10, 11, 14, 0.88)',
                      color: theme.colors.text,
                      cursor: 'pointer',
                      letterSpacing: '0.12em',
                      clipPath: theme.chamfer.buttonClipPath,
                    }}
                  >
                    CLEAR
                  </button>
                )}
              </div>
              <span style={{ color: theme.colors.textDim, fontSize: '10px', lineHeight: 1.5 }}>
                {model.activeSatellite
                  ? `${model.activeSatellite.status} / ${model.activeSatellite.fuel_kg.toFixed(1)} kg fuel`
                  : 'Fleet-wide track mode. Select a vehicle on the map to inspect.'}
              </span>
            </div>

            <div style={{ display: 'flex', flexWrap: 'wrap', gap: '6px' }}>
              <InfoChip label="Mission Time" value={model.missionValue} tone="primary" style={{ minWidth: '100%' }} />
              <InfoChip label="Threat" value={model.threatValue} tone={model.threatCounts.red > 0 ? 'critical' : model.threatCounts.yellow > 0 ? 'warning' : 'accent'} />
              <InfoChip label="Burn Queue" value={model.burnValue} tone={model.watchedPendingBurns.length > 0 ? 'warning' : 'accent'} />
            </div>

            <div style={{ display: 'grid', gridTemplateColumns: 'repeat(2, minmax(0, 1fr))', gap: '8px' }}>
              {proofCards}
            </div>

            <DetailList
              entries={[
                { label: 'Snapshot', value: model.snapshot?.timestamp?.replace('T', ' ').replace('Z', ' UTC') ?? 'Link pending', tone: 'primary' },
                { label: 'Update Age', value: minutesSince(model.snapshotUpdatedAtMs), tone: model.snapshot ? 'accent' : 'warning' },
                { label: 'Track Mode', value: selectedSatId ? 'Focused vehicle' : 'Fleet overview', tone: selectedSatId ? 'accent' : 'neutral' },
                { label: 'Path Focus', value: model.trajectory?.satellite_id ?? 'Standby', tone: model.trajectory?.satellite_id ? 'primary' : 'neutral' },
                { label: 'Trail Proof', value: `${formatMinutesValue(trailCoverageMinutes)} / cache ${formatMinutesValue(historyCoverageMinutes)}`, tone: trailCoverageMinutes >= 80 ? 'accent' : 'warning' },
                { label: 'Forecast Proof', value: formatMinutesValue(forecastCoverageMinutes), tone: forecastCoverageMinutes >= 80 ? 'warning' : 'neutral' },
                { label: 'Terminator Epoch', value: formatUtcClock(model.snapshot?.timestamp), tone: model.snapshot ? 'primary' : 'neutral' },
                { label: 'Objects', value: model.satellites.length.toLocaleString(), tone: 'accent' },
                { label: 'Debris', value: model.debris.length.toLocaleString(), tone: 'warning' },
              ]}
            />
          </div>
        </GlassPanel>
      </div>
    </section>
  );
}
