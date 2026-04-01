import type { BurnCounterfactualBranch, BurnCounterfactualResponse } from '../types/api';
import { theme } from '../styles/theme';
import { EmptyStatePanel, toneColor, type Tone } from './dashboard/UiPrimitives';

const COLLISION_THRESHOLD_KM = 0.1;
let counterfactualMotionInjected = false;

function ensureCounterfactualMotionStyles() {
  if (counterfactualMotionInjected || typeof document === 'undefined') return;
  counterfactualMotionInjected = true;
  const style = document.createElement('style');
  style.textContent = `
    @keyframes counterfactualPulse {
      0%, 100% { transform: translateX(-50%) scale(1); opacity: 0.95; }
      50% { transform: translateX(-50%) scale(1.08); opacity: 1; }
    }
    @keyframes counterfactualFlow {
      0% { background-position: 0% 50%; }
      100% { background-position: 100% 50%; }
    }
  `;
  document.head.appendChild(style);
}

function formatUtcTime(value?: string | null): string {
  if (!value) return '--';
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return '--';
  return `${date.toISOString().slice(11, 19)} UTC`;
}

function formatDistanceKm(value?: number | null): string {
  if (value == null || !Number.isFinite(value)) return '--';
  if (value < 1) return `${Math.round(value * 1000)} m`;
  return `${value.toFixed(2)} km`;
}

function formatSpeedKmS(value?: number | null): string {
  if (value == null || !Number.isFinite(value)) return '--';
  if (value < 1) return `${(value * 1000).toFixed(2)} m/s`;
  return `${value.toFixed(2)} km/s`;
}

function formatDeltaVMs(value?: number | null): string {
  if (value == null || !Number.isFinite(value)) return '--';
  return `${(value * 1000).toFixed(2)} m/s`;
}

function formatMassKg(value?: number | null): string {
  if (value == null || !Number.isFinite(value)) return '--';
  return `${value.toFixed(2)} kg`;
}

function branchTone(branch: BurnCounterfactualBranch): Tone {
  if (branch.fail_open) return 'warning';
  return branch.collision ? 'critical' : 'accent';
}

function branchStatus(branch: BurnCounterfactualBranch): string {
  if (branch.fail_open) return 'FAIL-OPEN';
  return branch.collision ? 'COLLISION' : 'CLEAR';
}

function scalePercent(value: number, maxValue: number): number {
  if (!Number.isFinite(value) || maxValue <= 0) return 0;
  return Math.min(100, Math.max(0, (value / maxValue) * 100));
}

function HeroMetric({
  label,
  value,
  detail,
  tone,
}: {
  label: string;
  value: string;
  detail: string;
  tone: Tone;
}) {
  const color = toneColor(tone);
  return (
    <div
      style={{
        display: 'flex',
        flexDirection: 'column',
        gap: '4px',
        minWidth: 0,
        padding: '10px 12px',
        border: `1px solid ${tone === 'neutral' ? theme.colors.border : `${color}44`}`,
        background: tone === 'neutral'
          ? 'rgba(9, 11, 14, 0.68)'
          : `linear-gradient(180deg, ${color}14, rgba(8, 9, 12, 0.86))`,
        clipPath: theme.chamfer.buttonClipPath,
      }}
    >
      <span style={{ color: theme.colors.textMuted, fontSize: '8px', letterSpacing: '0.16em', textTransform: 'uppercase' }}>{label}</span>
      <strong style={{ color, fontSize: '18px', lineHeight: 1.05 }}>{value}</strong>
      <span style={{ color: theme.colors.textDim, fontSize: '10px', lineHeight: 1.45 }}>{detail}</span>
    </div>
  );
}

