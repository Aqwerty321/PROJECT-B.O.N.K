# Optimization Plan — PROJECT-B.O.N.K / CASCADE

Generated 2026-03-21. Tracks all performance and code quality optimizations.

## A. Docker Build Optimization

| ID | Task | Priority | Est. Savings | Status |
|----|------|----------|-------------|--------|
| A1 | Fix `.dockerignore` — exclude `tle2025.txt` (2.8 GB), `build_julia/` (796 MB), `frontend/` (203 MB), `data.txt` (33 MB), `ps.txt` | HIGH | 30-120s/build | Pending |
| A2 | Replace `libboost-all-dev` (~500 MB) with `libboost-dev` (~10 MB) | HIGH | 30-60s/build | Pending |
| A3 | Add BuildKit `--mount=type=cache` for apt, `_deps`, and build directory | HIGH | 1-5min on incremental | Pending |
| A4 | Add `sccache` for C++ compiler caching | MEDIUM | 50-80% on incremental | Pending |
| A5 | Remove unused `curl`, `wget` from builder apt install | LOW | 5-10s | Pending |
| A6 | Switch runtime image from `ubuntu:22.04` (~77 MB) to `debian:bookworm-slim` (~52 MB) | MEDIUM | ~25 MB image size | Pending |

## B. C++ Backend Optimizations

| ID | Task | File(s) | Priority | Impact |
|----|------|---------|----------|--------|
| B1 | Parallelize propagation loop with `#pragma omp parallel for` | `simulation_engine.cpp:599` | HIGH | Major speedup on 10K+ objects |
| B2 | Add `-O3` + LTO build flags; `-march=native` for local builds only (not Docker) | `CMakeLists.txt` | HIGH | 10-30% speedup on numerical code |
| B3 | Pre-compute satellite orbit samples in MOID proxy gate, reuse across debris pairs | `simulation_engine.cpp:334-368` | HIGH | Eliminates redundant trig for each sat |
| B5 | Pre-build `unordered_set` for cooldown window lookup — O(S*B) → O(S+B) | `maneuver_common.cpp:642-654` | MEDIUM | Reduces linear scan per satellite |
| B6 | Cache `std::getenv()` result with `static const` in `resolve_moid_mode()` | `simulation_engine.cpp:459` | MEDIUM | Avoid syscall per tick |
| B7 | Fix string copies: `const auto&` for `store.id()` in hot loops | Multiple files | MEDIUM | Avoid heap alloc per satellite per tick |
| B8 | Move instead of copy burns in `execute_due_maneuvers()` | `maneuver_common.cpp:800-802` | MEDIUM | Avoid copying string-heavy structs |
| B9 | Deduplicate `env_double()` into single `env_util.hpp` | 5 files | MEDIUM | Code quality |
| B10 | Stats struct refactor (deferred) | `engine_runtime.hpp`, `simulation_engine.hpp` | LOW | Code quality |

## C. Frontend Optimizations

| ID | Task | File(s) | Priority | Impact |
|----|------|---------|----------|--------|
| C1 | Replace RAF loops with `useEffect`-driven draws | `GroundTrackMap.tsx`, `ManeuverGantt.tsx` | HIGH | Eliminates ~120fps wasted draw calls |
| C2 | Reuse Three.js debris/ring geometry via buffer updates | `EarthGlobe.tsx` | HIGH | Eliminates GPU alloc churn at 1Hz |
| C3 | Add `AbortController` to `useStatus`, `useBurns`, `useConjunctions`, `useTrajectory` | `useApi.ts` | HIGH | Prevents race conditions |
| C4 | Add `React.memo` to all child components | 7 component files | MEDIUM | Prevents unnecessary re-renders |
| C5 | Use refs for canvas data to avoid RAF loop restarts in `ConjunctionBullseye` | `ConjunctionBullseye.tsx` | MEDIUM | Prevents 1s RAF loop teardown |
| C6 | Dispose all Three.js GPU resources on unmount | `EarthGlobe.tsx` | MEDIUM | Prevents GPU memory leaks |
| C7 | Add DPR scaling to `GroundTrackMap` canvas | `GroundTrackMap.tsx` | MEDIUM | Fix blurry rendering on HiDPI |
| C8 | Extract inline style objects to module-level constants | Multiple files | LOW | Reduce GC pressure |
| C9 | Deduplicate `hexColor` utility | `FuelHeatmap.tsx`, `GroundTrackMap.tsx` | LOW | Code quality |
| C10 | Remove dead code: `react.svg`, unused types, duplicate keyframes | Multiple files | LOW | Code quality |

## Notes

- B4 (CDM computation frequency reduction) intentionally skipped — user wants every-tick computation.
- B10 deferred as lowest priority — large refactor for code quality only.
- B2 uses `-march=native` only for local builds; Docker uses generic `-O3` + LTO.
