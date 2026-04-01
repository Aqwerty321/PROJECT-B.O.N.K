import {Layout, makeScene2D} from '@motion-canvas/2d';
import {all, createRef, createRefArray, sequence, waitFor} from '@motion-canvas/core';

import {referenceAssets} from '../lib/assets';
import {replayCommandLines} from '../lib/referenceData';
import {makeBackdrop, makeHeader, makeInfoCard, makeLeader, makeScreenPanel} from '../lib/primitives';
import {fonts, palette} from '../lib/theme';
import {Rect, Txt} from '@motion-canvas/2d';

export default makeScene2D(function* (view) {
  const terminal = createRef<Layout>();
  const screen = createRef<Layout>();
  const callouts = createRefArray<Layout>();
  const leaders = createRefArray<any>();

  view.add(makeBackdrop());
  view.add(makeHeader({
    kicker: 'Real data scene',
    title: 'Ground Track with real catalog traffic',
    subtitle: 'This section should breathe on screen so the audience can actually absorb the density and path geometry.',
  }));

  view.add(
    <Layout ref={terminal} position={[-540, 210]} opacity={0}>
      <Rect width={720} height={240} radius={26} fill={'#07111c'} stroke={`${palette.primary}55`} lineWidth={2}>
        <Layout layout direction={'column'} gap={10} padding={24} width={720}>
          <Txt text={'CATALOG REPLAY'} fontFamily={fonts.mono} fontSize={22} letterSpacing={6} fill={palette.primary} />
          {replayCommandLines.map(line => (
            <Txt key={line} text={line} fontFamily={fonts.mono} fontSize={24} fill={palette.textMuted} />
          ))}
        </Layout>
      </Rect>
    </Layout>,
  );

  view.add(makeScreenPanel({
    ref: screen,
    src: referenceAssets.track,
    label: 'Ground Track',
    accent: palette.accent,
    width: 1240,
    position: [180, 180],
    opacity: 0,
  }));

  const calloutSpecs: Array<{
    title: string;
    body: string;
    accent: string;
    position: [number, number];
    points: [[number, number], [number, number], [number, number]];
  }> = [
    {
      title: 'Dense traffic picture',
      body: 'The operator fleet sits inside a live field of catalog-backed traffic.',
      accent: palette.critical,
      position: [-660, 300],
      points: [[-210, 215], [-360, 250], [-455, 250]],
    },
    {
      title: 'Trail + forecast',
      body: 'Historical trails and predicted paths make the motion readable at a glance.',
      accent: palette.primary,
      position: [640, -160],
      points: [[370, -70], [525, -110], [590, -110]],
    },
    {
      title: 'Operational context',
      body: 'The terminator and ground station hints make the scene feel mission-oriented, not decorative.',
      accent: palette.warning,
      position: [700, 340],
      points: [[500, 240], [610, 300], [655, 300]],
    },
  ];

  for (const spec of calloutSpecs) {
    view.add(makeInfoCard({
      ref: callouts,
      eyebrow: 'What to notice',
      title: spec.title,
      body: spec.body,
      accent: spec.accent,
      width: 430,
      height: 170,
      position: spec.position,
      opacity: 0,
    }));
    view.add(makeLeader({points: spec.points, accent: spec.accent, ref: leaders, opacity: 0.9}));
  }

  yield* terminal().opacity(1, 0.6);
  yield* waitFor(1.2);
  yield* all(terminal().opacity(0.22, 0.5), screen().opacity(1, 0.8), screen().scale(1.02, 0.8).to(1, 0.6));
  yield* waitFor(0.7);
  yield* sequence(0.28, ...callouts.map(item => item.opacity(1, 0.45)));
  yield* sequence(0.28, ...leaders.map(line => line.end(1, 0.35)));
  yield* waitFor(2.6);
});
