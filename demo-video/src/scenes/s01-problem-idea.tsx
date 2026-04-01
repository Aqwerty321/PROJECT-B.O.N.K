import {Circle, Layout, Line, Rect, Txt, makeScene2D} from '@motion-canvas/2d';
import {all, chain, createRef, createRefArray, sequence, waitFor} from '@motion-canvas/core';

import {makeBackdrop, makeHeader, makeInfoCard, makeMetricChip} from '../lib/primitives';
import {fonts, palette} from '../lib/theme';

function orbitRing(size: number, opacity: number) {
  return (
    <Circle
      size={size}
      stroke={palette.orbit}
      lineWidth={2}
      opacity={opacity}
      lineDash={[16, 18]}
      rotation={12}
    />
  );
}

export default makeScene2D(function* (view) {
  const globe = createRef<Circle>();
  const shell = createRef<Layout>();
  const hazard = createRef<Layout>();
  const titleCard = createRef<Layout>();
  const debrisDots = createRefArray<Circle>();
  const orbitLines = createRefArray<Circle>();

  view.add(makeBackdrop());

  view.add(
    <Layout ref={shell} position={[0, 40]} opacity={0}>
      {orbitRing(520, 0.22)}
      {orbitRing(660, 0.16)}
      {orbitRing(790, 0.12)}
      <Circle
        ref={globe}
        size={360}
        fill={'#0f2447'}
        stroke={'#68b8ff'}
        lineWidth={8}
        shadowColor={'#58b8ff55'}
        shadowBlur={60}
      />
      {Array.from({length: 140}, (_, index) => {
        const angle = (index * 37) % 360;
        const radius = 280 + (index % 3) * 55 + ((index * 17) % 40);
        const dotX = Math.cos((angle * Math.PI) / 180) * radius;
        const dotY = Math.sin((angle * Math.PI) / 180) * radius * 0.82;
        const size = 2 + (index % 2);
        return (
          <Circle
            key={`debris-${index}`}
            ref={debrisDots}
            position={[dotX, dotY]}
            size={size}
            fill={palette.debris}
            opacity={0}
          />
        );
      })}
      {Array.from({length: 6}, (_, index) => {
        const size = 470 + index * 42;
        return (
          <Circle
            key={`orbit-${index}`}
            ref={orbitLines}
            size={[size, size * 0.86]}
            stroke={palette.orbit}
            lineWidth={2}
            lineDash={[18, 14]}
            rotation={index * 16 - 12}
            opacity={0}
          />
        );
      })}
    </Layout>,
  );

  view.add(
    <Layout ref={hazard} position={[-610, -340]} opacity={0}>
      <Rect
        width={620}
        height={250}
        radius={28}
        fill={palette.panel}
        stroke={`${palette.critical}55`}
        lineWidth={2}
        shadowColor={`${palette.critical}22`}
        shadowBlur={30}
      >
        <Layout layout direction={'column'} gap={18} padding={28} width={620}>
          <Txt text={'LEO CROWDING PROBLEM'} fontFamily={fonts.mono} fontSize={24} letterSpacing={7} fill={palette.critical} />
          <Txt text={'More satellites. More debris. More collision risk.'} fontFamily={fonts.display} fontSize={50} fontWeight={700} fill={palette.text} width={560} />
          <Txt text={'As orbital traffic increases, close approaches become more frequent and harder to manage manually.'} fontFamily={fonts.body} fontSize={28} lineHeight={40} fill={palette.textMuted} width={560} />
        </Layout>
      </Rect>
    </Layout>,
  );

  view.add(
    <Layout ref={titleCard} position={[0, 360]} opacity={0}>
      <Rect
        width={1320}
        height={220}
        radius={34}
        fill={'#07111fdd'}
        stroke={`${palette.primary}55`}
        lineWidth={2}
        shadowColor={`${palette.primary}22`}
        shadowBlur={34}
      >
        <Layout layout direction={'column'} gap={18} padding={32} width={1320}>
          <Txt text={'CASCADE'} fontFamily={fonts.display} fontSize={88} fontWeight={700} fill={palette.text} />
          <Txt text={'Collision Avoidance System for Constellation Automation, Detection and Evasion'} fontFamily={fonts.body} fontSize={30} lineHeight={40} fill={palette.textMuted} width={1240} />
        </Layout>
      </Rect>
    </Layout>,
  );

  yield* all(shell().opacity(1, 1), globe().scale(1.04, 2).to(1, 1.4));
  yield* sequence(0.02, ...orbitLines.map(line => line.opacity(0.45, 0.5)));
  yield* sequence(0.006, ...debrisDots.map(dot => dot.opacity(0.9, 0.2)));
  yield* waitFor(0.7);

  yield* hazard().opacity(1, 0.7);
  yield* waitFor(1.1);

  yield* titleCard().opacity(1, 0.7);
  yield* all(shell().scale(0.96, 0.8), shell().y(10, 0.8));
  yield* waitFor(1.8);
});
