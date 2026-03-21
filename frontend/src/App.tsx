import { useState, useCallback, useMemo } from 'react';
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

  // ---- boot screen ----
  if (!booted) {
    return <BootSequence onComplete={handleBootComplete} />;
  }

  // ---- dashboard ----
  return (
    <div style={styles.root}>
      {/* CRT scanline overlay on entire viewport */}
      <div style={styles.scanlines} />

      {/* ---- Main grid ---- */}
      <div style={styles.grid}>

        {/* Left: 3D Globe (~55%) */}
        <GlassPanel
          title="ORBITAL VIEW"
          revealIndex={0}
          bootComplete={booted}
          noPadding
          style={styles.globePanel}
        >
          <EarthGlobe
            satellites={satellites}
            debris={debris}
            selectedSatId={selectedSatId}
            onSelectSat={onSelectSat}
            trailPoints={trailVectors}
            predictedPoints={predictedVectors}
          />
        </GlassPanel>

        {/* Center-right: Bullseye (~25%) */}
        <GlassPanel
          title="CONJUNCTION BULLSEYE"
          revealIndex={1}
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

        {/* Right sidebar: Status + Fuel (~20%) */}
        <div style={styles.sidebarColumn}>
          <GlassPanel
            title="SYSTEM STATUS"
            revealIndex={2}
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
            revealIndex={3}
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
        </div>

        {/* Bottom strip: Gantt timeline */}
        <GlassPanel
          title="MANEUVER TIMELINE"
          revealIndex={4}
          bootComplete={booted}
          noPadding
          style={styles.ganttPanel}
        >
          <div style={styles.ganttHeader}>
            <SimControls />
            {selectedSatId && (
              <span style={styles.ganttSelectedLabel}>
                Showing: {selectedSatId}
                <button
                  style={styles.ganttClearBtn}
                  onClick={() => onSelectSat(null)}
                >
                  CLEAR
                </button>
              </span>
            )}
          </div>
          <div style={styles.ganttCanvas}>
            <ManeuverGantt burns={burns} selectedSatId={selectedSatId} />
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
  grid: {
    display: 'grid',
    gridTemplateColumns: '55fr 25fr 20fr',
    gridTemplateRows: '1fr 160px',
    gap: '6px',
    padding: '6px',
    width: '100%',
    height: '100%',
  },
  globePanel: {
    gridColumn: '1 / 2',
    gridRow: '1 / 2',
    minHeight: 0,
  },
  bullseyePanel: {
    gridColumn: '2 / 3',
    gridRow: '1 / 2',
    minHeight: 0,
  },
  sidebarColumn: {
    gridColumn: '3 / 4',
    gridRow: '1 / 2',
    display: 'flex',
    flexDirection: 'column' as const,
    gap: '6px',
    minHeight: 0,
  },
  statusPanel: {
    flex: '0 0 auto',
    maxHeight: '50%',
    overflow: 'auto',
  },
  fuelPanel: {
    flex: '1 1 0',
    minHeight: 0,
    overflow: 'hidden',
  },
  ganttPanel: {
    gridColumn: '1 / -1',
    gridRow: '2 / 3',
    minHeight: 0,
  },
  ganttHeader: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'space-between',
    padding: '2px 12px',
    borderBottom: `1px solid ${theme.colors.border}`,
    flexShrink: 0,
  },
  ganttSelectedLabel: {
    fontSize: '10px',
    color: theme.colors.primary,
    fontFamily: theme.font.mono,
    display: 'flex',
    alignItems: 'center',
    gap: '8px',
  },
  ganttClearBtn: {
    fontSize: '9px',
    fontFamily: theme.font.mono,
    padding: '2px 8px',
    border: `1px solid ${theme.colors.border}`,
    borderRadius: '2px',
    background: 'transparent',
    color: theme.colors.textDim,
    cursor: 'pointer',
    letterSpacing: '0.08em',
  },
  ganttCanvas: {
    flex: 1,
    minHeight: 0,
  },
};
