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
        background: 'linear-gradient(180deg, rgba(17, 19, 23, 0.94), rgba(8, 9, 12, 0.96))',
        border: `1px solid ${tone === 'neutral' ? theme.colors.border : `${color}55`}`,
        boxShadow: `0 0 24px ${tone === 'neutral' ? 'rgba(88, 184, 255, 0.08)' : `${color}18`}`,
        clipPath: theme.chamfer.clipPath,
        padding: '14px 16px',
        minHeight: '92px',
        display: 'flex',
        flexDirection: 'column',
        justifyContent: 'space-between',
        gap: '10px',
      }}
    >
      <span
        style={{
          color: theme.colors.textMuted,
          fontSize: '10px',
          letterSpacing: '0.18em',
          textTransform: 'uppercase',
        }}
      >
        {label}
      </span>
      <span
        style={{
          color,
          fontSize: '24px',
          fontWeight: 700,
          letterSpacing: '-0.02em',
          fontVariantNumeric: 'tabular-nums',
          textWrap: 'balance',
        }}
      >
        {value}
      </span>
      <span
        style={{
          color: theme.colors.textDim,
          fontSize: '10px',
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
        gap: '4px',
        minWidth: '96px',
        padding: '9px 11px',
        border: `1px solid ${tone === 'neutral' ? theme.colors.border : `${color}55`}`,
        background: 'rgba(10, 11, 14, 0.88)',
        clipPath: theme.chamfer.buttonClipPath,
      }}
    >
      <span
        style={{
          color: theme.colors.textMuted,
          fontSize: '9px',
          letterSpacing: '0.14em',
          textTransform: 'uppercase',
        }}
      >
        {label}
      </span>
      <span
        style={{
          color: tone === 'neutral' ? theme.colors.text : color,
          fontSize: '14px',
          fontWeight: 600,
          fontVariantNumeric: 'tabular-nums',
        }}
      >
        {value}
      </span>
    </div>
  );
}

function HeroMetric({
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
    <div style={{ display: 'flex', flexDirection: 'column', gap: '6px' }}>
      <span style={{ color: theme.colors.textMuted, fontSize: '10px', letterSpacing: '0.16em', textTransform: 'uppercase' }}>
        {label}
      </span>
      <span style={{ color, fontSize: '18px', fontWeight: 700, fontVariantNumeric: 'tabular-nums' }}>
        {value}
      </span>
      <span style={{ color: theme.colors.textDim, fontSize: '11px', lineHeight: 1.5 }}>
        {detail}
      </span>
    </div>
  );
}

