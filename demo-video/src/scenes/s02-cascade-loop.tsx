import {Layout, Line, Rect, Txt, makeScene2D} from '@motion-canvas/2d';
import {all, chain, createRef, createRefArray, sequence, waitFor} from '@motion-canvas/core';

import {makeBackdrop, makeHeader, makeInfoCard} from '../lib/primitives';
import {fonts, palette} from '../lib/theme';

export default makeScene2D(function* (view) {
  const warningOnly = createRef<Layout>();
  const steps = createRefArray<Layout>();
  const flows = createRefArray<Line>();
  const closer = createRef<Layout>();

  view.add(makeBackdrop());
  view.add(makeHeader({
    kicker: 'CASCADE core loop',
    title: 'Not just a warning system',
    subtitle: 'CASCADE closes the loop from detection to verified avoidance and recovery.',
  }));

  view.add(
    <Layout ref={warningOnly} position={[-560, 110]} opacity={0}>
      <Rect width={420} height={180} radius={28} fill={palette.panel} stroke={`${palette.critical}66`} lineWidth={2}>
        <Layout layout direction={'column'} gap={14} padding={26} width={420}>
          <Txt text={'ALERT ONLY'} fontFamily={fonts.mono} fontSize={22} letterSpacing={6} fill={palette.critical} />
          <Txt text={'Detects a threat, then stops.'} fontFamily={fonts.display} fontSize={38} fontWeight={700} fill={palette.text} />
          <Txt text={'This is not enough for live constellation operations.'} fontFamily={fonts.body} fontSize={24} lineHeight={34} fill={palette.textMuted} width={360} />
        </Layout>
      </Rect>
      <Line points={[[-150, -74], [150, 74]]} stroke={palette.critical} lineWidth={10} end={0} />
      <Line points={[[150, -74], [-150, 74]]} stroke={palette.critical} lineWidth={10} end={0} />
    </Layout>,
  );

  const labels = [
    ['Detect', 'Screen conjunction risk early and conservatively.'],
    ['Decide', 'Pick a mitigation that is actually feasible.'],
    ['Evade', 'Execute the burn with operational constraints.'],
    ['Recover', 'Return the satellite toward stable service.'],
  ] as const;

  view.add(
    <Layout position={[0, 140]}>
      {labels.map((step, index) => {
        const x = -690 + index * 460;
        const accent = [palette.primary, palette.warning, palette.critical, palette.accent][index];
        return (
          <Layout key={step[0]} ref={steps} position={[x, 0]} opacity={0}>
            <Rect width={360} height={200} radius={30} fill={palette.panel} stroke={`${accent}66`} lineWidth={2}>
              <Layout layout direction={'column'} gap={16} padding={26} width={360}>
                <Txt text={`0${index + 1}`} fontFamily={fonts.mono} fontSize={20} letterSpacing={6} fill={accent} />
                <Txt text={step[0]} fontFamily={fonts.display} fontSize={46} fontWeight={700} fill={palette.text} />
                <Txt text={step[1]} fontFamily={fonts.body} fontSize={24} lineHeight={34} fill={palette.textMuted} width={308} />
              </Layout>
            </Rect>
          </Layout>
        );
      })}
      {labels.slice(0, -1).map((_, index) => {
        const startX = -510 + index * 460;
        return (
          <Line
            key={`flow-${index}`}
            ref={flows}
            points={[[startX, 0], [startX + 160, 0]]}
            stroke={palette.primary}
            lineWidth={4}
            start={0}
            end={0}
            endArrow
            arrowSize={18}
            opacity={0.9}
          />
        );
      })}
    </Layout>,
  );

  view.add(
    <Layout ref={closer} position={[0, 380]} opacity={0}>
      <Rect width={1200} height={110} radius={28} fill={'#07111fdd'} stroke={`${palette.accent}55`} lineWidth={2}>
        <Txt text={'Autonomous decision system for constellation safety'} fontFamily={fonts.display} fontSize={46} fontWeight={700} fill={palette.text} />
      </Rect>
    </Layout>,
  );

  yield* warningOnly().opacity(1, 0.6);
  yield* waitFor(0.5);
  yield* sequence(0.12, ...warningOnly().childrenAs<Line>().map(line => line.end(1, 0.35)));
  yield* waitFor(0.6);
  yield* warningOnly().opacity(0.2, 0.4);
  yield* sequence(0.15, ...steps.map(step => step.opacity(1, 0.45)));
  yield* sequence(0.14, ...flows.map(flow => flow.end(1, 0.35)));
  yield* waitFor(1.5);
  yield* closer().opacity(1, 0.6);
  yield* waitFor(1.5);
});