function LegendChip({ label, value, tone }: { label: string; value: string; tone: Tone }) {
  const color = toneColor(tone);
  return (
    <div
      style={{
        display: 'inline-flex',
        alignItems: 'center',
        gap: '8px',
        padding: '6px 9px',
        border: `1px solid ${tone === 'neutral' ? theme.colors.border : `${color}44`}`,
        background: 'rgba(7, 9, 12, 0.72)',
        clipPath: theme.chamfer.buttonClipPath,
      }}
    >
      <span style={{ width: '8px', height: '8px', borderRadius: '999px', background: color, boxShadow: `0 0 10px ${color}66` }} />
      <span style={{ color: theme.colors.textMuted, fontSize: '9px', letterSpacing: '0.12em', textTransform: 'uppercase' }}>{label}</span>
      <strong style={{ color, fontSize: '11px' }}>{value}</strong>
    </div>
  );
}

function OutcomeBranchCard({
  label,
  branch,
  tone,
  detail,
}: {
  label: string;
  branch: BurnCounterfactualBranch;
  tone: Tone;
  detail: string;
}) {
  const color = toneColor(tone);

  return (
    <div
      style={{
        position: 'relative',
        display: 'flex',
        flexDirection: 'column',
        gap: '10px',
        padding: '12px 14px',
        border: `1px solid ${tone === 'neutral' ? theme.colors.border : `${color}4d`}`,
        background: `linear-gradient(180deg, ${color}12, rgba(7, 9, 12, 0.92))`,
        clipPath: theme.chamfer.clipPath,
        overflow: 'hidden',
      }}
    >
      <div aria-hidden="true" style={{ position: 'absolute', left: 0, top: 0, bottom: 0, width: '3px', background: color, boxShadow: `0 0 18px ${color}66` }} />
      <div style={{ display: 'flex', justifyContent: 'space-between', gap: '10px', alignItems: 'flex-start' }}>
        <div style={{ display: 'flex', flexDirection: 'column', gap: '4px' }}>
          <span style={{ color: theme.colors.textMuted, fontSize: '8px', letterSpacing: '0.16em', textTransform: 'uppercase' }}>{label}</span>
          <strong style={{ color: theme.colors.text, fontSize: '20px', lineHeight: 1.05 }}>{formatDistanceKm(branch.min_miss_distance_km)}</strong>
          <span style={{ color: theme.colors.textDim, fontSize: '10px', lineHeight: 1.45 }}>{detail}</span>
        </div>
        <span
          style={{
            padding: '5px 8px',
            border: `1px solid ${tone === 'neutral' ? theme.colors.border : `${color}55`}`,
            background: `linear-gradient(180deg, ${color}18, rgba(9, 11, 14, 0.95))`,
            color,
            fontSize: '9px',
            fontWeight: 700,
            letterSpacing: '0.14em',
            textTransform: 'uppercase',
            clipPath: theme.chamfer.buttonClipPath,
            whiteSpace: 'nowrap',
          }}
        >
          {branchStatus(branch)}
        </span>
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(2, minmax(0, 1fr))', gap: '8px' }}>
        <div style={{ display: 'flex', flexDirection: 'column', gap: '3px' }}>
          <span style={{ color: theme.colors.textMuted, fontSize: '8px', letterSpacing: '0.14em', textTransform: 'uppercase' }}>Closest pass</span>
          <span style={{ color, fontSize: '12px', fontWeight: 700 }}>{formatUtcTime(branch.min_epoch)}</span>
        </div>
        <div style={{ display: 'flex', flexDirection: 'column', gap: '3px' }}>
          <span style={{ color: theme.colors.textMuted, fontSize: '8px', letterSpacing: '0.14em', textTransform: 'uppercase' }}>Evaluation</span>
          <span style={{ color: branch.evaluated ? theme.colors.accent : theme.colors.warning, fontSize: '12px', fontWeight: 700 }}>
            {branch.evaluated ? 'Deterministic scan' : 'Scan unavailable'}
          </span>
        </div>
      </div>
    </div>
  );
}

