import {Layout, Rect, Txt, makeScene2D} from '@motion-canvas/2d';
import {all, createRef, createRefArray, sequence, waitFor} from '@motion-canvas/core';

import {readinessLines, verificationStats} from '../lib/referenceData';
import {makeBackdrop, makeStatReveal} from '../lib/primitives';
import {fonts, palette, stage} from '../lib/theme';

/**
 * Scene 7 — Verification terminal + proof (~25s)
 *
 * Shows the machine-readable readiness output line by line,
 * then surfaces the key verification stats as dramatic reveals.
 */
export default makeScene2D(function* (view) {
  /* ── refs ─────────────────────────────────────────────────────────── */
  const terminal = createRef<Layout>();
  const termLines = createRefArray<Txt>();
  const stats = createRefArray<Layout>();
  const verdictHighlight = createRef<Rect>();

  /* ── backdrop ────────────────────────────────────────────────────── */
  view.add(makeBackdrop());

  /* ── section header ──────────────────────────────────────────────── */
  view.add(
    <Layout position={[0, -420]} opacity={1}>
      <Layout layout direction={'column'} alignItems={'center'} gap={10} width={800}>
        <Rect width={60} height={3} fill={palette.accent} radius={2} />
        <Txt
          text={'VERIFICATION'}
          fontFamily={fonts.mono}
          fontSize={22}
          letterSpacing={12}
          fill={palette.accent}
        />
        <Txt
          text={'Proof, not just visuals'}
          fontFamily={fonts.display}
          fontWeight={700}
          fontSize={56}
          lineHeight={66}
          fill={palette.text}
          textAlign={'center'}
        />
      </Layout>
    </Layout>,
  );

  /* ── terminal panel ──────────────────────────────────────────────── */
  view.add(
    <Layout ref={terminal} position={[0, -50]} opacity={0}>
      <Rect
        width={860}
        height={380}
        radius={22}
        fill={'#07111c'}
        stroke={`${palette.accent}55`}
        lineWidth={2}
        shadowColor={`${palette.accent}22`}
        shadowBlur={30}
      >
        <Layout layout direction={'column'} gap={7} padding={28} width={860}>
          <Txt text={'READINESS REPORT'} fontFamily={fonts.mono} fontSize={20} letterSpacing={6} fill={palette.accent} />
          <Rect width={810} height={1} fill={palette.border} opacity={0.4} />
          {readinessLines.map(line => (
            <Txt
              key={line}
              ref={termLines}
              text={line}
              fontFamily={fonts.mono}
              fontSize={22}
              fill={
                line.includes('ready') || line.includes('confirmed')
                  ? palette.accent
                  : line.includes('verdict')
                    ? palette.accent
                    : palette.textMuted
              }
              opacity={0}
            />
          ))}
        </Layout>
      </Rect>
    </Layout>,
  );

  /* ── verdict highlight bar ───────────────────────────────────────── */
  view.add(
    <Layout ref={verdictHighlight} position={[0, 200]} opacity={0}>
      <Rect width={480} height={70} radius={16} fill={'#0b2218'} stroke={`${palette.accent}aa`} lineWidth={2}>
        <Txt
          text={'VERDICT:  READY  ✓'}
          fontFamily={fonts.mono}
          fontSize={30}
          letterSpacing={6}
          fontWeight={700}
          fill={palette.accent}
        />
      </Rect>
    </Layout>,
  );

  /* ── stat reveals ────────────────────────────────────────────────── */
  verificationStats.forEach((entry, i) => {
    const accent = entry.accent === 'accent' ? palette.accent : entry.accent === 'warning' ? palette.warning : palette.primary;
    view.add(makeStatReveal({
      ref: stats,
      label: entry.label,
      value: entry.value,
      accent,
      position: [-420 + i * 420, 380],
    }));
  });

  /* ── animation ───────────────────────────────────────────────────── */

  // Phase 1: Terminal appears (0s - 2s)
  yield* terminal().opacity(1, 0.7);
  yield* waitFor(0.5);

  // Phase 2: Lines reveal one by one (2s - 12s)
  for (let i = 0; i < termLines.length; i++) {
    yield* termLines[i].opacity(1, 0.25);
    yield* waitFor(0.6);
  }

  yield* waitFor(1.0);

  // Phase 3: Verdict highlight (12s - 15s)
  yield* verdictHighlight().opacity(1, 0.6);
  yield* waitFor(2.5);

  // Phase 4: Stat reveals (15s - 22s)
  for (let i = 0; i < stats.length; i++) {
    yield* stats[i].opacity(1, 0.4);
    yield* waitFor(1.5);
  }

  // Phase 5: Hold (22s - 25s)
  yield* waitFor(3.0);
});
