import { useEffect, useRef, useState, type CSSProperties, type ReactNode } from 'react';
import { NAV_ITEMS, labelForNav, type PageId } from '../app/navigation';
import { GlobalStepStatus } from '../components/dashboard/GlobalStepStatus';
import { AnomalyBadge, SummaryCard, toneColor } from '../components/dashboard/UiPrimitives';
import SimControls from '../components/SimControls';
import { SatelliteFocusDropdown } from '../components/dashboard/SatelliteFocusControls';
import { useDashboard } from '../dashboard/DashboardContext';
import { theme } from '../styles/theme';

const SIDEBAR_EXPANDED = 336;
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
  isShort,
  children,
}: {
  pageId: PageId;
  navigate: (page: PageId) => void;
  isNarrow: boolean;
  isCompact: boolean;
  isShort: boolean;
  children: ReactNode;
}) {
  const { model, focusOrigin, focusSatFrom, reasoningLevel, selectedSatId, setAttentionTarget, setReasoningLevel, setSoundMode, soundMode, spotlightMode, setSpotlightMode } = useDashboard();
  const [sidebarOpen, setSidebarOpen] = useState(false);
  const [feedRecoveredVisible, setFeedRecoveredVisible] = useState(false);
  const snapshotTone = model.truthBanner.snapshotSeverity === 'critical'
    ? theme.colors.critical
    : model.truthBanner.snapshotSeverity === 'warning'
      ? theme.colors.warning
      : theme.colors.accent;
  const checklistTone = model.operatorChecklist.some(item => item.tone === 'critical')
    ? 'critical'
    : model.operatorChecklist.some(item => item.tone === 'warning')
      ? 'warning'
      : model.operatorChecklist.some(item => item.tone === 'accent')
        ? 'accent'
        : 'neutral';
  const checklistColor = toneColor(checklistTone);
  const previousSnapshotSeverityRef = useRef(model.truthBanner.snapshotSeverity);

  useEffect(() => {
    const previous = previousSnapshotSeverityRef.current;
    previousSnapshotSeverityRef.current = model.truthBanner.snapshotSeverity;
    if (model.truthBanner.snapshotSeverity === 'fresh' && previous !== 'fresh') {
      setFeedRecoveredVisible(true);
      const timeout = window.setTimeout(() => setFeedRecoveredVisible(false), 6000);
      return () => window.clearTimeout(timeout);
    }
    if (model.truthBanner.snapshotSeverity !== 'fresh') {
      setFeedRecoveredVisible(false);
    }
    return undefined;
  }, [model.truthBanner.snapshotSeverity]);

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

          <div style={styles.sidebarUtilityRail}>
            <button
              type="button"
              data-testid="sidebar-focus-rail-button"
              title={selectedSatId ? `Global focus pinned to ${selectedSatId}` : 'Global focus is in fleet overview'}
              aria-label={selectedSatId ? `Open sidebar utilities. Global focus pinned to ${selectedSatId}` : 'Open sidebar utilities. Global focus is in fleet overview'}
              onClick={() => setSidebarOpen(true)}
              style={{
                ...styles.sidebarUtilityRailButton,
                borderColor: selectedSatId ? `${theme.colors.accent}55` : theme.colors.border,
                background: selectedSatId ? 'linear-gradient(180deg, rgba(57, 217, 138, 0.14), rgba(8, 10, 14, 0.92))' : 'rgba(255,255,255,0.03)',
                boxShadow: selectedSatId ? `0 0 12px ${theme.colors.accent}18` : 'none',
              }}
            >
              <span style={{ ...styles.sidebarUtilityRailLabel, color: selectedSatId ? theme.colors.accent : theme.colors.textMuted }}>FCS</span>
              <strong style={{ ...styles.sidebarUtilityRailValue, color: selectedSatId ? theme.colors.accent : theme.colors.text }}>{selectedSatId ? 'PIN' : 'ALL'}</strong>
            </button>

            <button
              type="button"
              data-testid="sidebar-checklist-rail-button"
              title={model.operatorChecklist.length > 0 ? `${model.operatorChecklist.length} active operator checklist items` : 'No active operator checklist items'}
              aria-label={model.operatorChecklist.length > 0 ? `Open sidebar utilities. ${model.operatorChecklist.length} active operator checklist items` : 'Open sidebar utilities. No active operator checklist items'}
              onClick={() => setSidebarOpen(true)}
              style={{
                ...styles.sidebarUtilityRailButton,
                borderColor: model.operatorChecklist.length > 0 ? `${checklistColor}55` : theme.colors.border,
                background: model.operatorChecklist.length > 0 ? `linear-gradient(180deg, ${checklistColor}14, rgba(8, 10, 14, 0.92))` : 'rgba(255,255,255,0.03)',
                boxShadow: model.operatorChecklist.length > 0 ? `0 0 12px ${checklistColor}16` : 'none',
              }}
            >
              <span style={{ ...styles.sidebarUtilityRailLabel, color: model.operatorChecklist.length > 0 ? checklistColor : theme.colors.textMuted }}>OPS</span>
              <strong style={{ ...styles.sidebarUtilityRailValue, color: model.operatorChecklist.length > 0 ? checklistColor : theme.colors.text }}>{model.operatorChecklist.length}</strong>
            </button>
          </div>

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

          <div style={styles.sidebarOverlayScroll}>
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

            <div style={styles.sidebarUtilityStack}>
              <section data-testid="sidebar-global-focus-panel" style={styles.sidebarSectionPanel}>
                <div style={styles.sidebarSectionHeader}>
                  <div style={{ display: 'flex', flexDirection: 'column', gap: '3px', minWidth: 0 }}>
                    <span style={styles.sidebarSectionEyebrow}>Global Focus</span>
                    <strong data-testid="sidebar-global-focus-value" style={{ ...styles.sidebarSectionValue, color: selectedSatId ? theme.colors.accent : theme.colors.text }}>{selectedSatId ?? 'Fleet Overview'}</strong>
                  </div>
                  {selectedSatId ? (
                    <button
                      type="button"
                      aria-label="Return global focus to fleet overview"
                      onClick={() => focusSatFrom(null, null)}
                      style={{
                        ...styles.controlButton,
                        padding: '4px 7px',
                        fontSize: '8px',
                      }}
                    >
                      Auto
                    </button>
                  ) : (
                    <span style={styles.sidebarSectionBadge}>Auto</span>
                  )}
                </div>

                <SatelliteFocusDropdown
                  label="Global Focus"
                  satellites={model.satellites}
                  selectedSatId={selectedSatId}
                  onSelectSat={id => focusSatFrom(id, id ? { source: 'Global Focus Selector', detail: `Pinned ${id} from the sidebar focus console.` } : null)}
                  fleetLabel="Fleet Overview"
                  tone="accent"
                  variant="panel"
                  style={{ width: '100%' }}
                />

                <div style={styles.sidebarMetaGrid}>
                  <div style={styles.focusInfoTile}>
                    <span style={styles.focusInfoLabel}>Selection</span>
                    <strong style={{ ...styles.focusInfoValue, color: model.activeSatellite ? theme.colors.accent : theme.colors.text }}>{model.activeSatellite ? 'Pinned Vehicle' : 'Fleet Context'}</strong>
                    <span style={styles.focusInfoDetail}>{model.activeSatellite ? 'Shared focus stays synced across all route-level views.' : 'Aggregate-capable panels remain fleet-wide until a spacecraft is pinned.'}</span>
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

                <div style={styles.sidebarSectionNote}>
                  {focusOrigin && selectedSatId
                    ? `${focusOrigin.source}. ${focusOrigin.detail}`
                    : 'Use the sidebar focus console when you need to pin one spacecraft without giving up the main route canvas.'}
                </div>
              </section>

              <section data-testid="sidebar-operator-checklist-panel" style={styles.sidebarSectionPanel}>
                <div style={styles.sidebarSectionHeader}>
                  <div style={{ display: 'flex', flexDirection: 'column', gap: '3px', minWidth: 0 }}>
                    <span style={styles.sidebarSectionEyebrow}>Operation Checklist</span>
                    <strong style={{ ...styles.sidebarSectionValue, color: model.operatorChecklist.length > 0 ? checklistColor : theme.colors.text }}>{model.operatorChecklist.length > 0 ? `${model.operatorChecklist.length} active item${model.operatorChecklist.length === 1 ? '' : 's'}` : 'Nominal picture'}</strong>
                  </div>
                  <span style={{
                    ...styles.sidebarSectionBadge,
                    color: model.operatorChecklist.length > 0 ? checklistColor : theme.colors.textMuted,
                    borderColor: model.operatorChecklist.length > 0 ? `${checklistColor}44` : theme.colors.border,
                    background: model.operatorChecklist.length > 0 ? `${checklistColor}12` : 'rgba(255,255,255,0.03)',
                  }} data-testid="sidebar-operator-checklist-count">{model.operatorChecklist.length}</span>
                </div>

                {model.operatorChecklist.length > 0 ? (
                  <div style={styles.sidebarChecklistList}>
                    {model.operatorChecklist.map(item => {
                      const itemColor = toneColor(item.tone);

                      return (
                        <div
                          key={item.id}
                          style={{
                            ...styles.sidebarChecklistItem,
                            borderColor: item.tone === 'neutral' ? theme.colors.border : `${itemColor}44`,
                            background: item.tone === 'neutral'
                              ? 'linear-gradient(180deg, rgba(16, 18, 24, 0.84), rgba(8, 10, 14, 0.94))'
                              : `linear-gradient(180deg, ${itemColor}12, rgba(8, 10, 14, 0.94))`,
                          }}
                        >
                          <div style={styles.sidebarChecklistText}>
                            <strong style={{ color: item.tone === 'neutral' ? theme.colors.text : itemColor, fontSize: '11px', lineHeight: 1.35 }}>{item.label}</strong>
                            <span style={styles.sidebarChecklistDetail}>{item.detail}</span>
                          </div>
                          {item.actionLabel && item.actionPage ? (
                            <button
                              type="button"
                              onClick={() => {
                                setAttentionTarget(item.actionTarget ?? null);
                                if (item.actionPage) {
                                  navigate(item.actionPage);
                                }
                                setSidebarOpen(false);
                              }}
                              style={{
                                ...styles.controlButton,
                                alignSelf: 'flex-start',
                                padding: '4px 7px',
                                fontSize: '8px',
                                color: item.tone === 'neutral' ? theme.colors.primary : itemColor,
                                borderColor: item.tone === 'neutral' ? `${theme.colors.primary}44` : `${itemColor}44`,
                              }}
                            >
                              {item.actionLabel}
                            </button>
                          ) : null}
                        </div>
                      );
                    })}
                  </div>
                ) : (
                  <div style={styles.sidebarChecklistEmpty}>
                    No immediate actions. Keep the current route in view and advance the sim when you are ready.
                  </div>
                )}
              </section>
            </div>
          </div>

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
            testId="watch-target-summary-card"
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
            marginBottom: isShort ? '6px' : '8px',
          }}>
            {!isShort ? <div style={{
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
            </div> : null}

            <div style={styles.topControlRail}>
              <SimControls compact={isNarrow || isCompact} layout="cluster" />

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
          {!isShort ? <div style={{ color: theme.colors.textDim, fontSize: '10px', lineHeight: 1.45, marginBottom: '8px' }}>
            {model.truthBanner.snapshotSeverity !== 'fresh'
              ? model.truthBanner.snapshotDetail
              : reasoningLevel === 'minimal'
                ? 'Minimal keeps the core operator picture visible and trims secondary explanation rails until you ask for more.'
                : 'Detailed expands explanatory rails and comparison context while preserving the same backend-truthful state.'}
          </div> : null}
          {feedRecoveredVisible ? (
            <div style={{ marginBottom: '8px' }}>
              <div style={{ display: 'inline-flex', alignItems: 'center', gap: '8px', padding: '6px 9px', border: `1px solid ${theme.colors.accent}44`, background: 'rgba(57, 217, 138, 0.12)', color: theme.colors.accent, clipPath: theme.chamfer.buttonClipPath }}>
                <span style={{ fontSize: '8px', letterSpacing: '0.16em', textTransform: 'uppercase' }}>Feed Recovered</span>
                <span style={{ color: theme.colors.textDim, fontSize: '10px' }}>Snapshot cadence is back within the fresh window.</span>
              </div>
            </div>
          ) : null}
          {!isShort && (model.truthBanner.failOpenCount > 0 || model.truthBanner.droppedCount > 0 || model.truthBanner.uploadMissedCount > 0) ? (
            <div style={{ display: 'flex', flexWrap: 'wrap', gap: '6px', marginBottom: '8px' }}>
              {model.truthBanner.failOpenCount > 0 ? <AnomalyBadge label="Fail-open" value={`${model.truthBanner.failOpenCount}`} tone="warning" /> : null}
              {model.truthBanner.droppedCount > 0 ? <AnomalyBadge label="Dropped" value={`${model.truthBanner.droppedCount}`} tone="critical" /> : null}
              {model.truthBanner.uploadMissedCount > 0 ? <AnomalyBadge label="Upload Slips" value={`${model.truthBanner.uploadMissedCount}`} tone="warning" /> : null}
            </div>
          ) : null}
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
    flex: '0 0 auto',
    overflowY: 'visible',
    overflowX: 'hidden',
  },
  sidebarUtilityRail: {
    display: 'flex',
    flexDirection: 'column',
    gap: '6px',
    padding: '0 6px 10px',
    flexShrink: 0,
  },
  sidebarUtilityRailButton: {
    display: 'flex',
    flexDirection: 'column',
    alignItems: 'center',
    justifyContent: 'center',
    gap: '2px',
    minHeight: '48px',
    padding: '6px 4px',
    border: `1px solid ${theme.colors.border}`,
    background: 'rgba(255,255,255,0.03)',
    clipPath: theme.chamfer.buttonClipPath,
    cursor: 'pointer',
    fontFamily: theme.font.mono,
    transition: 'border-color 0.18s ease, box-shadow 0.18s ease, background 0.18s ease',
  },
  sidebarUtilityRailLabel: {
    fontSize: '8px',
    letterSpacing: '0.16em',
    textTransform: 'uppercase',
  },
  sidebarUtilityRailValue: {
    fontSize: '11px',
    lineHeight: 1,
    fontWeight: 700,
    letterSpacing: '0.08em',
  },
  sidebarOverlayScroll: {
    flex: 1,
    minHeight: 0,
    overflowY: 'auto',
    overflowX: 'hidden',
  },
  sidebarUtilityStack: {
    display: 'flex',
    flexDirection: 'column',
    gap: '10px',
    padding: '4px 10px 12px',
  },
  sidebarSectionPanel: {
    display: 'flex',
    flexDirection: 'column',
    gap: '10px',
    padding: '12px',
    border: `1px solid ${theme.colors.border}`,
    background: 'linear-gradient(180deg, rgba(13, 17, 24, 0.9), rgba(7, 10, 15, 0.96))',
    clipPath: theme.chamfer.buttonClipPath,
    boxShadow: 'inset 0 0 0 1px rgba(255,255,255,0.02)',
  },
  sidebarSectionHeader: {
    display: 'flex',
    alignItems: 'flex-start',
    justifyContent: 'space-between',
    gap: '8px',
  },
  sidebarSectionEyebrow: {
    color: theme.colors.textMuted,
    fontSize: '8px',
    letterSpacing: '0.16em',
    textTransform: 'uppercase',
  },
  sidebarSectionValue: {
    fontSize: '13px',
    lineHeight: 1.25,
    fontWeight: 700,
  },
  sidebarSectionBadge: {
    display: 'inline-flex',
    alignItems: 'center',
    justifyContent: 'center',
    minWidth: '36px',
    minHeight: '22px',
    padding: '0 8px',
    border: `1px solid ${theme.colors.border}`,
    background: 'rgba(255,255,255,0.03)',
    color: theme.colors.textMuted,
    clipPath: theme.chamfer.buttonClipPath,
    fontSize: '8px',
    fontWeight: 700,
    letterSpacing: '0.14em',
    textTransform: 'uppercase',
    flexShrink: 0,
  },
  sidebarMetaGrid: {
    display: 'grid',
    gridTemplateColumns: 'repeat(2, minmax(0, 1fr))',
    gap: '8px',
  },
  sidebarSectionNote: {
    color: theme.colors.textDim,
    fontSize: '10px',
    lineHeight: 1.5,
    paddingTop: '2px',
  },
  sidebarChecklistList: {
    display: 'flex',
    flexDirection: 'column',
    gap: '8px',
  },
  sidebarChecklistItem: {
    display: 'flex',
    flexDirection: 'column',
    gap: '8px',
    padding: '10px',
    border: `1px solid ${theme.colors.border}`,
    clipPath: theme.chamfer.buttonClipPath,
  },
  sidebarChecklistText: {
    display: 'flex',
    flexDirection: 'column',
    gap: '4px',
    minWidth: 0,
  },
  sidebarChecklistDetail: {
    color: theme.colors.textDim,
    fontSize: '10px',
    lineHeight: 1.45,
  },
  sidebarChecklistEmpty: {
    color: theme.colors.textDim,
    fontSize: '10px',
    lineHeight: 1.5,
    padding: '10px',
    border: `1px solid ${theme.colors.border}`,
    background: 'rgba(255,255,255,0.02)',
    clipPath: theme.chamfer.buttonClipPath,
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
