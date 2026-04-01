import { useEffect, useMemo, useState } from 'react';
import EarthGlobe, { trajectoryToVectors } from '../components/EarthGlobe';
import { ConjunctionBullseye } from '../components/ConjunctionBullseye';
import { GlassPanel } from '../components/GlassPanel';
import SimControls from '../components/SimControls';
import { SatelliteSelectionPlaceholder } from '../components/dashboard/SatelliteFocusControls';
import { ThreatSeverityFilters } from '../components/dashboard/ThreatSeverityFilters';
import {
  buildEncounterQueueGroups,
  countCollapsedEncounterSamples,
  flattenEncounterQueueGroups,
} from '../components/threat/queueModel';
import { useDashboard } from '../dashboard/DashboardContext';
import { theme } from '../styles/theme';
import { HeroMetric, InfoChip, SectionHeader } from '../components/dashboard/UiPrimitives';

function useFrameRateProbe(sampleWindowMs = 1250): number | null {
  const [fps, setFps] = useState<number | null>(null);

  useEffect(() => {
    let frameCount = 0;
    let rafId = 0;
    let windowStart = performance.now();

    const tick = (now: number) => {
      frameCount += 1;
      const elapsed = now - windowStart;
      if (elapsed >= sampleWindowMs) {
        setFps((frameCount * 1000) / elapsed);
        frameCount = 0;
        windowStart = now;
      }
      rafId = window.requestAnimationFrame(tick);
    };

    rafId = window.requestAnimationFrame(tick);
    return () => window.cancelAnimationFrame(rafId);
  }, [sampleWindowMs]);

  return fps;
}

function formatFps(value: number | null): string {
  if (value === null || !Number.isFinite(value) || value <= 0) return '--';
  return `${Math.round(value)} fps`;
}

function fpsTone(value: number | null): 'accent' | 'warning' | 'critical' | 'neutral' {
  if (value === null || !Number.isFinite(value)) return 'neutral';
  if (value >= 50) return 'accent';
  if (value >= 30) return 'warning';
  return 'critical';
}

