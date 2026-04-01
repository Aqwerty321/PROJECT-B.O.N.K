import {Layout, makeScene2D} from '@motion-canvas/2d';
import {createRefArray, sequence, waitFor} from '@motion-canvas/core';

import {referenceAssets} from '../lib/assets';
import {makeBackdrop, makeHeader, makeScreenPanel} from '../lib/primitives';
import {palette} from '../lib/theme';

export default makeScene2D(function* (view) {
  const panels = createRefArray<Layout>();

  view.add(makeBackdrop());
  view.add(makeHeader({
    kicker: 'Mission console',
    title: 'Operational frontend views',
    subtitle: 'Each route explains a different part of the mission picture instead of acting like cosmetic chrome.',
  }));

  const cards: Array<{src: string; label: string; accent: string; position: [number, number]}> = [
    {src: referenceAssets.command, label: 'Command', accent: palette.primary, position: [-470, -10]},
    {src: referenceAssets.track, label: 'Track', accent: palette.accent, position: [470, -10]},
    {src: referenceAssets.threat, label: 'Threat', accent: palette.critical, position: [-470, 350]},
    {src: referenceAssets.burnOps, label: 'Burn Ops', accent: palette.warning, position: [470, 350]},
  ];

  for (const card of cards) {
    view.add(makeScreenPanel({
      ref: panels,
      src: card.src,
      label: card.label,
      accent: card.accent,
      width: 700,
      position: card.position,
      opacity: 0,
    }));
  }

  yield* sequence(0.18, ...panels.map(panel => panel.opacity(1, 0.5)));
  yield* waitFor(2.4);
});
