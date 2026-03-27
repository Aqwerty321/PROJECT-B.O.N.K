import { riskLevelForEvent, type ConjunctionEvent } from '../../types/api';
import { useSound } from '../../hooks/useSound';
import { useDashboard } from '../../dashboard/DashboardContext';
import {
  hasActiveThreatSeverityFilter,
  threatFilterAllowsRiskLevel,
} from '../../types/dashboard';
import { InfoChip } from './UiPrimitives';

export function filterConjunctionsBySeverity(
  events: ConjunctionEvent[],
  filter: ReturnType<typeof useDashboard>['threatSeverityFilter'],
) {
  if (!hasActiveThreatSeverityFilter(filter)) {
    return [];
  }

  return events.filter(event => threatFilterAllowsRiskLevel(riskLevelForEvent(event), filter));
}

export function ThreatSeverityFilters({
  counts,
}: {
  counts: { red: number; yellow: number; green: number };
}) {
  const { play } = useSound();
  const { threatSeverityFilter, toggleThreatSeverity } = useDashboard();

  const toggle = (severity: 'critical' | 'warning' | 'watch') => {
    play('click');
    toggleThreatSeverity(severity);
  };

  return (
    <div role="group" aria-label="Threat severity filters" style={{ display: 'flex', flexWrap: 'wrap', gap: '6px', justifyContent: 'flex-start' }}>
      <InfoChip
        label="Critical"
        value={counts.red.toString()}
        tone="critical"
        active={threatSeverityFilter.critical}
        onClick={() => toggle('critical')}
      />
      <InfoChip
        label="Warning"
        value={counts.yellow.toString()}
        tone="warning"
        active={threatSeverityFilter.warning}
        onClick={() => toggle('warning')}
      />
      <InfoChip
        label="Watch"
        value={counts.green.toString()}
        tone="accent"
        active={threatSeverityFilter.watch}
        onClick={() => toggle('watch')}
      />
    </div>
  );
}
