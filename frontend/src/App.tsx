import { useEffect, useState } from 'react';
import { useHashRoute } from './app/useHashRoute';
import BootSequence from './components/BootSequence';
import { DashboardProvider, useDashboard } from './dashboard/DashboardContext';
import { AppShell } from './layout/AppShell';
import { CommandPage } from './pages/CommandPage';
import { TrackPage } from './pages/TrackPage';
import { ThreatPage } from './pages/ThreatPage';
import { BurnOpsPage } from './pages/BurnOpsPage';

function DashboardApp() {
  const { booted, setBooted } = useDashboard();
  const { pageId, navigate } = useHashRoute();
  const [viewportWidth, setViewportWidth] = useState(() => (
    typeof window === 'undefined' ? 1440 : window.innerWidth
  ));

  useEffect(() => {
    const onResize = () => setViewportWidth(window.innerWidth);
    window.addEventListener('resize', onResize);
    return () => window.removeEventListener('resize', onResize);
  }, []);

  const isNarrow = viewportWidth < 1080;
  const isCompact = viewportWidth < 760;

  if (!booted) {
    return <BootSequence onComplete={() => setBooted(true)} />;
  }

  return (
    <AppShell
      pageId={pageId}
      navigate={navigate}
      isNarrow={isNarrow}
      isCompact={isCompact}
    >
      {pageId === 'command' && <CommandPage isNarrow={isNarrow} isCompact={isCompact} />}
      {pageId === 'track' && <TrackPage isNarrow={isNarrow} isCompact={isCompact} />}
      {pageId === 'threat' && <ThreatPage isNarrow={isNarrow} isCompact={isCompact} />}
      {pageId === 'burn-ops' && <BurnOpsPage isNarrow={isNarrow} isCompact={isCompact} />}
    </AppShell>
  );
}

export default function App() {
  return (
    <DashboardProvider>
      <DashboardApp />
    </DashboardProvider>
  );
}
