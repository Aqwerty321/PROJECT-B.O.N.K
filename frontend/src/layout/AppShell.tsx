import { useState, type CSSProperties, type ReactNode } from 'react';
import { NAV_ITEMS, labelForNav, type PageId } from '../app/navigation';
import { GlobalStepStatus } from '../components/dashboard/GlobalStepStatus';
import { SummaryCard } from '../components/dashboard/UiPrimitives';
import { useDashboard } from '../dashboard/DashboardContext';
import { theme } from '../styles/theme';

const SIDEBAR_EXPANDED = 216;
const SIDEBAR_COLLAPSED = 52;

function pageDescription(pageId: PageId): string {
  const item = NAV_ITEMS.find(entry => entry.id === pageId);
  return item?.blurb ?? 'Orbital operations workspace';
}

/* ─── Hamburger Icon ─── */
function HamburgerIcon({ open, onClick }: { open: boolean; onClick: () => void }) {
  const barBase: CSSProperties = {
    display: 'block',
    width: '22px',
    height: '2px',
    background: theme.colors.primary,
    borderRadius: '1px',
    transition: 'all 0.35s cubic-bezier(0.4, 0, 0.2, 1)',
    transformOrigin: 'center',
    boxShadow: `0 0 6px ${theme.colors.primaryDim}`,
  };

  return (
    <button
      type="button"
      onClick={onClick}
      aria-label={open ? 'Close navigation' : 'Open navigation'}
      style={{
        display: 'flex',
        flexDirection: 'column',
        justifyContent: 'center',
        alignItems: 'center',
        gap: open ? '0px' : '5px',
        width: '40px',
        height: '40px',
        background: 'transparent',
        border: `1px solid ${open ? theme.colors.primary + '44' : theme.colors.border}`,
        borderRadius: '6px',
        cursor: 'pointer',
        padding: 0,
        flexShrink: 0,
        transition: 'border-color 0.3s ease, box-shadow 0.3s ease',
        boxShadow: open ? `0 0 12px ${theme.colors.primaryDim}` : 'none',
      }}
    >
      <span style={{ ...barBase, transform: open ? 'rotate(45deg) translateY(1px)' : 'none' }} />
      <span style={{ ...barBase, opacity: open ? 0 : 1, transform: open ? 'scaleX(0)' : 'none' }} />
      <span style={{ ...barBase, transform: open ? 'rotate(-45deg) translateY(-1px)' : 'none' }} />
    </button>
  );
}

/* ─── Nav icon for collapsed state ─── */
function navIcon(id: PageId): string {
  switch (id) {
    case 'command': return '⌘';
    case 'track': return '◎';
    case 'threat': return '⚠';
    case 'burn-ops': return '⚡';
    default: return '●';
  }
}

