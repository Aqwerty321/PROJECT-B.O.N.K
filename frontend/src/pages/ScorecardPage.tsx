import { GlassPanel } from '../components/GlassPanel';
import { DetailList, InfoChip, SectionHeader, SummaryCard, type Tone } from '../components/dashboard/UiPrimitives';
import { useDashboard } from '../dashboard/DashboardContext';
import { riskLevelForEvent } from '../types/api';
import { theme } from '../styles/theme';

type CriterionState = 'strong' | 'partial' | 'risk';

interface Criterion {
  id: string;
  label: string;
  weight: number;
  state: CriterionState;
  tone: Tone;
  value: string;
  detail: string;
  evidence: string;
}

function criterionTone(state: CriterionState): Tone {
  switch (state) {
    case 'strong':
      return 'accent';
    case 'partial':
      return 'warning';
    default:
      return 'critical';
  }
}

function criterionFactor(state: CriterionState): number {
  switch (state) {
    case 'strong':
      return 1.0;
    case 'partial':
      return 0.62;
    default:
      return 0.28;
  }
}

function formatPercent(value: number): string {
  if (!Number.isFinite(value)) return '--';
  return `${value.toFixed(0)}%`;
}

function formatMs(value: number | null): string {
  if (value === null || !Number.isFinite(value) || value <= 0) return '--';
  return `${value.toFixed(1)} ms`;
}

function formatKg(value: number | null | undefined): string {
  if (value == null || !Number.isFinite(value)) return '--';
  return `${value.toFixed(2)} kg`;
}

function CriterionRow({ criterion }: { criterion: Criterion }) {
  return (
    <div
      style={{
        display: 'grid',
        gridTemplateColumns: 'minmax(0, 150px) minmax(0, 90px) minmax(0, 110px) minmax(0, 1fr)',
        gap: '12px',
        padding: '10px 0',
        borderBottom: '1px solid rgba(255,255,255,0.05)',
        alignItems: 'start',
      }}
    >
      <div style={{ display: 'flex', flexDirection: 'column', gap: '4px' }}>
        <span style={{ color: theme.colors.text, fontSize: '12px', fontWeight: 700 }}>{criterion.label}</span>
        <span style={{ color: theme.colors.textMuted, fontSize: '10px', letterSpacing: '0.14em', textTransform: 'uppercase' }}>
          Weight {criterion.weight}%
        </span>
      </div>
      <span style={{ color: criterionTone(criterion.state) === 'accent' ? theme.colors.accent : criterionTone(criterion.state) === 'warning' ? theme.colors.warning : theme.colors.critical, fontSize: '12px', fontWeight: 700 }}>
        {criterion.value}
      </span>
      <span style={{ color: theme.colors.textDim, fontSize: '11px', lineHeight: 1.45 }}>{criterion.detail}</span>
      <span style={{ color: theme.colors.textMuted, fontSize: '11px', lineHeight: 1.5 }}>{criterion.evidence}</span>
    </div>
  );
}

