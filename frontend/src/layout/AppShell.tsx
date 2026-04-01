import { useState, type CSSProperties, type ReactNode } from 'react';
import { NAV_ITEMS, labelForNav, type PageId } from '../app/navigation';
import { GlobalStepStatus } from '../components/dashboard/GlobalStepStatus';
import { AnomalyBadge, SummaryCard } from '../components/dashboard/UiPrimitives';
import { SatelliteFocusDropdown } from '../components/dashboard/SatelliteFocusControls';
import { useDashboard } from '../dashboard/DashboardContext';
import { theme } from '../styles/theme';

const SIDEBAR_EXPANDED = 216;
const SIDEBAR_COLLAPSED = 52;

/* ─── Hamburger Icon ─── */
function HamburgerIcon({ open, onClick }: { open: boolean; onClick: () => void }) {
  const barBase: CSSProperties = {
    position: 'absolute',
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
        justifyContent: 'center',
        alignItems: 'center',
        position: 'relative',
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
      <span
        style={{
          ...barBase,
          transform: open ? 'rotate(45deg)' : 'translateY(-6px)',
        }}
      />
      <span
        style={{
          ...barBase,
          opacity: open ? 0 : 1,
          transform: open ? 'scaleX(0)' : 'translateY(0)',
        }}
      />
      <span
        style={{
          ...barBase,
          transform: open ? 'rotate(-45deg)' : 'translateY(6px)',
        }}
      />
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
    case 'evasion': return '◈';
    case 'fleet-status': return '◆';
    default: return '●';
  }
}