function GeometryMiniPlot({
  label,
  branch,
  tone,
}: {
  label: string;
  branch: BurnCounterfactualBranch;
  tone: Tone;
}) {
  const color = toneColor(tone);
  const relX = branch.deb_pos_km[0] - branch.sat_pos_km[0];
  const relY = branch.deb_pos_km[1] - branch.sat_pos_km[1];
  const scale = Math.max(Math.abs(relX), Math.abs(relY), 0.04);
  const satX = 72;
  const satY = 58;
  const debX = satX + (relX / scale) * 26;
  const debY = satY - (relY / scale) * 26;

  return (
    <div
      style={{
        display: 'flex',
        flexDirection: 'column',
        gap: '8px',
        padding: '10px 12px',
        border: `1px solid ${tone === 'neutral' ? theme.colors.border : `${color}33`}`,
        background: 'rgba(7, 9, 12, 0.72)',
        clipPath: theme.chamfer.clipPath,
      }}
    >
      <div style={{ display: 'flex', justifyContent: 'space-between', gap: '8px', alignItems: 'baseline' }}>
        <span style={{ color, fontSize: '9px', letterSpacing: '0.16em', textTransform: 'uppercase' }}>{label}</span>
        <span style={{ color: theme.colors.textDim, fontSize: '10px' }}>{branchStatus(branch)}</span>
      </div>
      <svg viewBox="0 0 144 116" style={{ width: '100%', height: '116px', display: 'block', overflow: 'visible' }}>
        <defs>
          <radialGradient id={`sat-${label}`} cx="50%" cy="50%" r="60%">
            <stop offset="0%" stopColor="rgba(88, 184, 255, 0.92)" />
            <stop offset="100%" stopColor="rgba(88, 184, 255, 0.12)" />
          </radialGradient>
          <radialGradient id={`deb-${label}`} cx="50%" cy="50%" r="60%">
            <stop offset="0%" stopColor={color} />
            <stop offset="100%" stopColor="rgba(255, 98, 98, 0.12)" />
          </radialGradient>
        </defs>
        <rect x="0" y="0" width="144" height="116" rx="12" fill="rgba(4, 6, 10, 0.86)" stroke="rgba(255,255,255,0.05)" />
        <circle cx={satX} cy={satY} r="28" fill="none" stroke="rgba(88, 184, 255, 0.18)" />
        <circle cx={satX} cy={satY} r="18" fill="none" stroke="rgba(88, 184, 255, 0.12)" />
        <line x1={satX} y1={satY} x2={debX} y2={debY} stroke={color} strokeWidth="1.4" strokeDasharray="4 4" opacity="0.7" />
        <circle cx={satX} cy={satY} r="8" fill={`url(#sat-${label})`} stroke="rgba(88, 184, 255, 0.88)" />
        <circle cx={debX} cy={debY} r="7" fill={`url(#deb-${label})`} stroke={color} />
        <text x={satX} y="18" fill="rgba(88, 184, 255, 0.88)" fontSize="9" textAnchor="middle">SAT</text>
        <text x={debX} y={Math.max(14, debY - 10)} fill={color} fontSize="9" textAnchor="middle">DEB</text>
        <text x="72" y="106" fill="rgba(153, 169, 188, 0.82)" fontSize="9" textAnchor="middle">closest-pass geometry snapshot</text>
      </svg>
    </div>
  );
}

function DetailGroup({
  title,
  tone,
  rows,
}: {
  title: string;
  tone: Tone;
  rows: Array<{ label: string; value: string; valueTone?: Tone }>;
}) {
  const color = toneColor(tone);
  return (
    <div
      style={{
        display: 'flex',
        flexDirection: 'column',
        gap: '8px',
        padding: '10px 12px',
        border: `1px solid ${tone === 'neutral' ? theme.colors.border : `${color}33`}`,
        background: 'rgba(7, 9, 12, 0.72)',
        clipPath: theme.chamfer.clipPath,
      }}
    >
      <span style={{ color, fontSize: '9px', letterSpacing: '0.16em', textTransform: 'uppercase' }}>{title}</span>
      <div style={{ display: 'flex', flexDirection: 'column', gap: '6px' }}>
        {rows.map(row => (
          <div key={row.label} style={{ display: 'flex', justifyContent: 'space-between', gap: '10px', alignItems: 'baseline', borderBottom: '1px solid rgba(255,255,255,0.05)', paddingBottom: '6px' }}>
            <span style={{ color: theme.colors.textMuted, fontSize: '9px', letterSpacing: '0.12em', textTransform: 'uppercase' }}>{row.label}</span>
            <span style={{ color: row.valueTone ? toneColor(row.valueTone) : theme.colors.text, fontSize: '11px', fontWeight: 700, textAlign: 'right' }}>{row.value}</span>
          </div>
        ))}
      </div>
    </div>
  );
}

