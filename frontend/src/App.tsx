import { useState, useCallback, useMemo, useEffect, useRef, type CSSProperties } from 'react';
import { theme } from './styles/theme';
import {
  useSnapshot,
  useStatus,
  useBurns,
  useConjunctions,
  useTrajectory,
} from './hooks/useApi';
import type { ConjunctionEvent, PendingBurn, SatelliteSnapshot } from './types/api';
import { riskLevelFromDistance } from './types/api';
import BootSequence from './components/BootSequence';
import EarthGlobe, { trajectoryToVectors } from './components/EarthGlobe';
import { GlassPanel } from './components/GlassPanel';
import { ConjunctionBullseye } from './components/ConjunctionBullseye';
import { StatusPanel } from './components/StatusPanel';
import { FuelHeatmap } from './components/FuelHeatmap';
import ManeuverGantt from './components/ManeuverGantt';
import SimControls from './components/SimControls';
import { GroundTrackMap } from './components/GroundTrackMap';

type Tone = 'primary' | 'accent' | 'warning' | 'critical' | 'neutral';

function toneColor(tone: Tone): string {
  switch (tone) {
    case 'primary':
      return theme.colors.primary;
    case 'accent':
      return theme.colors.accent;
    case 'warning':
      return theme.colors.warning;
    case 'critical':
      return theme.colors.critical;
    default:
      return theme.colors.textDim;
  }
}

function formatWithFormatter(formatter: Intl.DateTimeFormat, value: string): string {
  const date = new Date(value);
  return Number.isNaN(date.getTime()) ? '--' : formatter.format(date);
}

function findNextThreat(events: ConjunctionEvent[], nowEpochS: number): ConjunctionEvent | null {
  const future = [...events]
    .filter(event => event.tca_epoch_s >= nowEpochS - 300)
    .sort((a, b) => a.tca_epoch_s - b.tca_epoch_s);
  return future[0] ?? null;
}

function findNextPendingBurn(burns: PendingBurn[]): PendingBurn | null {
  const upcoming = [...burns].sort(
    (a, b) => new Date(a.burn_epoch).getTime() - new Date(b.burn_epoch).getTime(),
  );
  return upcoming[0] ?? null;
}

function findLowestFuelSatellite(satellites: SatelliteSnapshot[]): SatelliteSnapshot | null {
  return satellites.reduce<SatelliteSnapshot | null>((lowest, satellite) => {
    if (!lowest || satellite.fuel_kg < lowest.fuel_kg) {
      return satellite;
    }
    return lowest;
  }, null);
}

function SummaryCard({
  label,
  value,
  detail,
  tone = 'neutral',
}: {
  label: string;
  value: string;
  detail: string;
  tone?: Tone;
}) {
  const color = toneColor(tone);

  return (
    <div
      style={{
        background: 'linear-gradient(180deg, rgba(8, 15, 30, 0.78), rgba(4, 10, 20, 0.64))',
        border: `1px solid ${tone === 'neutral' ? theme.colors.border : `${color}55`}`,
        boxShadow: `0 0 16px ${tone === 'neutral' ? 'rgba(58, 159, 232, 0.07)' : `${color}16`}`,
        clipPath: theme.chamfer.clipPath,
        padding: '10px 12px',
        minHeight: '76px',
        display: 'flex',
        flexDirection: 'column',
        justifyContent: 'space-between',
        gap: '6px',
      }}
    >
      <span
        style={{
          color: theme.colors.textMuted,
          fontSize: '9px',
          letterSpacing: '0.16em',
          textTransform: 'uppercase',
        }}
      >
        {label}
      </span>
      <span
        style={{
          color,
          fontSize: '20px',
          fontWeight: 700,
          letterSpacing: '-0.01em',
          fontVariantNumeric: 'tabular-nums',
          textWrap: 'balance',
        }}
      >
        {value}
      </span>
      <span
        style={{
          color: theme.colors.textDim,
          fontSize: '9px',
          lineHeight: 1.45,
        }}
      >
        {detail}
      </span>
    </div>
  );
}

function InfoChip({
  label,
  value,
  tone = 'neutral',
}: {
  label: string;
  value: string;
  tone?: Tone;
}) {
  const color = toneColor(tone);

  return (
    <div
      style={{
        display: 'inline-flex',
        flexDirection: 'column',
        gap: '3px',
        minWidth: '82px',
        padding: '6px 9px',
        border: `1px solid ${tone === 'neutral' ? theme.colors.border : `${color}55`}`,
        background: 'rgba(5, 10, 20, 0.5)',
        clipPath: theme.chamfer.buttonClipPath,
      }}
    >
      <span
        style={{
          color: theme.colors.textMuted,
          fontSize: '8px',
          letterSpacing: '0.14em',
          textTransform: 'uppercase',
        }}
      >
        {label}
      </span>
      <span
        style={{
          color: tone === 'neutral' ? theme.colors.text : color,
          fontSize: '12px',
          fontWeight: 600,
          fontVariantNumeric: 'tabular-nums',
        }}
      >
        {value}
      </span>
    </div>
  );
}

