import {Layout, Rect, Txt, makeScene2D} from '@motion-canvas/2d';
import {all, createRef, createRefArray, sequence, waitFor} from '@motion-canvas/core';

import {referenceAssets} from '../lib/assets';
import {readinessLines, verificationStats} from '../lib/referenceData';
import {makeBackdrop, makeHeader, makeMetricChip, makeScreenPanel} from '../lib/primitives';
import {fonts, palette} from '../lib/theme';

export default makeScene2D(function* (view) {
  const terminal = createRef<Layout>();
  const metrics = createRefArray<Layout>();
  const panels = createRefArray<Layout>();
  const closer = createRef<Layout>();

  view.add(makeBackdrop());
  view.add(makeHeader({
    kicker: 'Verification',
    title: 'Proof, not just visuals',
    subtitle: 'We finish by showing machine-readable evidence and then reinforce it with the efficiency and fleet posture views.',
  }));

  view.add(
    <Layout ref={terminal} position={[-520, 120]} opacity={0}>
      <Rect width={760} height={420} radius={28} fill={'#07111c'} stroke={`${palette.accent}66`} lineWidth={2}>
        <Layout layout direction={'column'} gap={8} padding={26} width={760}>
          <Txt text={'READINESS REPORT'} fontFamily={fonts.mono} fontSize={22} letterSpacing={6} fill={palette.accent} />
          {readinessLines.map(line => (
            <Txt key={line} text={line} fontFamily={fonts.mono} fontSize={24} fill={line.includes('ready') || line.includes('confirmed') ? palette.accent : palette.textMuted} />
          ))}
        </Layout>
      </Rect>
    </Layout>,
  );

  verificationStats.forEach((entry, index) => {
    const accent = entry.accent === 'accent' ? palette.accent : entry.accent === 'warning' ? palette.warning : palette.primary;
    view.add(makeMetricChip({
      ref: metrics,
      label: entry.label,
      value: entry.value,
      accent,
      width: 320,
      position: [-420 + index * 340, -110],
      opacity: 0,
    }));
  });

  view.add(makeScreenPanel({
    ref: panels,
    src: referenceAssets.evasion,
    label: 'Evasion',
    accent: palette.accent,
    width: 700,
    position: [320, 70],
    opacity: 0,
  }));

  view.add(makeScreenPanel({
    ref: panels,
    src: referenceAssets.fleetStatus,
    label: 'Fleet Status',
    accent: palette.warning,
    width: 700,
    position: [320, 470],
    opacity: 0,
  }));

  view.add(
    <Layout ref={closer} position={[0, 455]} opacity={0}>
      <Rect width={1320} height={130} radius={30} fill={'#07111fdd'} stroke={`${palette.primary}55`} lineWidth={2}>
        <Layout layout direction={'column'} gap={10} padding={22} width={1320} alignItems={'center'}>
          <Txt text={'CASCADE turns collision awareness into collision avoidance'} fontFamily={fonts.display} fontSize={48} fontWeight={700} fill={palette.text} />
          <Txt text={'Real data ingestion. Conservative screening. Autonomous maneuvers. Recovery. Verifiable results.'} fontFamily={fonts.body} fontSize={26} fill={palette.textMuted} />
        </Layout>
      </Rect>
    </Layout>,
  );

  yield* terminal().opacity(1, 0.7);
  yield* waitFor(1.2);
  yield* sequence(0.12, ...metrics.map(metric => metric.opacity(1, 0.35)));
  yield* waitFor(0.9);
  yield* sequence(0.18, ...panels.map(panel => panel.opacity(1, 0.5)));
  yield* waitFor(1.4);
  yield* closer().opacity(1, 0.65);
  yield* waitFor(2.4);
});