/* ─── AppShell ─── */
export function AppShell({
  pageId,
  navigate,
  isNarrow,
  isCompact,
  children,
}: {
  pageId: PageId;
  navigate: (page: PageId) => void;
  isNarrow: boolean;
  isCompact: boolean;
  children: ReactNode;
}) {
  const { model, focusOrigin, focusSatFrom, reasoningLevel, selectedSatId, setReasoningLevel, setSoundMode, soundMode, spotlightMode, setSpotlightMode } = useDashboard();
  const [sidebarOpen, setSidebarOpen] = useState(false);
  const [focusConsoleCollapsed, setFocusConsoleCollapsed] = useState(false);
  const compactFocusRail = isCompact || isNarrow;
  const snapshotTone = model.truthBanner.snapshotSeverity === 'critical'
    ? theme.colors.critical
    : model.truthBanner.snapshotSeverity === 'warning'
      ? theme.colors.warning
      : theme.colors.accent;

  return (
    <div style={styles.root}>
      <div style={styles.backgroundLayer} aria-hidden="true" />
      <div style={styles.scanlines} aria-hidden="true" />

      <button
        type="button"
        aria-label="Close navigation overlay"
        onClick={() => setSidebarOpen(false)}
        style={{
          ...styles.sidebarBackdrop,
          opacity: sidebarOpen ? 1 : 0,
          pointerEvents: sidebarOpen ? 'auto' : 'none',
        }}
      />

      {/* ── Sidebar ── */}
      <aside
        aria-label="Primary navigation sidebar"
        style={styles.sidebar}
      >
        <div style={styles.sidebarRail}>
          <div style={styles.sidebarHeaderRail}>
            <HamburgerIcon open={sidebarOpen} onClick={() => setSidebarOpen(prev => !prev)} />
          </div>

          <nav aria-label="Primary dashboard navigation" style={styles.sidebarNav}>
            {NAV_ITEMS.map(item => {
              const active = item.id === pageId;
              return (
                <button
                  key={item.id}
                  type="button"
                  onClick={() => navigate(item.id)}
                  aria-current={active ? 'page' : undefined}
                  title={labelForNav(item)}
                  style={{
                    ...styles.navButtonRail,
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
                </button>
              );
            })}
          </nav>

          <div style={styles.sidebarFooter}>
            <GlobalStepStatus compact />
          </div>
        </div>

        <div
          aria-hidden={!sidebarOpen}
          style={{
            ...styles.sidebarOverlayPanel,
            opacity: sidebarOpen ? 1 : 0,
            transform: sidebarOpen ? 'translateX(0)' : 'translateX(-18px)',
            pointerEvents: sidebarOpen ? 'auto' : 'none',
          }}
        >
          <div style={styles.sidebarHeaderExpanded}>
            <HamburgerIcon open={sidebarOpen} onClick={() => setSidebarOpen(false)} />
            <span style={styles.sidebarBrand}>C.A.S.C.A.D.E</span>
          </div>

          <nav aria-label="Primary dashboard navigation overlay" style={styles.sidebarNavExpanded}>
            {NAV_ITEMS.map(item => {
              const active = item.id === pageId;
              return (
                <button
                  key={item.id}
                  type="button"
                  onClick={() => { navigate(item.id); if (isCompact) setSidebarOpen(false); }}
                  aria-current={active ? 'page' : undefined}
                  style={{
                    ...styles.navButtonExpanded,
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
                  <div style={styles.navTextGroup}>
                    <span style={styles.navLabel}>{labelForNav(item)}</span>
                    <span style={styles.navBlurb}>{item.blurb}</span>
                  </div>
                </button>
              );
            })}
          </nav>

          <div style={styles.sidebarFooterExpanded}>
            <GlobalStepStatus />
          </div>
        </div>
      </aside>

      {/* ── Main Content Area ── */}
      <div
        style={{
          ...styles.mainArea,
          marginLeft: `${SIDEBAR_COLLAPSED}px`,
        }}
      >
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
          <SummaryCard
            label="Watch Target"
            value={model.watchTargetValue}
            detail={model.watchTargetDetail}
            tone={model.activeSatellite ? 'accent' : 'neutral'}
          />
          <SummaryCard label="Threat Index" value={model.threatValue} detail={model.threatDetail} tone={model.threatCounts.red > 0 ? 'critical' : model.threatCounts.yellow > 0 ? 'warning' : 'accent'} />
          <SummaryCard label="Burn Queue" value={model.burnValue} detail={model.burnDetail} tone={model.watchedPendingBurns.length > 0 ? 'warning' : 'accent'} />
          <SummaryCard label="Resource Posture" value={model.resourceValue} detail={model.resourceDetail} tone={model.lowestFuelSatellite && model.lowestFuelSatellite.fuel_kg < 10 ? 'critical' : 'warning'} />
        </div>

        <div style={{
          padding: isCompact ? '8px 8px 10px' : '8px 14px 12px',
          borderBottom: `1px solid ${theme.colors.border}`,
          background: 'linear-gradient(180deg, rgba(7, 9, 12, 0.92), rgba(5, 7, 10, 0.78))',
          backdropFilter: 'blur(10px)',
          flexShrink: 0,
          position: 'sticky',
          top: 0,
          zIndex: 30,
        }}>
          <div style={{
            display: 'grid',
            gridTemplateColumns: isNarrow ? '1fr' : 'minmax(0, 1fr) auto',
            gap: '8px',
            alignItems: 'stretch',
            marginBottom: '8px',
          }}>
            <div style={{
              ...styles.truthBanner,
              borderColor: model.truthBanner.snapshotSeverity === 'fresh' ? theme.colors.border : `${snapshotTone}55`,
              boxShadow: model.truthBanner.snapshotSeverity === 'fresh' ? 'none' : `0 0 18px ${snapshotTone}14`,
            }}>
              <div style={styles.truthBannerGroup}>
                <span style={styles.truthBannerLabel}>Snapshot</span>
                <strong style={{ ...styles.truthBannerValue, color: snapshotTone }}>{model.truthBanner.snapshotAgeLabel}</strong>
              </div>
              <div style={styles.truthBannerGroup}>
                <span style={styles.truthBannerLabel}>Conjunctions</span>
                <strong style={styles.truthBannerValue}>{model.truthBanner.conjunctionSourceLabel}</strong>
              </div>
              <div style={styles.truthBannerGroup}>
                <span style={styles.truthBannerLabel}>Fail-open</span>
                <strong style={{ ...styles.truthBannerValue, color: model.truthBanner.failOpenCount > 0 ? theme.colors.warning : theme.colors.text }}>{model.truthBanner.failOpenCount}</strong>
              </div>
              <div style={styles.truthBannerGroup}>
                <span style={styles.truthBannerLabel}>Dropped</span>
                <strong style={{ ...styles.truthBannerValue, color: model.truthBanner.droppedCount > 0 ? theme.colors.critical : theme.colors.text }}>{model.truthBanner.droppedCount}</strong>
              </div>
              <div style={styles.truthBannerGroup}>
                <span style={styles.truthBannerLabel}>Upload Slips</span>
                <strong style={{ ...styles.truthBannerValue, color: model.truthBanner.uploadMissedCount > 0 ? theme.colors.warning : theme.colors.text }}>{model.truthBanner.uploadMissedCount}</strong>
              </div>
              {model.truthBanner.snapshotSeverity !== 'fresh' ? (
                <div style={{ ...styles.truthBannerGroup, marginLeft: 'auto' }}>
                  <span style={{ ...styles.truthBannerLabel, color: snapshotTone }}>Freshness</span>
                  <strong style={{ ...styles.truthBannerValue, color: snapshotTone }}>
                    {model.truthBanner.snapshotSeverity === 'critical' ? 'Pause Decisions' : 'Verify Feed'}
                  </strong>
                </div>
              ) : null}
            </div>

            <div style={styles.topControlRail}>
              <div style={styles.controlCluster}>
                <span style={styles.controlLabel}>Reasoning</span>
                <button
                  type="button"
                  onClick={() => setReasoningLevel('minimal')}
                  style={{
                    ...styles.controlButton,
                    ...(reasoningLevel === 'minimal' ? styles.controlButtonActive : {}),
                  }}
                >
                  Minimal
                </button>
                <button
                  type="button"
                  onClick={() => setReasoningLevel('detailed')}
                  style={{
                    ...styles.controlButton,
                    ...(reasoningLevel === 'detailed' ? styles.controlButtonActive : {}),
                  }}
                >
                  Detailed
                </button>
              </div>

              <div style={styles.controlCluster}>
                <span style={styles.controlLabel}>Spotlight</span>
                <button
                  type="button"
                  onClick={() => setSpotlightMode(!spotlightMode)}
                  style={{
                    ...styles.controlButton,
                    ...(spotlightMode ? { ...styles.controlButtonActive, borderColor: `${theme.colors.warning}55`, color: theme.colors.warning } : {}),
                  }}
                >
                  {spotlightMode ? 'On' : 'Off'}
                </button>
              </div>

              <div style={styles.controlCluster}>
                <span style={styles.controlLabel}>Audio</span>
                <button
                  type="button"
                  onClick={() => setSoundMode('muted')}
                  style={{
                    ...styles.controlButton,
                    ...(soundMode === 'muted' ? styles.controlButtonActive : {}),
                  }}
                >
                  Muted
                </button>
                <button
                  type="button"
                  onClick={() => setSoundMode('alerts')}
                  style={{
                    ...styles.controlButton,
                    ...(soundMode === 'alerts' ? styles.controlButtonActive : {}),
                  }}
                >
                  Alerts
                </button>
                <button
                  type="button"
                  onClick={() => setSoundMode('full')}
                  style={{
                    ...styles.controlButton,
                    ...(soundMode === 'full' ? styles.controlButtonActive : {}),
                  }}
                >
                  Full
                </button>
              </div>
            </div>
          </div>
          <div style={{ color: theme.colors.textDim, fontSize: '10px', lineHeight: 1.45, marginBottom: '8px' }}>
            {model.truthBanner.snapshotSeverity !== 'fresh'
              ? model.truthBanner.snapshotDetail
              : reasoningLevel === 'minimal'
                ? 'Minimal keeps the core operator picture visible and trims secondary explanation rails until you ask for more.'
                : 'Detailed expands explanatory rails and comparison context while preserving the same backend-truthful state.'}
          </div>
          {model.truthBanner.failOpenCount > 0 || model.truthBanner.droppedCount > 0 || model.truthBanner.uploadMissedCount > 0 ? (
            <div style={{ display: 'flex', flexWrap: 'wrap', gap: '6px', marginBottom: '8px' }}>
              {model.truthBanner.failOpenCount > 0 ? <AnomalyBadge label="Fail-open" value={`${model.truthBanner.failOpenCount}`} tone="warning" /> : null}
              {model.truthBanner.droppedCount > 0 ? <AnomalyBadge label="Dropped" value={`${model.truthBanner.droppedCount}`} tone="critical" /> : null}
              {model.truthBanner.uploadMissedCount > 0 ? <AnomalyBadge label="Upload Slips" value={`${model.truthBanner.uploadMissedCount}`} tone="warning" /> : null}
            </div>
          ) : null}
          {reasoningLevel === 'minimal' && model.operatorChecklist.length > 0 ? (
            <div style={{ display: 'flex', flexDirection: 'column', gap: '6px', marginBottom: '8px' }}>
              <span style={{ color: theme.colors.textMuted, fontSize: '8px', letterSpacing: '0.16em', textTransform: 'uppercase' }}>Operator Checklist</span>
              <div style={{ display: 'flex', flexWrap: 'wrap', gap: '6px' }}>
                {model.operatorChecklist.map(item => (
                  <div key={item.id} style={{ display: 'inline-flex', alignItems: 'center', gap: '8px', padding: '6px 9px', border: `1px solid ${item.tone === 'critical' ? `${theme.colors.critical}44` : item.tone === 'warning' ? `${theme.colors.warning}44` : item.tone === 'accent' ? `${theme.colors.accent}44` : theme.colors.border}`, background: 'rgba(8, 10, 14, 0.72)', clipPath: theme.chamfer.buttonClipPath }}>
                    <strong style={{ color: item.tone === 'critical' ? theme.colors.critical : item.tone === 'warning' ? theme.colors.warning : item.tone === 'accent' ? theme.colors.accent : theme.colors.text, fontSize: '10px' }}>{item.label}</strong>
                    <span style={{ color: theme.colors.textDim, fontSize: '10px' }}>{item.detail}</span>
                  </div>
                ))}
              </div>
            </div>
          ) : null}
          {focusOrigin && selectedSatId ? (
            <div style={{ marginBottom: '8px' }}>
              <div style={{ display: 'inline-flex', alignItems: 'center', gap: '8px', padding: '6px 9px', border: `1px solid ${theme.colors.border}`, background: 'rgba(8, 10, 14, 0.72)', clipPath: theme.chamfer.buttonClipPath }}>
                <span style={{ color: theme.colors.textMuted, fontSize: '8px', letterSpacing: '0.16em', textTransform: 'uppercase' }}>Focus Origin</span>
                <strong style={{ color: theme.colors.primary, fontSize: '10px' }}>{focusOrigin.source}</strong>
                <span style={{ color: theme.colors.textDim, fontSize: '10px' }}>{focusOrigin.detail}</span>
              </div>
            </div>
          ) : null}

          {compactFocusRail ? (
            <div style={{ display: 'flex', justifyContent: 'flex-end', marginBottom: focusConsoleCollapsed ? '0' : '8px' }}>
              <button
                type="button"
                onClick={() => setFocusConsoleCollapsed(value => !value)}
                style={{
                  padding: '6px 9px',
                  border: `1px solid ${theme.colors.border}`,
                  background: 'rgba(255,255,255,0.03)',
                  color: theme.colors.textDim,
                  clipPath: theme.chamfer.buttonClipPath,
                  cursor: 'pointer',
                  fontFamily: theme.font.mono,
                  fontSize: '9px',
                  letterSpacing: '0.14em',
                  textTransform: 'uppercase',
                }}
              >
                {focusConsoleCollapsed ? 'Show Focus Console' : 'Collapse Focus Console'}
              </button>
            </div>
          ) : null}
          {!focusConsoleCollapsed ? (
          <div
            data-testid="watch-target-card"
            style={{
              display: 'grid',
              gridTemplateColumns: isNarrow ? '1fr' : 'minmax(260px, 360px) minmax(0, 1fr)',
              gap: '10px',
              alignItems: 'stretch',
            }}
          >
            <SatelliteFocusDropdown
              label="Global Satellite Focus"
              satellites={model.satellites}
              selectedSatId={selectedSatId}
              onSelectSat={id => focusSatFrom(id, id ? { source: 'Global Focus Selector', detail: `Pinned ${id} from the shared watch console.` } : null)}
              fleetLabel="Fleet Overview"
              tone="accent"
              variant="hero"
            />
            <div style={{
              display: 'grid',
              gridTemplateColumns: isCompact ? '1fr 1fr' : 'repeat(4, minmax(0, 1fr))',
              gap: '8px',
              minWidth: 0,
            }}>
              <div style={styles.focusInfoTile}>
                <span style={styles.focusInfoLabel}>Selection State</span>
                <strong style={{ ...styles.focusInfoValue, color: model.activeSatellite ? theme.colors.accent : theme.colors.text }}>{model.activeSatellite ? 'Pinned Vehicle' : 'Fleet Context'}</strong>
                <span style={styles.focusInfoDetail}>{model.activeSatellite ? 'Shared focus flows through Command, Track, Threat, Burn Ops, and Evasion.' : 'Aggregate-capable views stay active until a spacecraft is explicitly pinned.'}</span>
              </div>
              <div style={styles.focusInfoTile}>
                <span style={styles.focusInfoLabel}>Vehicle Status</span>
                <strong style={{ ...styles.focusInfoValue, color: model.activeSatellite ? theme.colors.primary : theme.colors.textDim }}>{model.activeSatellite?.status ?? 'Fleet Overview'}</strong>
                <span style={styles.focusInfoDetail}>{model.activeSatellite ? `${model.activeSatellite.fuel_kg.toFixed(1)} kg fuel remaining` : `${model.statusCounts.nominal} nominal / ${model.statusCounts.maneuvering} maneuvering / ${model.statusCounts.degraded} degraded`}</span>
              </div>
              <div style={styles.focusInfoTile}>
                <span style={styles.focusInfoLabel}>Threat Scope</span>
                <strong style={{ ...styles.focusInfoValue, color: model.threatCounts.red > 0 ? theme.colors.critical : theme.colors.warning }}>{model.threatValue}</strong>
                <span style={styles.focusInfoDetail}>{model.threatDetail}</span>
              </div>
              <div style={styles.focusInfoTile}>
                <span style={styles.focusInfoLabel}>Burn Scope</span>
                <strong style={{ ...styles.focusInfoValue, color: model.watchedPendingBurns.length > 0 ? theme.colors.warning : theme.colors.primary }}>{model.burnValue}</strong>
                <span style={styles.focusInfoDetail}>{model.burnDetail}</span>
              </div>
            </div>
          </div>
          ) : (
            <div data-testid="watch-target-card" style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: '10px', padding: '8px 10px', border: `1px solid ${theme.colors.border}`, background: 'rgba(8, 10, 14, 0.74)', clipPath: theme.chamfer.buttonClipPath }}>
              <div style={{ display: 'flex', flexDirection: 'column', gap: '3px', minWidth: 0 }}>
                <span style={{ color: theme.colors.textMuted, fontSize: '8px', letterSpacing: '0.16em', textTransform: 'uppercase' }}>Global Focus</span>
                <strong style={{ color: selectedSatId ? theme.colors.accent : theme.colors.text, fontSize: '13px', lineHeight: 1.1 }}>{selectedSatId ?? 'Fleet Overview'}</strong>
              </div>
              <span style={{ color: theme.colors.textDim, fontSize: '10px', lineHeight: 1.4, textAlign: 'right' }}>{selectedSatId ? 'Pinned across all pages' : 'Fleet-wide context active'}</span>
            </div>
          )}
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
    width: `${SIDEBAR_COLLAPSED}px`,
    zIndex: 100,
    overflow: 'visible',
  },
  sidebarBackdrop: {
    position: 'fixed',
    inset: 0,
    zIndex: 90,
    border: 'none',
    background: 'linear-gradient(90deg, rgba(5, 9, 16, 0.48), rgba(3, 6, 11, 0.34) 38%, rgba(2, 4, 8, 0.18) 100%)',
    backdropFilter: 'blur(4px) saturate(0.86) brightness(0.72)',
    WebkitBackdropFilter: 'blur(4px) saturate(0.86) brightness(0.72)',
    cursor: 'pointer',
    padding: 0,
    transition: 'opacity 0.22s ease',
  },
  sidebarRail: {
    width: `${SIDEBAR_COLLAPSED}px`,
    height: '100dvh',
    display: 'flex',
    flexDirection: 'column',
    background: 'linear-gradient(180deg, rgba(8, 12, 18, 0.72), rgba(5, 8, 13, 0.62))',
    borderRight: '1px solid rgba(140, 190, 255, 0.18)',
    borderLeft: '1px solid rgba(255, 255, 255, 0.05)',
    backdropFilter: 'blur(20px) saturate(1.18)',
    WebkitBackdropFilter: 'blur(20px) saturate(1.18)',
    overflow: 'hidden',
    boxShadow: '6px 0 30px rgba(0, 0, 0, 0.28), inset -1px 0 0 rgba(255, 255, 255, 0.035), inset 0 1px 0 rgba(255, 255, 255, 0.04)',
  },
  sidebarOverlayPanel: {
    position: 'absolute',
    top: 0,
    left: 0,
    width: `${SIDEBAR_EXPANDED}px`,
    height: '100dvh',
    display: 'flex',
    flexDirection: 'column',
    background: 'linear-gradient(180deg, rgba(10, 15, 24, 0.78), rgba(5, 8, 14, 0.72))',
    borderRight: '1px solid rgba(140, 190, 255, 0.2)',
    borderLeft: '1px solid rgba(255, 255, 255, 0.05)',
    backdropFilter: 'blur(24px) saturate(1.2)',
    WebkitBackdropFilter: 'blur(24px) saturate(1.2)',
    overflow: 'hidden',
    boxShadow: '18px 0 42px rgba(0, 0, 0, 0.34), 0 0 44px rgba(88, 184, 255, 0.05), inset -1px 0 0 rgba(255, 255, 255, 0.04), inset 0 1px 0 rgba(255, 255, 255, 0.05)',
    transition: 'transform 0.26s cubic-bezier(0.22, 1, 0.36, 1), opacity 0.2s ease',
    willChange: 'transform, opacity',
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
  sidebarHeaderRail: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    padding: '10px',
    borderBottom: '1px solid rgba(140, 190, 255, 0.14)',
    flexShrink: 0,
    minHeight: '56px',
    background: 'linear-gradient(180deg, rgba(255, 255, 255, 0.03), rgba(255, 255, 255, 0.01))',
  },
  sidebarHeaderExpanded: {
    display: 'flex',
    alignItems: 'center',
    gap: '10px',
    padding: '10px',
    borderBottom: '1px solid rgba(140, 190, 255, 0.16)',
    flexShrink: 0,
    minHeight: '56px',
    background: 'linear-gradient(180deg, rgba(255, 255, 255, 0.04), rgba(255, 255, 255, 0.015))',
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
  sidebarNavExpanded: {
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
  navButtonRail: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    padding: '8px 10px',
    border: '1px solid transparent',
    background: 'transparent',
    color: theme.colors.textDim,
    cursor: 'pointer',
    borderRadius: '6px',
    fontFamily: theme.font.mono,
    transition: 'all 0.25s ease',
    overflow: 'hidden',
    minHeight: '40px',
  },
  navButtonExpanded: {
    display: 'flex',
    alignItems: 'flex-start',
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
    overflow: 'hidden',
    minHeight: '56px',
  },
  navTextGroup: {
    display: 'flex',
    flexDirection: 'column',
    gap: '3px',
    minWidth: 0,
    flex: 1,
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
    lineHeight: 1.4,
    whiteSpace: 'normal',
    overflowWrap: 'anywhere',
  },
  sidebarFooter: {
    padding: '8px 6px 10px',
    borderTop: '1px solid rgba(140, 190, 255, 0.14)',
    flexShrink: 0,
    background: 'linear-gradient(0deg, rgba(255, 255, 255, 0.03), rgba(255, 255, 255, 0.01))',
  },
  sidebarFooterExpanded: {
    padding: '8px 6px 10px',
    borderTop: '1px solid rgba(140, 190, 255, 0.16)',
    flexShrink: 0,
    background: 'linear-gradient(0deg, rgba(255, 255, 255, 0.035), rgba(255, 255, 255, 0.012))',
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
  focusInfoTile: {
    display: 'flex',
    flexDirection: 'column',
    gap: '4px',
    minWidth: 0,
    padding: '10px 12px',
    border: `1px solid ${theme.colors.border}`,
    background: 'linear-gradient(180deg, rgba(15, 18, 24, 0.82), rgba(8, 10, 14, 0.92))',
    clipPath: theme.chamfer.buttonClipPath,
  },
  focusInfoLabel: {
    color: theme.colors.textMuted,
    fontSize: '8px',
    letterSpacing: '0.16em',
    textTransform: 'uppercase',
  },
  focusInfoValue: {
    fontSize: '14px',
    lineHeight: 1.1,
    fontWeight: 700,
    fontVariantNumeric: 'tabular-nums',
  },
  focusInfoDetail: {
    color: theme.colors.textDim,
    fontSize: '10px',
    lineHeight: 1.45,
  },
  truthBanner: {
    display: 'flex',
    flexWrap: 'wrap',
    gap: '10px',
    alignItems: 'center',
    padding: '8px 10px',
    border: `1px solid ${theme.colors.border}`,
    background: 'linear-gradient(180deg, rgba(12, 14, 18, 0.86), rgba(7, 9, 12, 0.94))',
    clipPath: theme.chamfer.buttonClipPath,
  },
  truthBannerGroup: {
    display: 'flex',
    alignItems: 'baseline',
    gap: '6px',
  },
  truthBannerLabel: {
    color: theme.colors.textMuted,
    fontSize: '8px',
    letterSpacing: '0.16em',
    textTransform: 'uppercase',
  },
  truthBannerValue: {
    color: theme.colors.text,
    fontSize: '11px',
    fontWeight: 700,
  },
  topControlRail: {
    display: 'flex',
    flexWrap: 'wrap',
    gap: '8px',
    alignItems: 'center',
    justifyContent: 'flex-end',
  },
  controlCluster: {
    display: 'inline-flex',
    alignItems: 'center',
    gap: '6px',
    padding: '6px 8px',
    border: `1px solid ${theme.colors.border}`,
    background: 'rgba(8, 10, 14, 0.72)',
    clipPath: theme.chamfer.buttonClipPath,
  },
  controlLabel: {
    color: theme.colors.textMuted,
    fontSize: '8px',
    letterSpacing: '0.16em',
    textTransform: 'uppercase',
  },
  controlButton: {
    padding: '5px 8px',
    border: `1px solid ${theme.colors.border}`,
    background: 'rgba(255,255,255,0.03)',
    color: theme.colors.textDim,
    clipPath: theme.chamfer.buttonClipPath,
    cursor: 'pointer',
    fontFamily: theme.font.mono,
    fontSize: '9px',
    letterSpacing: '0.12em',
    textTransform: 'uppercase',
  },
  controlButtonActive: {
    borderColor: `${theme.colors.primary}55`,
    background: 'rgba(88, 184, 255, 0.14)',
    color: theme.colors.primary,
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