export function CounterfactualOutcomePanel({
  counterfactual,
  loading,
  error,
}: {
  counterfactual: BurnCounterfactualResponse | null;
  loading: boolean;
  error: string | null;
}) {
  if (loading) {
    return (
      <EmptyStatePanel
        title="Computing Counterfactual"
        detail="Comparing the executed burn against the no-burn branch from the stored execution snapshot."
      />
    );
  }

  if (error) {
    return (
      <EmptyStatePanel
        title="Counterfactual Unavailable"
        detail={error}
      />
    );
  }

  if (!counterfactual) {
    return (
      <EmptyStatePanel
        title="Counterfactual Pending"
        detail="Choose an eligible executed predictive burn to compare the actual outcome against a no-burn branch."
      />
    );
  }

  ensureCounterfactualMotionStyles();

  const actualTone = branchTone(counterfactual.actual);
  const withoutTone = branchTone(counterfactual.without_burn);
  const headlineTone: Tone = counterfactual.actual.fail_open || counterfactual.without_burn.fail_open
    ? 'warning'
    : counterfactual.delta.crossed_collision_threshold
      ? 'accent'
      : 'primary';
  const headlineColor = toneColor(headlineTone);
  const headline = counterfactual.actual.fail_open || counterfactual.without_burn.fail_open
    ? 'One branch required fail-open handling, so treat this compare as conservative rather than final.'
    : counterfactual.delta.crossed_collision_threshold
      ? 'This burn changed the branch outcome from collision to clear.'
      : 'This burn changed separation, even though both branches stayed on the same side of the threshold.';
  const headlineDetail = counterfactual.delta.crossed_collision_threshold
    ? `CASCADE bought ${formatDistanceKm(counterfactual.delta.clearance_gain_km)} of extra clearance for ${formatMassKg(counterfactual.delta.fuel_spent_kg)}.`
    : `Same execution snapshot, same encounter, ${formatDistanceKm(counterfactual.delta.clearance_gain_km)} of separation delta.`;
  const scaleMaxKm = Math.max(
    COLLISION_THRESHOLD_KM * 1.8,
    Math.max(
      counterfactual.actual.min_miss_distance_km,
      counterfactual.without_burn.min_miss_distance_km,
      counterfactual.trigger.predicted_miss_distance_km,
    ) * 1.18,
  );
  const actualPct = scalePercent(counterfactual.actual.min_miss_distance_km, scaleMaxKm);
  const withoutPct = scalePercent(counterfactual.without_burn.min_miss_distance_km, scaleMaxKm);
  const predictedPct = scalePercent(counterfactual.trigger.predicted_miss_distance_km, scaleMaxKm);
  const thresholdPct = scalePercent(COLLISION_THRESHOLD_KM, scaleMaxKm);
  const deltaLeftPct = Math.min(actualPct, withoutPct);
  const deltaWidthPct = Math.max(2, Math.abs(actualPct - withoutPct));

  const detailGroups = [
    {
      title: 'Trigger Geometry',
      tone: 'critical' as const,
      rows: [
        { label: 'Debris', value: counterfactual.trigger_debris_id, valueTone: 'critical' as const },
        { label: 'Predicted TCA', value: formatUtcTime(counterfactual.trigger.predicted_tca), valueTone: 'warning' as const },
        { label: 'Predicted Miss', value: formatDistanceKm(counterfactual.trigger.predicted_miss_distance_km), valueTone: 'critical' as const },
        { label: 'Approach Speed', value: formatSpeedKmS(counterfactual.trigger.approach_speed_km_s), valueTone: 'warning' as const },
      ],
    },
    {
      title: 'Executed Command',
      tone: 'primary' as const,
      rows: [
        { label: 'Burn Epoch', value: formatUtcTime(counterfactual.burn.burn_epoch), valueTone: 'primary' as const },
        { label: 'Upload Epoch', value: formatUtcTime(counterfactual.burn.upload_epoch), valueTone: 'accent' as const },
        { label: 'Upload Station', value: counterfactual.burn.upload_station || '--', valueTone: 'accent' as const },
        { label: 'Impulse', value: formatDeltaVMs(counterfactual.burn.delta_v_norm_km_s), valueTone: 'primary' as const },
      ],
    },
    {
      title: 'Branch Integrity',
      tone: 'accent' as const,
      rows: [
        { label: 'Compare Basis', value: counterfactual.compare_basis, valueTone: 'primary' as const },
        { label: 'Compare Epoch', value: formatUtcTime(counterfactual.compare_epoch), valueTone: 'primary' as const },
        { label: 'Actual Evaluated', value: counterfactual.actual.evaluated ? 'Yes' : 'No', valueTone: counterfactual.actual.evaluated ? 'accent' as const : 'warning' as const },
        { label: 'No-Burn Evaluated', value: counterfactual.without_burn.evaluated ? 'Yes' : 'No', valueTone: counterfactual.without_burn.evaluated ? 'accent' as const : 'warning' as const },
      ],
    },
  ];

  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: '12px' }}>
      <div
        style={{
          display: 'flex',
          flexDirection: 'column',
          gap: '10px',
          padding: '12px 14px',
          border: `1px solid ${headlineColor}44`,
          background: `linear-gradient(135deg, ${headlineColor}18, rgba(7, 9, 12, 0.96) 52%, rgba(10, 12, 18, 0.94))`,
          clipPath: theme.chamfer.clipPath,
          boxShadow: `0 0 24px ${headlineColor}14`,
          overflow: 'hidden',
          position: 'relative',
        }}
      >
        <div aria-hidden="true" style={{ position: 'absolute', inset: 0, background: 'radial-gradient(circle at 82% 16%, rgba(255,255,255,0.06), transparent 26%)', pointerEvents: 'none' }} />
        <div style={{ display: 'flex', flexDirection: 'column', gap: '5px', position: 'relative' }}>
          <span style={{ color: headlineColor, fontSize: '9px', letterSpacing: '0.18em', textTransform: 'uppercase' }}>Counterfactual theatre</span>
          <strong style={{ color: theme.colors.text, fontSize: '18px', lineHeight: 1.15, maxWidth: '36ch' }}>{headline}</strong>
          <p style={{ color: theme.colors.textDim, fontSize: '11px', lineHeight: 1.55, maxWidth: '54ch' }}>{headlineDetail}</p>
        </div>
        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(122px, 1fr))', gap: '8px', position: 'relative' }}>
          <HeroMetric
            label="Clearance Gain"
            value={formatDistanceKm(counterfactual.delta.clearance_gain_km)}
            detail={counterfactual.delta.crossed_collision_threshold ? 'Actual branch climbed back above the collision boundary.' : 'Extra separation gained, but not a branch flip.'}
            tone={counterfactual.delta.crossed_collision_threshold ? 'accent' : 'primary'}
          />
          <HeroMetric
            label="Burn Cost"
            value={formatMassKg(counterfactual.delta.fuel_spent_kg)}
            detail={`${formatDeltaVMs(counterfactual.burn.delta_v_norm_km_s)} impulse magnitude`}
            tone="warning"
          />
          <HeroMetric
            label="Actual Closest Pass"
            value={formatDistanceKm(counterfactual.actual.min_miss_distance_km)}
            detail={formatUtcTime(counterfactual.actual.min_epoch)}
            tone={actualTone}
          />
          <HeroMetric
            label="No-Burn Pass"
            value={formatDistanceKm(counterfactual.without_burn.min_miss_distance_km)}
            detail={formatUtcTime(counterfactual.without_burn.min_epoch)}
            tone={withoutTone}
          />
        </div>
      </div>

      <div
        style={{
          display: 'flex',
          flexDirection: 'column',
          gap: '10px',
          padding: '12px 14px 14px',
          border: `1px solid ${theme.colors.border}`,
          background: 'linear-gradient(180deg, rgba(9, 12, 18, 0.90), rgba(7, 9, 12, 0.96))',
          clipPath: theme.chamfer.clipPath,
        }}
      >
        <div style={{ display: 'flex', flexDirection: 'column', gap: '5px' }}>
          <span style={{ color: theme.colors.primary, fontSize: '9px', letterSpacing: '0.16em', textTransform: 'uppercase' }}>Clearance rail</span>
          <p style={{ color: theme.colors.textDim, fontSize: '11px', lineHeight: 1.55 }}>
            Same encounter, same execution snapshot. The dashed line marks the hard collision threshold at 100 m.
          </p>
        </div>

        <div style={{ display: 'flex', flexWrap: 'wrap', gap: '6px' }}>
          <LegendChip label="Predicted" value={formatDistanceKm(counterfactual.trigger.predicted_miss_distance_km)} tone="warning" />
          <LegendChip label="Threshold" value={formatDistanceKm(COLLISION_THRESHOLD_KM)} tone="warning" />
          <LegendChip label="No Burn" value={formatDistanceKm(counterfactual.without_burn.min_miss_distance_km)} tone={withoutTone} />
          <LegendChip label="Actual" value={formatDistanceKm(counterfactual.actual.min_miss_distance_km)} tone={actualTone} />
          <LegendChip label="Provenance" value="Executed snapshot" tone="neutral" />
        </div>

        <div style={{ position: 'relative', height: '92px' }}>
          <div style={{ position: 'absolute', left: 0, right: 0, top: '6px', display: 'flex', justifyContent: 'space-between', color: theme.colors.textMuted, fontSize: '9px' }}>
            <span>0 m</span>
            <span>{formatDistanceKm(scaleMaxKm)}</span>
          </div>
          <div style={{ position: 'absolute', left: 0, right: 0, top: '42px', height: '2px', background: 'linear-gradient(90deg, rgba(255, 98, 98, 0.35), rgba(88, 184, 255, 0.24) 52%, rgba(57, 217, 138, 0.45))' }} />
          <div style={{ position: 'absolute', left: `${deltaLeftPct}%`, top: '36px', width: `${deltaWidthPct}%`, height: '14px', background: 'linear-gradient(90deg, rgba(255, 98, 98, 0.24), rgba(57, 217, 138, 0.24), rgba(88, 184, 255, 0.20))', backgroundSize: '200% 100%', animation: 'counterfactualFlow 3.6s linear infinite', border: '1px solid rgba(255,255,255,0.08)', transform: 'translateX(-1px)', clipPath: theme.chamfer.buttonClipPath }} />
          <div style={{ position: 'absolute', left: `${thresholdPct}%`, top: '18px', bottom: '10px', borderLeft: `1px dashed ${theme.colors.warning}`, opacity: 0.9 }} />

          <div style={{ position: 'absolute', left: `${actualPct}%`, top: '4px', transform: 'translateX(-50%)', display: 'flex', flexDirection: 'column', alignItems: 'center', gap: '6px' }}>
            <div style={{ padding: '4px 7px', border: `1px solid ${toneColor(actualTone)}55`, background: `linear-gradient(180deg, ${toneColor(actualTone)}18, rgba(8, 9, 12, 0.94))`, color: toneColor(actualTone), fontSize: '9px', fontWeight: 700, letterSpacing: '0.12em', textTransform: 'uppercase', clipPath: theme.chamfer.buttonClipPath }}>Actual</div>
            <div style={{ width: '1px', height: '18px', background: `${toneColor(actualTone)}88` }} />
            <div style={{ width: '10px', height: '10px', borderRadius: '999px', background: toneColor(actualTone), boxShadow: `0 0 14px ${toneColor(actualTone)}aa`, animation: 'counterfactualPulse 2.2s ease-in-out infinite' }} />
          </div>

          <div style={{ position: 'absolute', left: `${predictedPct}%`, top: '30px', transform: 'translateX(-50%)', display: 'flex', flexDirection: 'column', alignItems: 'center', gap: '4px', opacity: 0.86 }}>
            <div style={{ width: '8px', height: '8px', borderRadius: '999px', background: theme.colors.warning, boxShadow: `0 0 10px ${theme.colors.warning}99` }} />
            <span style={{ color: theme.colors.warning, fontSize: '8px', letterSpacing: '0.14em', textTransform: 'uppercase' }}>Pred</span>
          </div>

          <div style={{ position: 'absolute', left: `${withoutPct}%`, top: '48px', transform: 'translateX(-50%)', display: 'flex', flexDirection: 'column', alignItems: 'center', gap: '6px' }}>
            <div style={{ width: '10px', height: '10px', borderRadius: '999px', background: toneColor(withoutTone), boxShadow: `0 0 14px ${toneColor(withoutTone)}aa` }} />
            <div style={{ width: '1px', height: '18px', background: `${toneColor(withoutTone)}88` }} />
            <div style={{ padding: '4px 7px', border: `1px solid ${toneColor(withoutTone)}55`, background: `linear-gradient(180deg, ${toneColor(withoutTone)}18, rgba(8, 9, 12, 0.94))`, color: toneColor(withoutTone), fontSize: '9px', fontWeight: 700, letterSpacing: '0.12em', textTransform: 'uppercase', clipPath: theme.chamfer.buttonClipPath, whiteSpace: 'nowrap' }}>No burn</div>
          </div>
        </div>
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(210px, 1fr))', gap: '10px' }}>
        <OutcomeBranchCard
          label="Executed Branch"
          branch={counterfactual.actual}
          tone={actualTone}
          detail={counterfactual.delta.crossed_collision_threshold
            ? 'The executed burn keeps the pass above the collision threshold.'
            : 'Executed branch remains the reference outcome after the applied maneuver.'}
        />
        <OutcomeBranchCard
          label="No-Burn Branch"
          branch={counterfactual.without_burn}
          tone={withoutTone}
          detail={counterfactual.delta.crossed_collision_threshold
            ? 'Removing the burn drops this pass back inside the collision threshold.'
            : 'Without the maneuver, the closest-pass geometry still shifts measurably.'}
        />
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(210px, 1fr))', gap: '10px' }}>
        <GeometryMiniPlot label="Actual" branch={counterfactual.actual} tone={actualTone} />
        <GeometryMiniPlot label="NoBurn" branch={counterfactual.without_burn} tone={withoutTone} />
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(190px, 1fr))', gap: '8px' }}>
        {detailGroups.map(group => (
          <DetailGroup key={group.title} title={group.title} tone={group.tone} rows={group.rows} />
        ))}
      </div>

      <div style={{ border: `1px solid ${theme.colors.border}`, background: 'rgba(7, 9, 12, 0.68)', padding: '10px 12px', clipPath: theme.chamfer.clipPath }}>
        <p style={{ color: theme.colors.textDim, fontSize: '11px', lineHeight: 1.55 }}>
          Actual and no-burn branches are both propagated from the stored execution snapshot for this burn, with the hard collision threshold fixed at {formatDistanceKm(COLLISION_THRESHOLD_KM)}. This remains a per-burn causal compare, not a whole-scene alternate-history replay.
        </p>
      </div>
    </div>
  );
}
