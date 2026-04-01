import {Layout, Rect, Txt, makeScene2D} from '@motion-canvas/2d';
import {all, createRef, createRefArray, sequence, waitFor} from '@motion-canvas/core';

import {referenceAssets} from '../lib/assets';
import {burnDecision, burnDecisionSecondary} from '../lib/referenceData';
import {makeBackdrop, makeHeader, makeInfoCard, makeLeader, makeMetricChip, makeScreenPanel} from '../lib/primitives';
import {fonts, palette} from '../lib/theme';

export default makeScene2D(function* (view) {
  const decisionRail = createRef<Layout>();
  const screen = createRef<Layout>();
  const chips = createRefArray<Layout>();
  const notes = createRefArray<Layout>();
  const leaders = createRefArray<any>();

  view.add(makeBackdrop());
  view.add(makeHeader({
    kicker: 'Autonomous response',
    title: 'CASCADE chooses and executes the burn',
    subtitle: 'This scene stays on screen longer because it is the core proof that the system does more than warn.',
  }));

  view.add(
    <Layout ref={decisionRail} position={[-635, 180]} opacity={0}>
      <Rect width={470} height={420} radius={28} fill={palette.panel} stroke={`${palette.warning}66`} lineWidth={2}>
        <Layout layout direction={'column'} gap={16} padding={26} width={470}>
          <Txt text={'BURN DECISION'} fontFamily={fonts.mono} fontSize={22} letterSpacing={6} fill={palette.warning} />
          <Txt text={burnDecision.id} fontFamily={fonts.display} fontSize={40} fontWeight={700} fill={palette.text} />
          <Txt text={`${burnDecision.satellite} vs ${burnDecision.debris}`} fontFamily={fonts.body} fontSize={26} fill={palette.textMuted} />
          <Txt text={`Predicted miss: ${burnDecision.missBefore}`} fontFamily={fonts.body} fontSize={28} fill={palette.critical} />
          <Txt text={`Mitigated miss: ${burnDecision.missAfter}`} fontFamily={fonts.body} fontSize={28} fill={palette.accent} />
          <Txt text={`Delta-v: ${burnDecision.deltaV}`} fontFamily={fonts.body} fontSize={28} fill={palette.warning} />
          <Txt text={`Upload station: ${burnDecision.uploadStation}`} fontFamily={fonts.body} fontSize={28} fill={palette.primary} />
          <Txt text={`Lead to TCA: ${burnDecision.leadTime}`} fontFamily={fonts.body} fontSize={28} fill={palette.text} />
          <Rect width={418} height={2} fill={palette.borderBright} opacity={0.6} />
          <Txt text={`${burnDecisionSecondary.satellite} also receives an autonomous avoid with ${burnDecisionSecondary.missAfter} clearance.`} fontFamily={fonts.body} fontSize={24} lineHeight={34} fill={palette.textMuted} width={418} />
        </Layout>
      </Rect>
    </Layout>,
  );

  view.add(makeScreenPanel({
    ref: screen,
    src: referenceAssets.burnOps,
    label: 'Burn Ops',
    accent: palette.warning,
    width: 1100,
    position: [350, 200],
    opacity: 0,
  }));

  const chipSpecs: Array<{
    label: string;
    value: string;
    accent: string;
    position: [number, number];
  }> = [
    {label: 'Trigger debris', value: burnDecision.debris, accent: palette.critical, position: [-140, -120]},
    {label: 'Chosen delta-v', value: burnDecision.deltaV, accent: palette.warning, position: [160, -120]},
    {label: 'Post result', value: burnDecision.missAfter, accent: palette.accent, position: [460, -120]},
  ];

  chipSpecs.forEach(spec => {
    view.add(makeMetricChip({
      ref: chips,
      label: spec.label,
      value: spec.value,
      accent: spec.accent,
      width: 260,
      position: spec.position,
      opacity: 0,
    }));
  });

  const detailSpecs: Array<{
    title: string;
    body: string;
    accent: string;
    position: [number, number];
    points: [[number, number], [number, number], [number, number]];
  }> = [
    {
      title: 'Timeline evidence',
      body: 'The Gantt-style timeline makes queued, executed, and avoided events legible in one place.',
      accent: palette.warning,
      position: [560, 360],
      points: [[430, 290], [520, 320], [540, 320]],
    },
    {
      title: 'Why this matters',
      body: 'The system validates a feasible maneuver instead of emitting a threat and leaving the operator there.',
      accent: palette.accent,
      position: [-580, 400],
      points: [[-260, 310], [-400, 360], [-470, 360]],
    },
  ];

  detailSpecs.forEach(spec => {
    view.add(makeInfoCard({
      ref: notes,
      eyebrow: 'Response proof',
      title: spec.title,
      body: spec.body,
      accent: spec.accent,
      width: 430,
      height: 170,
      position: spec.position,
      opacity: 0,
    }));
    view.add(makeLeader({points: spec.points, accent: spec.accent, ref: leaders, opacity: 0.95}));
  });

  yield* decisionRail().opacity(1, 0.7);
  yield* waitFor(1.0);
  yield* all(decisionRail().x(-760, 0.65), decisionRail().scale(0.92, 0.65), screen().opacity(1, 0.8));
  yield* waitFor(0.8);
  yield* sequence(0.18, ...chips.map(chip => chip.opacity(1, 0.35)));
  yield* waitFor(0.8);
  yield* sequence(0.25, ...notes.map(note => note.opacity(1, 0.4)));
  yield* sequence(0.25, ...leaders.map(line => line.end(1, 0.35)));
  yield* waitFor(3.0);
});
