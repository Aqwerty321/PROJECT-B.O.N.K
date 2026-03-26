import { useMemo } from 'react';
import EarthGlobe, { trajectoryToVectors } from '../components/EarthGlobe';
import { ConjunctionBullseye } from '../components/ConjunctionBullseye';
import { GlassPanel } from '../components/GlassPanel';
import SimControls from '../components/SimControls';
import { useDashboard } from '../dashboard/DashboardContext';
import { theme } from '../styles/theme';
import { HeroMetric, InfoChip, SectionHeader } from '../components/dashboard/UiPrimitives';

export function CommandPage({ isNarrow, isCompact }: { isNarrow: boolean; isCompact: boolean }) {
  const { model, selectedSatId, selectSat } = useDashboard();

  const bullseyeMaxTcaSeconds = useMemo(() => {
    const events = selectedSatId
      ? model.conjList.filter(c => c.satellite_id === selectedSatId)
      : model.conjList;
    const maxFutureDt = events.reduce((acc, evt) => {
      const dt = Math.max(0, evt.tca_epoch_s - model.nowEpochS);
      return Math.max(acc, dt);
    }, 0);
    if (maxFutureDt <= 5400) return 5400;
    if (maxFutureDt <= 21600) return 21600;
    return 86400;
  }, [model.conjList, model.nowEpochS, selectedSatId]);

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
            <InfoChip label="Watch" value={model.watchTargetValue} tone={selectedSatId ? 'accent' : 'neutral'} />
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
                <InfoChip label="Mode" value={model.heroModeValue} tone={selectedSatId ? 'accent' : 'primary'} />
                <InfoChip label="Path" value={model.heroPathValue} tone={model.trajectory?.satellite_id ? 'primary' : 'neutral'} />
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
                <div style={{ display: 'flex', flexWrap: 'wrap', alignItems: 'center', gap: '8px' }}>
                  <span style={{ color: theme.colors.text, fontSize: '20px', fontWeight: 700, lineHeight: 1.05, letterSpacing: '-0.03em', textWrap: 'balance' }}>
                    {selectedSatId ?? 'Fleet Overview'}
                  </span>
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
              </div>

              <div style={{ display: 'grid', gridTemplateColumns: '1fr', gap: '10px', paddingTop: '6px', borderTop: `1px solid ${theme.colors.border}` }}>
                <HeroMetric label="Threat Watch" value={model.threatValue} detail={model.threatDetail} tone={model.threatCounts.red > 0 ? 'critical' : model.threatCounts.yellow > 0 ? 'warning' : 'accent'} />
                <HeroMetric label="Burn Window" value={model.burnValue} detail={model.burnDetail} tone={model.watchedPendingBurns.length > 0 ? 'warning' : 'primary'} />
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
              <div style={{ display: 'flex', flexWrap: 'wrap', gap: '6px', justifyContent: 'flex-start', flexShrink: 0 }}>
                <InfoChip label="Critical" value={model.threatCounts.red.toString()} tone="critical" />
                <InfoChip label="Warning" value={model.threatCounts.yellow.toString()} tone="warning" />
                <InfoChip label="Nominal" value={model.threatCounts.green.toString()} tone="accent" />
              </div>
              <div style={{ flex: 1, minHeight: 0, overflow: 'hidden', clipPath: theme.chamfer.clipPath, border: `1px solid ${theme.colors.border}`, background: 'linear-gradient(180deg, rgba(10, 11, 14, 0.92), rgba(7, 8, 10, 0.98))' }}>
                <ConjunctionBullseye conjunctions={model.conjList} selectedSatId={selectedSatId} nowEpochS={model.nowEpochS} maxTcaSeconds={bullseyeMaxTcaSeconds} />
              </div>
            </div>
          </GlassPanel>
        </div>
      </div>
    </section>
  );
}