export function CommandPage({ isNarrow, isCompact }: { isNarrow: boolean; isCompact: boolean }) {
  const { model, selectedSatId, selectSat, threatSeverityFilter } = useDashboard();
  const renderFps = useFrameRateProbe();
  const queueGroups = useMemo(
    () => buildEncounterQueueGroups(model.conjList, selectedSatId),
    [model.conjList, selectedSatId],
  );
  const flatQueueEntries = useMemo(
    () => flattenEncounterQueueGroups(queueGroups),
    [queueGroups],
  );
  const collapsedQueueSamples = useMemo(
    () => countCollapsedEncounterSamples(queueGroups),
    [queueGroups],
  );
  const focusQueueEntries = useMemo(
    () => selectedSatId ? flatQueueEntries.filter(entry => entry.event.satellite_id === selectedSatId) : flatQueueEntries,
    [flatQueueEntries, selectedSatId],
  );

  const bullseyeMaxTcaSeconds = useMemo(() => {
    const events = focusQueueEntries.map(entry => entry.event);
    const maxFutureDt = events.reduce((acc, evt) => {
      const dt = Math.max(0, evt.tca_epoch_s - model.nowEpochS);
      return Math.max(acc, dt);
    }, 0);
    if (maxFutureDt <= 5400) return 5400;
    if (maxFutureDt <= 21600) return 21600;
    return 86400;
  }, [focusQueueEntries, model.nowEpochS]);

  const trailVectors = useMemo(
    () => model.selectedTrajectory?.trail ? trajectoryToVectors(model.selectedTrajectory.trail) : undefined,
    [model.selectedTrajectory?.trail],
  );
  const predictedVectors = useMemo(
    () => model.selectedTrajectory?.predicted ? trajectoryToVectors(model.selectedTrajectory.predicted) : undefined,
    [model.selectedTrajectory?.predicted],
  );

  return (
    <section aria-labelledby="command-heading" style={{ display: 'flex', flexDirection: 'column', gap: '12px', height: '100%' }}>
      <SectionHeader
        kicker="Command Deck"
        title="Mission Theatre"
        description="Global orbital picture on the left, operator actions and live mission context on the right."
        aside={
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: '6px', justifyContent: 'flex-end' }}>
            <InfoChip label="Render" value={formatFps(renderFps)} tone={fpsTone(renderFps)} />
            <InfoChip label="Payload" value={`${model.satellites.length}/${model.debris.length.toLocaleString()}`} tone={model.debris.length >= 1000 ? 'accent' : 'warning'} />
            <InfoChip label="Focus" value={selectedSatId ?? 'Fleet'} tone={selectedSatId ? 'accent' : 'neutral'} />
            <InfoChip label="Threat" value={model.threatValue} tone={model.threatCounts.red > 0 ? 'critical' : model.threatCounts.yellow > 0 ? 'warning' : 'accent'} />
            <InfoChip label="Burn Queue" value={model.burnValue} tone={model.watchedPendingBurns.length > 0 ? 'warning' : 'accent'} />
          </div>
        }
      />

      <div style={{ display: 'grid', gridTemplateColumns: isNarrow ? '1fr' : 'minmax(0, 1.68fr) minmax(300px, 0.88fr)', gap: '14px', flex: 1, minHeight: 0 }}>
        <GlassPanel
          title="3D ORBITAL COMMAND VIEW"
          noPadding
          priority="primary"
          accentColor={theme.colors.primary}
          style={{ minHeight: 0 }}
        >
          <div style={{ display: 'flex', flexDirection: 'column', gap: '10px', flex: 1, minHeight: 0, padding: '10px 14px 14px' }}>
            <div style={{ display: 'flex', flexDirection: isCompact ? 'column' : 'row', justifyContent: 'space-between', alignItems: isCompact ? 'stretch' : 'flex-start', gap: '10px', flexShrink: 0 }}>
              <div style={{ display: 'flex', flexDirection: 'column', gap: '4px', maxWidth: '60ch' }}>
                <span style={{ color: theme.colors.primary, fontSize: '9px', letterSpacing: '0.18em', textTransform: 'uppercase' }}>Live orbital picture</span>
                <h3 id="command-heading" style={{ color: theme.colors.text, fontSize: isCompact ? '20px' : '24px', lineHeight: 1.02, fontWeight: 700, letterSpacing: '-0.03em' }}>
                  Command geometry and route context
                </h3>
              </div>
              <div style={{ display: 'flex', flexWrap: 'wrap', gap: '6px', justifyContent: isCompact ? 'flex-start' : 'flex-end' }}>
                <InfoChip label="Mode" value={selectedSatId ? 'Focused' : 'Fleet'} tone={selectedSatId ? 'primary' : 'neutral'} />
                <InfoChip label="Path" value={selectedSatId ? model.heroPathValue : 'Select Satellite'} tone={selectedSatId && model.trajectory?.satellite_id ? 'primary' : 'neutral'} />
              </div>
            </div>

            <div style={{ flex: 1, minHeight: 0, overflow: 'hidden', clipPath: theme.chamfer.clipPath, border: '1px solid rgba(88, 184, 255, 0.34)', background: 'radial-gradient(circle at 50% 28%, rgba(16, 18, 24, 0.95), rgba(6, 7, 10, 0.98))', boxShadow: 'inset 0 0 36px rgba(0, 0, 0, 0.52), 0 0 28px rgba(88, 184, 255, 0.08)' }}>
              <EarthGlobe
                satellites={model.satellites}
                debris={model.debris}
                selectedSatId={selectedSatId}
                onSelectSat={selectSat}
                trailPoints={trailVectors}
                predictedPoints={predictedVectors}
                style={{ position: 'relative', width: '100%', height: '100%' }}
              />
            </div>
          </div>
        </GlassPanel>

        <div style={{ display: 'flex', flexDirection: 'column', gap: '14px', minHeight: 0 }}>
          <GlassPanel
            title="MISSION RAIL"
            noPadding
            priority="secondary"
            accentColor={theme.colors.accent}
            style={{ flex: 1, minHeight: 0 }}
          >
            <div style={{ display: 'flex', flexDirection: 'column', gap: '12px', flex: 1, minHeight: 0, padding: '10px 14px 14px', overflow: 'auto' }}>
              <div style={{ display: 'flex', flexDirection: 'column', gap: '4px' }}>
                <span style={{ color: theme.colors.textMuted, fontSize: '9px', letterSpacing: '0.16em', textTransform: 'uppercase' }}>Context Target</span>
                <div style={{ padding: '10px 12px', border: `1px solid ${theme.colors.border}`, background: 'rgba(9, 11, 14, 0.72)', clipPath: theme.chamfer.buttonClipPath }}>
                  <strong style={{ display: 'block', color: selectedSatId ? theme.colors.primary : theme.colors.text, fontSize: '14px', lineHeight: 1.1 }}>{selectedSatId ?? 'Fleet Overview'}</strong>
                  <span style={{ display: 'block', marginTop: '4px', color: theme.colors.textDim, fontSize: '10px', lineHeight: 1.5 }}>
                    Use the single global focus console above to pin one spacecraft across the command theatre and mission rail.
                  </span>
                </div>
              </div>

              <div style={{ display: 'grid', gridTemplateColumns: '1fr', gap: '10px', paddingTop: '6px', borderTop: `1px solid ${theme.colors.border}` }}>
                <HeroMetric label="Threat Watch" value={model.threatValue} detail={model.threatDetail} tone={model.threatCounts.red > 0 ? 'critical' : model.threatCounts.yellow > 0 ? 'warning' : 'accent'} />
                <HeroMetric label="Burn Window" value={selectedSatId ? model.burnValue : 'Select Satellite'} detail={selectedSatId ? model.burnDetail : 'Select a satellite to inspect its next burn window, pending queue, and timing context.'} tone={selectedSatId ? (model.watchedPendingBurns.length > 0 ? 'warning' : 'primary') : 'neutral'} />
                <HeroMetric label="Slot Integrity" value={model.opsHealthValue} detail={model.opsHealthDetail} tone={model.opsHealthWarn ? 'critical' : 'accent'} />
                <HeroMetric label="Fuel Posture" value={model.resourceValue} detail={model.resourceDetail} tone={model.lowestFuelSatellite && model.lowestFuelSatellite.fuel_kg < 10 ? 'critical' : 'warning'} />
              </div>

              <div style={{ marginTop: 'auto', display: 'flex', flexDirection: 'column', gap: '8px', paddingTop: '8px', borderTop: `1px solid ${theme.colors.border}` }}>
                <SimControls />
              </div>
            </div>
          </GlassPanel>

          <GlassPanel
            title="THREAT SNAPSHOT"
            noPadding
            priority="secondary"
            accentColor={theme.colors.primary}
            style={{ flex: 1, minHeight: 0 }}
          >
            <div style={{ display: 'flex', flexDirection: 'column', gap: '8px', flex: 1, minHeight: 0, padding: '10px 12px 12px' }}>
              <ThreatSeverityFilters counts={model.threatCounts} />
              <div style={{ display: 'flex', flexWrap: 'wrap', gap: '6px' }}>
                <InfoChip label="Vehicle Lanes" value={queueGroups.length.toString()} tone={queueGroups.length > 0 ? 'accent' : 'neutral'} />
                <InfoChip label="Queue" value={focusQueueEntries.length.toString()} tone={focusQueueEntries.length > 0 ? 'accent' : 'warning'} />
                <InfoChip label="Collapsed" value={collapsedQueueSamples.toString()} tone={collapsedQueueSamples > 0 ? 'accent' : 'neutral'} />
              </div>
              {selectedSatId ? (
                <div style={{ flex: 1, minHeight: 0, overflow: 'hidden', clipPath: theme.chamfer.clipPath, border: `1px solid ${theme.colors.border}`, background: 'linear-gradient(180deg, rgba(10, 11, 14, 0.92), rgba(7, 8, 10, 0.98))' }}>
                  <ConjunctionBullseye conjunctions={focusQueueEntries.map(entry => entry.event)} selectedSatId={selectedSatId} nowEpochS={model.nowEpochS} maxTcaSeconds={bullseyeMaxTcaSeconds} severityFilter={threatSeverityFilter} />
                </div>
              ) : (
                <SatelliteSelectionPlaceholder
                  title="Satellite Focus Required"
                  detail="Select a satellite to inspect its conjunction bullseye, local threat geometry, and watch-lane encounter stack."
                  tone="primary"
                />
              )}
            </div>
          </GlassPanel>
        </div>
      </div>
    </section>
  );
}
