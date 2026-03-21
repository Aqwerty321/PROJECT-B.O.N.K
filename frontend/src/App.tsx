import { useState, useCallback, useMemo, useEffect, useRef } from 'react';
import { theme } from './styles/theme';
import { useSnapshot, useStatus, useBurns, useConjunctions, useTrajectory } from './hooks/useApi';
import BootSequence from './components/BootSequence';
import EarthGlobe, { trajectoryToVectors } from './components/EarthGlobe';
import { GlassPanel } from './components/GlassPanel';
import { ConjunctionBullseye } from './components/ConjunctionBullseye';
import { StatusPanel } from './components/StatusPanel';
import { FuelHeatmap } from './components/FuelHeatmap';
import ManeuverGantt from './components/ManeuverGantt';
import SimControls from './components/SimControls';
import { GroundTrackMap } from './components/GroundTrackMap';

function App() {
  // ---- boot state ----
  const [booted, setBooted] = useState(false);
  const handleBootComplete = useCallback(() => setBooted(true), []);

  // ---- selected satellite ----
  const [selectedSatId, setSelectedSatId] = useState<string | null>(null);
  const onSelectSat = useCallback((id: string | null) => setSelectedSatId(id), []);

  // ---- API hooks (only poll after boot) ----
  const { snapshot, error: snapError } = useSnapshot(booted ? 1000 : 60000);
  const { status, error: statusError } = useStatus(booted ? 2000 : 60000);
  const { burns } = useBurns(booted ? 2000 : 60000);
  const { conjunctions } = useConjunctions(booted ? 2000 : 60000, selectedSatId ?? undefined);
  const { trajectory } = useTrajectory(selectedSatId, booted ? 2000 : 60000);

  // ---- derived data ----
  const satellites = snapshot?.satellites ?? [];
  const debris = snapshot?.debris_cloud ?? [];
  const conjList = conjunctions?.conjunctions ?? [];

  const nowEpochS = useMemo(() => {
    if (snapshot?.timestamp) {
      return new Date(snapshot.timestamp).getTime() / 1000;
    }
    return Date.now() / 1000;
  }, [snapshot?.timestamp]);

  const trailVectors = useMemo(
    () => trajectory?.trail ? trajectoryToVectors(trajectory.trail) : undefined,
    [trajectory?.trail],
  );
  const predictedVectors = useMemo(
    () => trajectory?.predicted ? trajectoryToVectors(trajectory.predicted) : undefined,
    [trajectory?.predicted],
  );

  // ---- track history accumulation for ground track mini-map ----
  const trackHistoryRef = useRef<Map<string, [number, number][]>>(new Map());
  const [trackHistory, setTrackHistory] = useState<Map<string, [number, number][]>>(new Map());

  useEffect(() => {
    if (!snapshot) return;
    const map = trackHistoryRef.current;
    for (const sat of snapshot.satellites) {
      let arr = map.get(sat.id);
      if (!arr) {
        arr = [];
        map.set(sat.id, arr);
      }
      arr.push([sat.lat, sat.lon]);
      // Keep last 200 points per satellite
      if (arr.length > 200) arr.shift();
    }
    setTrackHistory(new Map(map));
  }, [snapshot]);

  // ---- boot screen ----
  if (!booted) {
    return <BootSequence onComplete={handleBootComplete} />;
  }

  // ---- dashboard: full-screen globe + U-shaped cockpit HUD ----
  return (
    <div style={styles.root}>
      {/* Full-screen globe backdrop (z-index 0) */}
      <EarthGlobe
        satellites={satellites}
        debris={debris}
        selectedSatId={selectedSatId}
        onSelectSat={onSelectSat}
        trailPoints={trailVectors}
        predictedPoints={predictedVectors}
      />

      {/* HUD overlay container (z-index 10) */}
      <div style={styles.hudOverlay}>
        {/* CRT scanline overlay -- very subtle */}
        <div style={styles.scanlines} />

        {/* Left column: Status + Fuel + Mini-map */}
        <div style={styles.leftColumn}>
          <GlassPanel
            title="SYSTEM STATUS"
            revealIndex={0}
            bootComplete={booted}
            style={styles.statusPanel}
          >
            <StatusPanel
              status={status}
              apiError={statusError || snapError}
              snapshotTimestamp={snapshot?.timestamp}
              debrisCount={debris.length}
              satCount={satellites.length}
            />
          </GlassPanel>

          <GlassPanel
            title="FUEL RESERVES"
            revealIndex={1}
            bootComplete={booted}
            noPadding
            style={styles.fuelPanel}
          >
            <FuelHeatmap
              satellites={satellites}
              selectedSatId={selectedSatId}
              onSelectSat={onSelectSat}
            />
          </GlassPanel>

          <GlassPanel
            title="GROUND TRACK"
            revealIndex={2}
            bootComplete={booted}
            noPadding
            style={styles.miniMapPanel}
          >
            <GroundTrackMap
              snapshot={snapshot ?? null}
              selectedSatId={selectedSatId}
              onSelectSat={onSelectSat}
              trackHistory={trackHistory}
            />
          </GlassPanel>
        </div>

        {/* Right column: Bullseye + Sim Controls */}
        <div style={styles.rightColumn}>
          <GlassPanel
            title="CONJUNCTION BULLSEYE"
            revealIndex={3}
            bootComplete={booted}
            noPadding
            style={styles.bullseyePanel}
          >
            <ConjunctionBullseye
              conjunctions={conjList}
              selectedSatId={selectedSatId}
              nowEpochS={nowEpochS}
            />
          </GlassPanel>

          <GlassPanel
            title="SIM CONTROLS"
            revealIndex={4}
            bootComplete={booted}
            style={styles.simControlsPanel}
          >
            <SimControls />
            {selectedSatId && (
              <div style={styles.selectedLabel}>
                <span style={{ color: theme.colors.primary, fontSize: '10px' }}>
                  Tracking: {selectedSatId}
                </span>
                <button
                  style={styles.clearBtn}
                  onClick={() => onSelectSat(null)}
                >
                  CLEAR
                </button>
              </div>
            )}
          </GlassPanel>
        </div>

        {/* Bottom strip: Maneuver Timeline */}
        <GlassPanel
          title="MANEUVER TIMELINE"
          revealIndex={5}
          bootComplete={booted}
          noPadding
          style={styles.bottomStrip}
        >
          <div style={styles.ganttCanvas}>
            <ManeuverGantt burns={burns} selectedSatId={selectedSatId} nowEpochS={nowEpochS} />
          </div>
        </GlassPanel>
      </div>
    </div>
  );
}

