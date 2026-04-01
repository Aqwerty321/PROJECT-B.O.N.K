import {Layout, Rect, Txt, makeScene2D} from '@motion-canvas/2d';
import {all, createRef, createRefArray, sequence, waitFor} from '@motion-canvas/core';

import {referenceAssets} from '../lib/assets';
import {makeBackdrop, makeHeader, makeInfoCard, makeLeader, makeMetricChip, makeScreenPanel} from '../lib/primitives';
import {fonts, palette} from '../lib/theme';

export default makeScene2D(function* (view) {
  const alert = createRef<Layout>();
  const screen = createRef<Layout>();
  const cards = createRefArray<Layout>();
  const lines = createRefArray<any>();

  view.add(makeBackdrop());
  view.add(makeHeader({
    kicker: 'Threat assessment',
    title: 'Critical conjunctions become visible',
    subtitle: 'We keep the pace slower here so the bullseye framing, severity, and urgency have time to register.',
  }));

  view.add(
    <Layout ref={alert} position={[0, -60]} opacity={0}>
      <Rect width={660} height={110} radius={28} fill={'#140c13'} stroke={`${palette.critical}88`} lineWidth={2}>
        <Txt text={'CALIBRATED FUTURE ENCOUNTERS INJECTED'} fontFamily={fonts.display} fontSize={42} fontWeight={700} fill={palette.critical} />
      </Rect>
    </Layout>,
  );

  view.add(makeScreenPanel({
    ref: screen,
    src: referenceAssets.threat,
    label: 'Threat view',
    accent: palette.critical,
    width: 1220,
    position: [0, 210],
    opacity: 0,
  }));

  const notes: Array<{
    title: string;
    body: string;
    accent: string;
    position: [number, number];
    points: [[number, number], [number, number], [number, number]];
  }> = [
    {
      title: 'Closest miss',
      body: 'The threat card surfaces the nearest pass and ties it to a concrete debris object.',
      accent: palette.critical,
      position: [-650, -140],
      points: [[-230, 55], [-390, -40], [-430, -40]],
    },
    {
      title: 'Time to TCA',
      body: 'The layout makes urgency understandable instead of burying it inside logs.',
      accent: palette.warning,
      position: [640, -80],
      points: [[360, 60], [510, 20], [595, 20]],
    },
    {
      title: 'Operator framing',
      body: 'The queue and selected encounter rail explain which spacecraft is at risk and why it matters.',
      accent: palette.accent,
      position: [670, 330],
      points: [[420, 220], [580, 280], [620, 280]],
    },
  ];

  for (const note of notes) {
    view.add(makeInfoCard({
      ref: cards,
      eyebrow: 'Threat readout',
      title: note.title,
      body: note.body,
      accent: note.accent,
      width: 430,
      height: 170,
      position: note.position,
      opacity: 0,
    }));
    view.add(makeLeader({points: note.points, accent: note.accent, ref: lines, opacity: 0.95}));
  }

  yield* alert().opacity(1, 0.45);
  yield* waitFor(0.8);
  yield* all(alert().opacity(0.22, 0.45), screen().opacity(1, 0.8));
  yield* waitFor(0.8);
  yield* sequence(0.3, ...cards.map(card => card.opacity(1, 0.4)));
  yield* sequence(0.3, ...lines.map(line => line.end(1, 0.35)));
  yield* waitFor(2.4);
});