function App() {
  const [booted, setBooted] = useState(false);
  const [selectedSatId, setSelectedSatId] = useState<string | null>(null);
  const [viewportWidth, setViewportWidth] = useState(() => (
    typeof window === 'undefined' ? 1440 : window.innerWidth
  ));

  const handleBootComplete = useCallback(() => setBooted(true), []);
  const onSelectSat = useCallback((id: string | null) => setSelectedSatId(id), []);

  const { snapshot, error: snapError } = useSnapshot(booted ? 1000 : 60000);
  const { status, error: statusError } = useStatus(booted ? 2000 : 60000);
  const { burns } = useBurns(booted ? 2000 : 60000);
  const { conjunctions } = useConjunctions(booted ? 2000 : 60000, selectedSatId ?? undefined);

  const satellites = snapshot?.satellites ?? [];
  const debris = snapshot?.debris_cloud ?? [];
  const conjList = conjunctions?.conjunctions ?? [];
  const executedBurns = burns?.executed ?? [];
  const pendingBurns = burns?.pending ?? [];
  const trajectoryFocusId = selectedSatId ?? satellites[0]?.id ?? null;
  const { trajectory } = useTrajectory(trajectoryFocusId, booted ? 2000 : 60000);

  const nowEpochS = useMemo(() => {
    if (snapshot?.timestamp) {
      return new Date(snapshot.timestamp).getTime() / 1000;
    }
    return Date.now() / 1000;
  }, [snapshot?.timestamp]);

  const selectedTrajectory = selectedSatId && trajectory?.satellite_id === selectedSatId
    ? trajectory
    : null;

  const trailVectors = useMemo(
    () => selectedTrajectory?.trail ? trajectoryToVectors(selectedTrajectory.trail) : undefined,
    [selectedTrajectory?.trail],
  );
  const predictedVectors = useMemo(
    () => selectedTrajectory?.predicted ? trajectoryToVectors(selectedTrajectory.predicted) : undefined,
    [selectedTrajectory?.predicted],
  );

  const trackHistoryRef = useRef<Map<string, [number, number][]>>(new Map());
  const [trackVersion, setTrackVersion] = useState(0);

  useEffect(() => {
    const onResize = () => setViewportWidth(window.innerWidth);
    window.addEventListener('resize', onResize);
    return () => window.removeEventListener('resize', onResize);
  }, []);

  useEffect(() => {
    if (!snapshot) return;

    const map = trackHistoryRef.current;
    for (const satellite of snapshot.satellites) {
      let history = map.get(satellite.id);
      if (!history) {
        history = [];
        map.set(satellite.id, history);
      }
      history.push([satellite.lat, satellite.lon]);
      if (history.length > 200) {
        history.shift();
      }
    }
    setTrackVersion(version => version + 1);
  }, [snapshot]);

  useEffect(() => {
    if (!selectedSatId) return;
    if (satellites.some(satellite => satellite.id === selectedSatId)) return;
    setSelectedSatId(null);
  }, [satellites, selectedSatId]);

  const activeSatellite = useMemo(
    () => satellites.find(satellite => satellite.id === selectedSatId) ?? null,
    [satellites, selectedSatId],
  );

  const watchedPendingBurns = useMemo(
    () => selectedSatId
      ? pendingBurns.filter(burn => burn.satellite_id === selectedSatId)
      : pendingBurns,
    [pendingBurns, selectedSatId],
  );

  const watchedExecutedBurns = useMemo(
    () => selectedSatId
      ? executedBurns.filter(burn => burn.satellite_id === selectedSatId)
      : executedBurns,
    [executedBurns, selectedSatId],
  );

  const avgFuelKg = useMemo(() => {
    if (satellites.length === 0) return 0;
    const totalFuel = satellites.reduce((sum, satellite) => sum + satellite.fuel_kg, 0);
    return totalFuel / satellites.length;
  }, [satellites]);

  const statusCounts = useMemo(() => {
    return satellites.reduce(
      (counts, satellite) => {
        const key = satellite.status.toUpperCase();
        if (key === 'NOMINAL') counts.nominal += 1;
        else if (key === 'MANEUVERING') counts.maneuvering += 1;
        else if (key === 'DEGRADED') counts.degraded += 1;
        else if (key === 'GRAVEYARD') counts.graveyard += 1;
        return counts;
      },
      { nominal: 0, maneuvering: 0, degraded: 0, graveyard: 0 },
    );
  }, [satellites]);

  const lowestFuelSatellite = useMemo(
    () => findLowestFuelSatellite(satellites),
    [satellites],
  );

  const threatCounts = useMemo(() => {
    return conjList.reduce(
      (counts, event) => {
        const level = riskLevelFromDistance(event.miss_distance_km);
        counts[level] += 1;
        return counts;
      },
      { red: 0, yellow: 0, green: 0 },
    );
  }, [conjList]);

  const nextThreat = useMemo(
    () => findNextThreat(conjList, nowEpochS),
    [conjList, nowEpochS],
  );

  const nextPendingBurn = useMemo(
    () => findNextPendingBurn(watchedPendingBurns),
    [watchedPendingBurns],
  );

  const missionFormatter = useMemo(() => new Intl.DateTimeFormat(undefined, {
    month: 'short',
    day: 'numeric',
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
    hour12: false,
    timeZone: 'UTC',
  }), []);

  const shortTimeFormatter = useMemo(() => new Intl.DateTimeFormat(undefined, {
    hour: '2-digit',
    minute: '2-digit',
    hour12: false,
    timeZone: 'UTC',
  }), []);

  const isStacked = viewportWidth < 1240;
  const isCompact = viewportWidth < 760;
  const styles = buildStyles(isStacked, isCompact);

  if (!booted) {
    return <BootSequence onComplete={handleBootComplete} />;
  }

  const watchTargetValue = selectedSatId ?? 'Fleet Overview';
  const watchTargetDetail = activeSatellite
    ? `${activeSatellite.status} / ${activeSatellite.fuel_kg.toFixed(1)} kg fuel remaining`
    : `${statusCounts.nominal} nominal / ${statusCounts.maneuvering} maneuvering / ${statusCounts.degraded} degraded`;

  const threatValue = `${threatCounts.red} critical / ${threatCounts.yellow} watch`;
  const threatDetail = nextThreat
    ? `Next TCA ${formatWithFormatter(shortTimeFormatter, nextThreat.tca)} UTC at ${nextThreat.miss_distance_km.toFixed(2)} km`
    : 'No active conjunction warnings in the current watch list';

  const burnValue = `${watchedPendingBurns.length} pending / ${watchedExecutedBurns.length} logged`;
  const burnDetail = nextPendingBurn
    ? `Next burn ${formatWithFormatter(shortTimeFormatter, nextPendingBurn.burn_epoch)} UTC`
    : 'No pending burns scheduled for the current view';

  const watchStateValue = selectedSatId ?? 'Fleet';
  const watchStateTone: Tone = selectedSatId ? 'accent' : 'primary';

  const resourceValue = satellites.length > 0 ? `${avgFuelKg.toFixed(1)} kg avg` : 'Awaiting telemetry';
  const resourceDetail = lowestFuelSatellite
    ? `Lowest fuel ${lowestFuelSatellite.id} at ${lowestFuelSatellite.fuel_kg.toFixed(1)} kg`
    : 'Fuel posture appears once fleet telemetry is live';

  const missionValue = snapshot?.timestamp
    ? `${formatWithFormatter(missionFormatter, snapshot.timestamp)} UTC`
    : 'Link Pending';
  const missionDetail = `Tick ${status?.tick_count.toLocaleString() ?? '--'} / ${satellites.length.toLocaleString()} sats / ${debris.length.toLocaleString()} debris`;

  const operationsLiveSummary = selectedSatId
    ? `Tracking ${selectedSatId}. ${threatCounts.red} critical conjunctions. ${watchedPendingBurns.length} pending burns in focus.`
    : `Fleet overview active. ${threatCounts.red} critical conjunctions. ${watchedPendingBurns.length} pending burns in queue.`;

  return (
    <div style={styles.root}>
      <div style={styles.backgroundLayer} aria-hidden="true" />
      <div style={styles.scanlines} aria-hidden="true" />

      <main id="operations-main" style={styles.shell}>
        <header style={styles.header}>
          <div style={styles.headerLead}>
            <span style={styles.eyebrow}>Orbital Insight / Flight Dynamics Console</span>
            <h1 style={styles.title}>2D Operations Dashboard</h1>
            <p style={styles.subtitle}>
              Ground-track awareness and maneuver scheduling now anchor the layout so the mission-control view stays primary.
              The 3D globe remains available as supporting orbital context instead of the main stage.
            </p>
          </div>

          <div style={styles.summaryGrid}>
            <SummaryCard label="Mission Time" value={missionValue} detail={missionDetail} tone="primary" />
            <SummaryCard label="Watch Target" value={watchTargetValue} detail={watchTargetDetail} tone={selectedSatId ? 'accent' : 'neutral'} />
            <SummaryCard label="Threat Index" value={threatValue} detail={threatDetail} tone={threatCounts.red > 0 ? 'critical' : threatCounts.yellow > 0 ? 'warning' : 'accent'} />
            <SummaryCard label="Burn Queue" value={burnValue} detail={burnDetail} tone={watchedPendingBurns.length > 0 ? 'warning' : 'accent'} />
            <SummaryCard label="Resource Posture" value={resourceValue} detail={resourceDetail} tone={lowestFuelSatellite && lowestFuelSatellite.fuel_kg < 10 ? 'critical' : 'warning'} />
          </div>
        </header>

        <div style={isStacked ? styles.contentStack : styles.contentGrid}>
          <section aria-labelledby="ground-track-heading" style={styles.mapSlot}>
            <h2 id="ground-track-heading" style={styles.visuallyHidden}>Ground Track Operations</h2>
            <GlassPanel
              title="GROUND TRACK OPERATIONS"
              revealIndex={0}
              bootComplete={booted}
              noPadding
              priority="primary"
              accentColor={theme.colors.primary}
              style={styles.mapPanel}
            >
              <div style={styles.primaryPanelBody}>
                <div style={styles.panelLead}>
                  <div style={styles.primaryCopyBlock}>
                    <span style={styles.primaryTagline}>Primary mission surface</span>
                    <p style={styles.panelBrief}>
                      Live fleet map, debris field, line-of-sight context, and selection control in one command surface.
                    </p>
                  </div>
                  <div style={styles.chipRow}>
                    <InfoChip label="Mode" value={watchStateValue} tone={watchStateTone} />
                    <InfoChip label="Objects" value={satellites.length.toLocaleString()} tone="primary" />
                    <InfoChip label="Debris" value={debris.length.toLocaleString()} tone="warning" />
                    <InfoChip label="Path" value={trajectory?.satellite_id ?? 'Standby'} tone={trajectory?.satellite_id ? (selectedSatId ? 'accent' : 'primary') : 'neutral'} />
                  </div>
                </div>

                <div style={styles.primaryCanvasFrame}>
                  <GroundTrackMap
                    snapshot={snapshot ?? null}
                    selectedSatId={selectedSatId}
                    onSelectSat={onSelectSat}
                    trackHistory={trackHistoryRef.current}
                    trackVersion={trackVersion}
                    trajectory={trajectory}
                  />
                </div>

                <div style={styles.panelFooter}>
                  <span>Select on-map to drive the watchlist, bullseye, timeline, and 3D context.</span>
                  <span>Focused spacecraft renders a 90-minute trail and a dashed 90-minute forecast.</span>
                </div>
              </div>
            </GlassPanel>
          </section>

          <section aria-labelledby="timeline-heading" style={styles.timelineSlot}>
            <h2 id="timeline-heading" style={styles.visuallyHidden}>Predictive Maneuver Timeline</h2>
            <GlassPanel
              title="PREDICTIVE MANEUVER TIMELINE"
              revealIndex={1}
              bootComplete={booted}
              noPadding
              priority="primary"
              accentColor={theme.colors.warning}
              style={styles.timelinePanel}
            >
              <div style={styles.primaryPanelBody}>
                <div style={styles.panelLead}>
                  <div style={styles.primaryCopyBlock}>
                    <span style={styles.primaryTagline}>Predictive scheduler</span>
                    <p style={styles.panelBrief}>
                      Pending actions, executed burns, and future maneuver windows stay on the main deck for immediate review.
                    </p>
                  </div>
                  <div style={styles.chipRow}>
                    <InfoChip label="View" value={watchStateValue} tone={watchStateTone} />
                    <InfoChip label="Pending" value={watchedPendingBurns.length.toString()} tone={watchedPendingBurns.length > 0 ? 'warning' : 'accent'} />
                    <InfoChip label="Executed" value={watchedExecutedBurns.length.toString()} tone="primary" />
                    <InfoChip
                      label="Next Burn"
                      value={nextPendingBurn ? `${formatWithFormatter(shortTimeFormatter, nextPendingBurn.burn_epoch)} UTC` : 'None'}
                      tone={nextPendingBurn ? 'primary' : 'neutral'}
                    />
                  </div>
                </div>

                <div style={styles.timelineFrame}>
                  <ManeuverGantt burns={burns} selectedSatId={selectedSatId} nowEpochS={nowEpochS} />
                </div>

                <div style={styles.panelFooter}>
                  <span>Burn chronology stays adjacent to the map instead of dropping into the support rail.</span>
                  <span>Markers show burn start/end while dashed spans reserve the 600-second cooldown window.</span>
                </div>
              </div>
            </GlassPanel>
          </section>

          <aside style={styles.sideColumn}>
            <section aria-labelledby="conjunction-heading" style={styles.sideSlot}>
              <h2 id="conjunction-heading" style={styles.visuallyHidden}>Conjunction Watch</h2>
              <GlassPanel
                title="CONJUNCTION WATCH"
                revealIndex={2}
                bootComplete={booted}
                noPadding
                priority="secondary"
                accentColor={theme.colors.primary}
                style={styles.sidePanel}
              >
                <div style={styles.sidePanelBody}>
                  <div style={styles.chipRowLeft}>
                    <InfoChip label="Critical" value={threatCounts.red.toString()} tone="critical" />
                    <InfoChip label="Warning" value={threatCounts.yellow.toString()} tone="warning" />
                    <InfoChip label="Nominal" value={threatCounts.green.toString()} tone="accent" />
                  </div>

                  <div style={styles.sideCanvasFrame}>
                    <ConjunctionBullseye
                      conjunctions={conjList}
                      selectedSatId={selectedSatId}
                      nowEpochS={nowEpochS}
                    />
                  </div>

                  <p style={styles.sideNote}>
                    {selectedSatId
                      ? `Relative approach view locked to ${selectedSatId}.`
                      : 'Fleet-wide approach watch. Select a spacecraft for a centered bullseye view.'}
                  </p>
                </div>
              </GlassPanel>
            </section>

            <section aria-labelledby="mission-control-heading" style={styles.sideSlot}>
              <h2 id="mission-control-heading" style={styles.visuallyHidden}>Mission Status And Resources</h2>
              <GlassPanel
                title="MISSION STATUS & RESOURCES"
                revealIndex={3}
                bootComplete={booted}
                noPadding
                priority="secondary"
                accentColor={theme.colors.accent}
                style={styles.commandPanel}
              >
                <div style={styles.commandPanelBody}>
                  <div style={styles.commandHeader}>
                    <div style={styles.selectionBlock}>
                      <span style={styles.selectionLabel}>Watch Target</span>
                      <div style={styles.selectionValueRow}>
                        <span style={styles.selectionValue}>{selectedSatId ?? 'Fleet View'}</span>
                        {selectedSatId && (
                          <button
                            style={styles.clearBtn}
                            onClick={() => onSelectSat(null)}
                            aria-label="Clear selected satellite"
                          >
                            CLEAR
                          </button>
                        )}
                      </div>
                      <span style={styles.selectionMeta}>
                        {activeSatellite
                          ? `${activeSatellite.status} / ${activeSatellite.fuel_kg.toFixed(1)} kg fuel`
                          : 'Choose a spacecraft from the map, fuel watchlist, or globe to narrow the deck.'}
                      </span>
                    </div>

                    <div style={styles.commandControls}>
                      <SimControls />
                      <p style={styles.commandHint}>
                        Step the sim while keeping the map and scheduler in frame.
                      </p>
                    </div>
                  </div>

                  <div style={styles.statusFrame}>
                    <StatusPanel
                      status={status}
                      apiError={statusError || snapError}
                      snapshotTimestamp={snapshot?.timestamp}
                      debrisCount={debris.length}
                      satCount={satellites.length}
                    />
                  </div>

                  <div style={styles.resourceLead}>
                    <p style={styles.panelBriefCompact}>
                      Keep degraded assets visible before collision-avoidance demand spikes.
                    </p>
                    <div style={styles.chipRowLeft}>
                      <InfoChip label="Average Fuel" value={satellites.length > 0 ? `${avgFuelKg.toFixed(1)} kg` : '--'} tone="warning" />
                      <InfoChip label="Nominal" value={statusCounts.nominal.toString()} tone="accent" />
                      <InfoChip label="Degraded" value={statusCounts.degraded.toString()} tone={statusCounts.degraded > 0 ? 'critical' : 'neutral'} />
                    </div>
                  </div>

                  <div style={styles.fuelFrame}>
                    <FuelHeatmap
                      satellites={satellites}
                      selectedSatId={selectedSatId}
                      onSelectSat={onSelectSat}
                    />
                  </div>
                </div>
              </GlassPanel>
            </section>

            <section aria-labelledby="orbital-context-heading" style={styles.sideSlot}>
              <h2 id="orbital-context-heading" style={styles.visuallyHidden}>Orbital Context Globe</h2>
              <GlassPanel
                title="3D ORBITAL CONTEXT"
                revealIndex={4}
                bootComplete={booted}
                noPadding
                priority="support"
                accentColor={theme.colors.primary}
                style={styles.globePanel}
              >
                <div style={styles.sidePanelBody}>
                  <div style={styles.globeHeaderRow}>
                    <div style={styles.globeSelectionBlock}>
                      <span style={styles.selectionLabel}>Context Target</span>
                      <span style={styles.globeTargetValue}>{selectedSatId ?? 'Fleet Overview'}</span>
                    </div>
                    <div style={styles.chipRowLeft}>
                      <InfoChip label="Trail" value={selectedSatId ? 'Enabled' : 'Fleet'} tone={selectedSatId ? 'accent' : 'neutral'} />
                    </div>
                  </div>
                  <p style={styles.panelBriefCompact}>
                    Supporting orbital context only. It reinforces selection and trajectory awareness without taking command priority.
                  </p>
                  <div style={styles.globeFrame}>
                    <EarthGlobe
                      satellites={satellites}
                      debris={debris}
                      selectedSatId={selectedSatId}
                      onSelectSat={onSelectSat}
                      trailPoints={trailVectors}
                      predictedPoints={predictedVectors}
                      style={styles.globeViewport}
                    />
                  </div>
                </div>
              </GlassPanel>
            </section>
          </aside>
        </div>

        <div aria-live="polite" style={styles.visuallyHidden}>
          {operationsLiveSummary}
        </div>
      </main>
    </div>
  );
}