function SectionHeader({
  title,
  kicker,
  description,
}: {
  title: string;
  kicker: string;
  description: string;
}) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: '8px' }}>
      <span style={{ color: theme.colors.primary, fontSize: '10px', letterSpacing: '0.2em', textTransform: 'uppercase' }}>
        {kicker}
      </span>
      <div style={{ display: 'flex', flexDirection: 'column', gap: '4px' }}>
        <h2 style={{ fontSize: '28px', lineHeight: 1.05, color: theme.colors.text, fontWeight: 700 }}>
          {title}
        </h2>
        <p style={{ color: theme.colors.textDim, fontSize: '13px', lineHeight: 1.6, maxWidth: '68ch' }}>
          {description}
        </p>
      </div>
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

  const isNarrow = viewportWidth < 980;
  const isCompact = viewportWidth < 760;
  const styles = buildStyles(isNarrow, isCompact);

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

  const resourceValue = satellites.length > 0 ? `${avgFuelKg.toFixed(1)} kg avg` : 'Awaiting telemetry';
  const resourceDetail = lowestFuelSatellite
    ? `Lowest fuel ${lowestFuelSatellite.id} at ${lowestFuelSatellite.fuel_kg.toFixed(1)} kg`
    : 'Fuel posture appears once fleet telemetry is live';

  const missionValue = snapshot?.timestamp
    ? `${formatWithFormatter(missionFormatter, snapshot.timestamp)} UTC`
    : 'Link Pending';
  const missionDetail = `Tick ${status?.tick_count.toLocaleString() ?? '--'} / ${satellites.length.toLocaleString()} sats / ${debris.length.toLocaleString()} debris`;

  const heroModeValue = selectedSatId ?? 'Fleet';
  const heroPathValue = trajectory?.satellite_id ?? 'Standby';

  const operationsLiveSummary = selectedSatId
    ? `Tracking ${selectedSatId}. ${threatCounts.red} critical conjunctions. ${watchedPendingBurns.length} pending burns in focus.`
    : `Fleet overview active. ${threatCounts.red} critical conjunctions. ${watchedPendingBurns.length} pending burns in queue.`;

  return (
    <div style={styles.root}>
      <div style={styles.backgroundLayer} aria-hidden="true" />
      <div style={styles.scanlines} aria-hidden="true" />

      <main id="operations-main" style={styles.shell}>
        <header style={styles.heroHeader}>
          <div style={styles.headerLead}>
            <span style={styles.eyebrow}>Orbital Insight / Flight Dynamics Console</span>
            <h1 style={styles.title}>Orbital Operations Dashboard</h1>
            <p style={styles.subtitle}>
              Live flight-dynamics view for constellation posture, conjunction pressure, maneuver timing, and propellant health.
            </p>
          </div>
        </header>

        <div style={styles.stickySummaryRail}>
          <div style={styles.summaryGrid}>
            <SummaryCard label="Mission Time" value={missionValue} detail={missionDetail} tone="primary" />
            <SummaryCard label="Watch Target" value={watchTargetValue} detail={watchTargetDetail} tone={selectedSatId ? 'accent' : 'neutral'} />
            <SummaryCard label="Threat Index" value={threatValue} detail={threatDetail} tone={threatCounts.red > 0 ? 'critical' : threatCounts.yellow > 0 ? 'warning' : 'accent'} />
            <SummaryCard label="Burn Queue" value={burnValue} detail={burnDetail} tone={watchedPendingBurns.length > 0 ? 'warning' : 'accent'} />
            <SummaryCard label="Resource Posture" value={resourceValue} detail={resourceDetail} tone={lowestFuelSatellite && lowestFuelSatellite.fuel_kg < 10 ? 'critical' : 'warning'} />
          </div>
        </div>

        <section aria-labelledby="hero-heading" style={styles.heroSection}>
          <h2 id="hero-heading" style={styles.visuallyHidden}>Orbital Hero And Mission Rail</h2>
          <div style={styles.heroGrid}>
            <GlassPanel
              title="3D ORBITAL COMMAND VIEW"
              revealIndex={0}
              bootComplete={booted}
              noPadding
              priority="primary"
              accentColor={theme.colors.primary}
              style={styles.heroGlobePanel}
            >
              <div style={styles.heroPanelBody}>
                <div style={styles.heroLeadRow}>
                  <div style={styles.heroCopyBlock}>
                    <span style={styles.heroKicker}>Live orbital picture</span>
                    <h3 style={styles.heroTitle}>Mission Theatre</h3>
                    <p style={styles.heroDescription}>
                      Fleet geometry, debris corridors, and selected-vehicle trajectory in one view. Use the globe to set context before drilling into
                      track, conjunction, and burn planning.
                    </p>
                  </div>
                  <div style={styles.heroChipWrap}>
                    <InfoChip label="Mode" value={heroModeValue} tone={selectedSatId ? 'accent' : 'primary'} />
                    <InfoChip label="Path" value={heroPathValue} tone={trajectory?.satellite_id ? 'primary' : 'neutral'} />
                    <InfoChip label="Satellites" value={satellites.length.toLocaleString()} tone="accent" />
                    <InfoChip label="Debris" value={debris.length.toLocaleString()} tone="warning" />
                  </div>
                </div>

                <div style={styles.heroViewportFrame}>
                  <EarthGlobe
                    satellites={satellites}
                    debris={debris}
                    selectedSatId={selectedSatId}
                    onSelectSat={onSelectSat}
                    trailPoints={trailVectors}
                    predictedPoints={predictedVectors}
                    style={styles.heroViewport}
                  />
                </div>

                <div style={styles.heroFooterRow}>
                  <span>Select on the globe, map, or watchlist to lock the dashboard to one spacecraft.</span>
                  <span>Focused views share the same 90-minute trail, forecast path, conjunction plot, and burn clock.</span>
                </div>
              </div>
            </GlassPanel>

            <div style={styles.heroRail}>
              <GlassPanel
                title="MISSION RAIL"
                revealIndex={1}
                bootComplete={booted}
                noPadding
                priority="secondary"
                accentColor={theme.colors.accent}
                style={styles.heroRailPanel}
              >
                <div style={styles.railBody}>
                  <div style={styles.railSection}>
                    <span style={styles.selectionLabel}>Context Target</span>
                    <div style={styles.selectionValueRow}>
                      <span style={styles.heroRailValue}>{selectedSatId ?? 'Fleet Overview'}</span>
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
                        ? `${activeSatellite.status} / ${activeSatellite.fuel_kg.toFixed(1)} kg fuel remaining`
                        : 'Select a spacecraft to center the deck on one vehicle.'}
                    </span>
                  </div>

                  <div style={styles.heroMetricGrid}>
                    <HeroMetric label="Threat Watch" value={threatValue} detail={threatDetail} tone={threatCounts.red > 0 ? 'critical' : threatCounts.yellow > 0 ? 'warning' : 'accent'} />
                    <HeroMetric label="Burn Window" value={burnValue} detail={burnDetail} tone={watchedPendingBurns.length > 0 ? 'warning' : 'primary'} />
                    <HeroMetric label="Fuel Posture" value={resourceValue} detail={resourceDetail} tone={lowestFuelSatellite && lowestFuelSatellite.fuel_kg < 10 ? 'critical' : 'warning'} />
                  </div>

                  <div style={styles.heroControlsWrap}>
                    <SimControls />
                    <p style={styles.commandHint}>
                      Advance the sim here, then confirm track, conjunction, and burn status below.
                    </p>
                  </div>
                </div>
              </GlassPanel>

              <GlassPanel
                title="CONJUNCTION WATCH"
                revealIndex={2}
                bootComplete={booted}
                noPadding
                priority="secondary"
                accentColor={theme.colors.primary}
                style={styles.watchPanel}
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
                      ? `Relative approach plot centered on ${selectedSatId}.`
                      : 'Fleet relative-approach watch. Select a spacecraft to center the plot.'}
                  </p>
                </div>
              </GlassPanel>
            </div>
          </div>
        </section>

        <section aria-labelledby="ground-track-heading" style={styles.sectionStack}>
          <h2 id="ground-track-heading" style={styles.visuallyHidden}>Ground Track Operations</h2>
          <SectionHeader
            kicker="Section 01"
            title="Ground Track Operations"
            description="Global track view with trail history, forecast path, and station context for rapid spacecraft handoff."
          />
          <GlassPanel
            title="GROUND TRACK OPERATIONS"
            revealIndex={3}
            bootComplete={booted}
            noPadding
            priority="primary"
            accentColor={theme.colors.primary}
            style={styles.fullWidthPanel}
          >
            <div style={styles.primaryPanelBodyLarge}>
              <div style={styles.panelLeadLarge}>
                <div style={styles.primaryCopyBlockLarge}>
                  <span style={styles.primaryTagline}>2D tactical track</span>
                  <p style={styles.panelBriefLarge}>
                    Select any spacecraft to sync the track, conjunction view, and burn schedule. Focused vehicles show the last 90 minutes and the next 90-minute prediction.
                  </p>
                </div>
                <div style={styles.chipRow}>
                  <InfoChip label="Mode" value={selectedSatId ?? 'Fleet'} tone={selectedSatId ? 'accent' : 'primary'} />
                  <InfoChip label="Objects" value={satellites.length.toLocaleString()} tone="accent" />
                  <InfoChip label="Debris" value={debris.length.toLocaleString()} tone="warning" />
                  <InfoChip label="Path" value={trajectory?.satellite_id ?? 'Standby'} tone={trajectory?.satellite_id ? 'primary' : 'neutral'} />
                </div>
              </div>

              <div style={styles.primaryCanvasFrameLarge}>
                <GroundTrackMap
                  snapshot={snapshot ?? null}
                  selectedSatId={selectedSatId}
                  onSelectSat={onSelectSat}
                  trackHistory={trackHistoryRef.current}
                  trackVersion={trackVersion}
                  trajectory={trajectory}
                />
              </div>
            </div>
          </GlassPanel>
        </section>

        <section aria-labelledby="timeline-heading" style={styles.sectionStack}>
          <h2 id="timeline-heading" style={styles.visuallyHidden}>Predictive Maneuver Timeline</h2>
          <SectionHeader
            kicker="Section 02"
            title="Predictive Maneuver Timeline"
            description="Executed burns, queued maneuvers, and cooldown reservations aligned on one command timeline."
          />
          <GlassPanel
            title="PREDICTIVE MANEUVER TIMELINE"
            revealIndex={4}
            bootComplete={booted}
            noPadding
            priority="primary"
            accentColor={theme.colors.warning}
            style={styles.timelineHeroPanel}
          >
            <div style={styles.primaryPanelBodyLarge}>
              <div style={styles.panelLeadLarge}>
                <div style={styles.primaryCopyBlockLarge}>
                  <span style={styles.primaryTagline}>Burn scheduler</span>
                  <p style={styles.panelBriefLarge}>
                    Review executed burns, queued maneuvers, and cooldown holds on one continuous clock for rapid go or hold decisions.
                  </p>
                </div>
                <div style={styles.chipRow}>
                  <InfoChip label="View" value={selectedSatId ?? 'Fleet'} tone={selectedSatId ? 'accent' : 'primary'} />
                  <InfoChip label="Pending" value={watchedPendingBurns.length.toString()} tone={watchedPendingBurns.length > 0 ? 'warning' : 'accent'} />
                  <InfoChip label="Executed" value={watchedExecutedBurns.length.toString()} tone="primary" />
                  <InfoChip
                    label="Next Burn"
                    value={nextPendingBurn ? `${formatWithFormatter(shortTimeFormatter, nextPendingBurn.burn_epoch)} UTC` : 'None'}
                    tone={nextPendingBurn ? 'warning' : 'neutral'}
                  />
                </div>
              </div>

              <div style={styles.timelineFrameLarge}>
                <ManeuverGantt burns={burns} selectedSatId={selectedSatId} nowEpochS={nowEpochS} />
              </div>
            </div>
          </GlassPanel>
        </section>

        <section aria-labelledby="support-heading" style={styles.sectionStack}>
          <h2 id="support-heading" style={styles.visuallyHidden}>Mission Support Modules</h2>
          <SectionHeader
            kicker="Section 03"
            title="Mission Status And Resources"
            description="System state, live telemetry, and propellant watchlists for quick readiness checks."
          />
          <div style={styles.supportGrid}>
            <GlassPanel
              title="MISSION STATUS"
              revealIndex={5}
              bootComplete={booted}
              noPadding
              priority="secondary"
              accentColor={theme.colors.accent}
              style={styles.supportPanel}
            >
              <div style={styles.statusFrameWide}>
                <StatusPanel
                  status={status}
                  apiError={statusError || snapError}
                  snapshotTimestamp={snapshot?.timestamp}
                  debrisCount={debris.length}
                  satCount={satellites.length}
                />
              </div>
            </GlassPanel>

            <GlassPanel
              title="FUEL WATCHLIST"
              revealIndex={6}
              bootComplete={booted}
              noPadding
              priority="secondary"
              accentColor={theme.colors.warning}
              style={styles.supportPanel}
            >
              <div style={styles.commandPanelBody}>
                <div style={styles.resourceLeadLarge}>
                  <div style={styles.chipRowLeft}>
                    <InfoChip label="Average Fuel" value={satellites.length > 0 ? `${avgFuelKg.toFixed(1)} kg` : '--'} tone="warning" />
                    <InfoChip label="Nominal" value={statusCounts.nominal.toString()} tone="accent" />
                    <InfoChip label="Degraded" value={statusCounts.degraded.toString()} tone={statusCounts.degraded > 0 ? 'critical' : 'neutral'} />
                  </div>
                  <p style={styles.panelBriefCompactLarge}>
                    Scan degraded or low-fuel vehicles here after resolving track and maneuver priorities.
                  </p>
                </div>

                <div style={styles.fuelFrameWide}>
                  <FuelHeatmap
                    satellites={satellites}
                    selectedSatId={selectedSatId}
                    onSelectSat={onSelectSat}
                  />
                </div>
              </div>
            </GlassPanel>
          </div>
        </section>

        <div aria-live="polite" style={styles.visuallyHidden}>
          {operationsLiveSummary}
        </div>
      </main>
    </div>
  );
}

