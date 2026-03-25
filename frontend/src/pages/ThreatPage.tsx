import { useEffect, useMemo, useState } from 'react';
import { ConjunctionBullseye } from '../components/ConjunctionBullseye';
import { GlassPanel } from '../components/GlassPanel';
import { InfoChip, SectionHeader } from '../components/dashboard/UiPrimitives';
import { ConjunctionDetailCard } from '../components/threat/ConjunctionDetailCard';
import { ConjunctionEventList } from '../components/threat/ConjunctionEventList';
import { useDashboard } from '../dashboard/DashboardContext';
import { theme } from '../styles/theme';

export function ThreatPage({ isNarrow, isCompact: _isCompact }: { isNarrow: boolean; isCompact: boolean }) {
  const { model, selectedSatId, selectSat } = useDashboard();
  const [activeEventKey, setActiveEventKey] = useState<string | null>(null);

  const sortedEvents = useMemo(
    () => [...model.conjList].sort((a, b) => a.tca_epoch_s - b.tca_epoch_s),
    [model.conjList],
  );

  useEffect(() => {
    if (sortedEvents.length === 0) {
      setActiveEventKey(null);
      return;
    }

    const hasActive = activeEventKey && sortedEvents.some(event => `${event.satellite_id}-${event.debris_id}-${event.tca_epoch_s}` === activeEventKey);
    if (!hasActive) {
      const preferred = selectedSatId
        ? sortedEvents.find(event => event.satellite_id === selectedSatId)
        : sortedEvents[0];
      setActiveEventKey(preferred ? `${preferred.satellite_id}-${preferred.debris_id}-${preferred.tca_epoch_s}` : null);
    }
  }, [activeEventKey, selectedSatId, sortedEvents]);

  const activeEvent = sortedEvents.find(event => `${event.satellite_id}-${event.debris_id}-${event.tca_epoch_s}` === activeEventKey) ?? null;

  return (
    <section aria-labelledby="threat-heading" style={{ display: 'flex', flexDirection: 'column', gap: '12px', height: '100%' }}>
      <SectionHeader
        kicker="Threat Deck"
        title="Conjunction Watch"
        description="Relative proximity watch with event triage, encounter details, and globally synchronized spacecraft focus."
        aside={
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: '6px', justifyContent: 'flex-end' }}>
            <InfoChip label="Critical" value={model.threatCounts.red.toString()} tone="critical" />
            <InfoChip label="Warning" value={model.threatCounts.yellow.toString()} tone="warning" />
            <InfoChip label="Nominal" value={model.threatCounts.green.toString()} tone="accent" />
            <InfoChip label="Target" value={selectedSatId ?? 'Fleet'} tone={selectedSatId ? 'accent' : 'neutral'} />
          </div>
        }
      />

      <div style={{ display: 'grid', gridTemplateColumns: isNarrow ? '1fr' : 'minmax(0, 1.2fr) minmax(300px, 0.78fr)', gap: '14px', flex: 1, minHeight: 0 }}>
        <GlassPanel
          title="CONJUNCTION BULLSEYE"
          noPadding
          priority="primary"
          accentColor={theme.colors.primary}
          style={{ minHeight: 0 }}
        >
          <div style={{ display: 'flex', flexDirection: 'column', gap: '10px', flex: 1, minHeight: 0, padding: '10px 14px 14px' }}>
            <div style={{ display: 'flex', flexWrap: 'wrap', gap: '8px', justifyContent: 'space-between', alignItems: 'flex-start', flexShrink: 0 }}>
              <div style={{ display: 'flex', flexDirection: 'column', gap: '4px', maxWidth: '60ch' }}>
                <span style={{ color: theme.colors.primary, fontSize: '9px', letterSpacing: '0.16em', textTransform: 'uppercase' }}>Relative encounter radar</span>
                <p id="threat-heading" style={{ color: theme.colors.textDim, fontSize: '12px', lineHeight: 1.55 }}>
                  Center is the selected spacecraft, radial distance is TCA, angle is approach vector, risk color follows PS thresholds.
                </p>
              </div>
              <div style={{ display: 'flex', flexWrap: 'wrap', gap: '6px' }}>
                <InfoChip label="Next TCA" value={model.nextThreat ? `${new Date(model.nextThreat.tca).toISOString().slice(11, 19)} UTC` : 'None'} tone={model.nextThreat ? 'warning' : 'accent'} />
              </div>
            </div>

            <div style={{ flex: 1, minHeight: 0, overflow: 'hidden', clipPath: theme.chamfer.clipPath, border: '1px solid rgba(88, 184, 255, 0.34)', background: 'linear-gradient(180deg, rgba(10, 11, 14, 0.92), rgba(7, 8, 10, 0.98))' }}>
              <ConjunctionBullseye conjunctions={model.conjList} selectedSatId={selectedSatId} nowEpochS={model.nowEpochS} />
            </div>
          </div>
        </GlassPanel>

        <div style={{ display: 'flex', flexDirection: 'column', gap: '14px', minHeight: 0 }}>
          <GlassPanel
            title="ENCOUNTER WATCH LIST"
            noPadding
            priority="secondary"
            accentColor={theme.colors.warning}
            style={{ flex: 1, minHeight: 0 }}
          >
            <div style={{ display: 'flex', flexDirection: 'column', gap: '8px', flex: 1, minHeight: 0, padding: '10px 12px 12px', overflow: 'auto' }}>
              <p style={{ color: theme.colors.textDim, fontSize: '10px', lineHeight: 1.5, flexShrink: 0 }}>
                Choose an event to inspect details. Selecting updates the global spacecraft focus.
              </p>
              <ConjunctionEventList
                events={sortedEvents}
                activeEvent={activeEvent}
                onSelectEvent={event => {
                  setActiveEventKey(`${event.satellite_id}-${event.debris_id}-${event.tca_epoch_s}`);
                  selectSat(event.satellite_id);
                }}
              />
            </div>
          </GlassPanel>

          <GlassPanel
            title="ENCOUNTER DETAIL"
            noPadding
            priority="secondary"
            accentColor={theme.colors.accent}
            style={{ flex: 1, minHeight: 0 }}
          >
            <div style={{ padding: '10px 12px 12px', overflow: 'auto', flex: 1, minHeight: 0 }}>
              <ConjunctionDetailCard event={activeEvent} nowEpochS={model.nowEpochS} />
            </div>
          </GlassPanel>
        </div>
      </div>
    </section>
  );
}
