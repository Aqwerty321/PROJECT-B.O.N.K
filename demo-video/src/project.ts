import {makeProject} from '@motion-canvas/core';

import s01ProblemIdea from './scenes/s01-problem-idea?scene';
import s02CascadeLoop from './scenes/s02-cascade-loop?scene';
import s03Implementation from './scenes/s03-implementation?scene';
import s04FrontendOverview from './scenes/s04-frontend-overview?scene';
import s05RealDataTrack from './scenes/s05-real-data-track?scene';
import s06ThreatDetection from './scenes/s06-threat-detection?scene';
import s07AutonomousResponse from './scenes/s07-autonomous-response?scene';
import s08VerificationClose from './scenes/s08-verification-close?scene';

export default makeProject({
  scenes: [
    s01ProblemIdea,
    s02CascadeLoop,
    s03Implementation,
    s04FrontendOverview,
    s05RealDataTrack,
    s06ThreatDetection,
    s07AutonomousResponse,
    s08VerificationClose,
  ],
});