/* ─── AppShell ─── */
export function AppShell({
  pageId,
  navigate,
  isNarrow,
  isCompact,
  isShellCondensed,
  children,
}: {
  pageId: PageId;
  navigate: (page: PageId) => void;
  isNarrow: boolean;
  isCompact: boolean;
  isShellCondensed: boolean;
  children: ReactNode;
}) {
  const { model } = useDashboard();
  const [sidebarOpen, setSidebarOpen] = useState(false);

  const sidebarWidth = sidebarOpen ? SIDEBAR_EXPANDED : SIDEBAR_COLLAPSED;
  const showPageDescription = !isCompact && !isShellCondensed && sidebarOpen;
  const title = isCompact ? labelForNav(NAV_ITEMS.find(i => i.id === pageId)!) : 'Orbital Operations Dashboard';

  return (
    <div style={styles.root}>
      <div style={styles.backgroundLayer} aria-hidden="true" />
      <div style={styles.scanlines} aria-hidden="true" />

      {/* ── Sidebar ── */}
      <aside
        aria-label="Primary navigation sidebar"
        style={{
          ...styles.sidebar,
          width: `${sidebarWidth}px`,
        }}
      >
        {/* Hamburger */}
        <div style={styles.sidebarHeader}>
          <HamburgerIcon open={sidebarOpen} onClick={() => setSidebarOpen(prev => !prev)} />
          {sidebarOpen && (
            <span style={styles.sidebarBrand}>C.A.S.C.A.D.E</span>
          )}
        </div>

        {/* Nav items */}
        <nav aria-label="Primary dashboard navigation" style={styles.sidebarNav}>
          {NAV_ITEMS.map(item => {
            const active = item.id === pageId;
            return (
              <button
                key={item.id}
                type="button"
                onClick={() => { navigate(item.id); if (isCompact) setSidebarOpen(false); }}
                aria-current={active ? 'page' : undefined}
                title={!sidebarOpen ? labelForNav(item) : undefined}
                style={{
                  ...styles.navButton,
                  justifyContent: sidebarOpen ? 'flex-start' : 'center',
                  borderColor: active ? `${theme.colors.primary}55` : 'transparent',
                  color: active ? theme.colors.primary : theme.colors.textDim,
                  background: active ? 'rgba(88, 184, 255, 0.10)' : 'transparent',
                  boxShadow: active ? `inset 3px 0 0 ${theme.colors.primary}, 0 0 14px rgba(88, 184, 255, 0.08)` : 'none',
                }}
              >
                <span style={{
                  fontSize: '16px',
                  flexShrink: 0,
                  width: '24px',
                  textAlign: 'center',
                  filter: active ? `drop-shadow(0 0 4px ${theme.colors.primary})` : 'none',
                }}>{navIcon(item.id)}</span>
                {sidebarOpen && (
                  <div style={styles.navTextGroup}>
                    <span style={styles.navLabel}>{labelForNav(item)}</span>
                    <span style={styles.navBlurb}>{item.blurb}</span>
                  </div>
                )}
              </button>
            );
          })}
        </nav>

        {/* Command status at bottom of sidebar */}
        <div style={styles.sidebarFooter}>
          <GlobalStepStatus compact={!sidebarOpen} />
        </div>
      </aside>

      {/* ── Main Content Area ── */}
      <div
        style={{
          ...styles.mainArea,
          marginLeft: `${sidebarWidth}px`,
        }}
      >
        {/* Top header bar */}
        <header
          style={{
            ...styles.topBar,
            alignItems: 'center',
          }}
        >
          <div style={{ ...styles.topBarLead, gap: '8px' }}>
            {!isShellCondensed && !isCompact && sidebarOpen && (
              <span style={styles.eyebrow}>Orbital Insight / Flight Dynamics Console</span>
            )}
            <h1 style={styles.title}>
              {title}
            </h1>
          </div>
          <div style={{ ...styles.topBarRight, width: isCompact ? '100%' : 'auto' }}>
            {showPageDescription && (
              <span style={styles.pageDesc}>{pageDescription(pageId)}</span>
            )}
          </div>
        </header>

        {/* Summary rail */}
        <div
          style={{
            ...styles.summaryRail,
            gridTemplateColumns: isCompact
              ? 'repeat(1, minmax(0, 1fr))'
              : isNarrow
                ? 'repeat(2, minmax(0, 1fr))'
                : 'repeat(5, minmax(0, 1fr))',
          }}
        >
          <SummaryCard label="Mission Time" value={model.missionValue} detail={model.missionDetail} tone="primary" />
          <SummaryCard label="Watch Target" value={model.watchTargetValue} detail={model.watchTargetDetail} tone={model.activeSatellite ? 'accent' : 'neutral'} />
          <SummaryCard label="Threat Index" value={model.threatValue} detail={model.threatDetail} tone={model.threatCounts.red > 0 ? 'critical' : model.threatCounts.yellow > 0 ? 'warning' : 'accent'} />
          <SummaryCard label="Burn Queue" value={model.burnValue} detail={model.burnDetail} tone={model.watchedPendingBurns.length > 0 ? 'warning' : 'accent'} />
          <SummaryCard label="Resource Posture" value={model.resourceValue} detail={model.resourceDetail} tone={model.lowestFuelSatellite && model.lowestFuelSatellite.fuel_kg < 10 ? 'critical' : 'warning'} />
        </div>

        {/* Page content */}
        <main
          id="operations-main"
          style={{
            flex: 1,
            minHeight: 0,
            overflow: 'auto',
            padding: isCompact ? '0 8px 12px' : '0 14px 16px',
          }}
        >
          {children}
        </main>

        <div aria-live="polite" style={styles.visuallyHidden}>
          {model.operationsLiveSummary}
        </div>
      </div>
    </div>
  );
}

