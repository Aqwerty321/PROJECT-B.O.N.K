export const replayCommandLines = [
  'python3 scripts/replay_data_catalog.py \\',
  '  --data 3le_data.txt \\',
  '  --api-base http://localhost:8000 \\',
  '  --satellite-mode catalog \\',
  '  --operator-sats 10',
];

export {readinessLines, burnDecision, burnDecisionSecondary, verificationStats} from './generatedReferenceData';