export function ScorecardPage({ isNarrow, isCompact: _isCompact }: { isNarrow: boolean; isCompact: boolean }) {
  const { model } = useDashboard();

  const metrics = model.status?.internal_metrics;
  const propagation = metrics?.propagation_last_tick;
  const summary = model.burnSummary;

  const criticalCount = model.conjList.filter(event => riskLevelForEvent(event) === 'red').length;
  const predictedCount = metrics?.predictive_conjunction_count ?? model.conjList.length;
  const avoidedCount = summary?.collisions_avoided ?? 0;
  const droppedCount = summary?.burns_dropped ?? model.droppedBurns.length;
  const avgFuelKg = model.satellites.length > 0 ? model.avgFuelKg : null;
  const avoidanceFuelKg = summary?.avoidance_fuel_consumed_kg ?? null;
  const totalFuelKg = summary?.fuel_consumed_kg ?? null;
  const efficiencyRatio = avoidedCount / Math.max(avoidanceFuelKg ?? totalFuelKg ?? 1, 0.1);
  const nominalPct = model.satellites.length > 0
    ? (model.statusCounts.nominal / model.satellites.length) * 100
    : 0;
  const slotOutsideBox = propagation?.stationkeeping_outside_box ?? 0;
  const slotErrorMaxKm = propagation?.stationkeeping_slot_radius_error_max_km ?? 0;
  const stepLatencyMs = metrics?.command_latency_us?.step?.execution_us_mean
    ? metrics.command_latency_us.step.execution_us_mean / 1000
    : null;
  const telemetryLatencyMs = metrics?.command_latency_us?.telemetry?.execution_us_mean
    ? metrics.command_latency_us.telemetry.execution_us_mean / 1000
    : null;
  const scheduleLatencyMs = metrics?.command_latency_us?.schedule?.execution_us_mean
    ? metrics.command_latency_us.schedule.execution_us_mean / 1000
    : null;
  const queueRejected = metrics?.command_queue_rejected_total ?? 0;
  const queueTimeouts = metrics?.command_queue_timeout_total ?? 0;
  const queueCompleted = metrics?.command_queue_completed_total ?? 0;
  const queueDepth = metrics?.command_queue_depth ?? 0;
  const queueLimit = metrics?.command_queue_depth_limit ?? 0;
  const liveTrailPoints = model.selectedTrajectory?.trail.length ?? model.trajectory?.trail.length ?? 0;
  const livePredictionPoints = model.selectedTrajectory?.predicted.length ?? model.trajectory?.predicted.length ?? 0;
  const liveHistoryTracks = model.trackHistory.size;
  const livePayloadLabel = `${model.satellites.length} sats / ${model.debris.length.toLocaleString()} debris`;

  const criteria: Criterion[] = [
    {
      id: 'safety',
      label: 'Safety Score',
      weight: 25,
      state: avoidedCount >= 2 && droppedCount === 0
        ? 'strong'
        : avoidedCount > 0 || predictedCount > 0
          ? 'partial'
          : 'risk',
      tone: criterionTone(avoidedCount >= 2 && droppedCount === 0 ? 'strong' : avoidedCount > 0 || predictedCount > 0 ? 'partial' : 'risk'),
      value: `${avoidedCount} avoided`,
      detail: `${criticalCount} critical in current stream / ${droppedCount} dropped burns`,
      evidence: 'Live burn summary, predictive conjunction stream, and the documented ready-demo path in scripts/run_ready_demo.sh.',
    },
    {
      id: 'fuel',
      label: 'Fuel Efficiency',
      weight: 20,
      state: avoidedCount > 0 && efficiencyRatio >= 0.5 && (avgFuelKg ?? 0) > 35
        ? 'strong'
        : (avgFuelKg ?? 0) > 20
          ? 'partial'
          : 'risk',
      tone: criterionTone(avoidedCount > 0 && efficiencyRatio >= 0.5 && (avgFuelKg ?? 0) > 35 ? 'strong' : (avgFuelKg ?? 0) > 20 ? 'partial' : 'risk'),
      value: avoidedCount > 0 ? `${efficiencyRatio.toFixed(2)} avoided/kg` : formatKg(avoidanceFuelKg ?? totalFuelKg),
      detail: `${formatKg(avoidanceFuelKg)} avoidance fuel / ${formatKg(totalFuelKg)} total fuel`,
      evidence: 'Burn summary fuel totals, per-burn delta-v history, and the fleet average fuel posture shown in the dashboard model.',
    },
    {
      id: 'uptime',
      label: 'Constellation Uptime',
      weight: 15,
      state: model.satellites.length > 0 && slotOutsideBox === 0 && nominalPct >= 85
        ? 'strong'
        : model.satellites.length > 0 && nominalPct >= 60
          ? 'partial'
          : 'risk',
      tone: criterionTone(model.satellites.length > 0 && slotOutsideBox === 0 && nominalPct >= 85 ? 'strong' : model.satellites.length > 0 && nominalPct >= 60 ? 'partial' : 'risk'),
      value: formatPercent(nominalPct),
      detail: `${slotOutsideBox} outside box / worst drift ${slotErrorMaxKm.toFixed(2)} km`,
      evidence: 'Status counts, propagation_last_tick slot-integrity metrics, and live fleet status summaries.',
    },
    {
      id: 'speed',
      label: 'Algorithmic Speed',
      weight: 15,
      state: stepLatencyMs !== null && stepLatencyMs <= 150
        ? 'strong'
        : stepLatencyMs !== null && stepLatencyMs <= 500
          ? 'partial'
          : 'risk',
      tone: criterionTone(stepLatencyMs !== null && stepLatencyMs <= 150 ? 'strong' : stepLatencyMs !== null && stepLatencyMs <= 500 ? 'partial' : 'risk'),
      value: formatMs(stepLatencyMs),
      detail: `${formatMs(telemetryLatencyMs)} telemetry / ${formatMs(scheduleLatencyMs)} schedule`,
      evidence: `Live command latency metrics at ${model.status?.object_count ?? 0} tracked objects with status details enabled.`,
    },
    {
      id: 'visualization',
      label: 'UI/UX & Visualization',
      weight: 15,
      state: model.snapshot && model.debris.length >= 1000 && liveTrailPoints > 0 && livePredictionPoints > 0 && liveHistoryTracks > 0
        ? 'strong'
        : model.snapshot && (liveTrailPoints > 0 || livePredictionPoints > 0 || liveHistoryTracks > 0)
          ? 'partial'
          : 'risk',
      tone: criterionTone(model.snapshot && model.debris.length >= 1000 && liveTrailPoints > 0 && livePredictionPoints > 0 && liveHistoryTracks > 0 ? 'strong' : model.snapshot && (liveTrailPoints > 0 || livePredictionPoints > 0 || liveHistoryTracks > 0) ? 'partial' : 'risk'),
      value: livePayloadLabel,
      detail: `${liveHistoryTracks} cached tracks / ${liveTrailPoints} trail / ${livePredictionPoints} predicted`,
      evidence: 'Live snapshot payload, trajectory endpoint, track history cache, maneuver timeline, bullseye, and the added scorecard route itself.',
    },
    {
      id: 'quality',
      label: 'Code Quality & Logging',
      weight: 10,
      state: queueRejected === 0 && queueTimeouts === 0 && !model.statusError && !model.snapError && !model.burnsError
        ? 'strong'
        : queueTimeouts === 0 && !model.statusError
          ? 'partial'
          : 'risk',
      tone: criterionTone(queueRejected === 0 && queueTimeouts === 0 && !model.statusError && !model.snapError && !model.burnsError ? 'strong' : queueTimeouts === 0 && !model.statusError ? 'partial' : 'risk'),
      value: `${queueCompleted} cmds logged`,
      detail: `${queueRejected} rejected / ${queueTimeouts} timeout / depth ${queueDepth}/${queueLimit || '--'}`,
      evidence: 'Queue depth and completion counters, API polling health, contract tests, and the documented evidence index in docs/PS_EVIDENCE_INDEX.md.',
    },
  ];

  const weightedReadiness = criteria.reduce((sum, criterion) => {
    return sum + criterion.weight * criterionFactor(criterion.state);
  }, 0);
  const strongCount = criteria.filter(criterion => criterion.state === 'strong').length;
  const partialCount = criteria.filter(criterion => criterion.state === 'partial').length;
  const riskCount = criteria.filter(criterion => criterion.state === 'risk').length;

  const readinessTone: Tone = weightedReadiness >= 80 ? 'accent' : weightedReadiness >= 60 ? 'warning' : 'critical';
  const evidenceGapEntries = [
    {
      label: 'Technical Report',
      value: 'Package the architecture and numerical methods into a PDF-ready brief.',
      tone: 'warning' as const,
    },
    {
      label: 'Demo Video',
      value: 'Use docs/DEMO_STORYBOARD.md as the frozen under-5-minute speaking plan, then record the final run.',
      tone: 'warning' as const,
    },
    {
      label: 'FPS Proof',
      value: 'Run scripts/capture_fps_evidence.sh to regenerate a screenshot and JSON artifact for the dense UI path.',
      tone: 'warning' as const,
    },
    {
      label: 'Counterfactual',
      value: 'Run scripts/run_counterfactual_demo.sh to preserve the with-versus-without-intervention comparison.',
      tone: 'warning' as const,
    },
    {
      label: 'Docker Smoke',
      value: 'Promote the Docker boot and API smoke sequence into a release or CI gate.',
      tone: 'warning' as const,
    },
  ];

  return (
    <section aria-labelledby="scorecard-heading" style={{ display: 'flex', flexDirection: 'column', gap: '12px', minHeight: '100%', overflow: 'auto' }}>
      <SectionHeader
        kicker="Submission Deck"
        title="PS Section 7 Scorecard"
        description="Judge-facing evidence rollup across the weighted hackathon criteria. This is a readiness surface, not an official grader, and every line is backed by either a live metric or a documented proof path in the repository."
        aside={
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: '6px', justifyContent: 'flex-end' }}>
            <InfoChip label="Readiness" value={formatPercent(weightedReadiness)} tone={readinessTone} />
            <InfoChip label="Strong" value={strongCount.toString()} tone={strongCount > 0 ? 'accent' : 'neutral'} />
            <InfoChip label="Partial" value={partialCount.toString()} tone={partialCount > 0 ? 'warning' : 'neutral'} />
            <InfoChip label="Risk" value={riskCount.toString()} tone={riskCount > 0 ? 'critical' : 'neutral'} />
          </div>
        }
      />

      <div style={{ display: 'grid', gridTemplateColumns: isNarrow ? 'repeat(2, minmax(0, 1fr))' : 'repeat(5, minmax(0, 1fr))', gap: '10px' }}>
        <SummaryCard
          label="Weighted Readiness"
          value={formatPercent(weightedReadiness)}
          detail="Weighted against the PS.md evaluation table using strong/partial/risk evidence states."
          tone={readinessTone}
        />
        <SummaryCard
          label="Live Payload"
          value={livePayloadLabel}
          detail={`${predictedCount} conjunctions in evidence scope with live snapshot polling active.`}
          tone={model.snapshot ? 'primary' : 'critical'}
        />
        <SummaryCard
          label="Safety Proof"
          value={`${avoidedCount} avoids`}
          detail={`${criticalCount} critical in stream and ${droppedCount} dropped burns currently visible.`}
          tone={avoidedCount > 0 ? 'accent' : criticalCount > 0 ? 'warning' : 'neutral'}
        />
        <SummaryCard
          label="Latency Signal"
          value={formatMs(stepLatencyMs)}
          detail={`${formatMs(telemetryLatencyMs)} telemetry / ${formatMs(scheduleLatencyMs)} schedule mean execution.`}
          tone={stepLatencyMs !== null && stepLatencyMs <= 150 ? 'accent' : stepLatencyMs !== null ? 'warning' : 'neutral'}
        />
        <SummaryCard
          label="Evidence Gaps"
          value={evidenceGapEntries.length.toString()}
          detail="Open packaging items that still need explicit submission artifacts outside the live runtime."
          tone={evidenceGapEntries.length > 0 ? 'warning' : 'accent'}
        />
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: isNarrow ? '1fr' : 'minmax(0, 1.25fr) minmax(0, 1fr)', gap: '12px', minHeight: 0 }}>
        <GlassPanel
          title="WEIGHTED SCORECARD"
          priority="primary"
          accentColor={theme.colors.accent}
          style={{ minHeight: 0 }}
        >
          <div style={{ display: 'grid', gridTemplateColumns: isNarrow ? '1fr' : 'repeat(2, minmax(0, 1fr))', gap: '10px' }}>
            {criteria.map(criterion => (
              <SummaryCard
                key={criterion.id}
                label={`${criterion.label} · ${criterion.weight}%`}
                value={criterion.value}
                detail={criterion.detail}
                tone={criterion.tone}
              />
            ))}
          </div>
        </GlassPanel>

        <GlassPanel
          title="SUBMISSION GAPS"
          priority="secondary"
          accentColor={theme.colors.warning}
          style={{ minHeight: 0 }}
        >
          <div style={{ display: 'flex', flexDirection: 'column', gap: '10px' }}>
            <p style={{ color: theme.colors.textDim, fontSize: '12px', lineHeight: 1.55 }}>
              These are the remaining packaging items that still live outside the runtime. The repo-level commands and proof paths are collected in docs/PS_EVIDENCE_INDEX.md.
            </p>
            <DetailList entries={evidenceGapEntries} />
          </div>
        </GlassPanel>
      </div>

      <GlassPanel
        title="LIVE EVIDENCE MATRIX"
        priority="secondary"
        accentColor={theme.colors.primary}
        style={{ minHeight: 0 }}
      >
        <div style={{ display: 'flex', flexDirection: 'column', gap: '2px' }}>
          <div style={{ display: 'grid', gridTemplateColumns: 'minmax(0, 150px) minmax(0, 90px) minmax(0, 110px) minmax(0, 1fr)', gap: '12px', paddingBottom: '8px', borderBottom: '1px solid rgba(255,255,255,0.08)' }}>
            <span style={{ color: theme.colors.textMuted, fontSize: '10px', letterSpacing: '0.14em', textTransform: 'uppercase' }}>Criterion</span>
            <span style={{ color: theme.colors.textMuted, fontSize: '10px', letterSpacing: '0.14em', textTransform: 'uppercase' }}>Signal</span>
            <span style={{ color: theme.colors.textMuted, fontSize: '10px', letterSpacing: '0.14em', textTransform: 'uppercase' }}>Live read</span>
            <span style={{ color: theme.colors.textMuted, fontSize: '10px', letterSpacing: '0.14em', textTransform: 'uppercase' }}>Evidence path</span>
          </div>
          {criteria.map(criterion => (
            <CriterionRow key={criterion.id} criterion={criterion} />
          ))}
        </div>
      </GlassPanel>
    </section>
  );
}