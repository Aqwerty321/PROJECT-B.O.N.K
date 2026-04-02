import {Layout, Rect, Txt, makeScene2D} from '@motion-canvas/2d';
import {all, createRef, createRefArray, sequence, waitFor} from '@motion-canvas/core';

import {makeBackdrop} from '../lib/primitives';
import {fonts, palette, stage} from '../lib/theme';

/**
 * Scene 8 — Closing card (~15s)
 *
 * Clean, confident closing. Tagline, key proof chips,
 * and the system name. Premium space-control feel.
 */
export default makeScene2D(function* (view) {
  /* ── refs ─────────────────────────────────────────────────────────── */
  const rule1 = createRef<Rect>();
  const kicker = createRef<Txt>();
  const title = createRef<Txt>();
  const tagline = createRef<Txt>();
  const chips = createRefArray<Layout>();
  const rule2 = createRef<Rect>();

  /* ── backdrop ────────────────────────────────────────────────────── */
  view.add(makeBackdrop());

  /* ── closing group ───────────────────────────────────────────────── */
  view.add(
    <Layout layout direction={'column'} alignItems={'center'} gap={24} width={1200}>
      <Rect ref={rule1} width={0} height={3} fill={palette.primary} radius={2} />
      <Txt
        ref={kicker}
        text={'CASCADE'}
        fontFamily={fonts.mono}
        fontSize={24}
        letterSpacing={14}
        fill={palette.primary}
        opacity={0}
      />
      <Txt
        ref={title}
        text={'Collision awareness\ninto collision avoidance'}
        fontFamily={fonts.display}
        fontWeight={700}
        fontSize={72}
        lineHeight={88}
        fill={palette.text}
        textAlign={'center'}
        width={1000}
        opacity={0}
      />
      <Txt
        ref={tagline}
        text={'Real data ingestion  •  Conservative screening  •  Autonomous maneuvers  •  Verifiable results'}
        fontFamily={fonts.body}
        fontSize={26}
        lineHeight={36}
        fill={palette.textMuted}
        textAlign={'center'}
        width={1000}
        opacity={0}
      />
    </Layout>,
  );

  /* ── proof chips ─────────────────────────────────────────────────── */
  const chipSpecs: Array<{label: string; accent: string}> = [
    {label: '2 collisions avoided', accent: palette.accent},
    {label: '0.19 kg fuel', accent: palette.warning},
    {label: '0 dropped burns', accent: palette.primary},
    {label: '~13 ms / tick', accent: palette.primary},
  ];

  chipSpecs.forEach((spec, i) => {
    const x = -360 + i * 240;
    view.add(
      <Layout ref={chips} position={[x, 200]} opacity={0}>
        <Rect
          width={210}
          height={52}
          radius={12}
          fill={palette.surface}
          stroke={`${spec.accent}88`}
          lineWidth={2}
        >
          <Txt text={spec.label} fontFamily={fonts.mono} fontSize={17} letterSpacing={2} fill={spec.accent} />
        </Rect>
      </Layout>,
    );
  });

  view.add(<Rect ref={rule2} width={0} height={3} fill={palette.primary} radius={2} position={[0, 290]} />);

  /* ── animation ───────────────────────────────────────────────────── */

  // Phase 1: Build (0s - 5s)
  yield* rule1().width(80, 0.7);
  yield* kicker().opacity(1, 0.5);
  yield* waitFor(0.3);
  yield* title().opacity(1, 0.8);
  yield* waitFor(0.4);
  yield* tagline().opacity(1, 0.7);
  yield* waitFor(1.0);

  // Phase 2: Proof chips (5s - 9s)
  yield* sequence(0.3, ...chips.map(chip => chip.opacity(1, 0.4)));
  yield* waitFor(1.0);
  yield* rule2().width(80, 0.7);

  // Phase 3: Hold (9s - 15s)
  yield* waitFor(6.0);
});