export default App;

function buildStyles(isStacked: boolean, isCompact: boolean): Record<string, CSSProperties> {
  return {
    root: {
      width: '100%',
      height: '100%',
      position: 'relative',
      overflow: 'hidden',
      background: theme.colors.bg,
      color: theme.colors.text,
      fontFamily: theme.font.mono,
    },
    backgroundLayer: {
      position: 'absolute',
      inset: 0,
      background: `
        radial-gradient(circle at 14% 18%, rgba(58, 159, 232, 0.20), transparent 30%),
        radial-gradient(circle at 88% 12%, rgba(34, 197, 94, 0.10), transparent 22%),
        radial-gradient(circle at 72% 82%, rgba(234, 179, 8, 0.08), transparent 24%),
        linear-gradient(180deg, rgba(10, 18, 36, 0.96), rgba(5, 10, 20, 0.98))
      `,
      zIndex: 0,
    },
    scanlines: {
      position: 'absolute',
      inset: 0,
      background: `repeating-linear-gradient(
        0deg,
        transparent,
        transparent 2px,
        ${theme.colors.scanline} 2px,
        ${theme.colors.scanline} 4px
      )`,
      pointerEvents: 'none',
      zIndex: 0,
    },
    shell: {
      position: 'relative',
      zIndex: 1,
      height: '100%',
      display: 'flex',
      flexDirection: 'column',
      gap: isCompact ? '8px' : '10px',
      padding: isCompact ? '12px' : '16px',
      overflow: 'auto',
    },
    header: {
      display: 'flex',
      flexDirection: 'column',
      gap: '10px',
      flex: '0 0 auto',
    },
    headerLead: {
      display: 'flex',
      flexDirection: 'column',
      gap: '4px',
      maxWidth: isStacked ? '100%' : '64%',
    },
    eyebrow: {
      fontSize: '9px',
      letterSpacing: '0.2em',
      textTransform: 'uppercase',
      color: theme.colors.primary,
      textShadow: `0 0 14px ${theme.colors.primaryDim}`,
    },
    title: {
      fontSize: isCompact ? '24px' : '30px',
      fontWeight: 700,
      lineHeight: 1.05,
      letterSpacing: '-0.02em',
      color: theme.colors.text,
      textWrap: 'balance',
    },
    subtitle: {
      fontSize: isCompact ? '10px' : '11px',
      lineHeight: 1.55,
      color: theme.colors.textDim,
      maxWidth: '64ch',
    },
    summaryGrid: {
      display: 'grid',
      gridTemplateColumns: 'repeat(auto-fit, minmax(164px, 1fr))',
      gap: '8px',
    },
    contentGrid: {
      flex: 1,
      minHeight: 0,
      display: 'grid',
      gridTemplateColumns: 'minmax(0, 1.8fr) minmax(320px, 0.82fr)',
      gridTemplateRows: 'minmax(390px, 1.15fr) minmax(300px, 0.95fr)',
      gap: '12px',
      alignItems: 'stretch',
    },
    contentStack: {
      display: 'flex',
      flexDirection: 'column',
      gap: '12px',
      minHeight: 0,
      flex: 1,
    },
    mapSlot: isStacked ? {} : {
      gridColumn: 1,
      gridRow: 1,
      minHeight: 0,
    },
    timelineSlot: isStacked ? {} : {
      gridColumn: 1,
      gridRow: 2,
      minHeight: 0,
    },
    sideColumn: isStacked ? {
      display: 'flex',
      flexDirection: 'column',
      gap: '12px',
    } : {
      gridColumn: 2,
      gridRow: '1 / span 2',
      minHeight: 0,
      display: 'grid',
      gridTemplateRows: 'minmax(220px, 0.92fr) minmax(330px, 1.16fr) minmax(210px, 0.82fr)',
      gap: '12px',
    },
    sideSlot: {
      minHeight: 0,
    },
    mapPanel: {
      height: isStacked ? 'clamp(360px, 58vh, 540px)' : '100%',
    },
    timelinePanel: {
      height: isStacked ? 'clamp(300px, 44vh, 410px)' : '100%',
    },
    sidePanel: {
      height: isStacked ? 'clamp(280px, 42vh, 360px)' : '100%',
    },
    commandPanel: {
      height: isStacked ? 'clamp(420px, 70vh, 560px)' : '100%',
    },
    globePanel: {
      height: isStacked ? 'clamp(280px, 42vh, 360px)' : '100%',
    },
    primaryPanelBody: {
      display: 'flex',
      flexDirection: 'column',
      gap: '8px',
      height: '100%',
      minHeight: 0,
      padding: '8px 10px 10px',
    },
    panelLead: {
      display: 'flex',
      flexDirection: isCompact ? 'column' : 'row',
      alignItems: isCompact ? 'stretch' : 'flex-start',
      justifyContent: 'space-between',
      gap: '8px',
    },
    primaryCopyBlock: {
      display: 'flex',
      flexDirection: 'column',
      gap: '3px',
      minWidth: 0,
    },
    primaryTagline: {
      color: theme.colors.primary,
      fontSize: '9px',
      letterSpacing: '0.16em',
      textTransform: 'uppercase',
      opacity: 0.85,
    },
    panelBrief: {
      flex: 1,
      minWidth: 0,
      color: '#94a8c7',
      fontSize: '10px',
      lineHeight: 1.45,
      maxWidth: isCompact ? '100%' : '54ch',
    },
    panelBriefCompact: {
      color: theme.colors.textDim,
      fontSize: '9px',
      lineHeight: 1.45,
    },
    chipRow: {
      display: 'flex',
      flexWrap: 'wrap',
      gap: '6px',
      justifyContent: isCompact ? 'flex-start' : 'flex-end',
    },
    chipRowLeft: {
      display: 'flex',
      flexWrap: 'wrap',
      gap: '6px',
      justifyContent: 'flex-start',
    },
    primaryCanvasFrame: {
      flex: 1,
      minHeight: 0,
      overflow: 'hidden',
      clipPath: theme.chamfer.clipPath,
      border: '1px solid rgba(58, 159, 232, 0.32)',
      background: 'linear-gradient(180deg, rgba(4, 10, 20, 0.80), rgba(2, 7, 14, 0.94))',
      boxShadow: 'inset 0 0 28px rgba(2, 8, 16, 0.6), 0 0 20px rgba(58, 159, 232, 0.08)',
    },
    timelineFrame: {
      flex: 1,
      minHeight: 0,
      overflow: 'hidden',
      clipPath: theme.chamfer.clipPath,
      border: '1px solid rgba(234, 179, 8, 0.24)',
      background: 'linear-gradient(180deg, rgba(4, 10, 20, 0.78), rgba(2, 7, 14, 0.94))',
      boxShadow: 'inset 0 0 28px rgba(2, 8, 16, 0.6), 0 0 18px rgba(234, 179, 8, 0.05)',
    },
    panelFooter: {
      display: 'flex',
      flexDirection: isCompact ? 'column' : 'row',
      justifyContent: 'space-between',
      gap: '8px',
      color: theme.colors.textMuted,
      fontSize: '9px',
      lineHeight: 1.4,
    },
    sidePanelBody: {
      display: 'flex',
      flexDirection: 'column',
      gap: '8px',
      height: '100%',
      minHeight: 0,
      padding: '8px 10px 10px',
    },
    sideCanvasFrame: {
      flex: 1,
      minHeight: 0,
      overflow: 'hidden',
      clipPath: theme.chamfer.clipPath,
      border: `1px solid ${theme.colors.border}`,
      background: 'linear-gradient(180deg, rgba(3, 8, 16, 0.68), rgba(2, 7, 14, 0.88))',
    },
    sideNote: {
      color: theme.colors.textMuted,
      fontSize: '9px',
      lineHeight: 1.45,
    },
    commandPanelBody: {
      display: 'flex',
      flexDirection: 'column',
      gap: '8px',
      height: '100%',
      minHeight: 0,
      padding: '8px 10px 10px',
    },
    commandHeader: {
      display: 'flex',
      flexDirection: isCompact ? 'column' : 'row',
      justifyContent: 'space-between',
      alignItems: isCompact ? 'stretch' : 'flex-start',
      gap: '10px',
    },
    selectionBlock: {
      display: 'flex',
      flexDirection: 'column',
      gap: '4px',
      minWidth: 0,
    },
    selectionLabel: {
      color: theme.colors.textMuted,
      fontSize: '9px',
      letterSpacing: '0.14em',
      textTransform: 'uppercase',
    },
    selectionValueRow: {
      display: 'flex',
      flexWrap: 'wrap',
      alignItems: 'center',
      gap: '8px',
    },
    selectionValue: {
      color: theme.colors.text,
      fontSize: '15px',
      fontWeight: 700,
      minWidth: 0,
      textWrap: 'balance',
    },
    selectionMeta: {
      color: theme.colors.textDim,
      fontSize: '9px',
      lineHeight: 1.4,
      maxWidth: '52ch',
    },
    commandControls: {
      display: 'flex',
      flexDirection: 'column',
      gap: '6px',
      alignItems: isCompact ? 'flex-start' : 'flex-end',
    },
    commandHint: {
      color: theme.colors.textMuted,
      fontSize: '9px',
      lineHeight: 1.45,
      maxWidth: isCompact ? '100%' : '28ch',
      textAlign: isCompact ? 'left' : 'right',
    },
    clearBtn: {
      fontSize: '9px',
      fontFamily: theme.font.mono,
      padding: '4px 9px',
      border: `1px solid ${theme.colors.border}`,
      background: 'rgba(5, 10, 20, 0.6)',
      color: theme.colors.textDim,
      cursor: 'pointer',
      letterSpacing: '0.1em',
      clipPath: theme.chamfer.buttonClipPath,
    },
    statusFrame: {
      flex: '1 1 0',
      minHeight: 0,
      overflow: 'auto',
      clipPath: theme.chamfer.clipPath,
      border: `1px solid ${theme.colors.border}`,
      background: 'rgba(255, 255, 255, 0.015)',
    },
    resourceLead: {
      display: 'flex',
      flexDirection: 'column',
      gap: '8px',
    },
    fuelFrame: {
      flex: '0 0 148px',
      minHeight: '148px',
      overflow: 'hidden',
      clipPath: theme.chamfer.clipPath,
      border: `1px solid ${theme.colors.border}`,
      background: 'rgba(255, 255, 255, 0.02)',
    },
    globeHeaderRow: {
      display: 'flex',
      justifyContent: 'space-between',
      alignItems: 'flex-start',
      gap: '8px',
      flexWrap: 'wrap',
    },
    globeSelectionBlock: {
      display: 'flex',
      flexDirection: 'column',
      gap: '2px',
    },
    globeTargetValue: {
      color: theme.colors.text,
      fontSize: '12px',
      fontWeight: 600,
    },
    globeFrame: {
      flex: 1,
      minHeight: 0,
      overflow: 'hidden',
      clipPath: theme.chamfer.clipPath,
      border: `1px solid ${theme.colors.border}`,
      background: 'radial-gradient(circle at 50% 35%, rgba(22, 34, 64, 0.95), rgba(3, 8, 16, 0.94))',
      position: 'relative',
    },
    globeViewport: {
      position: 'relative',
      inset: 'auto',
      width: '100%',
      height: '100%',
    },
    visuallyHidden: {
      position: 'absolute',
      width: '1px',
      height: '1px',
      padding: 0,
      margin: '-1px',
      overflow: 'hidden',
      clip: 'rect(0, 0, 0, 0)',
      whiteSpace: 'nowrap',
      border: 0,
    },
  };
}