/* ─── Styles ─── */
const styles: Record<string, CSSProperties> = {
  root: {
    width: '100%',
    height: '100dvh',
    position: 'relative',
    overflow: 'hidden',
    background: theme.colors.bg,
    color: theme.colors.text,
    fontFamily: theme.font.mono,
    display: 'flex',
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
    pointerEvents: 'none',
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

  /* Sidebar */
  sidebar: {
    position: 'fixed',
    top: 0,
    left: 0,
    height: '100dvh',
    zIndex: 100,
    display: 'flex',
    flexDirection: 'column',
    background: 'rgba(6, 7, 10, 0.96)',
    borderRight: `1px solid ${theme.colors.border}`,
    backdropFilter: 'blur(16px) saturate(1.2)',
    WebkitBackdropFilter: 'blur(16px) saturate(1.2)',
    transition: 'width 0.35s cubic-bezier(0.4, 0, 0.2, 1)',
    overflow: 'hidden',
    boxShadow: '4px 0 24px rgba(0, 0, 0, 0.5), 0 0 40px rgba(88, 184, 255, 0.04)',
  },
  sidebarHeader: {
    display: 'flex',
    alignItems: 'center',
    gap: '10px',
    padding: '10px',
    borderBottom: `1px solid ${theme.colors.border}`,
    flexShrink: 0,
    minHeight: '56px',
  },
  sidebarBrand: {
    fontSize: '12px',
    fontWeight: 700,
    letterSpacing: '0.14em',
    color: theme.colors.primary,
    textShadow: `0 0 16px ${theme.colors.primaryDim}`,
    whiteSpace: 'nowrap',
    overflow: 'hidden',
  },
  sidebarNav: {
    display: 'flex',
    flexDirection: 'column',
    gap: '2px',
    padding: '8px 6px',
    flex: 1,
    overflowY: 'auto',
    overflowX: 'hidden',
  },
  navButton: {
    display: 'flex',
    alignItems: 'center',
    gap: '10px',
    padding: '8px 10px',
    border: '1px solid transparent',
    background: 'transparent',
    color: theme.colors.textDim,
    cursor: 'pointer',
    borderRadius: '6px',
    textAlign: 'left',
    fontFamily: theme.font.mono,
    transition: 'all 0.25s ease',
    whiteSpace: 'nowrap',
    overflow: 'hidden',
    minHeight: '40px',
  },
  navTextGroup: {
    display: 'flex',
    flexDirection: 'column',
    gap: '2px',
    overflow: 'hidden',
    minWidth: 0,
  },
  navLabel: {
    fontSize: '11px',
    letterSpacing: '0.12em',
    textTransform: 'uppercase' as const,
    fontWeight: 700,
  },
  navBlurb: {
    fontSize: '9px',
    color: theme.colors.textMuted,
    lineHeight: 1.3,
    overflow: 'hidden',
    textOverflow: 'ellipsis',
    whiteSpace: 'nowrap',
  },
  sidebarFooter: {
    padding: '8px 6px 10px',
    borderTop: `1px solid ${theme.colors.border}`,
    flexShrink: 0,
  },

  /* Main area */
  mainArea: {
    position: 'relative',
    zIndex: 1,
    display: 'flex',
    flexDirection: 'column',
    height: '100dvh',
    flex: 1,
    overflow: 'hidden',
    transition: 'margin-left 0.35s cubic-bezier(0.4, 0, 0.2, 1)',
  },
  topBar: {
    display: 'flex',
    justifyContent: 'space-between',
    flexWrap: 'wrap',
    gap: '10px',
    padding: '8px 14px',
    minHeight: '44px',
    flexShrink: 0,
    borderBottom: `1px solid ${theme.colors.border}`,
    background: 'rgba(6, 7, 10, 0.88)',
    backdropFilter: 'blur(8px)',
    zIndex: 10,
  },
  topBarLead: {
    display: 'flex',
    alignItems: 'center',
    flexWrap: 'wrap',
    minWidth: 0,
    flex: '1 1 360px',
  },
  eyebrow: {
    fontSize: '8px',
    letterSpacing: '0.18em',
    textTransform: 'uppercase' as const,
    color: theme.colors.primary,
    textShadow: `0 0 14px ${theme.colors.primaryDim}`,
    whiteSpace: 'nowrap',
    flexShrink: 0,
  },
  title: {
    fontSize: 'clamp(14px, 1vw, 17px)',
    fontWeight: 700,
    lineHeight: 1,
    letterSpacing: '-0.02em',
    color: theme.colors.text,
    minWidth: 0,
  },
  topBarRight: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'flex-end',
    flexWrap: 'wrap',
    gap: '14px',
    minWidth: 0,
    flex: '0 1 auto',
  },
  pageDesc: {
    fontSize: '10px',
    color: theme.colors.textDim,
    whiteSpace: 'normal',
    overflow: 'hidden',
    textOverflow: 'ellipsis',
    maxWidth: '280px',
    textAlign: 'right',
  },

  /* Summary rail */
  summaryRail: {
    display: 'grid',
    gap: '6px',
    padding: '6px 14px',
    flexShrink: 0,
    background: 'linear-gradient(180deg, rgba(6, 6, 7, 0.88), rgba(6, 6, 7, 0.72))',
    backdropFilter: 'blur(8px)',
    borderBottom: `1px solid ${theme.colors.border}`,
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
