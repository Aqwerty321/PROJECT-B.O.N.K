import {Layout, Line, Rect, Txt, makeScene2D} from '@motion-canvas/2d';
import {all, createRef, createRefArray, sequence, waitFor} from '@motion-canvas/core';

import {makeBackdrop, makeHeader, makeInfoCard, makeMetricChip} from '../lib/primitives';
import {fonts, palette} from '../lib/theme';

export default makeScene2D(function* (view) {
  const pipelineCards = createRefArray<Layout>();
  const connectors = createRefArray<Line>();
  const safetyCallout = createRef<Layout>();
  const constraints = createRefArray<Layout>();
  const metric = createRef<Layout>();

  view.add(makeBackdrop());
  view.add(makeHeader({
    kicker: 'Implementation',
    title: 'Deterministic backend pipeline',
    subtitle: 'The engine ingests telemetry, screens threats conservatively, plans a maneuver, and then schedules recovery.',
  }));

  const stages = [
    ['Telemetry', palette.primary],
    ['Propagation', palette.orbit],
    ['Filter cascade', palette.warning],
    ['Critical CDM', palette.critical],
    ['COLA burn', palette.accent],
    ['Recovery', palette.primary],
  ] as const;

  view.add(
    <Layout position={[0, 60]}>
      {stages.map(([name, accent], index) => {
        const x = -760 + index * 305;
        return (
          <Layout key={name} ref={pipelineCards} position={[x, 0]} opacity={0}>
            <Rect width={260} height={130} radius={24} fill={palette.panel} stroke={`${accent}66`} lineWidth={2}>
              <Layout layout direction={'column'} gap={12} padding={20} width={260}>
                <Txt text={`0${index + 1}`} fontFamily={fonts.mono} fontSize={18} letterSpacing={5} fill={accent} />
                <Txt text={name} fontFamily={fonts.display} fontSize={34} fontWeight={700} fill={palette.text} />
              </Layout>
            </Rect>
          </Layout>
        );
      })}
      {stages.slice(0, -1).map((_, index) => {
        const startX = -630 + index * 305;
        return (
          <Line
            key={`connector-${index}`}
            ref={connectors}
            points={[[startX, 0], [startX + 70, 0]]}
            stroke={palette.primary}
            lineWidth={4}
            end={0}
            endArrow
            arrowSize={16}
            opacity={0.8}
          />
        );
      })}
    </Layout>,
  );

  view.add(makeInfoCard({
    ref: safetyCallout,
    eyebrow: 'Safety rule',
    title: 'Uncertain pairs escalate',
    body: 'Cheap filters reduce load, but ambiguous cases are never silently dropped. They move forward for deeper analysis.',
    accent: palette.critical,
    width: 760,
    height: 190,
    position: [-520, 310],
    opacity: 0,
  }));

  const constraintItems = [
    ['Fuel budget', palette.warning],
    ['Cooldown windows', palette.primary],
    ['Thrust cap', palette.critical],
    ['Ground-station LOS', palette.accent],
  ] as const;

  view.add(
    <Layout position={[510, 280]}>
      {constraintItems.map(([label, accent], index) => {
        const y = -120 + index * 88;
        return makeMetricChip({
          ref: constraints,
          label: 'Constraint',
          value: label,
          accent,
          width: 420,
          position: [0, y],
          opacity: 0,
        });
      })}
    </Layout>,
  );

  view.add(makeMetricChip({
    ref: metric,
    label: 'Reference tick',
    value: '13.3 ms @ 50 sats / 10k debris',
    accent: palette.accent,
    width: 720,
    position: [0, 420],
    opacity: 0,
  }));

  yield* sequence(0.12, ...pipelineCards.map(card => card.opacity(1, 0.35)));
  yield* sequence(0.1, ...connectors.map(connector => connector.end(1, 0.25)));
  yield* waitFor(0.9);
  yield* safetyCallout().opacity(1, 0.6);
  yield* sequence(0.1, ...constraints.map(item => item.opacity(1, 0.35)));
  yield* waitFor(0.8);
  yield* metric().opacity(1, 0.55);
  yield* waitFor(1.8);
});
