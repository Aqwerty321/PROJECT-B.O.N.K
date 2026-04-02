import {Img, Layout, Rect, Txt, makeScene2D} from '@motion-canvas/2d';
import {all, createRef, waitFor} from '@motion-canvas/core';

import {referenceAssets} from '../lib/assets';
import {makeBackdrop, makeCalloutBadge, makeBottomBar, makeFullBleed} from '../lib/primitives';
import {fonts, palette, stage} from '../lib/theme';

/**
 * Scene 1 — Title interstitial + Command page tour (~40s)
 *
 * Opens with a cinematic CASCADE title card on the starfield backdrop,
 * then cross-dissolves into the live Command page screenshot with
 * slow zoom-in and callout overlays.
 */
export default makeScene2D(function* (view) {
  /* ── refs ─────────────────────────────────────────────────────────── */
  const titleGroup = createRef<Layout>();
  const rule1 = createRef<Rect>();
  const rule2 = createRef<Rect>();
  const kicker = createRef<Txt>();
  const titleTxt = createRef<Txt>();
  const tagline = createRef<Txt>();

  const appImg = createRef<Img>();
  const dimOverlay = createRef<Rect>();
  const badge1 = createRef<Layout>();
  const badge2 = createRef<Layout>();
  const badge3 = createRef<Layout>();
  const bottomBar = createRef<Layout>();

  /* ── backdrop (persistent) ───────────────────────────────────────── */
  view.add(makeBackdrop());

  /* ── title card group ────────────────────────────────────────────── */
  view.add(
    <Layout ref={titleGroup} opacity={0}>
      <Layout layout direction={'column'} alignItems={'center'} gap={22} width={1200}>
        <Rect ref={rule1} width={0} height={3} fill={palette.primary} radius={2} />
        <Txt
          ref={kicker}
          text={'NATIONAL SPACE HACKATHON 2026'}
          fontFamily={fonts.mono}
          fontSize={22}
          letterSpacing={12}
          fill={palette.primary}
          opacity={0}
        />
        <Txt
          ref={titleTxt}
          text={'CASCADE'}
          fontFamily={fonts.display}
          fontWeight={700}
          fontSize={110}
          lineHeight={120}
          fill={palette.text}
          opacity={0}
        />
        <Txt
          ref={tagline}
          text={'Collision Avoidance System for Constellation\nAutomation, Detection & Evasion'}
          fontFamily={fonts.body}
          fontSize={30}
          lineHeight={44}
          fill={palette.textMuted}
          textAlign={'center'}
          width={900}
          opacity={0}
        />
        <Rect ref={rule2} width={0} height={3} fill={palette.primary} radius={2} marginTop={8} />
      </Layout>
    </Layout>,
  );

  /* ── app screenshot (hidden initially) ───────────────────────────── */
  view.add(
    <Img
      ref={appImg}
      src={referenceAssets.command}
      width={stage.width}
      height={stage.height}
      opacity={0}
      scale={1.08}
    />,
  );

  /* ── dim overlay for text legibility on screenshot ───────────────── */
  view.add(
    <Rect
      ref={dimOverlay}
      width={stage.width}
      height={stage.height}
      fill={'#040814'}
      opacity={0}
    />,
  );

  /* ── callout badges (positioned over the command screenshot) ─────── */
  view.add(makeCalloutBadge({ref: badge1, label: '50 satellites — real NORAD catalog', accent: palette.primary, position: [-520, -340], width: 440}));
  view.add(makeCalloutBadge({ref: badge2, label: '10,000+ tracked debris objects', accent: palette.warning, position: [480, -340], width: 420}));
  view.add(makeCalloutBadge({ref: badge3, label: 'Live fleet health & conjunction status', accent: palette.accent, position: [0, 380], width: 480}));

  view.add(makeBottomBar({ref: bottomBar, text: 'Command Overview — operational fleet picture from real orbital data'}));

  /* ── animation ───────────────────────────────────────────────────── */

  // Phase 1: Title card build (0s - 10s)
  yield* titleGroup().opacity(1, 0.3);
  yield* rule1().width(80, 0.8);
  yield* kicker().opacity(1, 0.6);
  yield* waitFor(0.3);
  yield* titleTxt().opacity(1, 0.8);
  yield* waitFor(0.4);
  yield* tagline().opacity(1, 0.7);
  yield* rule2().width(80, 0.8);
  yield* waitFor(3.5);

  // Phase 2: Cross-dissolve to Command page (10s - 15s)
  yield* all(
    titleGroup().opacity(0, 1.0),
    appImg().opacity(1, 1.5),
    appImg().scale(1.0, 8.0),  // very slow ken-burns zoom-out over the whole phase
  );
  yield* waitFor(1.5);

  // Phase 3: Callout overlays (15s - 30s)
  yield* dimOverlay().opacity(0.35, 0.5);
  yield* badge1().opacity(1, 0.6);
  yield* waitFor(2.5);
  yield* badge2().opacity(1, 0.6);
  yield* waitFor(2.5);
  yield* badge3().opacity(1, 0.6);
  yield* waitFor(1.5);
  yield* bottomBar().opacity(1, 0.5);
  yield* waitFor(4.0);

  // Phase 4: Fade out (30s - 33s)
  yield* all(
    appImg().opacity(0, 1.2),
    dimOverlay().opacity(0, 1.0),
    badge1().opacity(0, 0.8),
    badge2().opacity(0, 0.8),
    badge3().opacity(0, 0.8),
    bottomBar().opacity(0, 0.8),
  );
  yield* waitFor(0.5);
});
