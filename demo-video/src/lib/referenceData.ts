export const replayCommandLines = [
  'python3 scripts/replay_data_catalog.py \\',
  '  --data 3le_data.txt \\',
  '  --api-base http://localhost:8000 \\',
  '  --satellite-mode catalog \\',
  '  --operator-sats 10',
];

export const readinessLines = [
  '[demo-readiness]',
  '{',
  '  "verdict": "ready",',
  '  "successful_avoids": 2,',
  '  "fleet_collisions_avoided": 2,',
  '  "executed_burns": 2,',
  '  "dropped_burns": 0,',
  '  "fuel_consumed_kg": 0.1869',
  '}',
  '[confirmed-avoids]',
  'SAT-67060 -> DEB-SYNTH-97001',
  'SAT-67061 -> DEB-SYNTH-97101',
];

export const burnDecision = {
  id: 'AUTO-COLA-4-0',
  satellite: 'SAT-67060',
  debris: 'DEB-SYNTH-97001',
  missBefore: '8.0 m',
  missAfter: '134.4 m',
  deltaV: '0.50 m/s',
  uploadStation: 'GS-003',
  leadTime: '~9.8 min',
};

export const burnDecisionSecondary = {
  id: 'AUTO-COLA-4-1',
  satellite: 'SAT-67061',
  debris: 'DEB-SYNTH-97101',
  missBefore: '8.1 m',
  missAfter: '100.5 m',
  deltaV: '0.50 m/s',
};

export const verificationStats = [
  {label: 'Successful avoids', value: '2', accent: 'accent'},
  {label: 'Dropped burns', value: '0', accent: 'primary'},
  {label: 'Fuel used', value: '0.1869 kg', accent: 'warning'},
];
