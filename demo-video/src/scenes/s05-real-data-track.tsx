import {Img, Layout, Rect, Txt, makeScene2D} from '@motion-canvas/2d';
import {all, createRef, createRefArray, sequence, waitFor} from '@motion-canvas/core';

import {referenceAssets} from '../lib/assets';
import {makeBackdrop, makeCalloutBadge, makeBottomBar} from '../lib/primitives';
import {fonts, palette, stage} from '../lib/theme';

/**
 * Scene 5 — Under the hood: architecture overlay on dimmed app (~35s)
 *
 * Opens with a chapter card, then shows a dimmed Command screenshot
 * with architecture stats overlaid. Brief and punchy because the
 * proof already landed in scenes 3-4.
 */
export default makeScene2D(function* (view) {
  /* ── refs ─────────────────────────────────────────────────────────── */
  const chapterBg = createRef<Layout>();
  const chapterTitle = createRef<Txt>();
  const chapterSub = createRef<Txt>();

  const appImg = createRef<Img>();
  const dimOverlay = createRef<Rect>();
  const archCards = createRefArray<Layout>();
  const bottomBar = createRef<Layout>();

  /* ── chapter interstitial ────────────────────────────────────────── */
  view.add(
    <Layout ref={chapterBg} opacity={0}>
      {makeBackdrop()}
      <Layout layout direction={'column'} alignItems={'center'} gap={20} width={1200}>
        <Rect width={80} height={3} fill={palette.primary} radius={2} />
        <Txt
          text={'ENGINEERING'}
          fontFamily={fonts.mono}
          fontSize={22}
          letterSpacing={12}
          fill={palette.primary}
        />
        <Txt
          ref={chapterTitle}
          text={'Under the Hood'}
          fontFamily={fonts.display}
          fontWeight={700}
          fontSize={86}
          lineHeight={100}
          fill={palette.text}
          opacity={0}
        />
        <Txt
          ref={chapterSub}
          text={'Deterministic C++20 engine with conservative safety logic'}
          fontFamily={fonts.body}
          fontSize={28}
          lineHeight={40}
          fill={palette.textMuted}
          opacity={0}
        />
        <Rect width={80} height={3} fill={palette.primary} radius={2} marginTop={8} />
      </Layout>
    </Layout>,
  );

  /* ── dimmed app background ───────────────────────────────────────── */
  view.add(
    <Img ref={appImg} src={referenceAssets.command} width={stage.width} height={stage.height} opacity={0} />,
  );
  view.add(
    <Rect ref={dimOverlay} width={stage.width} height={stage.height} fill={'#040814'} opacity={0} />,
  );

  /* ── architecture info cards overlaid on the dimmed app ──────────── */
  const archSpecs: Array<{label: string; value: string; accent: string; position: [number, number]}> = [
    {label: 'ENGINE', value: 'Deterministic C++20', accent: palette.primary, position: [-440, -240]},
    {label: 'TICK TIME', value: '~13 ms for 50 sats + 10k debris', accent: palette.accent, position: [440, -240]},
    {label: 'PIPELINE', value: 'Propagate → Broad phase → Narrow phase → CDM → COLA → Recovery', accent: palette.warning, position: [0, -60]},
    {label: 'SAFETY RULE', value: 'Uncertain → escalate.  Never silently drop.', accent: palette.critical, position: [-440, 130]},
    {label: 'CONSTRAINTS', value: 'Fuel limits • Thrust caps • Cooldown • Ground-station LOS', accent: palette.primary, position: [440, 130]},
    {label: 'API', value: 'REST endpoints — telemetry, schedule, step, snapshot', accent: palette.accent, position: [0, 310]},
  ];

  for (const spec of archSpecs) {
    view.add(
      <Layout ref={archCards} position={spec.position} opacity={0}>
        <Rect
          width={680}
          height={110}
          radius={18}
          fill={'#06101bee'}
          stroke={`${spec.accent}77`}
          lineWidth={2}
          shadowColor={`${spec.accent}33`}
          shadowBlur={24}
        >
          <Layout layout direction={'column'} gap={8} padding={20} width={680} alignItems={'center'}>
            <Txt text={spec.label} fontFamily={fonts.mono} fontSize={18} letterSpacing={8} fill={spec.accent} />
            <Txt text={spec.value} fontFamily={fonts.display} fontSize={28} fontWeight={700} fill={palette.text} textAlign={'center'} width={640} />
          </Layout>
        </Rect>
      </Layout>,
    );
  }

  view.add(makeBottomBar({ref: bottomBar, text: 'Backend architecture — multi-stage collision screening with conservative safety guarantees'}));

  /* ── animation ───────────────────────────────────────────────────── */

  // Phase 1: Chapter card (0s - 6s)
  yield* chapterBg().opacity(1, 0.4);
  yield* chapterTitle().opacity(1, 0.7);
  yield* waitFor(0.3);
  yield* chapterSub().opacity(1, 0.6);
  yield* waitFor(3.0);

  // Phase 2: Cross-dissolve to dimmed app (6s - 9s)
  yield* all(
    chapterBg().opacity(0, 1.0),
    appImg().opacity(1, 1.2),
    dimOverlay().opacity(0.78, 1.2),
  );
  yield* bottomBar().opacity(1, 0.4);
  yield* waitFor(1.0);

  // Phase 3: Architecture cards stagger in (9s - 28s)
  for (let i = 0; i < archCards.length; i++) {
    yield* archCards[i].opacity(1, 0.4);
    yield* waitFor(2.2);
  }

  // Phase 4: Hold (28s - 32s)
  yield* waitFor(3.0);

  // Phase 5: Fade out (32s - 34s)
  yield* all(
    appImg().opacity(0, 1.0),
    dimOverlay().opacity(0, 1.0),
    bottomBar().opacity(0, 0.8),
    ...archCards.map(c => c.opacity(0, 0.8)),
  );
  yield* waitFor(0.3);
});