export default App;

// ---- styles ----

const styles: Record<string, React.CSSProperties> = {
  root: {
    width: '100vw',
    height: '100vh',
    background: theme.colors.bg,
    position: 'relative',
    overflow: 'hidden',
    fontFamily: theme.font.mono,
  },
  hudOverlay: {
    position: 'fixed',
    inset: 0,
    zIndex: 10,
    pointerEvents: 'none',
  },
  scanlines: {
    position: 'fixed',
    inset: 0,
    background: `repeating-linear-gradient(
      0deg,
      transparent,
      transparent 2px,
      ${theme.colors.scanline} 2px,
      ${theme.colors.scanline} 4px
    )`,
    pointerEvents: 'none',
    zIndex: 100,
  },
  // Left column
  leftColumn: {
    position: 'fixed',
    left: '16px',
    top: '15vh',
    bottom: '180px',
    width: 'clamp(240px, 18vw, 320px)',
    display: 'flex',
    flexDirection: 'column' as const,
    gap: '8px',
    pointerEvents: 'auto' as const,
    zIndex: 11,
  },
  statusPanel: {
    flex: '0 0 auto',
    maxHeight: '40%',
    overflow: 'auto',
  },
  fuelPanel: {
    flex: '1 1 0',
    minHeight: 0,
    overflow: 'hidden',
  },
  miniMapPanel: {
    flex: '0 0 160px',
    overflow: 'hidden',
  },
  // Right column
  rightColumn: {
    position: 'fixed',
    right: '16px',
    top: '15vh',
    bottom: '180px',
    width: 'clamp(260px, 20vw, 360px)',
    display: 'flex',
    flexDirection: 'column' as const,
    gap: '8px',
    pointerEvents: 'auto' as const,
    zIndex: 11,
  },
  bullseyePanel: {
    flex: '1 1 0',
    minHeight: 0,
  },
  simControlsPanel: {
    flex: '0 0 auto',
  },
  selectedLabel: {
    display: 'flex',
    alignItems: 'center',
    gap: '8px',
    marginTop: '4px',
    fontFamily: theme.font.mono,
  },
  clearBtn: {
    fontSize: '9px',
    fontFamily: theme.font.mono,
    padding: '2px 8px',
    border: `1px solid ${theme.colors.border}`,
    background: 'transparent',
    color: theme.colors.textDim,
    cursor: 'pointer',
    letterSpacing: '0.08em',
    clipPath: theme.chamfer.buttonClipPath,
  },
  // Bottom strip
  bottomStrip: {
    position: 'fixed',
    bottom: '16px',
    left: '16px',
    right: '16px',
    height: 'clamp(120px, 12vh, 160px)',
    pointerEvents: 'auto' as const,
    zIndex: 11,
  },
  ganttCanvas: {
    width: '100%',
    height: '100%',
    minHeight: 0,
  },
};
