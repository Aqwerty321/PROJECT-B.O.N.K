import {Img, Layout, Rect, Txt, makeScene2D} from '@motion-canvas/2d';
import {all, createRef, createRefArray, waitFor} from '@motion-canvas/core';

import {referenceAssets} from '../lib/assets';
import {makeBackdrop, makeCalloutBadge, makeBottomBar, makeLeader} from '../lib/primitives';
import {fonts, palette, stage} from '../lib/theme';

/**
 * Scene 3 — "THREAT DETECTED" interstitial + Threat view tour (~40s)
 *
 * Opens with a dramatic chapter card pulsing red, then cross-dissolves
 * into the full-bleed Threat Assessment screenshot. Callouts highlight
 * the bullseye, severity colors, and the specific critical conjunction.
 */
export default makeScene2D(function* (view) {
  /* ── refs ─────────────────────────────────────────────────────────── */
  const chapterBg = createRef<Layout>();
  const alertLine = createRef<Rect>();
  const alertKicker = createRef<Txt>();
  const alertTitle = createRef<Txt>();
  const alertSub = createRef<Txt>();

  const appImg = createRef<Img>();
  const dimOverlay = createRef<Rect>();
  const callouts = createRefArray<Layout>();
  const leaders = createRefArray<any>();
  const bottomBar = createRef<Layout>();

  /* ── chapter interstitial ────────────────────────────────────────── */
  view.add(
    <Layout ref={chapterBg} opacity={0}>
      {makeBackdrop()}
      <Layout layout direction={'column'} alignItems={'center'} gap={20} width={1200}>
        <Rect ref={alertLine} width={0} height={3} fill={palette.critical} radius={2} />
        <Txt
          ref={alertKicker}
          text={'CONJUNCTION SCREENING'}
          fontFamily={fonts.mono}
          fontSize={22}
          letterSpacing={12}
          fill={palette.critical}
          opacity={0}
        />
        <Txt
          ref={alertTitle}
          text={'Threat Detected'}
          fontFamily={fonts.display}
          fontWeight={700}
          fontSize={90}
          lineHeight={100}
          fill={palette.text}
          opacity={0}
        />
        <Txt
          ref={alertSub}
          text={'Critical conjunction — 8 meters predicted miss distance'}
          fontFamily={fonts.body}
          fontSize={28}
          lineHeight={40}
          fill={palette.textMuted}
          opacity={0}
        />
        <Rect width={80} height={3} fill={palette.critical} radius={2} marginTop={8} opacity={0.5} />
      </Layout>
    </Layout>,
  );

  /* ── full-bleed Threat screenshot ────────────────────────────────── */
  view.add(
    <Img
      ref={appImg}
      src={referenceAssets.threat}
      width={stage.width}
      height={stage.height}
      opacity={0}
      scale={1.06}
    />,
  );

  view.add(
    <Rect ref={dimOverlay} width={stage.width + 200} height={stage.height + 200} fill={'#040814'} opacity={0} />,
  );

  /* ── callouts over the threat view ───────────────────────────────── */
  const specs: Array<{
    label: string;
    accent: string;
    position: [number, number];
    width: number;
    points: [[number, number], [number, number], [number, number]];
  }> = [
    {
      label: 'Bullseye — time rings + severity colors',
      accent: palette.critical,
      position: [-560, -370],
      width: 440,
      points: [[-180, -120], [-370, -300], [-410, -300]],
    },
    {
      label: 'Critical: < 100m miss distance',
      accent: palette.critical,
      position: [560, -370],
      width: 380,
      points: [[200, -60], [420, -300], [450, -300]],
    },
    {
      label: 'Conjunction queue — focused satellite',
      accent: palette.warning,
      position: [560, 340],
      width: 420,
      points: [[350, 200], [470, 280], [500, 280]],
    },
  ];

  for (const spec of specs) {
    view.add(makeCalloutBadge({
      ref: callouts,
      label: spec.label,
      accent: spec.accent,
      position: spec.position,
      width: spec.width,
    }));
    view.add(makeLeader({points: spec.points, accent: spec.accent, ref: leaders, opacity: 0.85}));
  }

  view.add(makeBottomBar({ref: bottomBar, text: 'Threat Assessment — conjunction bullseye with CDM severity tiers and Pc proxy sizing'}));

  /* ── animation ───────────────────────────────────────────────────── */

  // Phase 1: Chapter card (0s - 7s)
  yield* chapterBg().opacity(1, 0.4);
  yield* alertLine().width(80, 0.7);
  yield* alertKicker().opacity(1, 0.5);
  yield* waitFor(0.3);
  yield* alertTitle().opacity(1, 0.7);
  yield* waitFor(0.3);
  yield* alertSub().opacity(1, 0.6);
  yield* waitFor(3.0);

  // Phase 2: Cross-dissolve to Threat view (7s - 11s)
  yield* all(
    chapterBg().opacity(0, 1.0),
    appImg().opacity(1, 1.5),
    appImg().scale(1.0, 12.0),
  );
  yield* waitFor(2.0);

  // Phase 3: Callouts (11s - 30s)
  yield* dimOverlay().opacity(0.3, 0.5);
  yield* bottomBar().opacity(1, 0.5);
  yield* waitFor(0.8);

  for (let i = 0; i < callouts.length; i++) {
    yield* callouts[i].opacity(1, 0.5);
    yield* leaders[i].end(1, 0.4);
    yield* waitFor(3.5);
  }

  // Phase 4: Hold and fade (30s - 34s)
  yield* waitFor(2.5);
  yield* all(
    appImg().opacity(0, 1.2),
    dimOverlay().opacity(0, 1.0),
    bottomBar().opacity(0, 0.8),
    ...callouts.map(c => c.opacity(0, 0.8)),
    ...leaders.map(l => l.opacity(0, 0.8)),
  );
  yield* waitFor(0.3);
});