export default App;

function buildStyles(isNarrow: boolean, isCompact: boolean): Record<string, CSSProperties> {
  return {
    root: {
      width: '100%',
      minHeight: '100vh',
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
        radial-gradient(circle at 22% 8%, rgba(88, 184, 255, 0.12), transparent 24%),
        radial-gradient(circle at 84% 10%, rgba(255, 194, 71, 0.08), transparent 18%),
        linear-gradient(180deg, rgba(9, 10, 13, 1), rgba(6, 6, 7, 1))
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
      minHeight: '100vh',
      display: 'flex',
      flexDirection: 'column',
      gap: isCompact ? '18px' : '24px',
      padding: isCompact ? '16px 14px 28px' : '22px 24px 36px',
      overflow: 'visible',
    },
    heroHeader: {
      display: 'flex',
      flexDirection: 'column',
      gap: '12px',
    },
    headerLead: {
      display: 'flex',
      flexDirection: 'column',
      gap: '8px',
      maxWidth: isNarrow ? '100%' : '72ch',
    },
    eyebrow: {
      fontSize: '10px',
      letterSpacing: '0.22em',
      textTransform: 'uppercase',
      color: theme.colors.primary,
      textShadow: `0 0 18px ${theme.colors.primaryDim}`,
    },
    title: {
      fontSize: isCompact ? '36px' : '54px',
      fontWeight: 700,
      lineHeight: 0.98,
      letterSpacing: '-0.04em',
      color: theme.colors.text,
      textWrap: 'balance',
    },
    subtitle: {
      fontSize: isCompact ? '13px' : '15px',
      lineHeight: 1.6,
      color: theme.colors.textDim,
      maxWidth: '70ch',
    },
    stickySummaryRail: {
      position: 'sticky',
      top: 0,
      zIndex: 4,
      paddingTop: '2px',
      paddingBottom: '4px',
      backdropFilter: 'blur(10px)',
      WebkitBackdropFilter: 'blur(10px)',
      background: 'linear-gradient(180deg, rgba(6, 6, 7, 0.92), rgba(6, 6, 7, 0.76))',
      marginInline: '-6px',
      paddingInline: '6px',
    },
    summaryGrid: {
      display: 'grid',
      gridTemplateColumns: 'repeat(auto-fit, minmax(210px, 1fr))',
      gap: '12px',
    },
    heroSection: {
      display: 'flex',
      flexDirection: 'column',
      gap: '14px',
    },
    heroGrid: {
      display: 'grid',
      gridTemplateColumns: isNarrow ? '1fr' : 'minmax(0, 1.45fr) minmax(320px, 0.72fr)',
      gap: '18px',
      alignItems: 'stretch',
    },
    heroGlobePanel: {
      minHeight: isCompact ? '560px' : '720px',
    },
    heroRail: {
      display: 'flex',
      flexDirection: 'column',
      gap: '18px',
    },
    heroRailPanel: {
      minHeight: isCompact ? '320px' : '370px',
    },
    watchPanel: {
      minHeight: isCompact ? '320px' : '360px',
    },
    heroPanelBody: {
      display: 'flex',
      flexDirection: 'column',
      gap: '14px',
      flex: 1,
      minHeight: 0,
      padding: '14px 16px 16px',
    },
    heroLeadRow: {
      display: 'flex',
      flexDirection: isCompact ? 'column' : 'row',
      alignItems: isCompact ? 'stretch' : 'flex-start',
      justifyContent: 'space-between',
      gap: '14px',
    },
    heroCopyBlock: {
      display: 'flex',
      flexDirection: 'column',
      gap: '6px',
      minWidth: 0,
      maxWidth: '64ch',
    },
    heroKicker: {
      color: theme.colors.primary,
      fontSize: '10px',
      letterSpacing: '0.18em',
      textTransform: 'uppercase',
    },
    heroTitle: {
      color: theme.colors.text,
      fontSize: isCompact ? '26px' : '34px',
      lineHeight: 1.02,
      fontWeight: 700,
      letterSpacing: '-0.03em',
    },
    heroDescription: {
      color: theme.colors.textDim,
      fontSize: isCompact ? '13px' : '14px',
      lineHeight: 1.65,
    },
    heroChipWrap: {
      display: 'flex',
      flexWrap: 'wrap',
      gap: '8px',
      justifyContent: isCompact ? 'flex-start' : 'flex-end',
      maxWidth: isCompact ? '100%' : '360px',
    },
    heroViewportFrame: {
      flex: 1,
      overflow: 'hidden',
      clipPath: theme.chamfer.clipPath,
      border: '1px solid rgba(88, 184, 255, 0.34)',
      background: 'radial-gradient(circle at 50% 28%, rgba(16, 18, 24, 0.95), rgba(6, 7, 10, 0.98))',
      boxShadow: 'inset 0 0 36px rgba(0, 0, 0, 0.52), 0 0 28px rgba(88, 184, 255, 0.08)',
      position: 'relative',
      minHeight: isCompact ? '420px' : '520px',
    },
    heroViewport: {
      position: 'relative',
      width: '100%',
      height: '100%',
    },
    heroFooterRow: {
      display: 'flex',
      flexDirection: isCompact ? 'column' : 'row',
      justifyContent: 'space-between',
      gap: '10px',
      color: theme.colors.textMuted,
      fontSize: '11px',
      lineHeight: 1.5,
    },
    railBody: {
      display: 'flex',
      flexDirection: 'column',
      gap: '16px',
      flex: 1,
      minHeight: 0,
      padding: '14px 16px 16px',
    },
    railSection: {
      display: 'flex',
      flexDirection: 'column',
      gap: '6px',
    },
    selectionLabel: {
      color: theme.colors.textMuted,
      fontSize: '10px',
      letterSpacing: '0.16em',
      textTransform: 'uppercase',
    },
    selectionValueRow: {
      display: 'flex',
      flexWrap: 'wrap',
      alignItems: 'center',
      gap: '8px',
    },
    heroRailValue: {
      color: theme.colors.text,
      fontSize: '24px',
      fontWeight: 700,
      lineHeight: 1.05,
      letterSpacing: '-0.03em',
      textWrap: 'balance',
    },
    selectionMeta: {
      color: theme.colors.textDim,
      fontSize: '11px',
      lineHeight: 1.55,
    },
    heroMetricGrid: {
      display: 'grid',
      gridTemplateColumns: '1fr',
      gap: '14px',
      paddingTop: '8px',
      borderTop: `1px solid ${theme.colors.border}`,
    },
    heroControlsWrap: {
      marginTop: 'auto',
      display: 'flex',
      flexDirection: 'column',
      gap: '10px',
      paddingTop: '12px',
      borderTop: `1px solid ${theme.colors.border}`,
    },
    commandHint: {
      color: theme.colors.textMuted,
      fontSize: '11px',
      lineHeight: 1.55,
      maxWidth: '36ch',
    },
    sectionStack: {
      display: 'flex',
      flexDirection: 'column',
      gap: '14px',
    },
    fullWidthPanel: {
      height: 'auto',
    },
    timelineHeroPanel: {
      height: 'auto',
    },
    primaryPanelBodyLarge: {
      display: 'flex',
      flexDirection: 'column',
      gap: '14px',
      flex: 1,
      minHeight: 0,
      padding: '14px 16px 16px',
    },
    panelLeadLarge: {
      display: 'flex',
      flexDirection: isCompact ? 'column' : 'row',
      alignItems: isCompact ? 'stretch' : 'flex-start',
      justifyContent: 'space-between',
      gap: '14px',
    },
    primaryCopyBlockLarge: {
      display: 'flex',
      flexDirection: 'column',
      gap: '5px',
      minWidth: 0,
      maxWidth: '74ch',
    },
    primaryTagline: {
      color: theme.colors.primary,
      fontSize: '10px',
      letterSpacing: '0.16em',
      textTransform: 'uppercase',
      opacity: 0.95,
    },
    panelBriefLarge: {
      color: theme.colors.textDim,
      fontSize: '13px',
      lineHeight: 1.65,
    },
    chipRow: {
      display: 'flex',
      flexWrap: 'wrap',
      gap: '8px',
      justifyContent: isCompact ? 'flex-start' : 'flex-end',
    },
    chipRowLeft: {
      display: 'flex',
      flexWrap: 'wrap',
      gap: '8px',
      justifyContent: 'flex-start',
    },
    primaryCanvasFrameLarge: {
      width: '100%',
      flex: '0 0 auto',
      display: 'flex',
      flexDirection: 'column',
      alignSelf: 'center',
      maxWidth: isCompact ? '100%' : 'min(100%, 1840px)',
      aspectRatio: isCompact ? '4 / 3' : '20 / 7',
      minHeight: isCompact ? '320px' : '420px',
      maxHeight: isCompact ? '420px' : '640px',
      minWidth: 0,
      overflow: 'hidden',
      clipPath: theme.chamfer.clipPath,
      border: '1px solid rgba(88, 184, 255, 0.34)',
      background: 'linear-gradient(180deg, rgba(11, 13, 17, 0.92), rgba(7, 8, 10, 0.98))',
      boxShadow: 'inset 0 0 32px rgba(0, 0, 0, 0.62), 0 0 24px rgba(88, 184, 255, 0.06)',
    },
    timelineFrameLarge: {
      width: '100%',
      flex: '0 0 auto',
      display: 'flex',
      flexDirection: 'column',
      height: isCompact ? '320px' : '460px',
      minWidth: 0,
      overflow: 'hidden',
      clipPath: theme.chamfer.clipPath,
      border: '1px solid rgba(255, 194, 71, 0.28)',
      background: 'linear-gradient(180deg, rgba(11, 13, 17, 0.92), rgba(7, 8, 10, 0.98))',
      boxShadow: 'inset 0 0 32px rgba(0, 0, 0, 0.62), 0 0 22px rgba(255, 194, 71, 0.05)',
    },
    sidePanelBody: {
      display: 'flex',
      flexDirection: 'column',
      gap: '10px',
      flex: 1,
      minHeight: 0,
      padding: '12px 14px 14px',
    },
    sideCanvasFrame: {
      flex: 1,
      minHeight: 0,
      overflow: 'hidden',
      clipPath: theme.chamfer.clipPath,
      border: `1px solid ${theme.colors.border}`,
      background: 'linear-gradient(180deg, rgba(10, 11, 14, 0.92), rgba(7, 8, 10, 0.98))',
    },
    sideNote: {
      color: theme.colors.textMuted,
      fontSize: '11px',
      lineHeight: 1.55,
    },
    supportGrid: {
      display: 'grid',
      gridTemplateColumns: isNarrow ? '1fr' : 'minmax(0, 1fr) minmax(320px, 1fr)',
      gap: '18px',
    },
    supportPanel: {
      minHeight: isCompact ? '320px' : '380px',
    },
    statusFrameWide: {
      flex: 1,
      overflow: 'auto',
      clipPath: theme.chamfer.clipPath,
      border: `1px solid ${theme.colors.border}`,
      background: 'rgba(255, 255, 255, 0.02)',
    },
    commandPanelBody: {
      display: 'flex',
      flexDirection: 'column',
      gap: '12px',
      flex: 1,
      minHeight: 0,
      padding: '14px 16px 16px',
    },
    resourceLeadLarge: {
      display: 'flex',
      flexDirection: 'column',
      gap: '10px',
    },
    panelBriefCompactLarge: {
      color: theme.colors.textDim,
      fontSize: '12px',
      lineHeight: 1.6,
    },
    fuelFrameWide: {
      flex: 1,
      minHeight: '220px',
      overflow: 'hidden',
      clipPath: theme.chamfer.clipPath,
      border: `1px solid ${theme.colors.border}`,
      background: 'rgba(255, 255, 255, 0.02)',
    },
    clearBtn: {
      fontSize: '9px',
      fontFamily: theme.font.mono,
      padding: '5px 10px',
      border: `1px solid ${theme.colors.border}`,
      background: 'rgba(10, 11, 14, 0.88)',
      color: theme.colors.text,
      cursor: 'pointer',
      letterSpacing: '0.12em',
      clipPath: theme.chamfer.buttonClipPath,
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
