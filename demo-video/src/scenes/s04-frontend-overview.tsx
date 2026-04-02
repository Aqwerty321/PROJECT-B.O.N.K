import {Img, Layout, Rect, Txt, makeScene2D} from '@motion-canvas/2d';
import {all, createRef, createRefArray, sequence, waitFor} from '@motion-canvas/core';

import {referenceAssets} from '../lib/assets';
import {burnDecision, burnDecisionSecondary} from '../lib/referenceData';
import {makeCalloutBadge, makeBottomBar, makeLeader, makeStatReveal} from '../lib/primitives';
import {fonts, palette, stage} from '../lib/theme';

/**
 * Scene 4 — Burn Ops: The autonomous response (~50s)
 *
 * This is the key scene. Full-bleed Burn Ops screenshot with dramatic
 * stat reveals showing the actual burn decision numbers. Zoom into
 * the timeline. Show that CASCADE acted autonomously.
 */
export default makeScene2D(function* (view) {
  /* ── refs ─────────────────────────────────────────────────────────── */
  const appImg = createRef<Img>();
  const dimOverlay = createRef<Rect>();

  const prelude = createRef<Layout>();
  const preludeTitle = createRef<Txt>();
  const preludeSub = createRef<Txt>();

  const stats = createRefArray<Layout>();
  const callouts = createRefArray<Layout>();
  const leaders = createRefArray<any>();
  const bottomBar = createRef<Layout>();

  const secondBurnCard = createRef<Layout>();

  /* ── "CASCADE acts" prelude overlay ──────────────────────────────── */
  view.add(
    <Layout ref={prelude} opacity={0}>
      <Rect width={stage.width} height={stage.height} fill={'#040814'} />
      <Layout layout direction={'column'} alignItems={'center'} gap={18} width={1000}>
        <Rect width={60} height={3} fill={palette.warning} radius={2} />
        <Txt
          text={'AUTONOMOUS RESPONSE'}
          fontFamily={fonts.mono}
          fontSize={22}
          letterSpacing={12}
          fill={palette.warning}
        />
        <Txt
          ref={preludeTitle}
          text={'CASCADE doesn\'t just warn.\nIt acts.'}
          fontFamily={fonts.display}
          fontWeight={700}
          fontSize={72}
          lineHeight={88}
          fill={palette.text}
          textAlign={'center'}
          width={900}
          opacity={0}
        />
        <Txt
          ref={preludeSub}
          text={'Two critical conjunctions. Two autonomous avoidance burns.\nZero human intervention.'}
          fontFamily={fonts.body}
          fontSize={28}
          lineHeight={40}
          fill={palette.textMuted}
          textAlign={'center'}
          width={800}
          opacity={0}
        />
        <Rect width={60} height={3} fill={palette.warning} radius={2} marginTop={8} />
      </Layout>
    </Layout>,
  );

  /* ── full-bleed Burn Ops screenshot ──────────────────────────────── */
  view.add(
    <Img
      ref={appImg}
      src={referenceAssets.burnOps}
      width={stage.width}
      height={stage.height}
      opacity={0}
      scale={1.06}
    />,
  );

  view.add(
    <Rect ref={dimOverlay} width={stage.width + 200} height={stage.height + 200} fill={'#040814'} opacity={0} />,
  );

  /* ── stat reveal chips — real burn data ──────────────────────────── */
  view.add(makeStatReveal({ref: stats, label: 'Trigger debris', value: burnDecision.debris, accent: palette.critical, position: [-480, -360]}));
  view.add(makeStatReveal({ref: stats, label: 'Miss before', value: burnDecision.missBefore, accent: palette.critical, position: [0, -360]}));
  view.add(makeStatReveal({ref: stats, label: 'Miss after burn', value: burnDecision.missAfter, accent: palette.accent, position: [480, -360]}));

  /* ── callouts over the screenshot ────────────────────────────────── */
  const calloutSpecs: Array<{
    label: string;
    accent: string;
    position: [number, number];
    width: number;
    points: [[number, number], [number, number], [number, number]];
  }> = [
    {
      label: `Delta-v: ${burnDecision.deltaV}  •  Upload: ${burnDecision.uploadStation}`,
      accent: palette.warning,
      position: [-540, 350],
      width: 500,
      points: [[-200, 200], [-380, 300], [-380, 300]],
    },
    {
      label: 'Burn timeline — planned, queued, executed',
      accent: palette.primary,
      position: [520, 350],
      width: 450,
      points: [[280, 200], [430, 300], [430, 300]],
    },
  ];

  for (const spec of calloutSpecs) {
    view.add(makeCalloutBadge({
      ref: callouts,
      label: spec.label,
      accent: spec.accent,
      position: spec.position,
      width: spec.width,
    }));
    view.add(makeLeader({points: spec.points, accent: spec.accent, ref: leaders, opacity: 0.85}));
  }

  /* ── second burn summary card ────────────────────────────────────── */
  const secondText = burnDecisionSecondary
    ? `${burnDecisionSecondary.satellite}  →  ${burnDecisionSecondary.missAfter} clearance  (${burnDecisionSecondary.deltaV})`
    : '';

  view.add(
    <Layout ref={secondBurnCard} position={[0, 440]} opacity={0}>
      <Rect width={700} height={64} radius={14} fill={'#06101bee'} stroke={`${palette.accent}88`} lineWidth={2}>
        <Txt
          text={`SECOND AVOID:  ${secondText}`}
          fontFamily={fonts.mono}
          fontSize={20}
          letterSpacing={3}
          fill={palette.accent}
        />
      </Rect>
    </Layout>,
  );

  view.add(makeBottomBar({ref: bottomBar, text: `Burn Operations — ${burnDecision.satellite} vs ${burnDecision.debris}  •  ${burnDecision.id}`}));

  /* ── animation ───────────────────────────────────────────────────── */

  // Phase 1: Prelude interstitial (0s - 8s)
  yield* prelude().opacity(1, 0.5);
  yield* preludeTitle().opacity(1, 0.8);
  yield* waitFor(0.5);
  yield* preludeSub().opacity(1, 0.7);
  yield* waitFor(4.0);

  // Phase 2: Cross-dissolve to Burn Ops (8s - 13s)
  yield* all(
    prelude().opacity(0, 1.0),
    appImg().opacity(1, 1.5),
    appImg().scale(1.0, 14.0),
  );
  yield* waitFor(2.0);

  // Phase 3: Stat reveals (13s - 25s)
  yield* dimOverlay().opacity(0.35, 0.5);
  yield* bottomBar().opacity(1, 0.5);
  yield* waitFor(0.5);

  yield* stats[0].opacity(1, 0.5);
  yield* waitFor(2.0);
  yield* stats[1].opacity(1, 0.5);
  yield* waitFor(2.0);
  yield* stats[2].opacity(1, 0.5);
  yield* waitFor(2.5);

  // Phase 4: Callouts (25s - 35s)
  for (let i = 0; i < callouts.length; i++) {
    yield* callouts[i].opacity(1, 0.5);
    yield* leaders[i].end(1, 0.4);
    yield* waitFor(3.0);
  }

  // Phase 5: Second burn card (35s - 42s)
  if (burnDecisionSecondary) {
    yield* secondBurnCard().opacity(1, 0.6);
    yield* waitFor(4.0);
  }

  // Phase 6: Hold and fade (42s - 46s)
  yield* waitFor(1.5);
  yield* all(
    appImg().opacity(0, 1.2),
    dimOverlay().opacity(0, 1.0),
    bottomBar().opacity(0, 0.8),
    ...stats.map(s => s.opacity(0, 0.8)),
    ...callouts.map(c => c.opacity(0, 0.8)),
    ...leaders.map(l => l.opacity(0, 0.8)),
    secondBurnCard().opacity(0, 0.8),
  );
  yield* waitFor(0.3);
});
